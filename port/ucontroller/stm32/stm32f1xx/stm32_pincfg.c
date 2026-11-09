/*
 *   openMMC  --
 *
 *   Copyright (C) 2015  Henrique Silva  <henrique.silva@lnls.br>
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
 */

/**
 * @file stm32_pincfg.c
 * @brief Pin Config functions redirection for STM32
 *
 * @author Henrique Silva <henrique.silva@lnls.br>, LNLS
 */

#include "port.h"

/**
 * @brief       Sets I/O Control pin mux
 * @param       port    : GPIO port to mux
 * @param       pin     : GPIO pin to mux
 * @param       cfg     : Configuration bits to select pin mode/function
 * @see IOCON_17XX_40XX_MODE_FUNC
 */

void pin_init(void)
{
  uint8_t i;
  uint32_t cfg[] = {PIN_CFG_LIST};
  uint8_t list_len = sizeof(cfg) / (sizeof(cfg[0]));
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  GPIO_InitStruct.GPIO_Speed  = GPIO_Speed_2MHz;

  GPIO_PinRemapConfig(GPIO_Remap_SWJ_JTAGDisable,ENABLE);

  /* GPIO Ports Clock Enable */
  RCC_APB2PeriphClockCmd( RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB | RCC_APB2Periph_GPIOC
    | RCC_APB2Periph_AFIO, ENABLE );

  for (i = 0; i < list_len; i++)
  {
    if(PIN_DIR(cfg[i]) != NON_GPIO)
    {
      if (PIN_DIR(cfg[i]) == GPIO_DIR_INPUT)
      {
        GPIO_InitStruct.GPIO_Mode  = GPIO_Mode_IN_FLOATING;
      }
      else if (PIN_DIR(cfg[i]) == GPIO_DIR_OUTPUT)
      {
        GPIO_InitStruct.GPIO_Mode  = GPIO_Mode_Out_PP;
        if(PIN_FUNC(cfg[i]) == GPIO_LEVEL_HIGH)
        {
          GPIO_SetBits(PIN_PORT(cfg[i]), 1<<PIN_NUMBER(cfg[i]));
        }
        else
        {
          GPIO_ResetBits(PIN_PORT(cfg[i]), 1<<PIN_NUMBER(cfg[i]));
        }
      }
      GPIO_InitStruct.GPIO_Pin  = 1<<PIN_NUMBER(cfg[i]);
      GPIO_Init(PIN_PORT(cfg[i]), &GPIO_InitStruct);
    }
  }
}

GPIO_TypeDef *PIN_PORT(uint32_t pin_def)
{
  switch ((pin_def & 0xFF000000) >> 24)
  {
    case 1:
      return GPIOB;
    case 2:
      return GPIOC;
    default:
      return GPIOA;
  }
}
