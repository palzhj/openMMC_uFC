/*
 *   openMMC  --
 *
 *   Copyright (C) 2015  Henrique Silva  <henrique.silva@lnls.br>
 *   Copyright (C) 2025  Jie Zhang  <zhj@ihep.ac.cn>
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

/**
 * @file stm32_uart.h
 * @author Henrique Silva <henrique.silva@lnls.br>, LNLS
 * @author Jie Zhang  <zhj@ihep.ac.cn>, IHEP
 *
 * @brief STM32xx UART interface definitions
 */

#ifndef STM32_UART_H_
#define STM32_UART_H_

#include "port.h"

typedef enum UART_ID
{
  UART1_ID,            /**< ID USART1 */
  UART2_ID,            /**< ID USART2 */
  UART3_ID,            /**< ID USART3 */
  UART_NUM_INTERFACE /**< Number of I2C interfaces in the chip */
} UART_ID_T;

#define UART_DEBUG UART1_ID // 0=USART1

typedef struct stm32_uart_cfg
{
  USART_TypeDef *ptr;
  IRQn_Type irq;
} stm32_uart_cfg_t;

extern const stm32_uart_cfg_t usart_cfg[3];

#define uart_tx_enable(id) USART_Cmd(usart_cfg[id].ptr, ENABLE)
#define uart_tx_disable(id) USART_Cmd(usart_cfg[id].ptr, DISABLE)
#define uart_int_enable(id, mask) USART_ITConfig(usart_cfg[id].ptr, mask, ENABLE)
#define uart_int_disable(id, mask) USART_ITConfig(usart_cfg[id].ptr, mask, DISABLE)
#define uart_send_char(id, ch) USART_SendData(usart_cfg[id].ptr, (uint16_t)ch)
#define uart_read_char(id) USART_ReceiveData(usart_cfg[id].ptr)

void uart_init(uint8_t id);
void uart_send(uint8_t id, char *msg, uint32_t len);
void uart_read(uint8_t id, char *buf, uint32_t len);

#endif
