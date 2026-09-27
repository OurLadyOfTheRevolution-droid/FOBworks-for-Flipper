#include <assert.h>
#include <string.h>
#include "../protocol/flipper_saved_check.h"

int main(void) {
    FlipperPulseBuf pulses;
    FlipperSavedCheck check;
    memset(&pulses, 0, sizeof(pulses));

    flipper_saved_judge(NULL, "RAW", &check);
    assert(check.kind == FlipperSavedNoWave);
    assert(strcmp(check.line1, "Cannot check") == 0);

    pulses.len = 2;
    pulses.durations[0] = 1000000;
    pulses.durations[1] = 1000000;
    flipper_saved_judge(&pulses, "RAW", &check);
    assert(check.kind == FlipperSavedNone);
    assert(strcmp(check.line1, "No match") == 0);
    assert(strcmp(check.line3, "File label: RAW") == 0);
    return 0;
}