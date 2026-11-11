/*
 *   openMMC -- Open Source modular IPM Controller firmware
 *
 *   Copyright (C) 2015-2016  Henrique Silva <henrique.silva@lnls.br>
 *   Copyright (C) 2021  Krzysztof Macias <krzysztof.macias@creotech.pl>
 *   Copyright (C) 2025  Jie Zhang  <zhj@ihep.ac.cn>
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

/* Table 42 ipmi-second-gen */

/* Project Includes */
#include "port.h"
#include "sdr.h"
#include "utils.h"
#include "i2c_mapping.h"
#include "rtm_sensors.h"

/* Sensors includes */
#include "sensors.h"

/* SDR List */

#ifdef MODULE_HOTSWAP
/* AMC Hot-Swap sensor SDR */
const SDR_type_02h_t SDR_HOTSWAP_AMC = {

    .hdr.recID_LSB = 0x00, /* Filled by sdr_insert_entry() */
    .hdr.recID_MSB = 0x00,
    .hdr.SDRversion = 0x51,
    .hdr.rectype = TYPE_02,
    .hdr.reclength = sizeof(SDR_type_02h_t) - sizeof(SDR_entry_hdr_t),

    .ownerID = 0x00,   /* i2c address, -> SDR_Init */
    .ownerLUN = 0x00,  /* sensor owner LUN */
    .sensornum = 0x00, /* Filled by sdr_insert_entry() */

    /* record body bytes */
    .entityID = 0xC1,                   /* entity id: AMC Module */
    .entityinstance = 0x00,             /* entity instance -> SDR_Init */
    .sensorinit = 0x03,                 /* init: event generation + scanning enabled */
    .sensorcap = 0xc1,                  /* capabilities: auto re-arm,*/
    .sensortype = SENSOR_TYPE_HOT_SWAP, /* sensor type: HOT SWAP*/
    .event_reading_type = 0x6f,         /* sensor reading*/
    .assertion_event_mask = 0x0000,     /* assertion event mask */
    .deassertion_event_mask = 0x0000,   /* deassertion event mask */
    .readable_threshold_mask = 0x00,    /* LSB: readable Threshold mask: no thresholds are readable:  */
    .settable_threshold_mask = 0x00,    /* MSB: setable Threshold mask: no thresholds are setable: */
    .sensor_units_1 = 0xc0,             /* sensor units 1 : Does not return analog reading*/
    .sensor_units_2 = 0x00,             /* sensor units 2 :*/
    .sensor_units_3 = 0x00,             /* sensor units 3 :*/
    .record_sharing[0] = 0x00,
    .record_sharing[1] = 0x00,
    .pos_thr_hysteresis = 0x00,                  /* positive going Threshold hysteresis value */
    .neg_thr_hysteresis = 0x00,                  /* negative going Threshold hysteresis value */
    .reserved1 = 0x00,                           /* reserved */
    .reserved2 = 0x00,                           /* reserved */
    .reserved3 = 0x00,                           /* reserved */
    .OEM = 0x00,                                 /* OEM reserved */
    .IDtypelen = 0xc0 | STR_SIZE("HOTSWAP AMC"), /* 8 bit ASCII, number of bytes */
    .IDstring = "HOTSWAP AMC"                    /* sensor string */
};
#endif

#ifdef MODULE_UCD90XXX

#include "payload.h"
extern TaskHandle_t vTaskUCD_Handle;

#define SDR_AMC_12V_ID "AMC +12V"
#define SDR_RTM_12V_CURR_ID "RTM +12V Curr"
#define SDR_FMC0_12V_CURR_ID "FMC0 +12V Curr"
#define SDR_FMC0_VADJ_ID "FMC0 VADJ"
#define SDR_FMC0_VADJ_CURR_ID "FMC0 VADJ Curr"
#define SDR_FMC0_3V3_CURR_ID "FMC0 +3.3V Curr"
#define SDR_FMC1_12V_CURR_ID "FMC1 +12V Curr"
#define SDR_FMC1_VADJ_ID "FMC1 VADJ"
#define SDR_FMC1_VADJ_CURR_ID "FMC1 VADJ Curr"
#define SDR_FMC1_3V3_CURR_ID "FMC1 +3.3V Curr"
#define SDR_AMC_FPGA_0V85_ID "FPGA +0.85V"
#define SDR_AMC_FPGA_0V85_CURR_ID "FPGA +0.85V Curr"
#define SDR_AMC_FPGA_1V8_ID "FPGA +1.8V"
#define SDR_AMC_FPGA_1V8_CURR_ID "FPGA +1.8V Curr"
#define SDR_AMC_FPGA_0V9A_ID "FPGA +0.9VA"
#define SDR_AMC_FPGA_0V9A_CURR_ID "FPGA +0.9VA Curr"
#define SDR_AMC_FPGA_1V2A_ID "FPGA +1.2VA"
#define SDR_AMC_FPGA_1V2A_CURR_ID "FPGA +1.2VA Curr"
#define SDR_AMC_DDR_1V2T_ID "DDR +1.2VT"
#define SDR_AMC_DDR_1V2T_CURR_ID "DDR +1.2VT Curr"
#define SDR_AMC_DDR_1V2B_ID "DDR +1.2VB"
#define SDR_AMC_DDR_1V2B_CURR_ID "DDR +1.2VB Curr"
#define SDR_AMC_IO_3V3_ID "IO +3.3V"
#define SDR_AMC_IO_3V3_CURR_ID "IO +3.3V Curr"

/* AMC 12V */
const SDR_type_01h_t SDR_AMC_12V = {

    .hdr.recID_LSB = 0x00, /* Filled by sdr_insert_entry() */
    .hdr.recID_MSB = 0x00,
    .hdr.SDRversion = 0x51,
    .hdr.rectype = TYPE_01,
    .hdr.reclength = sizeof(SDR_type_01h_t) - sizeof(SDR_entry_hdr_t),

    .ownerID = 0x00,   /* i2c address, -> SDR_Init */
    .ownerLUN = 0x00,  /* sensor owner LUN */
    .sensornum = 0x00, /* Filled by sdr_insert_entry() */

    /* record body bytes */
    .entityID = 0xC1,                             /* entity id: AMC Module */
    .entityinstance = 0x00,                       /* entity instance -> SDR_Init */
    .sensorinit = 0x7F,                           /* init: event generation + scanning enabled */
    .sensorcap = 0x56,                            /* capabilities: auto re-arm,*/
    .sensortype = SENSOR_TYPE_VOLTAGE,            /* sensor type: Voltage*/
    .event_reading_type = 0x01,                   /* sensor reading*/
    .assertion_event_mask = 0x7A95,               /* assertion event mask (All upper going-high and lower going-low events) */
    .deassertion_event_mask = 0x7A80,             /* deassertion event mask (All upper going-high events) */
    .readable_threshold_mask = 0x3F,              /* LSB: readable Threshold mask: all thresholds are readable:  */
    .settable_threshold_mask = 0x00,              /* MSB: setable Threshold mask: none of the thresholds are setable: */
    .sensor_units_1 = 0x00,                       /* sensor units 1 :*/
    .sensor_units_2 = 0x04,                       /* sensor units 2 :*/
    .sensor_units_3 = 0x00,                       /* sensor units 3 :*/
    .linearization = 0x00,                        /* Linearization */
    .M = 64,                                      /* M */
    .M_tol = 0x00,                                /* M - Tolerance */
    .B = 0x00,                                    /* B */
    .B_accuracy = 0x00,                           /* B - Accuracy */
    .acc_exp_sensor_dir = 0x00,                   /* Sensor direction */
    .Rexp_Bexp = 0xD0,                            /* R-Exp = -3 , B-Exp = 0 */
    .analog_flags = 0x03,                         /* Analogue characteristics flags */
    .nominal_reading = (12000 >> 6),              /* Nominal reading [(M * x + B * 10^(B_exp)) * 10^(R_exp)] = 12.032 V */
    .normal_max = (13000 >> 6),                   /* Normal maximum = 12.544 V */
    .normal_min = (11000 >> 6),                   /* Normal minimum = 11.456 V */
    .sensor_max_reading = 0xFF,                   /* Sensor Maximum reading */
    .sensor_min_reading = 0x00,                   /* Sensor Minimum reading */
    .upper_nonrecover_thr = (16000 >> 6),         /* Upper non-recoverable Threshold = 13.056 V */
    .upper_critical_thr = (15000 >> 6),           /* Upper critical Threshold = 12.608 V */
    .upper_noncritical_thr = (14000 >> 6),        /* Upper non critical Threshold = 12.48 V */
    .lower_nonrecover_thr = (8000 >> 6),          /* Lower non-recoverable Threshold = 11.008 V */
    .lower_critical_thr = (9000 >> 6),            /* Lower critical Threshold = 11.392 V */
    .lower_noncritical_thr = (10000 >> 6),        /* Lower non-critical Threshold = 11.52 V */
    .pos_thr_hysteresis = 2,                      /* positive going Threshold hysteresis value */
    .neg_thr_hysteresis = 2,                      /* negative going Threshold hysteresis value */
    .reserved1 = 0x00,                            /* reserved */
    .reserved2 = 0x00,                            /* reserved */
    .OEM = UCD_AMC_12V_ID,                                     /* OEM reserved */
    .IDtypelen = 0xc0 | STR_SIZE(SDR_AMC_12V_ID), /* 8 bit ASCII, number of bytes */
    .IDstring = SDR_AMC_12V_ID                    /* sensor string */
};

/* FMC0 PVADJ */
const SDR_type_01h_t SDR_FMC0_VADJ = {
  .hdr.recID_LSB = 0x00, /* Filled by sdr_insert_entry() */
  .hdr.recID_MSB = 0x00,
  .hdr.SDRversion = 0x51,
  .hdr.rectype = TYPE_01,
  .hdr.reclength = sizeof(SDR_type_01h_t) - sizeof(SDR_entry_hdr_t),

  .ownerID = 0x00, /* i2c address, -> SDR_Init */
  .ownerLUN = 0x00, /* sensor owner LUN */
  .sensornum = 0x00, /* Filled by sdr_insert_entry() */

  /* record body bytes */
  .entityID = 0xC1, /* entity id: AMC Module */
  .entityinstance = 0x00, /* entity instance -> SDR_Init */
  .sensorinit = 0x7F, /* init: event generation + scanning enabled */
  .sensorcap = 0x56, /* capabilities: auto re-arm,*/
  .sensortype = SENSOR_TYPE_VOLTAGE, /* sensor type: voltage*/
  .event_reading_type = 0x01, /* sensor reading*/
  .assertion_event_mask = 0x7A95, /* assertion event mask (All upper going-high and lower going-low events) */
  .deassertion_event_mask = 0x7A80, /* deassertion event mask (All upper going-high events) */
  .readable_threshold_mask = 0x3F, /* LSB: readable Threshold mask: all thresholds are readable:  */
  .settable_threshold_mask = 0x00, /* MSB: setable Threshold mask: none of the thresholds are setable: */
  .sensor_units_1 = 0x00, /* sensor units 1 :*/
  .sensor_units_2 = 0x04, /* sensor units 2 :*/
  .sensor_units_3 = 0x00, /* sensor units 3 :*/
  .linearization = 0x00, /* Linearization */
  .M = 16, /* M */
  .M_tol = 0x00, /* M - Tolerance */
  .B = 0x00, /* B */
  .B_accuracy = 0x00, /* B - Accuracy */
  .acc_exp_sensor_dir = 0x00, /* Sensor direction */
  .Rexp_Bexp = 0xD0, /* R-Exp , B-Exp */
  .analog_flags = 0x07, /* Analogue characteristics flags */
  .nominal_reading = (1350 >> 4), /* Nominal reading */
  .normal_max = (1950 >> 4), /* Normal maximum */
  .normal_min = (850 >> 4), /* Normal minimum */
  .sensor_max_reading = 0xFF, /* Sensor Maximum reading */
  .sensor_min_reading = 0x00, /* Sensor Minimum reading */
  .upper_nonrecover_thr = (2100 >> 4), /* Upper non-recoverable Threshold */
  .upper_critical_thr = (2050 >> 4), /* Upper critical Threshold */
  .upper_noncritical_thr = (2000 >> 4), /* Upper non critical Threshold */
  .lower_nonrecover_thr = (700 >> 4), /* Lower non-recoverable Threshold */
  .lower_critical_thr = (750 >> 4), /* Lower critical Threshold */
  .lower_noncritical_thr = (800 >> 4), /* Lower non-critical Threshold */
  .pos_thr_hysteresis = 1, /* positive going Threshold hysteresis value */
  .neg_thr_hysteresis = 1, /* negative going Threshold hysteresis value */
  .reserved1 = 0x00, /* reserved */
  .reserved2 = 0x00, /* reserved */
  .OEM = UCD_FMC0_VADJ_ID, /* OEM reserved */
  .IDtypelen = 0xc0 | STR_SIZE(SDR_FMC0_VADJ_ID), /* 8 bit ASCII, number of bytes */
  .IDstring = SDR_FMC0_VADJ_ID /* sensor string */
};

/* FMC1 PVADJ */
const SDR_type_01h_t SDR_FMC1_VADJ = {
  .hdr.recID_LSB = 0x00, /* Filled by sdr_insert_entry() */
  .hdr.recID_MSB = 0x00,
  .hdr.SDRversion = 0x51,
  .hdr.rectype = TYPE_01,
  .hdr.reclength = sizeof(SDR_type_01h_t) - sizeof(SDR_entry_hdr_t),

  .ownerID = 0x00, /* i2c address, -> SDR_Init */
  .ownerLUN = 0x00, /* sensor owner LUN */
  .sensornum = 0x00, /* Filled by sdr_insert_entry() */

  /* record body bytes */
  .entityID = 0xC1, /* entity id: AMC Module */
  .entityinstance = 0x00, /* entity instance -> SDR_Init */
  .sensorinit = 0x7F, /* init: event generation + scanning enabled */
  .sensorcap = 0x56, /* capabilities: auto re-arm,*/
  .sensortype = SENSOR_TYPE_VOLTAGE, /* sensor type: voltage*/
  .event_reading_type = 0x01, /* sensor reading*/
  .assertion_event_mask = 0x7A95, /* assertion event mask (All upper going-high and lower going-low events) */
  .deassertion_event_mask = 0x7A80, /* deassertion event mask (All upper going-high events) */
  .readable_threshold_mask = 0x3F, /* LSB: readable Threshold mask: all thresholds are readable:  */
  .settable_threshold_mask = 0x00, /* MSB: setable Threshold mask: none of the thresholds are setable: */
  .sensor_units_1 = 0x00, /* sensor units 1 :*/
  .sensor_units_2 = 0x04, /* sensor units 2 :*/
  .sensor_units_3 = 0x00, /* sensor units 3 :*/
  .linearization = 0x00, /* Linearization */
  .M = 16, /* M */
  .M_tol = 0x00, /* M - Tolerance */
  .B = 0x00, /* B */
  .B_accuracy = 0x00, /* B - Accuracy */
  .acc_exp_sensor_dir = 0x00, /* Sensor direction */
  .Rexp_Bexp = 0xD0, /* R-Exp , B-Exp */
  .analog_flags = 0x07, /* Analogue characteristics flags */
  .nominal_reading = (1350 >> 4), /* Nominal reading */
  .normal_max = (1950 >> 4), /* Normal maximum */
  .normal_min = (850 >> 4), /* Normal minimum */
  .sensor_max_reading = 0xFF, /* Sensor Maximum reading */
  .sensor_min_reading = 0x00, /* Sensor Minimum reading */
  .upper_nonrecover_thr = (2100 >> 4), /* Upper non-recoverable Threshold */
  .upper_critical_thr = (2050 >> 4), /* Upper critical Threshold */
  .upper_noncritical_thr = (2000 >> 4), /* Upper non critical Threshold */
  .lower_nonrecover_thr = (700 >> 4), /* Lower non-recoverable Threshold */
  .lower_critical_thr = (750 >> 4), /* Lower critical Threshold */
  .lower_noncritical_thr = (800 >> 4), /* Lower non-critical Threshold */
  .pos_thr_hysteresis = 1, /* positive going Threshold hysteresis value */
  .neg_thr_hysteresis = 1, /* negative going Threshold hysteresis value */
  .reserved1 = 0x00, /* reserved */
  .reserved2 = 0x00, /* reserved */
  .OEM = UCD_FMC1_VADJ_ID, /* OEM reserved */
  .IDtypelen = 0xc0 | STR_SIZE(SDR_FMC1_VADJ_ID), /* 8 bit ASCII, number of bytes */
  .IDstring = SDR_FMC1_VADJ_ID /* sensor string */
};

