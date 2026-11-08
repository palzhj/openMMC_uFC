/*
 *   openMMC -- Open Source modular IPM Controller firmware
 *
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

/**
 * @file eeprom_access.c
 *
 * @brief Raw EEPROM access over IPMI (custom NetFn 0x32)
 *
 * Commands:
 *   0x12 write: request  [addr_lo, addr_hi, data...]      (max 30 data bytes)
 *   0x13 read : request  [addr_lo, addr_hi, length]       (max 32 bytes)
 *                response [data...]
 *
 * Only the FRU EEPROM (24AA025E48, 256 bytes, TCA9548A channel 0) is exposed.
 *
 * @ingroup EEPROM_ACCESS
 */

#include "ipmi.h"
#include "eeprom_24xx02.h"
#include "i2c_mapping.h"

#define EEPROM_ACCESS_CHIP        CHIP_ID_EEPROM
#define EEPROM_ACCESS_SIZE        256u                              /* 24AA025E48: 2 Kbit */
#define EEPROM_ACCESS_WRITE_MAX   ( IPMI_MSG_MAX_LENGTH - 2u )      /* addr + payload */
#define EEPROM_ACCESS_READ_MAX    ( IPMI_MSG_MAX_LENGTH - 1u )      /* completion code + payload */

/**
 * @brief Raw EEPROM write (custom command 0x12)
 */
IPMI_HANDLER(ipmi_eeprom_access_write, NETFN_CUSTOM, IPMI_CUSTOM_CMD_EEPROM_WRITE, ipmi_msg *req, ipmi_msg *rsp)
{
    uint16_t addr;
    uint8_t len;

    if ( req->data_len < 3 )
    {
        rsp->completion_code = IPMI_CC_REQ_DATA_INV_LENGTH;
        return;
    }

    addr = (uint16_t)( req->data[0] | ( req->data[1] << 8 ) );
    len  = (uint8_t)( req->data_len - 2 );

    if ( len > EEPROM_ACCESS_WRITE_MAX )
    {
        rsp->completion_code = IPMI_CC_REQ_DATA_INV_LENGTH;
        return;
    }
    if ( (uint32_t)addr + len > EEPROM_ACCESS_SIZE )
    {
        rsp->completion_code = IPMI_CC_INV_DATA_FIELD_IN_REQ;
        return;
    }

    if ( eeprom_24xx02_write( EEPROM_ACCESS_CHIP, addr, &req->data[2], len,
                              (uint32_t)pdMS_TO_TICKS( 200 ) ) != len )
    {
        rsp->completion_code = IPMI_CC_UNSPECIFIED_ERROR;
        return;
    }

    rsp->completion_code = IPMI_CC_OK;
}

/**
 * @brief Raw EEPROM read (custom command 0x13)
 */
IPMI_HANDLER(ipmi_eeprom_access_read, NETFN_CUSTOM, IPMI_CUSTOM_CMD_EEPROM_READ, ipmi_msg *req, ipmi_msg *rsp)
{
    uint16_t addr;
    uint8_t len;

    if ( req->data_len < 3 )
    {
        rsp->completion_code = IPMI_CC_REQ_DATA_INV_LENGTH;
        return;
    }

    addr = (uint16_t)( req->data[0] | ( req->data[1] << 8 ) );
    len  = req->data[2];

    if ( len == 0 || len > EEPROM_ACCESS_READ_MAX )
    {
        rsp->completion_code = IPMI_CC_REQ_DATA_INV_LENGTH;
        return;
    }
    if ( (uint32_t)addr + len > EEPROM_ACCESS_SIZE )
    {
        rsp->completion_code = IPMI_CC_INV_DATA_FIELD_IN_REQ;
        return;
    }

    if ( eeprom_24xx02_read( EEPROM_ACCESS_CHIP, addr, rsp->data, len,
                             (uint32_t)pdMS_TO_TICKS( 200 ) ) != len )
    {
        rsp->completion_code = IPMI_CC_UNSPECIFIED_ERROR;
        return;
    }

    rsp->data_len = len;
    rsp->completion_code = IPMI_CC_OK;
}
