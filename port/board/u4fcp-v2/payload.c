/*
 *   openMMC -- Open Source modular IPM Controller firmware
 *
 *   Copyright (C) 2015  Piotr Miedzik  <P.Miedzik@gsi.de>
 *   Copyright (C) 2015-2016  Henrique Silva <henrique.silva@lnls.br>
 *   Copyright (C) 2021  Krzysztof Macias <krzysztof.macias@creotech.pl>
 *   Copyright (C) 2025  Jie Zhang <zhj@ihep.ac.cn>
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

/* FreeRTOS Includes */
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "event_groups.h"

/* Project Includes */
#include "port.h"
#include "payload.h"
#include "ipmi.h"
#include "task_priorities.h"
#include "hotswap.h"
#include "utils.h"
#include "fru.h"
#include "led.h"
#include "board_led.h"
#include "board_config.h"
#include "eeprom_24xx512.h"
#include "i2c.h"
#include "i2c_mapping.h"
#ifdef MODULE_ADN4604
#include "adn4604.h"
#include "clock_config.h"
#include "default_0-Registers.h"
#include "default_1-Registers.h"
#endif
#ifdef MODULE_VIO_TPL0102
#include "tpl0102.h"
#endif
#ifdef MODULE_UCD90XXX
#include "ucd90xxx.h"
extern ucd_data_t ucd_data[SDR_CH_COUNT];
#endif


/* payload states
 *   0 - No power
 *
 *   1 - Power Good wait
 *       Enable DCDC Converters
 *       Hotswap backend power failure and shutdown status clear
 *
 *   2 - FPGA setup
 *       One-time configurations (clock switch - ADN4604)
 *
 *   3 - FPGA on
 *
 *   4 - Power switching off
 *       Disable DCDC Converters
 *       Send "quiesced" event if requested
 *
 *   5 - Power quiesced
 *       Payload was safely turned off
 *       Wait until payload power goes down to restart the cycle
 */

/**
 * @brief Set uFC's DCDC Converters state
 *
 * @param on DCDCs state
 *
 */
uint8_t setDC_DC_ConvertersON(bool on)
{
  if (on)
  {
    printf("Enable Main Power\n");
    gpio_set_pin_high(PIN_PORT(GPIO_PMBUS_CTRL), PIN_NUMBER(GPIO_PMBUS_CTRL));
  }
  else
  {
    printf("Disable Main Power\n");
    gpio_set_pin_low(PIN_PORT(GPIO_PMBUS_CTRL), PIN_NUMBER(GPIO_PMBUS_CTRL));
  }
  return 1;
}

static void fpga_soft_reset(void)
{
  gpio_set_pin_low(PIN_PORT(GPIO_FPGA_RESET_B), PIN_NUMBER(GPIO_FPGA_RESET_B));
  asm("NOP");
  printf("Reset FPGA\n");
  gpio_set_pin_high(PIN_PORT(GPIO_FPGA_RESET_B), PIN_NUMBER(GPIO_FPGA_RESET_B));

  /* Blink BLUE LED to indicate to the user that the Reset was performed */
  LEDUpdate(FRU_AMC, LED_BLUE, LEDMODE_LAMPTEST, LEDINIT_ON, 5, 0);
}

// TCA9554
#define TCA9554_INPUT_REG    0
#define TCA9554_OUTPUT_REG   1
#define TCA9554_POLARITY_REG 2
#define TCA9554_CFG_REG      3

# define JTAG_RTM_EN_B  0x80
# define JTAG_FMC1_EN_B 0x40
# define JTAG_FMC0_EN_B 0x20
# define JTAG_AMC_EN    0x10

