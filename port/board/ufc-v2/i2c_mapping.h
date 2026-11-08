/*
 *   openMMC -- Open Source modular IPM Controller firmware
 *
 *   Copyright (C) 2026  IHEP
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *   @license GPL-3.0+ <http://spdx.org/licenses/GPL-3.0+>
 */

/**
 * @file ufc-v2/i2c_mapping.h
 * @brief I2C bus/chip mapping for uFC v2.0
 *
 * @ingroup UFC_V2_I2C_MAPPING
 */

#ifndef I2C_MAPPING_H_
#define I2C_MAPPING_H_

#include "i2c.h"

#define I2CMODE_POOLING         1
#define I2CMODE_INTERRUPT       0
#define SPEED_100KHZ            100000

enum {
    I2C_BUS_UNKNOWN_ID = 0,
    I2C_BUS_FRU_ID,     /* TCA9548 channel 0 */
    I2C_BUS_CLOCK_ID,   /* TCA9548 channel 2 */
    I2C_BUS_MUX_ID,
    I2C_BUS_CNT
};

enum {
    CHIP_ID_MUX_0 = 0,
    CHIP_ID_EEPROM,
    CHIP_ID_8V54816,
    CHIP_ID_SI5345_0,
    CHIP_ID_PMBUS_0,
    I2C_CHIP_CNT
};

#define I2C_MUX_CNT    1

extern i2c_mux_state_t i2c_mux[I2C_MUX_CNT];
extern i2c_bus_mapping_t i2c_bus_map[I2C_BUS_CNT];
extern i2c_chip_mapping_t i2c_chip_map[I2C_CHIP_CNT];

#endif
