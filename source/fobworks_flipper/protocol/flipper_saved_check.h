#pragma once

#include <stdbool.h>
#include "flipper_decoders.h"

/* Read-only judgement of a saved RAW pulse train. Force means a parser accepted
   under a force-only decoder; it is not protocol verification. */
typedef enum {
    FlipperSavedNoWave = 0,
    FlipperSavedAuto,
    FlipperSavedForce,
    FlipperSavedNone,
} FlipperSavedKind;

typedef struct {
    FlipperSavedKind kind;
    char line1[22];
    char line2[22];
    char line3[22];
} FlipperSavedCheck;

/* protocol_field is the file's Protocol: value, or NULL. RAW/empty means the
   file did not name a decoder. pulses may be NULL or short. */
void flipper_saved_judge(
    const FlipperPulseBuf* pulses, const char* protocol_field,
    FlipperSavedCheck* out);