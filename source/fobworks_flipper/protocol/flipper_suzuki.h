#pragma once
#include "flipper_decoders.h"

/* ─────────────────────────────────────────────────────────────────────────── */
/* Suzuki RKE decoder for 64-bit PWM rolling-code frames. Fobs may transmit at
   315 MHz or 433.92 MHz (AM or FM). TE is near 250 µs; a run of short/short
   pairs precedes the data. Each bit uses a HIGH width of 2×TE for 1 or 1×TE
   for 0, followed by a short LOW.

   Frame, MSB first: [counter 20][serial 28][button 4][CRC-8][0000].
   Button values: 1=Panic, 2=Trunk, 3=Lock, 4=Unlock.
   CRC-8 uses polynomial 0x7F and initial value 0x00 over payload bytes
   [59:12], before the CRC. This checksum is the decoder's Auto gate; a match
   does not establish receiver acceptance. */
/* ─────────────────────────────────────────────────────────────────────────── */

/* flipper_decode_suzuki() is declared canonically in flipper_decoders.h. */
