/*
 *   openMMC -- Open Source modular IPM Controller firmware
 *
 *   Copyright (C) 2026  IHEP
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
 * @file stm32_hpm.c
 * @brief HPM.1 self-update for STM32F1 (MMC and bootloader components)
 *
 * The uploaded image is buffered in 256-byte pages and written to the firmware
 * update region (__FWUpdateFlash_start).  On activate, an fw_info record is
 * written at __FWInfo_addr and the MMC is reset after a short delay; newboot
 * then copies the staging area into the application or bootloader region.
 */

#include "FreeRTOS.h"
#include "task.h"
#include <stdbool.h>
#include "string.h"

#include "port.h"
#include "ipmi.h"
#include "hpm.h"

/* Absolute symbols defined in the linker script */
extern const uint32_t __AppFlash_start;
extern const uint32_t __AppFlash_end;
extern const uint32_t __BootFlash_start;
extern const uint32_t __BootFlash_end;
extern const uint32_t __FWUpdateFlash_start;
extern const uint32_t __FWUpdateFlash_end;

/* Must match the layout read by the STM32 newboot (magic at offset 4) */
typedef struct
{
    uint8_t version[3];
    uint8_t fw_type;
    uint32_t magic;
    uint8_t reserved[248];
} fw_info;

extern const fw_info __FWInfo_addr;

#define STM32_FLASH_PAGE_SIZE   0x400u
#define HPM_PAGE_SIZE           256u
#define HPM_FW_MAGIC            0xAAAAAAAAu

static uint32_t hpm_page_addr;      /* offset from the staging start */
static uint32_t hpm_image_size;
static uint32_t hpm_page_index;     /* bytes currently buffered */
static uint32_t hpm_page[HPM_PAGE_SIZE / 4];
static bool hpm_finish_ok;

static inline uint32_t staging_start(void)
{
    return (uint32_t)&__FWUpdateFlash_start;
}

static inline uint32_t fw_info_addr(void)
{
    return (uint32_t)&__FWInfo_addr;
}

/* Erase only the pages of the staging area that are not blank */
static uint8_t hpm_erase_staging(void)
{
    uint8_t ret = IPMI_CC_OK;
    const uint32_t start = staging_start();
    const uint32_t end = (uint32_t)&__FWUpdateFlash_end;

    FLASH_Unlock();
    FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_PGERR | FLASH_FLAG_WRPRTERR);

    for (uint32_t page = start; page <= end; page += STM32_FLASH_PAGE_SIZE) {
        bool dirty = false;

        for (uint32_t addr = page; addr < (page + STM32_FLASH_PAGE_SIZE); addr += 4) {
            if (*(volatile uint32_t *)addr != 0xFFFFFFFFu) {
                dirty = true;
                break;
            }
        }

        if (dirty) {
            portDISABLE_INTERRUPTS();
            FLASH_Status status = FLASH_ErasePage(page);
            portENABLE_INTERRUPTS();

            if (status != FLASH_COMPLETE) {
                ret = IPMI_CC_UNSPECIFIED_ERROR;
                break;
            }
        }
    }

    FLASH_Lock();
    return ret;
}

/* Program `size` bytes at an absolute flash address */
static uint8_t program_page_abs(uint32_t addr, const uint32_t *data, uint32_t size)
{
    uint8_t ret = IPMI_CC_OK;

    if ((size % 4u) != 0u || (addr & 3u) != 0u) {
        return IPMI_CC_PARAM_OUT_OF_RANGE;
    }

    FLASH_Unlock();
    FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_PGERR | FLASH_FLAG_WRPRTERR);

    portDISABLE_INTERRUPTS();
    for (uint32_t i = 0; i < (size / 4u); i++) {
        if (FLASH_ProgramWord(addr + (i * 4u), data[i]) != FLASH_COMPLETE) {
            ret = IPMI_CC_UNSPECIFIED_ERROR;
            break;
        }
    }
    portENABLE_INTERRUPTS();

    FLASH_Lock();
    return ret;
}

/* Program `size` bytes at `staging + offset` */
static uint8_t program_page(uint32_t offset, const uint32_t *data, uint32_t size)
{
    return program_page_abs(staging_start() + offset, data, size);
}

static void hpm_reset_task(void *arg)
{
    (void)arg;

    /* Give the IPMI response time to leave the MMC */
    vTaskDelay(pdMS_TO_TICKS(500));
    NVIC_SystemReset();

    for (;;) {
        /* not reached */
    }
}

static void hpm_schedule_reset(void)
{
    if (xTaskCreate(hpm_reset_task, "HPM reset", 128, NULL, tskIDLE_PRIORITY + 1, NULL) != pdPASS) {
        NVIC_SystemReset();
    }
}

static uint8_t hpm_prepare_comp(void)
{
    hpm_finish_ok = false;
    hpm_image_size = 0;
    hpm_page_index = 0;
    hpm_page_addr = 0;

    memset(hpm_page, 0xFF, sizeof(hpm_page));

    return hpm_erase_staging();
}

