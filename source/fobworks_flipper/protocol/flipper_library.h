#pragma once
#include <furi.h>
#include <stddef.h>
#include <storage/storage.h>
#include "flipper_capture.h"
#include "flipper_decoders.h"

/* ── On-device signal library ─────────────────────────────────────────────────
 * Persists captures to the SD card as Flipper-compatible SubGHz RAW (.sub)
 * files, so saved signals can also be opened/replayed by the stock SubGHz app
 * and copied off via qFlipper (that IS the "export" function).  Two categories,
 * mirroring the web dashboard library split:
 *   - decoded/ : captures that decoded to a known protocol (metadata embedded)
 *   - raw/     : undecoded OOK/FSK bursts (RAW only)
 * The browser mimics the native SubGHz "Saved" list: a list of file names,
 * select one to Send (replay) / Info / Recover key / Delete.
 */

#define FLIPPER_LIB_NAME_MAX 48
#define FLIPPER_LIB_LIST_MAX 16   /* per-category cap for BOTH the RAM browse cache and the on-SD save count;
                                      offload archival signals via "Export to SubGHz" (stock Saved browser) */

typedef struct FlipperLibEntry {
    char name[FLIPPER_LIB_NAME_MAX];   /* file name without .sub extension */
} FlipperLibEntry;

typedef enum {
    FlipperLibErrorNone = 0,
    FlipperLibErrorSdAbsent,
    FlipperLibErrorSdFull,
    FlipperLibErrorCorrupt,
    FlipperLibErrorWrite,
    FlipperLibErrorNotFound,
    FlipperLibErrorOom,
    FlipperLibErrorOversized,
} FlipperLibError;

/* Remote callers must pass a single library entry name, never a path. */
bool flipper_lib_valid_name(const char* name);

/* Create the library directory tree.  Safe to call repeatedly. */
void flipper_lib_init(Storage* storage);
FlipperLibError flipper_lib_last_error(void);
const char* flipper_lib_error_name(FlipperLibError error);

/* Number of saved signals in a category (decoded=true → decoded/, else raw/). */
int flipper_lib_count(Storage* storage, bool decoded);

/* Save one capture.  When decoded, protocol metadata (proto/serial/counter/…)
 * is embedded so a later load restores the decode without re-parsing.
 * Returns true on success (or when a same-named file already exists / dedup),
 * false on I/O error or when the category is full.
 * out_name (nullable) receives the saved file name without .sub extension;
 * the buffer must hold at least FLIPPER_LIB_NAME_MAX bytes. */
bool flipper_lib_save(Storage* storage, const FlipperCaptureResult* cap,
                      FlipperPreset preset, bool decoded, bool evict_oldest,
                      char* out_name);

/* Fill out[] with up to max entries (newest listing order is filesystem order).
 * Returns the number of entries written. */
int flipper_lib_list(Storage* storage, bool decoded, FlipperLibEntry* out, int max);

/* Bounded page of the listing. offset is in enumeration order. */
int flipper_lib_list_page(
    Storage* storage, bool decoded, FlipperLibEntry* out, int max, int offset);

/* Load a saved signal by name (without .sub) into cap + preset.
 * Reconstructs pulses (for replay) and, for decoded files, the decode result. */
bool flipper_lib_load(Storage* storage, bool decoded, const char* name,
                      FlipperCaptureResult* cap, FlipperPreset* preset);

/* Load a saved signal and return its on-disk Protocol: label (bounded).
 * Existing callers that do not need the label can use flipper_lib_load(). */
bool flipper_lib_load_with_protocol(
    Storage* storage, bool decoded, const char* name, FlipperCaptureResult* cap,
    FlipperPreset* preset, char* protocol, size_t protocol_size);

/* Delete a saved signal by name (without .sub). */
bool flipper_lib_delete(Storage* storage, bool decoded, const char* name);

/* Copy a saved signal into the stock SubGHz app's Saved folder (/ext/subghz)
 * so it shows up in the native SubGHz "Saved" browser (and survives trimming
 * this app's small library).  Non-destructive: the source .sub is left in place.
 * Returns true on success or when the file is already present there. */
bool flipper_lib_export_subghz(Storage* storage, bool decoded, const char* name);
