#include "flipper_decoders.h"
#include "flipper_scratch.h"
#include "flipper_keeloq.h"
#include "flipper_honda.h"
#include "flipper_subaru.h"
#include "flipper_suzuki.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ── TE estimator — k=2 coherence pass ──────────────────────────────────── */
static uint32_t estimate_te_locked(const uint32_t* buf, int n) {
    if(!buf || n < 8) return 0;

    /* 16-bit counts: I assume a capture cannot fill one bucket past 65535. */
    uint16_t* hist = flipper_scratch_b(sizeof(uint16_t) * 512);
    if(!hist) return 0;
    memset(hist, 0, sizeof(uint16_t) * 512);

    for(int i = 0; i < n; i++) {
        uint32_t v = buf[i];
        if(v < FLIPPER_MIN_PULSE_US || v > 16383) continue;
        uint32_t bucket = v >> 5;
        if(bucket < 512) hist[bucket]++;
    }

    /* I find the peak */
    uint32_t peak_buck = 0;
    uint16_t peak_cnt = 0;
    for(int b = 1; b < 512; b++) {
        if(hist[b] > peak_cnt) { peak_cnt = hist[b]; peak_buck = b; }
    }
    if(peak_cnt < 4) return 0;

    /* TE estimate = centre of peak bucket */
    uint32_t te = (peak_buck << 5) + 16;

    /* I reject implausible values */
    if(te < 100 || te > 4000) return 0;
    return te;
}

#ifdef FLIPPER_FAP_SLIM
#include "flipper_plugin.h"
#endif

uint32_t flipper_estimate_te(const uint32_t* buf, int n) {
#ifdef FLIPPER_FAP_SLIM
    flipper_plugin_lock();
#endif
    uint32_t te = estimate_te_locked(buf, n);
#ifdef FLIPPER_FAP_SLIM
    flipper_plugin_unlock();
#endif
    return te;
}

/* ── KeeLoq decoder ──────────────────────────────────────────────────────── */
bool flipper_decode_keeloq(const FlipperPulseBuf* buf, FlipperDecodeResult* r) {
    if(!buf || !r || buf->te_us == 0) return false;

    /* I try up to 3 TE candidates (te, te±15%) */
    uint32_t te_candidates[3] = {
        buf->te_us,
        buf->te_us * 85 / 100,
        buf->te_us * 115 / 100,
    };

    for(int c = 0; c < 3; c++) {
        uint32_t te = te_candidates[c];
        if(te < 100 || te > 4000) continue;

        char* bits = flipper_scratch_a(0, 600);
        if(!bits) continue;
        uint16_t blen = kl_pwm(buf->durations, buf->len, te, bits);
        if(blen < 66) continue;

        KLFrame f;
        int got = 0;
        int hcs300 = 0;
        if(kl_parse(bits, blen, &f)) {
        /* All-zero and all-one serials commonly come from PWM noise. */
            if(f.sn != 0 && f.sn != 0x0FFFFFFFu && f.enc != 0xFFFFFFFFu) {
                got = 1;
                uint8_t btn4  = f.btn & 0xF;
                uint8_t disc4 = f.disc & 0xF;
                uint32_t expected_lo = (btn4 << 8) | (disc4 << 4) | (f.ovf << 3);
                hcs300 = ((f.enc & 0xFFF) == expected_lo);
            }
        }
        if(!got) {
            /* Manchester-decode attempt (some clones emit Manchester KeeLoq). */
            char* klm = flipper_scratch_a(600, 600);
            if(!klm) continue;
            uint16_t ml = 0;
            for(int i = 0; i + 1 < blen; i += 2) {
                if     (bits[i]=='0' && bits[i+1]=='1') klm[ml++]='1';
                else if(bits[i]=='1' && bits[i+1]=='0') klm[ml++]='0';
            }
            klm[ml] = '\0';
            if(ml >= 66 && kl_parse(klm, ml, &f) &&
               f.sn != 0 && f.sn != 0x0FFFFFFFu && f.enc != 0xFFFFFFFFu) {
                got = 1;
                uint32_t expected_lo =
                    ((f.btn & 0xF) << 8) | ((f.disc & 0xF) << 4) | (f.ovf << 3);
                hcs300 = ((f.enc & 0xFFF) == expected_lo);
            }
        }
        /* kl_parse checks frame length and rejects an empty hop or button. The HCS300 low-12 test compares ciphertext, so Honda frames may not pass; I use it only to refine the label. Auto also requires a single known function button (lock, unlock, trunk, or panic). */
        if(!got) continue;

        uint8_t btn4 = f.btn & 0xF;
        int one_btn = (btn4 == 1 || btn4 == 2 || btn4 == 4 || btn4 == 8);
        if(!hcs300 && !one_btn && !flipper_decode_forced()) continue;

        kl_recover_key(&f);
        const char* label = hcs300 ? "KeeLoq-HCS300" : "KeeLoq";

        r->addr   = f.sn;
        r->cnt    = f.cnt;
        r->hop    = f.enc;
        r->btn    = f.btn;
        r->te_us  = te;
        r->bits   = blen;
        r->rolling = true;
        r->freq_mhz = buf->freq_mhz;
        strncpy(r->proto, label, sizeof(r->proto) - 1);
        memcpy(r->mfr_name, f.mfr_name, sizeof(r->mfr_name));
        r->mfr_name[sizeof(r->mfr_name) - 1] = '\0';
        memcpy(r->device_key_hex, f.device_key_hex, sizeof(r->device_key_hex));
        r->device_key_hex[sizeof(r->device_key_hex) - 1] = '\0';
        r->predict_window = f.predict_window;
        r->predict_lo     = f.predict_lo;
        r->predict_hi     = f.predict_hi;
        if(f.mfr_name[0] && f.device_key_hex[0]) {
            snprintf(r->predict_note, sizeof(r->predict_note),
                     "OK=next hop  Key:%s", f.mfr_name);
        } else {
            /* No recovered key. kl_parse() fills f->cnt from the LOW 16 BITS OF THE CIPHERTEXT, which carry no counter meaning — the NLFSR output is not the plaintext counter. I disable prediction entirely rather than emit a range that would mislead. */
            r->predict_window = 0;
            r->predict_lo = 0;
            r->predict_hi = 0;
            snprintf(r->predict_note, sizeof(r->predict_note),
                     "no key — counter derived from ciphertext, no prediction");
            r->device_key_hex[0] = '\0';
            r->mfr_name[0] = '\0';
        }
        return true;
    }
    return false;
}

