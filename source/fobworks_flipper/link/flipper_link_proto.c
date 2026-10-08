#include "flipper_link_proto.h"

bool flipper_proto_valid_access_code(const char* code) {
    if(!code) return false;
    for(size_t i = 0; i < 6; i++) {
        if(code[i] < '0' || code[i] > '9') return false;
    }
    return code[6] == '\0';
}
#if defined(FLIPPER_LINK_HOST_TEST)
#define FLIPPER_LINK_LIBRARY
enum FlipperPreset {
    FlipperPresetOOK650, FlipperPresetOOK270,
    FlipperPreset2FSKDev238, FlipperPreset2FSKDev476
};
struct FlipperCaptureResult {
    FlipperPulseBuf pulses;
    FlipperDecodeResult decode;
    bool decode_ok;
    uint32_t timestamp_ms;
};
struct FlipperLibEntry {
    char name[48];
};
#elif defined(__has_include)
#if __has_include(<furi.h>)
#define FLIPPER_LINK_LIBRARY
#endif
#endif
#if defined(FLIPPER_LINK_LIBRARY) && !defined(FLIPPER_LINK_HOST_TEST)
#include "../protocol/flipper_capture.h"
#include "../protocol/flipper_library.h"
#endif
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* My small JSON readers for the flat inbound command objects. */

/* I return a pointer to the value after the key's colon, or NULL if the key is absent. */
static const char* find_value(const char* line, const char* key) {
    char pat[32];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char* k = strstr(line, pat);
    if(!k) return NULL;
    const char* c = strchr(k, ':');
    if(!c) return NULL;
    c++;
    while(*c == ' ' || *c == '\t') c++;
    return c;
}

/* I copy an unquoted string value into buf. */
static bool read_str(const char* line, const char* key, char* buf, size_t n) {
    const char* v = find_value(line, key);
    if(!v || *v != '"' || n == 0) return false;
    buf[0] = '\0';
    v++;
    size_t i = 0;
    while(*v && *v != '"') {
        if(i + 1 >= n) {
            buf[0] = '\0';
            return false;
        }
        /* These values are identifiers, not general JSON strings. I reject escapes to avoid misreading a selected library name. */
        if(*v == '\\') {
            buf[0] = '\0';
            return false;
        }
        buf[i++] = *v++;
    }
    if(*v != '"') {
        buf[0] = '\0';
        return false;
    }
    buf[i] = '\0';
    return true;
}

static bool read_num(const char* line, const char* key, float* out) {
    const char* v = find_value(line, key);
    if(!v) return false;
    if(*v == '"') v++;              /* I accept quoted numbers such as "433.92". */
    char* end = NULL;
    float f = strtof(v, &end);
    if(end == v) return false;
    *out = f;
    return true;
}

static bool read_uint(const char* line, const char* key, uint32_t* out) {
    const char* v = find_value(line, key);
    if(!v) return false;
    if(*v == '"') v++;
    char* end = NULL;
    unsigned long value = strtoul(v, &end, 10);
    if(end == v) return false;
    *out = (uint32_t)value;
    return true;
}

/* ── Command classification ───────────────────────────────────────────────── */
static FlipperCmdKind classify(const char* name) {
    struct { const char* n; FlipperCmdKind k; } map[] = {
        {"status",       FlipperCmdStatus},
        {"keys",         FlipperCmdKeys},
        {"key_list",     FlipperCmdKeys},
        {"scan_toggle",  FlipperCmdScanToggle},
        {"setfreq",      FlipperCmdSetFreq},
        {"capture",      FlipperCmdCapture},
        {"replay",       FlipperCmdReplay},
        {"replay_stop",  FlipperCmdReplayStop},
        {"jam_start",    FlipperCmdJamStart},
        {"jam_stop",     FlipperCmdJamStop},
        {"squelch",      FlipperCmdSquelch},
        {"save",         FlipperCmdSave},
        {"fbk_arm",      FlipperCmdFbkArm},
        {"fbk_disarm",   FlipperCmdFbkDisarm},
        {"fbk_replay",   FlipperCmdFbkReplay},
        {"hello",        FlipperCmdHello},
        {"library_list", FlipperCmdLibraryList},
        {"library_get",  FlipperCmdLibraryGet},
        {"library_replay", FlipperCmdLibraryReplay},
        {"library_delete", FlipperCmdLibraryDelete},
        {"library_export", FlipperCmdLibraryExport},
        {"utility_status", FlipperCmdUtility},
        {"auth",           FlipperCmdAuth},
    };
    for(size_t i = 0; i < sizeof(map) / sizeof(map[0]); i++)
        if(strcmp(name, map[i].n) == 0) return map[i].k;

    /* I cannot honor these dashboard commands on a single-CC1101 device. I return an unsupported reply instead of ignoring them. */
    const char* unsupported[] = {
        "dual_band_replay", "cap_range", "sweep_range", "cap_mode", "fast_hop",
        "toy_softid", "set_filter", "relay_status", "fobrelay_status", "fobrelay",
        "canfuzz_status",
        "brute_status", "forcelf_status", "key_add", "key_del", "key_clear",
        "del_key", "play_key", "key_recover_start", "key_recover_cancel",
        "key_recover_offline", "decode", "save_decoded", "lib_replay",
        "replay_predicted", "replay_next", "replay_seq",
    };
    for(size_t i = 0; i < sizeof(unsupported) / sizeof(unsupported[0]); i++)
        if(strcmp(name, unsupported[i]) == 0) return FlipperCmdUnsupported;

    return FlipperCmdUnknown;
}

