#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/* Runtime KeeLoq vault — user-added 64-bit device keys, editable from FOBLoq (on-device) and from the dashboard ("key_add" / "key_del"), persisted to SD (/flipper_fobscan/keys.txt) and hot-reloaded into kl_recover_key()'s sweep so a newly learned key decrypts every later capture. keys.txt format (one key per line, '#' comments / blanks ignored): NAME HEXKEY; NAME 1..31 chars, [A-Za-z0-9_-]; HEXKEY exactly 16 hex digits (64-bit, most significant first). Pure module: no furi / HAL / storage dependency, host-testable. */
#define FLIPPER_KEYVAULT_MAX 16   /* ~64 B/entry RAM */

typedef struct {
    char     name[32];   /* label, e.g. "MYOEM-2024" */
    uint64_t key;        /* 64-bit device key */
    bool     used;
} FlipperVaultKey;

/* Number of populated slots in a keys[0..FLIPPER_KEYVAULT_MAX-1] array. */
size_t flipper_keyvault_count(const FlipperVaultKey* keys);

const FlipperVaultKey* flipper_keyvault_find(const FlipperVaultKey* keys, const char* name);

/* I insert or replace a named key (same name → new value wins). Name trimmed to 31 chars, charset-validated; false if invalid or full. */
bool flipper_keyvault_upsert(FlipperVaultKey* keys, const char* name, uint64_t key);

bool flipper_keyvault_delete(FlipperVaultKey* keys, const char* name);

/* ── keys.txt (de)serialization ──────────────────────────────────────────── */
/* I render the whole vault to keys.txt contents. I return bytes written (excl. NUL), 0 on overflow. */
size_t flipper_keyvault_to_text(const FlipperVaultKey* keys, char* out, size_t n);

/* I parse keys.txt contents. I replace the vault (I do not merge). Blank, comment, and malformed lines are skipped. count* receives unique keys loaded (≤ max). false only on structural errors. */
bool flipper_keyvault_from_text(const char* text, FlipperVaultKey* keys, int max, int* count);