/* AMC FPGA 0V85 */
const SDR_type_01h_t SDR_AMC_FPGA_0V85 = {
  .hdr.recID_LSB = 0x00, /* Filled by sdr_insert_entry() */
  .hdr.recID_MSB = 0x00,
  .hdr.SDRversion = 0x51,
  .hdr.rectype = TYPE_01,
  .hdr.reclength = sizeof(SDR_type_01h_t) - sizeof(SDR_entry_hdr_t),

  .ownerID = 0x00, /* i2c address, -> SDR_Init */
  .ownerLUN = 0x00, /* sensor owner LUN */
  .sensornum = 0x00, /* Filled by sdr_insert_entry() */

  /* record body bytes */
  .entityID = 0xC1, /* entity id: AMC Module */
  .entityinstance = 0x00, /* entity instance -> SDR_Init */
  .sensorinit = 0x7F, /* init: event generation + scanning enabled */
  .sensorcap = 0x56, /* capabilities: auto re-arm,*/
  .sensortype = SENSOR_TYPE_VOLTAGE, /* sensor type: VOLTAGE*/
  .event_reading_type = 0x01, /* sensor reading*/
  .assertion_event_mask = 0x7A95, /* assertion event mask (All upper going-high and lower going-low events) */
  .deassertion_event_mask = 0x7A80, /* deassertion event mask (All upper going-high events) */
  .readable_threshold_mask = 0x3F, /* LSB: readable Threshold mask: all thresholds are readable:  */
  .settable_threshold_mask = 0x00, /* MSB: setable Threshold mask: none of the thresholds are setable: */
  .sensor_units_1 = 0x00, /* sensor units 1 :*/
  .sensor_units_2 = 0x04, /* sensor units 2 :*/
  .sensor_units_3 = 0x00, /* sensor units 3 :*/
  .linearization = 0x00, /* Linearization */
  .M = 8, /* M */
  .M_tol = 0x00, /* M - Tolerance */
  .B = 0x00, /* B */
  .B_accuracy = 0x00, /* B - Accuracy */
  .acc_exp_sensor_dir = 0x00, /* Sensor direction */
  .Rexp_Bexp = 0xD0, /* R-Exp , B-Exp */
  .analog_flags = 0x07, /* Analogue characteristics flags */
  .nominal_reading = (850 >> 3), /* Nominal reading */
  .normal_max = (900 >> 3), /* Normal maximum */
  .normal_min = (800 >> 3), /* Normal minimum */
  .sensor_max_reading = 0xFF, /* Sensor Maximum reading */
  .sensor_min_reading = 0x00, /* Sensor Minimum reading */
  .upper_nonrecover_thr = (1150 >> 3), /* Upper non-recoverable Threshold */
  .upper_critical_thr = (1000 >> 3), /* Upper critical Threshold */
  .upper_noncritical_thr = (950 >> 3), /* Upper non critical Threshold */
  .lower_nonrecover_thr = (650 >> 3), /* Lower non-recoverable Threshold */
  .lower_critical_thr = (700 >> 3), /* Lower critical Threshold */
  .lower_noncritical_thr = (750 >> 3), /* Lower non-critical Threshold */
  .pos_thr_hysteresis = 2, /* positive going Threshold hysteresis value */
  .neg_thr_hysteresis = 2, /* negative going Threshold hysteresis value */
  .reserved1 = 0x00, /* reserved */
  .reserved2 = 0x00, /* reserved */
  .OEM = UCD_FPGA_0V85_ID, /* OEM reserved */
  .IDtypelen = 0xc0 | STR_SIZE(SDR_AMC_FPGA_0V85_ID), /* 8 bit ASCII, number of bytes */
  .IDstring = SDR_AMC_FPGA_0V85_ID /* sensor string */
};

/* AMC FPGA 1V8 */
const SDR_type_01h_t SDR_AMC_FPGA_1V8 = {
  .hdr.recID_LSB = 0x00, /* Filled by sdr_insert_entry() */
  .hdr.recID_MSB = 0x00,
  .hdr.SDRversion = 0x51,
  .hdr.rectype = TYPE_01,
  .hdr.reclength = sizeof(SDR_type_01h_t) - sizeof(SDR_entry_hdr_t),

  .ownerID = 0x00, /* i2c address, -> SDR_Init */
  .ownerLUN = 0x00, /* sensor owner LUN */
  .sensornum = 0x00, /* Filled by sdr_insert_entry() */

  /* record body bytes */
  .entityID = 0xC1, /* entity id: AMC Module */
  .entityinstance = 0x00, /* entity instance -> SDR_Init */
  .sensorinit = 0x7F, /* init: event generation + scanning enabled */
  .sensorcap = 0x56, /* capabilities: auto re-arm,*/
  .sensortype = SENSOR_TYPE_VOLTAGE, /* sensor type: VOLTAGE*/
  .event_reading_type = 0x01, /* sensor reading*/
  .assertion_event_mask = 0x7A95, /* assertion event mask (All upper going-high and lower going-low events) */
  .deassertion_event_mask = 0x7A80, /* deassertion event mask (All upper going-high events) */
  .readable_threshold_mask = 0x3F, /* LSB: readable Threshold mask: all thresholds are readable:  */
  .settable_threshold_mask = 0x00, /* MSB: setable Threshold mask: none of the thresholds are setable: */
  .sensor_units_1 = 0x00, /* sensor units 1 :*/
  .sensor_units_2 = 0x04, /* sensor units 2 :*/
  .sensor_units_3 = 0x00, /* sensor units 3 :*/
  .linearization = 0x00, /* Linearization */
  .M = 16, /* M */
  .M_tol = 0x00, /* M - Tolerance */
  .B = 0x00, /* B */
  .B_accuracy = 0x00, /* B - Accuracy */
  .acc_exp_sensor_dir = 0x00, /* Sensor direction */
  .Rexp_Bexp = 0xD0, /* R-Exp , B-Exp */
  .analog_flags = 0x07, /* Analogue characteristics flags */
  .nominal_reading = (1800 >> 4), /* Nominal reading */
  .normal_max = (1900 >> 4), /* Normal maximum */
  .normal_min = (1700 >> 4), /* Normal minimum */
  .sensor_max_reading = 0xFF, /* Sensor Maximum reading */
  .sensor_min_reading = 0x00, /* Sensor Minimum reading */
  .upper_nonrecover_thr = (2050 >> 4), /* Upper non-recoverable Threshold */
  .upper_critical_thr = (2000 >> 4), /* Upper critical Threshold */
  .upper_noncritical_thr = (1950 >> 4), /* Upper non critical Threshold */
  .lower_nonrecover_thr = (1550 >> 4), /* Lower non-recoverable Threshold */
  .lower_critical_thr = (1600 >> 4), /* Lower critical Threshold */
  .lower_noncritical_thr = (1650 >> 4), /* Lower non-critical Threshold */
  .pos_thr_hysteresis = 2, /* positive going Threshold hysteresis value */
  .neg_thr_hysteresis = 2, /* negative going Threshold hysteresis value */
  .reserved1 = 0x00, /* reserved */
  .reserved2 = 0x00, /* reserved */
  .OEM = UCD_FPGA_1V8_ID, /* OEM reserved */
  .IDtypelen = 0xc0 | STR_SIZE(SDR_AMC_FPGA_1V8_ID), /* 8 bit ASCII, number of bytes */
  .IDstring = SDR_AMC_FPGA_1V8_ID /* sensor string */
};

/* AMC FPGA +0.9V Analog */
const SDR_type_01h_t SDR_AMC_FPGA_0V9A = {
  .hdr.recID_LSB = 0x00, /* Filled by sdr_insert_entry() */
  .hdr.recID_MSB = 0x00,
  .hdr.SDRversion = 0x51,
  .hdr.rectype = TYPE_01,
  .hdr.reclength = sizeof(SDR_type_01h_t) - sizeof(SDR_entry_hdr_t),

  .ownerID = 0x00, /* i2c address, -> SDR_Init */
  .ownerLUN = 0x00, /* sensor owner LUN */
  .sensornum = 0x00, /* Filled by sdr_insert_entry() */

  /* record body bytes */
  .entityID = 0xC1, /* entity id: AMC Module */
  .entityinstance = 0x00, /* entity instance -> SDR_Init */
  .sensorinit = 0x7F, /* init: event generation + scanning enabled */
  .sensorcap = 0x56, /* capabilities: auto re-arm,*/
  .sensortype = SENSOR_TYPE_VOLTAGE, /* sensor type: VOLTAGE*/
  .event_reading_type = 0x01, /* sensor reading*/
  .assertion_event_mask = 0x7A95, /* assertion event mask (All upper going-high and lower going-low events) */
  .deassertion_event_mask = 0x7A80, /* deassertion event mask (All upper going-high events) */
  .readable_threshold_mask = 0x3F, /* LSB: readable Threshold mask: all thresholds are readable:  */
  .settable_threshold_mask = 0x00, /* MSB: setable Threshold mask: none of the thresholds are setable: */
  .sensor_units_1 = 0x00, /* sensor units 1 :*/
  .sensor_units_2 = 0x04, /* sensor units 2 :*/
  .sensor_units_3 = 0x00, /* sensor units 3 :*/
  .linearization = 0x00, /* Linearization */
  .M = 8, /* M */
  .M_tol = 0x00, /* M - Tolerance */
  .B = 0x00, /* B */
  .B_accuracy = 0x00, /* B - Accuracy */
  .acc_exp_sensor_dir = 0x00, /* Sensor direction */
  .Rexp_Bexp = 0xD0, /* R-Exp , B-Exp */
  .analog_flags = 0x07, /* Analogue characteristics flags */
  .nominal_reading = (900 >> 3), /* Nominal reading */
  .normal_max = (950 >> 3), /* Normal maximum */
  .normal_min = (850 >> 3), /* Normal minimum */
  .sensor_max_reading = 0xFF, /* Sensor Maximum reading */
  .sensor_min_reading = 0x00, /* Sensor Minimum reading */
  .upper_nonrecover_thr = (1100 >> 3), /* Upper non-recoverable Threshold */
  .upper_critical_thr = (1050 >> 3), /* Upper critical Threshold */
  .upper_noncritical_thr = (1000 >> 3), /* Upper non critical Threshold */
  .lower_nonrecover_thr = (700 >> 3), /* Lower non-recoverable Threshold */
  .lower_critical_thr = (750 >> 3), /* Lower critical Threshold */
  .lower_noncritical_thr = (800 >> 3), /* Lower non-critical Threshold */
  .pos_thr_hysteresis = 2, /* positive going Threshold hysteresis value */
  .neg_thr_hysteresis = 2, /* negative going Threshold hysteresis value */
  .reserved1 = 0x00, /* reserved */
  .reserved2 = 0x00, /* reserved */
  .OEM = UCD_FPGA_0V9A_ID, /* OEM reserved */
  .IDtypelen = 0xc0 | STR_SIZE(SDR_AMC_FPGA_0V9A_ID), /* 8 bit ASCII, number of bytes */
  .IDstring = SDR_AMC_FPGA_0V9A_ID /* sensor string */
};

/* AMC FPGA +1.2V Analog */
const SDR_type_01h_t SDR_AMC_FPGA_1V2A = {
  .hdr.recID_LSB = 0x00, /* Filled by sdr_insert_entry() */
  .hdr.recID_MSB = 0x00,
  .hdr.SDRversion = 0x51,
  .hdr.rectype = TYPE_01,
  .hdr.reclength = sizeof(SDR_type_01h_t) - sizeof(SDR_entry_hdr_t),

  .ownerID = 0x00, /* i2c address, -> SDR_Init */
  .ownerLUN = 0x00, /* sensor owner LUN */
  .sensornum = 0x00, /* Filled by sdr_insert_entry() */

  /* record body bytes */
  .entityID = 0xC1, /* entity id: AMC Module */
  .entityinstance = 0x00, /* entity instance -> SDR_Init */
  .sensorinit = 0x7F, /* init: event generation + scanning enabled */
  .sensorcap = 0x56, /* capabilities: auto re-arm,*/
  .sensortype = SENSOR_TYPE_VOLTAGE, /* sensor type: VOLTAGE*/
  .event_reading_type = 0x01, /* sensor reading*/
  .assertion_event_mask = 0x7A95, /* assertion event mask (All upper going-high and lower going-low events) */
  .deassertion_event_mask = 0x7A80, /* deassertion event mask (All upper going-high events) */
  .readable_threshold_mask = 0x3F, /* LSB: readable Threshold mask: all thresholds are readable:  */
  .settable_threshold_mask = 0x00, /* MSB: setable Threshold mask: none of the thresholds are setable: */
  .sensor_units_1 = 0x00, /* sensor units 1 :*/
  .sensor_units_2 = 0x04, /* sensor units 2 :*/
  .sensor_units_3 = 0x00, /* sensor units 3 :*/
  .linearization = 0x00, /* Linearization */
  .M = 8, /* M */
  .M_tol = 0x00, /* M - Tolerance */
  .B = 0x00, /* B */
  .B_accuracy = 0x00, /* B - Accuracy */
  .acc_exp_sensor_dir = 0x00, /* Sensor direction */
  .Rexp_Bexp = 0xD0, /* R-Exp , B-Exp */
  .analog_flags = 0x07, /* Analogue characteristics flags */
  .nominal_reading = (1200 >> 3), /* Nominal reading */
  .normal_max = (1250 >> 3), /* Normal maximum */
  .normal_min = (1150 >> 3), /* Normal minimum */
  .sensor_max_reading = 0xFF, /* Sensor Maximum reading */
  .sensor_min_reading = 0x00, /* Sensor Minimum reading */
  .upper_nonrecover_thr = (1400 >> 3), /* Upper non-recoverable Threshold */
  .upper_critical_thr = (1350 >> 3), /* Upper critical Threshold */
  .upper_noncritical_thr = (1300 >> 3), /* Upper non critical Threshold */
  .lower_nonrecover_thr = (1000 >> 3), /* Lower non-recoverable Threshold */
  .lower_critical_thr = (1050 >> 3), /* Lower critical Threshold */
  .lower_noncritical_thr = (1100 >> 3), /* Lower non-critical Threshold */
  .pos_thr_hysteresis = 2, /* positive going Threshold hysteresis value */
  .neg_thr_hysteresis = 2, /* negative going Threshold hysteresis value */
  .reserved1 = 0x00, /* reserved */
  .reserved2 = 0x00, /* reserved */
  .OEM = UCD_FPGA_1V2A_ID, /* OEM reserved */
  .IDtypelen = 0xc0 | STR_SIZE(SDR_AMC_FPGA_1V2A_ID), /* 8 bit ASCII, number of bytes */
  .IDstring = SDR_AMC_FPGA_1V2A_ID /* sensor string */
};

