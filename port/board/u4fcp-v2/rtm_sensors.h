/*
 * openMMC -- u4FCP-v2 AMC-side RTM FMC2/FMC3 monitor sensors
 *
 * The uRTM2.3 carries a UCD90124A (0x5B) that monitors the two FMC sites.
 * The AMC MMC owns those monitors and publishes them as IPMI sensors of the
 * RTM (entity 0xC0, instance 0): they are inserted into the SDR repository
 * while the RTM is present and removed again when the handle is opened.
 *
 * Access path: I2C2 -> TCA9548A 0x70 ch5 -> TCA9548A 0x71 ch7 -> 0x5B.
 *
 * The eight monitors (sensor numbers 0x1B-0x22, in this order) come from
 * six PMBus pages, because VADJ voltage and current share one page:
 *
 *   number  monitor         page  PMBus command
 *   0x20    FMC2 12V I       2    READ_IOUT 0x8C (LINEAR11)
 *   0x21    FMC2 3V3 I       4    READ_IOUT 0x8C (LINEAR11)
 *   0x22    FMC2 VADJ V      3    READ_VOUT 0x8B (LINEAR16, VOUT_MODE 0x14)
 *   0x23    FMC2 VADJ I      3    READ_IOUT 0x8C (LINEAR11)
 *   0x24    FMC3 12V I       5    READ_IOUT 0x8C (LINEAR11)
 *   0x25    FMC3 3V3 I       7    READ_IOUT 0x8C (LINEAR11)
 *   0x26    FMC3 VADJ V      6    READ_VOUT 0x8B (LINEAR16)
 *   0x27    FMC3 VADJ I      6    READ_IOUT 0x8C (LINEAR11)
 *
 * @license GPL-3.0+ <http://spdx.org/licenses/GPL-3.0+>
 */

#ifndef RTM_SENSORS_H_
#define RTM_SENSORS_H_

#include <stdbool.h>

#include "sdr.h"

#define RTM_SENSORS_COUNT       8

/* IPMI sensor numbers have to be unique per IPM controller (Get Sensor
 * Reading addresses by number only).  The AMC's own UCD90320 sensors occupy
 * 0x01-0x1A, so the RTM group starts right after them - the MCH then lists
 * the AMC sensors, then the RTM ones, without a gap.  rtm_sensors_insert()
 * moves the block if a number is ever taken (guards a future AMC sensor). */
#define RTM_SENSORS_FIRST_NUM   0x1B
#define RTM_SENSORS_ENTITY_ID   0xC0    /* RTM (one group for both FMC sites) */
#define RTM_SENSORS_INSTANCE    0x00

/* SDR records of the group, in sensor-number order (defined in sdr_list.c) */
extern const SDR_type_01h_t * const SDR_RTM_FMC_UCD[RTM_SENSORS_COUNT];

/**
 * @brief Create the RTM monitor task (board bring-up, before the scheduler)
 */
void rtm_sensors_init( void );

/**
 * @brief Read the eight monitors and print them to the boot log (integer units)
 *
 * Safe to call at any time: reports the path/UCD state instead of the
 * readings when the RTM is absent or its UCD does not answer.
 */
void rtm_sensors_dump( void );

/**
 * @brief Insert the eight RTM sensors into the SDR repository
 *
 * @retval true  the sensors are now in the repository
 * @retval false the RTM monitor group was already present
 */
bool rtm_sensors_insert( void );

/**
 * @brief Remove the RTM sensors from the SDR repository (no-op when absent)
 */
void rtm_sensors_remove( void );

/**
 * @brief Are the RTM monitor SDRs currently in the repository?
 */
bool rtm_sensors_is_inserted( void );

#endif