/* ── Security+ 1.0 / 2.0 ─────────────────────────────────────────────────── */
/* * Full parsers live in flipper_secplus.c. The device host FAP keeps only thin * force-plugin stubs so that .text stays under the loader cap; fw_force.fal * (and host tests) link the real file. Auto stays off either way. */
#ifdef FLIPPER_FAP_SLIM
#include "flipper_plugin.h"
bool flipper_decode_secplus1(const FlipperPulseBuf* buf, FlipperDecodeResult* r) {
    const FobworksForceApi* a = flipper_force_api();
    if(!a || !a->decode) return false;
    return a->decode(buf, r, FlipperForceSecplus1);
}
bool flipper_decode_secplus2(const FlipperPulseBuf* buf, FlipperDecodeResult* r) {
    const FobworksForceApi* a = flipper_force_api();
    if(!a || !a->decode) return false;
    return a->decode(buf, r, FlipperForceSecplus2);
}
/* Mazda / Honda / Toyota / Nissan / PSA parsers live in fw_force.fal. Thin
   stubs keep the host under the loader .text cap; Auto and Force both map
   the FAL on demand. Host tests compile the real protocol sources instead. */
bool flipper_decode_mazda(const FlipperPulseBuf* buf, FlipperDecodeResult* r) {
    const FobworksForceApi* a = flipper_force_api();
    if(!a || !a->decode) return false;
    return a->decode(buf, r, FlipperForceMazda);
}
bool flipper_decode_honda(const FlipperPulseBuf* buf, FlipperDecodeResult* r) {
    const FobworksForceApi* a = flipper_force_api();
    if(!a || !a->decode) return false;
    return a->decode(buf, r, FlipperForceHonda);
}
bool flipper_decode_honda_kr5(const FlipperPulseBuf* buf, FlipperDecodeResult* r) {
    const FobworksForceApi* a = flipper_force_api();
    if(!a || !a->decode) return false;
    return a->decode(buf, r, FlipperForceHondaKr5);
}
bool flipper_decode_toyota(const FlipperPulseBuf* buf, FlipperDecodeResult* r) {
    const FobworksForceApi* a = flipper_force_api();
    if(!a || !a->decode) return false;
    return a->decode(buf, r, FlipperForceToyota);
}
bool flipper_decode_nissan(const FlipperPulseBuf* buf, FlipperDecodeResult* r) {
    const FobworksForceApi* a = flipper_force_api();
    if(!a || !a->decode) return false;
    return a->decode(buf, r, FlipperForceNissan);
}
bool flipper_decode_psa(const FlipperPulseBuf* buf, FlipperDecodeResult* r) {
    const FobworksForceApi* a = flipper_force_api();
    if(!a || !a->decode) return false;
    return a->decode(buf, r, FlipperForcePsa);
}
#endif

