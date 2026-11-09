/*
 *   openMMC  --
 *
 *   Copyright (C) 2015  Henrique Silva  <henrique.silva@lnls.br>
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
 * @file port.h
 * @author Henrique Silva <henrique.silva@lnls.br>, LNLS
 * @date September 2015
 *
 * @brief Port layer (includes all portable functions headers)
 */

#ifndef PORT_H_
#define PORT_H_

#include <stdio.h>
#include <string.h>

/* List of all STM32xx specific headers to be included */

#include "stm32f10x_conf.h"
#include "misc.h"
#include "stm32_gpio.h"
#include "stm32_i2c.h"
// #include "lpc17_ssp.h"
// #include "lpc17_spi.h"
#include "stm32_watchdog.h"
// #include "lpc17_interruptions.h"
#include "stm32_hpm.h"
// #include "lpc17_power.h"
#include "pin_mapping.h"
// #include "arm_cm3_reset.h"

#ifdef UART_RINGBUFFER
#include "lpc17_uartrb.h"
#else
#include "stm32_uart.h"
#endif

#endif
