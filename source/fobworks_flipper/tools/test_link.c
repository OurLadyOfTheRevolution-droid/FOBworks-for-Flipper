/*
 * Host conformance test for the dashboard control protocol.
 *
 * Verifies the two things that made the old bridge silently non-functional:
 *   1. Outbound frames use the keys the dashboard normalizer dispatches on
 *      (event / proto / cmd) — NOT the "type" key the old scaffold emitted.
 *   2. Inbound command names the dashboard actually sends parse to the right
 *      kind — including "setfreq" (the old FAP wrongly parsed "set_freq").
 *
 * Build + run:
 *   cc -I.. -o test_link test_link.c ../link/flipper_link_proto.c && ./test_link
 */
#include "../link/flipper_link_proto.h"
#include <stdio.h>
#include <string.h>

static int fails = 0;
static int checks = 0;

static void expect(int cond, const char* what) {
    checks++;
    if(!cond) { printf("  FAIL: %s\n", what); fails++; }
}

/* assert that `hay` contains substring `needle` */
static void contains(const char* hay, const char* needle, const char* what) {
    checks++;
    if(!strstr(hay, needle)) {
        printf("  FAIL: %s — expected to find \"%s\" in: %s\n", what, needle, hay);
        fails++;
    }
}

static void json_line(const char* text, const char* what) {
    size_t n = text ? strlen(text) : 0;
    expect(n >= 3 && text[0] == '{' && text[n - 2] == '}' &&
               text[n - 1] == '\n', what);
}

static void test_parse(void) {
    printf("== inbound command parsing ==\n");
    struct { const char* json; FlipperCmdKind kind; } cases[] = {
        {"{\"cmd\":\"status\"}",                 FlipperCmdStatus},
        {"{\"cmd\":\"keys\"}",                   FlipperCmdKeys},
        {"{\"cmd\":\"key_list\"}",               FlipperCmdKeys},
        {"{\"cmd\":\"scan_toggle\"}",            FlipperCmdScanToggle},
        {"{\"cmd\":\"setfreq\",\"freq\":315.0}", FlipperCmdSetFreq},
        {"{\"cmd\":\"capture\"}",                FlipperCmdCapture},
        {"{\"cmd\":\"replay\"}",                 FlipperCmdReplay},
        {"{\"cmd\":\"replay_stop\"}",            FlipperCmdReplayStop},
        {"{\"cmd\":\"jam_start\",\"freq\":433.92}", FlipperCmdJamStart},
        {"{\"cmd\":\"jam_stop\"}",               FlipperCmdJamStop},
        {"{\"cmd\":\"squelch\",\"val\":-55}",    FlipperCmdSquelch},
        {"{\"cmd\":\"save\"}",                   FlipperCmdSave},
        {"{\"cmd\":\"fbk_arm\"}",                FlipperCmdFbkArm},
        /* single-radio hardware can't honor these — recognized as unsupported */
        {"{\"cmd\":\"dual_band_replay\"}",       FlipperCmdUnsupported},
        {"{\"cmd\":\"toy_softid\"}",             FlipperCmdUnsupported},
        {"{\"cmd\":\"key_add\",\"name\":\"x\"}", FlipperCmdUnsupported},
        {"{\"cmd\":\"sweep_range\"}",            FlipperCmdUnsupported},
        {"{\"cmd\":\"bogus_thing\"}",            FlipperCmdUnknown},
    };
    for(size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        FlipperCmd c;
        bool ok = flipper_proto_parse_cmd(cases[i].json, &c);
        expect(ok, cases[i].json);
        expect(c.kind == cases[i].kind, cases[i].json);
    }

    /* numeric params extracted */
    FlipperCmd c;
    flipper_proto_parse_cmd("{\"cmd\":\"setfreq\",\"freq\":315.00}", &c);
    expect(c.has_freq && c.freq_mhz > 314.9f && c.freq_mhz < 315.1f, "setfreq freq value");
    flipper_proto_parse_cmd("{\"cmd\":\"squelch\",\"val\":-55}", &c);
    expect(c.has_val && c.val < -54.9f && c.val > -55.1f, "squelch val value");

    /* the regression guard: dashboard sends "setfreq", not "set_freq" */
    flipper_proto_parse_cmd("{\"cmd\":\"set_freq\"}", &c);
    expect(c.kind != FlipperCmdSetFreq, "set_freq must NOT be treated as setfreq");
}

