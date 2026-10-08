#pragma once
#include <stdint.h>

/* * DO NOT LOAD THESE TABLES ONTO A LIVE CC1101. * * Historical register pairs I borrowed from third-party lists. Several address * labels in the old comments do not match the CC1101 map (SWRS061): *   0x04 = SYNC1, not MDMCFG4 (MDMCFG4 is 0x10) *   0x05 = SYNC0, not MDMCFG3 (MDMCFG3 is 0x11) *   0x29 = FSTEST, not PATABLE (PATABLE is burst access at 0x3E) * * The FAP does not compile this file (absent from application.fam). Live TX/RX * uses subghz_devices_load_preset() with the official Ook650 / Ook270 / 2FSK * presets. I keep this header only as a quarantine marker so the tables are not * mistaken for a ready-to-write register dump. */

typedef struct {
    uint8_t addr;
    uint8_t val;
} Cc1101Reg;

typedef enum {
    FbwPresetVAG_Pro = 0,
    FbwPresetPSA_Pro,
    FbwPresetKIA_Pro,
    FbwPresetHonda1_Pro,
    FbwPresetHonda2_Pro,
    FbwPresetRenault_Pro,
    FbwPresetFCA_Pro,
    FbwPresetAM650_Pro,
    FbwPresetFM476_Pro,
    FBW_PRESET_COUNT
} FbwPresetId;

/* Always returns NULL — tables are quarantined. */
const Cc1101Reg* fbw_preset_regs(FbwPresetId id, int* out_count);
const char* fbw_preset_name(FbwPresetId id);
uint32_t fbw_preset_freq_hz(FbwPresetId id);
FbwPresetId fbw_preset_find(const char* name);
