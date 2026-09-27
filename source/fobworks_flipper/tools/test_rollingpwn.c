#include "../protocol/flipper_rollingpwn.h"

#include <stdio.h>
#include <string.h>

static int failures;

static void expect(bool condition, const char* name) {
    if(!condition) {
        fprintf(stderr, "FAIL: %s\n", name);
        failures++;
    }
}

static RollingPwnFrame frame(uint32_t counter, uint8_t command) {
    RollingPwnFrame f;
    f.counter = counter;
    f.serial = 0x12345678;
    f.command = command;
    f.frequency_mhz = 315.0f;
    return f;
}

static bool analyze(const RollingPwnFrame* frames, int n, RollingPwnPlan* plan) {
    return rollingpwn_analyze(
        frames, n, 0xFFFF, 3, 4, 315.0f, 0.05f, plan);
}

int main(void) {
    RollingPwnPlan plan;
    RollingPwnFrame wrap[] = {
        frame(0xFFFE, 1), frame(0xFFFF, 1), frame(0x0000, 1)};
    expect(analyze(wrap, 3, &plan), "counter wrap accepted");
    expect(plan.base_counter == 0xFFFE && plan.top_counter == 0,
           "wrap boundaries preserved");
    expect(plan.order[0] == 0 && plan.order[1] == 1 && plan.order[2] == 2,
           "capture chronology retained");

    RollingPwnFrame gap[] = {frame(10, 1), frame(12, 1), frame(14, 1)};
    expect(analyze(gap, 3, &plan), "bounded counter gaps accepted");
    gap[2].counter = 20;
    expect(!analyze(gap, 3, &plan), "over-limit gap rejected");

    RollingPwnFrame duplicate[] = {frame(10, 1), frame(10, 1), frame(11, 1)};
    expect(!analyze(duplicate, 3, &plan), "duplicate counter rejected");

    RollingPwnFrame out_of_order[] = {frame(10, 1), frame(12, 1), frame(11, 1)};
    expect(!analyze(out_of_order, 3, &plan), "out-of-order capture rejected");

    RollingPwnFrame mixed_command[] = {frame(10, 1), frame(11, 2), frame(12, 1)};
    expect(!analyze(mixed_command, 3, &plan), "mixed command rejected");

    RollingPwnFrame mixed_serial[] = {frame(10, 1), frame(11, 1), frame(12, 1)};
    mixed_serial[1].serial++;
    expect(!analyze(mixed_serial, 3, &plan), "mixed serial rejected");

    RollingPwnFrame mixed_frequency[] = {frame(10, 1), frame(11, 1), frame(12, 1)};
    mixed_frequency[1].frequency_mhz = 433.92f;
    expect(!analyze(mixed_frequency, 3, &plan), "wrong profile frequency rejected");

    RollingPwnFrame insufficient[] = {frame(10, 1), frame(11, 1)};
    expect(!analyze(insufficient, 2, &plan), "too-short sequence rejected");
    expect(!plan.sequence_candidate, "false result is not marked candidate");

    if(failures) return 1;
    puts("rollingpwn analyzer tests passed");
    return 0;
}