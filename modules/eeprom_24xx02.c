/*
 *   openMMC -- Open Source modular IPM Controller firmware
 *
 *   Copyright (C) 2023  Gustavo Reis <gustavo.reis@lnls.br>
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
 * @file   eeprom_24xx02.c
 * @author Gustavo Reis <gustavo.reis@lnls.br>
 *
 * @brief  24xx02 EEPROM module interface implementation
 *
 * @ingroup 24xx02
 */

/* FreeRTOS includes */
#include "FreeRTOS.h"
#include "task.h"
#include "string.h"

/* Project Includes */
#include "eeprom_24xx02.h"
#include "port.h"
#include "i2c.h"

size_t eeprom_24xx02_read( uint8_t id, uint16_t address, uint8_t *rx_data, size_t buf_len, TickType_t timeout )
{
    uint8_t i2c_addr;
    uint8_t i2c_interface;
    uint8_t rx_len = 0;

    if ( rx_data == NULL ) {
        return 0;
    }

    /* The first bus accesses after power-up / I2C init can fail (the I2C mux
     * NACKs its first transactions), so retry the whole take+read. This may
     * run before the FreeRTOS scheduler starts -> busy wait, not vTaskDelay. */
    for ( uint8_t attempt = 0; attempt < 10; attempt++ ) {
        if ( i2c_take_by_chipid( id, &i2c_addr, &i2c_interface, timeout ) ) {
            rx_len = xI2CMasterWriteRead( i2c_interface, i2c_addr, (uint8_t *)&address, 1, rx_data, buf_len );
            i2c_give( i2c_interface );
            if ( rx_len == buf_len ) {
                break;
            }
        }
        for ( volatile uint32_t d = 0; d < 20000; d++ ) { }
    }

    return rx_len;
}

size_t eeprom_24xx02_write( uint8_t id, uint16_t address, uint8_t *tx_data, size_t buf_len, TickType_t timeout )
{
    uint8_t i2c_addr;
    uint8_t i2c_interface;
    uint8_t bytes_to_write;
    uint8_t i2c_written = 0;
    uint8_t page_buf[9];
    uint16_t curr_addr;

    size_t tx_len = 0;
    uint32_t retries = 0;

    if ( tx_data != NULL ) {
        curr_addr = address;

        while (tx_len < buf_len) {
            bytes_to_write = 8 - (curr_addr % 8);

            if (bytes_to_write > ( buf_len - tx_len )) {
                bytes_to_write = ( buf_len - tx_len );
            }
            page_buf[0] = (curr_addr) & 0xFF;

            memcpy(&page_buf[1], tx_data+tx_len, bytes_to_write);

            if (i2c_take_by_chipid( id, &i2c_addr, &i2c_interface, timeout)) {
                /* Write the data */
                i2c_written = xI2CMasterWrite( i2c_interface, i2c_addr, &page_buf[0] , bytes_to_write+1 );
                /* When trying to write, the EEPROM will reply with NACKs if it's busy.
                 * If the i2c receives a NACK when trying to write, the function returns only
                 * the number of bytes successfully written, and we should increment only this value
                 * in tx_len and in curr_addr */

                if (i2c_written) {
                    tx_len += i2c_written - 1; /* Remove byte address from data written size */
                    curr_addr += i2c_written - 1;
                    retries = 0;
                } else if (++retries > 200) {
                    /* EEPROM keeps NACKing (write-protected or absent) */
                    i2c_give( i2c_interface );
                    break;
                }
                i2c_give( i2c_interface );
            }
            /* 24AA025E48 internal write cycle is 5 ms. Do NOT use vTaskDelay():
             * fru_init() may run before the scheduler is started. */
            for (volatile uint32_t d = 0; d < 60000; d++) { }
        }
    }

    return tx_len;
}
