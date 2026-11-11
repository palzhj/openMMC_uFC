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
#ifdef MODULE_CLOCK_STORE
#include "clock_store.h"
#include "clock_program.h"
#endif
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
    log_printk("pwr", "main power on");
    gpio_set_pin_high(PIN_PORT(GPIO_PMBUS_CTRL), PIN_NUMBER(GPIO_PMBUS_CTRL));
  }
  else
  {
    log_printk("pwr", "main power off");
    gpio_set_pin_low(PIN_PORT(GPIO_PMBUS_CTRL), PIN_NUMBER(GPIO_PMBUS_CTRL));
  }
  return 1;
}

static void fpga_soft_reset(void)
{
  gpio_set_pin_low(PIN_PORT(GPIO_FPGA_RESET_B), PIN_NUMBER(GPIO_FPGA_RESET_B));
  asm("NOP");
  log_printk("fpga", "reset");
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
  uint8_t cur_out = 0;
  bool rtm_present;

  if (get_ipmb_addr() == IPMB_ADDR_DISCONNECTED) {
      standalone_mode = true;
  }

  /* Preserve P0..P3 (AMC_TX/RX_EN17 on the schematic): only the JTAG
   * selection bits P4..P7 are rewritten. */
  tx_data[0] = TCA9554_OUTPUT_REG;
  i2c_written = 0;
  err_cnt = 0;
  while (i2c_written != 1)
  {
    if (i2c_take_by_chipid(CHIP_ID_TCA9554_JTAG_0, &i2c_addr, &i2c_interface, portMAX_DELAY) == pdTRUE)
    {
      i2c_written = xI2CMasterWriteRead(i2c_interface, i2c_addr, &tx_data[0], 1, &cur_out, 1);
      i2c_give(i2c_interface);
    }
    if (i2c_written != 1)
    {
      if (++err_cnt == 0xF)
        return MMC_IO_ERR;
      vTaskDelay(pdMS_TO_TICKS(5));
    }
  }

  /* P4 = CON_JTAG_EN_L (local connector, active low), P5 = FMC0, P6 = FMC1,
   * P7 = RTM.  Default: FMC0/FMC1/RTM off, connector on for a standalone
   * board, off when the card sits in a crate (the AMC path is used). */
  cur_out &= 0x0F;
  cur_out |= (standalone_mode ? 0 : JTAG_AMC_EN) |
             JTAG_FMC0_EN_B | JTAG_FMC1_EN_B | JTAG_RTM_EN_B;

  /* RTM JTAG only when the RTM is present (UCD GPIO); a missing/unresponsive
   * UCD is treated as 'no RTM' (the RTM enabled by mistake breaks the chain). */
  rtm_present = (ucd_get_gpio(&ucd_data[0], UCD_GPI_RTM_PS_B_ID) == 0);
  if (rtm_present) cur_out &= ~JTAG_RTM_EN_B;

  log_printk("jtag", "CON=%d RTM=%d FMC0=%d FMC1=%d",
    (!standalone_mode ? 1 : 0), rtm_present, 0, 0);

  /* Config first (P4..P7 outputs, P0..P3 inputs), then the output register. */
  tx_data[0] = TCA9554_CFG_REG;
  tx_data[1] = (uint8_t)(~(JTAG_AMC_EN | JTAG_FMC0_EN_B | JTAG_FMC1_EN_B | JTAG_RTM_EN_B));
  i2c_written = 0;
  err_cnt = 0;
  while (i2c_written != 2)
  {
    if (i2c_take_by_chipid(CHIP_ID_TCA9554_JTAG_0, &i2c_addr, &i2c_interface, portMAX_DELAY) == pdTRUE)
    {
      i2c_written = xI2CMasterWrite(i2c_interface, i2c_addr, tx_data, 2);
      i2c_give(i2c_interface);
    }
    if (i2c_written != 2)
    {
      if (++err_cnt == 0xF)
        return MMC_IO_ERR;
      vTaskDelay(pdMS_TO_TICKS(5));
    }
  }

  tx_data[0] = TCA9554_OUTPUT_REG;
  tx_data[1] = cur_out;
  i2c_written = 0;
  err_cnt = 0;
  while (i2c_written != 2)
  {
    if (i2c_take_by_chipid(CHIP_ID_TCA9554_JTAG_0, &i2c_addr, &i2c_interface, portMAX_DELAY) == pdTRUE)
    {
      i2c_written = xI2CMasterWrite(i2c_interface, i2c_addr, tx_data, 2);
      i2c_give(i2c_interface);
    }
    if (i2c_written != 2)
    {
      if (++err_cnt == 0xF)
        return MMC_IO_ERR;
      vTaskDelay(pdMS_TO_TICKS(5));
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
  log_printk("vadj", "FMC%d DAC=%d", chn, dac);
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
mmc_err pll_configuration(uint8_t chip_id, const si5345_revd_register_t *si5345_revd_registers_ptr);
mmc_err clock_configuration(const uint8_t clk_cfg[16]);

#define ADN4604_IN_PLL1_OUT 8
#define ADN4604_OUTPUT_EN 0x80

/* Default ADN4604 crosspoint configuration, used when the EEPROM holds no
 * clock configuration. Bit 7 = output enable, bits 3:0 = input select.
 * Default: every output routed from input 8 (PLL1 out) but DISABLED. */
static const uint8_t clock_config_default[16] = {
  ADN4604_IN_PLL1_OUT, ADN4604_IN_PLL1_OUT, ADN4604_IN_PLL1_OUT, ADN4604_IN_PLL1_OUT,
  ADN4604_IN_PLL1_OUT, ADN4604_IN_PLL1_OUT, ADN4604_IN_PLL1_OUT, ADN4604_IN_PLL1_OUT,
  ADN4604_IN_PLL1_OUT, ADN4604_IN_PLL1_OUT, ADN4604_IN_PLL1_OUT, ADN4604_IN_PLL1_OUT,
  ADN4604_IN_PLL1_OUT, ADN4604_IN_PLL1_OUT, ADN4604_IN_PLL1_OUT, ADN4604_IN_PLL1_OUT,
};

/* Print the lock status of both Si5345s.
 * Status: 0x0C00 live / 0x0C01 sticky: [0] SYSINCAL, [1] LOSXAXB, [2] LOL,
 * [3] LOS, [4] OOF, [5] HOLD. */
static int payload_clock_log_status(void)
{
  static const char *names[2] = { "PLL0", "PLL1" };
  static const uint8_t chips[2] = { CHIP_ID_SI5345_0, CHIP_ID_SI5345_1 };

  char state[2][10] = { "??", "??" };
  int ok = 0;

  for (uint8_t i = 0; i < 2; i++)
  {
    uint8_t live = 0xFF;
    if (!clock_program_pll_status(chips[i], &live, NULL))
    {
      strcpy(state[i], "err");
      ok = -1;
      continue;
    }
    if (live & (1u << 2))
    {
      ok = -1;
      strcpy(state[i], "not lock");
    }
    else
    {
      strcpy(state[i], "locked");
    }
  }
  log_printk("clk", "PLL0 %s, PLL1 %s", state[0], state[1]);
  return ok;
}

/* Configure the clock switch and both PLLs.
 *
 * The clock configuration stored in the EEPROM wins when a valid slot is
 * present; when the EEPROM holds no clock configuration the compiled defaults
 * (clock_config[] + the ClockBuilder register tables) are programmed. */
static mmc_err payload_clock_configure(void)
{
  /* Fresh apply on every payload power-up: the Si5345/ADN4604 are powered
   * from the main power, so they lose their configuration at every cycle. */
  if (clock_store_apply())
  {
    log_printk("clk", "apply from EEPROM preset: %u records, %u registers (%u ms)",
               (unsigned)clock_store_get_last_records(),
               (unsigned)clock_store_get_last_registers(),
               (unsigned)clock_store_get_last_ms());
    vTaskDelay(pdMS_TO_TICKS(100));   /* let the PLLs lock */
    payload_clock_log_status();
    return MMC_OK;
  }

  log_printk("clk", "config from defaults (EEPROM blank)");
  if (adn4604_reset() != MMC_OK)
  {
    log_printk("clk", "ADN4604 reset failed");
    return MMC_TIMEOUT_ERR;
  }
  if (clock_configuration(clock_config_default) != MMC_OK)
  {
    log_printk("clk", "ADN4604 config failed");
    return MMC_IO_ERR;
  }
  log_printk("clk", "ADN4604 configured");
  if (pll_configuration(CHIP_ID_SI5345_0, si5345_revd_registers_0) != MMC_OK)
  {
    log_printk("clk", "PLL0 config failed");
    return MMC_IO_ERR;
  }
  log_printk("clk", "PLL0 configured");
  if (pll_configuration(CHIP_ID_SI5345_1, si5345_revd_registers_1) != MMC_OK)
  {
    log_printk("clk", "PLL1 config failed");
    return MMC_IO_ERR;
  }
  log_printk("clk", "PLL1 configured");
  vTaskDelay(pdMS_TO_TICKS(100));   /* let the PLLs lock */
  payload_clock_log_status();
  return MMC_OK;
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
#define ADN4604_IN_PLL0_OUT 9
#define ADN4604_IN_FMC1_CLK_M2C 10
#define ADN4604_IN_FMC1_CLK_BIDIR 11
#define ADN4604_IN_FMC0_CLK_M2C 12
#define ADN4604_IN_FMC0_CLK_BIDIR 13
#define ADN4604_IN_LEMO_CLK 14
#define ADN4604_IN_RTM2AMC_CLK 15


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

/* When the payload is unpowered its rails read 0 V / 0 A, which the SDR
 * thresholds interpret as failures. Stop scanning/event generation for the
 * payload sensors while the power is down; the hotswap sensor (the module
 * state) always stays enabled. */
static void payload_sensors_scan(bool enable)
{
  for (sensor_t *p = sdr_head; p != NULL; p = p->next)
  {
    if (p->task_handle != NULL && vTaskHotSwap_Handle != NULL &&
        *p->task_handle == vTaskHotSwap_Handle)
    {
      continue;
    }
    if (enable)
    {
      sensor_enable(p);
    }
    else
    {
      p->state = SENSOR_STATE_NORMAL;
      sensor_disable(p);
    }
  }
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

#endif


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
      /* re-apply the clocking: EEPROM configuration when present, compiled
       * defaults otherwise */
      if (PAYLOAD_FPGA_ON)
      {
        (void) payload_clock_configure();
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
      {
        QUIESCED_req = 1;
        payload_sensors_scan(false);   /* power is about to be removed */
      }
#ifdef DEBUG
      log_printk("pwr", "quiesce request");
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
      payload_sensors_scan(false);   /* rails are down */
      if (PP_good)
      {
        /* A payload power cycle does not reset the MMC: mark it explicitly
         * so a log that starts here is not mistaken for an MMC reboot. */
        static uint8_t payload_powerups;
        if (payload_powerups++ > 0)
        {
          log_printk("pwr", "payload restart (MMC uptime %u s, no MMC reset)",
                     (unsigned)(xTaskGetTickCount() / configTICK_RATE_HZ));
        }
        payload_sensors_scan(true);
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
        log_printk("pwr", "FMC0 power on");
        ucd_set_gpio(&ucd_data[0], UCD_GPO_FMC0_3V3AUX_EN_ID, 1);
        ucd_set_power(&ucd_data[0], UCD_FMC0_VADJ_ID, 1);
        // ucd_set_gpio(&ucd_data[0], UCD_GPO_FMC0_PG_C2M_ID, 1);
        vTaskDelay(1000/portTICK_PERIOD_MS);

        log_printk("pwr", "FMC1 power on");
        ucd_set_gpio(&ucd_data[0], UCD_GPO_FMC1_3V3AUX_EN_ID, 1);
        ucd_set_power(&ucd_data[0], UCD_FMC1_VADJ_ID, 1);
        // ucd_set_gpio(&ucd_data[0], UCD_GPO_FMC1_PG_C2M_ID, 1);
        vTaskDelay(1000/portTICK_PERIOD_MS);

#ifdef MODULE_VIO_TPL0102
        /* report the FMC VADJ actually programmed on every power-up */
        log_printk("vadj", "FMC0 %d mV, FMC1 %d mV",
                   (int)(1000 * tpl0102_get_amc_voltage(0)),
                   (int)(1000 * tpl0102_get_amc_voltage(1)));
#endif

        // Check RTM
        // ucd_set_gpio(&ucd_data[0], UCD_GPO_RTM_MP_EN_ID, 1);
        if (ucd_get_gpio(&ucd_data[0], UCD_GPI_RTM_PS_B_ID))
        {
          log_printk("pwr", "no RTM");
        }
        else
        {
          ucd_set_power(&ucd_data[0], UCD_RTM_12V_CURR_ID, 1);
          log_printk("pwr", "RTM power on");
        }

        // Turn on the green light
        LEDUpdate( FRU_AMC, LED2, LEDMODE_OVERRIDE, LEDINIT_ON, 0, 0 );
        // config JTAG
        if (config_jtag() != MMC_OK)
          log_printk("jtag", "config failed");
        new_state = PAYLOAD_STATE_CLK_SETUP;
      }
      break;

    case PAYLOAD_STATE_CLK_SETUP:
      if (QUIESCED_req || (PP_good == 0)) new_state = PAYLOAD_SWITCHING_OFF;
#ifdef MODULE_ADN4604
      /* The switch and PLLs are powered from the main power, which is up now.
       * EEPROM configuration when present, compiled defaults otherwise. */
      if (payload_clock_configure() == MMC_OK)
      {
        // Only change the state if the clock configuration succeeds
        FPGA_prom_timer = 0;
        new_state = PAYLOAD_STATE_FPGA_SETUP;
        log_printk("boot", "payload power-up done");
      }
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
          log_printk("fpga", "programming timeout");
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
      /* rails are going down: stop scanning them before they fall out of
       * range, otherwise every sensor sends a lower-non-recoverable event */
      payload_sensors_scan(false);
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
      payload_sensors_scan(false);
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

/* HPM functions for the payload component.
 *
 * The u4FCP payload (FPGA configuration flash) is not updated through the
 * MMC: the FPGA flash sits behind the payload's SPI interface, which this
 * STM32 port does not drive. Report the component as not upgradable; the
 * MMC/bootloader components are handled by port/ucontroller/stm32/stm32f1xx/
 * stm32_hpm.c through the internal staging area. */
#ifdef MODULE_HPM
uint8_t payload_hpm_prepare_comp(void)
{
    return IPMI_CC_ILLEGAL_COMMAND_DISABLED;
}

uint8_t payload_hpm_upload_block(uint8_t *block, uint16_t size)
{
    (void)block;
    (void)size;
    return IPMI_CC_ILLEGAL_COMMAND_DISABLED;
}

uint8_t payload_hpm_finish_upload(uint32_t image_size)
{
    (void)image_size;
    return IPMI_CC_ILLEGAL_COMMAND_DISABLED;
}

uint8_t payload_hpm_get_upgrade_status(void)
{
    return IPMI_CC_ILLEGAL_COMMAND_DISABLED;
}

uint8_t payload_hpm_activate_firmware(void)
{
    return IPMI_CC_ILLEGAL_COMMAND_DISABLED;
}

#endif

#if defined(MODULE_CLOCK_STORE)
/* Clock-store board hooks: the 16-byte switch record uses the ADN4604
 * configuration format (bit 7 = output enable, bits 3:0 = input select),
 * and the FRU device-header address selects the PLL (0x68 = PLL0,
 * 0x69 = PLL1). */
int clock_store_apply_switch(const uint8_t *matrix, uint8_t addr)
{
    if (addr != 0x48) {
        return 0;   /* ADN4604 */
    }
    return clock_configuration(matrix) == MMC_OK;
}

/* the store calls these around one PLL record: hold the clock bus while the
 * chunk of registers is written (the EEPROM reads between records need it) */
int clock_store_apply_begin(uint8_t addr)
{
    if (addr == 0x68) {
        return clock_program_batch_begin(CHIP_ID_SI5345_0);
    }
    if (addr == 0x69) {
        return clock_program_batch_begin(CHIP_ID_SI5345_1);
    }
    return 0;
}

void clock_store_apply_end(void)
{
    clock_program_batch_end();
}

int clock_store_apply_si5345(uint8_t addr, uint16_t reg, uint8_t val)
{
    if (addr == 0x68) {
        clock_program_select_chip(CHIP_ID_SI5345_0);
    } else if (addr == 0x69) {
        clock_program_select_chip(CHIP_ID_SI5345_1);
    } else {
        return 0;
    }
    return clock_program_write_entry(reg, val);
}
#endif
