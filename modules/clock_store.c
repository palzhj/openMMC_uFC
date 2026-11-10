/*
 *   openMMC -- Open Source modular IPM Controller firmware
 *
 *   Clock configuration store (FRU-format slots):
 *     0x18 status   -> [valid, slot, len_lo, len_hi, crc0..3]
 *     0x19 apply    -> [result]
 *     0x1A begin    -> [slot, len_lo, len_hi, crc0..3]
 *     0x1B block    -> [off_lo, off_hi, data...]
 *     0x1C finish   -> [crc0..3]
 *
 *   Two storage backends:
 *     - flash (uFC): 1 KB pointer page + 9 KB slot A/B, linker symbols
 *       __ClockPointer_addr / __ClockSlotA_addr / __ClockSlotB_addr;
 *     - EEPROM (u4FCP, CAT24C512): FRU 0x0000..0x01FF, pointer 0x0200,
 *       slot A 0x0300, slot B 0x1B00, 6 KB each (CLOCK_STORE_EEPROM).
 */

#include <string.h>

#include "port.h"
#include "clock_store.h"
#include "utils.h"
#include "clock_program.h"
#include "ipmi.h"
#include "i2c.h"
#include "i2c_mapping.h"

#if defined(CLOCK_STORE_EEPROM)
#include "eeprom_24xx512.h"
#endif

#define CLOCK_PTR_MAGIC   0x50434655u   /* "UFCP" */
#define CLOCK_SLOT_MAGIC  0x43434655u   /* 'UFCC' */
/* the second pointer copy must stay inside the pointer page (0x200..0x2FF):
 * copy #1 at 0x0200, copy #2 at 0x0280, slot A starts at 0x0300 */
#define CLOCK_PTR_COPY2   128u

#if defined(CLOCK_STORE_EEPROM)
/* ---- CAT24C512 (64 KB) layout ------------------------------------------
 *   0x0000 .. 0x01FF   FRU image (512 B, FRU_MAX_SIZE)
 *   0x0200 .. 0x02FF   clock pointer page (256 B)
 *                        copy #1 at 0x0200, copy #2 at 0x0280
 *   0x0300 .. 0x1AFF   clock slot A (6 KB)
 *   0x1B00 .. 0x32FF   clock slot B (6 KB)
 *   0x3300 .. 0xFFFF   free (future use)
 * ---------------------------------------------------------------------- */
#define CLOCK_SLOT_SIZE   6144u
#define CLOCK_PAGE_SIZE   128u
#define CLOCK_PTR_OFF     0x0200u
#define CLOCK_SLOT_A_OFF  0x0300u
#define CLOCK_SLOT_B_OFF  0x1B00u

#else
#define CLOCK_SLOT_SIZE   (9u * 1024u)
#define CLOCK_PAGE_SIZE   1024u
extern uint32_t __ClockPointer_addr;
extern uint32_t __ClockSlotA_addr;
extern uint32_t __ClockSlotB_addr;
#endif

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint32_t seq;
    uint8_t  slot;
    uint8_t  flags;        /* bit0 = valid */
    uint16_t len;
    uint32_t crc;          /* CRC32 of the slot payload */
    uint32_t hdr_crc;      /* CRC32 of the preceding 16 bytes */
} clock_ptr_t;

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint16_t version;
    uint16_t hdr_len;
    uint32_t payload_len;
    uint32_t crc;
    uint32_t seq;
    uint16_t flags;
    uint8_t  n_records;
    uint8_t  reserved;
    uint32_t config_id;
    uint32_t reserved2;
} clock_slot_hdr_t;

#if defined(CLOCK_STORE_EEPROM)
/* compile-time guard against the overlap that made slot CRCs fail: the
 * pointer copies must stay inside their page, before slot A */
_Static_assert(CLOCK_PTR_OFF + CLOCK_PTR_COPY2 + sizeof(clock_ptr_t) <= CLOCK_SLOT_A_OFF,
               "clock pointer copies overlap slot A");
