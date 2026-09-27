#include "flipper_cc1101_presets.h"
#include <string.h>

/* ─────────────────────────────────────────────────────────────────────────── */
/* CC1101 Custom Presets — FOBworks optimized configurations.                  */
/*   Each preset is a full register dump for the CC1101 radio.                */
/*   Tuned per manufacturer for enhanced RX/TX performance.                   */
/* ─────────────────────────────────────────────────────────────────────────── */

/* PATable reference (CC1101 datasheet, Table 25):
 * 12dBm=0xC0, 10dBm=0xC5, 7dBm=0xCD, 5dBm=0x86, 0dBm=0x50,
 * -6dBm=0x37, -10dBm=0x26, -15dBm=0x1D, -20dBm=0x17, -30dBm=0x03
 */

/* ── VAG_Pro: OOK, ~2kBaud, 434.42MHz, 135kHz BW ───────────────────────── */
static const Cc1101Reg fbw_vag_pro[] = {
    {0x00, 0x0D},  /* IOCFG0: async serial output */
    {0x03, 0x47},  /* FIFOTHR: ADC_RETENTION */
    {0x04, 0x67},  /* MDMCFG4: BW=135kHz */
    {0x05, 0x32},  /* MDMCFG3: 3.79 kBaud */
    {0x06, 0x30},  /* MDMCFG2: ASK/OOK, no preamble/sync */
    {0x07, 0x06},  /* FSCTRL1: IF=152kHz */
    {0x0C, 0x18},  /* MCSM0: autocal on idle->rx/tx */
    {0x11, 0x03},  /* FREND1: current loop */
    {0x14, 0x00},  /* FREND0: PATable index 0 */
    {0x18, 0x18},  /* MCSM2: RX timeout */
    {0x19, 0xB0},  /* MCSM1: RX after TX */
    {0x1A, 0x13},  /* FOCCFG: no compensation */
    {0x1C, 0xC7},  /* AGCCTRL2: relative threshold */
    {0x1D, 0x80},  /* AGCCTRL1: carrier sense */
    {0x1E, 0xB0},  /* AGCCTRL0: gain */
    {0x21, 0x56},  /* FREND1: LNA current */
    {0x22, 0x10},  /* FSCAL3: VCO cap calibration */
    {0x24, 0xA9},  /* TEST2: test register */
    {0x25, 0x0A},  /* TEST1: test register */
    {0x26, 0x00},  /* TEST0: test register */
    {0x29, 0x59},  /* PATABLE: 0x59 = ~7dBm */
    {0x2D, 0x00},  /* PKTCTRL1: no append status */
    {0x2E, 0x32},  /* PKTCTRL0: async, continuous, no whitening */
    {0x00, 0x00},  /* terminator */
};

/* ── PSA_Pro: OOK, ~4kBaud, 433.92MHz, 162kHz BW ───────────────────────── */
static const Cc1101Reg fbw_psa_pro[] = {
    {0x00, 0x0D},  /* IOCFG0: async serial output */
    {0x03, 0x47},  /* FIFOTHR: ADC_RETENTION */
    {0x04, 0x57},  /* MDMCFG4: BW=162kHz */
    {0x05, 0x22},  /* MDMCFG3: ~4 kBaud */
    {0x06, 0x30},  /* MDMCFG2: ASK/OOK, no preamble/sync */
    {0x07, 0x06},  /* FSCTRL1: IF=152kHz */
    {0x0C, 0x18},  /* MCSM0: autocal */
    {0x11, 0x03},  /* FREND1: current loop */
    {0x14, 0x00},  /* FREND0: PATable index 0 */
    {0x18, 0x18},  /* MCSM2: RX timeout */
    {0x19, 0xB0},  /* MCSM1: RX after TX */
    {0x1A, 0x13},  /* FOCCFG: no compensation */
    {0x1C, 0xC7},  /* AGCCTRL2: relative threshold */
    {0x1D, 0x80},  /* AGCCTRL1: carrier sense */
    {0x1E, 0xB0},  /* AGCCTRL0: gain */
    {0x21, 0x56},  /* FREND1: LNA current */
    {0x22, 0x10},  /* FSCAL3: VCO cap calibration */
    {0x24, 0xA9},  /* TEST2 */
    {0x25, 0x0A},  /* TEST1 */
    {0x26, 0x00},  /* TEST0 */
    {0x29, 0x59},  /* PATABLE: ~7dBm */
    {0x2D, 0x00},  /* PKTCTRL1 */
    {0x2E, 0x32},  /* PKTCTRL0: async, continuous */
    {0x00, 0x00},  /* terminator */
};