#ifdef FLIPPER_FAP_SLIM
#include "flipper_plugin.h"
/* Force-only fixed-code parsers live in fw_force.fal (flipper_fixed_force.c).
   Host tests compile that file directly instead of these stubs. */
static bool force_fixed(const FlipperPulseBuf* buf, FlipperDecodeResult* r,
                        FlipperForceProto force) {
    const FobworksForceApi* a = flipper_force_api();
    if(!a || !a->decode) return false;
    return a->decode(buf, r, force);
}
bool flipper_decode_came12(const FlipperPulseBuf* buf, FlipperDecodeResult* r) {
    return force_fixed(buf, r, FlipperForceCame12);
}
bool flipper_decode_nice_flo(const FlipperPulseBuf* buf, FlipperDecodeResult* r) {
    return force_fixed(buf, r, FlipperForceNiceFlo);
}
bool flipper_decode_faac_slh(const FlipperPulseBuf* buf, FlipperDecodeResult* r) {
    return force_fixed(buf, r, FlipperForceFaacSlh);
}
bool flipper_decode_doorhan(const FlipperPulseBuf* buf, FlipperDecodeResult* r) {
    return force_fixed(buf, r, FlipperForceDoorhan);
}
bool flipper_decode_ansonic(const FlipperPulseBuf* buf, FlipperDecodeResult* r) {
    return force_fixed(buf, r, FlipperForceAnsonic);
}
bool flipper_decode_linear10(const FlipperPulseBuf* buf, FlipperDecodeResult* r) {
    return force_fixed(buf, r, FlipperForceLinear10);
}
bool flipper_decode_holtek(const FlipperPulseBuf* buf, FlipperDecodeResult* r) {
    return force_fixed(buf, r, FlipperForceHoltek);
}
bool flipper_decode_pt2262(const FlipperPulseBuf* buf, FlipperDecodeResult* r) {
    return force_fixed(buf, r, FlipperForcePt2262);
}
bool flipper_decode_ev1527(const FlipperPulseBuf* buf, FlipperDecodeResult* r) {
    return force_fixed(buf, r, FlipperForceEv1527);
}
bool flipper_decode_tpms(const FlipperPulseBuf* buf, FlipperDecodeResult* r) {
    return force_fixed(buf, r, FlipperForceTpms);
}
#endif

/* ── Beninca XOR Type-1 decoder ─────────────────────────────────────────── */
/* * Beninca and compatible remotes use KeeLoq with XOR-Type-1 manufacturer-key * diversification (per the KeeLoq reference implementation analysis). I handle their pulse format * with the KeeLoq decoder and its key sweep; this wrapper only assigns the * Beninca label when key recovery identifies that variant. */
bool flipper_decode_beninca(const FlipperPulseBuf* buf, FlipperDecodeResult* r) {
    if(!flipper_decode_keeloq(buf, r)) return false;
    /* I change the label only when recovery identified an XOR-Type-1 key. */
    if(strstr(r->mfr_name, "xor-type1"))
        strncpy(r->proto, "KeeLoq-Beninca", sizeof(r->proto) - 1);
    return true;
}
#ifdef FLIPPER_FAP_SLIM
#include "flipper_plugin.h"
/* Device FAP keeps Scher-Khan out of the host image. Force-decode maps fw_force.fal and runs the Magicar PWM parser there. Host tests compile protocol/flipper_scher_khan.c instead of this stub. */
bool flipper_decode_scher_khan(const FlipperPulseBuf* buf, FlipperDecodeResult* r) {
    const FobworksForceApi* a = flipper_force_api();
    if(!a || !a->decode) return false;
    return a->decode(buf, r, FlipperForceScherKhan);
}
#endif