mmc_err config_jtag(void)
{
  bool standalone_mode = false;
  uint8_t i2c_addr, i2c_interface;
  uint8_t i2c_written = 0;
  uint8_t err_cnt = 0;
  uint8_t tx_data[2];

  if (get_ipmb_addr() == IPMB_ADDR_DISCONNECTED) {
      standalone_mode = true;
  }

  tx_data[0] = TCA9554_OUTPUT_REG;
  // Disable AMC, FMC0, FMC1, RTM as default
  tx_data[1] = JTAG_FMC0_EN_B | JTAG_FMC1_EN_B | JTAG_RTM_EN_B;

#define JTAG_FMC0_ENABLE 0
#define JTAG_FMC1_ENABLE 0

  if (JTAG_FMC0_ENABLE) tx_data[1] &= ~JTAG_FMC0_EN_B;
  if (JTAG_FMC1_ENABLE) tx_data[1] &= ~JTAG_FMC1_EN_B;
  if(!standalone_mode) tx_data[1] |= JTAG_AMC_EN;
  if(!ucd_get_gpio(&ucd_data[0], UCD_GPI_RTM_PS_B_ID))  tx_data[1] &= ~JTAG_RTM_EN_B;
  printf("JTAG Setup:\n");
  printf("\tAMC\tRTM\tFMC0\tFMC1\n");
  printf("\t%d\t%d\t%d\t%d\n",
    ((tx_data[1] & JTAG_AMC_EN)?1:0),
    ((tx_data[1] & JTAG_RTM_EN_B)?0:1),
    ((tx_data[1] & JTAG_FMC0_EN_B)?0:1),
    ((tx_data[1] & JTAG_FMC1_EN_B)?0:1)
  );

  // set output
  i2c_written = 0;
  while (i2c_written != 2)
  {

    if (i2c_take_by_chipid(CHIP_ID_TCA9554_JTAG_0, &i2c_addr, &i2c_interface, portMAX_DELAY) == pdTRUE)
    {
      i2c_written = xI2CMasterWrite(i2c_interface, i2c_addr, tx_data, 2);
      i2c_give(i2c_interface);
    }
    if (i2c_written != 2)
    {
      err_cnt++;
      if (err_cnt == 0xF)
        return MMC_IO_ERR;
      else
        vTaskDelay(pdMS_TO_TICKS(5)); /* Avoid too much unnecessary I2C trafic*/
    }
  }

  // set config as output: 0 = output
  i2c_written = 0;
  while (i2c_written != 2)
  {
    tx_data[0] = TCA9554_CFG_REG;
    tx_data[1] = (uint8_t)(~(JTAG_AMC_EN | JTAG_FMC0_EN_B | JTAG_FMC1_EN_B | JTAG_RTM_EN_B));
    if (i2c_take_by_chipid(CHIP_ID_TCA9554_JTAG_0, &i2c_addr, &i2c_interface, portMAX_DELAY) == pdTRUE)
    {
      i2c_written = xI2CMasterWrite(i2c_interface, i2c_addr, tx_data, 2);
      i2c_give(i2c_interface);
    }
    if (i2c_written != 2)
    {
      err_cnt++;
      if (err_cnt == 0xF)
        return MMC_IO_ERR;
      else
        vTaskDelay(pdMS_TO_TICKS(5)); /* Avoid too much unnecessary I2C trafic*/
    }
  }

  return MMC_OK;
}


#ifdef MODULE_UCD90XXX
uint8_t payload_check_pgood()
{
  sensor_t *p_sensor;
  SDR_type_01h_t *sdr;
  extern const SDR_type_01h_t SDR_AMC_12V;
  /* Iterate through the SDR Table to find all the ucd90xxx entries */
  for (p_sensor = sdr_head; (p_sensor != NULL) || (p_sensor->task_handle == NULL); p_sensor = p_sensor->next)
  {
    if (p_sensor->sdr == &SDR_AMC_12V)
    {
      sdr = (SDR_type_01h_t *)p_sensor->sdr;
      if ((p_sensor->readout_value >= (sdr->lower_critical_thr)) && (p_sensor->readout_value <= (sdr->upper_critical_thr)))
        return 1;
      else
        return 0;
    }
  }
  return 0;
}

uint8_t dcdc_check_pgood()
{
  sensor_t *p_sensor;
  SDR_type_01h_t *sdr;
  extern const SDR_type_01h_t SDR_AMC_IO_3V3;
  /* Iterate through the SDR Table to find all the ucd90xxx entries */
  for (p_sensor = sdr_head; (p_sensor != NULL) || (p_sensor->task_handle == NULL); p_sensor = p_sensor->next)
  {
    if (p_sensor->sdr == &SDR_AMC_IO_3V3)
    {
      sdr = (SDR_type_01h_t *)p_sensor->sdr;
      if ((p_sensor->readout_value >= (sdr->lower_critical_thr)) && (p_sensor->readout_value <= (sdr->upper_critical_thr)))
        return 1;
      else
        return 0;
    }
  }
  return 0;
}

#endif

#ifdef MODULE_VIO_TPL0102

// Vout = 0.6V * (60.4k/(30.1k + dac * 100k / 256)+1)
#define AMC_CONV_VLOT(voltage) (uint8_t)((60.4 / ((voltage) / 0.6 - 1) - 30.2) * 256 / 100)
#define AMC_CONV_DAC(dac) (float)(0.6 * (60.4 / (30.2 + (dac) * 100 / 256) + 1))

