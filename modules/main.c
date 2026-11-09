/*
 *   openMMC -- Open Source modular IPM Controller firmware
 *
 *   Copyright (C) 2015  Henrique Silva <henrique.silva@lnls.br>
 *   Copyright (C) 2015  Piotr Miedzik  <P.Miedzik@gsi.de>
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

/* Kernel includes. */
#include "FreeRTOS.h"

/* Project includes */
#include "GitSHA1.h"
#include "port.h"
#include "led.h"
#include "pin_cfg.h"
#include "ipmi.h"
#ifdef MODULE_PAYLOAD
#include "payload.h"
#endif
#include "i2c.h"
#if defined(USE_RAM_DIAG)
extern uint32_t __StackLimit;
extern uint32_t _estack;
#endif
#ifdef MODULE_CLOCK_STORE
#include "clock_store.h"
#endif
#include "i2c_mapping.h"
#include "fru.h"
#include "scansta1101.h"
#include "fpga_spi.h"
#include "watchdog.h"
#include "uart_debug.h"
#ifdef MODULE_RTM
#include "rtm.h"
#endif
#ifdef MODULE_BOARD_CONFIG
#include "board_config.h"
#endif

/*-----------------------------------------------------------*/


#ifdef UCD_PROBE
/* Temporary diagnostic (not for production): read UCD90320 rail voltages and
 * currents through the FRU/PMBus segment (TCA9548A channel 0, PMBus 0x11). */
void ucd_probe( void )
{
    static const char *rail_names[19] = {
        "3V3", "1V0", "VADJ0", "VADJ1", "5V0", "2V5", "1V0A", "1V2A",
        "FMC0_12V0", "FMC1_12V0", "FMC0_3V3", "FMC1_3V3", "FMC0_VADJ",
        "FMC1_VADJ", "DESKTOP_3V3", "DDR", "12V0", "CLK_3V3", "TEMP_S7"
    };
    uint8_t addr, iface, page, reg, buf[2];
    int vexp = 0;

    if ( !i2c_take_by_chipid( CHIP_ID_EEPROM, &addr, &iface, pdMS_TO_TICKS( 2000 ) ) )
    {
        printf( "[PMBUS] could not take channel 0\n" );
        return;
    }

    reg = 0x20;
    {
        int rc = xI2CMasterWriteRead( iface, 0x11, &reg, 1, buf, 1 );
        printf( "[PMBUS] VOUT_MODE rc=%d val=0x%02x\n", rc, buf[0] );
        if ( rc == 1 )
        {
            vexp = ( buf[0] > 0x0F ) ? (int)( buf[0] - 0x20 ) : (int)buf[0];
        }
    }

    for ( page = 0; page < 19; page++ )
    {
        uint8_t wbuf[2] = { 0x00, page };
        int wrc, vrc, irc;
        uint16_t vraw = 0xFFFF, iraw = 0xFFFF;

        wrc = xI2CMasterWrite( iface, 0x11, wbuf, 2 );
        reg = 0x8B;
        vrc = xI2CMasterWriteRead( iface, 0x11, &reg, 1, buf, 2 );
        if ( vrc == 2 ) vraw = (uint16_t)( buf[0] | ( buf[1] << 8 ) );
        reg = 0x8C;
        irc = xI2CMasterWriteRead( iface, 0x11, &reg, 1, buf, 2 );
        if ( irc == 2 ) iraw = (uint16_t)( buf[0] | ( buf[1] << 8 ) );

        printf( "[PMBUS] p%-2d %-12s wr=%d vr=%d v=0x%04x ir=%d i=0x%04x\n",
                page, rail_names[page], wrc, vrc, vraw, irc, iraw );
    }

    i2c_give( iface );
}
#endif /* UCD_PROBE */

#if defined(USE_RAM_DIAG)
static void ram_paint_stack(void)
{
    uint32_t sp;
    __asm volatile ("mov %0, sp" : "=r" (sp));
    uint32_t *top = (uint32_t *)((sp - 512u) & ~3u);   /* keep the live frames */
    for (uint32_t *p = &__StackLimit; p < top; p++) {
        *p = 0xA5A5A5A5u;
    }
}

static void ram_report(const char *where)
{
    uint32_t *p = &__StackLimit;
    while (p < &_estack && *p == 0xA5A5A5A5u) {
        p++;
    }
    uint32_t unused = (uint32_t)(&_estack - p) * 4;
    printf("[RAM] %-12s heap_free=%u stack_unused=%u\n",
           where, (unsigned)xPortGetFreeHeapSize(), (unsigned)unused);
}
#else
static void ram_paint_stack(void) {}
static void ram_report(const char *where) { (void)where; }
#endif

int main( void )
{
    pin_init();
    ram_paint_stack();

#ifdef MODULE_BOARD_CONFIG
    board_init();
#endif

#ifdef MODULE_UART_DEBUG
    uart_init( UART_DEBUG );
#endif

    printf("openMMC Starting!\n");
    printf("Build date: %s %s\n", __DATE__, __TIME__);
    printf("Version: %s\n", g_GIT_TAG);
    printf("SHA1: %s\n", g_GIT_SHA1);

#ifdef MODULE_WATCHDOG
    watchdog_init();
#endif

    LED_init();

    i2c_init();


    ipmb_addr = get_ipmb_addr( );

    ram_report("start");
#ifdef MODULE_FRU
    fru_init(FRU_AMC);
#endif
    ram_report("fru");

#ifdef MODULE_SDR
    sdr_init();
#endif
    ram_report("sdr");
#ifdef MODULE_SENSORS
    sensor_init();
#endif
    ram_report("sensors");
#ifdef MODULE_PAYLOAD
    payload_init();
#endif
    ram_report("payload");
#ifdef MODULE_SCANSTA1101
    scansta1101_init();
#endif
#ifdef MODULE_FPGA_SPI
    fpga_spi_init();
#endif
#ifdef MODULE_RTM
    rtm_manage_init();
#endif
    /*  Init IPMI interface */
    /* NOTE: ipmb_init() is called inside this function */
    ipmi_init();
    ram_report("ipmi");

#ifdef MODULE_BOARD_CONFIG
    board_config();
#endif
    ram_report("board");

    /* Start the tasks running. */
    vTaskStartScheduler();

    /* If all is well we will never reach here as the scheduler will now be
       running.  If we do reach here then it is likely that there was insufficient
       heap available for the idle task to be created. */
    for( ;; );

}


/* FreeRTOS debug functions */
#if (configUSE_MALLOC_FAILED_HOOK == 1)
void vApplicationMallocFailedHook( void ) {}
#endif

#if (configCHECK_FOR_STACK_OVERFLOW == 1)
void vApplicationStackOverflowHook ( TaskHandle_t pxTask, signed char * pcTaskName)
{
    (void) pxTask;
    (void) pcTaskName;
    taskDISABLE_INTERRUPTS();
    /* Place a breakpoint here, so we know when there's a stack overflow */
    for ( ; ; ) {
        uxTaskGetStackHighWaterMark(pxTask);
    }
}
#endif

