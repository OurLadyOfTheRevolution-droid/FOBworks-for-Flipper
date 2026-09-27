#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "flipper_decoders.h"

/* One capture session → one verdict.
   Confidence is Auto (CRC/header gate), Force (user-pinned or weak), or None.
   The action is the single next step that this burst actually supports. */

typedef enum {
    FlipperConfNone = 0,
    FlipperConfAuto,
    FlipperConfForce,
} FlipperConf;

typedef enum {
    FlipperActNone = 0,     /* no decode                                         */
    FlipperActSave,         /* decoded, nothing else to do yet                   */
    FlipperActPredict,      /* KeeLoq device key in hand                         */
    FlipperActResync,       /* same serial, ≥3 counters stepping by 1..4         */
    FlipperActReplay,       /* fixed / unencrypted code                          */
} FlipperAct;

#define FLIPPER_SESSION_MAX 8

typedef struct {
    uint32_t addr;
    uint32_t cnt[FLIPPER_SESSION_MAX];
    int      n;
    bool     rolling;
    bool     replay;
    char     proto[32];
} FlipperSession;

typedef struct {
    FlipperConf conf;
    FlipperAct  act;
    bool        key_known;
    int         presses;          /* presses of this serial in the session      */
    char        line_proto[24];
    char        line_conf[24];
    char        line_act[24];
    char        preset_hint[20]; /* "" or "try OOK" / "try 2FSK"                 */
} FlipperVerdict;

void flipper_session_reset(FlipperSession* s);
void flipper_session_push(FlipperSession* s, const FlipperDecodeResult* r);

/* decoded=false → None. was_auto is true only when the Auto chain accepted it. */
void flipper_verdict_build(FlipperVerdict* v, const FlipperDecodeResult* r,
                           bool decoded, bool was_auto, const FlipperSession* s,
                           const FlipperPulseBuf* pulses);

const char* flipper_conf_name(FlipperConf c);
const char* flipper_act_name(FlipperAct a);
