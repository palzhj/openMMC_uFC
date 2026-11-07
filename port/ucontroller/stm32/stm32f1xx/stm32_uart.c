/*
 *   openMMC  --
 *
 *   Copyright (C) 2025  Jie Zhang  <zhj@ihep.ac.cn>
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

/*!
 * @file stm32_uart.c
 * @author Jie Zhang <zhj@ihep.ac.cn>, IHEP
 * @date Feb.17 2025
 *
 * @brief
 */
#include "FreeRTOS.h"
#include "task.h"
#include "stm32_uart.h"

const stm32_uart_cfg_t usart_cfg[3] = {
    {USART1, USART1_IRQn},
    {USART2, USART2_IRQn},
    {USART3, USART3_IRQn}};

void StartDefaultTask(void *pvParameters);

/**
 * @brief Initialize a USART peripheral for serial communication
 *
 * This function configures GPIO pins, clock sources and USART settings for
 * serial communication. It handles initialization for three possible USART
 * interfaces (ID 0-2) with the following fixed configuration:
 * - 115200 baud rate
 * - 8 data bits, 1 stop bit, no parity
 * - Both transmitter and receiver enabled
 * - No hardware flow control
 *
 * @param id USART interface identifier (0-2 corresponding to USART1-USART3)
 *
 * @note The function configures:
 *        - TX pin as alternate function push-pull
 *        - RX pin as input floating
 *        - Required GPIO and USART peripheral clocks
 *        - Default alternate function mappings for STM32 USART interfaces
 */
void uart_init(uint8_t id)
{
  GPIO_InitTypeDef GPIO_InitStructure;
  USART_InitTypeDef USART_InitStructure;

  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
  GPIO_InitStructure.GPIO_Speed = GPIO_Speed_2MHz;
  switch (id)
  {
  case UART2_ID:
    /* Enable GPIO and USART clocks */
    // RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_AFIO, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART2, ENABLE);
    /* Configure USART Tx as alternate function push-pull */
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_2;
    GPIO_Init(GPIOA, &GPIO_InitStructure);
    break;

  case UART3_ID:
    /* Enable GPIO and USART clocks */
    // RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB | RCC_APB2Periph_AFIO, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART3, ENABLE);
    /* Configure USART Tx as alternate function push-pull */
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_10;
    GPIO_Init(GPIOB, &GPIO_InitStructure);
    break;

  default: // case 0
    /* Enable GPIO and USART clocks */
    // RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_AFIO, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1, ENABLE);
    /* Configure USART Tx as alternate function push-pull */
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_9;
    GPIO_Init(GPIOA, &GPIO_InitStructure);
  }

  /* Configure USART Rx as input floating */
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
  switch (id)
  {
  case UART2_ID:
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_1;
    GPIO_Init(GPIOA, &GPIO_InitStructure);
    break;
  case UART3_ID:
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_11;
    GPIO_Init(GPIOB, &GPIO_InitStructure);
    break;
  default: // case uart0
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_10;
    GPIO_Init(GPIOA, &GPIO_InitStructure);
  }

  /* Configure USART settings */
  USART_InitStructure.USART_BaudRate = 115200;
  USART_InitStructure.USART_WordLength = USART_WordLength_8b;
  USART_InitStructure.USART_StopBits = USART_StopBits_1;
  USART_InitStructure.USART_Parity = USART_Parity_No;
  USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
  USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;

  /* Initialize USART */
  USART_Init(usart_cfg[id].ptr, &USART_InitStructure);

  /* Enable USART */
  USART_Cmd(usart_cfg[id].ptr, ENABLE);

  printf("\n");

  // xTaskCreate(StartDefaultTask,           // Task function
  //   "UART Task",         // Task name
  //   configMINIMAL_STACK_SIZE,                // Stack size (in words)
  //   NULL,               // Parameters passed to the task (NULL in this case)
  //   (( UBaseType_t ) configTIMER_TASK_PRIORITY ) | portPRIVILEGE_BIT,                  // Task priority (1 is the lowest priority)
  //   NULL);
}

/**
 * @brief Transmit data over a USART interface
 *
 * This function sends a sequence of bytes through the specified USART peripheral
 * using blocking transmission. It waits for each byte to complete transmission
 * before sending the next one.
 *
 * @param id USART interface identifier (0-2 corresponding to USART1-USART3)
 * @param msg Pointer to the data buffer containing bytes to transmit
 * @param len Number of bytes to transmit
 *
 * @note This is a blocking implementation - it will wait indefinitely for each
 *       byte's transmission to complete using the USART_FLAG_TC status flag.
 *       Ensure proper buffer management as this function directly transmits
 *       without buffering.
 */
void uart_send(uint8_t id, char *msg, uint32_t len)
{
  uint32_t i;
  for (i = 0; i < len; i++)
  {
    // Send data byte by byte
    USART_SendData(usart_cfg[id].ptr, (uint16_t)msg[i]);

    // Wait until the data is transmitted
    while (USART_GetFlagStatus(usart_cfg[id].ptr, USART_FLAG_TC) == RESET)
      ; // Ensure Transmission Complete flag is set
  }
}

/**
 * @brief Read data from a USART interface
 *
 * This function receives a sequence of bytes through the specified USART peripheral
 * using blocking reception. It waits for each byte to be received before reading
 * the next one.
 *
 * @param id USART interface identifier (0-2 corresponding to USART1-USART3)
 * @param buf Pointer to the data buffer where received bytes will be stored
 * @param len Number of bytes to receive
 *
 * @note This is a blocking implementation - it will wait indefinitely for each
 *       byte's reception to complete using the USART_FLAG_RXNE status flag.
 *       Ensure proper buffer management as this function directly stores received
 *       data without intermediate buffering. The function reads bytes immediately
 *       when available in the USART receive buffer.
 */
void uart_read(uint8_t id, char *buf, uint32_t len)
{
  uint32_t i;
  for (i = 0; i < len; i++)
  {
    // Wait until data is received
    while (USART_GetFlagStatus(usart_cfg[id].ptr, USART_FLAG_RXNE) == RESET)
      ; // Wait until data is received

    // Read the received byte
    buf[i] = (char)USART_ReceiveData(usart_cfg[id].ptr); // Cast to char to avoid type mismatch
  }
}

/* USER CODE END Header_StartDefaultTask */
// void StartDefaultTask(void *pvParameters)
// {
//   /* Infinite loop */
//   for(;;)
//   {
//     vTaskDelay(pdMS_TO_TICKS(2000));
//     uart_send(UART_DEBUG, (char*)".", 1);
//   }
// }