_Static_assert(CLOCK_SLOT_A_OFF + CLOCK_SLOT_SIZE <= CLOCK_SLOT_B_OFF,
               "clock slot A overlaps slot B");
_Static_assert(CLOCK_SLOT_B_OFF + CLOCK_SLOT_SIZE <= 65536u,
               "clock slot B exceeds the EEPROM");
#endif

static struct {
    uint8_t active;
    uint8_t slot;
    uint16_t len;
    uint32_t crc;
    uint16_t written;
} wr;

static uint32_t crc32(const uint8_t *data, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t b = 0; b < 8; b++) {
            crc = (crc >> 1) ^ (0xEDB88320u & (~((crc & 1u)) + 1u));
        }
    }
    return ~crc;
}

/* ---- storage backend (region 0 = pointer, 1 = slot A, 2 = slot B) ------ */

#if defined(CLOCK_STORE_EEPROM)
static uint16_t region_off(uint8_t region)
{
    return (region == 0) ? CLOCK_PTR_OFF : (region == 1 ? CLOCK_SLOT_A_OFF : CLOCK_SLOT_B_OFF);
}

static int store_read(uint8_t region, uint16_t off, uint8_t *buf, uint16_t len)
{
    size_t n = eeprom_24xx512_read(CHIP_ID_EEPROM, (uint16_t)(region_off(region) + off),
                                   buf, len, pdMS_TO_TICKS(200));
    return n == len;
}

static int store_write(uint8_t region, uint16_t off, const uint8_t *buf, uint16_t len)
{
    size_t n = eeprom_24xx512_write(CHIP_ID_EEPROM, (uint16_t)(region_off(region) + off),
                                    (uint8_t *)buf, len, pdMS_TO_TICKS(200));
    return n == len;
}

static int store_erase(uint8_t region, uint16_t len)
{
    (void)region;
    (void)len;
    return 1;   /* EEPROM is overwritten, no erase needed */
}
#else
static uint32_t region_base(uint8_t region)
{
    return (region == 0) ? (uint32_t)&__ClockPointer_addr :
           (region == 1 ? (uint32_t)&__ClockSlotA_addr : (uint32_t)&__ClockSlotB_addr);
}

static int store_read(uint8_t region, uint16_t off, uint8_t *buf, uint16_t len)
{
    memcpy(buf, (const void *)(region_base(region) + off), len);
    return 1;
}

static int store_write(uint8_t region, uint16_t off, const uint8_t *buf, uint16_t len)
{
    uint32_t addr = region_base(region) + off;
    FLASH_Unlock();
    for (uint16_t i = 0; i + 1 < len; i += 2) {
        uint16_t hw = buf[i] | ((uint16_t)buf[i + 1] << 8);
        if (FLASH_ProgramHalfWord(addr + i, hw) != FLASH_COMPLETE) {
            FLASH_Lock();
            return 0;
        }
    }
    FLASH_Lock();
    return 1;
}

static int store_erase(uint8_t region, uint16_t len)
{
    uint32_t base = region_base(region);
    FLASH_Unlock();
    for (uint32_t a = base; a < base + len; a += CLOCK_PAGE_SIZE) {
        if (FLASH_ErasePage(a) != FLASH_COMPLETE) {
            FLASH_Lock();
            return 0;
        }
    }
    FLASH_Lock();
    return 1;
}
#endif

/* CRC over `len` bytes stored in a region, read in small chunks. */
static uint32_t store_crc(uint8_t region, uint16_t off, uint32_t len)
{
    uint8_t chunk[256];
    uint32_t crc = 0xFFFFFFFFu;
    while (len) {
        uint16_t n = (len > sizeof(chunk)) ? sizeof(chunk) : (uint16_t)len;
        if (!store_read(region, off, chunk, n)) {
            return 0;
        }
        for (uint16_t i = 0; i < n; i++) {
            crc ^= chunk[i];
            for (uint8_t b = 0; b < 8; b++) {
                crc = (crc >> 1) ^ (0xEDB88320u & (~((crc & 1u)) + 1u));
            }
        }
        off += n;
        len -= n;
    }
    return ~crc;
}

