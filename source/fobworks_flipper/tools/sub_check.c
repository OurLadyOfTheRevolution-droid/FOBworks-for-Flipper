/* sub_check — inspect decoder results for Flipper .sub RAW captures. Pass one or more files or directories. Directories are scanned recursively; each RAW_Data stream is split into bursts at long gaps or noise. The tool decodes every burst through Auto and, when Auto declines, the force-only decoders. It prints the first decode for each file and a protocol histogram. This host-side analysis tool takes capture paths as arguments. cc -std=c99 -I.. -o sub_check sub_check.c (protocol sources ...) ./sub_check /path/to/captures # directory, recursive ./sub_check a.sub b.sub # explicit files */
#include "../protocol/flipper_decoders.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define SUB_GAP_US  16000  /* keep ~12 ms Santa Fe / VAG syncs inside a burst; true inter-press gaps are usually >> 16 ms */
#define SUB_MIN_EDGES 32   /* bursts shorter than this are ignored */

/* ── protocol histogram ──────────────────────────────────────────────────── */
#define HIST_MAX 64
static char     hist_name[HIST_MAX][40];
static int      hist_count[HIST_MAX];
static int      hist_n = 0;
static int      g_report = 0;

#define GRP_MAX 48
static struct { char name[48]; int none, auto_n, force_n; } g_grp[GRP_MAX];
static int g_grp_n;

static void grp_add(const char* path, int kind) {
    const char* slash = strrchr(path, '/');
    char group[48] = "(files)";
    if(slash && slash != path) {
        const char* start = path;
        for(const char* p = path; p < slash; p++) if(*p == '/') start = p + 1;
        size_t n = (size_t)(slash - start);
        if(n > sizeof(group) - 1) n = sizeof(group) - 1;
        memcpy(group, start, n);
        group[n] = '\0';
    }
    int slot = -1;
    for(int i = 0; i < g_grp_n; i++)
        if(strcmp(g_grp[i].name, group) == 0) slot = i;
    if(slot < 0 && g_grp_n < GRP_MAX) {
        slot = g_grp_n++;
        memset(&g_grp[slot], 0, sizeof(g_grp[slot]));
        strncpy(g_grp[slot].name, group, sizeof(g_grp[slot].name) - 1);
    }
    if(slot < 0) return;
    if(kind == 1) g_grp[slot].auto_n++;
    else if(kind == 2) g_grp[slot].force_n++;
    else g_grp[slot].none++;
}

static void hist_add(const char* name) {
    for(int i = 0; i < hist_n; i++) {
        if(strcmp(hist_name[i], name) == 0) {
            hist_count[i]++;
            return;
        }
    }
    if(hist_n < HIST_MAX) {
        snprintf(hist_name[hist_n], sizeof(hist_name[0]), "%s", name);
        hist_count[hist_n] = 1;
        hist_n++;
    }
}

/* ── raw stream buffer (whole file, all RAW_Data lines concatenated) ──────── */
static long*  g_raw = NULL;
static size_t g_raw_len = 0, g_raw_cap = 0;

static void raw_push(long v) {
    if(g_raw_len == g_raw_cap) {
        g_raw_cap = g_raw_cap ? g_raw_cap * 2 : 4096;
        g_raw = realloc(g_raw, g_raw_cap * sizeof(long));
        if(!g_raw) {
            fprintf(stderr, "OOM\n");
            exit(1);
        }
    }
    g_raw[g_raw_len++] = v;
}

/* Parse a .sub file: fill g_raw with the RAW_Data ints, return freq in MHz. */
static float parse_sub(const char* path) {
    FILE* f = fopen(path, "r");
    if(!f) return 0.0f;
    g_raw_len = 0;
    float freq_mhz = 433.92f;

    /* Lines can be very long; read in chunks and split manually. */
    char line[1 << 16];
    while(fgets(line, sizeof(line), f)) {
        if(strncmp(line, "Frequency:", 10) == 0) {
            long hz = atol(line + 10);
            if(hz > 0) freq_mhz = (float)hz / 1e6f;
        } else if(strncmp(line, "RAW_Data:", 9) == 0) {
            const char* p = line + 9;
            while(*p) {
                while(*p && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) p++;
                if(!*p) break;
                char* end = NULL;
                long v = strtol(p, &end, 10);
                if(end == p) break;
                raw_push(v);
                p = end;
            }
        }
    }
    fclose(f);
    return freq_mhz;
}

