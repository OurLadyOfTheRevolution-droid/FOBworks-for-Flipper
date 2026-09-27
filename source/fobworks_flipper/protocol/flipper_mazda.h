#pragma once

#include "flipper_decoders.h"

typedef struct {
    uint32_t serial;
    uint8_t button;
    uint32_t counter;
    uint8_t checksum;
    bool crc_ok;
} MazdaFrame;

uint8_t mazda_checksum(uint32_t serial, uint8_t button, uint32_t counter);
void mazda_decode_key(uint64_t rawkey, MazdaFrame* frame);
uint64_t mazda_encode_key(uint32_t serial, uint8_t button, uint32_t counter);