/* ── Counter-delta prediction ────────────────────────────────────────────── */
void flipper_kl_predict(FlipperDecodeResult* r, const FlipperDecodeResult* r2) {
    if(!r) return;
    if(!r->rolling || r->predict_window == 0) {
        if(!r->predict_note[0])
            snprintf(r->predict_note, sizeof(r->predict_note), "Fixed code — no prediction");
        return;
    }
    if(r2 && r2->rolling && r2->addr == r->addr) {
        /* Two-capture delta */
        uint32_t delta = (r2->cnt - r->cnt) & 0xFFFF;
        if(delta > 0 && delta < 256) {
            r->predict_lo = (r2->cnt + 1) & 0xFFFF;
            r->predict_hi = (r2->cnt + delta * 2) & 0xFFFF;
            snprintf(r->predict_note, sizeof(r->predict_note),
                     "Delta=%u  next=%u..%u", (unsigned)delta, (unsigned)r->predict_lo, (unsigned)r->predict_hi);
            return;
        }
    }
    r->predict_lo = (r->cnt + 1) & 0xFFFF;
    r->predict_hi = (r->cnt + 16) & 0xFFFF;
    snprintf(r->predict_note, sizeof(r->predict_note),
             "Single capture  next~%u..%u", (unsigned)r->predict_lo, (unsigned)r->predict_hi);
}

/* ── Top-level decoder dispatcher ────────────────────────────────────────── */
/* * I maintain the Auto priority order in the registry below. I prefer parsers with * stronger checks ahead of broader structural matches to limit false positives. */
/* ── Decoder registry — single source of truth ───────────────────────────── */
/* This array is my Auto priority chain: parsers with OEM checks first, then * KeeLoq and other structured protocols. Broad fixed-code parsers are force- * only. Entries with auto_safe == false run only when explicitly selected. */
const FlipperDecoderReg FLIPPER_DECODERS[] = {
    /* I run OEM parsers before KeeLoq. Their CRC, checksum, or preamble checks * reduce false matches; the shared OOK-PWM encoding I see could otherwise let a * GM, Ford, or similar frame look like a KeeLoq hop. */
    { FlipperForceGm,       "GM",         flipper_decode_gm,       true  },
    { FlipperForceFord,     "Ford",       flipper_decode_ford,     true  },
    { FlipperForceChrysler, "Chrysler",   flipper_decode_chrysler, true  },
    /* I check Suzuki first: the Kia-V0 preamble and CRC can also match Suzuki * PWM, so this order gives the Suzuki CRC-8 parser the first chance. */
    { FlipperForceSuzuki,   "Suzuki",     flipper_decode_suzuki,   true  },
    { FlipperForceKia,      "KIA/Hyundai",flipper_decode_kia,      true  },
    { FlipperForceVag,      "VAG",        flipper_decode_vag,      true  },
    /* PSA Mode 0x23 has only XOR and an 8-bit sum, which matched unrelated * Buick/VW Manchester captures in my testing. I keep it force-only until its * checks are stronger; it did not match the Groupe PSA captures I have. */
    { FlipperForcePsa,      "PSA",        flipper_decode_psa,      false },
    { FlipperForceMazda,    "Mazda",      flipper_decode_mazda,    true  },
    { FlipperForceSubaru,   "Subaru",     flipper_decode_subaru,   true  },
    { FlipperForceHondaKr5, "Honda KR5",  flipper_decode_honda_kr5,true  },
    { FlipperForceLandRover,"Land Rover", flipper_decode_land_rover,true },
    /* I observe that BMW CAS3/CAS4 uses PPM with ~250 µs marks and 500/1500 µs spaces. The >=10 ms sync may be an inter-burst gap outside the pulse buffer, so I accept data-only runs in my parser. This is structural; I do not decrypt the AES payload. */
    { FlipperForceBmw,      "BMW CAS",    flipper_decode_bmw,      true  },
    /* V1 uses an XOR checksum and V2 has header 0001; V0 is force-only. I run these before KeeLoq so FCA Pacifica/Jeep Manchester frames get their parser before the shared 66-bit hop shape is considered. */
    { FlipperForceFiat,      "Fiat",       flipper_decode_fiat,       true  },
    /* KeeLoq follows the OEM parsers; I can also recover keys across frames. */
    { FlipperForceKeeloq,   "KeeLoq",     flipper_decode_keeloq,   true  },
    /* Sec+ 1.0 now matches the public ternary/OOK format (the secplus reference implementation, Flipper secplus_v1, rtl_433). It still has no transmitted checksum, so I keep it force-only until a live capture set measures false positives. */
    { FlipperForceSecplus1, "Sec+ 1.0",   flipper_decode_secplus1, false },
    /* These parsers lack a checksum or rely on weaker structural checks, so I run them only when I explicitly select them. */
    { FlipperForceSecplus2,  "Sec+ 2.0",   flipper_decode_secplus2,  false },
    { FlipperForceCame12,    "CAME",       flipper_decode_came12,    false },
    { FlipperForceNiceFlo,   "Nice FLO",   flipper_decode_nice_flo,  false },
    { FlipperForceAnsonic,   "Ansonic",    flipper_decode_ansonic,   false },
    { FlipperForceLinear10,  "Linear",     flipper_decode_linear10,  false },
    { FlipperForceHoltek,    "Holtek",     flipper_decode_holtek,    false },
    { FlipperForcePt2262,    "PT2262",     flipper_decode_pt2262,    false },
    { FlipperForceEv1527,    "EV1527",     flipper_decode_ev1527,    false },
    { FlipperForceTpms,      "TPMS",       flipper_decode_tpms,      false },
    { FlipperForceFaacSlh,   "FAAC SLH",   flipper_decode_faac_slh,  false },
    { FlipperForceDoorhan,   "DoorHan",    flipper_decode_doorhan,   false },
    /* These OEM layouts are provisional or have no checksum; I keep them out of Auto mode. */
    { FlipperForceHonda,     "Honda RKE",  flipper_decode_honda,      false },
    { FlipperForceScherKhan, "Scher-Khan", flipper_decode_scher_khan, false },
    { FlipperForceToyota,    "Toyota RKE", flipper_decode_toyota,     false },
    { FlipperForceNissan,    "Nissan RKE", flipper_decode_nissan,     false },
};
const int FLIPPER_DECODER_COUNT =
    (int)(sizeof(FLIPPER_DECODERS) / sizeof(FLIPPER_DECODERS[0]));

