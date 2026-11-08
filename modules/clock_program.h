/*
 * clock_program.h -- programme the uFC Si5345 clock generator over I2C
 */

#ifndef CLOCK_PROGRAM_H
#define CLOCK_PROGRAM_H

#include <stdint.h>

/** Clock programming status values */
#define CLOCK_PROG_IDLE     0
#define CLOCK_PROG_RUNNING  1
#define CLOCK_PROG_OK       2
#define CLOCK_PROG_ERROR    3

#endif /* CLOCK_PROGRAM_H */