/* ---- pointer record ---------------------------------------------------- */

static int ptr_load(clock_ptr_t *best, int *found)
{
    int have = 0;
    clock_ptr_t cand;
    *found = 0;
    for (int copy = 0; copy < 2; copy++) {
        if (!store_read(0, (uint16_t)(copy * CLOCK_PTR_COPY2), (uint8_t *)&cand, sizeof(cand))) {
            continue;
        }
        if (cand.magic != CLOCK_PTR_MAGIC) {
            continue;
        }
        uint32_t hcrc = crc32((const uint8_t *)&cand, 16);
        if (hcrc != cand.hdr_crc || !(cand.flags & 1u) || cand.len > CLOCK_SLOT_SIZE) {
            continue;
        }
        if (!have || cand.seq > best->seq) {
            *best = cand;
            have = 1;
        }
    }
    *found = have;
    return have;
}

static int ptr_write(uint8_t slot, uint16_t len, uint32_t crc)
{
    clock_ptr_t cur;
    int found;
    clock_ptr_t next;

    ptr_load(&cur, &found);
    memset(&next, 0xFF, sizeof(next));
    next.magic = CLOCK_PTR_MAGIC;
    next.seq = found ? cur.seq + 1u : 1u;
    next.slot = slot;
    next.flags = 1u;
    next.len = len;
    next.crc = crc;
    next.hdr_crc = crc32((const uint8_t *)&next, 16);

    if (!store_erase(0, CLOCK_PTR_COPY2)) {
        return 0;
    }
    for (int copy = 0; copy < 2; copy++) {
        if (!store_write(0, (uint16_t)(copy * CLOCK_PTR_COPY2), (const uint8_t *)&next, sizeof(next))) {
            return 0;
        }
    }
    return 1;
}

/* ---- slot validation / apply ------------------------------------------- */

/* Board hook: apply the 16-byte switch record. The default does nothing;
 * boards with a crosspoint provide a strong implementation (the uFC's
 * 8V54816A writer lives in idt_8v54816.c, the u4FCP's ADN4604 in payload.c). */
__attribute__((weak)) int clock_store_apply_switch(const uint8_t *matrix, uint8_t addr)
{
    (void)matrix;
    (void)addr;
    return 0;
}

/* Board hook: write one Si5345 table entry. addr is the I2C address from the
 * FRU record's device header (0x69 on the uFC), so boards with two PLLs can
 * pick the right chip. */
__attribute__((weak)) int clock_store_apply_si5345(uint8_t addr, uint16_t reg, uint8_t val)
{
    (void)addr;
    return clock_program_write_entry(reg, val);
}

/* Board hooks: called around one PLL register record, so that the bus can be
 * held for the whole chunk instead of taken for every register. */
__attribute__((weak)) int clock_store_apply_begin(uint8_t addr)
{
    (void)addr;
    return 1;
}

__attribute__((weak)) void clock_store_apply_end(void)
{
}

