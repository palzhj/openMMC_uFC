#ifndef I2C_MAPPING_H_
#define I2C_MAPPING_H_

#include "i2c.h"

#define I2CMODE_POOLING         1
#define I2CMODE_INTERRUPT       0
#define SPEED_100KHZ            100000

enum {
    I2C_BUS_UNKNOWN_ID = 0,
    I2C_BUS_FMC0_ID,
    I2C_BUS_FMC1_ID,
    I2C_BUS_CLOCK_ID,
    I2C_BUS_FPGA_ID,
    I2C_BUS_FIREFLY_ID,
    I2C_BUS_RTM_ID,
    I2C_BUS_DDR4_ID,
    I2C_BUS_POWER_ID,
    I2C_BUS_MUX_ID,
    I2C_BUS_CNT
};

enum {
    CHIP_ID_MUX_0 = 0,
    CHIP_ID_FMC0_EEPROM,
    CHIP_ID_FMC1_EEPROM,
    CHIP_ID_ADN,
    CHIP_ID_SI5345_0,
    CHIP_ID_SI5345_1,
    // CHIP_ID_FPGA_0,
    CHIP_ID_TCA9554_JTAG_0,
    // CHIP_ID_FIREFLY_0,
    // CHIP_ID_DDR4,
    CHIP_ID_EEPROM,
    CHIP_ID_PMBUS_0,
    CHIP_ID_TPL0102_0,
    I2C_CHIP_CNT
};

#define I2C_MUX_CNT    1

extern i2c_mux_state_t i2c_mux[I2C_MUX_CNT];
extern i2c_bus_mapping_t i2c_bus_map[I2C_BUS_CNT];
extern i2c_chip_mapping_t i2c_chip_map[I2C_CHIP_CNT];

#endif
