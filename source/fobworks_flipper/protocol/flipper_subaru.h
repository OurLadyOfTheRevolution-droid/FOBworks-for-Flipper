#pragma once
#include "flipper_decoders.h"

/* ─────────────────────────────────────────────────────────────────────────── */
/* Subaru RKE decoder — FOBworks for Flipper (rollback family).                */
/*                                                                             */
/* Subaru keyless fobs (2004-2011 Impreza/Forester/Legacy/Outback/Baja)         */
/* transmit an 80-bit OOK Manchester frame at 433.92 MHz (export) / 315 MHz     */
/* with a SEQUENTIAL (non-random) rolling counter — the well-documented Subaru   */
/* weakness that enables clone + rollback.                                      */
/*                                                                             */
/* Frame: [0x55 sync][serial 24][cmd nibble ×2][counter 20][checksum nibble].   */
/* The 0x55 start + duplicated command nibble + nibble-XOR checksum form a       */
/* strong gate, so this decoder is Auto-safe.  FOBback drives the rollback with  */
/* the recovered sequential counter.                                            */
/* ─────────────────────────────────────────────────────────────────────────── */

/* flipper_decode_subaru() is declared canonically in flipper_decoders.h. */