/* ── KIA_Pro: 2FSK, ~5kBaud, 433.92MHz, 47kHz deviation ────────────────── */
static const Cc1101Reg fbw_kia_pro[] = {
    {0x00, 0x0D},  /* IOCFG0: async serial output */
    {0x03, 0x47},  /* FIFOTHR: ADC_RETENTION */
    {0x04, 0x66},  /* MDMCFG4: BW=102kHz */
    {0x05, 0x56},  /* MDMCFG3: ~5 kBaud */
    {0x06, 0x04},  /* MDMCFG2: 2FSK, no preamble/sync */
    {0x07, 0x06},  /* FSCTRL1: IF=152kHz */
    {0x08, 0x47},  /* DEVIATN: ±47.6kHz deviation */
    {0x0C, 0x18},  /* MCSM0: autocal */
    {0x11, 0x03},  /* FREND1: current loop */
    {0x14, 0x00},  /* FREND0: PATable index 0 */
    {0x18, 0x18},  /* MCSM2: RX timeout */
    {0x19, 0xB0},  /* MCSM1: RX after TX */
    {0x1A, 0x13},  /* FOCCFG: no compensation */
    {0x1C, 0xC7},  /* AGCCTRL2: relative threshold */
    {0x1D, 0x80},  /* AGCCTRL1: carrier sense */
    {0x1E, 0xB0},  /* AGCCTRL0: gain */
    {0x21, 0x56},  /* FREND1: LNA current */
    {0x22, 0x10},  /* FSCAL3: VCO cap calibration */
    {0x24, 0xA9},  /* TEST2 */
    {0x25, 0x0A},  /* TEST1 */
    {0x26, 0x00},  /* TEST0 */
    {0x29, 0xC0},  /* PATABLE: 0xC0 = 12dBm (max TX) */
    {0x2D, 0x00},  /* PKTCTRL1 */
    {0x2E, 0x32},  /* PKTCTRL0: async, continuous */
    {0x00, 0x00},  /* terminator */
};

/* ── Honda1_Pro: OOK, Honda custom (ADC_RETENTION) ─────────────────────── */
static const Cc1101Reg fbw_honda1_pro[] = {
    {0x00, 0x0D},  /* IOCFG0: async serial output */
    {0x03, 0x47},  /* FIFOTHR: ADC_RETENTION */
    {0x04, 0x67},  /* MDMCFG4: BW=270kHz */
    {0x05, 0x32},  /* MDMCFG3: 3.79 kBaud */
    {0x06, 0x30},  /* MDMCFG2: ASK/OOK, no preamble/sync */
    {0x07, 0x06},  /* FSCTRL1: IF=152kHz */
    {0x0C, 0x18},  /* MCSM0: autocal */
    {0x11, 0x03},  /* FREND1: current loop */
    {0x14, 0x00},  /* FREND0: PATable index 0 */
    {0x18, 0x18},  /* MCSM2: RX timeout */
    {0x19, 0xB0},  /* MCSM1: RX after TX */
    {0x1A, 0x13},  /* FOCCFG: no compensation */
    {0x1C, 0xC7},  /* AGCCTRL2: relative threshold */
    {0x1D, 0x80},  /* AGCCTRL1: carrier sense */
    {0x1E, 0xB0},  /* AGCCTRL0: gain */
    {0x21, 0x56},  /* FREND1: LNA current */
    {0x22, 0x10},  /* FSCAL3: VCO cap calibration */
    {0x24, 0xA9},  /* TEST2 */
    {0x25, 0x0A},  /* TEST1 */
    {0x26, 0x00},  /* TEST0 */
    {0x29, 0xC0},  /* PATABLE: 12dBm */
    {0x2D, 0x00},  /* PKTCTRL1 */
    {0x2E, 0x32},  /* PKTCTRL0: async, continuous */
    {0x00, 0x00},  /* terminator */
};

