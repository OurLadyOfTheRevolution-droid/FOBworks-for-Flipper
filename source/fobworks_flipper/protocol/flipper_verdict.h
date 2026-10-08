#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "flipper_decoders.h"

/* I summarize one capture session. Auto means a decoder's built-in gate accepted the frame; Force means it matched a user-selected or weaker decoder; None means no decoder matched. These labels describe the parser path, not vehicle receiver acceptance. The action is the next step supported by this capture. */

typedef enum {
    FlipperConfNone = 0,
    FlipperConfAuto,
    FlipperConfForce,
} FlipperConf;

typedef enum {
    FlipperActNone = 0,     /* no decoder match */
    FlipperActSave,         /* I save the decoded capture */
    FlipperActPredict,      /* a KeeLoq device key is available */
    FlipperActResync,       /* same serial, 3+ counters step by 1–4 */
    FlipperActReplay,       /* fixed or unencrypted code */
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
    int         presses;          /* captures for this serial in the session */
    char        line_proto[24];
    char        line_conf[24];
    char        line_act[24];
    char        preset_hint[20]; /* empty, "try OOK", or "try 2FSK" */
} FlipperVerdict;

void flipper_session_reset(FlipperSession* s);
void flipper_session_push(FlipperSession* s, const FlipperDecodeResult* r);

/* I build the verdict from the decoder result and session. was_auto is true only when the automatic decoder chain accepted the frame. */
void flipper_verdict_build(FlipperVerdict* v, const FlipperDecodeResult* r,
                           bool decoded, bool was_auto, const FlipperSession* s,
                           const FlipperPulseBuf* pulses);

const char* flipper_conf_name(FlipperConf c);
const char* flipper_act_name(FlipperAct a);
