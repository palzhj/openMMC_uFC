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

/**
 * @file stm32_pincfg.h
 * @brief Pin Config functions redirection for STM32
 *
 * @author Henrique Silva <henrique.silva@lnls.br>, LNLS
 */

#ifndef STM32_PINCFG_H_
#define STM32_PINCFG_H_

#include "port.h"

/**
 * @brief       Sets I/O Control pin mux
 * @param       port    : GPIO port to mux
 * @param       pin     : GPIO pin to mux
 * @param       cfg     : Configuration bits to select pin mode/function
 * @see
 */

/* 32 bit value in the format -> [port][pin][func][dir] each field [] is one byte */
#define PIN_DEF( port, pin, func, dir ) ( (port << 24) | (pin << 16) | (func << 8) | dir )

GPIO_TypeDef *PIN_PORT(uint32_t pin_def);
#define PIN_NUMBER( pin_def )    ((pin_def & 0x00FF0000) >> 16)
#define PIN_FUNC( pin_def )      ((pin_def & 0x0000FF00) >> 8)
#define PIN_DIR( pin_def )       ((pin_def & 0x000000FF) >> 0)

/* For other mcus like Atmel's it should be PORTA, PORTB, etc */
#define PORT0 0 //GPIOA
#define PORT1 1 //GPIOB
#define PORT2 2 //GPIOC

#endif
