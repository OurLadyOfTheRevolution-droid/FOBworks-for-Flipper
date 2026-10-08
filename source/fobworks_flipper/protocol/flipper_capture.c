#include "flipper_capture.h"
#include <furi/core/memmgr.h>
#include <lib/subghz/devices/cc1101_configs.h>
#include <stdlib.h>
#include <string.h>

/* Event-flag bits for the ISR → notify-worker hand-off. */
#define FLIPPER_CAP_NOTIFY_EDGE (1UL << 0)
#define FLIPPER_CAP_NOTIFY_STOP (1UL << 1)
#define FLIPPER_CAP_NOTIFY_TX   (1UL << 2)

static int32_t flipper_capture_notify_worker(void* ctx);

static bool flipper_capture_freq_allowed(float mhz) {
    return (mhz >= 300.0f && mhz <= 348.0f) ||
           (mhz >= 387.0f && mhz <= 464.0f) ||
           (mhz >= 779.0f && mhz <= 928.0f);
}

static bool flipper_capture_add_u32(uint32_t* value, uint32_t add) {
    if(UINT32_MAX - *value < add) return false;
    *value += add;
    return true;
}

static void flipper_capture_tx_release_seq(FlipperCaptureEngine* e) {
    if(!e) return;
    if(e->tx_seq) {
        free(e->tx_seq);
        e->tx_seq = NULL;
    }
    e->tx_seq_n = 0;
}

static uint32_t flipper_capture_now_ms(void) {
    return furi_get_tick() * (1000u / furi_kernel_get_tick_frequency());
}

static bool flipper_capture_notify_start(FlipperCaptureEngine* e) {
    if(!e) return false;
    FuriThread* reap = NULL;
    furi_mutex_acquire(e->tx_mutex, FuriWaitForever);
    if(e->notify_thread && !e->notify_run) {
        reap = e->notify_thread;
        e->notify_thread = NULL;
    }
    bool need = !reap && !e->notify_thread &&
                (e->on_edge || e->tx_work != FlipperTxWorkIdle);
    if(need) {
        /* I publish the handle and running state while holding the tx_work mutex. Otherwise, a concurrent enqueue could see a worker that is already exiting and leave its request without a worker to service it. */
        e->notify_run = true;
        e->notify_thread = furi_thread_alloc_ex(
            "FlipperCapNotify", 2048, flipper_capture_notify_worker, e);
        if(!e->notify_thread) {
            e->notify_run = false;
            if(e->tx_work != FlipperTxWorkIdle) {
                e->tx_session.state = FlipperTxStateError;
                e->tx_session.cancel_reason = FlipperTxCancelError;
                e->tx_work = FlipperTxWorkIdle;
                e->tx_running = false;
                flipper_capture_tx_release_seq(e);
            }
        }
    }
    furi_mutex_release(e->tx_mutex);
    if(reap) {
        furi_thread_join(reap);
        furi_thread_free(reap);
        /* I restart after reaping the old worker. */
        return flipper_capture_notify_start(e);
    } else if(need && e->notify_thread) {
        furi_thread_start(e->notify_thread);
    }
    return !need || e->notify_thread != NULL;
}

static LevelDuration flipper_tx_cb(void* ctx) {
    FlipperTxWaveState* s = (FlipperTxWaveState*)ctx;
    if(s->pos >= s->count) return level_duration_reset();
    LevelDuration ld = level_duration_make(s->level, s->durations[s->pos]);
    s->pos++;
    s->level = !s->level;
    return ld;
}

/* * I acquire and release the SubGHz device through the device API. In firmware * 1.4.x, the receive path I use follows this order: * *   subghz_devices_init() *   device = subghz_devices_get_by_name(SUBGHZ_DEVICE_CC1101_INT_NAME) *   subghz_devices_begin(device)          ← acquires HW lock, sets known state *     subghz_devices_reset(device) *     subghz_devices_idle(device) *     subghz_devices_load_preset(device, FuriHalSubGhzPresetOok650Async, NULL) *     subghz_devices_set_frequency(device, hz) *     subghz_devices_start_async_rx(device, cb, ctx) *     ... *     subghz_devices_stop_async_rx(device) *     subghz_devices_idle(device) *     subghz_devices_sleep(device) *   subghz_devices_end(device)            ← releases HW lock *   subghz_devices_deinit() * * I do not replace these calls with raw furi_hal_subghz_*() calls. Without * subghz_devices_begin(), the system still owns the radio and the HAL state * checks fail. */

