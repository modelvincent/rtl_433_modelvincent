/** @file
    AOK Electronic Limited weather station.

    Copyright (C) 2023 Bruno OCTAU (ProfBoc75) (improve and add AOK-5056)
    Copyright (C) 2019 Ryan Mounce <ryan@mounce.com.au> (PCM version)
    Copyright (C) 2018 Brad Campbell (PWM version)

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
 */
/**
AOK Electronic Limited weather station.

Known Rebrand compatible with:
- Holman iWeather Station ws5029. https://www.holmanindustries.com.au/products/iweather-station/
- Conrad Renkforce AOK-5056
- Optex Electronique 99018 SM-018 5056

Appears to be related to the Fine pos WH1080 and Digitech XC0348.

- Modulation: FSK PCM
- Frequency: 917.0 MHz +- 40 kHz
- 10 kb/s bitrate, 100 us symbol/bit time

A transmission burst is sent every 57 seconds. Each burst consists of 3
repetitions of the same "package" separated by a 1 ms gap.
The length of 196 or 218 bits depends on the device type.

Package format:
- Preamble            {48}0xAAAAAAAAAAAA
- Header              {24}0x98F3A5
- Payload             {96 or 146} see below
- zeros               {36} 0 with battery ?
- Checksum/CRC        {8}  xor bytes checksum
- Trailer/postamble   {20} direction (previous ?) and 3 zeros

Payload format: Without UV Lux sensor

    Fixed Values 0x  : AA AA AA AA AA AA 98 F3 A5

    Byte position    : 00 01 02 03 04 05 06 07 08 09 10 11 12 13 14 15
    Payload          : II II CC CH HR RR WW Dx xx xx ?x xx ss 0d 00 0

- IIII        station ID (randomised on each battery insertion)
- CCC         degrees C, signed, in multiples of 0.1 C
- HH          humidity %
- RRR         cumulative rain in multiples of 0.79 mm
- WW          wind speed in km/h
- D           wind direction (0 = N, 4 = E, 8 = S, 12 = W)
- xxxxxxxxx   ???, usually zero
- ss          XOR checksum, lower nibble properly decoded, not the upper, unknown calcul.

Payload format: With UV Lux sensor

    Fixed Values 0x  : AA AA AA AA AA AA 98 F3 A5

    Byte position    : 00 01 02 03 04 05 06 07 08 09 10 11 12 13 14 15 16 17 18
    Payload          : II II CC CH HR RR WW DU UL LL BN NN SS 0D 00 00 00 00 0

- IIII        station ID (randomised on each battery insertion)
- CCC         degrees C, signed, in multiples of 0.1 C
- HH          humidity %
- RRR         cumulative rain in mm
- WW          wind speed in km/h
- D           wind direction (0 = N, 4 = E, 8 = S, 12 = W)
- UU          Index UV
- LLLB        Lux
- B           Batterie
- NNN         Payload number, increase at each message 000->FFF but not always, strange behavior. no clue
- SS          XOR bytes checksum, lower nibble properly decoded, not the upper, unknown calcul.
- D           Previous Wind direction other values
- Fixed values to 9 zeros

To get raw data
$ rtl_433 -f 917M -X 'name=AOK,modulation=FSK_PCM,short=100,long=100,preamble={48}0xAAAAAA98F3A5,reset=22000'

@sa holman_ws5029pwm_decode()

*/

#include "decoder.h"