void tpl0102_set_amc_voltage(uint8_t chn, float voltage, bool use_eeprom)
{
  uint8_t dac;
  if (voltage > 1.8)
    voltage = 1.8;
  if (voltage < 0.9)
    voltage = 0.9;

  dac = AMC_CONV_VLOT(voltage);
#ifdef DEBUG
  printf("Set FMC%d DAC=%d\n", chn, dac);
#endif
  if (use_eeprom)
    tpl0102_set_non_volatile_val(chn, dac);
  else
    tpl0102_set_val(chn, dac);
}

float tpl0102_get_amc_voltage(uint8_t chn)
{
  return AMC_CONV_DAC(tpl0102_get_val(chn));
}

#endif

#ifdef MODULE_ADN4604

mmc_err pll_configuration(uint8_t chip_id, const si5345_revd_register_t *si5345_revd_registers_ptr)
{
  uint8_t page_curr, page_prev = 0xFF;
  uint32_t i;
  uint8_t i2c_addr, i2c_interface;
  uint8_t i2c_written = 0;
  uint8_t err_cnt = 0;
  uint8_t tx_data[2];

  for (i = 0; i < SI5345_REVD_REG_CONFIG_NUM_REGS; i++)
  {
    if (i == 3)
      vTaskDelay(pdMS_TO_TICKS(300));
    page_curr = (uint8_t)((si5345_revd_registers_ptr[i].address >> 8) & 0xFF);
    tx_data[0] = 0x1;
    tx_data[1] = page_curr;
    if (page_curr != page_prev)
    {
      err_cnt = 0;
      i2c_written = 0;
      while (i2c_written != 2)
      {
        if (i2c_take_by_chipid(chip_id, &i2c_addr, &i2c_interface, portMAX_DELAY) == pdTRUE)
        {
          i2c_written = xI2CMasterWrite(i2c_interface, i2c_addr, tx_data, 2);
          i2c_give(i2c_interface);
        }
        if (i2c_written != 2)
        {
          err_cnt++;
          if (err_cnt == 0xF)
            return MMC_IO_ERR;
          else
            vTaskDelay(pdMS_TO_TICKS(5)); /* Avoid too much unnecessary I2C trafic*/
        }
        // else printf("PLL write 0x%x to 0x%x\n", tx_data[1], tx_data[0]);
      }
    }
    tx_data[0] = (uint8_t)(si5345_revd_registers_ptr[i].address & 0xFF);
    tx_data[1] = si5345_revd_registers_ptr[i].value;
    err_cnt = 0;
    i2c_written = 0;
    while (i2c_written != 2)
    {
      if (i2c_take_by_chipid(chip_id, &i2c_addr, &i2c_interface, portMAX_DELAY) == pdTRUE)
      {
        i2c_written = xI2CMasterWrite(i2c_interface, i2c_addr, tx_data, 2);
        i2c_give(i2c_interface);
      }
      if (i2c_written != 2)
      {
        err_cnt++;
        if (err_cnt == 0xF)
          return MMC_IO_ERR;
        else
          vTaskDelay(pdMS_TO_TICKS(5)); /* Avoid too much unnecessary I2C trafic*/
      }
      // else printf("PLL write 0x%x to 0x%x\n", tx_data[1], tx_data[0]);
    }
    page_prev = page_curr;
  }
  return MMC_OK;
}

// Inputs for ADN4604
#define ADN4604_IN_AMC_TX_P17 0
#define ADN4604_IN_AMC_RX_P17 1
#define ADN4604_IN_TCLKD 2
#define ADN4604_IN_TCLKC 3
#define ADN4604_IN_TCLKA 4
#define ADN4604_IN_TCLKB 5
#define ADN4604_IN_FCLKA 6
#define ADN4604_IN_FPGA_CLK_OUT 7
#define ADN4604_IN_PLL1_OUT 8
#define ADN4604_IN_PLL0_OUT 9
#define ADN4604_IN_FMC1_CLK_M2C 10
#define ADN4604_IN_FMC1_CLK_BIDIR 11
#define ADN4604_IN_FMC0_CLK_M2C 12
#define ADN4604_IN_FMC0_CLK_BIDIR 13
#define ADN4604_IN_LEMO_CLK 14
#define ADN4604_IN_RTM2AMC_CLK 15

#define ADN4604_OUTPUT_EN 0x80