/* ── Preset enum mapping ────────────────────────────────────────────────── */
static FuriHalSubGhzPreset to_hal_preset(FlipperPreset p) {
    switch(p) {
    case FlipperPresetOOK270:     return FuriHalSubGhzPresetOok270Async;
    case FlipperPreset2FSKDev238: return FuriHalSubGhzPreset2FSKDev238Async;
    case FlipperPreset2FSKDev476: return FuriHalSubGhzPreset2FSKDev476Async;
    case FlipperPresetOOK650:
    default:                      return FuriHalSubGhzPresetOok650Async;
    }
}

/* ── Alloc / free ────────────────────────────────────────────────────────── */
FlipperCaptureEngine* flipper_capture_alloc(void) {
    FlipperCaptureEngine* e = malloc(sizeof(FlipperCaptureEngine));
    if(!e) return NULL;
    memset(e, 0, sizeof(*e));

    e->edge_mutex  = furi_mutex_alloc(FuriMutexTypeNormal);
    e->squelch_dbm = -90.0f;
    e->preset      = FlipperPresetOOK650;
    e->freq_mhz    = 433.92f;
    e->peak_free_heap = memmgr_get_free_heap();

    subghz_devices_init();
    e->device = subghz_devices_get_by_name(SUBGHZ_DEVICE_CC1101_INT_NAME);
    if(!e->device) {
        furi_mutex_free(e->edge_mutex);
        free(e);
        return NULL;
    }
    subghz_devices_begin(e->device);

    /* ISR → thread hand-off for on_edge. */
    e->notify_flag   = furi_event_flag_alloc();
    /* I start the callback worker only when needed. Remote capture and RSSI reads do not use it, so they need not reserve its 2 KiB stack. */
    e->notify_run = false;
    e->notify_thread = NULL;
    e->tx_mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    flipper_tx_session_init(&e->tx_session);

    return e;
}

void flipper_capture_free(FlipperCaptureEngine* e) {
    if(!e) return;
    flipper_capture_stop(e);
    flipper_capture_tx_wait_stopped(e);
    flipper_capture_tx_release_seq(e);
    /* I stop the worker before freeing the device and mutex it can access. */
    if(e->notify_thread) {
        e->notify_run = false;
        furi_event_flag_set(e->notify_flag, FLIPPER_CAP_NOTIFY_STOP);
        furi_thread_join(e->notify_thread);
        furi_thread_free(e->notify_thread);
        e->notify_thread = NULL;
    }
    if(e->notify_flag) {
        furi_event_flag_free(e->notify_flag);
        e->notify_flag = NULL;
    }
    if(e->device) {
        subghz_devices_end(e->device);
    }
    subghz_devices_deinit();
    furi_mutex_free(e->tx_mutex);
    furi_mutex_free(e->edge_mutex);
    free(e);
}

/* ── Async RX edge callback (ISR context — keep minimal) ─────────────────── */
void flipper_capture_rx_cb(bool level, uint32_t duration_us, void* ctx) {
    FlipperCaptureEngine* e = (FlipperCaptureEngine*)ctx;
    if(!e->running) return;
    if(duration_us < FLIPPER_MIN_PULSE_US) return;

    int next = (e->edge_head + 1) % FLIPPER_CAP_EDGE_MAX;
    if(next == e->edge_tail) {
        e->edge_overflow++;
        return;
    }

    e->edges[e->edge_head]  = duration_us;
    e->levels[e->edge_head] = level;
    e->edge_head             = next;

    /* I queue one notification per burst. flipper_capture_flush() clears edge_pending after draining the ring, allowing the next burst to signal. Without this guard, I found one 100-edge press could fill the ViewDispatcher queue before it processes the first event. */
    if(e->on_edge && !e->edge_pending) {
        e->edge_pending = true;
        /* I notify the worker instead of calling on_edge in interrupt context. on_edge calls view_dispatcher_send_custom_event(), which waits forever on a queue and fails its furi_check() in an ISR. The event flag API is safe to call here. */
        if(e->notify_flag) furi_event_flag_set(e->notify_flag, FLIPPER_CAP_NOTIFY_EDGE);
    }
}

