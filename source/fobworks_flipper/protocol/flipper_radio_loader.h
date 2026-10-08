#pragma once
#include <stdint.h>
#include <stdbool.h>

/* ─────────────────────────────────────────────────────────────────────────── */
/* I select an internal or OTG-powered external CC1101 and provide frequency
   checks plus a region-unlock hook. The hook is a no-op on the official SDK. */
/* ─────────────────────────────────────────────────────────────────────────── */

typedef enum {
    RadioDeviceInternal,     /* Built-in CC1101 */
    RadioDeviceExternal,     /* External CC1101 via GPIO (e.g., Rabbit Labs) */
} RadioDeviceType;

/* Region-unlock hook. The official SDK implementation does not change region
   settings; custom firmware may override it. */
bool radio_loader_unlock_region(void);

/* I select the internal or external CC1101. */
bool radio_loader_set(RadioDeviceType type);
bool radio_loader_is_external(void);
bool radio_loader_is_connected(void);
void radio_loader_end(void);

/* True for CC1101 VERSION status values 0x04 and 0x14 (SGP / datasheet). */
bool radio_loader_chip_version_ok(uint8_t version);

/* I check whether a frequency is within one of the supported bands. */
bool radio_loader_freq_valid(float freq_mhz);

/* I return the 10-entry CC1101 PATable reference, from 12 dBm to -30 dBm. */
const uint8_t* radio_loader_patable_reference(int* out_count);
