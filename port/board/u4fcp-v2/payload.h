/*
 *   openMMC -- Open Source modular IPM Controller firmware
 *
 *   Copyright (C) 2015  Piotr Miedzik  <P.Miedzik@gsi.de>
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

/**
 * @file afcv4/payload.h
 * @brief Payload control module definitions for AFCv4.0
 *
 * @ingroup AFC_V4_0_PAYLOAD
 */

/**
 * @defgroup AFC_V4_0_PAYLOAD AFCv4 Payload Control
 * @ingroup AFC_V4_0
 * @{
 */

#ifndef PAYLOAD_H_
#define PAYLOAD_H_

#include "event_groups.h"

typedef struct
{
  uint16_t address; /* 16-bit register address */
  uint8_t value; /* 8-bit register data */
} si5345_revd_register_t;

/**
 * @brief Payload state machine state numbers
 */
extern enum {
    PAYLOAD_NO_POWER = 0,
    PAYLOAD_POWER_GOOD_WAIT,
    PAYLOAD_STATE_CLK_SETUP,
    PAYLOAD_STATE_FPGA_SETUP,
    PAYLOAD_FPGA_ON,
    PAYLOAD_SWITCHING_OFF,
    PAYLOAD_QUIESCED,
    PAYLOAD_RESET,
    PAYLOAD_MAX_STATES,
} payload_state;

/**
 * @defgroup AFC_V4_0_PAYLOAD_MSG Payload Messages Codes
 * @ingroup AFC_V4_0_PAYLOAD
 * @{
 */
#define PAYLOAD_MESSAGE_COLD_RST        (1 << 0)
#define PAYLOAD_MESSAGE_WARM_RST        (1 << 1)
#define PAYLOAD_MESSAGE_REBOOT          (1 << 2)
#define PAYLOAD_MESSAGE_QUIESCE         (1 << 3)
#define PAYLOAD_MESSAGE_RTM_ENABLE      (1 << 4)
#define PAYLOAD_MESSAGE_DCDC_PGOOD      (1 << 5)
#define PAYLOAD_MESSAGE_DCDC_PGOODn     (1 << 6)
#define PAYLOAD_MESSAGE_CLOCK_CONFIG	  (1 << 7)
/**
 * @}
 */

/**
 * @brief Payload task unblock delay
 */
#define PAYLOAD_BASE_DELAY 1000

#ifdef MODULE_UCD90XXX

extern enum {
  UCD_AMC_12V_ID = 0,
  UCD_RTM_12V_CURR_ID,
  UCD_FMC0_12V_CURR_ID,
  UCD_FMC0_VADJ_ID,
  UCD_FMC0_3V3_CURR_ID,
  UCD_FMC1_12V_CURR_ID,
  UCD_FMC1_VADJ_ID,
  UCD_FMC1_3V3_CURR_ID,
  UCD_FPGA_0V85_ID,
  UCD_FPGA_1V8_ID,
  UCD_FPGA_0V9A_ID,
  UCD_FPGA_1V2A_ID,
  UCD_DDR_1V2T_ID,
  UCD_DDR_1V2B_ID,
  UCD_AMC_IO_3V3_ID,
  UCD_CH_COUNT
} UCD_AMC_ANALOG_IN;

#define SDR_CH_COUNT (UCD_CH_COUNT * 2)

