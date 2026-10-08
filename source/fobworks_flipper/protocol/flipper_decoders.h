#pragma once
#define FLIPPER_DECODERS_H
#include <stdint.h>
#include <stdbool.h>

/* ── Decode result ────────────────────────────────────────────────────────── */
typedef struct {
    char     proto[32];        /* protocol name                               */
    bool     replay_vuln;      /* fixed/unencrypted code → replay/clonable     */
    uint32_t addr;             /* transmitter address / serial number         */
    uint32_t cnt;              /* rolling counter (0 for fixed codes)         */
    uint32_t hop;              /* hop / encrypted word                        */
    uint8_t  btn;              /* button bits                                 */
    float    freq_mhz;         /* capture frequency                           */
    uint32_t te_us;            /* estimated chip period µs                    */
    int      bits;             /* raw bit count                               */
    bool     rolling;          /* true = rolling/KeeLoq code                  */

    /* KeeLoq-specific */
    char     mfr_name[32];
    char     device_key_hex[17];
    uint32_t predict_window;   /* 0 = no prediction                           */
    uint32_t predict_lo;
    uint32_t predict_hi;
    char     predict_note[64];
    char     conf_tag[8];      /* Auto / Force / None — verdict card           */
    char     act_tag[12];      /* Save / Predict / Resync / Replay / None      */
} FlipperDecodeResult;

/* ── Pulse buffer ─────────────────────────────────────────────────────────── */
/* Max edges per capture. 512 alternating edges = 256 bit-pairs, well beyond any supported fob (KeeLoq is 66 bits; long rolling codes are a few hundred edges). I keep it small on purpose: FlipperPulseBuf is embedded many times over in FlipperApp (fobback.caps[5], fobclone.cap[2], ...), so this size is the dominant term in the app's single contiguous allocation on the Flipper. */
#define FLIPPER_PULSE_MAX 256

/* I drop only sub-chip noise. Honda KR5 Manchester marks sit near 60 µs and the Renault 66 µs family is in the same band; a 75 µs floor erased those edges before any decoder ran. */
#define FLIPPER_MIN_PULSE_US 40u

typedef struct {
    uint32_t  durations[FLIPPER_PULSE_MAX]; /* alternating H/L durations, µs; [0] = HIGH */
    int       len;
    float     freq_mhz;
    uint32_t  te_us;           /* TE estimated by coherence gate              */
} FlipperPulseBuf;

/* ── TE estimation ────────────────────────────────────────────────────────── */

/* I estimate TE from a raw pulse buffer using a k=2 coherence pass. I return 0 if the buffer is too noisy/short to produce a reliable estimate. */
uint32_t flipper_estimate_te(const uint32_t* buf, int n);

/* ── Signal decoder entry point ───────────────────────────────────────────── */

/* I try every known protocol decoder against buf. I fill the result and return true if any decoder succeeded. I try decoders in priority order (KeeLoq first). */
bool flipper_decode(const FlipperPulseBuf* buf, FlipperDecodeResult* result);

