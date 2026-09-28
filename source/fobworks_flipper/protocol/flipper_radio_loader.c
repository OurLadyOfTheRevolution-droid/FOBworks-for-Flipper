#include "flipper_radio_loader.h"
#include <furi.h>
#include <furi_hal_power.h>
#include <furi_hal_subghz.h>
#include <string.h>

/* ─────────────────────────────────────────────────────────────────────────── */
/* Selects the internal or OTG-powered external CC1101 and checks the
   frequency bands used by this application. The region-unlock hook is a no-op
   on the official SDK; custom firmware may provide its own implementation. */
/* ─────────────────────────────────────────────────────────────────────────── */

/* Select the internal radio or power an external CC1101 module (for example,
   a Rabbit Labs board) through OTG. */

static bool s_otg_enabled = false;

bool radio_loader_set(RadioDeviceType type) {
    if(type == RadioDeviceInternal) {
        return true;
    }

    /* Enable OTG power for the external CC1101, retrying transient failures. */
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

/* Accept only the three frequency ranges supported by this application:
   300–348 MHz, 387–464 MHz, and 779–928 MHz. */

bool radio_loader_freq_valid(float freq_mhz) {
    return (freq_mhz >= 300.0f && freq_mhz <= 348.0f) ||
           (freq_mhz >= 387.0f && freq_mhz <= 464.0f) ||
           (freq_mhz >= 779.0f && freq_mhz <= 928.0f);
}

/* The official SDK does not expose a region-unlock operation here. This
   implementation succeeds without changing the SDK's region settings. */

bool radio_loader_unlock_region(void) {
    return true;
}

/* Reference values from the CC1101 datasheet, Table 25. */

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
