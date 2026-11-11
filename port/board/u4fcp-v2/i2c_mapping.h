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
    /* on the RTM: I2C2 -> 0x70 ch5 -> TCA9548A 0x71 -> ... */
    CHIP_ID_RTM_MUX_1,      /**< TCA9548A 0x71, second mux level (RTM side) */
    CHIP_ID_RTM_UCD,        /**< UCD90124A 0x5B behind 0x70 ch5 -> 0x71 ch7 */
    I2C_CHIP_CNT
};

/* The uRTM2.3 PMBus monitor UCD90124A lives two mux levels deep: the AMC
 * switch 0x70 ch5 reaches the RTM bus, the RTM switch 0x71 ch7 the FRU/PMBus
 * segment.  i2c_take_by_chipid(CHIP_ID_RTM_UCD) selects the first level (it
 * is the bus mapping's mux_bus), i2c_rtm_mux_select() the second. */
#define RTM_MUX_1_ADDR          0x71
#define RTM_MUX_1_UCD_CHANNEL   7

/**
 * @brief Select the RTM-side mux channel of the UCD90124A (0x71, ch7)
 *
 * The caller must already hold the physical I2C interface returned by
 * i2c_take_by_chipid( CHIP_ID_RTM_UCD, ... ).
 *
 * @retval true  the RTM mux acknowledged the channel select
 * @retval false the RTM is not present / the mux did not answer
 */
bool i2c_rtm_mux_select( uint8_t i2c_interface );

#define I2C_MUX_CNT    1

extern i2c_mux_state_t i2c_mux[I2C_MUX_CNT];
extern i2c_bus_mapping_t i2c_bus_map[I2C_BUS_CNT];
extern i2c_chip_mapping_t i2c_chip_map[I2C_CHIP_CNT];

#endif
