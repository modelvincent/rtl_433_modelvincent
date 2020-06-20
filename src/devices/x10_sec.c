/*
 * X10 Security sensor decoder.
 *
 * Each packet starts with a sync pulse of 9000 us and 4500 us gap.
 * The message is OOK PPM encoded with 567 us pulse and long gap (0 bit)
 * of 1680 us or short gap (1 bit) of 590 us. There are 41 bits, the
 * message is repeated 5 times with a packet gap of 40000 us.
 *
 * Tested with American sensors operating at 310 MHz
 * e.g., rtl_433 -f 310.558M
 *
 * This is pretty rudimentary, and I bet the byte value decoding, based
 * on limited observations, doesn't take into account bits that might
 * be set to indicate something like a low battery condition.
 *
 * DS10A door/window sensor bitmask : CUUUUDUB
 *      C = Door/window closed flag.
 *      U = Unknown. Cleared in all samples.
 *      D = Delay setting. Min=1. Max=0.
 *      B = Battery low flag.
 *
 * Copyright (C) 2018 Anthony Kava
 * Based on code provided by Willi 'wherzig' in issue #30 (2014-04-21)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#include "decoder.h"

static int x10_sec_callback(r_device *decoder, bitbuffer_t *bitbuffer) {
    data_t *data;
    uint16_t r;                          /* a row index              */
    uint8_t *b;                          /* bits of a row            */
    char *event_str = "UNKNOWN";         /* human-readable event     */
    char x10_id_str[12] = "";            /* string showing hex value */
    char x10_code_str[5] = "";           /* string showing hex value */
    int battery_low = 0;                 /* battery indicator (DS10A) */
    int delay = 0;                       /* delay setting (DS10A) */

    for (r=0; r < bitbuffer->num_rows; ++r) {
        b = bitbuffer->bb[r];

        /* looking for five bytes */
        if (bitbuffer->bits_per_row[r] < 40)
            continue; // DECODE_ABORT_LENGTH

        /* validate what we received */
        if ((b[0] ^ b[1]) != 0x0f || (b[2] ^ b[3]) != 0xff)
            continue; // DECODE_FAIL_SANITY

        battery_low = b[2] & 0x01;

        /* set event_str based on code received */
        switch (b[2] & 0xfe) {
            case 0x00:
            case 0x04:
                event_str = "DS10A DOOR/WINDOW OPEN";
                delay = !(b[2] & 0x04);
                break;
            case 0x80:
            case 0x84:
                event_str = "DS10A DOOR/WINDOW CLOSED";
                delay = !(b[2] & 0x04);
                break;
            case 0x06:
                event_str = "KR10A KEY-FOB ARM";
                break;
            case 0x0c:
                event_str = "MS10A MOTION TRIPPED";
                break;
            case 0x46:
                event_str = "KR10A KEY-FOB LIGHTS-ON";
                break;
            case 0x82:
                event_str = "SH624 SEC-REMOTE DISARM";
                break;
            case 0x86:
                event_str = "KR10A KEY-FOB DISARM";
                break;
            case 0x88:
                event_str = "KR15A PANIC";
                break;
            case 0x8c:
                event_str = "MS10A MOTION READY";
                break;
            case 0x98:
                event_str = "KR15A PANIC-3SECOND";
                break;
            case 0xc6:
                event_str = "KR10A KEY-FOB LIGHTS-OFF";
                break;
        }

        /* get x10_id_str, x10_code_str ready for output */
        sprintf(x10_id_str, "%02x%02x", b[0], b[4]);
        sprintf(x10_code_str, "%02x", b[2]);

        /* debug output */
        if (decoder->verbose) {
            fprintf(stderr, "X10SEC: id=%02x%02x code=%02x event_str=%s\n", b[0], b[4], b[2], event_str);
            bitbuffer_print(bitbuffer);
        }

        /* build and handle data set for normal output */
        data = data_make(
                "model",    "",             DATA_STRING, _X("X10-Security","X10 Security"),
                "id",       "Device ID",    DATA_STRING, x10_id_str,
                "code",     "Code",         DATA_STRING, x10_code_str,
                "event",    "Event",        DATA_STRING, event_str,
                NULL);

        /* append delay indicator if set */
        if (delay) {
            data = data_append(data,
                    "delay",        "Delay",        DATA_INT, delay,
                    NULL);
        }
        /* append battery indicator if set */
        if (battery_low) {
            data = data_append(data,
                    "battery_ok",   "Battery OK",   DATA_INT, !battery_low,
                    NULL);
        }

        decoder_output_data(decoder, data);
        return 1;
    }
    // TODO: improve decode logging with earlier returns
    return DECODE_ABORT_EARLY;
}

static char *output_fields[] = {
    "model",
    "id",
    "code",
    "event",
    "delay"
    "battery_ok",
    NULL
};

/* r_device definition */
r_device x10_sec = {
    .name           = "X10 Security",
    .modulation     = OOK_PULSE_PPM,
    .short_width    = 500,  // Short gap 500µs
    .long_width     = 1680, // Long gap 1680µs
    .gap_limit      = 2200, // Gap after sync is 4.5ms (1125)
    .reset_limit    = 6000,
    .decode_fn      = &x10_sec_callback,
    .disabled       = 0,
    .fields         = output_fields
};