static int holman_ws5029pcm_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    int const wind_dir_degr[] = {0, 23, 45, 68, 90, 113, 135, 158, 180, 203, 225, 248, 270, 293, 315, 338};
    uint8_t const preamble[] = {0xAA, 0xAA, 0xAA, 0x98, 0xF3, 0xA5};

    data_t *data;
    uint8_t b[18];

    if (bitbuffer->num_rows != 1) {
        if (decoder->verbose) {
            fprintf(stderr, "%s: wrong number of rows (%d)\n", __func__, bitbuffer->num_rows);
        }
        return DECODE_ABORT_EARLY;
    }

    unsigned bits = bitbuffer->bits_per_row[0];

    if (bits < 192 ) {                 // too small
        return DECODE_ABORT_LENGTH;
    }

    unsigned pos = bitbuffer_search(bitbuffer, 0, 0, preamble, sizeof (preamble) * 8);

    if (pos >= bits) {
        return DECODE_ABORT_EARLY;
    }

    decoder_logf(decoder, 2, __func__, "Found AOK preamble pos: %d", pos);

    pos += sizeof(preamble) * 8;

    bitbuffer_extract_bytes(bitbuffer, 0, pos, b, sizeof(b) * 8);

    if ((xor_bytes(b, 12) & 0x0f) != (b[12] & 0x0f)) {    //lower nibble match xor, upper nibble does not match any crc or checksum
        decoder_log(decoder, 2, __func__, "Checksum fail");
        return DECODE_FAIL_MIC;
    }

    int device_id     = (b[0] << 8) | b[1];
    int temp_raw      = (int16_t)((b[2] << 8) | (b[3] & 0xf0)); // uses sign-extend
    float temp_c      = (temp_raw >> 4) * 0.1f;
    int humidity      = ((b[3] & 0x0f) << 4) | ((b[4] & 0xf0) >> 4);
    int rain_raw      = ((b[4] & 0x0f) << 8) | b[5];
    int speed_kmh     = b[6];
    int direction_deg = wind_dir_degr[(b[7] & 0xf0) >> 4];

    if (bits < 200) {                 // model without UV LUX
        float rain_mm     = rain_raw * 0.79f;

        /* clang-format off */
        data = data_make(
                "model",            "",                 DATA_STRING, "Holman-WS5029",
                "id",               "StationID",        DATA_FORMAT, "%04X",     DATA_INT,    device_id,
                "temperature_C",    "Temperature",      DATA_FORMAT, "%.01f C",  DATA_DOUBLE, temp_c,
                "humidity",         "Humidity",         DATA_FORMAT, "%u %%",    DATA_INT,    humidity,
                "rain_mm",          "Total rainfall",   DATA_FORMAT, "%.01f mm", DATA_DOUBLE, rain_mm,
                "wind_avg_km_h",    "Wind avg speed",   DATA_FORMAT, "%u km/h",  DATA_INT,    speed_kmh,
                "wind_dir_deg",     "Wind Direction",   DATA_INT, direction_deg,
                NULL);
        /* clang-format on */

        decoder_output_data(decoder, data);
        return 1;
    }
    else if (bits < 220) {                         // model with UV LUX
        float rain_mm    = rain_raw * 1.0f;
        int uv_index     = ((b[7] & 0x07) << 1) | ((b[8] & 0x80) >> 7);
        int light_lux    = ((b[8] & 0x7F) << 10) | (b[9] << 2) | ((b[10] & 0xC0) >> 6);
        int battery_low  = ((b[10] & 0x30) >> 4);
        int counter      = ((b[10] & 0x0f) << 8 | b[11]);
        /* clang-format off */
        data = data_make(
                "model",            "",                 DATA_STRING, "AOK-5056",
                "id",               "StationID",        DATA_FORMAT, "%04X",     DATA_INT,    device_id,
                "temperature_C",    "Temperature",      DATA_FORMAT, "%.01f C",  DATA_DOUBLE, temp_c,
                "humidity",         "Humidity",         DATA_FORMAT, "%u %%",    DATA_INT,    humidity,
                "rain_mm",          "Total rainfall",   DATA_FORMAT, "%.1f mm",  DATA_DOUBLE, rain_mm,
                "wind_avg_km_h",    "Wind avg speed",   DATA_FORMAT, "%u km/h",  DATA_INT,    speed_kmh,
                "wind_dir_deg",     "Wind Direction",   DATA_INT,                             direction_deg,
                "uv",               "UV Index",         DATA_FORMAT, "%u",       DATA_INT,    uv_index,
                "light_lux",        "Lux",              DATA_FORMAT, "%u",       DATA_INT,    light_lux,
                "counter",          "Counter",          DATA_FORMAT, "%u",       DATA_INT,    counter,
                "battery_ok",       "battery",          DATA_FORMAT, "%u",       DATA_INT,    !battery_low,
                NULL);
        /* clang-format on */

        decoder_output_data(decoder, data);
        return 1;
    }
    else {
        return 0;
    }
}

