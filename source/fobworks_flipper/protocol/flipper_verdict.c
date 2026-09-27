#include "flipper_verdict.h"
#include "flipper_keeloq.h"
#include <string.h>
#include <stdio.h>

void flipper_session_reset(FlipperSession* s) {
    if(s) memset(s, 0, sizeof(*s));
}

void flipper_session_push(FlipperSession* s, const FlipperDecodeResult* r) {
    if(!s || !r || !r->proto[0]) return;
    if(s->n > 0 && (s->addr != r->addr || strcmp(s->proto, r->proto) != 0)) {
        /* New transmitter — start over so the recommendation stays about one fob. */
        memset(s, 0, sizeof(*s));
    }
    if(s->n == 0) {
        s->addr = r->addr;
        strncpy(s->proto, r->proto, sizeof(s->proto) - 1);
    }
    s->rolling = r->rolling;
    s->replay = r->replay_vuln;
    if(s->n < FLIPPER_SESSION_MAX) {
        /* Skip an immediate duplicate counter (button held). */
        if(s->n == 0 || s->cnt[s->n - 1] != r->cnt)
            s->cnt[s->n++] = r->cnt;
    } else {
        memmove(&s->cnt[0], &s->cnt[1], sizeof(s->cnt[0]) * (FLIPPER_SESSION_MAX - 1));
        s->cnt[FLIPPER_SESSION_MAX - 1] = r->cnt;
    }
}

/* Longest run of counters that step by 1..4 (wrap on 16 bits). */
static int session_run(const FlipperSession* s) {
    if(!s || s->n < 2 || !s->rolling) return s ? s->n : 0;
    int best = 1, run = 1;
    for(int i = 1; i < s->n; i++) {
        uint32_t d = (s->cnt[i] - s->cnt[i - 1]) & 0xFFFF;
        if(d >= 1 && d <= 4) run++;
        else run = 1;
        if(run > best) best = run;
    }
    return best;
}

/* Rough OOK-vs-2FSK hint from edge lengths. Empty string if unsure. */
static void preset_hint(const FlipperPulseBuf* pulses, char* out, size_t n) {
    if(out && n) out[0] = '\0';
    if(!pulses || !out || n < 8 || pulses->len < 16) return;
    int ook = 0, fsk = 0, seen = 0;
    for(int i = 0; i + 1 < pulses->len && seen < 40; i += 2, seen++) {
        uint32_t a = pulses->durations[i];
        uint32_t b = pulses->durations[i + 1];
        if(a < 40 || b < 40 || a > 8000 || b > 8000) continue;
        uint32_t hi = a > b ? a : b;
        uint32_t lo = a > b ? b : a;
        if(lo == 0) continue;
        /* PWM OOK: one edge ~2× the other. 2FSK/Manchester: edges similar. */
        if(hi > lo + lo / 2) ook++;
        else if(hi < lo + lo / 3) fsk++;
    }
    if(ook >= 8 && ook > fsk * 2)
        snprintf(out, n, "try OOK");
    else if(fsk >= 8 && fsk > ook * 2)
        snprintf(out, n, "try 2FSK");
}

const char* flipper_conf_name(FlipperConf c) {
    switch(c) {
    case FlipperConfAuto:  return "Auto";
    case FlipperConfForce: return "Force";
    default:               return "None";
    }
}

const char* flipper_act_name(FlipperAct a) {
    switch(a) {
    case FlipperActSave:    return "Save";
    case FlipperActPredict: return "Predict";
    case FlipperActResync:  return "Resync";
    case FlipperActReplay:  return "Replay";
    default:                return "None";
    }
}

void flipper_verdict_build(FlipperVerdict* v, const FlipperDecodeResult* r,
                           bool decoded, bool was_auto, const FlipperSession* s,
                           const FlipperPulseBuf* pulses) {
    if(!v) return;
    memset(v, 0, sizeof(*v));
    if(!decoded || !r || !r->proto[0]) {
        v->conf = FlipperConfNone;
        v->act = FlipperActNone;
        snprintf(v->line_proto, sizeof(v->line_proto), "No decode");
        snprintf(v->line_conf, sizeof(v->line_conf), "Confidence: None");
        snprintf(v->line_act, sizeof(v->line_act), "Not enough");
        preset_hint(pulses, v->preset_hint, sizeof(v->preset_hint));
        return;
    }

    v->conf = was_auto ? FlipperConfAuto : FlipperConfForce;
    v->key_known = flipper_predict_can_synth(r);
    v->presses = s ? s->n : 1;
    int run = session_run(s);

    if(r->replay_vuln && !r->rolling)
        v->act = FlipperActReplay;
    else if(r->rolling && run >= 3)
        v->act = FlipperActResync;
    else if(v->key_known)
        v->act = FlipperActPredict;
    else
        v->act = FlipperActSave;

    memset(v->line_proto, 0, sizeof(v->line_proto));
    strncpy(v->line_proto, r->proto, sizeof(v->line_proto) - 1);
    snprintf(v->line_conf, sizeof(v->line_conf), "%s  key:%s  x%d",
             flipper_conf_name(v->conf), v->key_known ? "yes" : "no", v->presses);
    switch(v->act) {
    case FlipperActPredict: snprintf(v->line_act, sizeof(v->line_act), "OK = predict next"); break;
    case FlipperActResync:  snprintf(v->line_act, sizeof(v->line_act), "OK = open Resync"); break;
    case FlipperActReplay:  snprintf(v->line_act, sizeof(v->line_act), "OK = replay note"); break;
    case FlipperActSave:    snprintf(v->line_act, sizeof(v->line_act), "Saved — need key"); break;
    default:                snprintf(v->line_act, sizeof(v->line_act), "Not enough"); break;
    }
    preset_hint(pulses, v->preset_hint, sizeof(v->preset_hint));
}
