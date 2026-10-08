/* Host contract tests for the FAP library boundary. These tests cover the library rules and JSON emitters without emulating Flipper Storage. They do not test production filesystem access. I reported that the app-thread timer candidate seems to work, but full hardware qualification and the exact fault remain unconfirmed. Build with the tools/Makefile. */
#include "../link/flipper_link_proto.h"
#include "../protocol/flipper_library_rules.h"
#include <stdio.h>
#include <string.h>

enum FlipperPreset {
    FlipperPresetOOK650, FlipperPresetOOK270,
    FlipperPreset2FSKDev238, FlipperPreset2FSKDev476
};

struct FlipperLibEntry {
    char name[48];
};

struct FlipperCaptureResult {
    FlipperPulseBuf pulses;
    FlipperDecodeResult decode;
    bool decode_ok;
    uint32_t timestamp_ms;
};

static int checks;
static int failures;

static void expect(int condition, const char* label) {
    checks++;
    if(!condition) {
        printf("  FAIL: %s\n", label);
        failures++;
    }
}

static void contains(const char* text, const char* needle, const char* label) {
    expect(text && strstr(text, needle) != NULL, label);
}

static void test_selectors_and_bounds(void) {
    printf("== library selector and bounds rules ==\n");
    expect(flipper_lib_name_is_safe("KeeLoq_001-c1"), "safe basename");
    expect(!flipper_lib_name_is_safe("../KeeLoq_001"), "dot traversal rejected");
    expect(!flipper_lib_name_is_safe("raw/foo"), "slash path rejected");
    expect(!flipper_lib_name_is_safe("/ext/flipper_fobscan/library/raw/x"),
           "absolute path rejected");
    expect(!flipper_lib_name_is_safe("a.sub"), "extension is not a selector");
    expect(!flipper_lib_name_is_safe(""), "empty selector rejected");

    char long_name[64];
    memset(long_name, 'x', sizeof(long_name) - 1);
    long_name[sizeof(long_name) - 1] = '\0';
    expect(!flipper_lib_name_is_safe(long_name), "oversized selector rejected");

    expect(flipper_lib_page_offset(-1) == 0, "negative offset clamps");
    expect(flipper_lib_page_offset(999) == FLIPPER_LIBRARY_PAGE_LIMIT,
           "offset is bounded");
    expect(flipper_lib_page_limit(0) == FLIPPER_LIBRARY_PAGE_LIMIT,
           "default page limit");
    expect(flipper_lib_page_limit(999) == FLIPPER_LIBRARY_PAGE_LIMIT,
           "page limit is bounded");
    expect(!flipper_lib_pulse_count_is_safe(-1), "negative pulse count rejected");
    expect(flipper_lib_pulse_count_is_safe(FLIPPER_LIBRARY_PULSE_LIMIT),
           "maximum pulse count accepted");
    expect(!flipper_lib_pulse_count_is_safe(FLIPPER_LIBRARY_PULSE_LIMIT + 1),
           "oversized pulse count rejected");
    expect(flipper_lib_eviction_name_before("A_older", "B_newer"),
           "eviction key is deterministic lexical order");
    expect(!flipper_lib_eviction_name_before("B_newer", "A_older"),
           "eviction key is stable across boots");
}

static void test_command_inputs(void) {
    printf("== malformed and oversized command inputs ==\n");
    FlipperCmd cmd;
    expect(!flipper_proto_parse_cmd(NULL, &cmd), "null command rejected");
    expect(!flipper_proto_parse_cmd("{\"cmd\":}", &cmd), "malformed command rejected");
    expect(!flipper_proto_parse_cmd("{\"cmd\":\"library_get", &cmd),
           "unterminated command rejected");
    expect(flipper_proto_parse_cmd(
               "{\"cmd\":\"library_get\",\"name\":\"../escape\"}", &cmd),
           "well-formed traversal parses for dispatch validation");
    expect(!flipper_lib_name_is_safe(cmd.entry), "parsed traversal remains rejected");
    const char* selectors[] = {
        "{\"cmd\":\"library_delete\",\"name\":\"../x\"}",
        "{\"cmd\":\"library_replay\",\"name\":\"/absolute\"}",
        "{\"cmd\":\"library_export\",\"name\":\"raw/foo\"}"
    };
    for(size_t i = 0; i < sizeof(selectors) / sizeof(selectors[0]); i++) {
        expect(flipper_proto_parse_cmd(selectors[i], &cmd),
               "selector action parses");
        expect(!flipper_lib_name_is_safe(cmd.entry),
               "delete/replay/export selector is rejected");
    }

    char line[256];
    memset(line, 'x', sizeof(line) - 1);
    line[0] = '{';
    line[1] = '"';
    line[2] = 'c';
    line[3] = 'm';
    line[4] = 'd';
    line[5] = '"';
    line[6] = ':';
    line[7] = '"';
    line[8] = 'x';
    line[9] = '"';
    line[10] = '}';
    line[11] = '\0';
    expect(flipper_proto_parse_cmd(line, &cmd), "bounded unknown command parses");

    char oversized[256];
    snprintf(oversized, sizeof(oversized),
             "{\"cmd\":\"library_get\",\"name\":\"%060d\"}", 1);
    expect(flipper_proto_parse_cmd(oversized, &cmd),
           "oversized selector does not crash parser");
    expect(!flipper_lib_name_is_safe(cmd.entry),
           "oversized selector is not truncated into a target");
}