/* Try to decode a single burst.  Returns a static protocol label or NULL. Auto wins; otherwise the first force-only decoder that claims it. *is_force is set to 1 when only a force-only decoder claimed it (Auto declined). */
static const char* decode_burst(FlipperPulseBuf* buf, int* is_force) {
    static char label[48];
    FlipperDecodeResult r;
    memset(&r, 0, sizeof(r));
    if(flipper_decode(buf, &r)) {
        snprintf(label, sizeof(label), "%s", r.proto);
        *is_force = 0;
        return label;
    }
    for(int fp = 1; fp < FlipperForceCount; fp++) {
        memset(&r, 0, sizeof(r));
        if(flipper_decode_ex(buf, &r, (FlipperForceProto)fp)) {
            snprintf(label, sizeof(label), "%s*", r.proto); /* '*' = force-only */
            *is_force = 1;
            return label;
        }
    }
    return NULL;
}

/* Segment g_raw into bursts and try to decode each.  Returns the label of the first burst that decodes (or NULL), and reports the burst count via *bursts. */
static const char* check_file(float freq_mhz, int* bursts_out, int* best_edges,
                              int* is_force_out) {
    static char result[48];
    FlipperPulseBuf buf;
    int bursts = 0;
    int have_auto = 0, have_force = 0;
    char force_lbl[48];
    int force_edges = 0;

    size_t i = 0;
    while(i < g_raw_len) {
        /* Gather one burst.  Pulses in [10 ms, 50 ms) are treated as OEM sync (BMW CAS / similar) and prepended to the following data edges. Only |dur| ≥ 50 ms ends the previous press / starts a new gather. */
        uint32_t edges[FLIPPER_PULSE_MAX];
        int8_t   levels[FLIPPER_PULSE_MAX];
        int n = 0;
        uint32_t sync[4];
        int ns = 0;

        while(i < g_raw_len) {
            long v = g_raw[i];
            long mag = v < 0 ? -v : v;
            if(mag >= 50000) {
                i++;
                break; /* inter-press silence */
            }
            if(mag >= 10000) {
                /* Leading OEM sync (BMW CAS etc.): stash and prepend once data starts. Mid-burst longs (Santa Fe ~12 ms frame sync, etc.) stay in the edge stream so CRC parsers can see them. */
                if(n == 0) {
                    if(ns < 4) sync[ns++] = (uint32_t)mag;
                    i++;
                    continue;
                }
            }
            /* First data edge: emit sync then data. */
            if(n == 0) {
                for(int s = 0; s < ns && n < FLIPPER_PULSE_MAX; s++) {
                    edges[n] = sync[s];
                    levels[n] = 1;
                    n++;
                }
                ns = 0;
            }
            if(n < FLIPPER_PULSE_MAX) {
                edges[n] = (uint32_t)mag;
                levels[n] = (v > 0) ? 1 : 0;
                n++;
                i++;
            } else {
                break;
            }
        }
        if(n < SUB_MIN_EDGES) continue;

        /* Drop leading LOW edges so durations[0] is HIGH (capture convention). */
        int start = 0;
        while(start < n && levels[start] == 0) start++;
        if(n - start < SUB_MIN_EDGES) continue;

        memset(&buf, 0, sizeof(buf));
        buf.len = n - start;
        if(buf.len > FLIPPER_PULSE_MAX) buf.len = FLIPPER_PULSE_MAX;
        for(int k = 0; k < buf.len; k++) buf.durations[k] = edges[start + k];
        buf.freq_mhz = freq_mhz;

        uint32_t te = flipper_estimate_te(buf.durations, buf.len);
        if(te == 0) {
            uint32_t mn = 0xFFFFFFFFu;
            for(int k = 0; k < buf.len; k++)
                if(buf.durations[k] < mn) mn = buf.durations[k];
            te = mn;
        }
        buf.te_us = te;

        bursts++;
        int is_force = 0;
        const char* lbl = decode_burst(&buf, &is_force);
        if(!lbl) continue;
        if(!is_force && !have_auto) {
            /* Prefer the first Auto-chain hit — it is what FOBscan would show. */
            snprintf(result, sizeof(result), "%s", lbl);
            have_auto = 1;
            if(best_edges) *best_edges = buf.len;
            if(is_force_out) *is_force_out = 0;
        } else if(is_force && !have_force && !have_auto) {
            snprintf(force_lbl, sizeof(force_lbl), "%s", lbl);
            have_force = 1;
            force_edges = buf.len;
        }
    }
    if(bursts_out) *bursts_out = bursts;
    if(have_auto) return result;
    if(have_force) {
        snprintf(result, sizeof(result), "%s", force_lbl);
        if(best_edges) *best_edges = force_edges;
        if(is_force_out) *is_force_out = 1;
        return result;
    }
    return NULL;
}