/* A single on-demand worker handles edge notifications and all TX HAL calls. Callers can inspect or stop a transmission without waiting for the waveform. */
static int32_t flipper_capture_notify_worker(void* ctx) {
    FlipperCaptureEngine* e = (FlipperCaptureEngine*)ctx;
    while(e->notify_run) {
        uint32_t f = furi_event_flag_wait(
            e->notify_flag,
            FLIPPER_CAP_NOTIFY_EDGE | FLIPPER_CAP_NOTIFY_STOP |
                FLIPPER_CAP_NOTIFY_TX,
            FuriFlagWaitAny | FuriFlagNoClear, furi_ms_to_ticks(2));
        if(f & FuriFlagError) f = 0;
        furi_event_flag_clear(
            e->notify_flag, FLIPPER_CAP_NOTIFY_EDGE | FLIPPER_CAP_NOTIFY_TX);
        if(f & FLIPPER_CAP_NOTIFY_STOP) break;

        void (*done)(void*) = NULL;
        void* done_ctx = NULL;
        furi_mutex_acquire(e->tx_mutex, FuriWaitForever);
        uint32_t now = flipper_capture_now_ms();
        if(e->tx_work != FlipperTxWorkIdle) {
            bool finish = false;
            if(!flipper_tx_session_is_active(&e->tx_session)) {
                if(e->tx_hal_active) {
                    subghz_devices_stop_async_tx(e->device);
                    subghz_devices_idle(e->device);
                    e->tx_hal_active = false;
                }
                if(e->running) {
                    e->running = false;
                    subghz_devices_stop_async_rx(e->device);
                    subghz_devices_idle(e->device);
                }
                e->tx_running = false;
                e->tx_work = FlipperTxWorkIdle;
                finish = true;
            } else if(flipper_tx_session_tick(&e->tx_session, now)) {
                if(e->tx_hal_active) {
                    subghz_devices_stop_async_tx(e->device);
                    subghz_devices_idle(e->device);
                    e->tx_hal_active = false;
                }
                if(e->running) {
                    e->running = false;
                    subghz_devices_stop_async_rx(e->device);
                    subghz_devices_idle(e->device);
                }
                e->tx_running = false;
                e->tx_work = FlipperTxWorkIdle;
                finish = true;
            } else if(e->tx_hal_active) {
                if(subghz_devices_is_async_complete_tx(e->device)) {
                    subghz_devices_stop_async_tx(e->device);
                    subghz_devices_idle(e->device);
                    e->tx_hal_active = false;
                    if(!flipper_tx_session_frame(&e->tx_session)) {
                        e->tx_session.state = FlipperTxStateError;
                        e->tx_session.cancel_reason = FlipperTxCancelError;
                        e->tx_work = FlipperTxWorkIdle;
                        e->tx_running = false;
                        finish = true;
                    } else if(++e->tx_index >= e->tx_count) {
                        (void)flipper_tx_session_finish(&e->tx_session, now);
                        e->tx_work = FlipperTxWorkIdle;
                        e->tx_running = false;
                        finish = true;
                    } else {
                        e->tx_next_ms = now + e->tx_delay_ms;
                        e->tx_work = e->tx_delay_ms
                            ? FlipperTxWorkDelay : FlipperTxWorkPending;
                    }
                }
            } else if(e->tx_work == FlipperTxWorkDelay) {
                if(flipper_tx_deadline_reached(now, e->tx_next_ms))
                    e->tx_work = FlipperTxWorkPending;
            }
            if(e->tx_work == FlipperTxWorkPending &&
               flipper_tx_session_is_active(&e->tx_session)) {
                const FlipperPulseBuf* p = e->tx_buf;
                if(e->tx_seq && e->tx_index >= 0 && e->tx_index < e->tx_seq_n)
                    p = &e->tx_seq[e->tx_index];
                e->tx_wave.durations = p ? p->durations : NULL;
                e->tx_wave.count = p ? p->len : 0;
                e->tx_wave.pos = 0;
                e->tx_wave.level = true;
                if(e->running) {
                    e->running = false;
                    subghz_devices_stop_async_rx(e->device);
                    subghz_devices_idle(e->device);
                }
                subghz_devices_reset(e->device);
                subghz_devices_idle(e->device);
                subghz_devices_load_preset(
                    e->device, to_hal_preset(e->tx_preset), NULL);
                subghz_devices_set_frequency(
                    e->device, (uint32_t)(e->tx_freq_mhz * 1e6f));
                if(!p || p->len <= 0 ||
                   !subghz_devices_start_async_tx(
                       e->device, (void*)flipper_tx_cb, &e->tx_wave)) {
                    subghz_devices_idle(e->device);
                    e->tx_session.state = FlipperTxStateError;
                    e->tx_session.cancel_reason = FlipperTxCancelError;
                    e->tx_work = FlipperTxWorkIdle;
                    e->tx_running = false;
                    finish = true;
                } else {
                    e->tx_hal_active = true;
                    e->tx_work = FlipperTxWorkActive;
                }
            }
            if(finish) {
                flipper_capture_tx_release_seq(e);
                done = e->tx_done_cb;
                done_ctx = e->tx_done_ctx;
                e->tx_callback_active = done != NULL;
            }
        }
        bool idle = e->tx_work == FlipperTxWorkIdle && !e->on_edge;
        if(idle) e->notify_run = false;
        furi_mutex_release(e->tx_mutex);
        /* I wait for a quiet burst before notifying the scene, so it flushes the complete packet rather than only its first edges. */
        if(e->running && e->on_edge && e->edge_pending) {
            int head = e->edge_head;
            if(head != e->edge_mark) {
                e->edge_mark = head;
                e->edge_quiet = 0;
            } else if(e->edge_quiet < 15) {
                e->edge_quiet++;
            } else if(e->edge_quiet == 15) {
                e->edge_quiet = 16;
                e->on_edge(e->on_edge_ctx);
            }
        }
        if(done) {
            done(done_ctx);
            furi_mutex_acquire(e->tx_mutex, FuriWaitForever);
            e->tx_callback_active = false;
            furi_mutex_release(e->tx_mutex);
        }
        if(idle) break;
    }
    e->notify_run = false;
    return 0;
}

