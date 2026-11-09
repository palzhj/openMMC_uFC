/*
 * clock_program.h -- programme the uFC Si5345 clock generator over I2C
 */

#ifndef CLOCK_PROGRAM_H
#define CLOCK_PROGRAM_H

#include <stdint.h>

/** Calibration delay after the preamble, ms */
#define SI5345_DELAY_MS     300

/** Clock programming status values */
#define CLOCK_PROG_IDLE     0
#define CLOCK_PROG_RUNNING  1
#define CLOCK_PROG_OK       2
#define CLOCK_PROG_ERROR    3

/** Write one 16-bit-addressed register (page-aware). 1 on success. */
uint8_t clock_program_write_entry(uint16_t addr, uint8_t data);
void clock_program_select_chip(uint8_t chip_id);

#endif /* CLOCK_PROGRAM_H */
