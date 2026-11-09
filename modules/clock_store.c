/*
 *   openMMC -- Open Source modular IPM Controller firmware
 *
 *   Clock configuration store: 19 KB region at __ClockConfig_start
 *   - 1 KB pointer page: two copies of the active-slot record
 *   - 9 KB slot A, 9 KB slot B: FRU-format record file
 *
 *   IPMI custom commands (NetFn 0x32):
 *     0x18 status   -> [valid, slot, len_lo, len_hi, crc0..3]
 *     0x19 apply    -> [result]
 *     0x1A begin    -> [slot, len_lo, len_hi, crc0..3]
 *     0x1B block    -> [off_lo, off_hi, data...]
 *     0x1C finish   -> [crc0..3]
 */

#include <string.h>

#include "port.h"
#include "clock_store.h"
#include "clock_program.h"
#include "idt_8v54816.h"
#include "ipmi.h"
#include "i2c.h"
#include "i2c_mapping.h"

#define CLOCK_PTR_MAGIC   0x50434655u   /* "UFCP" */
#define CLOCK_SLOT_MAGIC  0x43434655u   /* 'UFCC' */
#define CLOCK_SLOT_SIZE   (9u * 1024u)
#define CLOCK_PTR_COPY2   512u
#define CLOCK_PAGE_SIZE   1024u

extern uint32_t __ClockPointer_addr;
extern uint32_t __ClockSlotA_addr;
extern uint32_t __ClockSlotB_addr;

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint32_t seq;
    uint8_t  slot;
    uint8_t  flags;        /* bit0 = valid */
    uint16_t len;
    uint32_t crc;          /* CRC32 of the slot payload */
    uint32_t hdr_crc;      /* CRC32 of the preceding 20 bytes */
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

static uint32_t slot_base(uint8_t slot)
{
    return (slot == 0) ? (uint32_t)&__ClockSlotA_addr : (uint32_t)&__ClockSlotB_addr;
}

/* ---- pointer record ---------------------------------------------------- */

