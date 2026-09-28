#include "../flipper_fobscan_app.h"
#include "flipper_link.h"
#include "flipper_link_proto.h"
#include <furi/core/memmgr.h>
#include <string.h>
#include <stdio.h>

/*
 * Runs in the link RX thread. It dispatches dashboard commands to the radio
 * and sends events and replies back to the host.
 *
 * The GUI scenes and remote commands share one CC1101. While a scene owns it
 * (app->gui_radio_active), remote radio commands receive "radio-busy".
 * Status and key queries remain available.
 */

/* Send a formatted line over each attached transport. */
void flipper_app_broadcast(FlipperApp* app, const char* line, size_t len) {
    if(app->usb_link)  flipper_link_send_line(app->usb_link, line, len);
    if(app->uart_link) flipper_link_send_line(app->uart_link, line, len);
}

static void reply(FlipperLink* origin, const char* line, size_t len) {
    if(origin) flipper_link_send_line(origin, line, len);
}

/*
 * GUI radio ownership handoff.
 *
 * GUI scenes run on the main/view-dispatcher thread; remote commands run on
 * the link RX thread. Both use the same CC1101, so they must not enter the
 * sub-GHz HAL concurrently or furi_check() can fail. The remote path checks
 * app->gui_radio_active under app->radio_mutex. The GUI must set that flag
 * under the same mutex before starting the radio; otherwise a remote command
 * could slip between radio startup and the flag update.
 *
 * acquire() stops an active remote scan and claims the radio. Once it returns,
 * remote commands cannot start a radio operation. release() clears the flag
 * when the scene exits.
 */
void flipper_app_gui_radio_acquire(FlipperApp* app) {
    furi_mutex_acquire(app->radio_mutex, FuriWaitForever);
    flipper_capture_stop(app->capture);
    flipper_capture_tx_wait_stopped(app->capture);
    if(app->remote_scanning) app->remote_scanning = false;
    app->gui_radio_active = true;
    furi_mutex_release(app->radio_mutex);
}

void flipper_app_gui_radio_release(FlipperApp* app) {
    furi_mutex_acquire(app->radio_mutex, FuriWaitForever);
    app->gui_radio_active = false;
    furi_mutex_release(app->radio_mutex);
}

static void ack(FlipperLink* origin, const FlipperCmd* cmd, bool ok, const char* err) {
    char buf[256];
    size_t len = flipper_proto_emit_cmdresp_id(
        buf, sizeof(buf), cmd->name, ok, err, cmd->has_id, cmd->id);
    reply(origin, buf, len);
}

static bool radio_free(FlipperApp* app) {
    return !app->gui_radio_active;
}

/* The command mutex serializes command threads, but the capture owner
   publishes TX state separately. A cancelled session stays busy until async
   TX stops and the CC1101 is back in Idle. */
static bool remote_radio_free(FlipperApp* app) {
    FlipperTxSession tx;
    bool ready = false;
    return radio_free(app) &&
           flipper_capture_tx_snapshot(app->capture, &tx, &ready) &&
           !flipper_tx_session_is_active(&tx) && ready;
}

static bool remote_radio_free_for(FlipperApp* app, FlipperLink* origin) {
    return remote_radio_free(app) &&
           (!app->remote_scanning ||
            app->remote_link_id == flipper_link_session_id(origin));
}

static bool freq_allowed(float mhz) {
    return (mhz >= 300.0f && mhz <= 348.0f) ||
           (mhz >= 387.0f && mhz <= 464.0f) ||
           (mhz >= 779.0f && mhz <= 928.0f);
}

static const char* tx_error(const FlipperCaptureEngine* capture) {
    FlipperTxSession tx;
    if(!flipper_capture_tx_status_copy(capture, &tx)) return "tx-error";
    if(flipper_tx_session_is_active(&tx)) return "radio-busy";
    switch(tx.state) {
    case FlipperTxStateBusy: return "radio-busy";
    case FlipperTxStateTimedOut: return "timeout";
    case FlipperTxStateRejected: return "tx-policy";
    case FlipperTxStateCancelled: return "cancelled";
    default: return "tx-failed";
    }
}

