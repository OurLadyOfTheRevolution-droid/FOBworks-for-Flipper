#pragma once
#include "flipper_decoders.h"

/* ─────────────────────────────────────────────────────────────────────────── */
/* Suzuki RKE decoder — FOBworks for Flipper.                                  */
/*                                                                             */
/* Suzuki keyless fobs transmit a 64-bit PWM rolling-code frame at 315 MHz /   */
/* 433.92 MHz (AM or FM).  A long run of short/short preamble pairs precedes    */
/* the data; each data bit is carried by the HIGH width (2×TE = 1, 1×TE = 0)    */
/* with a short LOW between bits.  TE ≈ 250 µs.                                 */
/*                                                                             */
/* Frame (64-bit, MSB-first):                                                  */
/*   [counter 20][serial 28][button 4][CRC-8][0000]                            */
/*   button: 1=Panic 2=Trunk 3=Lock 4=Unlock                                   */
/*   CRC-8: poly 0x7F, init 0x00, over payload bytes [59:12] (before the CRC)  */
/*                                                                             */
/* The 8-bit CRC is a strong gate, so this decoder is Auto-safe.               */
/* ─────────────────────────────────────────────────────────────────────────── */

/* flipper_decode_suzuki() is declared canonically in flipper_decoders.h. */