// GPIO
extern enum {
  UCD_GPI_PG_ID = 0,  //MAR01
  UCD_GPI_FMC1_PG_M2C_ID, // MAR02
  UCD_GPI_FMC0_12V0_FLT_B_ID, // MAR03
  UCD_GPIO_ID_3, // MAR04
  UCD_GPI_FMC0_3V3_FLT_B_ID, // MAR05
  UCD_GPI_FMC0_3V3AUX_FLT_B_ID, // MAR06
  UCD_GPI_FMC1_3V3AUX_FLT_B_ID, // MAR07
  UCD_GPI_FMC1_PRSNT_B_ID, // MAR08
  UCD_GPI_FMC0_PRSNT_B_ID, // MAR09
  UCD_GPI_FMC0_PG_M2C_ID, // MAR10
  UCD_GPIO_ID_10, // MAR11
  UCD_GPIO_ID_11, // MAR12
  UCD_GPIO_ID_12, // MAR13
  UCD_GPIO_ID_13, // MAR14
  UCD_GPI_RTM_12V0_FLT_B_ID, // MAR15
  UCD_GPIO_ID_15, // MAR16
  UCD_GPIO_ID_16, // MAR17
  UCD_GPI_RTM_PS_B_ID, // MAR18
  UCD_GPI_FMC1_12V0_FLT_B_ID, // MAR19
  UCD_GPI_FMC1_3V3_FLT_B_ID, // MAR20
  UCD_GPIO_ID_20, // MAR21
  UCD_GPI_OT_B_ID, // MAR22
  UCD_GPIO_ID_22, // MAR23
  UCD_GPI_FANFAIL_B_ID, // MAR24
  UCD_GPIO_ID_24, // EN1
  UCD_GPIO_ID_25, // EN2
  UCD_GPIO_ID_26, // EN3
  UCD_GPIO_ID_27, // EN4
  UCD_GPIO_ID_28, // EN5
  UCD_GPIO_ID_29, // EN6
  UCD_GPIO_ID_30, // EN7
  UCD_GPIO_ID_31, // EN8
  UCD_GPIO_ID_32, // EN9
  UCD_GPIO_ID_33, // EN10
  UCD_GPIO_ID_34, // EN11
  UCD_GPIO_ID_35, // EN12
  UCD_GPIO_ID_36, // EN13
  UCD_GPIO_ID_37, // EN14
  UCD_GPIO_ID_38, // EN15
  UCD_GPIO_ID_39, // EN16
  UCD_GPIO_ID_40, // EN17
  UCD_GPIO_ID_41, // EN18
  UCD_GPIO_ID_42, // EN19
  UCD_GPIO_ID_43, // EN20
  UCD_GPIO_ID_44, // EN21
  UCD_GPIO_ID_45, // EN22
  UCD_GPIO_ID_46, // EN23
  UCD_GPIO_ID_47, // EN24
  UCD_GPIO_ID_48, // EN25
  UCD_GPIO_ID_49, // EN26
  UCD_GPIO_ID_50, // EN27
  UCD_GPIO_ID_51, // EN28
  UCD_GPIO_ID_52, // EN29
  UCD_GPIO_ID_53, // EN30
  UCD_GPIO_ID_54, // EN31
  UCD_GPIO_ID_55, // EN32
  UCD_GPIO_ID_56, // LGPO1
  UCD_GPO_FMC0_3V3AUX_EN_ID, // LGPO2
  UCD_GPO_LED_FMC0_ID, // LGPO3
  UCD_GPO_LED_FMC1_ID, // LGPO4
  UCD_GPIO_ID_60, // LGPO5
  UCD_GPIO_ID_61, // LGPO6
  UCD_GPO_FMC1_3V3AUX_EN_ID, // LGPO7
  UCD_GPIO_ID_63, // LGPO8
  UCD_GPO_FMC0_PG_C2M_ID, // LGPO9
  UCD_GPO_LED_FMC0_VADJ_PG_ID, // LGPO10
  UCD_GPO_LED_FMC1_VADJ_PG_ID, // LGPO11
  UCD_GPIO_ID_67, // LGPO12
  UCD_GPO_FMC1_PG_C2M_ID, // LGPO13
  UCD_GPO_RTM_MP_EN_ID, // LGPO14
  UCD_GPIO_ID_70, // LGPO15
  UCD_GPI_RTM_MP_FLT_B_ID, // LGPO16
  UCD_GPI_FMC0_12V0_PG_B_ID, // DMON1
  UCD_GPI_FMC0_3V3_PG_B_ID, // DMON2
  UCD_GPIO_ID_74, // DMON3
  UCD_GPIO_ID_75, // DMON4
  UCD_GPI_FMC1_12V0_PG_B_ID, // DMON5
  UCD_GPIO_ID_77, // DMON6
  UCD_GPI_RTM_12V0_PG_B_ID, // DMON7
  UCD_GPI_FMC1_3V3_PG_B_ID, // DMON8
  UCD_GPIO_ID_80, // GPIO1
  UCD_GPIO_ID_81, // GPIO2
  UCD_GPIO_ID_82, // GPIO3
  UCD_GPIO_ID_83, // GPIO4
  UCD_GPIO_COUNT
} UCD_AMC_GPIO;

#endif


/**
 * @brief Payload task handle variable
 */
extern TaskHandle_t vTaskPayload_Handle;

/**
 * @brief Sends a message to the payload task
 *
 * This function basically sets a flag that the Payload task reads and advances (or not) on the state machine
 *
 * @param fru_id Target FRU ID (0:AMC 1:RTM)
 * @param msg Message to send, using @ref AFC_V3_1_PAYLOAD_MSG definitions
 */
void payload_send_message( uint8_t fru_id, EventBits_t msg );

/**
 * @brief Payload Control task
 *
 * @param pvParameters Pointer to buffer holding parameters passed to task upon initialization
 */
void vTaskPayload( void *pvParameters );

/**
 * @brief Creates Payload Control task and initializes the board's needed hardware
 */
void payload_init( void );

#ifdef MODULE_HPM

#define PAYLOAD_HPM_PAGE_SIZE    256

uint8_t payload_hpm_prepare_comp( void );
uint8_t payload_hpm_upload_block( uint8_t * block, uint16_t size );
uint8_t payload_hpm_finish_upload( uint32_t image_size );
uint8_t payload_hpm_get_upgrade_status( void );
uint8_t payload_hpm_activate_firmware( void );
#endif

#endif /* IPMI_PAYLOAD_H_ */

/**
 * @}
 */