/* AMC DDR +1.2V TOP */
const SDR_type_01h_t SDR_AMC_DDR_1V2T = {
  .hdr.recID_LSB = 0x00, /* Filled by sdr_insert_entry() */
  .hdr.recID_MSB = 0x00,
  .hdr.SDRversion = 0x51,
  .hdr.rectype = TYPE_01,
  .hdr.reclength = sizeof(SDR_type_01h_t) - sizeof(SDR_entry_hdr_t),

  .ownerID = 0x00, /* i2c address, -> SDR_Init */
  .ownerLUN = 0x00, /* sensor owner LUN */
  .sensornum = 0x00, /* Filled by sdr_insert_entry() */

  /* record body bytes */
  .entityID = 0xC1, /* entity id: AMC Module */
  .entityinstance = 0x00, /* entity instance -> SDR_Init */
  .sensorinit = 0x7F, /* init: event generation + scanning enabled */
  .sensorcap = 0x56, /* capabilities: auto re-arm,*/
  .sensortype = SENSOR_TYPE_VOLTAGE, /* sensor type: VOLTAGE*/
  .event_reading_type = 0x01, /* sensor reading*/
  .assertion_event_mask = 0x7A95, /* assertion event mask (All upper going-high and lower going-low events) */
  .deassertion_event_mask = 0x7A80, /* deassertion event mask (All upper going-high events) */
  .readable_threshold_mask = 0x3F, /* LSB: readable Threshold mask: all thresholds are readable:  */
  .settable_threshold_mask = 0x00, /* MSB: setable Threshold mask: none of the thresholds are setable: */
  .sensor_units_1 = 0x00, /* sensor units 1 :*/
  .sensor_units_2 = 0x04, /* sensor units 2 :*/
  .sensor_units_3 = 0x00, /* sensor units 3 :*/
  .linearization = 0x00, /* Linearization */
  .M = 8, /* M */
  .M_tol = 0x00, /* M - Tolerance */
  .B = 0x00, /* B */
  .B_accuracy = 0x00, /* B - Accuracy */
  .acc_exp_sensor_dir = 0x00, /* Sensor direction */
  .Rexp_Bexp = 0xD0, /* R-Exp , B-Exp */
  .analog_flags = 0x07, /* Analogue characteristics flags */
  .nominal_reading = (1200 >> 3), /* Nominal reading */
  .normal_max = (1250 >> 3), /* Normal maximum */
  .normal_min = (1150 >> 3), /* Normal minimum */
  .sensor_max_reading = 0xFF, /* Sensor Maximum reading */
  .sensor_min_reading = 0x00, /* Sensor Minimum reading */
  .upper_nonrecover_thr = (1400 >> 3), /* Upper non-recoverable Threshold */
  .upper_critical_thr = (1350 >> 3), /* Upper critical Threshold */
  .upper_noncritical_thr = (1300 >> 3), /* Upper non critical Threshold */
  .lower_nonrecover_thr = (1000 >> 3), /* Lower non-recoverable Threshold */
  .lower_critical_thr = (1050 >> 3), /* Lower critical Threshold */
  .lower_noncritical_thr = (1100 >> 3), /* Lower non-critical Threshold */
  .pos_thr_hysteresis = 2, /* positive going Threshold hysteresis value */
  .neg_thr_hysteresis = 2, /* negative going Threshold hysteresis value */
  .reserved1 = 0x00, /* reserved */
  .reserved2 = 0x00, /* reserved */
  .OEM = UCD_DDR_1V2T_ID, /* OEM reserved */
  .IDtypelen = 0xc0 | STR_SIZE(SDR_AMC_DDR_1V2T_ID), /* 8 bit ASCII, number of bytes */
  .IDstring = SDR_AMC_DDR_1V2T_ID /* sensor string */
};

/* AMC DDR +1.2V BOTTOM */
const SDR_type_01h_t SDR_AMC_DDR_1V2B = {
  .hdr.recID_LSB = 0x00, /* Filled by sdr_insert_entry() */
  .hdr.recID_MSB = 0x00,
  .hdr.SDRversion = 0x51,
  .hdr.rectype = TYPE_01,
  .hdr.reclength = sizeof(SDR_type_01h_t) - sizeof(SDR_entry_hdr_t),

  .ownerID = 0x00, /* i2c address, -> SDR_Init */
  .ownerLUN = 0x00, /* sensor owner LUN */
  .sensornum = 0x00, /* Filled by sdr_insert_entry() */

  /* record body bytes */
  .entityID = 0xC1, /* entity id: AMC Module */
  .entityinstance = 0x00, /* entity instance -> SDR_Init */
  .sensorinit = 0x7F, /* init: event generation + scanning enabled */
  .sensorcap = 0x56, /* capabilities: auto re-arm,*/
  .sensortype = SENSOR_TYPE_VOLTAGE, /* sensor type: VOLTAGE*/
  .event_reading_type = 0x01, /* sensor reading*/
  .assertion_event_mask = 0x7A95, /* assertion event mask (All upper going-high and lower going-low events) */
  .deassertion_event_mask = 0x7A80, /* deassertion event mask (All upper going-high events) */
  .readable_threshold_mask = 0x3F, /* LSB: readable Threshold mask: all thresholds are readable:  */
  .settable_threshold_mask = 0x00, /* MSB: setable Threshold mask: none of the thresholds are setable: */
  .sensor_units_1 = 0x00, /* sensor units 1 :*/
  .sensor_units_2 = 0x04, /* sensor units 2 :*/
  .sensor_units_3 = 0x00, /* sensor units 3 :*/
  .linearization = 0x00, /* Linearization */
  .M = 8, /* M */
  .M_tol = 0x00, /* M - Tolerance */
  .B = 0x00, /* B */
  .B_accuracy = 0x00, /* B - Accuracy */
  .acc_exp_sensor_dir = 0x00, /* Sensor direction */
  .Rexp_Bexp = 0xD0, /* R-Exp , B-Exp */
  .analog_flags = 0x07, /* Analogue characteristics flags */
  .nominal_reading = (1200 >> 3), /* Nominal reading */
  .normal_max = (1250 >> 3), /* Normal maximum */
  .normal_min = (1150 >> 3), /* Normal minimum */
  .sensor_max_reading = 0xFF, /* Sensor Maximum reading */
  .sensor_min_reading = 0x00, /* Sensor Minimum reading */
  .upper_nonrecover_thr = (1400 >> 3), /* Upper non-recoverable Threshold */
  .upper_critical_thr = (1350 >> 3), /* Upper critical Threshold */
  .upper_noncritical_thr = (1300 >> 3), /* Upper non critical Threshold */
  .lower_nonrecover_thr = (1000 >> 3), /* Lower non-recoverable Threshold */
  .lower_critical_thr = (1050 >> 3), /* Lower critical Threshold */
  .lower_noncritical_thr = (1100 >> 3), /* Lower non-critical Threshold */
  .pos_thr_hysteresis = 2, /* positive going Threshold hysteresis value */
  .neg_thr_hysteresis = 2, /* negative going Threshold hysteresis value */
  .reserved1 = 0x00, /* reserved */
  .reserved2 = 0x00, /* reserved */
  .OEM = UCD_DDR_1V2B_ID, /* OEM reserved */
  .IDtypelen = 0xc0 | STR_SIZE(SDR_AMC_DDR_1V2B_ID), /* 8 bit ASCII, number of bytes */
  .IDstring = SDR_AMC_DDR_1V2B_ID /* sensor string */
};

/* AMC IO 3V3 */
const SDR_type_01h_t SDR_AMC_IO_3V3 = {
  .hdr.recID_LSB = 0x00, /* Filled by sdr_insert_entry() */
  .hdr.recID_MSB = 0x00,
  .hdr.SDRversion = 0x51,
  .hdr.rectype = TYPE_01,
  .hdr.reclength = sizeof(SDR_type_01h_t) - sizeof(SDR_entry_hdr_t),

  .ownerID = 0x00, /* i2c address, -> SDR_Init */
  .ownerLUN = 0x00, /* sensor owner LUN */
  .sensornum = 0x00, /* Filled by sdr_insert_entry() */

  /* record body bytes */
  .entityID = 0xC1, /* entity id: AMC Module */
  .entityinstance = 0x00, /* entity instance -> SDR_Init */
  .sensorinit = 0x7F, /* init: event generation + scanning enabled */
  .sensorcap = 0x56, /* capabilities: auto re-arm,*/
  .sensortype = SENSOR_TYPE_VOLTAGE, /* sensor type: VOLTAGE*/
  .event_reading_type = 0x01, /* sensor reading*/
  .assertion_event_mask = 0x7A95, /* assertion event mask (All upper going-high and lower going-low events) */
  .deassertion_event_mask = 0x7A80, /* deassertion event mask (All upper going-high events) */
  .readable_threshold_mask = 0x3F, /* LSB: readable Threshold mask: all thresholds are readable:  */
  .settable_threshold_mask = 0x00, /* MSB: setable Threshold mask: none of the thresholds are setable: */
  .sensor_units_1 = 0x00, /* sensor units 1 :*/
  .sensor_units_2 = 0x04, /* sensor units 2 :*/
  .sensor_units_3 = 0x00, /* sensor units 3 :*/
  .linearization = 0x00, /* Linearization */
  .M = 16, /* M */
  .M_tol = 0x00, /* M - Tolerance */
  .B = 0x00, /* B */
  .B_accuracy = 0x00, /* B - Accuracy */
  .acc_exp_sensor_dir = 0x00, /* Sensor direction */
  .Rexp_Bexp = 0xD0, /* R-Exp , B-Exp */
  .analog_flags = 0x07, /* Analogue characteristics flags */
  .nominal_reading = (3300 >> 4), /* Nominal reading */
  .normal_max = (3500 >> 4), /* Normal maximum */
  .normal_min = (3000 >> 4), /* Normal minimum */
  .sensor_max_reading = 0xFF, /* Sensor Maximum reading */
  .sensor_min_reading = 0x00, /* Sensor Minimum reading */
  .upper_nonrecover_thr = (3700 >> 4), /* Upper non-recoverable Threshold */
  .upper_critical_thr = (3600 >> 4), /* Upper critical Threshold */
  .upper_noncritical_thr = (3550 >> 4), /* Upper non critical Threshold */
  .lower_nonrecover_thr = (2800 >> 4), /* Lower non-recoverable Threshold */
  .lower_critical_thr = (2900 >> 4), /* Lower critical Threshold */
  .lower_noncritical_thr = (2950 >> 4), /* Lower non-critical Threshold */
  .pos_thr_hysteresis = 2, /* positive going Threshold hysteresis value */
  .neg_thr_hysteresis = 2, /* negative going Threshold hysteresis value */
  .reserved1 = 0x00, /* reserved */
  .reserved2 = 0x00, /* reserved */
  .OEM = UCD_AMC_IO_3V3_ID, /* OEM reserved */
  .IDtypelen = 0xc0 | STR_SIZE(SDR_AMC_IO_3V3_ID), /* 8 bit ASCII, number of bytes */
  .IDstring = SDR_AMC_IO_3V3_ID /* sensor string */
};


/* RTM 12V Current */
const SDR_type_01h_t SDR_RTM_12V_CURR = {

  .hdr.recID_LSB = 0x00, /* Filled by sdr_insert_entry() */
  .hdr.recID_MSB = 0x00,
  .hdr.SDRversion = 0x51,
  .hdr.rectype = TYPE_01,
  .hdr.reclength = sizeof(SDR_type_01h_t) - sizeof(SDR_entry_hdr_t),

  .ownerID = 0x00, /* i2c address, -> SDR_Init */
  .ownerLUN = 0x00, /* sensor owner LUN */
  .sensornum = 0x00, /* Filled by sdr_insert_entry() */

  /* record body bytes */
  .entityID = 0xC1, /* entity id: AMC Module */
  .entityinstance = 0x00, /* entity instance -> SDR_Init */
  .sensorinit = 0x7F, /* init: event generation + scanning enabled */
  .sensorcap = 0x56, /* capabilities: auto re-arm */
  .sensortype = SENSOR_TYPE_CURRENT, /* sensor type: Current */
  .event_reading_type = 0x01, /* sensor reading*/
  .assertion_event_mask = 0x7A80, /* assertion event mask (All upper going-high and lower going-low events) */
  .deassertion_event_mask = 0x7A80, /* deassertion event mask (All upper going-high events) */
  .readable_threshold_mask = 0x3F, /* LSB: readable Threshold mask: all thresholds are readable:  */
  .settable_threshold_mask = 0x00, /* MSB: setable Threshold mask: none of the thresholds are setable: */
  .sensor_units_1 = 0x80, /* sensor units 1 :*/
  .sensor_units_2 = 0x05, /* sensor units 2 :*/
  .sensor_units_3 = 0x00, /* sensor units 3 :*/
  .linearization = 0x00, /* Linearization */
  .M = 64, /* M */
  .M_tol = 0x00, /* M - Tolerance */
  .B = 0x00, /* B */
  .B_accuracy = 0x00, /* B - Accuracy */
  .acc_exp_sensor_dir = 0x02, /* Sensor direction */
  .Rexp_Bexp = 0xD0, /* R-Exp , B-Exp */
  .analog_flags = 0x02, /* Analogue characteristics flags */
  .nominal_reading = (uint8_t)(1500 / 64), /* Nominal reading */
  .normal_max = (uint8_t)(3000 / 64), /* Normal maximum */
  .normal_min = 0, /* Normal minimum */
  .sensor_max_reading = 0xFF, /* Sensor Maximum reading */
  .sensor_min_reading = 0x00, /* Sensor Minimum reading */
  .upper_nonrecover_thr = (uint8_t)(4500 / 64), /* Upper non-recoverable Threshold */
  .upper_critical_thr = (uint8_t)(4000 / 64), /* Upper critical Threshold */
  .upper_noncritical_thr = (uint8_t)(3500 / 64), /* Upper non critical Threshold */
  .lower_nonrecover_thr = (uint8_t)(-1000 / 64), /* Lower non-recoverable Threshold */
  .lower_critical_thr = (uint8_t)(-500 / 64), /* Lower critical Threshold */
  .lower_noncritical_thr = (uint8_t)(0 / 64), /* Lower non-critical Threshold */
  .pos_thr_hysteresis = 2, /* positive going Threshold hysteresis value */
  .neg_thr_hysteresis = 2, /* negative going Threshold hysteresis value */
  .reserved1 = 0x00, /* reserved */
  .reserved2 = 0x00, /* reserved */
  .OEM = UCD_RTM_12V_CURR_ID, /* OEM reserved */
  .IDtypelen = 0xc0 | STR_SIZE(SDR_RTM_12V_CURR_ID) , /* 8 bit ASCII, number of bytes */
  .IDstring = SDR_RTM_12V_CURR_ID /* sensor string */
};

