/*
 *   openMMC -- Open Source modular IPM Controller firmware
 *
 *   Copyright (C) 2015  Henrique Silva <henrique.silva@lnls.br>
 *   Copyright (C) 2025-2026  Jie Zhang <zhj@ihep.ac.cn>
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

/* Project Includes */
#include "port.h"
#include "tpl0102.h"
#include "i2c.h"
#include "i2c_mapping.h"
#include "task.h"

// Address for Initial Value Register or Wiper Resistance Register for Potentiometer A
#define REG_IVRA_WRA 0x00
// Address for Initial Value Register or Wiper Resistance Register for Potentiometer B
#define REG_IVRB_WRB 0x01
// Address for Access Control Register
#define REG_ACR 0x10

#define ACR_VOL 0x80    // 1: Only Volatile Registers (WR) are accessible.
#define ACR_ENABLE 0x40 // 0: Shutdown
#define ACR_WIP 0x20    // read only, 1: Non-volatile write operation is in progress

void tpl0102_init(void)
{
  uint8_t i2c_addr, i2c_interface;
  uint8_t tx_data[2] = {REG_ACR, ACR_VOL | ACR_ENABLE}; // Enable Volatile Registers
  uint8_t i2c_written = 0;

  while (i2c_written!=2)
  {
    if (i2c_take_by_chipid(CHIP_ID_TPL0102_0, &i2c_addr, &i2c_interface, portMAX_DELAY) == pdTRUE)
    {
      i2c_written = xI2CMasterWrite(i2c_interface, i2c_addr, tx_data, 2);
      i2c_give(i2c_interface);
      if (i2c_written==0) vTaskDelay(pdMS_TO_TICKS(5)); /* Avoid too much unnecessary I2C trafic*/
    }
  }

}

void tpl0102_set_val(uint8_t chn, uint8_t val)
{
  uint8_t i2c_addr, i2c_interface;
  uint8_t tx_data[2];
  uint8_t i2c_written;

  // Enable Volatile Registers
  tx_data[0] = REG_ACR;
  tx_data[1] = ACR_VOL | ACR_ENABLE;
  i2c_written = 0;
  while (i2c_written!=2)
  {
    if (i2c_take_by_chipid(CHIP_ID_TPL0102_0, &i2c_addr, &i2c_interface, portMAX_DELAY) == pdTRUE)
    {
      i2c_written = xI2CMasterWrite(i2c_interface, i2c_addr, tx_data, 2);
      i2c_give(i2c_interface);
      if (i2c_written==0) vTaskDelay(pdMS_TO_TICKS(5)); /* Avoid too much unnecessary I2C trafic*/
    }
  }

  if (chn == 0)
    tx_data[0] = REG_IVRA_WRA;
  else
    tx_data[0] = REG_IVRB_WRB;
  tx_data[1] = val;
  i2c_written = 0;
  while (i2c_written != 2)
  {
    if (i2c_take_by_chipid(CHIP_ID_TPL0102_0, &i2c_addr, &i2c_interface, portMAX_DELAY) == pdTRUE)
    {
      i2c_written = xI2CMasterWrite(i2c_interface, i2c_addr, tx_data, 2);
      i2c_give(i2c_interface);
    }
    if (i2c_written==0) vTaskDelay(pdMS_TO_TICKS(5)); /* Avoid too much unnecessary I2C trafic*/
  }
}

void tpl0102_set_non_volatile_val(uint8_t chn, uint8_t val)
{
  uint8_t i2c_addr, i2c_interface;
  uint8_t tx_data[2];
  uint8_t i2c_written;

  // Disable Volatile Registers
  tx_data[0] = REG_ACR;
  tx_data[1] = ACR_ENABLE;
  i2c_written = 0;
  while (i2c_written!=2)
  {
    if (i2c_take_by_chipid(CHIP_ID_TPL0102_0, &i2c_addr, &i2c_interface, portMAX_DELAY) == pdTRUE)
    {
      i2c_written = xI2CMasterWrite(i2c_interface, i2c_addr, tx_data, 2);
      i2c_give(i2c_interface);
      if (i2c_written==0) vTaskDelay(pdMS_TO_TICKS(5)); /* Avoid too much unnecessary I2C trafic*/
    }
  }

  if (chn == 0)
    tx_data[0] = REG_IVRA_WRA;
  else
    tx_data[0] = REG_IVRB_WRB;
  tx_data[1] = val;
  i2c_written = 0;
  while (i2c_written != 2)
  {
    if (i2c_take_by_chipid(CHIP_ID_TPL0102_0, &i2c_addr, &i2c_interface, portMAX_DELAY) == pdTRUE)
    {
      i2c_written = xI2CMasterWrite(i2c_interface, i2c_addr, tx_data, 2);
      i2c_give(i2c_interface);
    }
    if (i2c_written==0) vTaskDelay(pdMS_TO_TICKS(5)); /* Avoid too much unnecessary I2C trafic*/
  }

  // Wait for Non-volatile last write operation
  tx_data[0] = REG_ACR;
  tx_data[1] = ACR_WIP;
  i2c_written = 0;
	while (tx_data[1]&ACR_WIP)
	{
    vTaskDelay(pdMS_TO_TICKS(30)); // Non-volatile write cycle time: 20 ms
    while (i2c_written!=1)
    {
      if (i2c_take_by_chipid(CHIP_ID_TPL0102_0, &i2c_addr, &i2c_interface, portMAX_DELAY) == pdTRUE)
      {
        i2c_written = xI2CMasterWriteRead(i2c_interface, i2c_addr, &tx_data[0], 1, &tx_data[1], 1);
        i2c_give(i2c_interface);
        if (i2c_written==0) vTaskDelay(pdMS_TO_TICKS(5)); /* Avoid too much unnecessary I2C trafic*/
      }
    }
	}

  tx_data[0] = REG_ACR;
  tx_data[1] = ACR_VOL | ACR_ENABLE; // Enable Volatile Registers
  while (i2c_written != 2)
  {
    if (i2c_take_by_chipid(CHIP_ID_TPL0102_0, &i2c_addr, &i2c_interface, portMAX_DELAY) == pdTRUE)
    {
      i2c_written = xI2CMasterWrite(i2c_interface, i2c_addr, tx_data, 2);
      i2c_give(i2c_interface);
    }
    if (i2c_written==0) vTaskDelay(pdMS_TO_TICKS(5)); /* Avoid too much unnecessary I2C trafic*/
  }
}

uint8_t tpl0102_get_val(uint8_t chn)
{
  uint8_t i2c_addr, i2c_interface;
  uint8_t tx_data;
  uint8_t rx_data;
  uint8_t i2c_read = 0;
  if (chn == 0)
    tx_data = REG_IVRA_WRA;
  else
    tx_data = REG_IVRB_WRB;
  while (i2c_read != 1)
  {
    if (i2c_take_by_chipid(CHIP_ID_TPL0102_0, &i2c_addr, &i2c_interface, portMAX_DELAY) == pdTRUE)
    {
      i2c_read = xI2CMasterWriteRead(i2c_interface, i2c_addr, &tx_data, 1, &rx_data, 1);
      i2c_give(i2c_interface);
    }
    if (i2c_read==0) vTaskDelay(pdMS_TO_TICKS(5)); /* Avoid too much unnecessary I2C trafic*/
  }
  return rx_data;
}

