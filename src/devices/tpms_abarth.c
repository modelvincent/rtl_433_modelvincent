/** @file
    Jansite FSK 7 byte Manchester encoded checksummed TPMS data.
    Copyright (C) 2019 Andreas Spiess and Christian W. Zuckschwerdt <zany@triq.net>
    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

/**
Jansite Solar TPMS (Internal/External) Model TY02S
Working Temperature:-40 °C to 125 °C
Working Frequency: 433.92MHz+-38KHz
Tire monitoring range value: 0kPa-350kPa+-7kPa
Data layout (nibbles):
    II II II IS PP TT CC
- I: 28 bit ID
- S: 4 bit Status (deflation alarm, battery low etc)
- P: 8 bit Pressure (best guess quarter PSI, i.e. ~0.58 kPa)
- T: 8 bit Temperature (deg. C offset by 50)
- C: 8 bit Checksum
- The preamble is 0xaa..aa9 (or 0x55..556 depending on polarity)
*/

/**
Abarth 124 Spider TPMS
Protocol slightly similar (and based on) Jansite Solar TPMS by Andreas Spiess and Christian W. Zuckschwerdt

Data layout (nibbles):
    II II II II ?? PP TT SS CC

- I: 32 bit ID
- ?: 4 bit unknown (seems to change with status)
- ?: 4 bit unknown (seems static)
- P: 8 bit Pressure (multiplyed by 1.4 = kPa)
- T: 8 bit Temperature (deg. C offset by 50)
- C: 8 bit Checksum (Checksum8 XOR on bytes 0 to 8)

*/

#include "decoder.h"

// preamble
static const unsigned char preamble_pattern[] = {0xaa, 0xa9}; // after invert

static int tpms_abarth_decode(r_device *decoder, bitbuffer_t *bitbuffer, unsigned row, unsigned bitpos)
{
    data_t *data;
    unsigned int start_pos;
    bitbuffer_t packet_bits = {0};
    uint8_t *b;
    unsigned id;
    char id_str[7 + 1];
    char flags;
    int pressure;
    int temperature;
    char check;
    char code_str[7 * 2 + 1];

    start_pos = bitbuffer_manchester_decode(bitbuffer, row, bitpos, &packet_bits, 72);
    b = packet_bits.bb[0];

// check checksum checksum8 xor, not crc8
    if (crc8(b, 8, 0x07, 0x00) != b[8]) {
        //return 0;
    }

    id          = (unsigned)b[0] << 32 | b[1] << 24 | b[2] << 16 | b[3] << 8 | b[4] << 0;
    flags       = b[4];
    pressure    = b[5];
    temperature = b[6];
    status      = b[7];
    check       = b[6];
    sprintf(id_str, "%07x", id);
    sprintf(code_str, "%02x%02x%02x%02x%02x%02x%02x%02x%02x", b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7], b[8]);

    /* clang-format off */
    data = data_make(
            "model",            "",             DATA_STRING, "Abarth 124 Spider",
            "type",             "",             DATA_STRING, "TPMS",
            "id",               "",             DATA_STRING, id_str,
            "flags",            "",             DATA_STRING, flags,
            "pressure_kPa",     "Pressure",     DATA_FORMAT, "%.0f kPa", DATA_DOUBLE, (double)pressure * 1.4,
            "temperature_C",    "Temperature",  DATA_FORMAT, "%.0f C", DATA_DOUBLE, (double)temperature - 50.0,
            "status",           "",             DATA_INT, status,
            "code",             "",             DATA_STRING, code_str,
            "check",            "",             DATA_STRING, check,
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static int tpms_abarth_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    unsigned bitpos = 0;
    int events      = 0;

    bitbuffer_invert(bitbuffer);
    // Find a preamble with enough bits after it that it could be a complete packet
    while ((bitpos = bitbuffer_search(bitbuffer, 0, bitpos, (uint8_t *)&preamble_pattern, 24)) + 80 <=
            bitbuffer->bits_per_row[0]) {
        events += tpms_abarth_decode(decoder, bitbuffer, 0, bitpos + 24);
        bitpos += 2;
    }

    return events;
}

static char *output_fields[] = {
        "model",
        "type",
        "id",
        "flags",
        "pressure_kPa",
        "temperature_C",
        "status",
        "code",
        "check",
        NULL,
};

r_device tpms_abarth = {
        .name        = "Abarth 124 Spider TPMS",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 52,  // 12-13 samples @250k
        .long_width  = 52,  // FSK
        .reset_limit = 150, // Maximum gap size before End Of Message [us].
        .decode_fn   = &tpms_abarth_callback,
        .disabled    = 0,
        .fields      = output_fields,
};
