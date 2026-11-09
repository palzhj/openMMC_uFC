/*
 *   openMMC -- Open Source modular IPM Controller firmware
 *
 *   Clock configuration store in the 19 KB region
 *   (pointer page + slot A/B, FRU-format records).
 */

#ifndef CLOCK_STORE_H_
#define CLOCK_STORE_H_

#include <stdint.h>

/* Validate the active slot and apply it (called at boot). */
int clock_store_init(void);
int clock_store_apply_boot(void);

/* Apply the active slot now. Returns 1 on success. */
int clock_store_apply(void);

/* Board hooks (weak defaults in clock_store.c): the switch record is 16
 * bytes; addr comes from the FRU device header (0x58 on the uFC,
 * 0x48 on the u4FCP). The Si5345 hook gets the chip I2C address so boards
 * with two PLLs can pick the right one. */
int clock_store_apply_switch(const uint8_t *matrix, uint8_t addr);
int clock_store_apply_si5345(uint8_t addr, uint16_t reg, uint8_t val);

/* Active slot info: valid, slot index, payload length, CRC. */
int clock_store_status(uint8_t *valid, uint8_t *slot, uint16_t *len, uint32_t *crc);

#endif /* CLOCK_STORE_H_ */
