#pragma once
#include <stdint.h>
#include <stdbool.h>

/* ─────────────────────────────────────────────────────────────────────────── */
/* Radio Device Loader + Region Unlock — FOBworks custom implementation.    */
/*   Internal/external CC1101 abstraction with OTG power management.          */
/*   Region unlock pattern for full SubGHz spectrum access.                   */
/* ─────────────────────────────────────────────────────────────────────────── */

typedef enum {
    RadioDeviceInternal,     /* Built-in CC1101 */
    RadioDeviceExternal,     /* External CC1101 via GPIO (e.g., Rabbit Labs) */
} RadioDeviceType;

/* Apply region unlock (3 bands at 20dBm, 50% duty cycle). */
bool radio_loader_unlock_region(void);

/* Switch between internal and external CC1101. */
bool radio_loader_set(RadioDeviceType type);
bool radio_loader_is_external(void);
bool radio_loader_is_connected(void);
void radio_loader_end(void);

/* Check if frequency is within unlocked region. */
bool radio_loader_freq_valid(float freq_mhz);

/* PATable reference values (10 entries, 12dBm to -30dBm). */
const uint8_t* radio_loader_patable_reference(int* out_count);