static uint8_t hpm_upload_block(uint8_t *block, uint16_t size)
{
    const uint32_t available = HPM_PAGE_SIZE - hpm_page_index;

    if (size <= available) {
        memcpy(&((uint8_t *)hpm_page)[hpm_page_index], block, size);
        hpm_page_index += size;
        return IPMI_CC_OK;
    }

    /* Fill the current page and flush it */
    memcpy(&((uint8_t *)hpm_page)[hpm_page_index], block, available);
    if (program_page(hpm_page_addr, hpm_page, HPM_PAGE_SIZE) != IPMI_CC_OK) {
        return IPMI_CC_UNSPECIFIED_ERROR;
    }

    hpm_page_addr += HPM_PAGE_SIZE;
    hpm_image_size += HPM_PAGE_SIZE;
    hpm_page_index = 0;
    memset(hpm_page, 0xFF, sizeof(hpm_page));

    /* Buffer the remainder */
    uint32_t remaining = size - available;
    if (remaining > HPM_PAGE_SIZE) {
        return IPMI_CC_PARAM_OUT_OF_RANGE;
    }
    memcpy(&((uint8_t *)hpm_page)[0], &block[available], remaining);
    hpm_page_index = remaining;

    return IPMI_CC_COMMAND_IN_PROGRESS;
}

static uint8_t hpm_finish_upload(uint32_t image_size)
{
    /* The staged image must fit the firmware update region */
    if (image_size == 0u || image_size > ((uint32_t)&__FWUpdateFlash_end - staging_start() + 1u)) {
        return 0x81;
    }

    if (hpm_page_index != 0u) {
        if (program_page(hpm_page_addr, hpm_page, HPM_PAGE_SIZE) != IPMI_CC_OK) {
            return IPMI_CC_UNSPECIFIED_ERROR;
        }
        hpm_image_size += hpm_page_index;
        hpm_page_index = 0;
        hpm_page_addr = 0;
    }

    if (hpm_image_size != image_size) {
        return 0x81;
    }

    hpm_finish_ok = true;
    return IPMI_CC_OK;
}

static uint8_t hpm_activate_firmware(enum fw_type type)
{
    if (!hpm_finish_ok) {
        /* Some HPM clients do not send 'finish firmware upload'. Accept the
         * activation when the staging region actually contains an image
         * (a valid vector table starts with a stack pointer in SRAM). */
        const uint32_t *img = (const uint32_t *)staging_start();
        if (img[0] == 0xFFFFFFFFu || img[0] == 0x00000000u) {
            return IPMI_CC_UNSPECIFIED_ERROR;
        }
    }

    fw_info header;
    memset(&header, 0xFF, sizeof(header));

    header.magic = HPM_FW_MAGIC;
    header.fw_type = (uint8_t)type;
    header.version[0] = 1;
    header.version[1] = 4;
    header.version[2] = 1;

    /* The fw_info page may contain stale data (it now lives in the former
     * 8 KB gap), so erase it before programming the record. */
    {
        uint32_t page = fw_info_addr() & ~(STM32_FLASH_PAGE_SIZE - 1u);
        FLASH_Unlock();
        FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_PGERR | FLASH_FLAG_WRPRTERR);
        portDISABLE_INTERRUPTS();
        FLASH_Status status = FLASH_ErasePage(page);
        portENABLE_INTERRUPTS();
        FLASH_Lock();
        if (status != FLASH_COMPLETE) {
            return IPMI_CC_UNSPECIFIED_ERROR;
        }
    }

    if (program_page_abs(fw_info_addr(), (const uint32_t *)&header, sizeof(header)) != IPMI_CC_OK) {
        return IPMI_CC_UNSPECIFIED_ERROR;
    }

    hpm_schedule_reset();
    return IPMI_CC_OK;
}

uint8_t ipmc_hpm_prepare_comp(void)
{
    return hpm_prepare_comp();
}

uint8_t ipmc_hpm_upload_block(uint8_t *block, uint16_t size)
{
    return hpm_upload_block(block, size);
}

uint8_t ipmc_hpm_finish_upload(uint32_t image_size)
{
    return hpm_finish_upload(image_size);
}

uint8_t ipmc_hpm_get_upgrade_status(void)
{
    return IPMI_CC_OK;
}

uint8_t ipmc_hpm_activate_firmware(void)
{
    return hpm_activate_firmware(APPLICATION);
}

uint8_t bootloader_hpm_prepare_comp(void)
{
    return hpm_prepare_comp();
}

uint8_t bootloader_hpm_upload_block(uint8_t *block, uint16_t size)
{
    return hpm_upload_block(block, size);
}

uint8_t bootloader_hpm_finish_upload(uint32_t image_size)
{
    return hpm_finish_upload(image_size);
}

uint8_t bootloader_hpm_get_upgrade_status(void)
{
    return IPMI_CC_OK;
}

uint8_t bootloader_hpm_activate_firmware(void)
{
    return hpm_activate_firmware(BOOTLOADER);
}
