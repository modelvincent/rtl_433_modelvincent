/** @file
    X10 sensor (Stub for decoding test data only).

    Copyright (C) 2015 Tommy Vestermark

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/
/**
X10  sensor decoder.

Each packet starts with a sync pulse of 9000 us (16x a bit time) 
and a 500 us gap.
The message is OOK PPM encoded with 562 us pulse and long gap (0 bit)
of 1687 us or short gap (1 bit) of 562 us.

There are 32bits, the message is repeated 5 times with
a packet gap of 40000 us.

The protocol has a lot of similarities to the NEC IR protocol

The second byte is the inverse of the first.
The fourth byte is the inverse of the third.

Based on protocol informtation found at:
http://www.wgldesigns.com/protocols/w800rf32_protocol.txt

Tested with American sensors operating at 310 MHz
e.g., rtl_433 -f 310M

Tested with HR12A, RMS18, 

*/

#include "decoder.h"

static int x10_rf_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    data_t *data;
    uint8_t *b = bitbuffer->bb[1];

    uint8_t arrbKnownConstBitMask[4]  = {0x0B, 0x0B, 0x07, 0x07};
    uint8_t arrbKnownConstBitValue[4] = {0x00, 0x0B, 0x00, 0x07};

    // Row [0] is sync pulse
    // Validate length
    if (bitbuffer->bits_per_row[1] != 32) { // Don't waste time on a wrong length package
        if (decoder->verbose)
            fprintf(stderr, "X10-RF: DECODE_ABORT_LENGTH, Received message length=%i\n", bitbuffer->bits_per_row[1]);
        return DECODE_ABORT_LENGTH;
    }

    // Validate complement values
    if ((b[0] ^ b[1]) != 0xff || (b[2] ^ b[3]) != 0xff) {
        if (decoder->verbose)
            fprintf(stderr, "X10-RF: DECODE_FAIL_SANITY, b0=%02x b1=%02x b2=%02x b3=%02x\n", b[0], b[1], b[2], b[3]);
        return DECODE_FAIL_SANITY;
    }

    // Some bits are constant.
    for (int8_t bIdx = 0; bIdx < 4; bIdx++) {
        uint8_t bTest = arrbKnownConstBitMask[bIdx] & b[bIdx];  // Mask the appropriate bits

        if (bTest != arrbKnownConstBitValue[bIdx]) {  // If resulting bits are incorrectly set
            if (decoder->verbose)
                fprintf(stderr, "X10-RF: DECODE_FAIL_SANITY, b0=%02x b1=%02x b2=%02x b3=%02x\n", b[0], b[1], b[2], b[3]);
            return DECODE_FAIL_SANITY;
        }
    }

    // We have received a valid message, decode it

    unsigned code = (unsigned)b[0] << 24 | b[1] << 16 | b[2] << 8 | b[3];

    uint8_t bHouseCode  = 0;
    uint8_t bDeviceCode = 0;
    uint8_t arrbHouseBits[4] = {0, 0, 0, 0};

    // Extract House bits
    arrbHouseBits[0] = (b[0] & 0x80) >> 7;
    arrbHouseBits[1] = (b[0] & 0x40) >> 6;
    arrbHouseBits[2] = (b[0] & 0x20) >> 5;
    arrbHouseBits[3] = (b[0] & 0x10) >> 4;

    // Convert bits into integer
    bHouseCode   = (~(arrbHouseBits[0] ^ arrbHouseBits[1])  & 0x01) << 3;
    bHouseCode  |= ( ~arrbHouseBits[1]                      & 0x01) << 2;
    bHouseCode  |= ( (arrbHouseBits[1] ^ arrbHouseBits[2])  & 0x01) << 1;
    bHouseCode  |=    arrbHouseBits[3]                      & 0x01;

    // Extract and convert Unit bits to integer
    bDeviceCode  = (b[0] & 0x04) << 1;
    bDeviceCode |= (b[2] & 0x40) >> 4;
    bDeviceCode |= (b[2] & 0x08) >> 2;
    bDeviceCode |= (b[2] & 0x10) >> 4;
    bDeviceCode += 1;

    char housecode[2] = {0};
    *housecode = bHouseCode + 'A';

    int state = (b[2] & 0x20) == 0x00;

    char *event_str = "UNKNOWN";         // human-readable event

    if ((b[2] & 0x80) == 0x80) {         // Dim Bright bit
        bDeviceCode = 0;                 // No device for dim and bright
        event_str = ((b[2] & 0x10) == 0x10) ? "DIM" : "BRI";
    }
    else {
        event_str = state ? "ON" : "OFF";
    }

    // debug output
    if (decoder->verbose) {
        fprintf(stderr, "X10-RF: id=%s%i event_str=%s\n", housecode, bDeviceCode, event_str);
        bitbuffer_print(bitbuffer);
    }

    data = data_make(
            "model",                   "", DATA_STRING, "X10-RF",
            _X("id", "deviceid"),      "", DATA_INT,    bDeviceCode,
            _X("channel", "houseid"),  "", DATA_STRING, housecode,
            "state",              "State", DATA_STRING, event_str,
            "data",                "Data", DATA_FORMAT, "%08x", DATA_INT, code,
            NULL);

    decoder_output_data(decoder, data);

    return 1;
}

static char *output_fields[] = {
        "model",
        "channel",
        "id",
        "houseid",  // TODO: remove ??
        "deviceid", // TODO: remove ??
        "state",
        "data",
        NULL,
};

r_device X10_RF = {
        .name        = "X10 RF",
        .modulation  = OOK_PULSE_PPM,
        .short_width = 562,  // Short gap 562.5 µs
        .long_width  = 1687, // Long gap 1687.5 µs
        .gap_limit   = 2200, // Gap after sync is 4.5ms (1125)
        .reset_limit = 6000, // Gap seen between messages is ~40ms so let's get them individually
        .decode_fn   = &x10_rf_callback,
        .disabled    = 1,
        .fields      = output_fields,
};
