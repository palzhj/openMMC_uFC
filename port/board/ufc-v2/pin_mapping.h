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
 * @defgroup uFC_v2 Board Port
 * @ingroup BOARD_PORTS
 */

/**
 * @file ufc-v2/pin_mapping.h
 * @brief Hardware pin definitions for uFC v2.0 (STM32F100CBT6)
 *
 * @ingroup UFC_V2_PIN_MAPPING
 */

/**
 * @defgroup UFC_V2_PIN_MAPPING uFC v2.0 Pin Mapping
 * @ingroup UFC_V2
 * @{
 */

#ifndef PIN_MAPPING_H_
#define PIN_MAPPING_H_

/* Project Includes */
#include "port.h"

void gpio_init(void);

/*
 * uFC v2.0 uses the STM32F100CBT6 (LQFP48, 8 KB SRAM).
 *
 * NOTE: PB3 (JTDO), PB4 (NJTRST) and PA15 (JTDI) are used as GPIO. pin_init()
 * disables JTAG while keeping SWD (GPIO_Remap_SWJ_JTAGDisable), which frees
 * these pins. Do not enable full JTAG on this board.
 */

/* AMC / desktop mode strap (was AMC_MODE on u4FCP) */
#define GPIO_DESKTOP_EN PIN_DEF(PORT2, 13, 0, GPIO_DIR_INPUT)

/* Geographic Address pin definitions */
#define GPIO_GA2 PIN_DEF(PORT0, 11, 0, GPIO_DIR_INPUT)
#define GPIO_GA1 PIN_DEF(PORT0, 12, 0, GPIO_DIR_INPUT)
#define GPIO_GA0 PIN_DEF(PORT0, 15, 0, GPIO_DIR_INPUT)
#define GPIO_GA_TEST PIN_DEF(PORT1, 3, 0, GPIO_DIR_OUTPUT)

/* Board LEDs */
#define GPIO_LEDBLUE PIN_DEF(PORT1, 4, 1, GPIO_DIR_OUTPUT)
#define GPIO_LEDGREEN PIN_DEF(PORT1, 0, 1, GPIO_DIR_OUTPUT)
#define GPIO_LEDRED PIN_DEF(PORT1, 5, 1, GPIO_DIR_OUTPUT)

/* AMC module handle / hot-swap handle */
#define GPIO_AMC_MODULE_HANDLE PIN_DEF(PORT0, 0, 0, GPIO_DIR_INPUT)
#define GPIO_HOT_SWAP_HANDLE GPIO_AMC_MODULE_HANDLE

/* PMBUS */
#define GPIO_PMBUS_ALERT_B PIN_DEF(PORT1, 8, 0, GPIO_DIR_INPUT)
/* UCD90320 CONTROL pin (sheet 4: U60 pin 46 = PB9 = PMBUS_CTRL) */
#define GPIO_PMBUS_CTRL PIN_DEF(PORT1, 9, 1, GPIO_DIR_OUTPUT)

/* FPGA control */
#define GPIO_FPGA_DONE PIN_DEF(PORT2, 15, 0, GPIO_DIR_INPUT)
#define GPIO_FPGA_PROG_B PIN_DEF(PORT2, 14, 1, GPIO_DIR_OUTPUT)
#define GPIO_FPGA_RESET_B PIN_DEF(PORT0, 1, 1, GPIO_DIR_OUTPUT)

/* Pin initialization (config) list */
#define PIN_CFG_LIST        \
      GPIO_DESKTOP_EN,    \
      GPIO_GA0,           \
      GPIO_GA1,           \
      GPIO_GA2,           \
      GPIO_GA_TEST,       \
      GPIO_LEDBLUE,       \
      GPIO_LEDGREEN,      \
      GPIO_LEDRED,        \
      GPIO_AMC_MODULE_HANDLE, \
      GPIO_PMBUS_ALERT_B, \
      GPIO_PMBUS_CTRL,    \
      GPIO_FPGA_DONE,     \
      GPIO_FPGA_PROG_B,   \
      GPIO_FPGA_RESET_B

#endif /* PIN_MAPPING_H_ */

/**
 * @}
 */
