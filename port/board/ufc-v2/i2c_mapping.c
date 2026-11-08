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
 * @file ufc-v2/i2c_mapping.c
 * @brief I2C bus/chip mapping for uFC v2.0
 *
 * Topology:
 *   I2C2 (PB10/PB11)
 *     +- TCA9548 @0x70 (A2=A1=A0=0)
 *          |- channel 0: 24AA025E48 @0x50 (A1=A0=0)   [FRU EEPROM]
 *          |- channel 2: 8V54816   @0x58 (A1=A0=0)
 *          |               SI5345A  @0x69 (A1=0,A0=1)
 *
 * @ingroup UFC_V2_I2C_MAPPING
 */

#include "i2c.h"
#include "i2c_mapping.h"
#include "port.h"

// Data Fields: i2c_interface, state, semaphore
i2c_mux_state_t i2c_mux[I2C_MUX_CNT] = {
  {I2C2_ID, -1, 0}
};

// Data Fields: i2c_interface, mux_bus, enabled
i2c_bus_mapping_t i2c_bus_map[I2C_BUS_CNT] = {
  [I2C_BUS_UNKNOWN_ID]  = {I2C2_ID, -1, 0},
  [I2C_BUS_FRU_ID]      = {I2C2_ID, 0, 1},
  [I2C_BUS_CLOCK_ID]    = {I2C2_ID, 2, 1},
  [I2C_BUS_MUX_ID]      = {I2C2_ID, -1, 1}
};

// Data Fields: bus_id, i2c_address
i2c_chip_mapping_t i2c_chip_map[I2C_CHIP_CNT] = {
  [CHIP_ID_MUX_0]         = {I2C_BUS_MUX_ID,    0x70},
  [CHIP_ID_EEPROM]        = {I2C_BUS_FRU_ID,    0x50},
  [CHIP_ID_8V54816]       = {I2C_BUS_CLOCK_ID,  0x58},
  [CHIP_ID_SI5345_0]      = {I2C_BUS_CLOCK_ID,  0x69},
  [CHIP_ID_PMBUS_0]       = {I2C_BUS_FRU_ID,    0x11}
};

bool i2c_set_mux_bus(uint8_t bus_id, i2c_mux_state_t *i2c_mux, int8_t new_state)
{
  portENABLE_INTERRUPTS();

  if (i2c_mux->i2c_interface == i2c_bus_map[i2c_chip_map[CHIP_ID_MUX_0].bus_id].i2c_interface) {
    uint8_t tca_channel = 1 << new_state;

    /* Select desired channel in the I2C switch.
     * The TCA9548A may NACK its first few transactions right after power-up /
     * peripheral init, so retry with a short busy wait (this can run before the
     * FreeRTOS scheduler starts, so vTaskDelay() must not be used). */
    {
        int retries = 0;
        while ( xI2CMasterWrite( i2c_bus_map[i2c_chip_map[CHIP_ID_MUX_0].bus_id].i2c_interface, i2c_chip_map[CHIP_ID_MUX_0].i2c_address, &tca_channel, 1 ) != 0 ) {
            if ( ++retries >= 50 ) {
                /* We failed to configure the I2C Mux, release the semaphore */
                xSemaphoreGive( i2c_mux->semaphore );
                return false;
            }
            for ( volatile uint32_t d = 0; d < 2000; d++ ) { }
        }
    }
  }

  i2c_mux->state = new_state;
  return true;
}

uint8_t i2c_get_mux_bus(uint8_t bus_id, i2c_mux_state_t *i2c_mux)
{
  if (i2c_mux->i2c_interface == i2c_bus_map[i2c_chip_map[CHIP_ID_MUX_0].bus_id].i2c_interface) {
      /* Include enable bit (fourth bit) on channel selection byte */
      uint8_t tca_channel;

      portENABLE_INTERRUPTS();
      /* Read bus state (other master on the bus may have switched it) */
      xI2CMasterRead( i2c_bus_map[i2c_chip_map[CHIP_ID_MUX_0].bus_id].i2c_interface, i2c_chip_map[CHIP_ID_MUX_0].i2c_address, &tca_channel, 1 );

      /* Convert bit position from tca register to actual channel number */
      uint8_t num;
      for (num = 0; num <= 8 ; num++)
      {
      	if (tca_channel & 1 << num)
      		break;
      }
      return num;

  } else {
      return i2c_mux->state;
  }
}