/* Parse the FRU multirecord file stored in a slot region and apply it. */
static int apply_fru_stream(uint8_t region, uint16_t fru_off, uint32_t len)
{
    uint8_t fru[8];
    uint8_t rec[4 + 255];
    uint16_t si_count = 0;
    uint16_t rec_count = 0;
    TickType_t t_apply0 = xTaskGetTickCount();

    if (len < 8 || !store_read(region, fru_off, fru, 8) || fru[0] != 0x01) {
        return 0;
    }
    uint32_t off = (uint32_t)fru[5] * 8u;

    while (off + 4u <= len) {
        if (!store_read(region, (uint16_t)(fru_off + off), rec, 4)) {
            return 0;
        }
        uint8_t rlen = rec[2];
        if (rec[0] != 0xC0 || off + 4u + rlen > len) {
            break;
        }
        if (!store_read(region, (uint16_t)(fru_off + off + 4u), rec + 4, rlen)) {
            return 0;
        }
        uint8_t chk = 0;
        /* the counter must hold 4 + rlen (up to 259): a uint8_t wraps and
         * hangs the apply on records of 252 bytes or more */
        for (uint16_t i = 0; i < 4u + rlen; i++) {
            chk += rec[i];
        }
        if (chk != 0) {
            return 0;
        }
        const uint8_t *data = rec + 4;
        uint8_t recid = data[3];
        rec_count++;
#ifdef MODULE_UART_DEBUG
        log_printk("clk", "rec off=%u rlen=%u chk=%s recid=0x%02x\n",
               (unsigned)off, rlen, (chk == 0) ? "ok" : "bad", recid);
#endif
        uint8_t plen = (uint8_t)(rlen - 6u);
        const uint8_t *payload = data + 5;
        if (plen >= 8u) {
            const uint8_t *p = payload + 8;
            uint8_t dlen = (uint8_t)(plen - 8u);
            uint8_t dev_addr = payload[4];
            if (recid == 0x40 && dlen == 16u) {
#ifdef MODULE_UART_DEBUG
                log_printk("clk", "switch record addr=0x%02x dlen=%u\n", dev_addr, dlen);
#endif
                if (!clock_store_apply_switch(p, dev_addr)) {
#ifdef MODULE_UART_DEBUG
                    log_printk("clk", "switch apply FAILED\n");
#endif
                    return 0;
                }
#ifdef MODULE_UART_DEBUG
                log_printk("clk", "switch apply OK\n");
#endif
            } else if ((recid & 0xF0) == 0x30 && dlen >= 3u) {
                if (!clock_store_apply_begin(dev_addr)) {
                    return 0;
                }
                for (uint8_t i = 0; i + 2u < dlen; i += 3u) {
                    uint16_t addr = ((uint16_t)p[i] << 8) | p[i + 1];
                    if (!clock_store_apply_si5345(dev_addr, addr, p[i + 2])) {
                        clock_store_apply_end();   /* never leak the bus */
                        return 0;
                    }
                    if (++si_count == 3u) {
                        vTaskDelay(pdMS_TO_TICKS(SI5345_DELAY_MS));
                    }
                }
                clock_store_apply_end();
            }
        }
        if (rec[1] & 0x80) {   /* end of list */
            break;
        }
        off += 4u + rlen;
    }
#ifdef MODULE_UART_DEBUG
    log_printk("clk", "apply done: %u records, %u registers, %u ms\n",
           (unsigned)rec_count, (unsigned)si_count,
           (unsigned)((xTaskGetTickCount() - t_apply0) * portTICK_PERIOD_MS));
#endif
    return 1;
}

static int apply_slot(uint8_t slot)
{
    clock_slot_hdr_t hdr;
    uint8_t region = (uint8_t)(slot ? 2 : 1);

    if (!store_read(region, 0, (uint8_t *)&hdr, sizeof(hdr))) {
        return 0;
    }
    if (hdr.magic != CLOCK_SLOT_MAGIC || hdr.hdr_len != sizeof(hdr)) {
        return 0;
    }
    if (hdr.payload_len == 0 || hdr.payload_len + hdr.hdr_len > CLOCK_SLOT_SIZE) {
        return 0;
    }
    if (store_crc(region, (uint16_t)hdr.hdr_len, hdr.payload_len) != hdr.crc) {
        return 0;
    }
    return apply_fru_stream(region, (uint16_t)hdr.hdr_len, hdr.payload_len);
}