/* FMC0 12V Current */
const SDR_type_01h_t SDR_FMC0_12V_CURR = {
  .hdr.recID_LSB = 0x00, /* Filled by sdr_insert_entry() */
  .hdr.recID_MSB = 0x00,
  .hdr.SDRversion = 0x51,
  .hdr.rectype = TYPE_01,
  .hdr.reclength = sizeof(SDR_type_01h_t) - sizeof(SDR_entry_hdr_t),

  .ownerID = 0x00, /* i2c address, -> SDR_Init */
  .ownerLUN = 0x00, /* sensor owner LUN */
  .sensornum = 0x00, /* Filled by sdr_insert_entry() */

  /* record body bytes */
  .entityID = 0xC1, /* entity id: AMC Module */
  .entityinstance = 0x00, /* entity instance -> SDR_Init */
  .sensorinit = 0x7F, /* init: event generation + scanning enabled */
  .sensorcap = 0x56, /* capabilities: auto re-arm,*/
  .sensortype = SENSOR_TYPE_CURRENT, /* sensor type: Current */
  .event_reading_type = 0x01, /* sensor reading */
  .assertion_event_mask = 0x7A80, /* assertion event mask (All upper going-high and lower going-low events) */
  .deassertion_event_mask = 0x7A80, /* deassertion event mask (All upper going-high events) */
  .readable_threshold_mask = 0x3F, /* LSB: readable Threshold mask: all thresholds are readable:  */
  .settable_threshold_mask = 0x00, /* MSB: setable Threshold mask: none of the thresholds are setable: */
  .sensor_units_1 = 0x80, /* sensor units 1 :*/
  .sensor_units_2 = 0x05, /* sensor units 2 :*/
  .sensor_units_3 = 0x00, /* sensor units 3 :*/
  .linearization = 0x00, /* Linearization */
  .M = 32, /* M */
  .M_tol = 0x00, /* M - Tolerance */
  .B = 0x00, /* B */
  .B_accuracy = 0x00, /* B - Accuracy */
  .acc_exp_sensor_dir = 0x02, /* Sensor direction */
  .Rexp_Bexp = 0xD0, /* R-Exp , B-Exp */
  .analog_flags = 0x02, /* Analogue characteristics flags */
  .nominal_reading = (uint8_t)(500 / 32), /* Nominal reading */
  .normal_max = (uint8_t)(1000 / 32), /* Normal maximum */
  .normal_min = 0, /* Normal minimum */
  .sensor_max_reading = 0xFF, /* Sensor Maximum reading */
  .sensor_min_reading = 0x00, /* Sensor Minimum reading */
  .upper_nonrecover_thr = (uint8_t)(3000 / 32), /* Upper non-recoverable Threshold */
  .upper_critical_thr = (uint8_t)(2500 / 32), /* Upper critical Threshold */
  .upper_noncritical_thr = (uint8_t)(1500 / 32), /* Upper non critical Threshold */
  .lower_nonrecover_thr = (uint8_t)(-1000 / 32), /* Lower non-recoverable Threshold */
  .lower_critical_thr = (uint8_t)(-500 / 32), /* Lower critical Threshold */
  .lower_noncritical_thr =  (uint8_t)(0 / 32), /* Lower non-critical Threshold */
  .pos_thr_hysteresis = 2, /* positive going Threshold hysteresis value */
  .neg_thr_hysteresis = 2, /* negative going Threshold hysteresis value */
  .reserved1 = 0x00, /* reserved */
  .reserved2 = 0x00, /* reserved */
  .OEM = UCD_FMC0_12V_CURR_ID, /* OEM reserved */
  .IDtypelen = 0xc0 | STR_SIZE(SDR_FMC0_12V_CURR_ID) , /* 8 bit ASCII, number of bytes */
  .IDstring = SDR_FMC0_12V_CURR_ID /* sensor string */
};

/* FMC0 PVADJ Current */
const SDR_type_01h_t SDR_FMC0_VADJ_CURR = {
  .hdr.recID_LSB = 0x00, /* Filled by sdr_insert_entry() */
  .hdr.recID_MSB = 0x00,
  .hdr.SDRversion = 0x51,
  .hdr.rectype = TYPE_01,
  .hdr.reclength = sizeof(SDR_type_01h_t) - sizeof(SDR_entry_hdr_t),

  .ownerID = 0x00, /* i2c address, -> SDR_Init */
  .ownerLUN = 0x00, /* sensor owner LUN */
  .sensornum = 0x00, /* Filled by sdr_insert_entry() */

  /* record body bytes */
  .entityID = 0xC1, /* entity id: AMC Module */
  .entityinstance = 0x00, /* entity instance -> SDR_Init */
  .sensorinit = 0x7F, /* init: event generation + scanning enabled */
  .sensorcap = 0x56, /* capabilities: auto re-arm */
  .sensortype = SENSOR_TYPE_CURRENT, /* sensor type: CURRENT */
  .event_reading_type = 0x01, /* sensor reading*/
  .assertion_event_mask = 0x7A80, /* assertion event mask (All upper going-high and lower going-low events) */
  .deassertion_event_mask = 0x7A80, /* deassertion event mask (All upper going-high events) */
  .readable_threshold_mask = 0x3F, /* LSB: readable Threshold mask: all thresholds are readable:  */
  .settable_threshold_mask = 0x00, /* MSB: setable Threshold mask: none of the thresholds are setable: */
  .sensor_units_1 = 0x80, /* sensor units 1 :*/
  .sensor_units_2 = 0x05, /* sensor units 2 :*/
  .sensor_units_3 = 0x00, /* sensor units 3 :*/
  .linearization = 0x00, /* Linearization */
  .M = 64, /* M */
  .M_tol = 0x00, /* M - Tolerance */
  .B = 0x00, /* B */
  .B_accuracy = 0x00, /* B - Accuracy */
  .acc_exp_sensor_dir = 0x02, /* Sensor direction */
  .Rexp_Bexp = 0xD0, /* R-Exp , B-Exp */
  .analog_flags = 0x02, /* Analogue characteristics flags */
  .nominal_reading = (uint8_t)(1500 / 64), /* Nominal reading */
  .normal_max = (uint8_t)(4000 / 64), /* Normal maximum */
  .normal_min = 0, /* Normal minimum */
  .sensor_max_reading = 0xFF, /* Sensor Maximum reading */
  .sensor_min_reading = 0x00, /* Sensor Minimum reading */
  .upper_nonrecover_thr = (uint8_t)(5000 / 64), /* Upper non-recoverable Threshold */
  .upper_critical_thr = (uint8_t)(4600 / 64), /* Upper critical Threshold */
  .upper_noncritical_thr = (uint8_t)(4300 / 64), /* Upper non critical Threshold */
  .lower_nonrecover_thr = (uint8_t)(-1000 / 64), /* Lower non-recoverable Threshold */
  .lower_critical_thr = (uint8_t)(-500 / 64), /* Lower critical Threshold */
  .lower_noncritical_thr =  (uint8_t)(0 / 64), /* Lower non-critical Threshold */
  .pos_thr_hysteresis = 2, /* positive going Threshold hysteresis value */
  .neg_thr_hysteresis = 2, /* negative going Threshold hysteresis value */
  .reserved1 = 0x00, /* reserved */
  .reserved2 = 0x00, /* reserved */
  .OEM = UCD_FMC0_VADJ_ID, /* OEM reserved */
  .IDtypelen = 0xc0 | STR_SIZE(SDR_FMC0_VADJ_CURR_ID), /* 8 bit ASCII, number of bytes */
  .IDstring = SDR_FMC0_VADJ_CURR_ID /* sensor string */
};

/* FMC0 P3V3 Current */
const SDR_type_01h_t SDR_FMC0_P3V3_CURR = {
  .hdr.recID_LSB = 0x00, /* Filled by sdr_insert_entry() */
  .hdr.recID_MSB = 0x00,
  .hdr.SDRversion = 0x51,
  .hdr.rectype = TYPE_01,
  .hdr.reclength = sizeof(SDR_type_01h_t) - sizeof(SDR_entry_hdr_t),

  .ownerID = 0x00, /* i2c address, -> SDR_Init */
  .ownerLUN = 0x00, /* sensor owner LUN */
  .sensornum = 0x00, /* Filled by sdr_insert_entry() */

  /* record body bytes */
  .entityID = 0xC1, /* entity id: AMC Module */
  .entityinstance = 0x00, /* entity instance -> SDR_Init */
  .sensorinit = 0x7F, /* init: event generation + scanning enabled */
  .sensorcap = 0x56, /* capabilities: auto re-arm,*/
  .sensortype = SENSOR_TYPE_CURRENT, /* sensor type: CURRENT */
  .event_reading_type = 0x01, /* sensor reading */
  .assertion_event_mask = 0x7A80, /* assertion event mask (All upper going-high and lower going-low events) */
  .deassertion_event_mask = 0x7A80, /* deassertion event mask (All upper going-high events) */
  .readable_threshold_mask = 0x3F, /* LSB: readable Threshold mask: all thresholds are readable:  */
  .settable_threshold_mask = 0x00, /* MSB: setable Threshold mask: none of the thresholds are setable: */
  .sensor_units_1 = 0x80, /* sensor units 1 :*/
  .sensor_units_2 = 0x05, /* sensor units 2 :*/
  .sensor_units_3 = 0x00, /* sensor units 3 :*/
  .linearization = 0x00, /* Linearization */
  .M = 32, /* M */
  .M_tol = 0x00, /* M - Tolerance */
  .B = 0x00, /* B */
  .B_accuracy = 0x00, /* B - Accuracy */
  .acc_exp_sensor_dir = 0x02, /* Sensor direction */
  .Rexp_Bexp = 0xD0, /* R-Exp , B-Exp */
  .analog_flags = 0x02, /* Analogue characteristics flags */
  .nominal_reading = (uint8_t)(1500 / 32), /* Nominal reading */
  .normal_max = (uint8_t)(3000 / 32), /* Normal maximum */
  .normal_min = 0, /* Normal minimum */
  .sensor_max_reading = 0xFF, /* Sensor Maximum reading */
  .sensor_min_reading = 0x00, /* Sensor Minimum reading */
  .upper_nonrecover_thr = (uint8_t)(4000 / 32), /* Upper non-recoverable Threshold */
  .upper_critical_thr = (uint8_t)(3600 / 32), /* Upper critical Threshold */
  .upper_noncritical_thr = (uint8_t)(3300 / 32), /* Upper non critical Threshold */
  .lower_nonrecover_thr = (uint8_t)(-1000 / 32), /* Lower non-recoverable Threshold */
  .lower_critical_thr = (uint8_t)(-500 / 32), /* Lower critical Threshold */
  .lower_noncritical_thr =  (uint8_t)(0 / 32), /* Lower non-critical Threshold */
  .pos_thr_hysteresis = 2, /* positive going Threshold hysteresis value */
  .neg_thr_hysteresis = 2, /* negative going Threshold hysteresis value */
  .reserved1 = 0x00, /* reserved */
  .reserved2 = 0x00, /* reserved */
  .OEM = UCD_FMC0_3V3_CURR_ID, /* OEM reserved */
  .IDtypelen = 0xc0 | STR_SIZE(SDR_FMC0_3V3_CURR_ID), /* 8 bit ASCII, number of bytes */
  .IDstring = SDR_FMC0_3V3_CURR_ID /* sensor string */
};

/* FMC1 12V Current */
const SDR_type_01h_t SDR_FMC1_12V_CURR = {
  .hdr.recID_LSB = 0x00, /* Filled by sdr_insert_entry() */
  .hdr.recID_MSB = 0x00,
  .hdr.SDRversion = 0x51,
  .hdr.rectype = TYPE_01,
  .hdr.reclength = sizeof(SDR_type_01h_t) - sizeof(SDR_entry_hdr_t),

  .ownerID = 0x00, /* i2c address, -> SDR_Init */
  .ownerLUN = 0x00, /* sensor owner LUN */
  .sensornum = 0x00, /* Filled by sdr_insert_entry() */

  /* record body bytes */
  .entityID = 0xC1, /* entity id: AMC Module */
  .entityinstance = 0x00, /* entity instance -> SDR_Init */
  .sensorinit = 0x7F, /* init: event generation + scanning enabled */
  .sensorcap = 0x56, /* capabilities: auto re-arm,*/
  .sensortype = SENSOR_TYPE_CURRENT, /* sensor type: Current */
  .event_reading_type = 0x01, /* sensor reading */
  .assertion_event_mask = 0x7A80, /* assertion event mask (All upper going-high and lower going-low events) */
  .deassertion_event_mask = 0x7A80, /* deassertion event mask (All upper going-high events) */
  .readable_threshold_mask = 0x3F, /* LSB: readable Threshold mask: all thresholds are readable:  */
  .settable_threshold_mask = 0x00, /* MSB: setable Threshold mask: none of the thresholds are setable: */
  .sensor_units_1 = 0x80, /* sensor units 1 :*/
  .sensor_units_2 = 0x05, /* sensor units 2 :*/
  .sensor_units_3 = 0x00, /* sensor units 3 :*/
  .linearization = 0x00, /* Linearization */
  .M = 32, /* M */
  .M_tol = 0x00, /* M - Tolerance */
  .B = 0x00, /* B */
  .B_accuracy = 0x00, /* B - Accuracy */
  .acc_exp_sensor_dir = 0x02, /* Sensor direction */
  .Rexp_Bexp = 0xD0, /* R-Exp , B-Exp */
  .analog_flags = 0x02, /* Analogue characteristics flags */
  .nominal_reading = (uint8_t)(500 / 32), /* Nominal reading */
  .normal_max = (uint8_t)(1000 / 32), /* Normal maximum */
  .normal_min = 0, /* Normal minimum */
  .sensor_max_reading = 0xFF, /* Sensor Maximum reading */
  .sensor_min_reading = 0x00, /* Sensor Minimum reading */
  .upper_nonrecover_thr = (uint8_t)(3000 / 32), /* Upper non-recoverable Threshold */
  .upper_critical_thr = (uint8_t)(2500 / 32), /* Upper critical Threshold */
  .upper_noncritical_thr = (uint8_t)(1500 / 32), /* Upper non critical Threshold */
  .lower_nonrecover_thr = (uint8_t)(-1000 / 32), /* Lower non-recoverable Threshold */
  .lower_critical_thr = (uint8_t)(-500 / 32), /* Lower critical Threshold */
  .lower_noncritical_thr =  (uint8_t)(0 / 32), /* Lower non-critical Threshold */
  .pos_thr_hysteresis = 2, /* positive going Threshold hysteresis value */
  .neg_thr_hysteresis = 2, /* negative going Threshold hysteresis value */
  .reserved1 = 0x00, /* reserved */
  .reserved2 = 0x00, /* reserved */
  .OEM = UCD_FMC1_12V_CURR_ID, /* OEM reserved */
  .IDtypelen = 0xc0 | STR_SIZE(SDR_FMC1_12V_CURR_ID) , /* 8 bit ASCII, number of bytes */
  .IDstring = SDR_FMC1_12V_CURR_ID /* sensor string */
};