mmc_err clock_configuration(const uint8_t clk_cfg[16])
{
  adn_connect_map_t con;
  mmc_err error;

  /* Disable input and output termination*/
  error = adn4604_termination_ctl(0xF);
  if (error != MMC_OK)
  {
    return error;
  }

  /* Translate the configuration to enable or disable the outputs */
  uint16_t out_enable_flag = {
      ((clk_cfg[0] & 0x80) >> 7) << 0 |
      ((clk_cfg[1] & 0x80) >> 7) << 1 |
      ((clk_cfg[2] & 0x80) >> 7) << 2 |
      ((clk_cfg[3] & 0x80) >> 7) << 3 |
      ((clk_cfg[4] & 0x80) >> 7) << 4 |
      ((clk_cfg[5] & 0x80) >> 7) << 5 |
      ((clk_cfg[6] & 0x80) >> 7) << 6 |
      ((clk_cfg[7] & 0x80) >> 7) << 7 |
      ((clk_cfg[8] & 0x80) >> 7) << 8 |
      ((clk_cfg[9] & 0x80) >> 7) << 9 |
      ((clk_cfg[10] & 0x80) >> 7) << 10 |
      ((clk_cfg[11] & 0x80) >> 7) << 11 |
      ((clk_cfg[12] & 0x80) >> 7) << 12 |
      ((clk_cfg[13] & 0x80) >> 7) << 13 |
      ((clk_cfg[14] & 0x80) >> 7) << 14 |
      ((clk_cfg[15] & 0x80) >> 7) << 15};

  /* Configure the interconnects*/
  con.out0 = clk_cfg[0] & 0x0F;
  con.out1 = clk_cfg[1] & 0x0F;
  con.out2 = clk_cfg[2] & 0x0F;
  con.out3 = clk_cfg[3] & 0x0F;
  con.out4 = clk_cfg[4] & 0x0F;
  con.out5 = clk_cfg[5] & 0x0F;
  con.out6 = clk_cfg[6] & 0x0F;
  con.out7 = clk_cfg[7] & 0x0F;
  con.out8 = clk_cfg[8] & 0x0F;
  con.out9 = clk_cfg[9] & 0x0F;
  con.out10 = clk_cfg[10] & 0x0F;
  con.out11 = clk_cfg[11] & 0x0F;
  con.out12 = clk_cfg[12] & 0x0F;
  con.out13 = clk_cfg[13] & 0x0F;
  con.out14 = clk_cfg[14] & 0x0F;
  con.out15 = clk_cfg[15] & 0x0F;

  error = adn4604_xpt_config(ADN_XPT_MAP0_CON_REG, con);
  if (error != MMC_OK)
  {
    return error;
  }

  /* Enable desired outputs */
  for (uint8_t i = 0; i < 16; i++)
  {
    if ((out_enable_flag >> i) & 0x1)
    {
      error = adn4604_tx_control(i, TX_ENABLED);
      if (error != MMC_OK)
      {
        return error;
      }
    }
    else
    {
      error = adn4604_tx_control(i, TX_DISABLED);
      if (error != MMC_OK)
      {
        return error;
      }
    }
  }

  error = adn4604_active_map(ADN_XPT_MAP0);
  if (error != MMC_OK)
  {
    return error;
  }

  return adn4604_update();
}
#endif

EventGroupHandle_t amc_payload_evt = NULL;
#ifdef MODULE_RTM
EventGroupHandle_t rtm_payload_evt = NULL;
#endif

void payload_send_message(uint8_t fru_id, EventBits_t msg)
{
  if ((fru_id == FRU_AMC) && amc_payload_evt)
  {
    xEventGroupSetBits(amc_payload_evt, msg);
#ifdef MODULE_RTM
  }
  else if ((fru_id == FRU_RTM) && rtm_payload_evt)
  {
    xEventGroupSetBits(rtm_payload_evt, msg);
#endif
  }
}

TaskHandle_t vTaskPayload_Handle;

void payload_init(void)
{
  /* Set standalone mode if the module is disconnected from a create*/
  xTaskCreate(vTaskPayload, "Payload", 256, NULL, tskPAYLOAD_PRIORITY, &vTaskPayload_Handle);

  amc_payload_evt = xEventGroupCreate();
#ifdef MODULE_RTM
  rtm_payload_evt = xEventGroupCreate();
  mcp23016_write_pin(ext_gpios[EXT_GPIO_EN_RTM_MP].port_num, ext_gpios[EXT_GPIO_EN_RTM_MP].pin_num, true);
#endif
}

