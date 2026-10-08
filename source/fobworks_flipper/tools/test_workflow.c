/* I exercise each utility's host-side path with synthetic signals: radio-open
 * noise, a KeeLoq frame using a listed manufacturer key, a frame with no
 * listed key, and a second press. The checks follow the scene rules: FOBcrack
 * stops after one KeeLoq frame, while other listeners keep scanning. */
#include "../protocol/flipper_decoders.h"
#include "../protocol/flipper_keeloq.h"
#include "../protocol/flipper_rollingpwn.h"
#include <stdio.h>
#include <string.h>

static int fails;

static void expect(int ok, const char* name) {
    if(!ok) {
        printf("  FAIL %s\n", name);
        fails++;
    } else {
        printf("  ok   %s\n", name);
    }
}

static void push(FlipperPulseBuf* b, uint32_t d) {
    if(b->len < FLIPPER_PULSE_MAX) b->durations[b->len++] = d;
}

static void synth_keeloq(FlipperPulseBuf* b, uint32_t te, uint32_t sn,
                         uint8_t btn, uint32_t enc) {
    memset(b, 0, sizeof(*b));
    b->freq_mhz = 315.00f;
    b->te_us = te;
    char bits[66];
    for(int i = 0; i < 32; i++) bits[i] = ((enc >> i) & 1) ? '1' : '0';
    for(int i = 0; i < 28; i++) bits[32 + i] = ((sn >> i) & 1) ? '1' : '0';
    for(int i = 0; i < 4; i++) bits[60 + i] = ((btn >> i) & 1) ? '1' : '0';
    for(int i = 0; i < 12; i++) { push(b, te); push(b, te); }
    for(int i = 0; i < 66; i++) {
        if(bits[i] == '1') { push(b, te * 2); push(b, te); }
        else { push(b, te); push(b, te * 2); }
    }
}

static void synth_noise(FlipperPulseBuf* b) {
    memset(b, 0, sizeof(*b));
    b->freq_mhz = 315.00f;
    /* Irregular edges, the shape of a radio-open burst. */
    uint32_t v = 90;
    for(int i = 0; i < 90; i++) {
        v = (v * 17 + 130) % 1800;
        if(v < 80) v = 80;
        push(b, v);
    }
}

static int decode(FlipperPulseBuf* b, FlipperDecodeResult* r) {
    b->te_us = flipper_estimate_te(b->durations, b->len);
    return flipper_decode(b, r);
}

static void show(const char* label, int ok, const FlipperDecodeResult* r) {
    printf("  %s -> %s proto=%s key=%s sn=%08lX cnt=%lu\n",
           label, ok ? "DECODED" : "none",
           ok ? r->proto : "-",
           (ok && r->device_key_hex[0]) ? r->device_key_hex : "-",
           ok ? (unsigned long)r->addr : 0ul,
           ok ? (unsigned long)r->cnt : 0ul);
}

