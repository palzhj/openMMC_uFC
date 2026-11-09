/*
 *   openMMC -- Open Source modular IPM Controller firmware
 *
 *   Copyright (C) 2015  Henrique Silva <henrique.silva@lnls.br>
 *   Copyright (C) 2025  Jie Zhang <zhj@ihep.ac.cn>
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
 * @file   eeprom_24xx512.c
 * @author Henrique Silva <henrique.silva@lnls.br>
 * @author Jie Zhang <zhj@ihep.ac.cn>, IHEP
 * @date Feb. 2025
 *
 * @brief  24xx512 EEPROM module interface implementation
 *
 * @ingroup 24xx512
 */

/* FreeRTOS includes */
#include "FreeRTOS.h"
#include "task.h"
#include "string.h"

/* Project Includes */
#include "eeprom_24xx512.h"
#include "port.h"
#include "i2c.h"

#define EEPROM_PAGE_SIZE 128

size_t eeprom_24xx512_read(uint8_t id, uint16_t address, uint8_t *rx_data, size_t buf_len, TickType_t timeout)
{
  uint8_t i2c_addr;
  uint8_t i2c_interface;
  size_t bytes_to_read;
  uint8_t i2c_read = 0;
  size_t rx_len = 0;
  uint8_t addr8[2];
  uint8_t retries = 0;

  if (rx_data != NULL)
  {
    while (rx_len < buf_len)
    {
      /* The I2C transfer is limited to one byte of length; read the rest in
       * chunks and move the EEPROM address forward by the bytes already read. */
      bytes_to_read = buf_len - rx_len;
      if (bytes_to_read > 255) {
        bytes_to_read = 255;
      }
      addr8[0] = ((address + rx_len) >> 8) & 0xFF;
      addr8[1] = (address + rx_len) & 0xFF;
      if (i2c_take_by_chipid(id, &i2c_addr, &i2c_interface, timeout))
      {
        i2c_read = xI2CMasterWriteRead(i2c_interface, i2c_addr, addr8, 2,
                                       rx_data + rx_len, bytes_to_read);
        /* When trying to read, the EEPROM will reply with NACKs if it's busy.
          * If the i2c receives a NACK when trying to read, the function returns only
          * the number of bytes successfully read, and we should increment only this value
          * in tx_len and in curr_addr */
        rx_len += i2c_read;
        i2c_give(i2c_interface);
      }
      if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) {
        vTaskDelay(pdMS_TO_TICKS(5)); /* Avoid too much unnecessary I2C trafic*/
      } else if (i2c_read == 0) {
        /* No scheduler yet (FRU read at boot): wait with a busy loop for the
         * EEPROM to finish its power-up/write cycle, but do not spin forever. */
        if (++retries > 20) {
          break;
        }
        for (volatile uint32_t spin = 0; spin < 50000; spin++) { }
      }
    }
  }

  return rx_len;
}

size_t eeprom_24xx512_write(uint8_t id, uint16_t address, uint8_t *tx_data, size_t buf_len, TickType_t timeout)
{
  uint8_t i2c_addr;
  uint8_t i2c_interface;
  uint8_t bytes_to_write;
  uint8_t page_buf[EEPROM_PAGE_SIZE+2];
  uint16_t curr_addr;
  uint8_t i2c_written = 0;
  size_t tx_len = 0;

  if (tx_data != NULL)
  {
    curr_addr = address;

    while (tx_len < buf_len)
    {
      bytes_to_write = EEPROM_PAGE_SIZE - (curr_addr % EEPROM_PAGE_SIZE);

      if (bytes_to_write > (buf_len - tx_len))
      {
        bytes_to_write = (buf_len - tx_len);
      }
      page_buf[0] = (curr_addr >> 8) & 0xFF;
      page_buf[1] = (curr_addr) & 0xFF;

      memcpy(&page_buf[2], tx_data + tx_len, bytes_to_write);

      if (i2c_take_by_chipid(id, &i2c_addr, &i2c_interface, timeout))
      {
        /* Write the data */
        i2c_written = xI2CMasterWrite(i2c_interface, i2c_addr, &page_buf[0], bytes_to_write + 2);
        /* When trying to write, the EEPROM will reply with NACKs if it's busy.
         * If the i2c receives a NACK when trying to write, the function returns only
         * the number of bytes successfully written, and we should increment only this value
         * in tx_len and in curr_addr */

        if (i2c_written)
        {
          tx_len += i2c_written - 2; /* Remove byte address from data written size */
          curr_addr += i2c_written - 2;
        }
        i2c_give(i2c_interface);
      }
      if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) {
        vTaskDelay(pdMS_TO_TICKS(5)); /* Avoid too much unnecessary I2C trafic*/
      } else if (i2c_written == 0) {
        break; /* no scheduler: do not spin forever on a silent bus */
      }
    }
  }

  return tx_len;
}