/* ── Honda2_Pro: OOK, Honda custom variant (ADC_RETENTION) ─────────────── */
static const Cc1101Reg fbw_honda2_pro[] = {
    {0x00, 0x0D},  /* IOCFG0: async serial output */
    {0x03, 0x47},  /* FIFOTHR: ADC_RETENTION */
    {0x04, 0x17},  /* MDMCFG4: BW=650kHz */
    {0x05, 0x32},  /* MDMCFG3: 3.79 kBaud */
    {0x06, 0x30},  /* MDMCFG2: ASK/OOK, no preamble/sync */
    {0x07, 0x06},  /* FSCTRL1: IF=152kHz */
    {0x0C, 0x18},  /* MCSM0: autocal */
    {0x11, 0x03},  /* FREND1: current loop */
    {0x14, 0x00},  /* FREND0: PATable index 0 */
    {0x18, 0x18},  /* MCSM2: RX timeout */
    {0x19, 0xB0},  /* MCSM1: RX after TX */
    {0x1A, 0x13},  /* FOCCFG: no compensation */
    {0x1C, 0xC7},  /* AGCCTRL2: relative threshold */
    {0x1D, 0x80},  /* AGCCTRL1: carrier sense */
    {0x1E, 0xB0},  /* AGCCTRL0: gain */
    {0x21, 0x56},  /* FREND1: LNA current */
    {0x22, 0x10},  /* FSCAL3: VCO cap calibration */
    {0x24, 0xA9},  /* TEST2 */
    {0x25, 0x0A},  /* TEST1 */
    {0x26, 0x00},  /* TEST0 */
    {0x29, 0xC0},  /* PATABLE: 12dBm */
    {0x2D, 0x00},  /* PKTCTRL1 */
    {0x2E, 0x32},  /* PKTCTRL0: async, continuous */
    {0x00, 0x00},  /* terminator */
};

/* ── Renault_Pro: OOK, ~8kBaud, 433.92MHz, 232kHz BW ───────────────────── */
static const Cc1101Reg fbw_renault_pro[] = {
    {0x00, 0x0D},  /* IOCFG0: async serial output */
    {0x03, 0x47},  /* FIFOTHR: ADC_RETENTION */
    {0x04, 0x27},  /* MDMCFG4: BW=232kHz */
    {0x05, 0x12},  /* MDMCFG3: ~8 kBaud */
    {0x06, 0x30},  /* MDMCFG2: ASK/OOK, no preamble/sync */
    {0x07, 0x06},  /* FSCTRL1: IF=152kHz */
    {0x0C, 0x18},  /* MCSM0: autocal */
    {0x11, 0x03},  /* FREND1: current loop */
    {0x14, 0x00},  /* FREND0: PATable index 0 */
    {0x18, 0x18},  /* MCSM2: RX timeout */
    {0x19, 0xB0},  /* MCSM1: RX after TX */
    {0x1A, 0x13},  /* FOCCFG: no compensation */
    {0x1C, 0xC7},  /* AGCCTRL2: relative threshold */
    {0x1D, 0x80},  /* AGCCTRL1: carrier sense */
    {0x1E, 0xB0},  /* AGCCTRL0: gain */
    {0x21, 0x56},  /* FREND1: LNA current */
    {0x22, 0x10},  /* FSCAL3: VCO cap calibration */
    {0x24, 0xA9},  /* TEST2 */
    {0x25, 0x0A},  /* TEST1 */
    {0x26, 0x00},  /* TEST0 */
    {0x29, 0xC0},  /* PATABLE: 12dBm */
    {0x2D, 0x00},  /* PKTCTRL1 */
    {0x2E, 0x32},  /* PKTCTRL0: async, continuous */
    {0x00, 0x00},  /* terminator */
};