/* ── Configure CC1101 and start async RX ─────────────────────────────────── */
void flipper_capture_start(FlipperCaptureEngine* e) {
    if(!e || !e->device || e->running ||
       !flipper_capture_freq_allowed(e->freq_mhz))
        return;

    if(e->on_edge && !flipper_capture_notify_start(e)) return;
    furi_mutex_acquire(e->tx_mutex, FuriWaitForever);
    if(e->tx_work != FlipperTxWorkIdle) {
        furi_mutex_release(e->tx_mutex);
        return;
    }

    subghz_devices_reset(e->device);
    subghz_devices_idle(e->device);
    subghz_devices_load_preset(e->device, to_hal_preset(e->preset), NULL);
    subghz_devices_set_frequency(e->device, (uint32_t)(e->freq_mhz * 1e6f));
    /* I leave the device idle here. start_async_rx() requires SubGhzStateIdle; I found calling set_rx() first changes the state to SubGhzStateRx, which fails the Unleashed HAL check. start_async_rx() performs the transition itself. */

    e->edge_head    = 0;
    e->edge_tail    = 0;
    e->running      = true;
    e->capture_done = false;
    e->edge_pending = false;
    e->edge_mark    = -1;
    e->edge_quiet   = 0;
    e->rx_hold_until = furi_get_tick() + furi_ms_to_ticks(1000);
    e->start_ms     = furi_get_tick() * (1000u / furi_kernel_get_tick_frequency());
    uint32_t free_heap = memmgr_get_free_heap();
    if(free_heap < e->peak_free_heap) e->peak_free_heap = free_heap;

    subghz_devices_start_async_rx(e->device, (void*)flipper_capture_rx_cb, e);
    furi_mutex_release(e->tx_mutex);
}

/* ── Stop async RX ───────────────────────────────────────────────────────── */
void flipper_capture_stop(FlipperCaptureEngine* e) {
    if(!e || !e->device) return;
    furi_mutex_acquire(e->tx_mutex, FuriWaitForever);
    bool tx_busy = e->tx_work != FlipperTxWorkIdle;
    furi_mutex_release(e->tx_mutex);
    if(tx_busy) {
        (void)flipper_capture_tx_cancel_ex(
            e, 0, 0, 0, FlipperTxCancelSceneExit);
        return;
    }
    if(e->running) {
        furi_mutex_acquire(e->tx_mutex, FuriWaitForever);
        if(e->tx_work != FlipperTxWorkIdle) {
            furi_mutex_release(e->tx_mutex);
            return;
        }
        e->running = false;
        subghz_devices_stop_async_rx(e->device);
        subghz_devices_idle(e->device);
        furi_mutex_release(e->tx_mutex);
    }
    /* I keep the device idle between captures in the same session. In several Unleashed HAL versions, sleep() changes the state to SubGhzStateSleep, which the next idle() call rejects. subghz_devices_end() handles the sleep/release step when flipper_capture_free() gives up the device. */
}

