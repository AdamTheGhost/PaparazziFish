/*
 * Copyright (C) Gautier Hattenberger <gautier.hattenberger@enac.fr>
 *
 * This file is part of paparazzi
 *
 * paparazzi is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * paparazzi is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with paparazzi; see the file COPYING.  If not, see
 * <http://www.gnu.org/licenses/>.
 */
/**
 * @file "modules/range_finder/teraranger_one.c"
 * @author Gautier Hattenberger <gautier.hattenberger@enac.fr>
 * Driver for the TeraRanger One range finder (I2C)
 */

#include "modules/range_finder/teraranger_one.h"
#include "mcu_periph/i2c.h"
#include "subsystems/abi.h"
#include "subsystems/datalink/downlink.h"

// check if I2C device is selected
#ifndef TERARANGER_ONE_I2C_DEV
#error TERARANGER_ONE_I2C_DEV needs to be defined
#endif
PRINT_CONFIG_VAR(TERARANGER_ONE_I2C_DEV)

// default base address on 8 bits
#ifndef TERARANGER_ONE_I2C_ADDR
#define TERARANGER_ONE_I2C_ADDR 0x60
#endif

// add an offset to the measurments (in meters)
#ifndef TERARANGER_ONE_OFFSET
#define TERARANGER_ONE_OFFSET 0.f
#endif

// send AGL data over ABI
#ifndef USE_TERARANGER_ONE_AGL
#define USE_TERARANGER_ONE_AGL TRUE
#endif

// CRC8 table used to check data validity
static const uint8_t crc_table[] = {
  0x00, 0x07, 0x0e, 0x09, 0x1c, 0x1b, 0x12, 0x15, 0x38, 0x3f, 0x36, 0x31, 0x24, 0x23,
  0x2a, 0x2d, 0x70, 0x77, 0x7e, 0x79, 0x6c, 0x6b, 0x62, 0x65, 0x48, 0x4f, 0x46, 0x41,
  0x54, 0x53, 0x5a, 0x5d, 0xe0, 0xe7, 0xee, 0xe9, 0xfc, 0xfb, 0xf2, 0xf5, 0xd8, 0xdf,
  0xd6, 0xd1, 0xc4, 0xc3, 0xca, 0xcd, 0x90, 0x97, 0x9e, 0x99, 0x8c, 0x8b, 0x82, 0x85,
  0xa8, 0xaf, 0xa6, 0xa1, 0xb4, 0xb3, 0xba, 0xbd, 0xc7, 0xc0, 0xc9, 0xce, 0xdb, 0xdc,
  0xd5, 0xd2, 0xff, 0xf8, 0xf1, 0xf6, 0xe3, 0xe4, 0xed, 0xea, 0xb7, 0xb0, 0xb9, 0xbe,
  0xab, 0xac, 0xa5, 0xa2, 0x8f, 0x88, 0x81, 0x86, 0x93, 0x94, 0x9d, 0x9a, 0x27, 0x20,
  0x29, 0x2e, 0x3b, 0x3c, 0x35, 0x32, 0x1f, 0x18, 0x11, 0x16, 0x03, 0x04, 0x0d, 0x0a,
  0x57, 0x50, 0x59, 0x5e, 0x4b, 0x4c, 0x45, 0x42, 0x6f, 0x68, 0x61, 0x66, 0x73, 0x74,
  0x7d, 0x7a, 0x89, 0x8e, 0x87, 0x80, 0x95, 0x92, 0x9b, 0x9c, 0xb1, 0xb6, 0xbf, 0xb8,
  0xad, 0xaa, 0xa3, 0xa4, 0xf9, 0xfe, 0xf7, 0xf0, 0xe5, 0xe2, 0xeb, 0xec, 0xc1, 0xc6,
  0xcf, 0xc8, 0xdd, 0xda, 0xd3, 0xd4, 0x69, 0x6e, 0x67, 0x60, 0x75, 0x72, 0x7b, 0x7c,
  0x51, 0x56, 0x5f, 0x58, 0x4d, 0x4a, 0x43, 0x44, 0x19, 0x1e, 0x17, 0x10, 0x05, 0x02,
  0x0b, 0x0c, 0x21, 0x26, 0x2f, 0x28, 0x3d, 0x3a, 0x33, 0x34, 0x4e, 0x49, 0x40, 0x47,
  0x52, 0x55, 0x5c, 0x5b, 0x76, 0x71, 0x78, 0x7f, 0x6a, 0x6d, 0x64, 0x63, 0x3e, 0x39,
  0x30, 0x37, 0x22, 0x25, 0x2c, 0x2b, 0x06, 0x01, 0x08, 0x0f, 0x1a, 0x1d, 0x14, 0x13,
  0xae, 0xa9, 0xa0, 0xa7, 0xb2, 0xb5, 0xbc, 0xbb, 0x96, 0x91, 0x98, 0x9f, 0x8a, 0x8d,
  0x84, 0x83, 0xde, 0xd9, 0xd0, 0xd7, 0xc2, 0xc5, 0xcc, 0xcb, 0xe6, 0xe1, 0xe8, 0xef,
  0xfa, 0xfd, 0xf4, 0xf3
};

// CRC check function
static uint8_t teraranger_crc8(uint8_t *p, uint8_t len)
{
  uint16_t i;
  uint16_t crc = 0x0;

  while (len--)
  {
    i = (crc ^ *p++) & 0xFF;
    crc = (crc_table[i] ^ (crc << 8)) & 0xFF;
  }
  return crc & 0xFF;
}

// data stucture
struct TeraRanger teraranger;

// private data
struct i2c_transaction teraranger_i2c_trans;

void teraranger_init(void)
{
  teraranger.raw = 0;
  teraranger.dist = 0.f;
  teraranger.offset = TERARANGER_ONE_OFFSET;
  teraranger.data_available = false;

  teraranger_i2c_trans.status = I2CTransDone;
}

// data are available in continous reading
void teraranger_periodic(void)
{
  // next reading
  if (teraranger_i2c_trans.status == I2CTransDone) {
    i2c_receive(&TERARANGER_ONE_I2C_DEV, &teraranger_i2c_trans, TERARANGER_ONE_I2C_ADDR, 3);
  }
}

void teraranger_event(void)
{
  if (teraranger_i2c_trans.status == I2CTransSuccess) {
    // check if CRC is valid
    if (teraranger_crc8((uint8_t*)teraranger_i2c_trans.buf, 2) == teraranger_i2c_trans.buf[2]) {
      // raw value
      teraranger.raw =
        ((uint16_t)(teraranger_i2c_trans.buf[0]) << 8) |
        (uint16_t)(teraranger_i2c_trans.buf[1]);
      // check if data is within ranges
      if (teraranger.raw != 0) {
        teraranger.dist = (float)teraranger.raw / 1000.f + teraranger.offset;
        teraranger.data_available = true;
#if USE_TERARANGER_ONE_AGL
        uint32_t now_ts = get_sys_time_usec();
        AbiSendMsgAGL(AGL_TERARANGER_ONE_ID, now_ts, teraranger.dist);
#endif
      } else {
        // data are not valid any more
        teraranger.data_available = false;
      }
    }
    teraranger_i2c_trans.status = I2CTransDone;
  } else if (teraranger_i2c_trans.status == I2CTransFailed) {
    // retry if failed
    teraranger_i2c_trans.status = I2CTransDone;
  }
}

void teraranger_downlink(void)
{
  DOWNLINK_SEND_SONAR(DefaultChannel, DefaultDevice, &teraranger.raw, &teraranger.dist);
}