/* ── FCA_Pro: OOK, ~4kBaud, 433.92MHz, 162kHz BW (Fiat/Chrysler) ───────── */
static const Cc1101Reg fbw_fca_pro[] = {
    {0x00, 0x0D},  /* IOCFG0: async serial output */
    {0x03, 0x47},  /* FIFOTHR: ADC_RETENTION */
    {0x04, 0x57},  /* MDMCFG4: BW=162kHz */
    {0x05, 0x22},  /* MDMCFG3: ~4 kBaud */
    {0x06, 0x30},  /* MDMCFG2: ASK/OOK, no preamble/sync */
    {0x07, 0x06},  /* FSCTRL1: IF=152kHz */
    {0x0C, 0x18},  /* MCSM0: autocal */
    {0x11, 0x03},  /* FREND1: current loop */
    {0x14, 0x00},  /* FREND0: PATable index 0 */
    {0x18, 0x18},  /* MCSM2: RX timeout */
    {0x19, 0xB0},  /* MCSM1: RX after TX */
    {0x1A, 0x13},  /* FOCCFG: no compensation */
    {0x1C, 0xC7},  /* AGCCTRL2: relative threshold */
    {0x1D, 0x80},  /* AGCCTRL1: carrier sense */
    {0x1E, 0xB0},  /* AGCCTRL0: gain */
    {0x21, 0x56},  /* FREND1: LNA current */
    {0x22, 0x10},  /* FSCAL3: VCO cap calibration */
    {0x24, 0xA9},  /* TEST2 */
    {0x25, 0x0A},  /* TEST1 */
    {0x26, 0x00},  /* TEST0 */
    {0x29, 0xC0},  /* PATABLE: 12dBm */
    {0x2D, 0x00},  /* PKTCTRL1 */
    {0x2E, 0x32},  /* PKTCTRL0: async, continuous */
    {0x00, 0x00},  /* terminator */
};

/* ── AM650_Pro: OOK, 650kHz BW, enhanced sensitivity ───────────────────── */
static const Cc1101Reg fbw_am650_pro[] = {
    {0x00, 0x0D},  /* IOCFG0: async serial output */
    {0x03, 0x47},  /* FIFOTHR: ADC_RETENTION */
    {0x04, 0x17},  /* MDMCFG4: BW=650kHz */
    {0x05, 0x32},  /* MDMCFG3: 3.79 kBaud */
    {0x06, 0x30},  /* MDMCFG2: ASK/OOK, no preamble/sync */
    {0x07, 0x06},  /* FSCTRL1: IF=152kHz */
    {0x0C, 0x18},  /* MCSM0: autocal */
    {0x11, 0x03},  /* FREND1: current loop */
    {0x14, 0x00},  /* FREND0: PATable index 0 */
    {0x18, 0x18},  /* MCSM2: RX timeout */
    {0x19, 0xB0},  /* MCSM1: RX after TX */
    {0x1A, 0x13},  /* FOCCFG: no compensation */
    {0x1C, 0xC7},  /* AGCCTRL2: relative threshold */
    {0x1D, 0x80},  /* AGCCTRL1: carrier sense */
    {0x1E, 0xB0},  /* AGCCTRL0: gain */
    {0x21, 0x56},  /* FREND1: LNA current */
    {0x22, 0x10},  /* FSCAL3: VCO cap calibration */
    {0x24, 0xA9},  /* TEST2 */
    {0x25, 0x0A},  /* TEST1 */
    {0x26, 0x00},  /* TEST0 */
    {0x29, 0xC0},  /* PATABLE: 12dBm */
    {0x2D, 0x00},  /* PKTCTRL1 */
    {0x2E, 0x32},  /* PKTCTRL0: async, continuous */
    {0x00, 0x00},  /* terminator */
};

