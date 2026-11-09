#include "i2c.h"
#include "i2c_mapping.h"
#include "port.h"

// Data Fields: i2c_interface, state, semaphore
i2c_mux_state_t i2c_mux[I2C_MUX_CNT] = {
  {I2C2_ID, -1, 0}
};

// Data Fields: i2c_interface, mux_bus, enabled
i2c_bus_mapping_t i2c_bus_map[I2C_BUS_CNT] = {
  [I2C_BUS_UNKNOWN_ID]  = {I2C2_ID, -1, 0},
  [I2C_BUS_FMC0_ID]     = {I2C2_ID, 0, 0},
  [I2C_BUS_FMC1_ID]     = {I2C2_ID, 1, 0},
  [I2C_BUS_CLOCK_ID]    = {I2C2_ID, 2, 1},
  [I2C_BUS_FIREFLY_ID]  = {I2C2_ID, 3, 0},
  [I2C_BUS_FPGA_ID]     = {I2C2_ID, 4, 1},
  [I2C_BUS_RTM_ID]      = {I2C2_ID, 5, 0},
  [I2C_BUS_DDR4_ID]     = {I2C2_ID, 6, 0},
  [I2C_BUS_POWER_ID]    = {I2C2_ID, 7, 1},
  [I2C_BUS_MUX_ID]      = {I2C2_ID, -1, 1}
};

// Data Fields: bus_id, i2c_address
i2c_chip_mapping_t i2c_chip_map[I2C_CHIP_CNT] = {
  [CHIP_ID_MUX_0]         = {I2C_BUS_MUX_ID,    0x70},
  [CHIP_ID_FMC0_EEPROM]   = {I2C_BUS_FMC0_ID,   0x50},
  [CHIP_ID_FMC1_EEPROM]   = {I2C_BUS_FMC1_ID,   0x51},
  [CHIP_ID_ADN]           = {I2C_BUS_CLOCK_ID,  0x48},
  [CHIP_ID_SI5345_0]      = {I2C_BUS_CLOCK_ID,  0x68},
  [CHIP_ID_SI5345_1]      = {I2C_BUS_CLOCK_ID,  0x69},
  // [CHIP_ID_FPGA_0]        = {I2C_BUS_FPGA_ID,   0x0},
  [CHIP_ID_TCA9554_JTAG_0]= {I2C_BUS_FPGA_ID,   0x39},
  // [CHIP_ID_FIREFLY_0]     = {I2C_BUS_FIREFLY_ID,0x50},
  // [CHIP_ID_DDR4]        = {I2C_BUS_DDR4_ID,   0x0},
  [CHIP_ID_EEPROM]        = {I2C_BUS_POWER_ID,  0x54},
  [CHIP_ID_PMBUS_0]       = {I2C_BUS_POWER_ID,  0x11},
  [CHIP_ID_TPL0102_0]     = {I2C_BUS_POWER_ID,  0x56}
};

bool i2c_set_mux_bus(uint8_t bus_id, i2c_mux_state_t *i2c_mux, int8_t new_state)
{
  portENABLE_INTERRUPTS();

  if (i2c_mux->i2c_interface == i2c_bus_map[i2c_chip_map[CHIP_ID_MUX_0].bus_id].i2c_interface) {
    uint8_t tca_channel = 1 << new_state;

    /* Select desired channel in the I2C switch */
    if( xI2CMasterWrite( i2c_bus_map[i2c_chip_map[CHIP_ID_MUX_0].bus_id].i2c_interface, i2c_chip_map[CHIP_ID_MUX_0].i2c_address, &tca_channel, 1 ) != 0 ) {
        /* We failed to configure the I2C Mux, release the semaphore */
        xSemaphoreGive( i2c_mux->semaphore );
        return false;
    }
  }

  i2c_mux->state = new_state;
  return true;
}

uint8_t i2c_get_mux_bus(uint8_t bus_id, i2c_mux_state_t *i2c_mux)
{
  if (i2c_mux->i2c_interface == i2c_bus_map[i2c_chip_map[CHIP_ID_MUX_0].bus_id].i2c_interface) {
      /* Include enable bit (fourth bit) on channel selection byte */
      uint8_t tca_channel;

      portENABLE_INTERRUPTS();
      /* Read bus state (other master on the bus may have switched it */
      xI2CMasterRead( i2c_bus_map[i2c_chip_map[CHIP_ID_MUX_0].bus_id].i2c_interface, i2c_chip_map[CHIP_ID_MUX_0].i2c_address, &tca_channel, 1 );

      /* Convert bit position from tca register to actual channel number */
      uint8_t num;
      for (num = 0; num <= 8 ; num++)
      {
      	if (tca_channel & 1 << num)
      		break;
      }
      return num;

  } else {
      return i2c_mux->state;
  }
}
