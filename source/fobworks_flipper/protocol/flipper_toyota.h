#pragma once

#include "flipper_decoders.h"

/* The Toyota/Denso 40-bit fields are interpreted from frame structure; the payload carries no transmitted checksum. This parser is force-only, so a match is not protocol authentication or evidence of receiver acceptance. */