/* FMC1 PVADJ Current */
const SDR_type_01h_t SDR_FMC1_VADJ_CURR = {
  .hdr.recID_LSB = 0x00, /* Filled by sdr_insert_entry() */
  .hdr.recID_MSB = 0x00,
  .hdr.SDRversion = 0x51,
  .hdr.rectype = TYPE_01,
  .hdr.reclength = sizeof(SDR_type_01h_t) - sizeof(SDR_entry_hdr_t),

  .ownerID = 0x00, /* i2c address, -> SDR_Init */
  .ownerLUN = 0x00, /* sensor owner LUN */
  .sensornum = 0x00, /* Filled by sdr_insert_entry() */

  /* record body bytes */
  .entityID = 0xC1, /* entity id: AMC Module */
  .entityinstance = 0x00, /* entity instance -> SDR_Init */
  .sensorinit = 0x7F, /* init: event generation + scanning enabled */
  .sensorcap = 0x56, /* capabilities: auto re-arm */
  .sensortype = SENSOR_TYPE_CURRENT, /* sensor type: CURRENT */
  .event_reading_type = 0x01, /* sensor reading*/
  .assertion_event_mask = 0x7A80, /* assertion event mask (All upper going-high and lower going-low events) */
  .deassertion_event_mask = 0x7A80, /* deassertion event mask (All upper going-high events) */
  .readable_threshold_mask = 0x3F, /* LSB: readable Threshold mask: all thresholds are readable:  */
  .settable_threshold_mask = 0x00, /* MSB: setable Threshold mask: none of the thresholds are setable: */
  .sensor_units_1 = 0x80, /* sensor units 1 :*/
  .sensor_units_2 = 0x05, /* sensor units 2 :*/
  .sensor_units_3 = 0x00, /* sensor units 3 :*/
  .linearization = 0x00, /* Linearization */
  .M = 64, /* M */
  .M_tol = 0x00, /* M - Tolerance */
  .B = 0x00, /* B */
  .B_accuracy = 0x00, /* B - Accuracy */
  .acc_exp_sensor_dir = 0x02, /* Sensor direction */
  .Rexp_Bexp = 0xD0, /* R-Exp , B-Exp */
  .analog_flags = 0x02, /* Analogue characteristics flags */
  .nominal_reading = (uint8_t)(1500 / 64), /* Nominal reading */
  .normal_max = (uint8_t)(4000 / 64), /* Normal maximum */
  .normal_min = 0, /* Normal minimum */
  .sensor_max_reading = 0xFF, /* Sensor Maximum reading */
  .sensor_min_reading = 0x00, /* Sensor Minimum reading */
  .upper_nonrecover_thr = (uint8_t)(5000 / 64), /* Upper non-recoverable Threshold */
  .upper_critical_thr = (uint8_t)(4600 / 64), /* Upper critical Threshold */
  .upper_noncritical_thr = (uint8_t)(4300 / 64), /* Upper non critical Threshold */
  .lower_nonrecover_thr = (uint8_t)(-1000 / 64), /* Lower non-recoverable Threshold */
  .lower_critical_thr = (uint8_t)(-500 / 64), /* Lower critical Threshold */
  .lower_noncritical_thr =  (uint8_t)(0 / 64), /* Lower non-critical Threshold */
  .pos_thr_hysteresis = 2, /* positive going Threshold hysteresis value */
  .neg_thr_hysteresis = 2, /* negative going Threshold hysteresis value */
  .reserved1 = 0x00, /* reserved */
  .reserved2 = 0x00, /* reserved */
  .OEM = UCD_FMC1_VADJ_ID, /* OEM reserved */
  .IDtypelen = 0xc0 | STR_SIZE(SDR_FMC1_VADJ_CURR_ID), /* 8 bit ASCII, number of bytes */
  .IDstring = SDR_FMC1_VADJ_CURR_ID /* sensor string */
};

/* FMC1 P3V3 Current */
const SDR_type_01h_t SDR_FMC1_P3V3_CURR = {
  .hdr.recID_LSB = 0x00, /* Filled by sdr_insert_entry() */
  .hdr.recID_MSB = 0x00,
  .hdr.SDRversion = 0x51,
  .hdr.rectype = TYPE_01,
  .hdr.reclength = sizeof(SDR_type_01h_t) - sizeof(SDR_entry_hdr_t),

  .ownerID = 0x00, /* i2c address, -> SDR_Init */
  .ownerLUN = 0x00, /* sensor owner LUN */
  .sensornum = 0x00, /* Filled by sdr_insert_entry() */

  /* record body bytes */
  .entityID = 0xC1, /* entity id: AMC Module */
  .entityinstance = 0x00, /* entity instance -> SDR_Init */
  .sensorinit = 0x7F, /* init: event generation + scanning enabled */
  .sensorcap = 0x56, /* capabilities: auto re-arm,*/
  .sensortype = SENSOR_TYPE_CURRENT, /* sensor type: CURRENT */
  .event_reading_type = 0x01, /* sensor reading */
  .assertion_event_mask = 0x7A80, /* assertion event mask (All upper going-high and lower going-low events) */
  .deassertion_event_mask = 0x7A80, /* deassertion event mask (All upper going-high events) */
  .readable_threshold_mask = 0x3F, /* LSB: readable Threshold mask: all thresholds are readable:  */
  .settable_threshold_mask = 0x00, /* MSB: setable Threshold mask: none of the thresholds are setable: */
  .sensor_units_1 = 0x80, /* sensor units 1 :*/
  .sensor_units_2 = 0x05, /* sensor units 2 :*/
  .sensor_units_3 = 0x00, /* sensor units 3 :*/
  .linearization = 0x00, /* Linearization */
  .M = 32, /* M */
  .M_tol = 0x00, /* M - Tolerance */
  .B = 0x00, /* B */
  .B_accuracy = 0x00, /* B - Accuracy */
  .acc_exp_sensor_dir = 0x02, /* Sensor direction */
  .Rexp_Bexp = 0xD0, /* R-Exp , B-Exp */
  .analog_flags = 0x02, /* Analogue characteristics flags */
  .nominal_reading = (uint8_t)(1500 / 32), /* Nominal reading */
  .normal_max = (uint8_t)(3000 / 32), /* Normal maximum */
  .normal_min = 0, /* Normal minimum */
  .sensor_max_reading = 0xFF, /* Sensor Maximum reading */
  .sensor_min_reading = 0x00, /* Sensor Minimum reading */
  .upper_nonrecover_thr = (uint8_t)(4000 / 32), /* Upper non-recoverable Threshold */
  .upper_critical_thr = (uint8_t)(3600 / 32), /* Upper critical Threshold */
  .upper_noncritical_thr = (uint8_t)(3300 / 32), /* Upper non critical Threshold */
  .lower_nonrecover_thr = (uint8_t)(-1000 / 32), /* Lower non-recoverable Threshold */
  .lower_critical_thr = (uint8_t)(-500 / 32), /* Lower critical Threshold */
  .lower_noncritical_thr =  (uint8_t)(0 / 32), /* Lower non-critical Threshold */
  .pos_thr_hysteresis = 2, /* positive going Threshold hysteresis value */
  .neg_thr_hysteresis = 2, /* negative going Threshold hysteresis value */
  .reserved1 = 0x00, /* reserved */
  .reserved2 = 0x00, /* reserved */
  .OEM = UCD_FMC1_3V3_CURR_ID, /* OEM reserved */
  .IDtypelen = 0xc0 | STR_SIZE(SDR_FMC1_3V3_CURR_ID), /* 8 bit ASCII, number of bytes */
  .IDstring = SDR_FMC1_3V3_CURR_ID /* sensor string */
};

/* AMC FPGA 0V85 Current */
const SDR_type_01h_t SDR_AMC_FPGA_0V85_CURR = {
  .hdr.recID_LSB = 0x00, /* Filled by sdr_insert_entry() */
  .hdr.recID_MSB = 0x00,
  .hdr.SDRversion = 0x51,
  .hdr.rectype = TYPE_01,
  .hdr.reclength = sizeof(SDR_type_01h_t) - sizeof(SDR_entry_hdr_t),

  .ownerID = 0x00, /* i2c address, -> SDR_Init */
  .ownerLUN = 0x00, /* sensor owner LUN */
  .sensornum = 0x00, /* Filled by sdr_insert_entry() */

  /* record body bytes */
  .entityID = 0xC1, /* entity id: AMC Module */
  .entityinstance = 0x00, /* entity instance -> SDR_Init */
  .sensorinit = 0x7F, /* init: event generation + scanning enabled */
  .sensorcap = 0x56, /* capabilities: auto re-arm */
  .sensortype = SENSOR_TYPE_CURRENT, /* sensor type: CURRENT */
  .event_reading_type = 0x01, /* sensor reading*/
  .assertion_event_mask = 0x7A80, /* assertion event mask (All upper going-high and lower going-low events) */
  .deassertion_event_mask = 0x7A80, /* deassertion event mask (All upper going-high events) */
  .readable_threshold_mask = 0x3F, /* LSB: readable Threshold mask: all thresholds are readable:  */
  .settable_threshold_mask = 0x00, /* MSB: setable Threshold mask: none of the thresholds are setable: */
  .sensor_units_1 = 0x80, /* sensor units 1 :*/
  .sensor_units_2 = 0x05, /* sensor units 2 :*/
  .sensor_units_3 = 0x00, /* sensor units 3 :*/
  .linearization = 0x00, /* Linearization */
  .M = 250, /* M */
  .M_tol = 0x00, /* M - Tolerance */
  .B = 0x00, /* B */
  .B_accuracy = 0x00, /* B - Accuracy */
  .acc_exp_sensor_dir = 0x02, /* Sensor direction */
  .Rexp_Bexp = 0xD0, /* R-Exp , B-Exp */
  .analog_flags = 0x02, /* Analogue characteristics flags */
  .nominal_reading = (uint8_t)(10000 / 250), /* Nominal reading */
  .normal_max = (uint8_t)(30000 / 250), /* Normal maximum */
  .normal_min = 0, /* Normal minimum - 0A */
  .sensor_max_reading = 0xFF, /* Sensor Maximum reading */
  .sensor_min_reading = 0x00, /* Sensor Minimum reading */
  .upper_nonrecover_thr = (uint8_t)(31750 / 250), /* Upper non-recoverable Threshold */
  .upper_critical_thr = (uint8_t)(31000 / 250), /* Upper critical Threshold */
  .upper_noncritical_thr = (uint8_t)(30500 / 250), /* Upper non critical Threshold */
  .lower_nonrecover_thr = (uint8_t)(-1000 / 250), /* Lower non-recoverable Threshold */
  .lower_critical_thr = (uint8_t)(-500 / 250), /* Lower critical Threshold */
  .lower_noncritical_thr = (uint8_t)(0 / 250), /* Lower non-critical Threshold */
  .pos_thr_hysteresis = 2, /* positive going Threshold hysteresis value */
  .neg_thr_hysteresis = 2, /* negative going Threshold hysteresis value */
  .reserved1 = 0x00, /* reserved */
  .reserved2 = 0x00, /* reserved */
  .OEM = UCD_FPGA_0V85_ID, /* OEM reserved */
  .IDtypelen = 0xc0 | STR_SIZE(SDR_AMC_FPGA_0V85_CURR_ID), /* 8 bit ASCII, number of bytes */
  .IDstring = SDR_AMC_FPGA_0V85_CURR_ID /* sensor string */
};

/* AMC FPGA 1V8 Current */
const SDR_type_01h_t SDR_AMC_FPGA_1V8_CURR = {
  .hdr.recID_LSB = 0x00, /* Filled by sdr_insert_entry() */
  .hdr.recID_MSB = 0x00,
  .hdr.SDRversion = 0x51,
  .hdr.rectype = TYPE_01,
  .hdr.reclength = sizeof(SDR_type_01h_t) - sizeof(SDR_entry_hdr_t),

  .ownerID = 0x00, /* i2c address, -> SDR_Init */
  .ownerLUN = 0x00, /* sensor owner LUN */
  .sensornum = 0x00, /* Filled by sdr_insert_entry() */

  /* record body bytes */
  .entityID = 0xC1, /* entity id: AMC Module */
  .entityinstance = 0x00, /* entity instance -> SDR_Init */
  .sensorinit = 0x7F, /* init: event generation + scanning enabled */
  .sensorcap = 0x56, /* capabilities: auto re-arm,*/
  .sensortype = SENSOR_TYPE_CURRENT, /* sensor type: CURRENT */
  .event_reading_type = 0x01, /* sensor reading */
  .assertion_event_mask = 0x7A80, /* assertion event mask (All upper going-high and lower going-low events) */
  .deassertion_event_mask = 0x7A80, /* deassertion event mask (All upper going-high events) */
  .readable_threshold_mask = 0x3F, /* LSB: readable Threshold mask: all thresholds are readable:  */
  .settable_threshold_mask = 0x00, /* MSB: setable Threshold mask: none of the thresholds are setable: */
  .sensor_units_1 = 0x80, /* sensor units 1 :*/
  .sensor_units_2 = 0x05, /* sensor units 2 :*/
  .sensor_units_3 = 0x00, /* sensor units 3 :*/
  .linearization = 0x00, /* Linearization */
  .M = 128, /* M */
  .M_tol = 0x00, /* M - Tolerance */
  .B = 0x00, /* B */
  .B_accuracy = 0x00, /* B - Accuracy */
  .acc_exp_sensor_dir = 0x02, /* Sensor direction */
  .Rexp_Bexp = 0xD0, /* R-Exp , B-Exp */
  .analog_flags = 0x02, /* Analogue characteristics flags */
  .nominal_reading = (uint8_t)(1500 / 128), /* Nominal reading */
  .normal_max = (uint8_t)(10000 / 128), /* Normal maximum */
  .normal_min = 0, /* Normal minimum */
  .sensor_max_reading = 0xFF, /* Sensor Maximum reading */
  .sensor_min_reading = 0x00, /* Sensor Minimum reading */
  .upper_nonrecover_thr = (uint8_t)(11000 / 128), /* Upper non-recoverable Threshold */
  .upper_critical_thr = (uint8_t)(10600 / 128), /* Upper critical Threshold */
  .upper_noncritical_thr = (uint8_t)(10300 / 128), /* Upper non critical Threshold */
  .lower_nonrecover_thr = (uint8_t)(-1000 / 128), /* Lower non-recoverable Threshold */
  .lower_critical_thr = (uint8_t)(-500 / 128), /* Lower critical Threshold */
  .lower_noncritical_thr = (uint8_t)(0 / 128), /* Lower non-critical Threshold */
  .pos_thr_hysteresis = 2, /* positive going Threshold hysteresis value */
  .neg_thr_hysteresis = 2, /* negative going Threshold hysteresis value */
  .reserved1 = 0x00, /* reserved */
  .reserved2 = 0x00, /* reserved */
  .OEM = UCD_FPGA_1V8_ID, /* OEM reserved */
  .IDtypelen = 0xc0 | STR_SIZE(SDR_AMC_FPGA_1V8_CURR_ID), /* 8 bit ASCII, number of bytes */
  .IDstring = SDR_AMC_FPGA_1V8_CURR_ID /* sensor string */
};

