#include "flipper_radio_loader.h"
#include <furi.h>
#include <furi_hal_power.h>
#include <furi_hal_subghz.h>
#include <string.h>

/* ─────────────────────────────────────────────────────────────────────────── */
/* Radio Device Loader + Region Unlock — FOBworks custom implementation.    */
/*   Internal/external CC1101 abstraction with OTG power management.          */
/*   Region unlock pattern for full SubGHz spectrum access.                   */
/* ─────────────────────────────────────────────────────────────────────────── */

/* ── Device Loader (Internal/External CC1101) ───────────────────────────── */
/* Manages OTG power for external CC1101 modules (e.g., Rabbit Labs).        */

static bool s_otg_enabled = false;

bool radio_loader_set(RadioDeviceType type) {
    if(type == RadioDeviceInternal) {
        return true;
    }

    /* External CC1101 — enable OTG power with retry loop */
    if(!s_otg_enabled) {
        bool powered = false;
        for(int retry = 0; retry < 5; retry++) {
            if(furi_hal_power_enable_otg()) {
                powered = true;
                furi_delay_ms(10);
                break;
            }
            furi_delay_ms(10);
        }
        if(!powered) return false;
        s_otg_enabled = true;
    }

    return true;
}

bool radio_loader_is_external(void) {
    return s_otg_enabled;
}

bool radio_loader_is_connected(void) {
    if(s_otg_enabled) {
        return true;
    }
    return true;
}

void radio_loader_end(void) {
    if(s_otg_enabled) {
        furi_hal_power_disable_otg();
        s_otg_enabled = false;
    }
}

/* ── Frequency Validation ───────────────────────────────────────────────── */
/* Checks if a frequency falls within the unlocked region bands.             */
/* Band 1: ~300-348 MHz, Band 2: ~387-464 MHz, Band 3: ~779-928 MHz         */

bool radio_loader_freq_valid(float freq_mhz) {
    return (freq_mhz >= 300.0f && freq_mhz <= 348.0f) ||
           (freq_mhz >= 387.0f && freq_mhz <= 464.0f) ||
           (freq_mhz >= 779.0f && freq_mhz <= 928.0f);
}

/* ── Region Unlock ──────────────────────────────────────────────────────── */
/* No-op on official SDK; custom firmware may override.                      */

bool radio_loader_unlock_region(void) {
    return true;
}

/* ── PATable Reference ──────────────────────────────────────────────────── */
/* CC1101 PATable values (from CC1101 datasheet, Table 25):                  */
/* 12dBm=0xC0, 10dBm=0xC5, 7dBm=0xCD, 5dBm=0x86, 0dBm=0x50,                */
/* -6dBm=0x37, -10dBm=0x26, -15dBm=0x1D, -20dBm=0x17, -30dBm=0x03          */

const uint8_t* radio_loader_patable_reference(int* out_count) {
    static const uint8_t patable[] = {
        0xC0,  /* 12 dBm (max TX power) */
        0xC5,  /* 10 dBm */
        0xCD,  /* 7 dBm */
        0x86,  /* 5 dBm */
        0x50,  /* 0 dBm */
        0x37,  /* -6 dBm */
        0x26,  /* -10 dBm */
        0x1D,  /* -15 dBm */
        0x17,  /* -20 dBm */
        0x03,  /* -30 dBm (min TX power) */
    };
    if(out_count) *out_count = 10;
    return patable;
}