static void test_reply_schemas(void) {
    printf("== deterministic library reply schemas and JSON escaping ==\n");
    char out[4096];
    struct FlipperLibEntry entries[2];
    strcpy(entries[0].name, "safe_entry");
    strcpy(entries[1].name, "quote\"name");
    size_t n = flipper_proto_emit_library_list(
        out, sizeof(out), true, 0, 2, entries, 2);
    expect(n > 0, "library_list emits");
    contains(out, "\"event\":\"library_list\"", "library_list event");
    contains(out, "\"cmd\":\"library_list\"", "library_list cmd");
    contains(out, "\"decoded\":true", "library_list category");
    contains(out, "\"offset\":0", "library_list offset");
    contains(out, "\"total\":2", "library_list total");
    contains(out, "\"name\":\"quote\\\"name\"", "library_list escapes names");
    expect(flipper_proto_emit_library_list(
               out, sizeof(out), true, 0, 2, entries, 17) == 0,
           "library_list entry bound");

    struct FlipperCaptureResult cap;
    memset(&cap, 0, sizeof(cap));
    cap.pulses.freq_mhz = 433.92f;
    cap.pulses.len = 3;
    cap.pulses.durations[0] = 400;
    cap.pulses.durations[1] = 800;
    cap.pulses.durations[2] = 400;
    cap.decode_ok = true;
    strcpy(cap.decode.proto, "Proto\"X");
    strcpy(cap.decode.mfr_name, "Mfr\nName");
    strcpy(cap.decode.predict_note, "bounded\twindow");
    cap.decode.addr = 0x1234;
    cap.decode.cnt = 7;
    cap.decode.bits = 66;
    cap.decode.rolling = true;
    cap.decode.predict_window = 8;
    cap.decode.predict_lo = 8;
    cap.decode.predict_hi = 16;
    n = flipper_proto_emit_library_detail(
        out, sizeof(out), true, "entry\"one", &cap, (FlipperPreset)0);
    expect(n > 0, "library_get emits");
    contains(out, "\"event\":\"library_detail\"", "library_get event");
    contains(out, "\"cmd\":\"library_get\"", "library_get cmd");
    contains(out, "\"name\":\"entry\\\"one\"", "library_get escapes name");
    contains(out, "\"proto\":\"Proto\\\"X\"", "library_get escapes protocol");
    contains(out, "\"mfr\":\"Mfr\\nName\"", "library_get escapes metadata");
    contains(out, "\"pulse_count\":3", "library_get pulse count");
    contains(out, "\"pulses\":[400,800,400]", "library_get pulse data");
    cap.pulses.len = FLIPPER_PULSE_MAX + 1;
    expect(flipper_proto_emit_library_detail(
               out, sizeof(out), true, "entry", &cap, (FlipperPreset)0) == 0,
           "library_get pulse bound");
    cap.pulses.len = 3;

    n = flipper_proto_emit_library_event(
        out, sizeof(out), "delete", true, "raw_entry");
    expect(n > 0, "library event emits");
    contains(out, "\"event\":\"library\"", "library event");
    contains(out, "\"action\":\"delete\"", "delete action");

    /* A too-small output buffer must fail rather than emit partial JSON. */
    char tiny[16];
    expect(flipper_proto_emit_library_detail(
               tiny, sizeof(tiny), true, "entry", &cap, (FlipperPreset)0) == 0,
           "library_get output bound");
}

int main(void) {
    test_selectors_and_bounds();
    test_command_inputs();
    test_reply_schemas();
    printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}