/* AMC FPGA +0.9V Analog Current */
const SDR_type_01h_t SDR_AMC_FPGA_0V9A_CURR = {
  .hdr.recID_LSB = 0x00, /* Filled by sdr_insert_entry() */
  .hdr.recID_MSB = 0x00,
  .hdr.SDRversion = 0x51,
  .hdr.rectype = TYPE_01,
  .hdr.reclength = sizeof(SDR_type_01h_t) - sizeof(SDR_entry_hdr_t),

  .ownerID = 0x00, /* i2c address, -> SDR_Init */
  .ownerLUN = 0x00, /* sensor owner LUN */
  .sensornum = 0x00, /* Filled by sdr_insert_entry() */

  /* record body bytes */
  .entityID = 0xC1, /* entity id: AMC Module */
  .entityinstance = 0x00, /* entity instance -> SDR_Init */
  .sensorinit = 0x7F, /* init: event generation + scanning enabled */
  .sensorcap = 0x56, /* capabilities: auto re-arm,*/
  .sensortype = SENSOR_TYPE_CURRENT, /* sensor type: CURRENT */
  .event_reading_type = 0x01, /* sensor reading */
  .assertion_event_mask = 0x7A80, /* assertion event mask (All upper going-high and lower going-low events) */
  .deassertion_event_mask = 0x7A80, /* deassertion event mask (All upper going-high events) */
  .readable_threshold_mask = 0x3F, /* LSB: readable Threshold mask: all thresholds are readable:  */
  .settable_threshold_mask = 0x00, /* MSB: setable Threshold mask: none of the thresholds are setable: */
  .sensor_units_1 = 0x80, /* sensor units 1 :*/
  .sensor_units_2 = 0x05, /* sensor units 2 :*/
  .sensor_units_3 = 0x00, /* sensor units 3 :*/
  .linearization = 0x00, /* Linearization */
  .M = 128, /* M */
  .M_tol = 0x00, /* M - Tolerance */
  .B = 0x00, /* B */
  .B_accuracy = 0x00, /* B - Accuracy */
  .acc_exp_sensor_dir = 0x02, /* Sensor direction */
  .Rexp_Bexp = 0xD0, /* R-Exp , B-Exp */
  .analog_flags = 0x02, /* Analogue characteristics flags */
  .nominal_reading = (uint8_t)(1500 / 128), /* Nominal reading */
  .normal_max = (uint8_t)(10000 / 128), /* Normal maximum */
  .normal_min = 0, /* Normal minimum */
  .sensor_max_reading = 0xFF, /* Sensor Maximum reading */
  .sensor_min_reading = 0x00, /* Sensor Minimum reading */
  .upper_nonrecover_thr = (uint8_t)(11000 / 128), /* Upper non-recoverable Threshold */
  .upper_critical_thr = (uint8_t)(10600 / 128), /* Upper critical Threshold */
  .upper_noncritical_thr = (uint8_t)(10300 / 128), /* Upper non critical Threshold */
  .lower_nonrecover_thr = (uint8_t)(-1000 / 128), /* Lower non-recoverable Threshold */
  .lower_critical_thr = (uint8_t)(-500 / 128), /* Lower critical Threshold */
  .lower_noncritical_thr = (uint8_t)(0 / 128), /* Lower non-critical Threshold */
  .pos_thr_hysteresis = 2, /* positive going Threshold hysteresis value */
  .neg_thr_hysteresis = 2, /* negative going Threshold hysteresis value */
  .reserved1 = 0x00, /* reserved */
  .reserved2 = 0x00, /* reserved */
  .OEM = UCD_FPGA_0V9A_ID, /* OEM reserved */
  .IDtypelen = 0xc0 | STR_SIZE(SDR_AMC_FPGA_0V9A_CURR_ID), /* 8 bit ASCII, number of bytes */
  .IDstring = SDR_AMC_FPGA_0V9A_CURR_ID /* sensor string */
};

/* AMC FPGA +1.2V Analog Current */
const SDR_type_01h_t SDR_AMC_FPGA_1V2A_CURR = {
  .hdr.recID_LSB = 0x00, /* Filled by sdr_insert_entry() */
  .hdr.recID_MSB = 0x00,
  .hdr.SDRversion = 0x51,
  .hdr.rectype = TYPE_01,
  .hdr.reclength = sizeof(SDR_type_01h_t) - sizeof(SDR_entry_hdr_t),

  .ownerID = 0x00, /* i2c address, -> SDR_Init */
  .ownerLUN = 0x00, /* sensor owner LUN */
  .sensornum = 0x00, /* Filled by sdr_insert_entry() */

  /* record body bytes */
  .entityID = 0xC1, /* entity id: AMC Module */
  .entityinstance = 0x00, /* entity instance -> SDR_Init */
  .sensorinit = 0x7F, /* init: event generation + scanning enabled */
  .sensorcap = 0x56, /* capabilities: auto re-arm,*/
  .sensortype = SENSOR_TYPE_CURRENT, /* sensor type: CURRENT */
  .event_reading_type = 0x01, /* sensor reading */
  .assertion_event_mask = 0x7A80, /* assertion event mask (All upper going-high and lower going-low events) */
  .deassertion_event_mask = 0x7A80, /* deassertion event mask (All upper going-high events) */
  .readable_threshold_mask = 0x3F, /* LSB: readable Threshold mask: all thresholds are readable:  */
  .settable_threshold_mask = 0x00, /* MSB: setable Threshold mask: none of the thresholds are setable: */
  .sensor_units_1 = 0x80, /* sensor units 1 :*/
  .sensor_units_2 = 0x05, /* sensor units 2 :*/
  .sensor_units_3 = 0x00, /* sensor units 3 :*/
  .linearization = 0x00, /* Linearization */
  .M = 128, /* M */
  .M_tol = 0x00, /* M - Tolerance */
  .B = 0x00, /* B */
  .B_accuracy = 0x00, /* B - Accuracy */
  .acc_exp_sensor_dir = 0x02, /* Sensor direction */
  .Rexp_Bexp = 0xD0, /* R-Exp , B-Exp */
  .analog_flags = 0x02, /* Analogue characteristics flags */
  .nominal_reading = (uint8_t)(1500 / 128), /* Nominal reading */
  .normal_max = (uint8_t)(10000 / 128), /* Normal maximum */
  .normal_min = 0, /* Normal minimum */
  .sensor_max_reading = 0xFF, /* Sensor Maximum reading */
  .sensor_min_reading = 0x00, /* Sensor Minimum reading */
  .upper_nonrecover_thr = (uint8_t)(11000 / 128), /* Upper non-recoverable Threshold */
  .upper_critical_thr = (uint8_t)(10600 / 128), /* Upper critical Threshold */
  .upper_noncritical_thr = (uint8_t)(10300 / 128), /* Upper non critical Threshold */
  .lower_nonrecover_thr = (uint8_t)(-1000 / 128), /* Lower non-recoverable Threshold */
  .lower_critical_thr = (uint8_t)(-500 / 128), /* Lower critical Threshold */
  .lower_noncritical_thr =  (uint8_t)(0 / 128), /* Lower non-critical Threshold */
  .pos_thr_hysteresis = 2, /* positive going Threshold hysteresis value */
  .neg_thr_hysteresis = 2, /* negative going Threshold hysteresis value */
  .reserved1 = 0x00, /* reserved */
  .reserved2 = 0x00, /* reserved */
  .OEM = UCD_FPGA_1V2A_ID, /* OEM reserved */
  .IDtypelen = 0xc0 | STR_SIZE(SDR_AMC_FPGA_1V2A_CURR_ID), /* 8 bit ASCII, number of bytes */
  .IDstring = SDR_AMC_FPGA_1V2A_CURR_ID /* sensor string */
};

/* AMC DDR +1.2V TOP Current */
const SDR_type_01h_t SDR_AMC_DDR_1V2T_CURR = {
  .hdr.recID_LSB = 0x00, /* Filled by sdr_insert_entry() */
  .hdr.recID_MSB = 0x00,
  .hdr.SDRversion = 0x51,
  .hdr.rectype = TYPE_01,
  .hdr.reclength = sizeof(SDR_type_01h_t) - sizeof(SDR_entry_hdr_t),

  .ownerID = 0x00, /* i2c address, -> SDR_Init */
  .ownerLUN = 0x00, /* sensor owner LUN */
  .sensornum = 0x00, /* Filled by sdr_insert_entry() */

  /* record body bytes */
  .entityID = 0xC1, /* entity id: AMC Module */
  .entityinstance = 0x00, /* entity instance -> SDR_Init */
  .sensorinit = 0x7F, /* init: event generation + scanning enabled */
  .sensorcap = 0x56, /* capabilities: auto re-arm,*/
  .sensortype = SENSOR_TYPE_CURRENT, /* sensor type: CURRENT */
  .event_reading_type = 0x01, /* sensor reading */
  .assertion_event_mask = 0x7A80, /* assertion event mask (All upper going-high and lower going-low events) */
  .deassertion_event_mask = 0x7A80, /* deassertion event mask (All upper going-high events) */
  .readable_threshold_mask = 0x3F, /* LSB: readable Threshold mask: all thresholds are readable:  */
  .settable_threshold_mask = 0x00, /* MSB: setable Threshold mask: none of the thresholds are setable: */
  .sensor_units_1 = 0x80, /* sensor units 1 :*/
  .sensor_units_2 = 0x05, /* sensor units 2 :*/
  .sensor_units_3 = 0x00, /* sensor units 3 :*/
  .linearization = 0x00, /* Linearization */
  .M = 32, /* M */
  .M_tol = 0x00, /* M - Tolerance */
  .B = 0x00, /* B */
  .B_accuracy = 0x00, /* B - Accuracy */
  .acc_exp_sensor_dir = 0x02, /* Sensor direction */
  .Rexp_Bexp = 0xD0, /* R-Exp , B-Exp */
  .analog_flags = 0x02, /* Analogue characteristics flags */
  .nominal_reading = (uint8_t)(1500 / 32), /* Nominal reading */
  .normal_max = (uint8_t)(3000 / 32), /* Normal maximum */
  .normal_min = 0, /* Normal minimum */
  .sensor_max_reading = 0xFF, /* Sensor Maximum reading */
  .sensor_min_reading = 0x00, /* Sensor Minimum reading */
  .upper_nonrecover_thr = (uint8_t)(4000 / 32), /* Upper non-recoverable Threshold */
  .upper_critical_thr = (uint8_t)(3600 / 32), /* Upper critical Threshold */
  .upper_noncritical_thr = (uint8_t)(3300 / 32), /* Upper non critical Threshold */
  .lower_nonrecover_thr = (uint8_t)(-1000 / 32), /* Lower non-recoverable Threshold */
  .lower_critical_thr = (uint8_t)(-500 / 32), /* Lower critical Threshold */
  .lower_noncritical_thr =  (uint8_t)(0 / 32), /* Lower non-critical Threshold */
  .pos_thr_hysteresis = 2, /* positive going Threshold hysteresis value */
  .neg_thr_hysteresis = 2, /* negative going Threshold hysteresis value */
  .reserved1 = 0x00, /* reserved */
  .reserved2 = 0x00, /* reserved */
  .OEM = UCD_DDR_1V2T_ID, /* OEM reserved */
  .IDtypelen = 0xc0 | STR_SIZE(SDR_AMC_DDR_1V2T_CURR_ID), /* 8 bit ASCII, number of bytes */
  .IDstring = SDR_AMC_DDR_1V2T_CURR_ID /* sensor string */
};

/* AMC DDR +1.2V BOTTOM Current */
const SDR_type_01h_t SDR_AMC_DDR_1V2B_CURR = {
  .hdr.recID_LSB = 0x00, /* Filled by sdr_insert_entry() */
  .hdr.recID_MSB = 0x00,
  .hdr.SDRversion = 0x51,
  .hdr.rectype = TYPE_01,
  .hdr.reclength = sizeof(SDR_type_01h_t) - sizeof(SDR_entry_hdr_t),

  .ownerID = 0x00, /* i2c address, -> SDR_Init */
  .ownerLUN = 0x00, /* sensor owner LUN */
  .sensornum = 0x00, /* Filled by sdr_insert_entry() */

  /* record body bytes */
  .entityID = 0xC1, /* entity id: AMC Module */
  .entityinstance = 0x00, /* entity instance -> SDR_Init */
  .sensorinit = 0x7F, /* init: event generation + scanning enabled */
  .sensorcap = 0x56, /* capabilities: auto re-arm,*/
  .sensortype = SENSOR_TYPE_CURRENT, /* sensor type: CURRENT */
  .event_reading_type = 0x01, /* sensor reading */
  .assertion_event_mask = 0x7A80, /* assertion event mask (All upper going-high and lower going-low events) */
  .deassertion_event_mask = 0x7A80, /* deassertion event mask (All upper going-high events) */
  .readable_threshold_mask = 0x3F, /* LSB: readable Threshold mask: all thresholds are readable:  */
  .settable_threshold_mask = 0x00, /* MSB: setable Threshold mask: none of the thresholds are setable: */
  .sensor_units_1 = 0x80, /* sensor units 1 :*/
  .sensor_units_2 = 0x05, /* sensor units 2 :*/
  .sensor_units_3 = 0x00, /* sensor units 3 :*/
  .linearization = 0x00, /* Linearization */
  .M = 32, /* M */
  .M_tol = 0x00, /* M - Tolerance */
  .B = 0x00, /* B */
  .B_accuracy = 0x00, /* B - Accuracy */
  .acc_exp_sensor_dir = 0x02, /* Sensor direction */
  .Rexp_Bexp = 0xD0, /* R-Exp , B-Exp */
  .analog_flags = 0x02, /* Analogue characteristics flags */
  .nominal_reading = (uint8_t)(1500 / 32), /* Nominal reading */
  .normal_max = (uint8_t)(3000 / 32), /* Normal maximum */
  .normal_min = 0, /* Normal minimum */
  .sensor_max_reading = 0xFF, /* Sensor Maximum reading */
  .sensor_min_reading = 0x00, /* Sensor Minimum reading */
  .upper_nonrecover_thr = (uint8_t)(4000 / 32), /* Upper non-recoverable Threshold */
  .upper_critical_thr = (uint8_t)(3600 / 32), /* Upper critical Threshold */
  .upper_noncritical_thr = (uint8_t)(3300 / 32), /* Upper non critical Threshold */
  .lower_nonrecover_thr = (uint8_t)(-1000 / 32), /* Lower non-recoverable Threshold */
  .lower_critical_thr = (uint8_t)(-500 / 32), /* Lower critical Threshold */
  .lower_noncritical_thr =  (uint8_t)(0 / 32), /* Lower non-critical Threshold */
  .pos_thr_hysteresis = 2, /* positive going Threshold hysteresis value */
  .neg_thr_hysteresis = 2, /* negative going Threshold hysteresis value */
  .reserved1 = 0x00, /* reserved */
  .reserved2 = 0x00, /* reserved */
  .OEM = UCD_DDR_1V2B_ID, /* OEM reserved */
  .IDtypelen = 0xc0 | STR_SIZE(SDR_AMC_DDR_1V2B_CURR_ID), /* 8 bit ASCII, number of bytes */
  .IDstring = SDR_AMC_DDR_1V2B_CURR_ID /* sensor string */
};