static void remote_scan_stop(FlipperApp* app);

void flipper_app_tx_done(void* ctx) {
    FlipperApp* app = (FlipperApp*)ctx;
    if(!app) return;
    char line[96];
    size_t len = flipper_proto_emit_replay_done(line, sizeof(line));
    flipper_app_broadcast(app, line, len);
    if(app->view_dispatcher)
        view_dispatcher_send_custom_event(
            app->view_dispatcher, FlipperEventReplayDone);
}

void flipper_app_gui_tx_done(void* ctx) {
    FlipperApp* app = (FlipperApp*)ctx;
    if(!app) return;
    flipper_capture_tx_set_done_cb(app->capture, flipper_app_tx_done, app);
    flipper_app_gui_radio_release(app);
}

void flipper_app_link_disconnected(void* app_ctx, FlipperLink* link) {
    FlipperApp* app = (FlipperApp*)app_ctx;
    if(!app || !app->capture) return;
    uint32_t owner = flipper_link_session_id(link);
    FlipperTxSession tx;
    if(flipper_capture_tx_status_copy(app->capture, &tx) &&
       tx.owner_id == owner) {
        flipper_capture_tx_cancel_ex(
            app->capture, tx.operation_id, tx.request_id, owner,
            FlipperTxCancelDisconnect);
        flipper_capture_tx_wait_stopped(app->capture);
    }
    if(app->remote_scanning && app->remote_link_id == owner)
        remote_scan_stop(app);
}

/* Configure or restart the capture engine for remote scanning. */
static bool remote_scan_start(FlipperApp* app) {
    app->capture->preset      = FlipperPresetOOK650;
    app->capture->squelch_dbm = app->remote_squelch_dbm;
    app->capture->on_edge     = NULL;    /* remote path polls in the tick */
    app->capture->on_edge_ctx = NULL;
    flipper_capture_start(app->capture);
    if(!app->capture->running) return false;
    app->remote_scanning = true;
    return true;
}

static void remote_scan_stop(FlipperApp* app) {
    flipper_capture_stop(app->capture);
    app->remote_scanning = false;
    app->remote_link_id = 0;
}

