/*
 *   openMMC -- Open Source modular IPM Controller firmware
 *
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

/* Project Includes */
#include "port.h"
#include "ipmb.h"

void board_init()
{
  // NVIC_PriorityGroupConfig( NVIC_PriorityGroup_4 );
#ifdef DEBUG
  // printf("MMC_ENABLE_B=%d\n", gpio_read_pin(PIN_PORT(GPIO_MMC_ENABLE), PIN_NUMBER(GPIO_MMC_ENABLE)));
  // printf("AMC_MODE=%d\n", gpio_read_pin(PIN_PORT(AMC_MODE_ENABLE), PIN_NUMBER(AMC_MODE_ENABLE)));
  // printf("PMBUS_ALERT_B=%d\n", gpio_read_pin(PIN_PORT(GPIO_PMBUS_ALERT_B), PIN_NUMBER(GPIO_PMBUS_ALERT_B)));
  // printf("FPGA_DONE=%d\n", gpio_read_pin(PIN_PORT(GPIO_FPGA_DONE), PIN_NUMBER(GPIO_FPGA_DONE)));
  // printf("CLK0_INT_B=%d\n", gpio_read_pin(PIN_PORT(GPIO_CLK0_INT_B), PIN_NUMBER(GPIO_CLK0_INT_B)));
  // printf("CLK1_INT_B=%d\n", gpio_read_pin(PIN_PORT(GPIO_CLK1_INT_B), PIN_NUMBER(GPIO_CLK1_INT_B)));
  // printf("CLK0_LOL_B=%d\n", gpio_read_pin(PIN_PORT(GPIO_CLK0_LOL_B), PIN_NUMBER(GPIO_CLK0_LOL_B)));
  // printf("CLK1_LOL_B=%d\n", gpio_read_pin(PIN_PORT(GPIO_CLK1_LOL_B), PIN_NUMBER(GPIO_CLK1_LOL_B)));
  // printf("FMC0_PRSNT_B=%d\n", gpio_read_pin(PIN_PORT(GPIO_FMC0_PRSNT_B), PIN_NUMBER(GPIO_FMC0_PRSNT_B)));
  // printf("FMC1_PRSNT_B=%d\n", gpio_read_pin(PIN_PORT(GPIO_FMC1_PRSNT_B), PIN_NUMBER(GPIO_FMC1_PRSNT_B)));
  // printf("FMC0_CLK_DIR=%d\n", gpio_read_pin(PIN_PORT(GPIO_FMC0_CLK_DIR), PIN_NUMBER(GPIO_FMC0_CLK_DIR)));
  // printf("FMC1_CLK_DIR=%d\n", gpio_read_pin(PIN_PORT(GPIO_FMC1_CLK_DIR), PIN_NUMBER(GPIO_FMC1_CLK_DIR)));
  // printf("RTM_PS_B=%d\n", gpio_read_pin(PIN_PORT(GPIO_RTM_PS_B), PIN_NUMBER(GPIO_RTM_PS_B)));
  // printf("RTM_RES=%d\n", gpio_read_pin(PIN_PORT(GPIO_RTM_RES), PIN_NUMBER(GPIO_RTM_RES)));
  // printf("RTM_INT_B=%d\n", gpio_read_pin(PIN_PORT(GPIO_RTM_INT_B), PIN_NUMBER(GPIO_RTM_INT_B)));
  // printf("GPIO_TP=%d\n", gpio_read_pin(PIN_PORT(GPIO_TP), PIN_NUMBER(GPIO_TP)));
#endif
}

extern uint8_t ipmb_addr;

void board_config()
{
  // // Enable interrupt
  // __set_PRIMASK(0);

  // uint32_t delay;
  // uint8_t i, id;
  // if (ipmb_addr == IPMB_ADDR_DISCONNECTED)
  //   id = 0;
  // else
  //   id = (ipmb_addr - 0x70) >> 1;
  // for (i = 0; i < id; i++)
  // {
  //   // printf("%d\n", i);
  //   for (delay = 0; delay < 10000; delay++)
  //   {
  //     asm("NOP");
  //   }
  // }

}