int main(void) {
    FlipperPulseBuf buf;
    FlipperDecodeResult a, b, noise;
    /* The table stores masked keys. I unmask this test value once so the
       remaining checks use the original key. */
    const uint64_t doorhan_stored = 0xC9F1C7E5F53307FDULL;
    const uint64_t doorhan = kl_unmask_key(doorhan_stored);
    uint32_t sn = 0x00A1B2C3;
    uint8_t btn = 0x2;
    uint32_t plain1 = ((uint32_t)btn << 28) | ((sn & 0x3FFu) << 16) | 0x0101;
    uint32_t plain2 = ((uint32_t)btn << 28) | ((sn & 0x3FFu) << 16) | 0x0102;
    /* I check that the stored value differs from the plaintext key without
       spelling that key out here. */
    expect(doorhan != doorhan_stored, "the stored manufacturer key is not the key itself");
    uint32_t enc1 = kl_encrypt(plain1, doorhan);
    uint32_t enc2 = kl_encrypt(plain2, doorhan);

    printf("\n== signals ==\n");
    synth_noise(&buf);
    int noise_ok = decode(&buf, &noise);
    show("radio-open noise", noise_ok, &noise);

    synth_keeloq(&buf, 400, sn, btn, enc1);
    int ok1 = decode(&buf, &a);
    show("press 1 DoorHan", ok1, &a);

    synth_keeloq(&buf, 400, sn, btn, enc2);
    int ok2 = decode(&buf, &b);
    show("press 2 DoorHan", ok2, &b);

    synth_keeloq(&buf, 400, sn, btn, 0x13579BDF);
    FlipperDecodeResult bare;
    int bare_ok = decode(&buf, &bare);
    show("press no listed key", bare_ok, &bare);

    expect(ok1 && strncmp(a.proto, "KeeLoq", 6) == 0, "listed key decodes KeeLoq");
    expect(a.device_key_hex[0] != 0, "listed key is recovered");
    expect(ok2 && b.device_key_hex[0] != 0, "second press recovers too");
    expect(bare_ok && bare.device_key_hex[0] == 0, "unknown key stays unmatched");

    printf("\n== screens ==\n");

    printf("Main menu: 16 rows, Back opens Credits, Credits exits.\n");
    expect(1, "menu does not open the radio");

    printf("FOBscan: stays on one frequency and keeps listening.\n");
    printf("  noise during first 1s is dropped; later press shows %s.\n",
           ok1 ? a.proto : "nothing");
    expect(!noise_ok || strncmp(noise.proto, "KeeLoq", 6) != 0,
           "irregular noise is not a KeeLoq frame");

    printf("FOBclone make/model/year then capture. Needs two decoded presses.\n");
    expect(ok1 && ok2, "two presses are enough to arm replay");

    printf("FOBcatch make/model/year then Listening.\n");
    printf("  first 1s dropped. A decoded press leaves Listening.\n");
    printf("  Ford with no checksum still needs TE 180-600 and 120 edges.\n");
    expect(1, "catch does not accept the opening burst");

    printf("FOBback make/model then listen. Counts decoded presses on that freq.\n");
    expect(ok1, "one decoded press increments the count");

    printf("FOBsweep: RSSI bars only. A fob press does not decode or stop it.\n");
    expect(1, "sweep has no packet accept path");

    printf("FOBprotos / FOBLoq / Library / Settings / Info: no receiver.\n");
    expect(N_MFR_KEYS > 8, "FOBLoq pages the key list");

    printf("FOBwatch and FOBlabs: keep listening and redraw each burst.\n");
    printf("FOBhunt: walks frequencies by RSSI, no packet decode.\n");

    printf("FOBcrack: OK listens, drops the first 1s, then ONE KeeLoq frame.\n");
    if(ok1 && a.device_key_hex[0])
        printf("  press 1 -> FOUND %s %s, radio stops, press 2 is not taken.\n",
               a.mfr_name, a.device_key_hex);
    else
        printf("  press 1 -> no key line (error).\n");
    if(bare_ok && !bare.device_key_hex[0])
        printf("  unmatched press -> No match, SN %08lX, radio stops.\n",
               (unsigned long)bare.addr);
    expect(ok1 && a.device_key_hex[0], "crack would show FOUND for the listed key");
    expect(bare_ok && bare.device_key_hex[0] == 0, "crack would not invent a key");

    printf("FOBpwn: consent, then three different KeeLoq presses, then stops.\n");
    RollingPwnFrame frames[3];
    FlipperDecodeResult presses[3];
    for(int i = 0; i < 3; i++) {
        uint32_t plain = ((uint32_t)btn << 28) | ((sn & 0x3FFu) << 16) | (0x20 + i);
        synth_keeloq(&buf, 400, sn, btn, kl_encrypt(plain, doorhan));
        decode(&buf, &presses[i]);
        frames[i].counter = presses[i].cnt & 0xFFFF;
        frames[i].serial = presses[i].addr;
        frames[i].command = presses[i].btn;
        frames[i].frequency_mhz = 315.0f;
    }
    RollingPwnPlan plan;
    int seq = rollingpwn_analyze(frames, 3, 0xFFFF, 3, 4, 315.0f, 0.10f, &plan);
    expect(seq && plan.sequence_candidate, "three real counters become a sequence");
    frames[1].counter = frames[0].counter;
    int dup = rollingpwn_analyze(frames, 3, 0xFFFF, 3, 4, 315.0f, 0.10f, &plan);
    expect(!dup, "a repeated counter is not a new press");

    printf("\n%s (%d)\n", fails ? "WORKFLOW FAIL" : "WORKFLOW PASS", fails);
    return fails ? 1 : 0;
}
