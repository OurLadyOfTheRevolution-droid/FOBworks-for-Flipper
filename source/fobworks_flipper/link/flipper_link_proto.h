#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "../protocol/flipper_decoders.h"
typedef struct FlipperLibEntry FlipperLibEntry;
typedef struct FlipperCaptureResult FlipperCaptureResult;
typedef enum FlipperPreset FlipperPreset;
#ifndef FLIPPER_LIB_LIST_MAX
#define FLIPPER_LIB_LIST_MAX 16
#endif
#ifndef FLIPPER_LIB_NAME_MAX
#define FLIPPER_LIB_NAME_MAX 48
#endif
#ifndef FLIPPER_PULSE_MAX
#define FLIPPER_PULSE_MAX 512
#endif

/*
 * Transport-neutral wire protocol shared by the USB CDC and GPIO UART links.
 *
 * These functions do not depend on Furi or hardware, so tools/test_link.c can
 * exercise them on the host. flipper_link.c uses them to format outbound lines
 * and parse inbound commands.
 *
 * The dashboard's JSON normalizer dispatches on "event", "proto", and "cmd";
 * it does not read a "type" key. Keep this schema in sync with that parser or
 * the dashboard will drop frames.
 *
 * Outbound (Flipper -> dashboard), each newline-terminated:
 *   {"event":"boot", "freq":433.92, "cc1101":true}
 *   {"event":"heartbeat","freq":433.92,"scan":true,"batt_pct":100,"uptime":12,"cc1101":true,"keys":0}
 *   {"proto":"KeeLoq","f":"433.92","sn":123,"btn":2,"ctr":45,"hop":678,"rssi":-72,"hex":"...","mfr":"...","predict":{...}}
 *   {"cmd":"status","freq":433.92,"scan":true,"cc1101":true,...}
 *   {"cmd":"keys","keys":[]}
 *   {"cmd":"<name>","ok":true|false[,"error":"..."]}
 *   {"event":"replay_playing","n":1,"of":1,"ctr":0}
 *   {"event":"replay_stopped","n":1,"ctr":0,"proto":"KeeLoq","sn":123}
 *   {"event":"replay_done"}
 *
 * Inbound (dashboard -> Flipper): {"cmd":"<name>"[,"freq":..][,"val":..]}
 */

/* ── Inbound command kinds ────────────────────────────────────────────────── */
typedef enum {
    FlipperCmdUnknown = 0,   /* not recognized at all                          */
    FlipperCmdStatus,        /* status                                         */
    FlipperCmdKeys,          /* keys / key_list                                */
    FlipperCmdScanToggle,    /* scan_toggle                                    */
    FlipperCmdSetFreq,       /* setfreq (freq)                                 */
    FlipperCmdCapture,       /* capture (arm one-shot)                         */
    FlipperCmdReplay,        /* replay                                         */
    FlipperCmdReplayStop,    /* replay_stop                                    */
    FlipperCmdJamStart,      /* jam_start (freq)                               */
    FlipperCmdJamStop,       /* jam_stop                                       */
    FlipperCmdSquelch,       /* squelch (val = dBm)                            */
    FlipperCmdSave,          /* save                                           */
    FlipperCmdFbkArm,        /* fbk_arm                                        */
    FlipperCmdFbkDisarm,     /* fbk_disarm                                     */
    FlipperCmdFbkReplay,     /* fbk_replay                                     */
    FlipperCmdHello,         /* hello / capability negotiation                 */
    FlipperCmdLibraryList,   /* library_list                                   */
    FlipperCmdLibraryGet,    /* library_get                                    */
    FlipperCmdLibraryReplay, /* library_replay                                 */
    FlipperCmdLibraryDelete, /* library_delete                                 */
    FlipperCmdLibraryExport, /* library_export                                 */
    FlipperCmdUtility,       /* device-only utility lifecycle query             */
    /* Recognized but not possible on single-CC1101 Flipper hardware.
       Dispatched as an honest {"ok":false,"error":"unsupported..."} reply so
       the UI reflects reality instead of hanging. */
    FlipperCmdUnsupported,
} FlipperCmdKind;

typedef struct {
    FlipperCmdKind kind;
    char           name[24];   /* raw command name, always populated when found */
    bool           has_freq;
    float          freq_mhz;
    bool           has_val;
    float          val;        /* generic numeric param (squelch dBm, etc.)     */
    bool           has_id;
    uint32_t       id;
    char           entry[48];  /* validated by the library before use           */
    int            offset;
    int            limit;
    bool           has_decoded;
    bool           decoded;
} FlipperCmd;

/* Parse one newline-free JSON line into a command.
   Returns true if a "cmd" key was found (kind may be Unknown/Unsupported). */
bool flipper_proto_parse_cmd(const char* line, FlipperCmd* out);

/* ── Outbound emitters — write a newline-terminated line into out[0..n) ─────
   All return the number of bytes written (excluding the NUL), 0 on overflow. */

size_t flipper_proto_emit_boot(char* out, size_t n, float freq_mhz, bool cc1101_ok);

size_t flipper_proto_emit_heartbeat(
    char* out, size_t n,
    float freq_mhz, bool scanning, int batt_pct,
    uint32_t uptime_s, bool cc1101_ok, int keys_used);

size_t flipper_proto_emit_signal(
    char* out, size_t n, const FlipperDecodeResult* r, float rssi_dbm);

size_t flipper_proto_emit_status(
    char* out, size_t n,
    float freq_mhz, bool scanning, int batt_pct,
    uint32_t uptime_s, bool cc1101_ok, int keys_used,
    float squelch_dbm);

size_t flipper_proto_emit_status_ex(
    char* out, size_t n,
    float freq_mhz, bool scanning, int batt_pct,
    uint32_t uptime_s, bool cc1101_ok, int keys_used,
    float squelch_dbm, const char* tx_state, uint32_t tx_id,
    uint32_t tx_deadline_ms, uint32_t capture_overflow,
    uint32_t peak_free_heap, uint32_t free_heap);

/* Flipper FAP has no host-writable key store, so it honestly reports none. */
size_t flipper_proto_emit_keys_empty(char* out, size_t n);

size_t flipper_proto_emit_cmdresp(
    char* out, size_t n, const char* cmd, bool ok, const char* error_or_null);

size_t flipper_proto_emit_cmdresp_id(
    char* out, size_t n, const char* cmd, bool ok, const char* error_or_null,
    bool has_id, uint32_t id);

size_t flipper_proto_emit_capabilities(
    char* out, size_t n, bool ok, bool has_id, uint32_t id);
size_t flipper_proto_emit_library_list(
    char* out, size_t n, bool decoded, int offset, int total,
    const FlipperLibEntry* entries, int count);
size_t flipper_proto_emit_library_detail(
    char* out, size_t n, bool decoded, const char* name,
    const FlipperCaptureResult* cap, FlipperPreset preset);
size_t flipper_proto_emit_library_event(
    char* out, size_t n, const char* action, bool ok, const char* name);

size_t flipper_proto_emit_replay_playing(
    char* out, size_t n, int idx, int of, uint32_t ctr);
size_t flipper_proto_emit_replay_stopped(
    char* out, size_t n, int idx, uint32_t ctr, const char* proto, uint32_t sn);
size_t flipper_proto_emit_replay_done(char* out, size_t n);
size_t flipper_proto_emit_jam_start(
    char* out, size_t n, bool ok, bool active, float freq_mhz,
    const char* error_or_null);