bool flipper_proto_parse_cmd(const char* line, FlipperCmd* out) {
    if(!line || !out) return false;
    memset(out, 0, sizeof(*out));

    if(!read_str(line, "cmd", out->name, sizeof(out->name))) return false;
    out->kind = classify(out->name);

    float f;
    if(read_num(line, "freq", &f)) { out->has_freq = true; out->freq_mhz = f; }
    /* I accept the supported spellings for the squelch/generic numeric value. */
    if(read_num(line, "val", &f) || read_num(line, "dbm", &f) ||
       read_num(line, "db", &f)) {
        out->has_val = true;
        out->val = f;
    }
    if(read_uint(line, "id", &out->id)) out->has_id = true;
    if(!read_str(line, "name", out->entry, sizeof(out->entry)))
        read_str(line, "entry", out->entry, sizeof(out->entry));
    uint32_t u;
    if(read_uint(line, "offset", &u)) out->offset = (int)(u > 16 ? 16 : u);
    if(read_uint(line, "limit", &u)) out->limit = (int)(u > 16 ? 16 : u);
    if(out->limit == 0) out->limit = 16;
    const char* dv = find_value(line, "decoded");
    if(dv) {
        out->has_decoded = true;
        out->decoded = strncmp(dv, "false", 5) != 0;
    } else if(strstr(line, "\"raw\":true")) {
        out->has_decoded = true;
        out->decoded = false;
    }
    if(read_str(line, "code", out->code, sizeof(out->code))) out->has_code = true;
    if(read_str(line, "new_code", out->new_code, sizeof(out->new_code)))
        out->has_new_code = true;
    return true;
}

/* ── Emitters ─────────────────────────────────────────────────────────────── */
static size_t done(char* out, int written, size_t n) {
    if(written < 0 || (size_t)written >= n) { if(n) out[0] = '\0'; return 0; }
    return (size_t)written;
}

size_t flipper_proto_emit_boot(char* out, size_t n, float freq_mhz, bool cc1101_ok) {
    return done(out, snprintf(out, n,
        "{\"event\":\"boot\",\"freq\":%.2f,\"cc1101\":%s}\n",
        (double)freq_mhz, cc1101_ok ? "true" : "false"), n);
}

size_t flipper_proto_emit_heartbeat(
    char* out, size_t n,
    float freq_mhz, bool scanning, int batt_pct,
    uint32_t uptime_s, bool cc1101_ok, int keys_used) {
    return done(out, snprintf(out, n,
        "{\"event\":\"heartbeat\",\"freq\":%.2f,\"scan\":%s,\"batt_pct\":%d,"
        "\"uptime\":%lu,\"cc1101\":%s,\"keys\":%d}\n",
        (double)freq_mhz, scanning ? "true" : "false", batt_pct,
        (unsigned long)uptime_s, cc1101_ok ? "true" : "false", keys_used), n);
}