static char const *output_fields[] = {
        "model",
        "id",
        "temperature_C",
        "humidity",
        "battery_ok",
        "rain_mm",
        "wind_avg_km_h",
        "wind_dir_deg",
        "uv",
        "light_lux",
        "counter",
        "mic",
        NULL,
};

r_device const holman_ws5029pcm = {
        .name        = "AOK Weather Station rebrand Holman Industries iWeather WS5029, Conrad AOK-5056, Optex 99018",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 100,
        .long_width  = 100,
        .reset_limit = 19200,
        .decode_fn   = &holman_ws5029pcm_decode,
        .fields      = output_fields,
};

/**
Holman Industries WS5029 weather station using PWM.

- The checksum used is an xor of all 11 bytes.
- The bottom nybble results in 0. The top does not
- and I've been unable to figure out why. We only
- check the bottom nybble therefore.
- Have tried all permutations of init/poly for lfsr8 & crc8
- Rain is 0.79mm / count
  618 counts / 488.2mm - 190113 - Multiplier is exactly 0.79
- Wind is discrete kph
- Preamble is 0xaa 0xa5. Device is 0x98

*/
static int holman_ws5029pwm_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t const preamble[] = {0x55, 0x5a, 0x67}; // Preamble/Device inverted

    data_t *data;
    uint8_t *b;
    uint16_t temp_raw;
    int id, humidity, speed_kmh, wind_dir, battery_low;
    float temp_c, rain_mm;

    // Data is inverted, but all these checks can be performed
    // and validated prior to inverting the buffer. Invert
    // only if we have a valid row to process.
    int r = bitbuffer_find_repeated_row(bitbuffer, 3, 96);
    if (r < 0 || bitbuffer->bits_per_row[r] != 96)
        return DECODE_ABORT_LENGTH;

    b = bitbuffer->bb[r];

    // Test for preamble / device code
    if (memcmp(b, preamble, 3))
        return DECODE_FAIL_SANITY;

    // Test Checksum.
    if ((xor_bytes(b, 11) & 0xF) ^ 0xF)
        return DECODE_FAIL_MIC;

    // Invert data for processing
    bitbuffer_invert(bitbuffer);

    id          = b[3];                                                // changes on each power cycle
    battery_low = (b[4] & 0x80);                                       // High bit is low battery indicator
    temp_raw    = (int16_t)(((b[4] & 0x0f) << 12) | (b[5] << 4));      // uses sign-extend
    temp_c      = (temp_raw >> 4) * 0.1f;                              // Convert sign extended int to float
    humidity    = b[6];                                                // Simple 0-100 RH
    rain_mm     = ((b[7] << 4) + (b[8] >> 4)) * 0.79f;                  // Multiplier tested empirically over 618 pulses
    speed_kmh   = ((b[8] & 0xF) << 4) + (b[9] >> 4);                   // In discrete kph
    wind_dir    = b[9] & 0xF;                                          // 4 bit wind direction, clockwise from North

    /* clang-format off */
    data = data_make(
            "model",            "",                 DATA_STRING, "Holman-WS5029",
            "id",               "",                 DATA_INT,    id,
            "battery_ok",       "Battery",          DATA_INT,    !battery_low,
            "temperature_C",    "Temperature",      DATA_FORMAT, "%.01f C",  DATA_DOUBLE, temp_c,
            "humidity",         "Humidity",         DATA_FORMAT, "%u %%",    DATA_INT,    humidity,
            "rain_mm",          "Total rainfall",   DATA_FORMAT, "%.01f mm", DATA_DOUBLE, rain_mm,
            "wind_avg_km_h",    "Wind avg speed",   DATA_FORMAT, "%u km/h",  DATA_INT,    speed_kmh,
            "wind_dir_deg",     "Wind Direction",   DATA_INT,    (int)(wind_dir * 22.5),
            "mic",              "Integrity",        DATA_STRING, "CHECKSUM",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

r_device const holman_ws5029pwm = {
        .name        = "Holman Industries iWeather WS5029 weather station (older PWM)",
        .modulation  = FSK_PULSE_PWM,
        .short_width = 488,
        .long_width  = 976,
        .reset_limit = 6000,
        .gap_limit   = 2000,
        .decode_fn   = &holman_ws5029pwm_decode,
        .fields      = output_fields,
};
