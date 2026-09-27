#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* FlipperPulseBuf comes from flipper_decoders.h. */
#ifndef FLIPPER_DECODERS_H
#include "flipper_decoders.h"
#endif

/* ── KeeLoq frame (output of ks_parseKL) ─────────────────────────────────── */
typedef struct {
    uint32_t enc;          /* 32-bit encrypted hop counter                    */
    uint32_t dec;          /* decrypted hop counter (after key recovery)       */
    uint32_t sn;           /* 28-bit serial number                             */
    uint8_t  btn;          /* button bits (4)                                  */
    uint8_t  disc;         /* discriminant nibble (4)                          */
    uint8_t  ovf;          /* overflow bit                                     */
    uint8_t  rep;          /* repeat bit                                       */
    uint32_t cnt;          /* raw rolling counter                              */
    uint32_t predict_lo;   /* predicted next counter (low)                     */
    uint32_t predict_hi;   /* predicted next counter (high)                    */
    uint32_t predict_window; /* 0 = fixed code (no prediction)                 */
    char     mfr_name[32]; /* matched manufacturer key label, or ""           */
    uint64_t mfr_key;      /* matched key value (0 if none)                   */
    uint64_t key;          /* the KEY that actually decrypted `enc`           */
    char     device_key_hex[17]; /* derived device key as 16-hex string        */
} KLFrame;

/* ── Manufacturer key entry ──────────────────────────────────────────────── */
typedef struct {
    const char* name;
    uint64_t    key;
    uint8_t     learn_type;  /* 0=iter,1=simple,2=normal,3=secure,4=magic_xor,5-11=magic_serial */
} MfrKey;

/* ── Key derivation result ───────────────────────────────────────────────── */
typedef struct {
    char     name[40];
    uint64_t key;
} DerivedKey;

#define N_MFR_KEYS        73
#define MAX_DERIVED_KEYS  (N_MFR_KEYS * 14)

/* Public key table */
extern const MfrKey FLIPPER_MFR_KEYS[N_MFR_KEYS];

/* ── Key mask ────────────────────────────────────────────────────────────── */
/*  FLIPPER_MFR_KEYS stores each key MASKED, not in the clear. The app needs the real keys
    (it derives and decrypts with them), so this changes what a reader sees, not what the
    device computes -- obfuscation, not a security boundary.

        mask(k)   = ROTL(k ^ A, N) ^ B
        unmask(v) = ROTR(v ^ B, N) ^ A

    Every read of a table key goes through kl_unmask_key(). Lives in the header so the app and
    the host tests share one definition. Must match tools/mask_mfrkeys.py.                  */
#define MFR_KEY_A  0x5A5A5A5A5A5A5A5AULL
#define MFR_KEY_B  0x3C3C3C3C3C3C3C3CULL
#define MFR_KEY_N  13

static inline uint64_t kl_unmask_key(uint64_t v) {
    uint64_t x = (v ^ MFR_KEY_B);
    const uint8_t n = MFR_KEY_N;
    return ((x >> n) | (x << (64 - n))) ^ MFR_KEY_A;
}

/* ── API ──────────────────────────────────────────────────────────────────── */

/* Core cipher */
uint32_t kl_encrypt(uint32_t plain,  uint64_t key);
uint32_t kl_decrypt(uint32_t cipher, uint64_t key);

/* Self-test against 3 published reference vectors.  Returns true on pass. */
bool kl_self_test(void);

/* NLF (non-linear function) — exposed for decoder use */
uint32_t kl_nlf(uint32_t w);

/* PWM bit extraction from a raw pulse buffer.
   buf[]  = alternating HIGH/LOW durations in µs (buf[0] is HIGH).
   n      = number of pulses.
   te     = estimated chip period in µs.
   out    = output bit-string (ASCII '0'/'1', NUL-terminated).
   Returns number of bits written, 0 on failure. */
uint16_t kl_pwm(const uint32_t* buf, int n, uint32_t te, char* out);

/* Parse a PWM bit-string into a KLFrame.
   bits   = ASCII '0'/'1' string from kl_pwm().
   n      = length of bits.
   f      = output frame (caller provides).
   Returns true if the frame is structurally valid (≥66 bits, hop≠0, btn≠0). */
bool kl_parse(const char* bits, int n, KLFrame* f);

/* Derive up to MAX_DERIVED_KEYS entries from FLIPPER_MFR_KEYS.
   out    = caller-allocated array of DerivedKey[MAX_DERIVED_KEYS].
   Returns actual count written. */
int kl_derive_all_keys(DerivedKey* out);

/* Try every derived key against enc/sn; fill f->mfr_name/mfr_key/key/
   device_key_hex and f->dec on success.  Returns true if a key matched. */
bool kl_recover_key(KLFrame* f);

/* ── Next-code synthesis (matched key → fresh frame on the wire) ──────────── */
/* With a KEY that decrypted a captured frame, encrypt counter `ctr` under the
   same key and re-emit a complete PWM bitstream (preamble + enc + sn + btn +
   ovf/rep), LSB-first, alternating H/L durations at the given chip period.
   Returns true on success. */
bool flipper_kl_next_pulses(const KLFrame* f, uint32_t ctr, uint64_t key,
                            uint32_t te, float freq_mhz, FlipperPulseBuf* out);

/* True when a KeeLoq decode carries a recovered device key (hex) and can
   synthesize a valid next hop — not merely a counter window. */
bool flipper_predict_can_synth(const FlipperDecodeResult* r);

/* Build the next KeeLoq PWM frame (counter = cnt + offset, offset≥1) into
   `out` and update `out_decode` metadata.  Returns false if no key / bad TE. */
bool flipper_predict_keeloq_next(const FlipperDecodeResult* r, uint32_t offset,
                                 FlipperPulseBuf* out,
                                 FlipperDecodeResult* out_decode);

/* Register up-to-FLIPPER_KEYVAULT_MAX user vault keys to be appended to
   kl_recover_key()'s sweep.  Called whenever the vault changes (add/del/
   load) and at boot after SD load. */
void flipper_kl_set_vault_keys(const DerivedKey* keys, int n);