/* ── file / directory walk ───────────────────────────────────────────────── */
static int g_files = 0, g_decoded_auto = 0, g_decoded_force = 0;

static int ends_with_sub(const char* name) {
    size_t l = strlen(name);
    return l > 4 && strcasecmp(name + l - 4, ".sub") == 0;
}

static void process_one(const char* path) {
    float freq = parse_sub(path);
    if(g_raw_len == 0) return;
    g_files++;
    int bursts = 0, edges = 0, is_force = 0;
    const char* lbl = check_file(freq, &bursts, &edges, &is_force);
    const char* base = strrchr(path, '/');
    base = base ? base + 1 : path;
    if(lbl) {
        if(is_force) g_decoded_force++;
        else g_decoded_auto++;
        hist_add(is_force ? lbl : lbl);
        grp_add(path, is_force ? 2 : 1);
        if(!g_report)
            printf("  %-6s  %-30s  %s (%d edges, %.3fMHz, %d bursts)\n",
                   is_force ? "force" : "AUTO", lbl, base, edges, freq, bursts);
    } else {
        grp_add(path, 0);
        if(!g_report)
            printf("  ------  %-38s  no decode (%d bursts)\n", base, bursts);
    }
}

static void walk(const char* path) {
    struct stat st;
    if(stat(path, &st) != 0) {
        fprintf(stderr, "skip (stat failed): %s\n", path);
        return;
    }
    if(S_ISDIR(st.st_mode)) {
        DIR* d = opendir(path);
        if(!d) return;
        struct dirent* e;
        while((e = readdir(d))) {
            if(e->d_name[0] == '.') continue;
            char child[4096];
            snprintf(child, sizeof(child), "%s/%s", path, e->d_name);
            walk(child);
        }
        closedir(d);
    } else if(ends_with_sub(path)) {
        process_one(path);
    }
}

int main(int argc, char** argv) {
    if(argc < 2) {
        fprintf(stderr, "usage: %s [--report] <file-or-dir> [...]\n", argv[0]);
        return 2;
    }
    int a0 = 1;
    if(strcmp(argv[1], "--report") == 0) { g_report = 1; a0 = 2; }
    if(a0 >= argc) {
        fprintf(stderr, "usage: %s [--report] <file-or-dir> [...]\n", argv[0]);
        return 2;
    }
    for(int a = a0; a < argc; a++) walk(argv[a]);

    int total = g_decoded_auto + g_decoded_force;
    printf("\n=== summary ===\n");
    printf("files: %d\n", g_files);
    printf("  Auto-chain decoded : %d (%.1f%%)\n",
           g_decoded_auto, g_files ? 100.0 * g_decoded_auto / g_files : 0.0);
    printf("  force-only decoded : %d (%.1f%%)  [Auto declined; would NOT fire in normal use]\n",
           g_decoded_force, g_files ? 100.0 * g_decoded_force / g_files : 0.0);
    printf("  no decode          : %d (%.1f%%)\n",
           g_files - total, g_files ? 100.0 * (g_files - total) / g_files : 0.0);
    printf("protocol histogram (* = force-only):\n");
    for(int i = 0; i < hist_n; i++)
        printf("  %-28s %d\n", hist_name[i], hist_count[i]);
    if(g_report) {
        printf("\n=== folder buckets (no serials, no counters) ===\n");
        printf("%-40s %6s %6s %6s\n", "folder", "auto", "force", "none");
        for(int i = 0; i < g_grp_n; i++)
            printf("%-40s %6d %6d %6d\n", g_grp[i].name,
                   g_grp[i].auto_n, g_grp[i].force_n, g_grp[i].none);
    }
    free(g_raw);
    return 0;
}