int clock_store_apply(void)
{
    clock_ptr_t ptr;
    int found;

    int ok = ptr_load(&ptr, &found);
#ifdef MODULE_UART_DEBUG
    log_printk("clk", "ptr_load=%d found=%d slot=%d len=%u\n", ok, found, ptr.slot, ptr.len);
#endif
    if (!ok) {
        return apply_slot(0) || apply_slot(1);
    }
    if (apply_slot(ptr.slot)) {
        return 1;
    }
    /* fall back to the other slot (both are self describing) */
    return apply_slot(ptr.slot ? 0 : 1);
}

/* Apply the stored configuration once per boot. Both the IPMI task and the
 * payload's clock step call this; the first caller that finds a valid store
 * applies it, later callers see the flag (no second programming of the
 * switch/PLLs). Returns 1 when a stored configuration is in effect. */
int clock_store_apply_boot(void)
{
    static int applied = 0;

    if (applied) {
        return 1;
    }
    if (clock_store_apply()) {
        applied = 1;
        return 1;
    }
    return 0;
}

int clock_store_init(void)
{
    return clock_store_apply_boot();
}

int clock_store_status(uint8_t *valid, uint8_t *slot, uint16_t *len, uint32_t *crc)
{
    clock_ptr_t ptr;
    int found;
    clock_slot_hdr_t hdr;
    uint8_t region;

    *valid = 0;
    if (!ptr_load(&ptr, &found)) {
        return 0;
    }
    region = (uint8_t)(ptr.slot ? 2 : 1);
    if (!store_read(region, 0, (uint8_t *)&hdr, sizeof(hdr)) ||
        hdr.magic != CLOCK_SLOT_MAGIC || hdr.hdr_len != sizeof(hdr) ||
        hdr.payload_len == 0 || hdr.payload_len + hdr.hdr_len > CLOCK_SLOT_SIZE ||
        store_crc(region, (uint16_t)hdr.hdr_len, hdr.payload_len) != hdr.crc) {
        return 0;
    }
    *valid = 1;
    *slot = ptr.slot;
    *len = ptr.len;
    *crc = ptr.crc;
    return 1;
}

/* ---- IPMI --------------------------------------------------------------- */

IPMI_HANDLER(ipmi_clock_store_status_cmd, NETFN_CUSTOM, IPMI_CUSTOM_CMD_CLOCK_STORE_STATUS,
             ipmi_msg *req, ipmi_msg *rsp)
{
    uint8_t valid, slot;
    uint16_t len;
    uint32_t crc;
    (void)req;
    memset(rsp->data, 0, 8);
    if (clock_store_status(&valid, &slot, &len, &crc)) {
        rsp->data[0] = valid;
        rsp->data[1] = slot;
        rsp->data[2] = (uint8_t)len;
        rsp->data[3] = (uint8_t)(len >> 8);
        rsp->data[4] = (uint8_t)crc;
        rsp->data[5] = (uint8_t)(crc >> 8);
        rsp->data[6] = (uint8_t)(crc >> 16);
        rsp->data[7] = (uint8_t)(crc >> 24);
    }
    rsp->data_len = 8;
    rsp->completion_code = IPMI_CC_OK;
}

IPMI_HANDLER(ipmi_clock_store_apply_cmd, NETFN_CUSTOM, IPMI_CUSTOM_CMD_CLOCK_STORE_APPLY,
             ipmi_msg *req, ipmi_msg *rsp)
{
    (void)req;
    rsp->data[0] = clock_store_apply() ? 1 : 0;
    rsp->data_len = 1;
    rsp->completion_code = IPMI_CC_OK;
}

