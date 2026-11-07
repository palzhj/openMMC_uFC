/*
 *   openMMC -- Open Source modular IPM Controller firmware
 *
 *   Copyright (C) 2015-2016  Henrique Silva <henrique.silva@lnls.br>
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

/*! @file fru.c
 */

#include "port.h"
#include "fru.h"
#include "fru_editor.h"
#include "at24mac.h"
#ifdef MODULE_EEPROM_24XX02
#include "eeprom_24xx02.h"
#define FRU_EEPROM_READ     eeprom_24xx02_read
#define FRU_EEPROM_WRITE    eeprom_24xx02_write
#else
#include "eeprom_24xx512.h"
#define FRU_EEPROM_READ     eeprom_24xx512_read
#define FRU_EEPROM_WRITE    eeprom_24xx512_write
#endif
#ifdef RENODE_TEST
/* RAM-backed EEPROM for the Renode FRU write/read self-test. */
size_t fru_test_read( uint8_t id, uint16_t address, uint8_t *buffer, size_t len, uint32_t timeout );
size_t fru_test_write( uint8_t id, uint16_t address, uint8_t *buffer, size_t len, uint32_t timeout );
#undef FRU_EEPROM_READ
#undef FRU_EEPROM_WRITE
#define FRU_EEPROM_READ     fru_test_read
#define FRU_EEPROM_WRITE    fru_test_write
#endif
#include "i2c_mapping.h"
#ifdef MODULE_RTM
#include "rtm_i2c_mapping.h"
#endif

#if defined(FRU_READ_ONLY) || defined(FRU_NO_BUILDER)
#define AMC_FRU_BUILD   NULL
#else
#define AMC_FRU_BUILD   amc_fru_info_build
#endif

fru_data_t fru[FRU_COUNT] = {
    [FRU_AMC] = {
        .cfg = {
            .eeprom_id = CHIP_ID_EEPROM,
            .build_f = AMC_FRU_BUILD,
            .read_f = FRU_EEPROM_READ,
            .write_f = FRU_EEPROM_WRITE,
        },
        .runtime = false
    },
#ifdef MODULE_RTM

    [FRU_RTM] = {
        .cfg = {
            .eeprom_id = CHIP_ID_RTM_EEPROM,
            .build_f = rtm_fru_info_build,
            .read_f = FRU_EEPROM_READ,
            .write_f = FRU_EEPROM_WRITE,
        },
        .runtime = false
    }
#endif
};
