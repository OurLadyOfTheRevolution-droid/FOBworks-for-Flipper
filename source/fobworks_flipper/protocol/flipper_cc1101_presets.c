#include "flipper_cc1101_presets.h"

/* Quarantined. See flipper_cc1101_presets.h. I return fail-closed stubs so any accidental caller cannot obtain a register dump to write to the radio. */

const Cc1101Reg* fbw_preset_regs(FbwPresetId id, int* out_count) {
    (void)id;
    if(out_count) *out_count = 0;
    return NULL;
}

const char* fbw_preset_name(FbwPresetId id) {
    (void)id;
    return "quarantined";
}

uint32_t fbw_preset_freq_hz(FbwPresetId id) {
    (void)id;
    return 0;
}

FbwPresetId fbw_preset_find(const char* name) {
    (void)name;
    return FBW_PRESET_COUNT;
}
