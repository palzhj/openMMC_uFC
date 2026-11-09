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

/* Apply the active slot now. Returns 1 on success. */
int clock_store_apply(void);

/* Active slot info: valid, slot index, payload length, CRC. */
int clock_store_status(uint8_t *valid, uint8_t *slot, uint16_t *len, uint32_t *crc);

#endif /* CLOCK_STORE_H_ */