void flipper_app_handle_command(void* app_ctx, const FlipperCmd* cmd, FlipperLink* origin) {
    FlipperApp* app = (FlipperApp*)app_ctx;
    char buf[4096];
    size_t len;
    uint32_t uptime_s =
        (furi_get_tick() - app->boot_tick) / furi_ms_to_ticks(1000);

    /* Serialize command state changes: USB and UART RX threads both land here.
       TX itself is only queued here; the capture owner task performs every
       async-TX HAL call, so this mutex is never held for waveform duration.
       The gui_radio_active check remains consistent while a queue operation is
       committed. */
    furi_mutex_acquire(app->radio_mutex, FuriWaitForever);

    switch(cmd->kind) {
    case FlipperCmdHello:
        len = flipper_proto_emit_capabilities(buf, sizeof(buf), true, cmd->has_id, cmd->id);
        reply(origin, buf, len);
        break;

    case FlipperCmdStatus:
        FlipperTxSession tx_snapshot;
        bool have_tx = flipper_capture_tx_status_copy(
            app->capture, &tx_snapshot);
        uint32_t now_ms = furi_get_tick() * (1000u / furi_kernel_get_tick_frequency());
        len = flipper_proto_emit_status_ex(
            buf, sizeof(buf),
            app->capture->freq_mhz > 0 ? app->capture->freq_mhz : 433.92f,
            app->remote_scanning, 100, uptime_s, true, 0,
            app->remote_squelch_dbm,
            have_tx ? flipper_tx_state_name(tx_snapshot.state) : "idle",
            have_tx ? tx_snapshot.operation_id : 0,
            have_tx && flipper_tx_session_is_active(&tx_snapshot)
                ? flipper_tx_deadline_remaining(now_ms, tx_snapshot.deadline_ms) : 0,
            app->capture->edge_overflow, app->capture->peak_free_heap,
            memmgr_get_free_heap());
        reply(origin, buf, len);
        break;

    case FlipperCmdKeys:
        len = flipper_proto_emit_keys_empty(buf, sizeof(buf));
        reply(origin, buf, len);
        break;

    case FlipperCmdScanToggle:
        if(!remote_radio_free_for(app, origin)) { ack(origin, cmd, false, "radio-busy"); break; }
        if(app->remote_scanning) remote_scan_stop(app);
        else {
            app->remote_link_id = flipper_link_session_id(origin);
            if(!remote_scan_start(app)) {
                app->remote_link_id = 0;
                ack(origin, cmd, false, "radio-busy");
                break;
            }
        }
        ack(origin, cmd, true, NULL);
        break;

    case FlipperCmdSetFreq:
        if(!remote_radio_free_for(app, origin)) { ack(origin, cmd, false, "radio-busy"); break; }
        if(cmd->has_freq && freq_allowed(cmd->freq_mhz)) {
            app->capture->freq_mhz = cmd->freq_mhz;
            if(app->remote_scanning) {
                remote_scan_stop(app);
                if(!remote_scan_start(app)) {
                    ack(origin, cmd, false, "radio-busy");
                    break;
                }
            }
            char e[64];
            snprintf(e, sizeof(e), "{\"cmd\":\"setfreq\",\"ok\":true,\"freq\":%.2f}\n",
                     (double)cmd->freq_mhz);
            reply(origin, e, strlen(e));
        } else {
            ack(origin, cmd, false, "bad-freq");
        }
        break;

    case FlipperCmdCapture:
        if(!remote_radio_free_for(app, origin)) { ack(origin, cmd, false, "radio-busy"); break; }
        if(flipper_capture_flush(app->capture)) {
            app->remote_last       = app->capture->result;
            app->remote_last_valid = true;
            len = flipper_proto_emit_signal(buf, sizeof(buf),
                     &app->remote_last.decode, flipper_capture_rssi(app->capture));
            reply(origin, buf, len);
            ack(origin, cmd, true, NULL);
        } else {
            ack(origin, cmd, false, "no-signal");
        }
        break;

    case FlipperCmdReplay:
        if(!remote_radio_free_for(app, origin)) { ack(origin, cmd, false, "radio-busy"); break; }
        if(app->remote_last_valid) {
             /* TX owns the radio exclusively; scanning is deliberately left
                stopped during replay/jam and must be restarted explicitly. */
             if(app->remote_scanning) remote_scan_stop(app);
             bool ok = flipper_capture_tx_ex_owner(app->capture, &app->remote_last.pulses,
                     app->remote_last.decode.freq_mhz > 0
                         ? app->remote_last.decode.freq_mhz : app->capture->freq_mhz,
                       FlipperPresetOOK650, 2000, FlipperTxKindReplay,
                       cmd->has_id ? cmd->id : 0,
                       flipper_link_session_id(origin), 1, 0);
             if(ok) {
                 len = flipper_proto_emit_replay_playing(
                     buf, sizeof(buf), 1, 1, app->remote_last.decode.cnt);
                 flipper_app_broadcast(app, buf, len);
             }
             ack(origin, cmd, ok, ok ? NULL : tx_error(app->capture));
        } else {
            ack(origin, cmd, false, "nothing to replay");
        }
        break;

    case FlipperCmdReplayStop:
        if(!radio_free(app)) {
            ack(origin, cmd, false, "radio-busy");
            break;
        }
        bool stopped = flipper_capture_tx_cancel_ex(
            app->capture, 0, cmd->has_id ? cmd->id : 0,
            flipper_link_session_id(origin), FlipperTxCancelTick);
        if(stopped || app->remote_link_id == flipper_link_session_id(origin)) {
            flipper_capture_stop(app->capture);
            app->remote_scanning = false;
            if(app->remote_link_id == flipper_link_session_id(origin))
                app->remote_link_id = 0;
            ack(origin, cmd, true, NULL);
        } else {
            ack(origin, cmd, false, "not-owner");
        }
        break;

    case FlipperCmdJamStart:
        if(!remote_radio_free_for(app, origin)) { ack(origin, cmd, false, "radio-busy"); break; }
        else {
            float jf = cmd->has_freq ? cmd->freq_mhz : app->capture->freq_mhz;
            static FlipperPulseBuf jam;
            jam.len = 2; jam.te_us = 1000; jam.freq_mhz = jf;
            jam.durations[0] = 400000; /* 400 ms HIGH */
            jam.durations[1] = 100000; /* 100 ms LOW: exactly 80% duty */
            /* Leave remote scanning stopped even if enqueue fails; reporting
               a failed jam while silently restarting RX is unsafe. */
            if(app->remote_scanning) remote_scan_stop(app);
            flipper_capture_stop(app->capture);
             bool jam_ok = flipper_capture_tx_ex_owner(
                app->capture, &jam, jf, FlipperPresetOOK650, 600,
                 FlipperTxKindJam, cmd->has_id ? cmd->id : 0,
                 flipper_link_session_id(origin), 1, 0);
             len = flipper_proto_emit_jam_start(
                 buf, sizeof(buf), jam_ok, jam_ok, jf,
                 jam_ok ? NULL : tx_error(app->capture));
             reply(origin, buf, len);
        }
        break;

    case FlipperCmdJamStop:
        if(!radio_free(app)) {
            ack(origin, cmd, false, "not-owner");
            break;
        }
        {
            bool stopped = flipper_capture_tx_cancel_ex(
                app->capture, 0, cmd->has_id ? cmd->id : 0,
                flipper_link_session_id(origin), FlipperTxCancelTick);
            if(stopped) {
                flipper_capture_stop(app->capture);
                ack(origin, cmd, true, NULL);
            } else {
                ack(origin, cmd, false, "not-owner");
            }
        }
        break;

    case FlipperCmdSquelch:
        if(!remote_radio_free_for(app, origin)) {
            ack(origin, cmd, false, "radio-busy");
        } else if(cmd->has_val && cmd->val < 0 && cmd->val > -120) {
            app->remote_squelch_dbm  = cmd->val;
            app->capture->squelch_dbm = cmd->val;
        ack(origin, cmd, true, NULL);
        } else {
            ack(origin, cmd, false, "bad-value");
        }
        break;

    case FlipperCmdSave:
        if(!app->remote_last_valid) {
            ack(origin, cmd, false, "nothing captured");
        } else {
            char saved[FLIPPER_LIB_NAME_MAX];
            bool saved_ok = flipper_lib_save(
                app->storage, &app->remote_last, app->adv.preset,
                app->remote_last.decode_ok, app->adv.lib_evict_oldest, saved);
            len = flipper_proto_emit_library_event(
                buf, sizeof(buf), "save", saved_ok, saved);
            reply(origin, buf, len);
            ack(origin, cmd, saved_ok, saved_ok ? NULL :
                flipper_lib_error_name(flipper_lib_last_error()));
        }
        break;

    case FlipperCmdLibraryList: {
        bool decoded = cmd->has_decoded ? cmd->decoded : true;
        int total = flipper_lib_count(app->storage, decoded);
        int offset = cmd->offset < 0 ? 0 : cmd->offset;
        if(offset > FLIPPER_LIB_LIST_MAX) offset = FLIPPER_LIB_LIST_MAX;
        int limit = cmd->limit > 0 && cmd->limit <= FLIPPER_LIB_LIST_MAX
                        ? cmd->limit : FLIPPER_LIB_LIST_MAX;
        FlipperLibEntry entries[FLIPPER_LIB_LIST_MAX];
        int count = flipper_lib_list_page(
            app->storage, decoded, entries, limit, offset);
        len = flipper_proto_emit_library_list(
            buf, sizeof(buf), decoded, offset, total, entries, count);
        reply(origin, buf, len);
        break;
    }

    case FlipperCmdLibraryGet: {
        bool decoded = cmd->has_decoded ? cmd->decoded : true;
        FlipperCaptureResult cap;
        FlipperPreset preset = FlipperPresetOOK650;
        if(!flipper_lib_valid_name(cmd->entry)) {
            ack(origin, cmd, false, "invalid-library-entry");
            break;
        }
        if(!flipper_lib_load(app->storage, decoded, cmd->entry, &cap, &preset)) {
            ack(origin, cmd, false,
                flipper_lib_error_name(flipper_lib_last_error()));
            break;
        }
        len = flipper_proto_emit_library_detail(
            buf, sizeof(buf), decoded, cmd->entry, &cap, preset);
        reply(origin, buf, len);
        break;
    }

    case FlipperCmdLibraryReplay: {
        bool decoded = cmd->has_decoded ? cmd->decoded : true;
        FlipperCaptureResult cap;
        FlipperPreset preset = FlipperPresetOOK650;
        if(!remote_radio_free_for(app, origin)) { ack(origin, cmd, false, "radio-busy"); break; }
        if(!flipper_lib_valid_name(cmd->entry)) {
            ack(origin, cmd, false, "invalid-library-entry");
            break;
        }
        if(!flipper_lib_load(app->storage, decoded, cmd->entry, &cap, &preset)) {
            ack(origin, cmd, false, "library-entry-not-found");
            break;
        }
        if(app->remote_scanning) remote_scan_stop(app);
         bool replay_ok = flipper_capture_tx_ex_owner(
            app->capture, &cap.pulses, cap.pulses.freq_mhz, preset, 3000,
             FlipperTxKindReplay, cmd->has_id ? cmd->id : 0,
             flipper_link_session_id(origin), 1, 0);
         if(replay_ok) {
             len = flipper_proto_emit_replay_playing(
                 buf, sizeof(buf), 1, 1, cap.decode.cnt);
             flipper_app_broadcast(app, buf, len);
         }
         ack(origin, cmd, replay_ok, replay_ok ? NULL : tx_error(app->capture));
        break;
    }

    case FlipperCmdLibraryDelete: {
        bool decoded = cmd->has_decoded ? cmd->decoded : true;
        if(!flipper_lib_valid_name(cmd->entry)) {
            ack(origin, cmd, false, "invalid-library-entry");
            break;
        }
        bool deleted = flipper_lib_delete(app->storage, decoded, cmd->entry);
        len = flipper_proto_emit_library_event(
            buf, sizeof(buf), "delete", deleted, cmd->entry);
        reply(origin, buf, len);
        ack(origin, cmd, deleted, deleted ? NULL :
            flipper_lib_error_name(flipper_lib_last_error()));
        break;
    }

    case FlipperCmdLibraryExport: {
        bool decoded = cmd->has_decoded ? cmd->decoded : true;
        if(!flipper_lib_valid_name(cmd->entry)) {
            ack(origin, cmd, false, "invalid-library-entry");
            break;
        }
        bool exported = flipper_lib_export_subghz(
            app->storage, decoded, cmd->entry);
        len = flipper_proto_emit_library_event(
            buf, sizeof(buf), "export", exported, cmd->entry);
        reply(origin, buf, len);
        ack(origin, cmd, exported, exported ? NULL :
            flipper_lib_error_name(flipper_lib_last_error()));
        break;
    }

    case FlipperCmdUtility:
        /* Guided utilities own scene state and are intentionally not driven
           from the link thread.  Do not claim a remote implementation. */
        ack(origin, cmd, false, "device-only utility; run on Flipper");
        break;

    case FlipperCmdFbkArm:
    case FlipperCmdFbkDisarm:
    case FlipperCmdFbkReplay:
        /* RollBack sequence capture is driven from the on-device FOBback scene;
           remote sequencing is a documented bring-up seam. */
        ack(origin, cmd, false, "run FOBback on-device");
        break;

    case FlipperCmdUnsupported:
        ack(origin, cmd, false, "unsupported on Flipper (single CC1101)");
        break;

    case FlipperCmdUnknown:
    default:
        ack(origin, cmd, false, "unknown command");
        break;
    }

    furi_mutex_release(app->radio_mutex);
}
