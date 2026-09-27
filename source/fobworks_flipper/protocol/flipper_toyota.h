#pragma once

#include "flipper_decoders.h"

/* Toyota/Denso's extracted 40-bit fields are a structural interpretation:
   the payload has no transmitted checksum. The decoder is force-only. */