/* ── FM476_Pro: 2FSK, 47.6kHz deviation, enhanced sensitivity ──────────── */
static const Cc1101Reg fbw_fm476_pro[] = {
    {0x00, 0x0D},  /* IOCFG0: async serial output */
    {0x03, 0x47},  /* FIFOTHR: ADC_RETENTION */
    {0x04, 0x66},  /* MDMCFG4: BW=102kHz */
    {0x05, 0x56},  /* MDMCFG3: ~5 kBaud */
    {0x06, 0x04},  /* MDMCFG2: 2FSK, no preamble/sync */
    {0x07, 0x06},  /* FSCTRL1: IF=152kHz */
    {0x08, 0x47},  /* DEVIATN: ±47.6kHz deviation */
    {0x0C, 0x18},  /* MCSM0: autocal */
    {0x11, 0x03},  /* FREND1: current loop */
    {0x14, 0x00},  /* FREND0: PATable index 0 */
    {0x18, 0x18},  /* MCSM2: RX timeout */
    {0x19, 0xB0},  /* MCSM1: RX after TX */
    {0x1A, 0x13},  /* FOCCFG: no compensation */
    {0x1C, 0xC7},  /* AGCCTRL2: relative threshold */
    {0x1D, 0x80},  /* AGCCTRL1: carrier sense */
    {0x1E, 0xB0},  /* AGCCTRL0: gain */
    {0x21, 0x56},  /* FREND1: LNA current */
    {0x22, 0x10},  /* FSCAL3: VCO cap calibration */
    {0x24, 0xA9},  /* TEST2 */
    {0x25, 0x0A},  /* TEST1 */
    {0x26, 0x00},  /* TEST0 */
    {0x29, 0xC0},  /* PATABLE: 12dBm */
    {0x2D, 0x00},  /* PKTCTRL1 */
    {0x2E, 0x32},  /* PKTCTRL0: async, continuous */
    {0x00, 0x00},  /* terminator */
};

/* ── Preset table ────────────────────────────────────────────────────────── */
typedef struct {
    const char*          name;
    const Cc1101Reg*     regs;
    uint32_t             freq_hz;
} FbwPresetEntry;

static const FbwPresetEntry fbw_presets[FBW_PRESET_COUNT] = {
    { "VAG_Pro",       fbw_vag_pro,       434420000 },
    { "PSA_Pro",       fbw_psa_pro,       433920000 },
    { "KIA_Pro",       fbw_kia_pro,       433920000 },
    { "Honda1_Pro",    fbw_honda1_pro,    433920000 },
    { "Honda2_Pro",    fbw_honda2_pro,    433920000 },
    { "Renault_Pro",   fbw_renault_pro,   433920000 },
    { "FCA_Pro",       fbw_fca_pro,       433920000 },
    { "AM650_Pro",     fbw_am650_pro,     433920000 },
    { "FM476_Pro",     fbw_fm476_pro,     433920000 },
};

/* ── Public API ──────────────────────────────────────────────────────────── */
const Cc1101Reg* fbw_preset_regs(FbwPresetId id, int* out_count) {
    if((int)id >= FBW_PRESET_COUNT) {
        if(out_count) *out_count = 0;
        return NULL;
    }
    int count = 0;
    const Cc1101Reg* r = fbw_presets[id].regs;
    while(r->addr != 0x00 || r->val != 0x00) {
        count++;
        r++;
    }
    if(out_count) *out_count = count;
    return fbw_presets[id].regs;
}

const char* fbw_preset_name(FbwPresetId id) {
    if((int)id >= FBW_PRESET_COUNT) return "Unknown";
    return fbw_presets[id].name;
}

uint32_t fbw_preset_freq_hz(FbwPresetId id) {
    if((int)id >= FBW_PRESET_COUNT) return 433920000;
    return fbw_presets[id].freq_hz;
}

FbwPresetId fbw_preset_find(const char* name) {
    if(!name) return FBW_PRESET_COUNT;
    for(int i = 0; i < FBW_PRESET_COUNT; i++) {
        if(strcmp(fbw_presets[i].name, name) == 0)
            return (FbwPresetId)i;
    }
    return FBW_PRESET_COUNT;
}