/* ── Flush edge ring → FlipperPulseBuf + decode ──────────────────────────── */
bool flipper_capture_flush(FlipperCaptureEngine* e) {
    if(!e) return false;

    furi_mutex_acquire(e->edge_mutex, FuriWaitForever);

    int head  = e->edge_head;
    int tail  = e->edge_tail;
    int count = (head >= tail) ? (head - tail) : (FLIPPER_CAP_EDGE_MAX - tail + head);

    /* I discard edges collected while the radio settles, then keep listening. */
    if(furi_get_tick() < e->rx_hold_until) {
        e->edge_tail = head;
        e->edge_pending = false;
        e->edge_quiet = 0;
        e->edge_mark = -1;
        furi_mutex_release(e->edge_mutex);
        return false;
    }

    if(count < 16 || (e->edge_pending && e->edge_quiet < 15)) {
        if(count < 16 && e->edge_quiet >= 15) {
            e->edge_pending = false;
            e->edge_quiet = 0;
        }
        furi_mutex_release(e->edge_mutex);
        return false;
    }

    FlipperPulseBuf* pb = &e->result.pulses;
    pb->len      = 0;
    pb->freq_mhz = e->freq_mhz;

    int idx = tail;
    while(idx != head && pb->len < FLIPPER_PULSE_MAX) {
        pb->durations[pb->len++] = e->edges[idx];
        idx = (idx + 1) % FLIPPER_CAP_EDGE_MAX;
    }
    e->edge_tail = idx;

    furi_mutex_release(e->edge_mutex);

    /* The ring is drained; I clear the burst flag so a later ISR edge can queue the next notification. I do this after releasing the mutex to avoid losing an edge that arrives as the buffer is drained. */
    e->edge_pending = false;
    e->edge_quiet = 0;
    e->edge_mark = -1;

    if(pb->len < 16) return false;

    pb->te_us           = flipper_estimate_te(pb->durations, pb->len);
    e->result.decode_ok = flipper_decode_ex(pb, &e->result.decode, e->force_proto);
    e->result.timestamp_ms =
        furi_get_tick() * (1000u / furi_kernel_get_tick_frequency());

    return true;
}

/* TX is queued and advanced only by flipper_capture_notify_worker. */

bool flipper_capture_tx_ex_owner(
    FlipperCaptureEngine* e,
    const FlipperPulseBuf* buf,
    float freq_mhz,
    FlipperPreset preset,
    uint32_t timeout_ms,
    FlipperTxKind kind,
    uint32_t requested_id,
    uint32_t owner_id,
    uint32_t frame_count,
    uint32_t delay_ms) {
    if(!e || !buf || buf->len <= 0 || buf->len > FLIPPER_PULSE_MAX ||
       !flipper_capture_freq_allowed(freq_mhz))
        return false;
    if(timeout_ms == 0) timeout_ms = 3000;

    uint32_t on_us = 0;
    uint32_t total_us = 0;
    for(int i = 0; i < buf->len; i++) {
        if(!flipper_capture_add_u32(&total_us, buf->durations[i]) ||
           ((i & 1) == 0 &&
            !flipper_capture_add_u32(&on_us, buf->durations[i])))
            return false;
    }
    uint32_t now = flipper_capture_now_ms();
    uint64_t on_total = (uint64_t)on_us * frame_count;
    uint64_t all_total = (uint64_t)total_us * frame_count;
    if(on_total > UINT32_MAX || all_total > UINT32_MAX) return false;
    furi_mutex_acquire(e->tx_mutex, FuriWaitForever);
    if(frame_count == 0 || frame_count > 8 ||
       flipper_tx_session_begin(
           &e->tx_session, now, kind, owner_id, requested_id, timeout_ms,
           frame_count, (uint32_t)on_total, (uint32_t)all_total) !=
           FlipperTxBeginOk)
    {
        furi_mutex_release(e->tx_mutex);
        return false;
    }
    e->tx_owned = *buf;
    e->tx_buf = &e->tx_owned;
    flipper_capture_tx_release_seq(e);
    e->tx_count = (int)frame_count;
    e->tx_index = 0;
    e->tx_freq_mhz = freq_mhz;
    e->tx_preset = preset;
    e->tx_delay_ms = delay_ms;
    e->tx_work = FlipperTxWorkPending;
    e->tx_running = true;
    furi_mutex_release(e->tx_mutex);
    if(!flipper_capture_notify_start(e)) return false;
    furi_event_flag_set(e->notify_flag, FLIPPER_CAP_NOTIFY_TX);
    return true;
}

