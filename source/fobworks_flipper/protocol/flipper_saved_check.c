#include "flipper_saved_check.h"
#include <stdio.h>
#include <string.h>

static void norm_name(const char* s, char* d, size_t n) {
    size_t j = 0;
    if(!s) s = "";
    for(; *s && j + 1 < n; s++) {
        char c = *s;
        if(c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        if((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) d[j++] = c;
    }
    d[j] = '\0';
}

/* Allow a protocol label such as "KeeLoq" to match "KeeLoq-HCS300". Labels
   shorter than three characters must match exactly. */
static bool names_agree(const char* claimed, const char* found) {
    char a[32], b[32];
    const char* sh;
    const char* ln;
    norm_name(claimed, a, sizeof(a));
    norm_name(found, b, sizeof(b));
    if(!a[0] || !b[0]) return false;
    if(strlen(a) <= strlen(b)) {
        sh = a;
        ln = b;
    } else {
        sh = b;
        ln = a;
    }
    if(strlen(sh) < 3) return strcmp(a, b) == 0;
    return strncmp(ln, sh, strlen(sh)) == 0;
}

static void copy_trim(char* dst, size_t n, const char* src) {
    size_t i = 0;
    if(!src) src = "";
    while(*src == ' ' || *src == '\t') src++;
    while(*src && *src != '\n' && *src != '\r' && i + 1 < n)
        dst[i++] = *src++;
    dst[i] = '\0';
}

static void put(char* dst, size_t n, const char* s) {
    if(!s) s = "";
    strncpy(dst, s, n - 1);
    dst[n - 1] = '\0';
}

void flipper_saved_judge(
    const FlipperPulseBuf* pulses, const char* protocol_field,
    FlipperSavedCheck* out) {
    char claimed[24];
    char found[24];
    bool named;
    FlipperDecodeResult r;

    if(!out) return;
    memset(out, 0, sizeof(*out));
    copy_trim(claimed, sizeof(claimed), protocol_field);
    named = claimed[0] && strcmp(claimed, "RAW") != 0;
    found[0] = '\0';

    if(!pulses || pulses->len < 2) {
        out->kind = FlipperSavedNoWave;
        put(out->line1, sizeof(out->line1), "Cannot check");
        put(out->line2, sizeof(out->line2), "No RAW in file");
        put(out->line3, sizeof(out->line3), "Open the Read .sub");
        return;
    }

    if(flipper_decode(pulses, &r)) {
        out->kind = FlipperSavedAuto;
        copy_trim(found, sizeof(found), r.proto);
    } else {
        /* Mirror the dispatcher's forced-only coverage here. Toyota and Honda
           have no Auto gate, and Fiat's forced path tries V2/V0 in addition to
           the checksummed V1 decoder used by Auto. */
        static const FlipperForceProto force_only[] = {
            FlipperForceToyota,
            FlipperForceHonda,
            FlipperForceFiat,
        };
        for(size_t i = 0; i < sizeof(force_only) / sizeof(force_only[0]); i++) {
            if(flipper_decode_ex(pulses, &r, force_only[i])) {
                out->kind = FlipperSavedForce;
                copy_trim(found, sizeof(found), r.proto);
                break;
            }
        }
        if(out->kind != FlipperSavedForce) out->kind = FlipperSavedNone;
    }

    if(out->kind == FlipperSavedAuto && (!named || names_agree(claimed, found))) {
        char tmp[40];
        /* A match means the parser accepted this pulse train. It does not
           authenticate the transmitter or independently verify the file label. */
        put(out->line1, sizeof(out->line1), "Auto match");
        snprintf(tmp, sizeof(tmp), "Auto %s", found);
        put(out->line2, sizeof(out->line2), tmp);
    } else if(out->kind == FlipperSavedAuto) {
        char tmp[40];
        put(out->line1, sizeof(out->line1), "Other label");
        snprintf(tmp, sizeof(tmp), "Auto %s", found);
        put(out->line2, sizeof(out->line2), tmp);
    } else if(out->kind == FlipperSavedForce) {
        char tmp[40];
        put(out->line1, sizeof(out->line1), "Force only");
        snprintf(tmp, sizeof(tmp), "Force %s", found);
        put(out->line2, sizeof(out->line2), tmp);
    } else {
        put(out->line1, sizeof(out->line1), "No match");
        put(out->line2, sizeof(out->line2), "No decoder agreed");
    }

    if(named) {
        char tmp[40];
        snprintf(tmp, sizeof(tmp), "File: %s", claimed);
        put(out->line3, sizeof(out->line3), tmp);
    } else {
        put(out->line3, sizeof(out->line3), "File label: RAW");
    }
}