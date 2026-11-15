/*
 * openMMC -- u4FCP-v2 AMC-side RTM identification
 *
 * The AMC publishes the FMC2/FMC3 monitor sensors of the uRTM v2.3 (the
 * UCD90124A at 0x5B).  Those exist only on that board - another RTM (the RTM
 * Carrier for LattePanda Mu carries its own MMC and no UCD) - so the FRU at
 * 0x55 is what tells the two apart.
 *
 * This file only reads and classifies the FRU: one attempt, no caching and no
 * timing.  The RTM manager task in rtm.c decides when the module may be
 * addressed (after its 12 V came up and settled) and how many attempts are
 * worth making, and it caches the answer.
 *
 * @license GPL-3.0+ <http://spdx.org/licenses/GPL-3.0+>
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

#include "port.h"
#include "i2c.h"
#include "i2c_mapping.h"
#include "rtm_fru.h"

/* 24C512 on the RTM side: 2-byte word address, 0x55 */
#define RTM_FRU_ADDR            0x55
#define RTM_FRU_TIMEOUT_TICKS   20

/* common header (8) + board area + product area of the 512-byte image */
#define RTM_FRU_IMAGE_LEN       128

/* product-area field numbers (IPMI FRU spec) */
#define FRU_FIELD_PRODUCT_NAME  1
#define FRU_FIELD_VERSION       3

#define FRU_FIELD_TYPE_ASCII    0x03
#define FRU_FIELD_END           0xC1

/* One EEPROM read at offset 0; the word address is 2 bytes (24C512). */
static bool rtm_fru_read_at(uint8_t i2c_interface, uint8_t *buf, size_t len)
{
    uint8_t word_addr[2] = { 0x00, 0x00 };

    return xI2CMasterWriteRead(i2c_interface, RTM_FRU_ADDR, word_addr, 2,
                               buf, len) == (int)len;
}

static bool rtm_fru_read(uint8_t *buf, size_t len)
{
    uint8_t i2c_interface = 0;
    uint8_t i2c_address = 0;
    bool ok = false;

    /* takes the bus and selects 0x70 ch5 (the RTM bus) */
    if (!i2c_take_by_chipid(CHIP_ID_RTM_UCD, &i2c_address, &i2c_interface,
                            RTM_FRU_TIMEOUT_TICKS)) {
        return false;
    }

    /* uRTM: the FRU sits behind the module's own switch, 0x71 ch7 */
    if (i2c_rtm_mux_select(i2c_interface)) {
        ok = rtm_fru_read_at(i2c_interface, buf, len);
    }

    /* no 0x71 on the bus: the FRU is directly on the RTM bus */
    if (!ok) {
        ok = rtm_fru_read_at(i2c_interface, buf, len);
    }

    i2c_give(i2c_interface);
    return ok;
}

/* Copy one 8-bit-ASCII field of a FRU area into out (NUL terminated). */
static bool rtm_fru_field(const uint8_t *image, size_t image_len, size_t area,
                          uint8_t field_id, char *out, size_t out_len)
{
    size_t pos;
    uint8_t field;

    if (area == 0 || area + 3 > image_len || out_len == 0) {
        return false;
    }
    if (image[area] != 0x01) {          /* area format version */
        return false;
    }

    pos = area + 3;                     /* skip format, length, language */
    for (field = 0; pos < image_len; field++) {
        uint8_t type_len = image[pos++];
        uint8_t type;
        uint8_t len;

        if (type_len == FRU_FIELD_END) {
            return false;
        }
        type = (uint8_t)(type_len >> 6);
        len = (uint8_t)(type_len & 0x3F);
        if (pos + len > image_len) {
            return false;
        }
        if (type == FRU_FIELD_TYPE_ASCII && field == field_id) {
            size_t n = len;
            while (n > 0 && (image[pos + n - 1] == '\0' || image[pos + n - 1] == ' ')) {
                n--;
            }
            if (n >= out_len) {
                n = out_len - 1;
            }
            memcpy(out, &image[pos], n);
            out[n] = '\0';
            return true;
        }
        pos += len;
    }
    return false;
}

rtm_fru_type_t rtm_fru_probe(void)
{
    uint8_t image[RTM_FRU_IMAGE_LEN];
    char product[16] = { 0 };
    char version[8] = { 0 };
    size_t product_area;

    if (!rtm_fru_read(image, sizeof(image))) {
        return RTM_FRU_UNKNOWN;
    }
    if (image[0] != 0x01) {             /* common header format */
        return RTM_FRU_OTHER;
    }
    {
        uint8_t sum = 0;
        for (uint8_t i = 0; i < 8; i++) {
            sum = (uint8_t)(sum + image[i]);
        }
        if (sum != 0) {                 /* header checksum (mod 256) */
            return RTM_FRU_OTHER;
        }
    }

    product_area = (size_t)image[4] * 8;
    if (!rtm_fru_field(image, sizeof(image), product_area,
                       FRU_FIELD_PRODUCT_NAME, product, sizeof(product))) {
        return RTM_FRU_OTHER;
    }
    if (strncmp(product, "uRTM", 4) != 0) {
        return RTM_FRU_OTHER;
    }
    if (!rtm_fru_field(image, sizeof(image), product_area,
                       FRU_FIELD_VERSION, version, sizeof(version))) {
        return RTM_FRU_OTHER;
    }
    return (strncmp(version, "2.3", 3) == 0) ? RTM_FRU_URTM_V23
                                             : RTM_FRU_OTHER;
}