bool flipper_capture_tx_ex(
    FlipperCaptureEngine* e,
    const FlipperPulseBuf* buf,
    float freq_mhz,
    FlipperPreset preset,
    uint32_t timeout_ms,
    FlipperTxKind kind,
    uint32_t requested_id) {
    return flipper_capture_tx_ex_owner(
        e, buf, freq_mhz, preset, timeout_ms, kind, requested_id,
        FLIPPER_TX_OWNER_GUI, 1, 0);
}

bool flipper_capture_tx(
    FlipperCaptureEngine* e,
    const FlipperPulseBuf* buf,
    float freq_mhz,
    FlipperPreset preset,
    uint32_t timeout_ms) {
    return flipper_capture_tx_ex(
        e, buf, freq_mhz, preset, timeout_ms, FlipperTxKindDirect, 0);
}

bool flipper_capture_tx_repeat(
    FlipperCaptureEngine* e,
    const FlipperPulseBuf* buf,
    uint32_t repeats,
    float freq_mhz,
    FlipperPreset preset,
    uint32_t delay_ms) {
    uint64_t duration = (uint64_t)repeats * 3000u;
    if(repeats > 1) duration += (uint64_t)(repeats - 1) * delay_ms;
    if(repeats == 0 || repeats > 8 || duration > UINT32_MAX) return false;
    return flipper_capture_tx_ex_owner(
        e, buf, freq_mhz, preset, (uint32_t)duration,
        FlipperTxKindSequence, 0, FLIPPER_TX_OWNER_GUI, repeats, delay_ms);
}

bool flipper_capture_tx_cancel(
    FlipperCaptureEngine* e,
    uint32_t operation_id,
    FlipperTxCancelReason reason) {
    if(!e) return false;
    return flipper_capture_tx_cancel_ex(e, 0, operation_id, 0, reason);
}

bool flipper_capture_tx_cancel_ex(
    FlipperCaptureEngine* e,
    uint32_t generation,
    uint32_t request_id,
    uint32_t owner_id,
    FlipperTxCancelReason reason) {
    if(!e) return false;
    furi_mutex_acquire(e->tx_mutex, FuriWaitForever);
    bool matched = flipper_tx_session_cancel_ex(
        &e->tx_session, generation, request_id, owner_id,
        reason, flipper_capture_now_ms());
    furi_mutex_release(e->tx_mutex);
    if(matched && e->notify_flag)
        furi_event_flag_set(e->notify_flag, FLIPPER_CAP_NOTIFY_TX);
    return matched;
}

bool flipper_capture_tx_status_copy(
    const FlipperCaptureEngine* e, FlipperTxSession* out) {
    if(!e || !out) return false;
    furi_mutex_acquire(e->tx_mutex, FuriWaitForever);
    *out = e->tx_session;
    furi_mutex_release(e->tx_mutex);
    return true;
}

bool flipper_capture_tx_snapshot(
    const FlipperCaptureEngine* e, FlipperTxSession* out, bool* ready) {
    if(!e || !out || !ready) return false;
    furi_mutex_acquire(e->tx_mutex, FuriWaitForever);
    *out = e->tx_session;
    *ready = e->tx_work == FlipperTxWorkIdle;
    furi_mutex_release(e->tx_mutex);
    return true;
}

void flipper_capture_tx_set_done_cb(
    FlipperCaptureEngine* e, void (*cb)(void*), void* ctx) {
    if(!e) return;
    furi_mutex_acquire(e->tx_mutex, FuriWaitForever);
    e->tx_done_cb = cb;
    e->tx_done_ctx = ctx;
    furi_mutex_release(e->tx_mutex);
}

