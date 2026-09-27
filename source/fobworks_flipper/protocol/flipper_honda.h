#pragma once

#include "flipper_decoders.h"

/* The regular source-layout parser is provisional and force-only; its
   integrity is unverified, and it does not perform prediction/key recovery.
   The separate KR5 decoder is checksum-gated and participates in Auto. */