/* I escape short protocol and manufacturer strings for JSON output. */
static void esc(const char* in, char* out, size_t n) {
    if(!out || n == 0) return;
    size_t o = 0;
    for(size_t i = 0; in && in[i] && o + 1 < n; i++) {
        char c = in[i];
        const char* short_escape = NULL;
        if(c == '"') short_escape = "\\\"";
        else if(c == '\\') short_escape = "\\\\";
        else if(c == '\b') short_escape = "\\b";
        else if(c == '\f') short_escape = "\\f";
        else if(c == '\n') short_escape = "\\n";
        else if(c == '\r') short_escape = "\\r";
        else if(c == '\t') short_escape = "\\t";
        if(short_escape) {
            size_t m = strlen(short_escape);
            if(o + m >= n) break;
            memcpy(out + o, short_escape, m);
            o += m;
        } else if((unsigned char)c < 0x20) {
            if(o + 6 >= n) break;
            snprintf(out + o, n - o, "\\u%04x", (unsigned char)c);
            o += 6;
        } else {
            out[o++] = c;
        }
    }
    out[o] = '\0';
}

size_t flipper_proto_emit_signal(
    char* out, size_t n, const FlipperDecodeResult* r, float rssi_dbm) {
    if(!r) return 0;
    char proto[40], mfr[40];
    esc(r->proto, proto, sizeof(proto));
    esc(r->mfr_name, mfr, sizeof(mfr));

    /* I include prediction details only when the decoder produced a window. The dashboard uses delta to distinguish predicted rolling codes. */
    char predict[128] = "";
    if(r->rolling && r->predict_window > 0) {
        snprintf(predict, sizeof(predict),
            ",\"predict\":{\"next_ctr\":%lu,\"next_1\":%lu,\"delta\":1,"
            "\"brute_window\":[%lu,%lu],\"key_found\":%s}",
            (unsigned long)(r->cnt + 1), (unsigned long)(r->cnt + 1),
            (unsigned long)r->predict_lo, (unsigned long)r->predict_hi,
             r->device_key_hex[0] ? "true" : "false");
    }

    return done(out, snprintf(out, n,
        "{\"event\":\"signal\",\"proto\":\"%s\",\"f\":\"%.2f\",\"sn\":%lu,\"btn\":%u,\"ctr\":%lu,"
        "\"hop\":%lu,\"rssi\":%.0f,\"te_us\":%lu,\"bits\":%d,"
        "\"rolling\":%s,\"mfr\":\"%s\","
        "\"confirmed\":%s,\"validation_tier\":\"decoder\","
        "\"predict_eligible\":%s,\"crypto_confirmed\":false%s}\n",
        proto, (double)r->freq_mhz, (unsigned long)r->addr, r->btn,
        (unsigned long)r->cnt, (unsigned long)r->hop, (double)rssi_dbm,
        (unsigned long)r->te_us, r->bits, r->rolling ? "true" : "false", mfr,
        r->proto[0] ? "true" : "false",
        (r->rolling && r->predict_window > 0) ? "true" : "false", predict), n);
}

size_t flipper_proto_emit_status(
    char* out, size_t n,
    float freq_mhz, bool scanning, int batt_pct,
    uint32_t uptime_s, bool cc1101_ok, int keys_used,
    float squelch_dbm) {
    return flipper_proto_emit_status_ex(
        out, n, freq_mhz, scanning, batt_pct, uptime_s, cc1101_ok,
        keys_used, squelch_dbm, "idle", 0, 0, 0, 0, 0);
}

size_t flipper_proto_emit_status_ex(
    char* out, size_t n,
    float freq_mhz, bool scanning, int batt_pct,
    uint32_t uptime_s, bool cc1101_ok, int keys_used,
    float squelch_dbm, const char* tx_state, uint32_t tx_id,
    uint32_t tx_deadline_ms, uint32_t capture_overflow,
    uint32_t peak_free_heap, uint32_t free_heap) {
    if(!tx_state) tx_state = "idle";
    return done(out, snprintf(out, n,
        "{\"cmd\":\"status\",\"product\":\"fobworks-for-flipper\","
        "\"firmware\":\"1.0\",\"link_proto\":{\"major\":1,\"minor\":2},"
        "\"freq\":%.2f,\"batt_v\":0,\"batt_pct\":%d,"
        "\"scan\":%s,\"uptime\":%lu,\"cc1101\":%s,\"fsk_mode\":false,"
        "\"keys_used\":%d,\"squelch_dbm\":%.0f,\"ch_b_ready\":false,"
        "\"tx\":{\"state\":\"%s\",\"id\":%lu,\"deadline_ms\":%lu},"
        "\"capture\":{\"overflow\":%lu,\"peak_free_heap\":%lu,\"free_heap\":%lu}}\n",
        (double)freq_mhz, batt_pct, scanning ? "true" : "false",
        (unsigned long)uptime_s, cc1101_ok ? "true" : "false", keys_used,
        (double)squelch_dbm, tx_state, (unsigned long)tx_id,
        (unsigned long)tx_deadline_ms, (unsigned long)capture_overflow,
        (unsigned long)peak_free_heap, (unsigned long)free_heap), n);
}

