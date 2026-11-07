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
 * @file stm32_hpm.h
 * @brief HPM.1 self-update for STM32F1 (MMC and bootloader components)
 */

#ifdef STM32_HPM_H_
#undef STM32_HPM_H_
#endif
#define STM32_HPM_H_

enum fw_type {
    APPLICATION = 1,
    BOOTLOADER = 2
};

uint8_t ipmc_hpm_prepare_comp(void);
uint8_t ipmc_hpm_upload_block(uint8_t *block, uint16_t size);
uint8_t ipmc_hpm_finish_upload(uint32_t image_size);
uint8_t ipmc_hpm_activate_firmware(void);
uint8_t ipmc_hpm_get_upgrade_status(void);

uint8_t bootloader_hpm_prepare_comp(void);
uint8_t bootloader_hpm_upload_block(uint8_t *block, uint16_t size);
uint8_t bootloader_hpm_finish_upload(uint32_t image_size);
uint8_t bootloader_hpm_activate_firmware(void);
uint8_t bootloader_hpm_get_upgrade_status(void);
