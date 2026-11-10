/*
 *   AFCIPMI  --
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

/*!
 * @file lpc17_watchdog.h
 * @author Henrique Silva <henrique.silva@lnls.br>, LNLS
 * @date December 2015
 *
 * @brief Definitions of Watchdog driver implementation for LPC17xx
 */

#ifdef LPC17_WATCHDOG_H_
#undef LPC17_WATCHDOG_H_
#endif
#define LPC17_WATCHDOG_H_

#include "port.h"

/* The STM32F1 independent watchdog (IWDG) runs from the ~40 kHz LSI.
 * With the prescaler 64 the counter ticks at 625 Hz, so the load value is
 * 625 ticks per second and 0..4095 covers 0..6.55 s.
 * wdt_set_timeout() receives the count computed by modules/watchdog.c:
 *   timeout = (WATCHDOG_TIMEOUT / 1000) * WATCHDOG_CLK_FREQ
 * with WATCHDOG_CLK_FREQ = 8 000 000, i.e. 12800 counts per IWDG tick. */
#define WDT_IWDG_TICKS_PER_S   625u
#define WDT_CLK_COUNTS_PER_TICK (8000000u / WDT_IWDG_TICKS_PER_S)

/**
 * @brief       Initialize the Watchdog timer
 * @return      None
 */
#define wdt_init()                                             \
    do {                                                        \
        RCC_LSICmd(ENABLE);                                     \
        while (RCC_GetFlagStatus(RCC_FLAG_LSIRDY) == RESET) { } \
        IWDG_WriteAccessCmd(IWDG_WriteAccess_Enable);           \
        IWDG_SetPrescaler(IWDG_Prescaler_64);                   \
        IWDG_ReloadCounter();                                   \
    } while (0)

/**
 * @brief       Start the Watchdog timer
 * @return      None
 */
#define wdt_start()     do { IWDG_Enable(); } while (0)

/**
 * @brief       Config the Watchdog timer to reset upon count end
 * @return      None
 */
#define wdt_config()
/**
 * @brief       Set Watchdog Timer max count
 *
 * Select the max count for the timer, when this count reaches 0, the chip is reseted
 *
 * @param [in]  timeout Max count for Watchdog timer
 * @return      None
 */
#define wdt_set_timeout( timeout )                              \
    do {                                                        \
        uint32_t _reload = (uint32_t)(timeout) / WDT_CLK_COUNTS_PER_TICK; \
        if (_reload == 0u) { _reload = 1u; }                    \
        if (_reload > 0xFFFu) { _reload = 0xFFFu; }             \
        IWDG_WriteAccessCmd(IWDG_WriteAccess_Enable);           \
        IWDG_SetReload((uint16_t)_reload);                      \
        IWDG_ReloadCounter();                                   \
    } while (0)

/**
 * @brief       Reload the watchdog counter
 * @return      None
 */
#define wdt_feed()      do { IWDG_ReloadCounter(); } while (0)