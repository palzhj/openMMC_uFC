/*
 *   openMMC  --
 *
 *   Copyright (C) 2015  Henrique Silva  <henrique.silva@lnls.br>
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
 */

/*!
 * @file stm32_i2c.h
 * @author Henrique Silva <henrique.silva@lnls.br>, LNLS
 * @author Jie Zhang <zhj@ihep.ac.cn>, IHEP
 * @date Feb.18 2025
 *
 * @brief I2C driver for STM32F10x
 */

/**
 * @brief	I2C interface IDs
 * @note
 * All Chip functions will take this as the first parameter,
 * I2C_NUM_INTERFACE must never be used for calling any Chip
 * functions, it is only used to find the number of interfaces
 * available in the Chip.
 */
typedef enum I2C_ID
{
  I2C1_ID,          /**< ID I2C1 */
  I2C2_ID,          /**< ID I2C2 */
  I2C_NUM_INTERFACE /**< Number of I2C interfaces in the chip */
} I2C_ID_T;

/*! @brief Max message length (in bits) used in I2C */
#define i2cMAX_MSG_LENGTH 32
#define I2C_TIMEOUT 1000

uint8_t xI2CMasterWrite(I2C_ID_T id, uint8_t slave_addr, uint8_t *tx_buff, uint8_t buff_len);
uint8_t xI2CMasterRead(I2C_ID_T id, uint8_t slave_addr, uint8_t *rx_buff, uint8_t buff_len);
uint8_t xI2CMasterWriteRead(I2C_ID_T id, uint8_t addr, const uint8_t *tx_buff, uint8_t tx_len, uint8_t *rx_buff, uint8_t rx_len);
uint8_t xI2CSlaveReceive(I2C_ID_T id, uint8_t *rx_buff, uint8_t buff_len, uint32_t timeout);
void vI2CSlaveSetup(I2C_ID_T id, uint8_t slave_addr);
void vI2CConfig(I2C_ID_T id, uint32_t speed);
