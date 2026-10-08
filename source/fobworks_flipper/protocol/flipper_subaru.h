#pragma once
#include "flipper_decoders.h"

/* ─────────────────────────────────────────────────────────────────────────── */
/* Subaru RKE decoder for the sequential-counter frame family. Supported coverage includes 2004–2011 Impreza, Forester, Legacy, Outback, and Baja remotes. They use an 80-bit OOK Manchester frame at 433.92 MHz (export) or 315 MHz with a sequential, not random, counter. Frame: [0x55 sync][24-bit serial][command nibble ×2][20-bit counter] [checksum nibble]. The start byte, repeated command nibble, and nibble-XOR checksum are my Auto parser's structural gate. A matching frame is not evidence that a receiver will accept a replay or counter sequence. */
/* ─────────────────────────────────────────────────────────────────────────── */

/* I declare flipper_decode_subaru() canonically in flipper_decoders.h. */
