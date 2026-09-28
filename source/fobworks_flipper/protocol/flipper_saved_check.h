#pragma once

#include <stdbool.h>
#include "flipper_decoders.h"

/* Read-only check of a saved RAW pulse train. Force means a force-only parser
   accepted the data; it does not verify that the protocol label is correct. */
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

/* protocol_field is the file's Protocol: value, or NULL. An empty or RAW value
   means the file did not name a decoder. pulses may be NULL or too short. */
void flipper_saved_judge(
    const FlipperPulseBuf* pulses, const char* protocol_field,
    FlipperSavedCheck* out);