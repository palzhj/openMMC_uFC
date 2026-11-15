/*
 * openMMC -- u4FCP-v2 AMC-side RTM identification
 *
 * The AMC publishes the FMC2/FMC3 monitor sensors of the uRTM v2.3 (the
 * UCD90124A at 0x5B).  Those exist only on that board: another RTM - the RTM
 * Carrier for LattePanda Mu carries its own MMC and no UCD - and a blank or
 * unreadable FRU must therefore not get the nine SDR records.
 *
 * What the AMC can look at is the RTM's FRU EEPROM at 0x55.  Its product area
 * has to say "uRTM" / "2.3".  Two paths are tried:
 *
 *   uRTM   : I2C2 -> TCA9548A 0x70 ch5 -> TCA9548A 0x71 ch7 -> 0x55
 *   others : I2C2 -> TCA9548A 0x70 ch5 ----------------------> 0x55
 *
 * The uRTM keeps its EEPROM behind the 12 V it gets from the AMC, so a failed
 * probe is not final: it is repeated every RTM_FRU_RECHECK_MS until the
 * module answers, and the answer is cached once it is positive (a module that
 * identified itself does not stop being a uRTM while it is plugged).
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

/* how often a negative answer is retried (the uRTM FRU needs its 12 V);
 * keep it short: the SDR records should follow the module power-up closely */
#define RTM_FRU_RECHECK_MS      2000

/* product-area field numbers (IPMI FRU spec) */
#define FRU_FIELD_PRODUCT_NAME  1
#define FRU_FIELD_VERSION       3

#define FRU_FIELD_TYPE_ASCII    0x03
#define FRU_FIELD_END           0xC1

static int8_t rtm_fru_urtm = -1;        /* -1 unknown, else rtm_fru_type_t */
static TickType_t rtm_fru_next_probe;

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

/* uRTM v2.3 -> RTM_FRU_URTM_V23, a readable other FRU -> RTM_FRU_OTHER,
 * nothing readable -> RTM_FRU_UNKNOWN */
static rtm_fru_type_t rtm_fru_identify(void)
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

rtm_fru_type_t rtm_fru_type(void)
{
    TickType_t now;

    /* Only a positive answer is cached: "nothing answered" (UNKNOWN) has to be
     * retried, the uRTM only exposes its FRU once its 12 V is on. */
    if (rtm_fru_urtm > 0) {
        return (rtm_fru_type_t)rtm_fru_urtm;
    }

    now = xTaskGetTickCount();
    if (rtm_fru_next_probe != 0 && (int32_t)(now - rtm_fru_next_probe) < 0) {
        return RTM_FRU_UNKNOWN;
    }
    rtm_fru_next_probe = now + pdMS_TO_TICKS(RTM_FRU_RECHECK_MS);

    rtm_fru_urtm = (int8_t)rtm_fru_identify();
    return (rtm_fru_type_t)rtm_fru_urtm;
}

bool rtm_fru_is_urtm_v23(void)
{
    return rtm_fru_type() == RTM_FRU_URTM_V23;
}

void rtm_fru_invalidate(void)
{
    rtm_fru_urtm = -1;
    rtm_fru_next_probe = 0;
}