void vTaskPayload(void *pvParameters)
{
#ifdef MODULE_VIO_TPL0102

#define WRITE_VADJ_VAL 		1.8

  /* Configure the PVADJ DAC */
#ifdef FRU_WRITE_EEPROM
		tpl0102_set_amc_voltage(0, WRITE_VADJ_VAL, true);
		tpl0102_set_amc_voltage(1, WRITE_VADJ_VAL, true);
#endif
  float voltage;
	// print vadj setting
	voltage = tpl0102_get_amc_voltage(0);
  printf("FMC0 Vadj = %d mV\n", (int)(1000*voltage));
  tpl0102_get_amc_voltage(1);
  printf("FMC1 Vadj = %d mV\n", (int)(1000*voltage));

#endif

  uint8_t clock_config[16];
  clock_config[0] = ADN4604_IN_PLL1_OUT;
  clock_config[1] = ADN4604_IN_PLL1_OUT | ADN4604_OUTPUT_EN;
  clock_config[2] = ADN4604_IN_PLL1_OUT;
  clock_config[3] = ADN4604_IN_PLL1_OUT;
  clock_config[4] = ADN4604_IN_PLL1_OUT;
  clock_config[5] = ADN4604_IN_PLL1_OUT;
  clock_config[6] = ADN4604_IN_PLL1_OUT;
  clock_config[7] = ADN4604_IN_PLL1_OUT;
  clock_config[8] = ADN4604_IN_PLL1_OUT;
  clock_config[9] = ADN4604_IN_PLL1_OUT;
  clock_config[10] = ADN4604_IN_PLL1_OUT;
  clock_config[11] = ADN4604_IN_PLL1_OUT;
  clock_config[12] = ADN4604_IN_PLL1_OUT;
  clock_config[13] = ADN4604_IN_PLL1_OUT;
  clock_config[14] = ADN4604_IN_PLL1_OUT;
  clock_config[15] = ADN4604_IN_PLL1_OUT;

  uint8_t state = PAYLOAD_NO_POWER;
  /* Use arbitrary state value to force the first state update */
  uint8_t new_state = -1;

  /* Payload power good flag */
  uint8_t PP_good = 0;

  /* Payload DCDCs good flag */
  uint8_t DCDC_good = 0;

  // uint8_t ucd_id_str[33];
  // ucd_id_str[32] = '\0';
  // ucd_read_id(&ucd_data[0], ucd_id_str);
  // printf("%s\n", ucd_id_str);

/* FPGA program done flag */
#define FPGA_PROM_TIMEOUT 100000 // ms
#define FPGA_PROM_MAX_CNT (FPGA_PROM_TIMEOUT / PAYLOAD_BASE_DELAY)
  uint8_t FPGA_prom_timer = 0;
  uint8_t FPGA_prom_done = 0;

  uint8_t QUIESCED_req = 0;
  EventBits_t current_evt;

  extern sensor_t *hotswap_amc_sensor;

  TickType_t xLastWakeTime;
  xLastWakeTime = xTaskGetTickCount();

#ifdef MODULE_ADN4604
  /* Recover clock switch configuration saved in EEPROM */
  // eeprom_24xx512_read(CHIP_ID_RTC_EEPROM, 0x0, clock_config, 16, 10);
#endif

  for (;;)
  {
    new_state = state;
    // printf("Payload status = %d\n", state);

    current_evt = xEventGroupGetBits(amc_payload_evt);

    /*
     * When receive a PAYLOAD_MESSAGE_CLOCK_CONFIG message, configure the clock switch
     * and write the new configuration in EEPROM
     */
    if (current_evt & PAYLOAD_MESSAGE_CLOCK_CONFIG)
    {
#ifdef MODULE_ADN4604
      // eeprom_24xx02_write(CHIP_ID_RTC_EEPROM, 0x0, clock_config, 16, 10);
      if (PAYLOAD_FPGA_ON)
      {
        if (adn4604_reset() == MMC_OK)
          printf("ADN4604 reset OK\n");
        else
          printf("ADN4604 reset ERROR\n");
        if (clock_configuration(clock_config) == MMC_OK)
          printf("ADN4604 config OK\n");
        else
          printf("ADN4604 config ERROR\n");
        if (pll_configuration(CHIP_ID_SI5345_0, si5345_revd_registers_0) == MMC_OK)
          printf("PLL0 config OK\n");
        else
          printf("PLL0 config ERROR\n");
        if (pll_configuration(CHIP_ID_SI5345_1, si5345_revd_registers_1) == MMC_OK)
          printf("PLL1 config OK\n");
        else
          printf("PLL1 config ERROR\n");
      }
#endif
      xEventGroupClearBits(amc_payload_evt, PAYLOAD_MESSAGE_CLOCK_CONFIG);
    }
    if (current_evt & PAYLOAD_MESSAGE_COLD_RST)
    {
      state = PAYLOAD_RESET;
      xEventGroupClearBits(amc_payload_evt, PAYLOAD_MESSAGE_COLD_RST);
    }

    if ((current_evt & PAYLOAD_MESSAGE_REBOOT) || (current_evt & PAYLOAD_MESSAGE_WARM_RST))
    {
      fpga_soft_reset();
      xEventGroupClearBits(amc_payload_evt, PAYLOAD_MESSAGE_REBOOT | PAYLOAD_MESSAGE_WARM_RST);
    }

    if (current_evt & PAYLOAD_MESSAGE_QUIESCE)
    {
      /*
       * If you issue a shutdown fru command in the MCH shell, the payload power
       * task will receive a PAYLOAD_MESSAGE_QUIESCE message and set the
       * QUIESCED_req flag to '1' and the MCH will shutdown the 12VP0 power,
  {     * maki}ng the payload power task go to PAYLOAD_NO_POWER state.
       * So, if we are in the PAYLOAD_QUIESCED state and receive a
       * PAYLOAD_MESSAGE_QUIESCE message, the QUIESCED_req flag
       * should be '0'
       */
      if (state == PAYLOAD_QUIESCED)
        QUIESCED_req = 0;
      else
        QUIESCED_req = 1;
#ifdef DEBUG
      printf("QUIESCED received\n");
#endif
      xEventGroupClearBits(amc_payload_evt, PAYLOAD_MESSAGE_QUIESCE);
    }

#ifdef MODULE_UCD90XXX
    PP_good = payload_check_pgood();
    // DCDC_good = ucd_get_gpio(&ucd_data[0], UCD_GPI_PG_ID);
    DCDC_good = dcdc_check_pgood();
#else
    PP_good = 1;
    DCDC_good = 1;
#endif

    switch (state)
    {

    case PAYLOAD_NO_POWER:
      if (PP_good)
      {
        /* Turn DCDC converters on */
        setDC_DC_ConvertersON(true);
        /* Clear hotswap sensor backend power failure bits */
        hotswap_clear_mask_bit(HOTSWAP_AMC, HOTSWAP_BACKEND_PWR_SHUTDOWN_MASK);
        hotswap_clear_mask_bit(HOTSWAP_AMC, HOTSWAP_BACKEND_PWR_FAILURE_MASK);
        new_state = PAYLOAD_POWER_GOOD_WAIT;
      }
      break;

    case PAYLOAD_POWER_GOOD_WAIT:
      if (QUIESCED_req || (PP_good == 0)) new_state = PAYLOAD_SWITCHING_OFF;
      if (DCDC_good == 1)
      {
        printf("Enable FMC0 Power\n");
        ucd_set_gpio(&ucd_data[0], UCD_GPO_FMC0_3V3AUX_EN_ID, 1);
        ucd_set_power(&ucd_data[0], UCD_FMC0_VADJ_ID, 1);
        // ucd_set_gpio(&ucd_data[0], UCD_GPO_FMC0_PG_C2M_ID, 1);
        vTaskDelay(1000/portTICK_PERIOD_MS);

        printf("Enable FMC1 Power\n");
        ucd_set_gpio(&ucd_data[0], UCD_GPO_FMC1_3V3AUX_EN_ID, 1);
        ucd_set_power(&ucd_data[0], UCD_FMC1_VADJ_ID, 1);
        // ucd_set_gpio(&ucd_data[0], UCD_GPO_FMC1_PG_C2M_ID, 1);
        vTaskDelay(1000/portTICK_PERIOD_MS);

        // Check RTM
        // ucd_set_gpio(&ucd_data[0], UCD_GPO_RTM_MP_EN_ID, 1);
        if (ucd_get_gpio(&ucd_data[0], UCD_GPI_RTM_PS_B_ID))
        {
          printf("No RTM\n");
        }
        else
        {
          ucd_set_power(&ucd_data[0], UCD_RTM_12V_CURR_ID, 1);
          printf("Enable RTM Power\n");
        }

        // Turn on the green light
        LEDUpdate( FRU_AMC, LED2, LEDMODE_OVERRIDE, LEDINIT_ON, 0, 0 );
        // config JTAG
        if (config_jtag() != MMC_OK)
          printf("JTAG config ERROR\n");
        new_state = PAYLOAD_STATE_CLK_SETUP;
      }
      break;

    case PAYLOAD_STATE_CLK_SETUP:
      if (QUIESCED_req || (PP_good == 0)) new_state = PAYLOAD_SWITCHING_OFF;
#ifdef MODULE_ADN4604
      /* Configure clock switch */
      if (adn4604_reset() == MMC_OK)
      {
        if (clock_configuration(clock_config) == MMC_OK)
        {
          printf("ADN4604 config OK\n");
          if (pll_configuration(CHIP_ID_SI5345_0, si5345_revd_registers_0) == MMC_OK)
          {
            printf("PLL0 config OK\n");
            if (pll_configuration(CHIP_ID_SI5345_1, si5345_revd_registers_1) == MMC_OK)
            {
              printf("PLL1 config OK\n");
              // Only change the state if the clock configuration succeeds
              FPGA_prom_timer = 0;
              new_state = PAYLOAD_STATE_FPGA_SETUP;
            }
            else
              printf("PLL1 config ERROR\n");
          }
          else
            printf("PLL0 config ERROR\n");
        }
        else
          printf("ADN4604 config ERROR\n");
      }
      else
        printf("ADN4604 reset ERROR\n");
#else
      new_state = PAYLOAD_STATE_FPGA_SETUP;
#endif
      break;

    case PAYLOAD_STATE_FPGA_SETUP:
      if (QUIESCED_req || (PP_good == 0)) new_state = PAYLOAD_SWITCHING_OFF;
      FPGA_prom_done = gpio_read_pin(PIN_PORT(GPIO_FPGA_DONE), PIN_NUMBER(GPIO_FPGA_DONE));
      if (FPGA_prom_done)
      {
        // reset FPGA
        fpga_soft_reset();
        new_state = PAYLOAD_FPGA_ON;
      }
      else
      {
        FPGA_prom_timer++;
        if (FPGA_prom_timer == FPGA_PROM_MAX_CNT)
        {
          printf("FPGA program timeout\n");
          /* Blink RED LED to indicate to the user that the Reset was performed */
          LEDUpdate(FRU_AMC, LED1, LEDMODE_LAMPTEST, LEDINIT_ON, 5, 0);
          new_state = PAYLOAD_FPGA_ON;
        }
      }
      break;

    case PAYLOAD_FPGA_ON:
      if (QUIESCED_req == 1 || PP_good == 0 || DCDC_good == 0)
      {
        new_state = PAYLOAD_SWITCHING_OFF;
      }
      break;

    case PAYLOAD_SWITCHING_OFF:
      ucd_set_power(&ucd_data[0], UCD_RTM_12V_CURR_ID, 0);
      // ucd_set_gpio(&ucd_data[0], UCD_GPO_RTM_MP_EN_ID, 0);

      // ucd_set_gpio(&ucd_data[0], UCD_GPO_FMC0_PG_C2M_ID, 0);
      ucd_set_power(&ucd_data[0], UCD_FMC0_VADJ_ID, 0);
      ucd_set_gpio(&ucd_data[0], UCD_GPO_FMC0_3V3AUX_EN_ID, 0);

      // ucd_set_gpio(&ucd_data[0], UCD_GPO_FMC1_PG_C2M_ID, 0);
      ucd_set_power(&ucd_data[0], UCD_FMC1_VADJ_ID, 0);
      ucd_set_gpio(&ucd_data[0], UCD_GPO_FMC1_3V3AUX_EN_ID, 0);

      setDC_DC_ConvertersON(false);
      // Toggle the green light
      LEDUpdate( FRU_AMC, LED2, LEDMODE_OVERRIDE, LEDINIT_ON, 5, 5 );
      /* Respond to quiesce event if any */
      if (QUIESCED_req)
      {
        hotswap_set_mask_bit(HOTSWAP_AMC, HOTSWAP_QUIESCED_MASK);
        hotswap_send_event(hotswap_amc_sensor, HOTSWAP_STATE_QUIESCED);
        hotswap_clear_mask_bit(HOTSWAP_AMC, HOTSWAP_QUIESCED_MASK);
        QUIESCED_req = 0;
      }
      new_state = PAYLOAD_QUIESCED;
      break;

    case PAYLOAD_QUIESCED:
      /* Wait until power goes down to restart the cycle */
      if (PP_good == 0)
      {
        new_state = PAYLOAD_NO_POWER;
      }
      break;

    case PAYLOAD_RESET:
      /*Reset DCDC converters*/
      setDC_DC_ConvertersON(false);
      new_state = PAYLOAD_NO_POWER;
      break;

    default:
      break;
    }

    state = new_state;
    vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(PAYLOAD_BASE_DELAY));
  }
}