void flipper_capture_tx_wait_stopped(FlipperCaptureEngine* e) {
    if(!e) return;
    while(true) {
        furi_mutex_acquire(e->tx_mutex, FuriWaitForever);
        bool active = e->tx_work != FlipperTxWorkIdle || e->tx_callback_active;
        furi_mutex_release(e->tx_mutex);
        if(!active) break;
        furi_delay_ms(1);
    }
    if(e->notify_thread && !e->notify_run) {
        furi_thread_join(e->notify_thread);
        furi_thread_free(e->notify_thread);
        e->notify_thread = NULL;
    }
}

/* ── TX sequence (FOBback RollBack replay) ───────────────────────────────── */
bool flipper_capture_tx_sequence(
    FlipperCaptureEngine* e,
    const FlipperCaptureResult* caps,
    int count,
    float freq_mhz,
    FlipperPreset preset,
    uint32_t delay_ms)
{
    if(!e || !caps || count <= 0 || count > 8 ||
       !flipper_capture_freq_allowed(freq_mhz))
        return false;
    uint32_t total_us = 0;
    uint32_t on_us = 0;
    for(int i = 0; i < count; i++) {
        if(caps[i].pulses.len <= 0 || caps[i].pulses.len > FLIPPER_PULSE_MAX)
            return false;
        for(int j = 0; j < caps[i].pulses.len; j++) {
            if(!flipper_capture_add_u32(
                   &total_us, caps[i].pulses.durations[j]) ||
               ((j & 1) == 0 &&
                !flipper_capture_add_u32(
                    &on_us, caps[i].pulses.durations[j])))
                return false;
        }
    }
    uint64_t duration64 = (uint64_t)count * 3000u;
    if(count > 1) duration64 += (uint64_t)(count - 1) * delay_ms;
    if(duration64 > UINT32_MAX) return false;
    uint32_t duration = (uint32_t)duration64;
    uint32_t now = flipper_capture_now_ms();
    FlipperPulseBuf* copy = malloc(sizeof(FlipperPulseBuf) * (size_t)count);
    if(!copy) return false;
    for(int i = 0; i < count; i++) copy[i] = caps[i].pulses;

    furi_mutex_acquire(e->tx_mutex, FuriWaitForever);
    if(flipper_tx_session_begin(
           &e->tx_session, now, FlipperTxKindSequence,
           FLIPPER_TX_OWNER_GUI, 0, duration,
           (uint32_t)count, on_us, total_us) != FlipperTxBeginOk)
    {
        furi_mutex_release(e->tx_mutex);
        free(copy);
        return false;
    }
    flipper_capture_tx_release_seq(e);
    e->tx_buf = NULL;
    e->tx_seq = copy;
    e->tx_seq_n = count;
    e->tx_count = count;
    e->tx_index = 0;
    e->tx_freq_mhz = freq_mhz;
    e->tx_preset = preset;
    e->tx_delay_ms = delay_ms;
    e->tx_work = FlipperTxWorkPending;
    e->tx_running = true;
    furi_mutex_release(e->tx_mutex);
    if(!flipper_capture_notify_start(e)) return false;
    furi_event_flag_set(e->notify_flag, FLIPPER_CAP_NOTIFY_TX);
    return true;
}

/* ── Frequency sweep ─────────────────────────────────────────────────────── */
int flipper_capture_sweep(
    FlipperCaptureEngine* e,
    const float* freqs_mhz,
    int count,
    uint32_t dwell_ms)
{
    for(int i = 0; i < count; i++) {
        flipper_capture_stop(e);
        e->freq_mhz = freqs_mhz[i];
        flipper_capture_start(e);
        furi_delay_ms(dwell_ms);

        if(flipper_capture_flush(e)) {
            float rssi = flipper_capture_rssi(e);
            if(rssi > e->squelch_dbm) return i;
        }
    }
    return -1;
}

/* ── RSSI ─────────────────────────────────────────────────────────────────── */
float flipper_capture_rssi(FlipperCaptureEngine* e) {
    if(!e || !e->device || !e->running) return -100.0f;
    furi_mutex_acquire(e->tx_mutex, FuriWaitForever);
    if(e->tx_work != FlipperTxWorkIdle) {
        furi_mutex_release(e->tx_mutex);
        return -100.0f;
    }
    float rssi = subghz_devices_get_rssi(e->device);
    furi_mutex_release(e->tx_mutex);
    return rssi;
}
