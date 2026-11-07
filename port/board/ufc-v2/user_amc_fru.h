/*
 *   openMMC -- Open Source modular IPM Controller firmware
 *
 *   Copyright (C) 2015  Julian Mendez  <julian.mendez@cern.ch>
 *   Copyright (C) 2015-2016  Henrique Silva <henrique.silva@lnls.br>
 *   Copyright (C) 2021  Krzysztof Macias <krzysztof.macias@creotech.pl>
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

#include "fru_editor.h"

/*********************************************
 * Common defines
 *********************************************/
#define AMC_LANG_CODE               0
#define AMC_FRU_FILE_ID             ""              //Dropped to fit the 128-byte writable EEPROM half

/*********************************************
 * Board information area
 *********************************************/
#define AMC_BOARD_MANUFACTURING_TIME 16226822               /* Amount of minutes since 0:00 1/1/96 */
#define AMC_BOARD_MANUFACTURER      "IHEP"
#define AMC_BOARD_NAME              "uFC"
#define AMC_BOARD_SN                "H01"
#define AMC_BOARD_PN                "HEPS"

/*********************************************
 * Product information area
 *********************************************/
#define AMC_PRODUCT_MANUFACTURER    "IHEP"
#define AMC_PRODUCT_NAME            "uFC"
#define AMC_PRODUCT_PN              "AMC"
#define AMC_PRODUCT_VERSION         "2.0"
#define AMC_PRODUCT_SN              ""
#define AMC_PRODUCT_ASSET_TAG       ""

/*
 * The FRU EEPROM is a 24AA025E48 (2 Kbit). Its upper half (0x80..0xFF) is
 * permanently write-protected and holds the factory EUI-48, so only 128 bytes
 * (0x00..0x7F) are usable for the FRU. Keep the build under that limit.
 */
#define AMC_FRU_MAX_SIZE            128
/* The clock record alone is 50 bytes and does not fit together with the
 * mandatory module current record; it is omitted on this board. */
#define AMC_FRU_INCLUDE_CLOCK_RECORD 0

/*********************************************
 * AMC: Point to point connectivity record
 *********************************************/
#define AMC_POINT_TO_POINT_RECORD_LIST                                  \
    GENERIC_POINT_TO_POINT_RECORD(0, PORT(4), PORT(5), PORT(6), PORT(7), ETHERNET, BASE_10G_BX4, MATCHES_01)

/*********************************************
 * AMC: Point to point clock record
 *********************************************/
/* Example:
   DIRECT_CLOCK_CONNECTION(CLOCK_ID, ACTIVATION, PLL_USE, SOURCE/RECEIVER, CLOCK_FAMILY, ACCURACY, FREQUENCY, MIN FREQUENCY, MAX FREQUENCY) */
#define AMC_CLOCK_CONFIGURATION_LIST                                    \
    DIRECT_CLOCK_CONNECTION(FCLKA, CIPMC, NO_PLL, RECEIVER, PCI_RESERVED, 0, MHz(100), MHz(99), MHz(101)) \
    DIRECT_CLOCK_CONNECTION(TCLKA, APP, NO_PLL, RECEIVER, UNSPEC_FAMILY, 0, KHz(125000), KHz(124900), KHz(125100))
/**********************************************
 * PICMG: Module current record
 **********************************************/
#define AMC_MODULE_CURRENT_RECORD            current_in_ma(5400)

/*********************************************
 * Zone 3
 *********************************************/
/* uFC has no Zone 3 connector, so no Zone 3 Interface Compatibility record. */
#define AMC_FRU_INCLUDE_ZONE3_RECORD         0