size_t flipper_proto_emit_keys_empty(char* out, size_t n) {
    return done(out, snprintf(out, n, "{\"cmd\":\"keys\",\"keys\":[]}\n"), n);
}

size_t flipper_proto_emit_cmdresp(
    char* out, size_t n, const char* cmd, bool ok, const char* error_or_null) {
    return flipper_proto_emit_cmdresp_id(out, n, cmd, ok, error_or_null, false, 0);
}

size_t flipper_proto_emit_cmdresp_id(
    char* out, size_t n, const char* cmd, bool ok, const char* error_or_null,
    bool has_id, uint32_t id) {
    char c[32];
    esc(cmd, c, sizeof(c));
    char idbuf[32] = "";
    if(has_id) snprintf(idbuf, sizeof(idbuf), ",\"id\":%lu", (unsigned long)id);
    if(error_or_null && error_or_null[0]) {
        char e[96];
        esc(error_or_null, e, sizeof(e));
        return done(out, snprintf(out, n,
            "{\"cmd\":\"%s\",\"ok\":%s%s,\"error\":\"%s\"}\n",
            c, ok ? "true" : "false", idbuf, e), n);
    }
    return done(out, snprintf(out, n,
        "{\"cmd\":\"%s\",\"ok\":%s%s}\n", c, ok ? "true" : "false", idbuf), n);
}

size_t flipper_proto_emit_jam_start(
    char* out, size_t n, bool ok, bool active, float freq_mhz,
    const char* error_or_null) {
    char error[96] = "";
    if(error_or_null && error_or_null[0]) esc(error_or_null, error, sizeof(error));
    return done(out, snprintf(out, n,
        "{\"cmd\":\"jam_start\",\"ok\":%s,\"active\":%s,\"freq\":%.2f%s%s%s}\n",
        ok ? "true" : "false", active ? "true" : "false",
        (double)freq_mhz, error[0] ? ",\"error\":\"" : "",
        error, error[0] ? "\"" : ""), n);
}

#ifdef FLIPPER_LINK_LIBRARY
size_t flipper_proto_emit_capabilities(
    char* out, size_t n, bool ok, bool has_id, uint32_t id) {
    char idbuf[32] = "";
    if(has_id) snprintf(idbuf, sizeof(idbuf), ",\"id\":%lu", (unsigned long)id);
    return done(out, snprintf(out, n,
        "{\"event\":\"capabilities\",\"ok\":%s%s,\"proto_major\":1,"
        "\"proto_minor\":2,\"capabilities\":["
        "\"status\",\"keys\",\"scan\",\"setfreq\",\"squelch\",\"capture\","
        "\"replay\",\"jam\",\"save\"]}\n",
        ok ? "true" : "false", idbuf), n);
}

size_t flipper_proto_emit_library_list(
    char* out, size_t n, bool decoded, int offset, int total,
    const FlipperLibEntry* entries, int count) {
    if(!out || (count > 0 && !entries) || count < 0 ||
       count > FLIPPER_LIB_LIST_MAX) return 0;
    int p = snprintf(out, n,
        "{\"event\":\"library_list\",\"cmd\":\"library_list\",\"ok\":true,"
        "\"decoded\":%s,\"offset\":%d,\"limit\":%d,\"total\":%d,\"entries\":[",
        decoded ? "true" : "false", offset, count, total);
    if(p < 0 || (size_t)p >= n) return 0;
    for(int i = 0; i < count; i++) {
        char name[FLIPPER_LIB_NAME_MAX * 2];
        esc(entries[i].name, name, sizeof(name));
        int w = snprintf(out + p, n - (size_t)p, "%s{\"name\":\"%s\"}",
                         i ? "," : "", name);
        if(w < 0 || (size_t)w >= n - (size_t)p) return 0;
        p += w;
    }
    int w = snprintf(out + p, n - (size_t)p, "]}\n");
    if(w < 0 || (size_t)w >= n - (size_t)p) return 0;
    return (size_t)(p + w);
}

