/*
 *   openMMC -- Open Source modular IPM Controller firmware
 *
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
 *
 *   @license GPL-3.0+ <http://spdx.org/licenses/GPL-3.0+>
 */

/**
 * @defgroup u4FCP_v2.0 Board Port
 * @ingroup BOARD_PORTS
 */

/**
 * @file u4fcp-v2/pin_mapping.h
 * @brief Hardware pin definitions for u4FCPv2.0
 *
 * @ingroup u4FCP_V2_0_PIN_MAPPING
 */

/**
 * @defgroup u4FCP_V2_0_PIN_MAPPING AFCv4.0 Pin Mapping
 * @ingroup u4FCP_V2_0
 * @{
 */

#ifndef PIN_MAPPING_H_
#define PIN_MAPPING_H_

/* Project Includes */
#include "port.h"

void gpio_init(void);

/* Pin definitions */
// AMC
#define GPIO_MMC_ENABLE PIN_DEF(PORT1, 2, 0, GPIO_DIR_INPUT)  // BOOT1
#define AMC_MODE_ENABLE PIN_DEF(PORT2, 13, 0, GPIO_DIR_INPUT)

/* Geographic Address pin definitions */
#define GPIO_GA2 PIN_DEF(PORT0, 11, 0, GPIO_DIR_INPUT)
#define GPIO_GA1 PIN_DEF(PORT0, 12, 0, GPIO_DIR_INPUT)
#define GPIO_GA0 PIN_DEF(PORT0, 15, 0, GPIO_DIR_INPUT)
#define GPIO_GA_TEST PIN_DEF(PORT1, 3, 0, GPIO_DIR_OUTPUT)

/* Board LEDs */
#define GPIO_LEDBLUE PIN_DEF(PORT1, 4, 1, GPIO_DIR_OUTPUT)
#define GPIO_LEDGREEN PIN_DEF(PORT1, 0, 1, GPIO_DIR_OUTPUT)
#define GPIO_LEDRED PIN_DEF(PORT1, 5, 1, GPIO_DIR_OUTPUT)

/* Hot swap handle */
#define GPIO_HOT_SWAP_HANDLE PIN_DEF(PORT0, 0, 0, GPIO_DIR_INPUT)

// PMBUS
#define GPIO_PMBUS_ALERT_B PIN_DEF(PORT1, 8, 0, GPIO_DIR_INPUT)
#define GPIO_PMBUS_CTRL PIN_DEF(PORT1, 9, 1, GPIO_DIR_OUTPUT)

/* FPGA Control */
#define GPIO_FPGA_DONE   PIN_DEF(PORT2, 15, 0, GPIO_DIR_INPUT)
#define GPIO_FPGA_PROG_B PIN_DEF(PORT2, 14, 1, GPIO_DIR_OUTPUT)
#define GPIO_FPGA_RESET_B PIN_DEF(PORT0, 1, 1, GPIO_DIR_OUTPUT)

// CLK
#define GPIO_CLK0_INT_B PIN_DEF(PORT0, 4, 0, GPIO_DIR_INPUT)
#define GPIO_CLK1_INT_B PIN_DEF(PORT0, 5, 0, GPIO_DIR_INPUT)
#define GPIO_CLK0_LOL_B PIN_DEF(PORT0, 6, 0, GPIO_DIR_INPUT)
#define GPIO_CLK1_LOL_B PIN_DEF(PORT0, 7, 0, GPIO_DIR_INPUT)

// FMC
#define GPIO_FMC0_PRSNT_B PIN_DEF(PORT1, 12, 0, GPIO_DIR_INPUT)
#define GPIO_FMC1_PRSNT_B PIN_DEF(PORT1, 13, 0, GPIO_DIR_INPUT)
#define GPIO_FMC0_CLK_DIR PIN_DEF(PORT1, 14, 0, GPIO_DIR_INPUT)
#define GPIO_FMC1_CLK_DIR PIN_DEF(PORT1, 15, 0, GPIO_DIR_INPUT)

// RTM
#define GPIO_RTM_PS_B PIN_DEF(PORT0, 8, 0, GPIO_DIR_INPUT)
#define GPIO_RTM_RES PIN_DEF(PORT0, 2, 0, GPIO_DIR_OUTPUT)
#define GPIO_RTM_INT_B PIN_DEF(PORT0, 3, 0, GPIO_DIR_INPUT)

// Test Point
#define GPIO_TP PIN_DEF(PORT1, 1, 0, GPIO_DIR_INPUT)

/* Pin initialization (config) list */
#define PIN_CFG_LIST        \
      AMC_MODE_ENABLE,      \
      GPIO_GA0,             \
      GPIO_GA1,             \
      GPIO_GA2,             \
      GPIO_GA_TEST,         \
      GPIO_LEDBLUE,         \
      GPIO_LEDGREEN,        \
      GPIO_LEDRED,          \
      GPIO_HOT_SWAP_HANDLE, \
      GPIO_PMBUS_ALERT_B,   \
      GPIO_PMBUS_CTRL,      \
      GPIO_FPGA_DONE  ,     \
      GPIO_FPGA_PROG_B,     \
      GPIO_FPGA_RESET_B,    \
      GPIO_CLK0_INT_B,      \
      GPIO_CLK1_INT_B,      \
      GPIO_CLK0_LOL_B,      \
      GPIO_CLK1_LOL_B,      \
      GPIO_FMC0_PRSNT_B,    \
      GPIO_FMC1_PRSNT_B,    \
      GPIO_FMC0_CLK_DIR,    \
      GPIO_FMC1_CLK_DIR,    \
      GPIO_RTM_PS_B,        \
      GPIO_RTM_RES,         \
      GPIO_RTM_INT_B,       \
      GPIO_TP

typedef struct external_gpio
{
  uint8_t port_num;
  uint8_t pin_num;
} external_gpio_t;

extern const external_gpio_t ext_gpios[16];

enum
{
  EXT_GPIO_EN_P1V0,
  EXT_GPIO_EN_P1V8,
  EXT_GPIO_EN_P3V3,
  EXT_GPIO_EN_FMC1_PVADJ,
  EXT_GPIO_EN_FMC2_PVADJ,
  EXT_GPIO_P1V5_VTT_EN,
  EXT_GPIO_EN_P1V2,
  EXT_GPIO_EN_FMC1_P12V,
  EXT_GPIO_EN_FMC1_P3V3,
  EXT_GPIO_EN_FMC2_P12V,
  EXT_GPIO_EN_FMC2_P3V3,
  EXT_GPIO_EN_RTM_PWR,
  EXT_GPIO_EN_RTM_MP,
  EXT_GPIO_FPGA_I2C_RESET,
  EXT_GPIO_DAC_VADJ_RSTn,
  EXT_GPIO_PROGRAM_B,
};

#endif