const char* flipper_force_proto_name(FlipperForceProto f) {
    if(f == FlipperForceAuto) return "Auto";
    for(int i = 0; i < FLIPPER_DECODER_COUNT; i++)
        if(FLIPPER_DECODERS[i].id == f) return FLIPPER_DECODERS[i].name;
    return "Auto";
}

static bool s_decode_forced;

bool flipper_decode_forced(void) { return s_decode_forced; }

static bool decode_locked(const FlipperPulseBuf* buf, FlipperDecodeResult* result,
                       FlipperForceProto force) {
    if(!buf || !result) return false;
    memset(result, 0, sizeof(*result));
    result->freq_mhz = buf->freq_mhz;
    s_decode_forced = (force != FlipperForceAuto);

    /* Forced mode runs only the decoder I selected. */
    if(force != FlipperForceAuto) {
        for(int i = 0; i < FLIPPER_DECODER_COUNT; i++)
            if(FLIPPER_DECODERS[i].id == force)
                return FLIPPER_DECODERS[i].fn(buf, result);
        return false;
    }

    /* Auto mode tries eligible decoders in registry order. */
    for(int i = 0; i < FLIPPER_DECODER_COUNT; i++) {
        if(!FLIPPER_DECODERS[i].auto_safe) continue;
        if(FLIPPER_DECODERS[i].fn(buf, result)) return true;
        /* A failed parser may have partially filled the result. I clear it before trying the next decoder. */
        memset(result, 0, sizeof(*result));
        result->freq_mhz = buf->freq_mhz;
    }
    return false;
}

bool flipper_decode_ex(const FlipperPulseBuf* buf, FlipperDecodeResult* result,
                      FlipperForceProto force) {
#ifdef FLIPPER_FAP_SLIM
    flipper_plugin_lock();
#endif
    bool ok = decode_locked(buf, result, force);
#ifdef FLIPPER_FAP_SLIM
    flipper_plugin_unlock();
#endif
    return ok;
}

bool flipper_decode(const FlipperPulseBuf* buf, FlipperDecodeResult* result) {
    return flipper_decode_ex(buf, result, FlipperForceAuto);
}

/* ── Custom firmware protocol registry hooks ─────────────────────────────── */
#ifdef FLIPPER_UNLEASHED_INTEGRATION
#include <lib/subghz/subghz_environment.h>

void flipper_register_unleashed_protocols(SubGhzEnvironment* env) {
    /* I register firmware protocols through subghz_environment_add_protocol(). The SubGhzProtocol wrappers live in separate translation units, and I instantiate them from the firmware protocol template when building inside the firmware tree. */
    (void)env;
}
#endif