static int ptr_load(clock_ptr_t *best, int *found)
{
    int have = 0;
    clock_ptr_t cand;
    *found = 0;
    for (int copy = 0; copy < 2; copy++) {
        memcpy(&cand, (const void *)((uint32_t)&__ClockPointer_addr + copy * CLOCK_PTR_COPY2),
               sizeof(cand));
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

    FLASH_Unlock();
    FLASH_Status st = FLASH_ErasePage((uint32_t)&__ClockPointer_addr);
    if (st == FLASH_COMPLETE) {
        for (int copy = 0; copy < 2; copy++) {
            uint32_t base = (uint32_t)&__ClockPointer_addr + copy * CLOCK_PTR_COPY2;
            const uint8_t *p = (const uint8_t *)&next;
            for (uint32_t i = 0; i < sizeof(next); i += 2) {
                uint16_t hw = p[i] | ((uint16_t)p[i + 1] << 8);
                if (FLASH_ProgramHalfWord(base + i, hw) != FLASH_COMPLETE) {
                    st = FLASH_ERROR_PG;
                    break;
                }
            }
        }
    }
    FLASH_Lock();
    return st == FLASH_COMPLETE;
}

/* ---- flash helpers ----------------------------------------------------- */

static int flash_erase(uint32_t addr, uint32_t len)
{
    FLASH_Unlock();
    for (uint32_t a = addr & ~(CLOCK_PAGE_SIZE - 1u); a < addr + len; a += CLOCK_PAGE_SIZE) {
        if (FLASH_ErasePage(a) != FLASH_COMPLETE) {
            FLASH_Lock();
            return 0;
        }
    }
    FLASH_Lock();
    return 1;
}

static int flash_write(uint32_t addr, const uint8_t *data, uint16_t len)
{
    FLASH_Unlock();
    for (uint16_t i = 0; i + 1 < len; i += 2) {
        uint16_t hw = data[i] | ((uint16_t)data[i + 1] << 8);
        if (FLASH_ProgramHalfWord(addr + i, hw) != FLASH_COMPLETE) {
            FLASH_Lock();
            return 0;
        }
    }
    FLASH_Lock();
    return 1;
}

/* ---- slot validation / apply ------------------------------------------- */

static int slot_load(const clock_ptr_t *ptr, clock_slot_hdr_t *hdr, const uint8_t **fru)
{
    const uint8_t *base = (const uint8_t *)slot_base(ptr->slot);
    memcpy(hdr, base, sizeof(*hdr));
    if (hdr->magic != CLOCK_SLOT_MAGIC || hdr->hdr_len != sizeof(*hdr)) {
        return 0;
    }
    if (hdr->payload_len == 0 || hdr->payload_len + hdr->hdr_len > CLOCK_SLOT_SIZE) {
        return 0;
    }
    if (crc32(base + hdr->hdr_len, hdr->payload_len) != hdr->crc) {
        return 0;
    }
    *fru = base + hdr->hdr_len;
    return 1;
}

/* Write the 16-byte switch matrix (retries the mux NACK like the PLL). */
static int switch_write(const uint8_t *matrix)
{
    uint8_t addr, iface;
    for (int attempt = 0; attempt < 10; attempt++) {
        if (i2c_take_by_chipid(CHIP_ID_8V54816, &addr, &iface, pdMS_TO_TICKS(200))) {
            uint8_t n = xI2CMasterWrite(iface, addr, (uint8_t *)matrix, 16);
            i2c_give(iface);
            if (n == 16) {
                return 1;
            }
            vI2CBusRecover(iface);
        }
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    return 0;
}

/* FRU multirecord parsing: returns 1 on success. */
static int apply_fru(const uint8_t *fru, uint32_t len)
{
    if (len < 8 || fru[0] != 0x01) {
        return 0;
    }
    uint32_t off = (uint32_t)fru[5] * 8u;
    uint16_t si_count = 0;

    while (off + 4u <= len) {
        const uint8_t *rec = fru + off;
        uint8_t rlen = rec[2];
        uint8_t chk = 0;
        if (rec[0] != 0xC0 || off + 4u + rlen > len) {
            break;
        }
        for (uint8_t i = 0; i < 4; i++) chk += rec[i];
        for (uint8_t i = 0; i < rlen; i++) chk += rec[4 + i];
        if (chk != 0) {
            return 0;
        }
        const uint8_t *data = rec + 4;
        uint8_t recid = data[3];
        uint8_t plen = (uint8_t)(rlen - 6u);
        const uint8_t *payload = data + 5;
        if (plen >= 8u) {
            const uint8_t *p = payload + 8;
            uint8_t dlen = (uint8_t)(plen - 8u);
            if (recid == 0x40 && dlen == 16u) {
                if (!switch_write(p)) {
                    return 0;
                }
            } else if ((recid & 0xF0) == 0x30 && dlen >= 3u) {
                for (uint8_t i = 0; i + 2u < dlen; i += 3u) {
                    uint16_t addr = ((uint16_t)p[i] << 8) | p[i + 1];
                    if (!clock_program_write_entry(addr, p[i + 2])) {
                        return 0;
                    }
                    if (++si_count == 3u) {
                        vTaskDelay(pdMS_TO_TICKS(SI5345_DELAY_MS));
                    }
                }
            }
        }
        if (rec[1] & 0x80) {   /* end of list */
            break;
        }
        off += 4u + rlen;
    }
    return 1;
}

int clock_store_apply(void)
{
    clock_ptr_t ptr;
    clock_slot_hdr_t hdr;
    const uint8_t *fru;
    int found;

    if (!ptr_load(&ptr, &found)) {
        return 0;
    }
    if (!slot_load(&ptr, &hdr, &fru)) {
        return 0;
    }
    return apply_fru(fru, hdr.payload_len);
}

int clock_store_init(void)
{
    return clock_store_apply();
}

int clock_store_status(uint8_t *valid, uint8_t *slot, uint16_t *len, uint32_t *crc)
{
    clock_ptr_t ptr;
    int found;
    clock_slot_hdr_t hdr;
    const uint8_t *fru;

    *valid = 0;
    if (!ptr_load(&ptr, &found)) {
        return 0;
    }
    if (!slot_load(&ptr, &hdr, &fru)) {
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
    if (!flash_erase(slot_base(slot), len)) {
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
    if (!flash_write(slot_base(wr.slot) + off, &req->data[2], n)) {
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
    uint32_t base = slot_base(wr.slot);
    uint32_t calc = crc32((const uint8_t *)base, wr.written);
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
