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
 *   0x14 write: request  [i2c_addr, addr_lo, addr_hi, data...]
 *   0x15 read : request  [i2c_addr, addr_lo, addr_hi, length]
 *                response [data...]
 *
 * 0x12/0x13 use the board's FRU EEPROM (24AA025E48, 256 bytes, TCA9548A
 * channel 0). 0x14/0x15 take an explicit 7-bit I2C address so other EEPROMs
 * on the same bus can be addressed through the same word-offset interface.
 *
 * @ingroup EEPROM_ACCESS
 */

#include "ipmi.h"
#include "eeprom_24xx02.h"
#include "i2c_mapping.h"

#define EEPROM_ACCESS_CHIP        CHIP_ID_EEPROM
#define EEPROM_ACCESS_SIZE        256u                              /* 24AA025E48: 2 Kbit */
#define EEPROM_ACCESS_WRITE_MAX   ( IPMI_MSG_MAX_LENGTH - 2u )      /* addr + payload */
#define EEPROM_ACCESS_WRITE_AT_MAX ( IPMI_MSG_MAX_LENGTH - 3u )     /* i2c + addr + payload */
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

/**
 * @brief Raw EEPROM write at an explicit I2C address (custom command 0x14)
 */
IPMI_HANDLER(ipmi_eeprom_access_write_at, NETFN_CUSTOM, IPMI_CUSTOM_CMD_EEPROM_WRITE_AT, ipmi_msg *req, ipmi_msg *rsp)
{
    uint8_t i2c_addr;
    uint16_t addr;
    uint8_t len;

    if ( req->data_len < 4 )
    {
        rsp->completion_code = IPMI_CC_REQ_DATA_INV_LENGTH;
        return;
    }

    i2c_addr = req->data[0];
    addr = (uint16_t)( req->data[1] | ( req->data[2] << 8 ) );
    len  = (uint8_t)( req->data_len - 3 );

    if ( i2c_addr < 0x08 || i2c_addr > 0x77 )
    {
        rsp->completion_code = IPMI_CC_INV_DATA_FIELD_IN_REQ;
        return;
    }
    if ( len > EEPROM_ACCESS_WRITE_AT_MAX )
    {
        rsp->completion_code = IPMI_CC_REQ_DATA_INV_LENGTH;
        return;
    }
    if ( (uint32_t)addr + len > EEPROM_ACCESS_SIZE )
    {
        rsp->completion_code = IPMI_CC_INV_DATA_FIELD_IN_REQ;
        return;
    }

    /* Probe first: writing to an absent device would sit in the retry loop
     * (200 x write timeout) and block the IPMI task for seconds. */
    {
        uint8_t probe;
        if ( eeprom_24xx02_read_at( EEPROM_ACCESS_CHIP, i2c_addr, addr, &probe, 1,
                                    (uint32_t)pdMS_TO_TICKS( 200 ) ) != 1 )
        {
            rsp->completion_code = IPMI_CC_UNSPECIFIED_ERROR;
            return;
        }
    }

    if ( eeprom_24xx02_write_at( EEPROM_ACCESS_CHIP, i2c_addr, addr, &req->data[3], len,
                                 (uint32_t)pdMS_TO_TICKS( 200 ) ) != len )
    {
        rsp->completion_code = IPMI_CC_UNSPECIFIED_ERROR;
        return;
    }

    rsp->completion_code = IPMI_CC_OK;
}

/**
 * @brief Raw EEPROM read at an explicit I2C address (custom command 0x15)
 */
IPMI_HANDLER(ipmi_eeprom_access_read_at, NETFN_CUSTOM, IPMI_CUSTOM_CMD_EEPROM_READ_AT, ipmi_msg *req, ipmi_msg *rsp)
{
    uint8_t i2c_addr;
    uint16_t addr;
    uint8_t len;

    if ( req->data_len < 4 )
    {
        rsp->completion_code = IPMI_CC_REQ_DATA_INV_LENGTH;
        return;
    }

    i2c_addr = req->data[0];
    addr = (uint16_t)( req->data[1] | ( req->data[2] << 8 ) );
    len  = req->data[3];

    if ( i2c_addr < 0x08 || i2c_addr > 0x77 )
    {
        rsp->completion_code = IPMI_CC_INV_DATA_FIELD_IN_REQ;
        return;
    }
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

    if ( eeprom_24xx02_read_at( EEPROM_ACCESS_CHIP, i2c_addr, addr, rsp->data, len,
                                (uint32_t)pdMS_TO_TICKS( 200 ) ) != len )
    {
        rsp->completion_code = IPMI_CC_UNSPECIFIED_ERROR;
        return;
    }

    rsp->data_len = len;
    rsp->completion_code = IPMI_CC_OK;
}