size_t flipper_proto_emit_library_detail(
    char* out, size_t n, bool decoded, const char* name,
    const FlipperCaptureResult* cap, FlipperPreset preset) {
    if(!out || !name || !cap || cap->pulses.len < 0 ||
       cap->pulses.len > FLIPPER_PULSE_MAX) return 0;
    char ename[FLIPPER_LIB_NAME_MAX * 2], proto[64], mfr[64], pnote[128];
    esc(name, ename, sizeof(ename));
    esc(cap->decode.proto, proto, sizeof(proto));
    esc(cap->decode.mfr_name, mfr, sizeof(mfr));
    esc(cap->decode.predict_note, pnote, sizeof(pnote));
    int shown = cap->pulses.len < 64 ? cap->pulses.len : 64;
    char pulses[768];
    int pp = 0;
    for(int i = 0; i < shown; i++) {
        int w = snprintf(pulses + pp, sizeof(pulses) - (size_t)pp,
                         "%s%lu", i ? "," : "",
                         (unsigned long)cap->pulses.durations[i]);
        if(w < 0 || (size_t)w >= sizeof(pulses) - (size_t)pp) return 0;
        pp += w;
    }
    return done(out, snprintf(out, n,
        "{\"event\":\"library_detail\",\"cmd\":\"library_get\",\"ok\":true,"
        "\"decoded\":%s,\"name\":\"%s\",\"preset\":%d,\"freq\":%.2f,"
        "\"pulse_count\":%d,\"pulses\":[%s],\"pulse_truncated\":%s,"
        "\"decode\":{\"ok\":%s,\"proto\":\"%s\",\"sn\":%lu,\"ctr\":%lu,"
        "\"hop\":%lu,\"btn\":%u,\"te_us\":%lu,\"bits\":%d,\"rolling\":%s,"
        "\"mfr\":\"%s\",\"predict_window\":%lu,\"predict_lo\":%lu,"
        "\"predict_hi\":%lu,\"predict_note\":\"%s\"}}\n",
        decoded ? "true" : "false", ename, (int)preset,
        (double)cap->pulses.freq_mhz, cap->pulses.len, pulses,
        cap->pulses.len > shown ? "true" : "false",
        cap->decode_ok ? "true" : "false", proto,
        (unsigned long)cap->decode.addr, (unsigned long)cap->decode.cnt,
        (unsigned long)cap->decode.hop, cap->decode.btn,
        (unsigned long)cap->decode.te_us, cap->decode.bits,
        cap->decode.rolling ? "true" : "false", mfr,
        (unsigned long)cap->decode.predict_window,
        (unsigned long)cap->decode.predict_lo, (unsigned long)cap->decode.predict_hi,
        pnote), n);
}

size_t flipper_proto_emit_library_event(
    char* out, size_t n, const char* action, bool ok, const char* name) {
    char a[32], e[FLIPPER_LIB_NAME_MAX * 2];
    esc(action, a, sizeof(a));
    esc(name, e, sizeof(e));
    return done(out, snprintf(out, n,
        "{\"event\":\"library\",\"action\":\"%s\",\"ok\":%s,\"name\":\"%s\"}\n",
        a, ok ? "true" : "false", e), n);
}
#endif

size_t flipper_proto_emit_replay_playing(
    char* out, size_t n, int idx, int of, uint32_t ctr) {
    return done(out, snprintf(out, n,
        "{\"event\":\"replay_playing\",\"n\":%d,\"of\":%d,\"ctr\":%lu}\n",
        idx, of, (unsigned long)ctr), n);
}

size_t flipper_proto_emit_replay_stopped(
    char* out, size_t n, int idx, uint32_t ctr, const char* proto, uint32_t sn) {
    char p[40];
    esc(proto, p, sizeof(p));
    return done(out, snprintf(out, n,
        "{\"event\":\"replay_stopped\",\"n\":%d,\"ctr\":%lu,\"proto\":\"%s\",\"sn\":%lu}\n",
        idx, (unsigned long)ctr, p, (unsigned long)sn), n);
}

size_t flipper_proto_emit_replay_done(char* out, size_t n) {
    return done(out, snprintf(out, n, "{\"event\":\"replay_done\"}\n"), n);
}