IPMI_HANDLER(ipmi_clock_store_begin_cmd, NETFN_CUSTOM, IPMI_CUSTOM_CMD_CLOCK_STORE_BEGIN,
             ipmi_msg *req, ipmi_msg *rsp)
{
    clock_ptr_t ptr;
    int found;
    if (req->data_len < 7) {
        rsp->completion_code = IPMI_CC_INV_DATA_FIELD_IN_REQ;
        return;
    }
    uint8_t slot = req->data[0];
    uint16_t len = (uint16_t)(req->data[1] | (req->data[2] << 8));
    uint32_t crc = (uint32_t)req->data[3] | ((uint32_t)req->data[4] << 8) |
                   ((uint32_t)req->data[5] << 16) | ((uint32_t)req->data[6] << 24);

    if (slot == 0xFF) {
        slot = 0;
        if (ptr_load(&ptr, &found)) {
            slot = ptr.slot ? 0 : 1;
        }
    }
    if (slot > 1 || len == 0 || len > CLOCK_SLOT_SIZE || (len & 1u)) {
        rsp->completion_code = IPMI_CC_INV_DATA_FIELD_IN_REQ;
        return;
    }
    if (!store_erase((uint8_t)(slot ? 2 : 1), len)) {
        rsp->completion_code = IPMI_CC_UNSPECIFIED_ERROR;
        return;
    }
    wr.active = 1;
    wr.slot = slot;
    wr.len = len;
    wr.crc = crc;
    wr.written = 0;
    rsp->data[0] = slot;
    rsp->data_len = 1;
    rsp->completion_code = IPMI_CC_OK;
}

IPMI_HANDLER(ipmi_clock_store_block_cmd, NETFN_CUSTOM, IPMI_CUSTOM_CMD_CLOCK_STORE_BLOCK,
             ipmi_msg *req, ipmi_msg *rsp)
{
    if (req->data_len < 3 || !wr.active) {
        rsp->completion_code = IPMI_CC_INV_DATA_FIELD_IN_REQ;
        return;
    }
    uint16_t off = (uint16_t)(req->data[0] | (req->data[1] << 8));
    uint16_t n = (uint16_t)(req->data_len - 2);
    if ((off & 1u) || (n & 1u) || off != wr.written || off + n > wr.len) {
        rsp->completion_code = IPMI_CC_INV_DATA_FIELD_IN_REQ;
        return;
    }
    if (!store_write((uint8_t)(wr.slot ? 2 : 1), off, &req->data[2], n)) {
        rsp->completion_code = IPMI_CC_UNSPECIFIED_ERROR;
        return;
    }
    wr.written = (uint16_t)(off + n);
    rsp->data[0] = (uint8_t)wr.written;
    rsp->data[1] = (uint8_t)(wr.written >> 8);
    rsp->data_len = 2;
    rsp->completion_code = IPMI_CC_OK;
}

IPMI_HANDLER(ipmi_clock_store_finish_cmd, NETFN_CUSTOM, IPMI_CUSTOM_CMD_CLOCK_STORE_FINISH,
             ipmi_msg *req, ipmi_msg *rsp)
{
    if (req->data_len < 4 || !wr.active) {
        rsp->completion_code = IPMI_CC_INV_DATA_FIELD_IN_REQ;
        return;
    }
    uint32_t crc = (uint32_t)req->data[0] | ((uint32_t)req->data[1] << 8) |
                   ((uint32_t)req->data[2] << 16) | ((uint32_t)req->data[3] << 24);
    uint8_t region = (uint8_t)(wr.slot ? 2 : 1);
    uint32_t calc = store_crc(region, 0, wr.written);
    if (crc != wr.crc || calc != crc) {
        wr.active = 0;
        rsp->data[0] = 0;
        rsp->data_len = 1;
        rsp->completion_code = IPMI_CC_UNSPECIFIED_ERROR;
        return;
    }
    if (!ptr_write(wr.slot, wr.written, crc)) {
        wr.active = 0;
        rsp->completion_code = IPMI_CC_UNSPECIFIED_ERROR;
        return;
    }
    wr.active = 0;
    rsp->data[0] = 1;
    rsp->data_len = 1;
    rsp->completion_code = IPMI_CC_OK;
}
