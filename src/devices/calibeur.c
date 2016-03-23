#include "rtl_433.h"
#include "util.h"
#include "data.h"

//static int calibeur_rf104_callback(uint8_t bb[BITBUF_ROWS][BITBUF_COLS], int16_t bits_per_row[BITBUF_ROWS]) {
static int calibeur_rf104_callback(bitbuffer_t *bitbuffer) {
  data_t *data;
  char time_str[LOCAL_TIME_BUFLEN];

  uint8_t ID;
  float temperature;
  float humidity;
  bitrow_t *bb = bitbuffer->bb;

  // Validate package (row [0] is empty due to sync bit)
  if ((bitbuffer->bits_per_row[1] == 21)      // Dont waste time on a long/short package
   && (crc8(bb[1], 3, 0x80, 0) != 0)    // It should be odd parity
   && (memcmp(bb[1], bb[2], 3) == 0)  // We want at least two messages in a row
  )
  {
    uint8_t bits;

    bits  = ((bb[1][0] & 0x80) >> 7); // [0]
    bits |= ((bb[1][0] & 0x40) >> 5); // [1]
    bits |= ((bb[1][0] & 0x20) >> 3); // [2]
    bits |= ((bb[1][0] & 0x10) >> 1); // [3]
    bits |= ((bb[1][0] & 0x08) << 1); // [4]
    bits |= ((bb[1][0] & 0x04) << 3); // [5]
    ID = bits / 10;
    temperature = (float)(bits % 10) / 10.0;

    bits  = ((bb[1][0] & 0x02) << 3); // [4]
    bits |= ((bb[1][0] & 0x01) << 5); // [5]
    bits |= ((bb[1][1] & 0x80) >> 7); // [0]
    bits |= ((bb[1][1] & 0x40) >> 5); // [1]
    bits |= ((bb[1][1] & 0x20) >> 3); // [2]
    bits |= ((bb[1][1] & 0x10) >> 1); // [3]
    bits |= ((bb[1][1] & 0x08) << 3); // [6]
    temperature += (float)bits - 41.0;

    bits  = ((bb[1][1] & 0x02) << 4); // [5]
    bits |= ((bb[1][1] & 0x01) << 6); // [6]
    bits |= ((bb[1][2] & 0x80) >> 7); // [0]
    bits |= ((bb[1][2] & 0x40) >> 5); // [1]
    bits |= ((bb[1][2] & 0x20) >> 3); // [2]
    bits |= ((bb[1][2] & 0x10) >> 1); // [3]
    bits |= ((bb[1][2] & 0x08) << 1); // [4]
    humidity = bits;

    local_time_str(0, time_str);

    data = data_make("time",          "",            DATA_STRING, time_str,
                     "model",         "",            DATA_STRING, "Calibeur RF-104",
                     "id",            "",            DATA_INT, ID, // this should be named "id"
                     "temperature_C", "Temperature", DATA_FORMAT, "%.1f C", DATA_DOUBLE, temperature,
                     "humidity",      "Humidity",    DATA_FORMAT, "%2.0f %%", DATA_DOUBLE, humidity,
                      NULL);
    data_acquired_handler(data);

    return 1;
  }
  return 0;
}

static char *output_fields[] = {
    "time",
    "model",
    "id",
    "temperature_C",
    "humidity",
    NULL
};

r_device calibeur_RF104 = {
  .name           = "Calibeur RF-104 Sensor",
  .modulation     = OOK_PULSE_PWM_TERNARY,
  .short_limit    = 1160, // Short pulse 760  s, Startbit 1560  s, Long pulse 2240  s
  .long_limit     = 1900, // Maximum pulse period (long pulse + fixed gap)
  .reset_limit    = 3200, // Longest gap (2960-760  s)
  .json_callback  = &calibeur_rf104_callback,
  .disabled       = 0,
  .demod_arg      = 1,   // Startbit is middle bit
  .fields         = output_fields
};
