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
#define AMC_FRU_FILE_ID             "u4fcp-v2.fru"       //Allows knowing the source of the FRU present in the memory

/*********************************************
 * Board information area
 *********************************************/
#define AMC_BOARD_MANUFACTURING_TIME 16226822               /* Amount of minutes since 0:00 1/1/96 */
#define AMC_BOARD_MANUFACTURER      "IHEP"
#define AMC_BOARD_NAME              "u4FCP"
#define AMC_BOARD_SN                "SL01"
#define AMC_BOARD_PN                "u4FCP-v2"

/*********************************************
 * Product information area
 *********************************************/
#define AMC_PRODUCT_MANUFACTURER    "IHEP"
#define AMC_PRODUCT_NAME            "u4FCP"
#define AMC_PRODUCT_PN              "u4FCP-v2"
#define AMC_PRODUCT_VERSION         "2.2"
#define AMC_PRODUCT_SN              "SL01"
#define AMC_PRODUCT_ASSET_TAG       "IHEP"

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
#define AMC_MODULE_CURRENT_RECORD            current_in_ma(8000)

/*********************************************
 * RTM Compatibility Record
 *********************************************/
#define AMC_COMPATIBILITY_CODE               0x55667788
