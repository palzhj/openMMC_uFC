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

#ifndef TPL0102_H_
#define TPL0102_H_

/**
 * @brief Write a value to the TPL0102 potentiometer output register
 *
 * @param chn Selects which channel of the potentiometer will be set
 * @param val Value to be set
 */
void tpl0102_set_val( uint8_t chn, uint8_t val );
void tpl0102_set_non_volatile_val(uint8_t chn, uint8_t val);

/**
 * @brief Read from the TPL0102 potentiometer output register
 *
 * @param chn Selects which channel of the potentiometer will be set
 * @return Output value
 */
uint8_t tpl0102_get_val( uint8_t chn);

#endif