/* AMC IO 3V3 Current */
const SDR_type_01h_t SDR_AMC_IO_3V3_CURR = {
  .hdr.recID_LSB = 0x00, /* Filled by sdr_insert_entry() */
  .hdr.recID_MSB = 0x00,
  .hdr.SDRversion = 0x51,
  .hdr.rectype = TYPE_01,
  .hdr.reclength = sizeof(SDR_type_01h_t) - sizeof(SDR_entry_hdr_t),

  .ownerID = 0x00, /* i2c address, -> SDR_Init */
  .ownerLUN = 0x00, /* sensor owner LUN */
  .sensornum = 0x00, /* Filled by sdr_insert_entry() */

  /* record body bytes */
  .entityID = 0xC1, /* entity id: AMC Module */
  .entityinstance = 0x00, /* entity instance -> SDR_Init */
  .sensorinit = 0x7F, /* init: event generation + scanning enabled */
  .sensorcap = 0x56, /* capabilities: auto re-arm,*/
  .sensortype = SENSOR_TYPE_CURRENT, /* sensor type: CURRENT */
  .event_reading_type = 0x01, /* sensor reading */
  .assertion_event_mask = 0x7A80, /* assertion event mask (All upper going-high and lower going-low events) */
  .deassertion_event_mask = 0x7A80, /* deassertion event mask (All upper going-high events) */
  .readable_threshold_mask = 0x3F, /* LSB: readable Threshold mask: all thresholds are readable:  */
  .settable_threshold_mask = 0x00, /* MSB: setable Threshold mask: none of the thresholds are setable: */
  .sensor_units_1 = 0x80, /* sensor units 1 :*/
  .sensor_units_2 = 0x05, /* sensor units 2 :*/
  .sensor_units_3 = 0x00, /* sensor units 3 :*/
  .linearization = 0x00, /* Linearization */
  .M = 128, /* M */
  .M_tol = 0x00, /* M - Tolerance */
  .B = 0x00, /* B */
  .B_accuracy = 0x00, /* B - Accuracy */
  .acc_exp_sensor_dir = 0x02, /* Sensor direction */
  .Rexp_Bexp = 0xD0, /* R-Exp , B-Exp */
  .analog_flags = 0x02, /* Analogue characteristics flags */
  .nominal_reading = (uint8_t)(3500 / 128), /* Nominal reading */
  .normal_max = (uint8_t)(10000 / 128), /* Normal maximum */
  .normal_min = 0, /* Normal minimum */
  .sensor_max_reading = 0xFF, /* Sensor Maximum reading */
  .sensor_min_reading = 0x00, /* Sensor Minimum reading */
  .upper_nonrecover_thr = (uint8_t)(13000 / 128), /* Upper non-recoverable Threshold */
  .upper_critical_thr = (uint8_t)(12000 / 128), /* Upper critical Threshold */
  .upper_noncritical_thr = (uint8_t)(11000 / 128), /* Upper non critical Threshold */
  .lower_nonrecover_thr = (uint8_t)(-1000 / 128), /* Lower non-recoverable Threshold */
  .lower_critical_thr = (uint8_t)(-500 / 128), /* Lower critical Threshold */
  .lower_noncritical_thr = (uint8_t)(0 / 128), /* Lower non-critical Threshold */
  .pos_thr_hysteresis = 2, /* positive going Threshold hysteresis value */
  .neg_thr_hysteresis = 2, /* negative going Threshold hysteresis value */
  .reserved1 = 0x00, /* reserved */
  .reserved2 = 0x00, /* reserved */
  .OEM = UCD_AMC_IO_3V3_ID, /* OEM reserved */
  .IDtypelen = 0xc0 | STR_SIZE(SDR_AMC_IO_3V3_CURR_ID), /* 8 bit ASCII, number of bytes */
  .IDstring = SDR_AMC_IO_3V3_CURR_ID /* sensor string */
};


/* UCD Temperature*/
const SDR_type_01h_t SDR_UCD_TEMP = {
  .hdr.recID_LSB = 0x00, /* Filled by sdr_insert_entry() */
  .hdr.recID_MSB = 0x00,
  .hdr.SDRversion = 0x51,
  .hdr.rectype = TYPE_01,
  .hdr.reclength = sizeof(SDR_type_01h_t) - sizeof(SDR_entry_hdr_t),

  .ownerID = 0x00,   /* i2c address, -> SDR_Init */
  .ownerLUN = 0x00,  /* sensor owner LUN */
  .sensornum = 0x00, /* Filled by sdr_insert_entry() */

  /* record body bytes */
  .entityID = 0xC1,                        /* entity id: AMC Module */
  .entityinstance = 0x00,                  /* entity instance -> SDR_Init */
  .sensorinit = 0x7F,                      /* init: event generation + scanning enabled */
  .sensorcap = 0x56,                       /* capabilities: auto re-arm,*/
  .sensortype = SENSOR_TYPE_TEMPERATURE,   /* sensor type */
  .event_reading_type = 0x01,              /* sensor reading*/
  .assertion_event_mask = 0x7A95,          /* assertion event mask (All upper going-high and lower going-low events) */
  .deassertion_event_mask = 0x7A80,        /* deassertion event mask (All upper going-high events) */
  .readable_threshold_mask = 0x3F,         /* LSB: readable Threshold mask: all thresholds are readable:  */
  .settable_threshold_mask = 0x00,         /* MSB: setable Threshold mask: none of the thresholds are setable: */
  .sensor_units_1 = 0x00,                  /* sensor units 1 :*/
  .sensor_units_2 = 0x01,                  /* sensor units 2 :*/
  .sensor_units_3 = 0x00,                  /* sensor units 3 :*/
  .linearization = 0x00,                   /* Linearization */
  .M = 5,                                  /* M */
  .M_tol = 0x00,                           /* M - Tolerance */
  .B = 0x00,                               /* B */
  .B_accuracy = 0x00,                      /* B - Accuracy */
  .acc_exp_sensor_dir = 0x00,              /* Sensor direction */
  .Rexp_Bexp = 0xF0,                       /* R-Exp , B-Exp */
  .analog_flags = 0x03,                    /* Analogue characteristics flags */
  .nominal_reading = (24 << 1),            /* Nominal reading */
  .normal_max = (60 << 1),                 /* Normal maximum */
  .normal_min = (9 << 1),                  /* Normal minimum */
  .sensor_max_reading = 0xFF,              /* Sensor Maximum reading */
  .sensor_min_reading = 0x00,              /* Sensor Minimum reading */
  .upper_nonrecover_thr = (90 << 1),       /* Upper non-recoverable Threshold */
  .upper_critical_thr = (80 << 1),         /* Upper critical Threshold */
  .upper_noncritical_thr = (70 << 1),      /* Upper non critical Threshold */
  .lower_nonrecover_thr = (0 << 1),        /* Lower non-recoverable Threshold */
  .lower_critical_thr = (3 << 1),          /* Lower critical Threshold */
  .lower_noncritical_thr = (6 << 1),       /* Lower non-critical Threshold */
  .pos_thr_hysteresis = 2,                 /* positive going Threshold hysteresis value */
  .neg_thr_hysteresis = 2,                 /* negative going Threshold hysteresis value */
  .reserved1 = 0x00,                       /* reserved */
  .reserved2 = 0x00,                       /* reserved */
  .OEM = 0x00,                             /* OEM reserved */
  .IDtypelen = 0xc0 | STR_SIZE("TEMP UCD"),/* 8 bit ASCII, number of bytes */
  .IDstring = "TEMP UCD"                   /* sensor string */
};

#endif

void amc_sdr_init(void)
{
  /* Hotswap Sensor */
  sdr_insert_entry(TYPE_02, (void *)&SDR_HOTSWAP_AMC, &vTaskHotSwap_Handle, 0, 0);

  /* UCD90320 sensors*/
#ifdef MODULE_UCD90XXX
  /* Module input rails: AMC 12 V, then the RTM feed current */
  sdr_insert_entry(TYPE_01, (void *)&SDR_AMC_12V, &vTaskUCD_Handle, 0, CHIP_ID_PMBUS_0);
  sdr_insert_entry(TYPE_01, (void *)&SDR_RTM_12V_CURR, &vTaskUCD_Handle, 0, CHIP_ID_PMBUS_0);

  /* FPGA rails: voltage/current pair per rail */
  sdr_insert_entry(TYPE_01, (void *)&SDR_AMC_FPGA_1V8, &vTaskUCD_Handle, 0, CHIP_ID_PMBUS_0);
  sdr_insert_entry(TYPE_01, (void *)&SDR_AMC_FPGA_1V8_CURR, &vTaskUCD_Handle, 0, CHIP_ID_PMBUS_0);
  sdr_insert_entry(TYPE_01, (void *)&SDR_AMC_FPGA_0V85, &vTaskUCD_Handle, 0, CHIP_ID_PMBUS_0);
  sdr_insert_entry(TYPE_01, (void *)&SDR_AMC_FPGA_0V85_CURR, &vTaskUCD_Handle, 0, CHIP_ID_PMBUS_0);
  sdr_insert_entry(TYPE_01, (void *)&SDR_AMC_FPGA_0V9A, &vTaskUCD_Handle, 0, CHIP_ID_PMBUS_0);
  sdr_insert_entry(TYPE_01, (void *)&SDR_AMC_FPGA_0V9A_CURR, &vTaskUCD_Handle, 0, CHIP_ID_PMBUS_0);
  sdr_insert_entry(TYPE_01, (void *)&SDR_AMC_FPGA_1V2A, &vTaskUCD_Handle, 0, CHIP_ID_PMBUS_0);
  sdr_insert_entry(TYPE_01, (void *)&SDR_AMC_FPGA_1V2A_CURR, &vTaskUCD_Handle, 0, CHIP_ID_PMBUS_0);

  /* DDR rails: voltage/current pair per rail */
  sdr_insert_entry(TYPE_01, (void *)&SDR_AMC_DDR_1V2T, &vTaskUCD_Handle, 0, CHIP_ID_PMBUS_0);
  sdr_insert_entry(TYPE_01, (void *)&SDR_AMC_DDR_1V2T_CURR, &vTaskUCD_Handle, 0, CHIP_ID_PMBUS_0);
  sdr_insert_entry(TYPE_01, (void *)&SDR_AMC_DDR_1V2B, &vTaskUCD_Handle, 0, CHIP_ID_PMBUS_0);
  sdr_insert_entry(TYPE_01, (void *)&SDR_AMC_DDR_1V2B_CURR, &vTaskUCD_Handle, 0, CHIP_ID_PMBUS_0);

  /* IO rail */
  sdr_insert_entry(TYPE_01, (void *)&SDR_AMC_IO_3V3, &vTaskUCD_Handle, 0, CHIP_ID_PMBUS_0);
  sdr_insert_entry(TYPE_01, (void *)&SDR_AMC_IO_3V3_CURR, &vTaskUCD_Handle, 0, CHIP_ID_PMBUS_0);

  /* UCD90320 temperature */
  sdr_insert_entry(TYPE_01, (void *)&SDR_UCD_TEMP, &vTaskUCD_Handle, 0, CHIP_ID_PMBUS_0 );

  /* FMC rails: the currents grouped by rail family (FMC0/FMC1), then the
   * VADJ voltages - same pattern as the RTM FMC2/FMC3 block that follows. */
  sdr_insert_entry(TYPE_01, (void *)&SDR_FMC0_12V_CURR, &vTaskUCD_Handle, 0, CHIP_ID_PMBUS_0);
  sdr_insert_entry(TYPE_01, (void *)&SDR_FMC1_12V_CURR, &vTaskUCD_Handle, 0, CHIP_ID_PMBUS_0);
  sdr_insert_entry(TYPE_01, (void *)&SDR_FMC0_P3V3_CURR, &vTaskUCD_Handle, 0, CHIP_ID_PMBUS_0);
  sdr_insert_entry(TYPE_01, (void *)&SDR_FMC1_P3V3_CURR, &vTaskUCD_Handle, 0, CHIP_ID_PMBUS_0);
  sdr_insert_entry(TYPE_01, (void *)&SDR_FMC0_VADJ_CURR, &vTaskUCD_Handle, 0, CHIP_ID_PMBUS_0);
  sdr_insert_entry(TYPE_01, (void *)&SDR_FMC1_VADJ_CURR, &vTaskUCD_Handle, 0, CHIP_ID_PMBUS_0);
  sdr_insert_entry(TYPE_01, (void *)&SDR_FMC0_VADJ, &vTaskUCD_Handle, 0, CHIP_ID_PMBUS_0);
  sdr_insert_entry(TYPE_01, (void *)&SDR_FMC1_VADJ, &vTaskUCD_Handle, 0, CHIP_ID_PMBUS_0);
#endif

  /* INA3221 sensors */
#ifdef MODULE_INA3221_VOLTAGE

  /* AMC RTM Voltage */
  sdr_insert_entry(TYPE_01, (void *)&SDR_AMC_12V, &vTaskINA3221_Handle, 0, CHIP_ID_INA_0);
  sdr_insert_entry(TYPE_01, (void *)&SDR_RTM_12V, &vTaskINA3221_Handle, 0, CHIP_ID_INA_0);
  sdr_add_settings(CHIP_ID_INA_0, (void *)&INA3221_SETTINGS);

  /* FMC1 Voltage */
  sdr_insert_entry(TYPE_01, (void *)&SDR_FMC1_12V, &vTaskINA3221_Handle, FMC1_12V_DEVID, CHIP_ID_INA_1);
  sdr_insert_entry(TYPE_01, (void *)&SDR_FMC1_VADJ, &vTaskINA3221_Handle, FMC1_VADJ_DEVID, CHIP_ID_INA_1);
  sdr_insert_entry(TYPE_01, (void *)&SDR_FMC1_P3V3, &vTaskINA3221_Handle, FMC1_P3V3_DEVID, CHIP_ID_INA_1);
  sdr_add_settings(CHIP_ID_INA_1, (void *)&INA3221_SETTINGS);

  /* FMC2 Voltage */
  sdr_insert_entry(TYPE_01, (void *)&SDR_FMC2_12V, &vTaskINA3221_Handle, FMC2_12V_DEVID, CHIP_ID_INA_2);
  sdr_insert_entry(TYPE_01, (void *)&SDR_FMC2_VADJ, &vTaskINA3221_Handle, FMC2_VADJ_DEVID, CHIP_ID_INA_2);
  sdr_insert_entry(TYPE_01, (void *)&SDR_FMC2_P3V3, &vTaskINA3221_Handle, FMC2_P3V3_DEVID, CHIP_ID_INA_2);
  sdr_add_settings(CHIP_ID_INA_2, (void *)&INA3221_SETTINGS);

#endif

#ifdef MODULE_INA3221_CURRENT

  /* AMC RTM Current */
  sdr_insert_entry(TYPE_01, (void *)&SDR_AMC_12V_CURR, &vTaskINA3221_Handle, 0, CHIP_ID_INA_0);
  sdr_insert_entry(TYPE_01, (void *)&SDR_RTM_12V_CURR, &vTaskINA3221_Handle, 0, CHIP_ID_INA_0);
  sdr_add_settings(CHIP_ID_INA_0, (void *)&INA3221_SETTINGS);

  /* FMC1 Current */
  sdr_insert_entry(TYPE_01, (void *)&SDR_FMC1_12V_CURR, &vTaskINA3221_Handle, FMC1_12V_CURR_DEVID, CHIP_ID_INA_1);
  sdr_insert_entry(TYPE_01, (void *)&SDR_FMC1_VADJ_CURR, &vTaskINA3221_Handle, FMC1_VADJ_CURR_DEVID, CHIP_ID_INA_1);
  sdr_insert_entry(TYPE_01, (void *)&SDR_FMC1_P3V3_CURR, &vTaskINA3221_Handle, FMC1_P3V3_CURR_DEVID, CHIP_ID_INA_1);
  sdr_add_settings(CHIP_ID_INA_1, (void *)&INA3221_SETTINGS);

  /* FMC2 Current */
  sdr_insert_entry(TYPE_01, (void *)&SDR_FMC2_12V_CURR, &vTaskINA3221_Handle, FMC2_12V_CURR_DEVID, CHIP_ID_INA_2);
  sdr_insert_entry(TYPE_01, (void *)&SDR_FMC2_VADJ_CURR, &vTaskINA3221_Handle, FMC2_VADJ_CURR_DEVID, CHIP_ID_INA_2);
  sdr_insert_entry(TYPE_01, (void *)&SDR_FMC2_P3V3_CURR, &vTaskINA3221_Handle, FMC2_P3V3_CURR_DEVID, CHIP_ID_INA_2);
  sdr_add_settings(CHIP_ID_INA_2, (void *)&INA3221_SETTINGS);

#endif

}