/* ── Force-protocol selection (Advanced Settings) ─────────────────────────── */
/* When force != FlipperForceAuto, I only attempt that one decoder, so a weak/ambiguous signal is parsed as my selected protocol instead of being claimed by a higher-priority decoder. */
typedef enum {
    FlipperForceAuto = 0,
    FlipperForceKeeloq,
    FlipperForceSecplus2,
    FlipperForceSecplus1,
    FlipperForceDoorhan,
    FlipperForceCame12,
    FlipperForceNiceFlo,
    FlipperForceHoltek,
    FlipperForceFaacSlh,
    FlipperForceAnsonic,
    FlipperForceLinear10,
    FlipperForcePt2262,
    FlipperForceEv1527,
    FlipperForceTpms,
    FlipperForceHonda,     /* Honda RKE (RollingPWN) — force-only, not auto      */
    FlipperForceSubaru,    /* Subaru 80-bit Manchester, 0x55 sync + checksum     */
    FlipperForceHondaKr5,  /* Honda KR5V2X/1X Manchester, EC0F62 preamble + CRC-8 */
    /* OEM frame decoders (real CRC/checksum gates → auto-safe) ─────────────── */
    FlipperForceGm,        /* GM PPM 112-bit, additive checksum                  */
    FlipperForceFord,      /* Ford V0 Manchester / V2 PWM, CRC-8 + checksum      */
    FlipperForceChrysler,  /* Chrysler 80-bit PWM, XOR-table transform          */
    FlipperForceKia,       /* KIA/Hyundai V0 PWM, CRC-8                          */
    FlipperForceVag,       /* VW/Audi/Skoda/SEAT Manchester, 0xAF3F/0xAF1C sync  */
    FlipperForcePsa,       /* Peugeot/Citroen Manchester, XOR + checksum         */
    FlipperForceMazda,     /* Mazda V0 Manchester, 0xD7 sync + additive checksum */
    FlipperForceSuzuki,    /* Suzuki 64-bit PWM, CRC-8 (poly 0x7F)               */
    FlipperForceLandRover, /* Land Rover/Jaguar V0 diff-Manch, count-check+tail  */
    FlipperForceBmw,       /* BMW CAS3/CAS4 PPM structural (mark/space ≥64 bits) */
    /* Real layout without a checksum gate → force-only (never in Auto) ─────── */
    FlipperForceFiat,      /* Fiat/Alfa V0 64-bit Manchester (fix|hop)           */
    FlipperForceScherKhan, /* Scher-Khan/Magicar PWM alarm (no checksum)         */
    /* Provisional layouts (guessed fields → force-only, NOT auto) ──────────── */
    FlipperForceToyota,    /* Toyota/Lexus RKE — provisional, force-only         */
    FlipperForceNissan,    /* Nissan RKE — provisional, force-only               */
    FlipperForceCount,
} FlipperForceProto;

/* Human-readable label for a force-protocol id (for menus). */
const char* flipper_force_proto_name(FlipperForceProto f);

/* ── Decoder registry ─────────────────────────────────────────────────────── */
/* Single source of truth for the dispatcher: the force switch, the Auto priority chain, and the menu labels are all derived from this one table so the two lists can never drift. Array order == Auto priority (KeeLoq first); entries with auto_safe == false are reachable only via a forced selection. */
typedef struct {
    FlipperForceProto id;
    const char*       name;
    bool (*fn)(const FlipperPulseBuf* buf, FlipperDecodeResult* r);
    bool              auto_safe;   /* true → part of the Auto chain            */
} FlipperDecoderReg;

extern const FlipperDecoderReg FLIPPER_DECODERS[];
extern const int               FLIPPER_DECODER_COUNT;

/* Decode honoring a forced protocol selection (FlipperForceAuto == flipper_decode). */
bool flipper_decode_ex(const FlipperPulseBuf* buf, FlipperDecodeResult* result,
                       FlipperForceProto force);
/* True while flipper_decode_ex is serving a forced (non-Auto) selection. Weak/no-checksum fallbacks (e.g. Fiat V0) consult this so Auto stays clean. */
bool flipper_decode_forced(void);

/* ── Individual decoders (called internally; exposed for testing) ─────────── */

bool flipper_decode_keeloq    (const FlipperPulseBuf* buf, FlipperDecodeResult* r);
bool flipper_decode_secplus1  (const FlipperPulseBuf* buf, FlipperDecodeResult* r);
bool flipper_decode_secplus2  (const FlipperPulseBuf* buf, FlipperDecodeResult* r);
bool flipper_decode_came12    (const FlipperPulseBuf* buf, FlipperDecodeResult* r);
bool flipper_decode_nice_flo  (const FlipperPulseBuf* buf, FlipperDecodeResult* r);
bool flipper_decode_faac_slh  (const FlipperPulseBuf* buf, FlipperDecodeResult* r);
bool flipper_decode_doorhan   (const FlipperPulseBuf* buf, FlipperDecodeResult* r);
bool flipper_decode_ansonic    (const FlipperPulseBuf* buf, FlipperDecodeResult* r);
bool flipper_decode_linear10  (const FlipperPulseBuf* buf, FlipperDecodeResult* r);
bool flipper_decode_holtek    (const FlipperPulseBuf* buf, FlipperDecodeResult* r);
bool flipper_decode_beninca   (const FlipperPulseBuf* buf, FlipperDecodeResult* r);
bool flipper_decode_pt2262    (const FlipperPulseBuf* buf, FlipperDecodeResult* r);
bool flipper_decode_ev1527    (const FlipperPulseBuf* buf, FlipperDecodeResult* r);
bool flipper_decode_tpms      (const FlipperPulseBuf* buf, FlipperDecodeResult* r);
bool flipper_decode_honda     (const FlipperPulseBuf* buf, FlipperDecodeResult* r);
bool flipper_decode_subaru    (const FlipperPulseBuf* buf, FlipperDecodeResult* r);