/* HPM Functions */
#ifdef MODULE_HPM

#include "flash_spi.h"
#include "string.h"

uint8_t *hpm_page = NULL;
uint8_t hpm_pg_index;
uint32_t hpm_page_addr;

uint8_t payload_hpm_prepare_comp(void)
{
  /* Initialize variables */
  if (hpm_page != NULL)
  {
    vPortFree(hpm_page);
  }

  hpm_page = (uint8_t *)pvPortMalloc(PAYLOAD_HPM_PAGE_SIZE);

  if (hpm_page == NULL)
  {
    /* Malloc failed */
    return IPMI_CC_OUT_OF_SPACE;
  }

  memset(hpm_page, 0xFF, PAYLOAD_HPM_PAGE_SIZE);

  hpm_pg_index = 0;
  hpm_page_addr = 0;

  /* Initialize flash */
  ssp_init(FLASH_SPI, FLASH_SPI_BITRATE, FLASH_SPI_FRAME_SIZE, SSP_MASTER, SSP_INTERRUPT);

  /* Prevent the FPGA from accessing the Flash to configure itself now */
  gpio_set_pin_state(PIN_PORT(GPIO_FPGA_PROGRAM_B), PIN_NUMBER(GPIO_FPGA_PROGRAM_B), GPIO_LEVEL_HIGH);
  gpio_set_pin_state(PIN_PORT(GPIO_FPGA_PROGRAM_B), PIN_NUMBER(GPIO_FPGA_PROGRAM_B), GPIO_LEVEL_LOW);
  gpio_set_pin_state(PIN_PORT(GPIO_FPGA_PROGRAM_B), PIN_NUMBER(GPIO_FPGA_PROGRAM_B), GPIO_LEVEL_HIGH);
  gpio_set_pin_state(PIN_PORT(GPIO_FPGA_PROGRAM_B), PIN_NUMBER(GPIO_FPGA_PROGRAM_B), GPIO_LEVEL_LOW);

  /* Erase FLASH */
  flash_bulk_erase();

  return IPMI_CC_COMMAND_IN_PROGRESS;
}

