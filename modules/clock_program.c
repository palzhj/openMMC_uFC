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
 *   @license GPL-3.0+ <http://spdx.org/licenses/GPL-3.0+>
 */

/**
 * @file   clock_program.c
 *
 * @brief  Program the uFC Si5345 clock generator from the ClockBuilder table
 *
 * The register list in <board>/si5345_regs.h is generated from
 * uFCv2/template/CLK/Si5345-RevD-Registers.txt:
 *
 *   preamble (3 writes) -> 300 ms calibration delay -> 518 registers
 *   -> postamble (5 writes)
 *
 * Commands (NetFn 0x32):
 *   0x16 start programming
 *   0x17 status: [status, fail_addr_hi, fail_addr_lo]
 *
 * @ingroup CLOCK_PROGRAM
 */

#include "FreeRTOS.h"
#include "task.h"
#include "ipmi.h"
#include "i2c.h"
#include "i2c_mapping.h"
#include "port.h"
#include "clock_program.h"
#if defined(SI5345_HAVE_REGS_H)
#include "si5345_regs.h"   /* board-generated built-in table (optional) */
#endif

#define CLOCK_PROG_CHIP     CHIP_ID_SI5345_0
#define CLOCK_PROG_TIMEOUT  pdMS_TO_TICKS(200)

static volatile uint8_t clock_prog_status = CLOCK_PROG_IDLE;
static volatile uint16_t clock_prog_fail;

static uint8_t si5345_current_page = 0xFF;
static uint8_t si5345_chip = CLOCK_PROG_CHIP;

/* The Si5345 uses 8-bit register addresses with a page register at 0x01:
 * the 16-bit ClockBuilder address is (page << 8) | register. */
static uint8_t batch_on;
static uint8_t batch_addr, batch_iface;

/* Hold the clock I2C bus for a whole register table: the per-register
 * take/give is what makes a 526-register table slow when the sensor task
 * polls the same interface. */
int clock_program_batch_begin(void)
{
    if (batch_on) {
        return 1;
    }
    if (!i2c_take_by_chipid(si5345_chip, &batch_addr, &batch_iface, CLOCK_PROG_TIMEOUT)) {
        return 0;
    }
    si5345_current_page = 0xFF;
    batch_on = 1;
    return 1;
}

void clock_program_batch_end(void)
{
    if (batch_on) {
        i2c_give(batch_iface);
        batch_on = 0;
    }
}

static uint8_t si5345_write8(uint8_t reg, uint8_t data)
{
    uint8_t i2c_addr, iface;
    uint8_t buf[2] = { reg, data };

    if (batch_on) {
        return xI2CMasterWrite(batch_iface, batch_addr, buf, 2) == 2;
    }

    /* The first transaction after the mux selects channel 2 can NACK
     * (TCA9548A power-up behaviour), so retry like the EEPROM driver. */
    for (uint8_t attempt = 0; attempt < 10; attempt++) {
        if (i2c_take_by_chipid(si5345_chip, &i2c_addr, &iface, CLOCK_PROG_TIMEOUT)) {
            uint8_t written = xI2CMasterWrite(iface, i2c_addr, buf, 2);
            i2c_give(iface);
            if (written == 2) {
                return 1;
            }
            vI2CBusRecover(iface);
        }
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    return 0;
}

static uint8_t si5345_read8(uint8_t reg, uint8_t *data)
{
    uint8_t i2c_addr, iface;
    uint8_t rx = 0;

    for (uint8_t attempt = 0; attempt < 10; attempt++) {
        if (i2c_take_by_chipid(si5345_chip, &i2c_addr, &iface, CLOCK_PROG_TIMEOUT)) {
            rx = xI2CMasterWriteRead(iface, i2c_addr, &reg, 1, data, 1);
            i2c_give(iface);
            if (rx == 1) {
                return 1;
            }
            vI2CBusRecover(iface);
        }
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    return 0;
}

static uint8_t si5345_write(uint16_t addr, uint8_t data)
{
    uint8_t page = (uint8_t)(addr >> 8);
    uint8_t reg = (uint8_t)addr;

    if (page != si5345_current_page) {
        if (!si5345_write8(0x01, page)) {
            return 0;
        }
        si5345_current_page = page;
    }
    return si5345_write8(reg, data);
}

void clock_program_select_chip(uint8_t chip_id)
{
    if (chip_id != si5345_chip) {
        si5345_current_page = 0xFF;
        si5345_chip = chip_id;
    }
}

uint8_t clock_program_write_entry(uint16_t addr, uint8_t data)
{
    return si5345_write(addr, data);
}

#if defined(SI5345_HAVE_REGS_H)
static uint8_t si5345_write_table(const si5345_reg_t *regs, uint16_t count)
{
    for (uint16_t i = 0; i < count; i++) {
        if (!si5345_write(regs[i].addr, regs[i].data)) {
            clock_prog_fail = regs[i].addr;
            return 0;
        }
    }
    return 1;
}
#endif

static uint8_t clock_program_run(void)
{
#ifdef SI5345_HAVE_REGS_H
    clock_prog_status = CLOCK_PROG_RUNNING;
    clock_prog_fail = 0;

    if (!si5345_write_table(si5345_preamble, SI5345_PREAMBLE_LEN)) {
        clock_prog_status = CLOCK_PROG_ERROR;
        return 0;
    }

    /* Si5345 calibration after the preamble (see ClockBuilder script) */
    vTaskDelay(pdMS_TO_TICKS(SI5345_DELAY_MS));

    if (!si5345_write_table(si5345_body, SI5345_BODY_LEN) ||
        !si5345_write_table(si5345_postamble, SI5345_POSTAMBLE_LEN)) {
        clock_prog_status = CLOCK_PROG_ERROR;
        return 0;
    }

    clock_prog_status = CLOCK_PROG_OK;
    return 1;
#else
    /* no compiled-in table on this board: use the file-based write */
    clock_prog_status = CLOCK_PROG_ERROR;
    return 0;
#endif
}

IPMI_HANDLER(ipmi_clock_program_start, NETFN_CUSTOM, IPMI_CUSTOM_CMD_CLOCK_PROGRAM, ipmi_msg *req, ipmi_msg *rsp)
{
    (void)req;
    if (clock_prog_status == CLOCK_PROG_RUNNING) {
        rsp->completion_code = IPMI_CC_NODE_BUSY;
        return;
    }
    /* 526 I2C writes + 300 ms calibration: ~0.5 s in the IPMI task */
    clock_program_run();
    rsp->completion_code = (clock_prog_status == CLOCK_PROG_OK) ? IPMI_CC_OK
                                                                : IPMI_CC_UNSPECIFIED_ERROR;
}

IPMI_HANDLER(ipmi_clock_program_status, NETFN_CUSTOM, IPMI_CUSTOM_CMD_CLOCK_STATUS, ipmi_msg *req, ipmi_msg *rsp)
{
    (void)req;
    rsp->data[0] = clock_prog_status;
    rsp->data[1] = (uint8_t)(clock_prog_fail >> 8);
    rsp->data[2] = (uint8_t)clock_prog_fail;
    rsp->data_len = 3;
    rsp->completion_code = IPMI_CC_OK;
}
