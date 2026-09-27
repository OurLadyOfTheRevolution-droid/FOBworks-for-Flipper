#pragma once
#include <stdint.h>

/* ─────────────────────────────────────────────────────────────────────────── */
/* CC1101 Custom Presets — FOBworks optimized configurations.                  */
/*   Tuned per manufacturer for enhanced RX/TX performance.                    */
/* ─────────────────────────────────────────────────────────────────────────── */

/* CC1101 register entry (address, value). Terminated by {0x00, 0x00}.        */
typedef struct {
    uint8_t addr;
    uint8_t val;
} Cc1101Reg;

/* CC1101 custom preset identifiers — matches FlipperPreset enum offset.      */
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

/* Get register array for a preset. Returns NULL if id is invalid.             */
/* out_count receives the number of registers (excluding terminator).          */
const Cc1101Reg* fbw_preset_regs(FbwPresetId id, int* out_count);

/* Human-readable preset name. */
const char* fbw_preset_name(FbwPresetId id);

/* Default center frequency for the preset (Hz). */
uint32_t fbw_preset_freq_hz(FbwPresetId id);

/* Find preset ID by name. Returns FBW_PRESET_COUNT if not found. */
FbwPresetId fbw_preset_find(const char* name);