uint8_t payload_hpm_upload_block(uint8_t *block, uint16_t size)
{
  /* TODO: Check DONE pin before accessing the SPI bus, since the FPGA may be reading it in order to boot */
  uint8_t remaining_bytes_start;

  if (PAYLOAD_HPM_PAGE_SIZE - hpm_pg_index > size)
  {
    /* Our page is not full yet, just append the new data */
    memcpy(&hpm_page[hpm_pg_index], block, size);
    hpm_pg_index += size;

    return IPMI_CC_OK;
  }
  else
  {
    /* Complete the remaining bytes on the buffer */
    memcpy(&hpm_page[hpm_pg_index], block, (PAYLOAD_HPM_PAGE_SIZE - hpm_pg_index));
    remaining_bytes_start = (PAYLOAD_HPM_PAGE_SIZE - hpm_pg_index);

    /* Program the complete page in the Flash */
    flash_program_page(hpm_page_addr, &hpm_page[0], PAYLOAD_HPM_PAGE_SIZE);

    hpm_page_addr += PAYLOAD_HPM_PAGE_SIZE;

    /* Empty our buffer and reset the index */
    memset(hpm_page, 0xFF, PAYLOAD_HPM_PAGE_SIZE);
    hpm_pg_index = 0;

    /* Save the trailing bytes */
    memcpy(&hpm_page[hpm_pg_index], block + remaining_bytes_start, size - remaining_bytes_start);

    hpm_pg_index = size - remaining_bytes_start;

    return IPMI_CC_COMMAND_IN_PROGRESS;
  }
}

