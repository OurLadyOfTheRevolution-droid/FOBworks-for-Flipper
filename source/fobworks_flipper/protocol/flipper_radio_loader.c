#include "flipper_radio_loader.h"
#include <furi.h>
#include <furi_hal_power.h>
#include <lib/subghz/devices/devices.h>
#include <applications/drivers/subghz/cc1101_ext/cc1101_ext_interconnect.h>
#include <string.h>

/* I select the internal or OTG-powered external CC1101. Version acceptance
   matches the SGP Card Mini probe (CC1101 VERSION status 0x04 or 0x14).
   External presence uses the official cc1101_ext interconnect's is_connect
   path (SPI VERSION under the hood) after OTG is up. */

static bool s_otg_enabled = false;

bool radio_loader_chip_version_ok(uint8_t version) {
    return version == 0x04u || version == 0x14u;
}

bool radio_loader_set(RadioDeviceType type) {
    if(type == RadioDeviceInternal) {
        if(s_otg_enabled) {
            furi_hal_power_disable_otg();
            s_otg_enabled = false;
        }
        return true;
    }

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

static bool radio_loader_probe_external(void) {
    /* OTG rail alone is not proof — I ask the external CC1101 driver whether
       SPI VERSION looks like a live chip (0x04 / 0x14). */
    subghz_devices_init();
    const SubGhzDevice* dev = subghz_devices_get_by_name(SUBGHZ_DEVICE_CC1101_EXT_NAME);
    bool ok = false;
    if(dev) {
        if(subghz_devices_begin(dev)) {
            ok = subghz_devices_is_connect(dev);
            subghz_devices_end(dev);
        } else {
            /* begin() failed; is_connect may still report a VERSION hit. */
            ok = subghz_devices_is_connect(dev);
        }
    }
    subghz_devices_deinit();
    return ok;
}

bool radio_loader_is_connected(void) {
    if(!s_otg_enabled) return true; /* internal CC1101 */
    return radio_loader_probe_external();
}

void radio_loader_end(void) {
    if(s_otg_enabled) {
        furi_hal_power_disable_otg();
        s_otg_enabled = false;
    }
}

bool radio_loader_freq_valid(float freq_mhz) {
    return (freq_mhz >= 300.0f && freq_mhz <= 348.0f) ||
           (freq_mhz >= 387.0f && freq_mhz <= 464.0f) ||
           (freq_mhz >= 779.0f && freq_mhz <= 928.0f);
}

bool radio_loader_unlock_region(void) {
    return true;
}

const uint8_t* radio_loader_patable_reference(int* out_count) {
    static const uint8_t patable[] = {
        0xC0, 0xC5, 0xCD, 0x86, 0x50, 0x37, 0x26, 0x1D, 0x17, 0x03,
    };
    if(out_count) *out_count = 10;
    return patable;
}