static void test_emit(void) {
    printf("== outbound frame schema ==\n");
    char b[512];

    flipper_proto_emit_boot(b, sizeof(b), 433.92f, true);
    contains(b, "\"event\":\"boot\"", "boot event key");
    contains(b, "\"cc1101\":true", "boot cc1101");

    flipper_proto_emit_heartbeat(b, sizeof(b), 433.92f, true, 100, 12, true, 0);
    contains(b, "\"event\":\"heartbeat\"", "heartbeat event key");
    contains(b, "\"batt_pct\":100", "heartbeat batt_pct");
    contains(b, "\"scan\":true", "heartbeat scan");
    expect(!strstr(b, "\"type\""), "heartbeat must NOT use a 'type' key");

    FlipperDecodeResult r;
    memset(&r, 0, sizeof(r));
    strcpy(r.proto, "KeeLoq");
    strcpy(r.mfr_name, "Test Mfr");
    r.addr = 12345; r.cnt = 42; r.hop = 6789; r.btn = 2;
    r.freq_mhz = 433.92f; r.te_us = 200; r.rolling = true;
    r.predict_window = 16; r.predict_lo = 43; r.predict_hi = 59;
    flipper_proto_emit_signal(b, sizeof(b), &r, -72.0f);
    contains(b, "\"proto\":\"KeeLoq\"", "signal proto key");
    contains(b, "\"f\":\"433.92\"", "signal f is a string");
    contains(b, "\"sn\":12345", "signal sn");
    contains(b, "\"predict\":{", "signal predict object");
    contains(b, "\"mfr\":\"Test Mfr\"", "signal mfr escaped");

    flipper_proto_emit_status(b, sizeof(b), 315.0f, false, 100, 5, true, 0, -55.0f);
    contains(b, "\"cmd\":\"status\"", "status cmd key");
    contains(b, "\"freq\":315.00", "status freq present");

    flipper_proto_emit_status_ex(
        b, sizeof(b), 315.0f, false, 100, 5, true, 0, -55.0f,
        "cancelled", 42, 1234, 3, 8192, 4096);
    contains(b, "\"tx\":{\"state\":\"cancelled\",\"id\":42",
             "tx state telemetry");
    contains(b, "\"capture\":{\"overflow\":3,\"peak_free_heap\":8192,\"free_heap\":4096}",
             "capture telemetry");

    flipper_proto_emit_keys_empty(b, sizeof(b));
    contains(b, "\"cmd\":\"keys\"", "keys cmd key");
    contains(b, "\"keys\":[]", "keys empty array");

    flipper_proto_emit_cmdresp(b, sizeof(b), "jam_start", true, NULL);
    contains(b, "\"cmd\":\"jam_start\"", "cmdresp cmd key");
    contains(b, "\"ok\":true", "cmdresp ok");

    flipper_proto_emit_cmdresp(b, sizeof(b), "dual_band_replay", false, "unsupported");
    contains(b, "\"error\":\"unsupported\"", "cmdresp error");

    flipper_proto_emit_jam_start(b, sizeof(b), true, true, 433.92f, NULL);
    contains(b, "\"cmd\":\"jam_start\"", "jam start success emitter");
    contains(b, "\"freq\":433.92}", "jam success is complete JSON");
    json_line(b, "jam success is parseable JSON line");
    flipper_proto_emit_jam_start(b, sizeof(b), false, false, 433.92f, "radio-busy");
    contains(b, "\"error\":\"radio-busy\"}", "jam failure is complete JSON");
    json_line(b, "jam failure is parseable JSON line");

    flipper_proto_emit_replay_playing(b, sizeof(b), 1, 3, 42);
    contains(b, "\"event\":\"replay_playing\"", "replay_playing event");
    flipper_proto_emit_replay_done(b, sizeof(b));
    contains(b, "\"event\":\"replay_done\"", "replay_done event");

    /* overflow safety: a tiny buffer must not overrun and returns 0 */
    char tiny[8];
    size_t n = flipper_proto_emit_heartbeat(tiny, sizeof(tiny), 433.92f, true, 100, 1, true, 0);
    expect(n == 0, "overflow returns 0");
}

int main(void) {
    test_parse();
    test_emit();
    printf("\n%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
