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
 * @file ufc-v2/pin_mapping.c
 * @brief Hardware pin initialization for uFC v2.0
 *
 * @ingroup UFC_V2_PIN_MAPPING
 */

#include "pin_mapping.h"

/**
 * @brief Board specific GPIO initialization
 *
 * The actual pin directions/levels are configured by pin_init() from
 * PIN_CFG_LIST; nothing extra is needed for uFC v2.0.
 */
void gpio_init(void)
{
}