/* -------------------------------------------------------------------------
 * RTM FMC2/FMC3 monitors (entity 0xC0, instance 0x00)
 *
 * The uRTM2.3's UCD90124A (0x5B) monitors the two FMC sites.  The AMC MMC
 * reads it (rtm_sensors.c) and owns these eight sensors: they are inserted
 * with the fixed sensor numbers 0x20-0x27 while the RTM is present and
 * removed again when the handle is opened.
 *
 * The fields shared by all eight records live in the prefix macro below.
 * The thresholds are per rail; currents use 32 mA/LSB (M = 32, R = -3) and
 * the FMC VADJ voltage 16 mV/LSB (M = 16, R = -3) so every value fits the
 * 8-bit SDR reading.  Only upper going-high events are enabled: the RTM rails
 * fall to 0 V whenever the payload cuts the 12 V, and lower-threshold events
 * would just flood the event log.
 * ------------------------------------------------------------------------- */

#define SDR_RTM_MONITOR_PREFIX                                                 \
  .hdr.recID_LSB = 0x00, /* Filled by sdr_insert_entry_full() */               \
  .hdr.recID_MSB = 0x00,                                                       \
  .hdr.SDRversion = 0x51,                                                      \
  .hdr.rectype = TYPE_01,                                                      \
  .hdr.reclength = sizeof(SDR_type_01h_t) - sizeof(SDR_entry_hdr_t),           \
                                                                               \
  .ownerID = 0x00,   /* i2c address, -> SDR_Init */                            \
  .ownerLUN = 0x00,                                                            \
  .sensornum = 0x00, /* sensor number, -> sdr_insert_entry_full() */           \
                                                                               \
  .entityID = 0xC0,          /* entity id: RTM */                              \
  .entityinstance = 0x00,    /* entity instance, -> sdr_insert_entry_full() */ \
  .sensorinit = 0x7F,        /* event generation + scanning enabled */         \
  .sensorcap = 0x56,         /* capabilities: auto re-arm */                   \
  .event_reading_type = 0x01,                                                  \
  .readable_threshold_mask = 0x3F,  /* all thresholds readable */              \
  .settable_threshold_mask = 0x00,  /* none settable */                        \
  .sensor_units_3 = 0x00,                                                      \
  .linearization = 0x00,                                                       \
  .M_tol = 0x00,                                                               \
  .B = 0x00,                                                                   \
  .B_accuracy = 0x00,                                                          \
  .sensor_max_reading = 0xFF,                                                  \
  .sensor_min_reading = 0x00,                                                  \
  .reserved1 = 0x00,                                                           \
  .reserved2 = 0x00,                                                           \
  .OEM = 0x00

#define SDR_RTM_TEMP_UCD_ID      "RTM TEMP UCD"
#define SDR_RTM_FMC2_12V_I_ID    "FMC2 +12V Curr"
#define SDR_RTM_FMC2_3V3_I_ID    "FMC2 +3.3V Curr"
#define SDR_RTM_FMC2_VADJ_V_ID   "FMC2 VADJ"
#define SDR_RTM_FMC2_VADJ_I_ID   "FMC2 VADJ Curr"
#define SDR_RTM_FMC3_12V_I_ID    "FMC3 +12V Curr"
#define SDR_RTM_FMC3_3V3_I_ID    "FMC3 +3.3V Curr"
#define SDR_RTM_FMC3_VADJ_V_ID   "FMC3 VADJ"
#define SDR_RTM_FMC3_VADJ_I_ID   "FMC3 VADJ Curr"

/* The linearization, units and thresholds below are the ones of the AMC's own
 * FMC0/FMC1 records above: the RTM FMC sites are reported like the two on the
 * AMC (32 mA/LSB for the 12 V and 3.3 V currents, 64 mA/LSB for VADJ current,
 * 16 mV/LSB for VADJ voltage). */

/* RTM UCD temperature (the UCD90124A's internal sensor), same thresholds as
 * the AMC's own TEMP UCD record. */
const SDR_type_01h_t SDR_RTM_TEMP_UCD = {
  SDR_RTM_MONITOR_PREFIX,
  .sensor_units_1 = 0x00,
  .sensor_units_2 = 0x01, /* degrees C */
  .sensortype = SENSOR_TYPE_TEMPERATURE,
  .assertion_event_mask = 0x7A95,
  .deassertion_event_mask = 0x7A80,
  .M = 5,                          /* 0.5 C per LSB with R = -1 */
  .Rexp_Bexp = 0xF0,
  .acc_exp_sensor_dir = 0x00,
  .analog_flags = 0x03,
  .pos_thr_hysteresis = 2,
  .neg_thr_hysteresis = 2,
  .nominal_reading = (24 << 1),
  .normal_max = (60 << 1),
  .normal_min = (9 << 1),
  .upper_nonrecover_thr = (90 << 1),
  .upper_critical_thr = (80 << 1),
  .upper_noncritical_thr = (70 << 1),
  .lower_nonrecover_thr = (0 << 1),
  .lower_critical_thr = (3 << 1),
  .lower_noncritical_thr = (6 << 1),
  .IDtypelen = 0xc0 | STR_SIZE(SDR_RTM_TEMP_UCD_ID),
  .IDstring = SDR_RTM_TEMP_UCD_ID
};

/* FMC 12 V current, as SDR_FMC0_12V_CURR */
#define SDR_RTM_FMC_12V_I_THRESHOLDS                                           \
  .nominal_reading = (uint8_t)(500 / 32),                                      \
  .normal_max = (uint8_t)(1000 / 32),                                          \
  .normal_min = 0,                                                             \
  .upper_nonrecover_thr = (uint8_t)(3000 / 32),                                \
  .upper_critical_thr = (uint8_t)(2500 / 32),                                  \
  .upper_noncritical_thr = (uint8_t)(1500 / 32),                               \
  .lower_nonrecover_thr = (uint8_t)(-1000 / 32),                               \
  .lower_critical_thr = (uint8_t)(-500 / 32),                                  \
  .lower_noncritical_thr = (uint8_t)(0 / 32)

/* FMC 3.3 V current, as SDR_FMC0_P3V3_CURR */
#define SDR_RTM_FMC_3V3_I_THRESHOLDS                                           \
  .nominal_reading = (uint8_t)(1500 / 32),                                     \
  .normal_max = (uint8_t)(3000 / 32),                                          \
  .normal_min = 0,                                                             \
  .upper_nonrecover_thr = (uint8_t)(4000 / 32),                                \
  .upper_critical_thr = (uint8_t)(3600 / 32),                                  \
  .upper_noncritical_thr = (uint8_t)(3300 / 32),                               \
  .lower_nonrecover_thr = (uint8_t)(-1000 / 32),                               \
  .lower_critical_thr = (uint8_t)(-500 / 32),                                  \
  .lower_noncritical_thr = (uint8_t)(0 / 32)

/* FMC VADJ current, as SDR_FMC0_VADJ_CURR */
#define SDR_RTM_FMC_VADJ_I_THRESHOLDS                                          \
  .nominal_reading = (uint8_t)(1500 / 64),                                     \
  .normal_max = (uint8_t)(4000 / 64),                                          \
  .normal_min = 0,                                                             \
  .upper_nonrecover_thr = (uint8_t)(5000 / 64),                                \
  .upper_critical_thr = (uint8_t)(4600 / 64),                                  \
  .upper_noncritical_thr = (uint8_t)(4300 / 64),                               \
  .lower_nonrecover_thr = (uint8_t)(-1000 / 64),                               \
  .lower_critical_thr = (uint8_t)(-500 / 64),                                  \
  .lower_noncritical_thr = (uint8_t)(0 / 64)

/* FMC VADJ voltage, as SDR_FMC0_VADJ */
#define SDR_RTM_FMC_VADJ_V_THRESHOLDS                                          \
  .nominal_reading = (1350 / 16),                                              \
  .normal_max = (1950 / 16),                                                   \
  .normal_min = (850 / 16),                                                    \
  .upper_nonrecover_thr = (2100 / 16),                                         \
  .upper_critical_thr = (2050 / 16),                                           \
  .upper_noncritical_thr = (2000 / 16),                                        \
  .lower_nonrecover_thr = (700 / 16),                                          \
  .lower_critical_thr = (750 / 16),                                            \
  .lower_noncritical_thr = (800 / 16)

/* current monitor record: same fields as SDR_FMC0_12V_CURR (_CURR records) */
#define SDR_RTM_CURRENT(m, thresholds, id)                                     \
  SDR_RTM_MONITOR_PREFIX,                                                      \
  .sensor_units_1 = 0x80,                                                      \
  .sensor_units_2 = 0x05, /* Amps */                                           \
  .sensortype = SENSOR_TYPE_CURRENT,                                           \
  .Rexp_Bexp = 0xD0,         /* R = -3, B exponent = 0 */                      \
  .assertion_event_mask = 0x7A80,                                              \
  .deassertion_event_mask = 0x7A80,                                            \
  .M = m,                                                                      \
  .acc_exp_sensor_dir = 0x02,                                                  \
  .analog_flags = 0x02,                                                        \
  .pos_thr_hysteresis = 2,                                                     \
  .neg_thr_hysteresis = 2,                                                     \
  thresholds,                                                                  \
  .IDtypelen = 0xc0 | STR_SIZE(id),                                            \
  .IDstring = id

/* VADJ voltage record: same fields as SDR_FMC0_VADJ */
#define SDR_RTM_VADJ_VOLTAGE(thresholds, id)                                   \
  SDR_RTM_MONITOR_PREFIX,                                                      \
  .sensor_units_1 = 0x00,                                                      \
  .sensor_units_2 = 0x04, /* Volts */                                          \
  .sensortype = SENSOR_TYPE_VOLTAGE,                                           \
  .Rexp_Bexp = 0xD0,         /* R = -3, B exponent = 0 */                      \
  .assertion_event_mask = 0x7A95,                                              \
  .deassertion_event_mask = 0x7A80,                                            \
  .M = 16,                                                                     \
  .acc_exp_sensor_dir = 0x00,                                                  \
  .analog_flags = 0x07,                                                        \
  .pos_thr_hysteresis = 1,                                                     \
  .neg_thr_hysteresis = 1,                                                     \
  thresholds,                                                                  \
  .IDtypelen = 0xc0 | STR_SIZE(id),                                            \
  .IDstring = id

const SDR_type_01h_t SDR_RTM_FMC2_12V_I = {
  SDR_RTM_CURRENT(32, SDR_RTM_FMC_12V_I_THRESHOLDS, SDR_RTM_FMC2_12V_I_ID)
};

const SDR_type_01h_t SDR_RTM_FMC2_3V3_I = {
  SDR_RTM_CURRENT(32, SDR_RTM_FMC_3V3_I_THRESHOLDS, SDR_RTM_FMC2_3V3_I_ID)
};

const SDR_type_01h_t SDR_RTM_FMC2_VADJ_V = {
  SDR_RTM_VADJ_VOLTAGE(SDR_RTM_FMC_VADJ_V_THRESHOLDS, SDR_RTM_FMC2_VADJ_V_ID)
};

const SDR_type_01h_t SDR_RTM_FMC2_VADJ_I = {
  SDR_RTM_CURRENT(64, SDR_RTM_FMC_VADJ_I_THRESHOLDS, SDR_RTM_FMC2_VADJ_I_ID)
};

const SDR_type_01h_t SDR_RTM_FMC3_12V_I = {
  SDR_RTM_CURRENT(32, SDR_RTM_FMC_12V_I_THRESHOLDS, SDR_RTM_FMC3_12V_I_ID)
};

const SDR_type_01h_t SDR_RTM_FMC3_3V3_I = {
  SDR_RTM_CURRENT(32, SDR_RTM_FMC_3V3_I_THRESHOLDS, SDR_RTM_FMC3_3V3_I_ID)
};

const SDR_type_01h_t SDR_RTM_FMC3_VADJ_V = {
  SDR_RTM_VADJ_VOLTAGE(SDR_RTM_FMC_VADJ_V_THRESHOLDS, SDR_RTM_FMC3_VADJ_V_ID)
};

const SDR_type_01h_t SDR_RTM_FMC3_VADJ_I = {
  SDR_RTM_CURRENT(64, SDR_RTM_FMC_VADJ_I_THRESHOLDS, SDR_RTM_FMC3_VADJ_I_ID)
};

/* Sensor number order (0x20-0x27): this table and rtm_sensors.c's monitor
 * table must stay in the same order. */
const SDR_type_01h_t * const SDR_RTM_UCD[RTM_SENSORS_COUNT] = {
  &SDR_RTM_TEMP_UCD,
  &SDR_RTM_FMC2_12V_I,
  &SDR_RTM_FMC3_12V_I,
  &SDR_RTM_FMC2_3V3_I,
  &SDR_RTM_FMC3_3V3_I,
  &SDR_RTM_FMC2_VADJ_I,
  &SDR_RTM_FMC3_VADJ_I,
  &SDR_RTM_FMC2_VADJ_V,
  &SDR_RTM_FMC3_VADJ_V
};
