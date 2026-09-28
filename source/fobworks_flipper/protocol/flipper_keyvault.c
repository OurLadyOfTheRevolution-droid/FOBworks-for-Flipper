#include "flipper_keyvault.h"
#include <string.h>
#include <stdio.h>

#define KEY_FILE_HEADER \
    "# FOBworks KeeLoq vault — one key per line: NAME HEXKEY (16 hex digits)\n"

size_t flipper_keyvault_count(const FlipperVaultKey* keys) {
    if(!keys) return 0;
    size_t n = 0;
    for(size_t i = 0; i < FLIPPER_KEYVAULT_MAX; i++)
        if(keys[i].used) n++;
    return n;
}

const FlipperVaultKey* flipper_keyvault_find(const FlipperVaultKey* keys, const char* name) {
    if(!keys || !name || !name[0]) return NULL;
    for(size_t i = 0; i < FLIPPER_KEYVAULT_MAX; i++) {
        if(keys[i].used && strcmp(keys[i].name, name) == 0) return &keys[i];
    }
    return NULL;
}

static bool kv_name_ok(const char* s, size_t n) {
    if(n == 0) return false;
    for(size_t i = 0; i < n; i++) {
        char c = s[i];
        if(!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
             (c >= '0' && c <= '9') || c == '_' || c == '-'))
            return false;
    }
    return true;
}

static int kv_hex_nibble(char c) {
    if(c >= '0' && c <= '9') return c - '0';
    if(c >= 'a' && c <= 'f') return c - 'a' + 10;
    if(c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool flipper_keyvault_upsert(FlipperVaultKey* keys, const char* name, uint64_t key) {
    if(!keys || !name || !name[0]) return false;
    size_t n = strlen(name);
    if(n > 31) n = 31;
    if(!kv_name_ok(name, n)) return false;

    /* Reuse a matching entry when found; otherwise take a free slot. */
    FlipperVaultKey* slot = NULL;
    for(size_t i = 0; i < FLIPPER_KEYVAULT_MAX; i++) {
        if(!keys[i].used) { slot = &keys[i]; break; }
        if(strcmp(keys[i].name, name) == 0) slot = &keys[i];
    }
    if(!slot) return false;                          /* unreachable */
    if(!slot->used && flipper_keyvault_count(keys) >= FLIPPER_KEYVAULT_MAX)
        return false;                                /* vault full */

    memset(slot->name, 0, sizeof(slot->name));
    memcpy(slot->name, name, n);
    slot->name[n] = '\0';
    slot->key  = key;
    slot->used = true;
    return true;
}

bool flipper_keyvault_delete(FlipperVaultKey* keys, const char* name) {
    if(!keys) return false;
    FlipperVaultKey* k = (FlipperVaultKey*)flipper_keyvault_find(keys, name);
    if(!k) return false;
    memset(k, 0, sizeof(*k));
    return true;
}

size_t flipper_keyvault_to_text(const FlipperVaultKey* keys, char* out, size_t n) {
    if(!keys || !out || n == 0) return 0;
    int w = snprintf(out, n, KEY_FILE_HEADER);
    if(w < 0 || (size_t)w >= n) return 0;
    for(size_t i = 0; i < FLIPPER_KEYVAULT_MAX; i++) {
        if(!keys[i].used) continue;
        size_t rem = n - (size_t)w;
        int x = snprintf(out + (size_t)w, rem, "%s %016llX\n",
                         keys[i].name, (unsigned long long)keys[i].key);
        if(x < 0 || (size_t)x >= rem) return 0;
        w += x;
    }
    return (size_t)w;
}

bool flipper_keyvault_from_text(const char* text, FlipperVaultKey* keys, int max, int* count) {
    if(!text || !keys || !count) return false;
    if(max > FLIPPER_KEYVAULT_MAX) max = FLIPPER_KEYVAULT_MAX;
    *count = 0;

    /* Work on a copy so '#' comments can be removed without changing input. */
    char work[1024];
    size_t slen = strlen(text);
    if(slen == 0 || slen >= sizeof(work)) return false;
    memcpy(work, text, slen + 1);

    /* Split lines manually because strtok_r() is unavailable in the firmware. */
    char* p = work;
    while(*p) {
        char* line = p;
        /* Locate the end of this line. */
        while(*p && *p != '\n' && *p != '\r') p++;
        char* end = p;
        /* Advance past CR/LF before processing the next line. */
        while(*p == '\n' || *p == '\r') p++;
        *end = '\0';

        char* hash = strchr(line, '#');
        if(hash) *hash = '\0';
        char* s = line;
        while(*s == ' ' || *s == '\t') s++;
        if(*s == '\0') continue;                 /* blank or comment-only line */

        /* Read the entry as NAME followed by a hexadecimal key. */
        size_t nlen = 0;
        while(s[nlen] != '\0' && s[nlen] != ' ' && s[nlen] != '\t') nlen++;
        if(!kv_name_ok(s, nlen)) continue;       /* malformed name */
        const char* hp = s + nlen;
        while(*hp == ' ' || *hp == '\t') hp++;

        int hv = -1;
        uint64_t key = 0;
        for(int k = 0; k < 16; k++) {
            hv = kv_hex_nibble(hp[k]);
            if(hv < 0) break;
            key = (key << 4) | (uint64_t)hv;
        }
        if(hv < 0) continue;                      /* fewer than 16 hex digits */
        char after = hp[16];
        if(after != '\0' && after != '\r' && after != '\n' &&
           after != ' ' && after != '\t') continue; /* trailing junk */

        /* in-place match wins; otherwise claim a free slot */
        FlipperVaultKey* slot = NULL;
        for(int i = 0; i < max; i++) {
            if(!keys[i].used) { slot = &keys[i]; break; }
            if(strcmp(keys[i].name, "") != 0 && strncmp(keys[i].name, s, nlen + 1) == 0)
                slot = &keys[i];
        }
        if(!slot) continue;                        /* vault full */
        memset(slot->name, 0, sizeof(slot->name));
        memcpy(slot->name, s, nlen);
        slot->name[nlen] = '\0';
        slot->key  = key;
        slot->used = true;
        (*count)++;
    }
    return true;
}
