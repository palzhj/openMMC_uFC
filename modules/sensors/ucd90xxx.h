/*
 *   openMMC -- Open Source modular IPM Controller firmware
 *
 *   Copyright (C) 2015-2016  Henrique Silva <henrique.silva@lnls.br>
 *   Copyright (C) 2025-2026  Jie Zhang <zhj@ihep.ac.cn>
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
 * @defgroup UCD90xxx UCD90xxx - Voltage, Current and Temperature Monitor
 * @ingroup SENSORS
 *
 * The UCD90xxx is a power monitor with an I2C- or PMBUS-compatible interface. <br>
 * The UCD90xxx monitors both shunt drop and supply voltage.
 */

/**
 * @file ucd90xxx.h
 *
 * @brief Definitions for UCD90xxx Voltage/Current/Temperature Sensor
 *
 * @ingroup ucd90xxx
 */

#ifndef UCD90XXX_H_
#define UCD90XXX_H_

#include "sdr.h"

typedef struct {
    uint8_t chipid;
    sensor_t * sensor;
} ucd_data_t;

// void ucd_read_voltages( ucd_data_t * data );
// void ucd_read_current( ucd_data_t * data );
// void ucd_read_temperature( ucd_data_t * data );

void ucd_read_id(ucd_data_t * data, uint8_t *str);
uint8_t ucd_get_gpio(ucd_data_t * data, uint8_t pin_number);
void ucd_set_gpio(ucd_data_t * data, uint8_t pin_number, uint8_t status);
void ucd_set_power(ucd_data_t * data, uint8_t page, uint8_t state);

void ucd_init( void );
void vTaskUCD( void* Parameters );

#endif