/* Honda KR5V2X/1X — documented Manchester layout with an OpenSafety CRC-8 gate. Distinct from the provisional RollingPWN path above, so this one is Auto-safe. */
bool flipper_decode_honda_kr5 (const FlipperPulseBuf* buf, FlipperDecodeResult* r);

/* OEM frame decoders — pulse front-ends over the real vendor parsers. Auto-safe: each is gated by the vendor's own CRC/checksum/preamble. */
bool flipper_decode_gm        (const FlipperPulseBuf* buf, FlipperDecodeResult* r);
bool flipper_decode_ford      (const FlipperPulseBuf* buf, FlipperDecodeResult* r);
bool flipper_decode_chrysler  (const FlipperPulseBuf* buf, FlipperDecodeResult* r);
bool flipper_decode_kia       (const FlipperPulseBuf* buf, FlipperDecodeResult* r);
bool flipper_decode_vag       (const FlipperPulseBuf* buf, FlipperDecodeResult* r);
bool flipper_decode_land_rover(const FlipperPulseBuf* buf, FlipperDecodeResult* r);
bool flipper_decode_bmw       (const FlipperPulseBuf* buf, FlipperDecodeResult* r);
bool flipper_decode_psa       (const FlipperPulseBuf* buf, FlipperDecodeResult* r);

/* Mazda V0 — real Manchester decoder (0xD7 sync + additive checksum). Auto-safe. */
bool flipper_decode_mazda     (const FlipperPulseBuf* buf, FlipperDecodeResult* r);

/* Suzuki — 64-bit PWM rolling code with an 8-bit CRC (poly 0x7F). Auto-safe. */
bool flipper_decode_suzuki    (const FlipperPulseBuf* buf, FlipperDecodeResult* r);

/* Real-layout vehicle decoders without a checksum gate — force-only. Structurally strict (shape + entropy) but never joined to Auto. Toyota's serial/button/counter split is a structural read of the enciphered Denso payload (no transmitted checksum exists). */
bool flipper_decode_fiat      (const FlipperPulseBuf* buf, FlipperDecodeResult* r);
bool flipper_decode_toyota    (const FlipperPulseBuf* buf, FlipperDecodeResult* r);

/* Scher-Khan / Magicar PWM car-alarm remote — recognized layout, no transmitted checksum, so force-only (never joined to the Auto chain). */
bool flipper_decode_scher_khan(const FlipperPulseBuf* buf, FlipperDecodeResult* r);

/* Provisional vehicle RKE decoder — guessed field layout, force-only. NOT registered in the Auto chain until validated against real captures. */
bool flipper_decode_nissan    (const FlipperPulseBuf* buf, FlipperDecodeResult* r);

/* ── Prediction (next-code) ───────────────────────────────────────────────── */

/* I fill predict_lo/predict_hi/predict_note from an already-decoded KeeLoq result. I require a second capture (r2 != NULL) for counter-delta estimation, or I use a single-capture window estimate when r2 is NULL. */
void flipper_kl_predict(FlipperDecodeResult* r, const FlipperDecodeResult* r2);

#ifdef FLIPPER_UNLEASHED_INTEGRATION
/* I register Flipper decoders into a custom firmware SubGhzEnvironment. I call this once after subghz_environment_alloc(). */
#include <lib/subghz/subghz_environment.h>
void flipper_register_unleashed_protocols(SubGhzEnvironment* env);
#endif