uint8_t payload_hpm_finish_upload(uint32_t image_size)
{
  uint8_t cc = IPMI_CC_OK;

  /* Check if the last page was already programmed */
  if (!hpm_pg_index)
  {
    /* Program the complete page in the Flash */
    flash_program_page(hpm_page_addr, &hpm_page[0], (PAYLOAD_HPM_PAGE_SIZE - hpm_pg_index));
    hpm_pg_index = 0;
    hpm_page_addr = 0;

    cc = IPMI_CC_COMMAND_IN_PROGRESS;
  }

  /* Free page buffer */
  vPortFree(hpm_page);
  hpm_page = NULL;

  return cc;
}

uint8_t payload_hpm_get_upgrade_status(void)
{
  if (is_flash_busy())
  {
    return IPMI_CC_COMMAND_IN_PROGRESS;
  }
  else
  {
    return IPMI_CC_OK;
  }
}

uint8_t payload_hpm_activate_firmware(void)
{
  /* Reset FPGA - Pulse PROGRAM_B pin */
  gpio_set_pin_state(PIN_PORT(GPIO_FPGA_PROGRAM_B), PIN_NUMBER(GPIO_FPGA_PROGRAM_B), GPIO_LEVEL_LOW);
  gpio_set_pin_state(PIN_PORT(GPIO_FPGA_PROGRAM_B), PIN_NUMBER(GPIO_FPGA_PROGRAM_B), GPIO_LEVEL_HIGH);

  return IPMI_CC_OK;
}
#endif
