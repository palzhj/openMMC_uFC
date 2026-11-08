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
 * @file   i2c_access.c
 *
 * @brief  Generic I2C transfer over IPMI (custom NetFn 0x32, command 0x20)
 *
 * One command covers EEPROM, PMBus, clock chips and any other I2C device,
 * including a chain of up to three I2C multiplexers:
 *
 *   request : [bus, n_mux, (mux_addr, mux_ch) * n_mux,
 *              dev_addr, wlen, wdata..., rlen]
 *   response: [rdata...]   (rlen bytes, 0 for a write-only transfer)
 *
 * bus is the physical interface (0 = I2C1, 1 = I2C2). The mux chain is
 * selected in order before the transfer; wdata is written first (register or
 * command byte), then rlen bytes are read with a repeated START.
 *
 * @ingroup I2C_ACCESS
 */

#include "ipmi.h"
#include "i2c.h"
#include "port.h"

#define I2C_XFER_MAX_MUX        3u      /**< cascade depth */
#define I2C_XFER_MAX_READ       ( IPMI_MSG_MAX_LENGTH - 1u )    /**< completion code + data */
#define I2C_XFER_TIMEOUT_MS     200u

/**
 * @brief Generic I2C transfer (write, and optionally read back)
 */
IPMI_HANDLER(ipmi_i2c_transfer, NETFN_CUSTOM, IPMI_CUSTOM_CMD_I2C_TRANSFER, ipmi_msg *req, ipmi_msg *rsp)
{
    uint8_t bus, n_mux, dev, wlen, rlen;
    const uint8_t *mux;
    const uint8_t *wdata;
    uint8_t idx;

    if ( req->data_len < 5 )
    {
        rsp->completion_code = IPMI_CC_REQ_DATA_INV_LENGTH;
        return;
    }

    bus   = req->data[0];
    n_mux = req->data[1];

    if ( bus > I2C2_ID || n_mux > I2C_XFER_MAX_MUX )
    {
        rsp->completion_code = IPMI_CC_INV_DATA_FIELD_IN_REQ;
        return;
    }

    idx = 2;
    if ( (uint16_t)idx + 2u * n_mux + 3u > req->data_len )
    {
        rsp->completion_code = IPMI_CC_REQ_DATA_INV_LENGTH;
        return;
    }
    mux = &req->data[idx];
    idx += 2u * n_mux;

    dev  = req->data[idx++];
    wlen = req->data[idx++];

    if ( dev > 0x7F )
    {
        rsp->completion_code = IPMI_CC_INV_DATA_FIELD_IN_REQ;
        return;
    }
    if ( (uint16_t)idx + wlen + 1u > req->data_len )
    {
        rsp->completion_code = IPMI_CC_REQ_DATA_INV_LENGTH;
        return;
    }
    wdata = &req->data[idx];
    idx += wlen;

    rlen = req->data[idx];
    if ( rlen > I2C_XFER_MAX_READ )
    {
        rsp->completion_code = IPMI_CC_REQ_DATA_INV_LENGTH;
        return;
    }

    if ( !i2c_take_by_interface( bus, I2C_XFER_TIMEOUT_MS ) )
    {
        rsp->completion_code = IPMI_CC_NODE_BUSY;
        return;
    }

    uint8_t ok = 1;

    /* Walk the mux chain: each stage is a single channel-select byte */
    for ( uint8_t i = 0; i < n_mux && ok; i++ )
    {
        uint8_t mux_addr = mux[2u * i];
        uint8_t mux_ch   = mux[2u * i + 1u];

        /* mux_ch is the channel number (0..7); the switch takes a bit mask */
        if ( mux_ch > 7 )
        {
            ok = 0;
            break;
        }
        uint8_t mask = (uint8_t)( 1u << mux_ch );

        if ( mux_addr > 0x7F || xI2CMasterWrite( bus, mux_addr, &mask, 1 ) != 1 )
        {
            ok = 0;
        }
    }

    if ( ok )
    {
        if ( rlen )
        {
            if ( xI2CMasterWriteRead( bus, dev, wdata, wlen, rsp->data, rlen ) != rlen )
            {
                ok = 0;
            }
        }
        else
        {
            if ( xI2CMasterWrite( bus, dev, wdata, wlen ) != wlen )
            {
                ok = 0;
            }
        }
    }

    i2c_give( bus );

    if ( !ok )
    {
        vI2CBusRecover( bus );
        rsp->completion_code = IPMI_CC_UNSPECIFIED_ERROR;
        return;
    }

    rsp->data_len = rlen;
    rsp->completion_code = IPMI_CC_OK;
}
