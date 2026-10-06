#include "flipper_decoders.h"
#include <string.h>
#include <stdio.h>

/* Scher-Khan / Magicar PWM car-alarm remote.
 * Symmetric PWM at 433.92 MHz. After at least two long HIGH header pulses
 * (~1500 µs = 2T) and a short start bit, each data bit is a HIGH/LOW pair:
 * both short (~750 µs) for 0 or both long (~1100 µs) for 1. A longer HIGH
 * (at least ~1420 µs) ends the frame. No transmitted checksum — force-only.
 * The 51-bit "MAGIC CODE, Dynamic" includes serial, button, and rolling
 * counter fields. Other recognized lengths are reported by length only.
 */
static int sk_near(uint32_t v, uint32_t ref) {
    uint32_t d = (v > ref) ? v - ref : ref - v;
    return d <= 160;
}

bool flipper_decode_scher_khan(const FlipperPulseBuf* buf, FlipperDecodeResult* r) {
    if(!buf || !r) return false;
    const uint32_t S = 750, L = 1100;
    const uint32_t* p = buf->durations;
    int n = buf->len;

    for(int h = 0; h + 1 < n; h += 2) {
        int hc = 0, j = h;
        while(j + 1 < n && sk_near(p[j], S * 2)) {
            hc++;
            j += 2;
        }
        if(hc < 2) continue;
        if(!(j + 1 < n && sk_near(p[j], S))) continue;

        int k = j + 2;
        uint64_t data = 0;
        int count = 1;
        bool ok = true, stop = false;
        while(k < n) {
            uint32_t hi = p[k];
            if(hi >= L + 320) {
                stop = true;
                break;
            }
            if(k + 1 >= n) {
                ok = false;
                break;
            }
            uint32_t lo = p[k + 1];
            if(sk_near(hi, S) && sk_near(lo, S)) {
                data = (data << 1);
                count++;
            } else if(sk_near(hi, L) && sk_near(lo, L)) {
                data = (data << 1) | 1ULL;
                count++;
            } else {
                ok = false;
                break;
            }
            k += 2;
            if(count > 64) {
                ok = false;
                break;
            }
        }
        if(!ok || !stop || count < 35) continue;

        memset(r, 0, sizeof(*r));
        r->freq_mhz = buf->freq_mhz;
        r->te_us = S;
        r->bits = count;
        r->hop = (uint32_t)data;
        strncpy(r->proto, "Scher-Khan", sizeof(r->proto) - 1);

        if(count == 51) {
            r->addr = (uint32_t)(((data >> 24) & 0xFFFFFF0) | ((data >> 20) & 0x0F));
            r->btn = (uint8_t)((data >> 24) & 0x0F);
            r->cnt = (uint32_t)(data & 0xFFFF);
            r->rolling = true;
            r->predict_window = 256;
            r->predict_lo = (r->cnt + 1) & 0xFFFF;
            r->predict_hi = (r->cnt + 8) & 0xFFFF;
            snprintf(
                r->predict_note,
                sizeof(r->predict_note),
                "Magicar Dynamic  sn=0x%07lX",
                (unsigned long)r->addr);
        } else {
            r->replay_vuln = (count == 35);
            snprintf(r->predict_note, sizeof(r->predict_note), "Magicar %d-bit", count);
        }
        return true;
    }
    return false;
}
