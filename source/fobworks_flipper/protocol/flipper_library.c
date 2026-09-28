#include "flipper_library.h"
#include "flipper_library_rules.h"
#include "../flipper_fobscan_app.h"   /* FLIPPER_LIB_PATH */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* Storage locations beneath FLIPPER_LIB_PATH. */
#define LIB_DECODED_DIR FLIPPER_LIB_PATH "/decoded"
#define LIB_RAW_DIR     FLIPPER_LIB_PATH "/raw"

static FlipperLibError s_last_error = FlipperLibErrorNone;
static bool s_storage_ready;

static const char* lib_dir(bool decoded) {
    return decoded ? LIB_DECODED_DIR : LIB_RAW_DIR;
}

static bool lib_digits(const char* p, const char** end) {
    const char* s = p;
    while(*p >= '0' && *p <= '9') p++;
    if(p == s) return false;
    if(end) *end = p;
    return true;
}

static bool lib_generated_tmp_name(const char* name, bool decoded) {
    size_t n = name ? strlen(name) : 0;
    if(n < 10 || strcmp(name + n - 4, ".tmp") != 0) return false;
    char base[FLIPPER_LIB_NAME_MAX + 1];
    size_t blen = n - 4;
    if(blen > FLIPPER_LIB_NAME_MAX) return false;
    memcpy(base, name, blen);
    base[blen] = '\0';
    if(!decoded) {
        if(strncmp(base, "raw_", 4) != 0) return false;
        const char* p = base + 4;
        if(!lib_digits(p, &p) || *p++ != '_' || !lib_digits(p, &p) || *p)
            return false;
        return true;
    }
    const char* c = strrchr(base, '_');
    if(c && c[1] != 'c') c = NULL;
    if(!c || c == base || c[2] == '\0') return false;
    for(const char* p = base; p < c; p++) {
        char ch = *p;
        if(!((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
             (ch >= '0' && ch <= '9') || ch == '_' || ch == '-'))
            return false;
    }
    const char* p = c + 2;
    if(!lib_digits(p, &p) || *p) return false;
    const char* underscore = NULL;
    for(const char* u = base; u < c; u++)
        if(*u == '_') underscore = u;
    if(!underscore) return false;
    for(const char* h = underscore + 1; h < c; h++) {
        char ch = *h;
        if(!((ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'F')))
            return false;
    }
    return c - (underscore + 1) == 8;
}

static bool lib_exact_sub_name(const char* name) {
    size_t n = name ? strlen(name) : 0;
    return n > 4 && strcmp(name + n - 4, ".sub") == 0;
}

static bool lib_dir_ready(Storage* storage, const char* path) {
    File* f = storage_file_alloc(storage);
    if(!f) return false;
    bool ok = storage_dir_open(f, path);
    storage_dir_close(f);
    storage_file_free(f);
    return ok;
}

bool flipper_lib_valid_name(const char* name) {
    return flipper_lib_name_is_safe(name);
}

/* The Flipper firmware API does not provide atol(), so parse decimal fields
   locally. Accept leading whitespace and an optional sign; stop at the first
   non-digit. */
static long lib_atol(const char* s) {
    if(!s) return 0;
    while(*s == ' ' || *s == '\t') s++;
    int neg = 0;
    if(*s == '-') { neg = 1; s++; }
    else if(*s == '+') s++;
    long v = 0;
    while(*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0');
    return neg ? -v : v;
}

/* ── Preset <-> .sub preset string ────────────────────────────────────────── */
static const char* preset_to_str(FlipperPreset p) {
    switch(p) {
    case FlipperPresetOOK270:     return "FuriHalSubGhzPresetOok270Async";
    case FlipperPreset2FSKDev238: return "FuriHalSubGhzPreset2FSKDev238Async";
    case FlipperPreset2FSKDev476: return "FuriHalSubGhzPreset2FSKDev476Async";
    case FlipperPresetOOK650:
    default:                      return "FuriHalSubGhzPresetOok650Async";
    }
}

static FlipperPreset preset_from_str(const char* s) {
    if(strstr(s, "Ook270"))     return FlipperPresetOOK270;
    if(strstr(s, "2FSKDev238")) return FlipperPreset2FSKDev238;
    if(strstr(s, "2FSKDev476")) return FlipperPreset2FSKDev476;
    return FlipperPresetOOK650;
}

/* ── Init ─────────────────────────────────────────────────────────────────── */
void flipper_lib_init(Storage* storage) {
    s_last_error = FlipperLibErrorNone;
    s_storage_ready = false;
    if(!storage) {
        s_last_error = FlipperLibErrorSdAbsent;
        return;
    }
    /* storage_simply_mkdir() creates only the leaf directory. Create each
       level in order so the /ext parent exists before the library folders;
       otherwise saves fail when opening their files. */
    storage_simply_mkdir(storage, EXT_PATH("flipper_fobscan"));
    storage_simply_mkdir(storage, FLIPPER_LIB_PATH);
    storage_simply_mkdir(storage, LIB_DECODED_DIR);
    storage_simply_mkdir(storage, LIB_RAW_DIR);
    bool ok = lib_dir_ready(storage, EXT_PATH("flipper_fobscan")) &&
              lib_dir_ready(storage, FLIPPER_LIB_PATH) &&
              lib_dir_ready(storage, LIB_DECODED_DIR) &&
              lib_dir_ready(storage, LIB_RAW_DIR);
    if(!ok) {
        s_last_error = FlipperLibErrorSdAbsent;
        return;
    }
    s_storage_ready = true;

    /* A reset or power loss may leave a temporary file that was never
       published. Remove only files matching our generated .tmp names, not
       saved captures or unrelated files. */
    for(int cat = 0; cat < 2; cat++) {
        File* dir = storage_file_alloc(storage);
        if(!dir) {
            s_last_error = FlipperLibErrorOom;
            s_storage_ready = false;
            return;
        }
        if(storage_dir_open(dir, lib_dir(cat == 0))) {
            char name[FLIPPER_LIB_NAME_MAX + 8];
            FileInfo info;
            while(storage_dir_read(dir, &info, name, sizeof(name))) {
                if(!(info.flags & FSF_DIRECTORY) &&
                   lib_generated_tmp_name(name, cat == 0)) {
                    char path[128];
                    snprintf(path, sizeof(path), "%s/%s",
                             lib_dir(cat == 0), name);
                    storage_common_remove(storage, path);
                }
            }
        }
        storage_dir_close(dir);
        storage_file_free(dir);
    }
}

FlipperLibError flipper_lib_last_error(void) {
    return s_last_error;
}

const char* flipper_lib_error_name(FlipperLibError error) {
    switch(error) {
    case FlipperLibErrorSdAbsent: return "sd-absent";
    case FlipperLibErrorSdFull: return "sd-full";
    case FlipperLibErrorCorrupt: return "storage-corrupt";
    case FlipperLibErrorWrite: return "storage-write";
    case FlipperLibErrorNotFound: return "not-found";
    case FlipperLibErrorOom: return "out-of-memory";
    case FlipperLibErrorOversized: return "oversized";
    case FlipperLibErrorNone:
    default: return "storage-error";
    }
}

/* ── Count / list ─────────────────────────────────────────────────────────── */
int flipper_lib_count(Storage* storage, bool decoded) {
    if(!storage || !s_storage_ready) return 0;
    File* dir = storage_file_alloc(storage);
    if(!dir) {
        s_last_error = FlipperLibErrorOom;
        return 0;
    }
    int n = 0;
    if(storage_dir_open(dir, lib_dir(decoded))) {
        char name[FLIPPER_LIB_NAME_MAX + 8];
        FileInfo info;
        while(storage_dir_read(dir, &info, name, sizeof(name))) {
            if(info.flags & FSF_DIRECTORY) continue;
            char* ext = strstr(name, ".sub");
            if(ext && ext[4] == '\0') {
                *ext = '\0';
                if(flipper_lib_valid_name(name) && n < FLIPPER_LIB_LIST_MAX) n++;
            }
        }
    }
    storage_dir_close(dir);
    storage_file_free(dir);
    return n;
}

int flipper_lib_list(Storage* storage, bool decoded, FlipperLibEntry* out, int max) {
    return flipper_lib_list_page(storage, decoded, out, max, 0);
}

int flipper_lib_list_page(
    Storage* storage, bool decoded, FlipperLibEntry* out, int max, int offset) {
    if(!storage || !out || max <= 0) return 0;
    if(offset < 0) offset = 0;
    if(offset >= FLIPPER_LIB_LIST_MAX) return 0;
    File* dir = storage_file_alloc(storage);
    if(!dir) {
        s_last_error = FlipperLibErrorOom;
        return 0;
    }
    int n = 0;
    int skipped = 0;
    if(storage_dir_open(dir, lib_dir(decoded))) {
        char name[FLIPPER_LIB_NAME_MAX + 8];
        FileInfo info;
        while(n < max && skipped < FLIPPER_LIB_LIST_MAX &&
              storage_dir_read(dir, &info, name, sizeof(name))) {
            if(info.flags & FSF_DIRECTORY) continue;
            char* ext = strstr(name, ".sub");
            if(!ext || ext[4] != '\0') continue;
            if(!flipper_lib_valid_name(name)) {
                *ext = '\0';
                if(!flipper_lib_valid_name(name)) continue;
            }
            if(skipped++ < offset) continue;
            *ext = '\0';   /* strip extension for display */
            strncpy(out[n].name, name, FLIPPER_LIB_NAME_MAX - 1);
            out[n].name[FLIPPER_LIB_NAME_MAX - 1] = '\0';
            n++;
        }
    }
    storage_dir_close(dir);
    storage_file_free(dir);
    return n;
}

/* ── Filename builders ────────────────────────────────────────────────────── */
static void sanitize(char* s) {
    for(; *s; s++) {
        char c = *s;
        bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                  (c >= '0' && c <= '9') || c == '_' || c == '-';
        if(!ok) *s = '_';
    }
}

static void build_name(const FlipperCaptureResult* cap, bool decoded, char* out, size_t n) {
    if(decoded && cap->decode.proto[0]) {
        char proto[24];
        strncpy(proto, cap->decode.proto, sizeof(proto) - 1);
        proto[sizeof(proto) - 1] = '\0';
        sanitize(proto);
        snprintf(out, n, "%s_%08lX_c%lu", proto,
                 (unsigned long)cap->decode.addr,
                 (unsigned long)cap->decode.cnt);
    } else {
        snprintf(out, n, "raw_%lu_%lu",
                 (unsigned long)(cap->pulses.freq_mhz * 100.0f),
                 (unsigned long)cap->timestamp_ms);
    }
}

/* ── Eviction ─────────────────────────────────────────────────────────────── */
/* Choose a repeatable eviction victim by exact filename. Firmware storage
   metadata does not provide stable wall-clock ordering, and capture ticks
   reset at reboot, so this does not claim to remove the oldest capture. It
   selects the lexicographically smallest valid generated name and ignores
   suffixes such as ".sub.bak". */
static bool lib_evict_deterministic(Storage* storage, bool decoded) {
    File* dir = storage_file_alloc(storage);
    if(!dir) {
        s_last_error = FlipperLibErrorOom;
        return false;
    }
    char name[FLIPPER_LIB_NAME_MAX + 8];
    char victim[FLIPPER_LIB_NAME_MAX + 8];
    victim[0] = '\0';
    FileInfo info;
    if(storage_dir_open(dir, lib_dir(decoded))) {
        while(storage_dir_read(dir, &info, name, sizeof(name))) {
            if(info.flags & FSF_DIRECTORY) continue;
            if(!lib_exact_sub_name(name)) continue;
            char base[FLIPPER_LIB_NAME_MAX + 1];
            size_t blen = strlen(name) - 4;
            memcpy(base, name, blen);
            base[blen] = '\0';
            if(!flipper_lib_valid_name(base)) continue;
            if(!victim[0] ||
               flipper_lib_eviction_name_before(name, victim)) {
                strncpy(victim, name, sizeof(victim) - 1);
                victim[sizeof(victim) - 1] = '\0';
            }
        }
    }
    storage_dir_close(dir);
    storage_file_free(dir);
    if(!victim[0]) return false;

    char path[128];
    snprintf(path, sizeof(path), "%s/%s", lib_dir(decoded), victim);  /* victim keeps .sub */
    return storage_common_remove(storage, path) == FSE_OK;
}

/* ── Save ─────────────────────────────────────────────────────────────────── */
bool flipper_lib_save(Storage* storage, const FlipperCaptureResult* cap,
                      FlipperPreset preset, bool decoded, bool evict_oldest,
                      char* out_name) {
    if(out_name) out_name[0] = '\0';
    s_last_error = FlipperLibErrorNone;
    if(!storage || !s_storage_ready) {
        s_last_error = FlipperLibErrorSdAbsent;
        return false;
    }
    if(!cap || cap->pulses.len < 2 || cap->pulses.len > FLIPPER_PULSE_MAX) {
        s_last_error = FlipperLibErrorCorrupt;
        return false;
    }

    char base[FLIPPER_LIB_NAME_MAX];
    build_name(cap, decoded, base, sizeof(base));

    char path[128];
    snprintf(path, sizeof(path), "%s/%s.sub", lib_dir(decoded), base);

    /* The saved name is valid whether we write fresh or hit the dedup path. */
    if(out_name) {
        strncpy(out_name, base, FLIPPER_LIB_NAME_MAX - 1);
        out_name[FLIPPER_LIB_NAME_MAX - 1] = '\0';
    }

    /* Check for an identical saved frame before capacity. A duplicate needs no
       new slot and must not trigger eviction. */
    if(storage_file_exists(storage, path)) return true;

    /* This is a new file. If the category is full, either remove the
       deterministic-name victim when requested or report that storage is full. */
    if(flipper_lib_count(storage, decoded) >= FLIPPER_LIB_LIST_MAX) {
        if(!evict_oldest) {
            s_last_error = FlipperLibErrorSdFull;
            return false;
        }
        int guard = 0;
        while(flipper_lib_count(storage, decoded) >= FLIPPER_LIB_LIST_MAX) {
            if(!lib_evict_deterministic(storage, decoded)) {
                if(s_last_error == FlipperLibErrorNone)
                    s_last_error = FlipperLibErrorSdFull;
                return false;  /* nothing to drop */
            }
            if(++guard > FLIPPER_LIB_LIST_MAX + 4) {
                s_last_error = FlipperLibErrorSdFull;
                return false;  /* safety net */
            }
        }
    }

    /* Write to a temporary path and publish it by rename only after the write
       completes. An interrupted or SD-full write then cannot leave a partial
       .sub file that a later dedup check would mistake for a complete capture. */
    char tmp[136];
    snprintf(tmp, sizeof(tmp), "%s/%s.tmp", lib_dir(decoded), base);

    File* f = storage_file_alloc(storage);
    if(!f) {
        s_last_error = FlipperLibErrorOom;
        return false;
    }
    bool ok = false;
    if(storage_file_open(f, tmp, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        char hdr[256];
        uint32_t hz = (uint32_t)(cap->pulses.freq_mhz * 1e6f);
        int hn = snprintf(hdr, sizeof(hdr),
            "Filetype: Flipper SubGhz RAW File\n"
            "Version: 1\n"
            "Frequency: %lu\n"
            "Preset: %s\n"
            "Protocol: RAW\n",
            (unsigned long)hz, preset_to_str(preset));
        ok = storage_file_write(f, hdr, hn) == (size_t)hn;

        /* Write RAW_Data as signed durations, 32 values per line; index 0
           starts HIGH and therefore has a positive value. */
        const int CHUNK = 32;
        for(int i = 0; ok && i < cap->pulses.len; i += CHUNK) {
            char line[512];
            int p = snprintf(line, sizeof(line), "RAW_Data:");
            int end = i + CHUNK;
            if(end > cap->pulses.len) end = cap->pulses.len;
            for(int j = i; j < end; j++) {
                long v = (long)cap->pulses.durations[j];
                if(j & 1) v = -v;   /* odd index = LOW */
                p += snprintf(line + p, sizeof(line) - p, " %ld", v);
            }
            p += snprintf(line + p, sizeof(line) - p, "\n");
            ok = storage_file_write(f, line, p) == (size_t)p;
        }

        /* Store decode details as metadata. The stock SubGHz RAW loader ignores
           these fields; this library reads them when loading the capture. */
        if(ok && decoded) {
            char meta[256];
            int mn = snprintf(meta, sizeof(meta),
                "FT_Proto: %s\n"
                "FT_Serial: %lu\n"
                "FT_Counter: %lu\n"
                "FT_Button: %u\n"
                "FT_TE: %lu\n"
                "FT_Rolling: %d\n"
                "FT_Mfr: %s\n",
                cap->decode.proto,
                (unsigned long)cap->decode.addr,
                (unsigned long)cap->decode.cnt,
                cap->decode.btn,
                (unsigned long)cap->decode.te_us,
                cap->decode.rolling ? 1 : 0,
                cap->decode.mfr_name[0] ? cap->decode.mfr_name : "-");
            ok = storage_file_write(f, meta, mn) == (size_t)mn;
        }
    }
    else {
        s_last_error = FlipperLibErrorSdFull;
    }
    storage_file_close(f);
    storage_file_free(f);

    if(ok) {
        /* Publish the completed file by rename. Remove the temporary file if
           that final step fails. */
        if(storage_common_rename(storage, tmp, path) != FSE_OK) {
            storage_common_remove(storage, tmp);
            s_last_error = FlipperLibErrorWrite;
            ok = false;
        }
    } else {
        storage_common_remove(storage, tmp);
        if(s_last_error == FlipperLibErrorNone) s_last_error = FlipperLibErrorWrite;
    }
    return ok;
}

/* ── Load ─────────────────────────────────────────────────────────────────── */
static const char* find_val(const char* text, const char* key) {
    const char* p = strstr(text, key);
    if(!p) return NULL;
    p += strlen(key);
    while(*p == ' ') p++;
    return p;
}

bool flipper_lib_load_with_protocol(
    Storage* storage, bool decoded, const char* name, FlipperCaptureResult* cap,
    FlipperPreset* preset, char* protocol, size_t protocol_size) {
    if(protocol && protocol_size) protocol[0] = '\0';
    s_last_error = FlipperLibErrorNone;
    if(!storage || !s_storage_ready) {
        s_last_error = FlipperLibErrorSdAbsent;
        return false;
    }
    if(!flipper_lib_valid_name(name) || !cap) {
        s_last_error = FlipperLibErrorCorrupt;
        return false;
    }

    char path[128];
    snprintf(path, sizeof(path), "%s/%s.sub", lib_dir(decoded), name);

    File* f = storage_file_alloc(storage);
    if(!f) {
        s_last_error = FlipperLibErrorOom;
        return false;
    }
    bool ok = false;
    if(storage_file_open(f, path, FSAM_READ, FSOM_OPEN_EXISTING)) {
        uint64_t sz = storage_file_size(f);
        if(sz > 0 && sz < 32768) {
            char* text = malloc((size_t)sz + 1);
            if(text) {
                size_t rd = storage_file_read(f, text, (size_t)sz);
                text[rd] = '\0';

                memset(cap, 0, sizeof(*cap));

                const char* fv = find_val(text, "Frequency:");
                float freq = fv ? (float)(lib_atol(fv) / 1e6) : 433.92f;
                cap->pulses.freq_mhz = freq;
                cap->decode.freq_mhz = freq;

                const char* pv = find_val(text, "Preset:");
                if(preset) *preset = pv ? preset_from_str(pv) : FlipperPresetOOK650;
                const char* proto = find_val(text, "Protocol:");
                if(protocol && protocol_size && proto) {
                    size_t i = 0;
                    while(proto[i] && proto[i] != '\n' && proto[i] != '\r' &&
                          i + 1 < protocol_size) {
                        protocol[i] = proto[i];
                        i++;
                    }
                    protocol[i] = '\0';
                }

                /* Read all RAW_Data lines as absolute durations. SD contents
                   are user-controlled, so bound the parser and ensure every
                   iteration advances; malformed tokens must not stall the UI. */
                int len = 0;
                const char* end_text = text + rd;
                const char* q = text;
                while((q = strstr(q, "RAW_Data:")) && len < FLIPPER_PULSE_MAX) {
                    q += 9;
                    const char* eol = strchr(q, '\n');
                    const char* stop = eol ? eol : end_text;
                    while(q < stop && len < FLIPPER_PULSE_MAX) {
                        while(q < stop && (*q == ' ' || *q == '\t' || *q == '\r')) q++;
                        if(q >= stop) break;
                        char* tend = NULL;
                        long v = strtol(q, &tend, 10);
                        if(tend == q || tend > stop) break;  /* no progress / overran */
                        q = tend;
                        if(v < 0) v = -v;
                        if(v > 0 && v < 1000000) cap->pulses.durations[len++] = (uint32_t)v;
                    }
                    q = eol ? eol + 1 : stop;
                }
                cap->pulses.len = len;

                if(decoded) {
                    const char* v;
                    if((v = find_val(text, "FT_Proto:"))) {
                        int i = 0;
                        while(v[i] && v[i] != '\n' && i < 31) { cap->decode.proto[i] = v[i]; i++; }
                        cap->decode.proto[i] = '\0';
                    }
                    if((v = find_val(text, "FT_Serial:")))  cap->decode.addr = (uint32_t)lib_atol(v);
                    if((v = find_val(text, "FT_Counter:"))) cap->decode.cnt  = (uint32_t)lib_atol(v);
                    if((v = find_val(text, "FT_Button:")))  cap->decode.btn  = (uint8_t)atoi(v);
                    if((v = find_val(text, "FT_TE:")))      cap->decode.te_us = (uint32_t)lib_atol(v);
                    if((v = find_val(text, "FT_Rolling:"))) cap->decode.rolling = atoi(v) != 0;
                    if((v = find_val(text, "FT_Mfr:"))) {
                        int i = 0;
                        while(v[i] && v[i] != '\n' && i < 31) { cap->decode.mfr_name[i] = v[i]; i++; }
                        cap->decode.mfr_name[i] = '\0';
                        if(cap->decode.mfr_name[0] == '-' && cap->decode.mfr_name[1] == '\0')
                            cap->decode.mfr_name[0] = '\0';
                    }
                    cap->decode_ok = cap->decode.proto[0] != '\0';
                }
                cap->pulses.te_us = cap->decode.te_us;
                ok = cap->pulses.len >= 2;
                if(!ok) s_last_error = FlipperLibErrorCorrupt;
                free(text);
            }
            else s_last_error = FlipperLibErrorOom;
        } else {
            s_last_error = FlipperLibErrorOversized;
        }
    } else {
        s_last_error = FlipperLibErrorNotFound;
    }
    storage_file_close(f);
    storage_file_free(f);
    return ok;
}

bool flipper_lib_load(Storage* storage, bool decoded, const char* name,
                      FlipperCaptureResult* cap, FlipperPreset* preset) {
    return flipper_lib_load_with_protocol(
        storage, decoded, name, cap, preset, NULL, 0);
}

/* ── Delete ───────────────────────────────────────────────────────────────── */
bool flipper_lib_delete(Storage* storage, bool decoded, const char* name) {
    if(!storage || !flipper_lib_valid_name(name)) {
        s_last_error = FlipperLibErrorCorrupt;
        return false;
    }
    char path[128];
    snprintf(path, sizeof(path), "%s/%s.sub", lib_dir(decoded), name);
    FS_Error e = storage_common_remove(storage, path);
    s_last_error = e == FSE_OK ? FlipperLibErrorNone : FlipperLibErrorNotFound;
    return e == FSE_OK;
}

/* ── Export to stock SubGHz "Saved" ───────────────────────────────────────── */
bool flipper_lib_export_subghz(Storage* storage, bool decoded, const char* name) {
    if(!storage || !flipper_lib_valid_name(name)) {
        s_last_error = FlipperLibErrorCorrupt;
        return false;
    }
    /* Create the stock SubGHz folder if needed. These .sub files use the
       Flipper SubGHz RAW format, so the native browser can open and replay them. */
    storage_simply_mkdir(storage, EXT_PATH("subghz"));

    char src[128], dst[128];
    snprintf(src, sizeof(src), "%s/%s.sub", lib_dir(decoded), name);
    snprintf(dst, sizeof(dst), "%s/%s.sub", EXT_PATH("subghz"), name);

    FS_Error e = storage_common_copy(storage, src, dst);
    s_last_error = (e == FSE_OK || e == FSE_EXIST) ? FlipperLibErrorNone :
        FlipperLibErrorWrite;
    return e == FSE_OK || e == FSE_EXIST;
}
