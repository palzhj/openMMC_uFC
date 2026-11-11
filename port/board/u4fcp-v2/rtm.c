/*
 * openMMC -- u4FCP-v2 AMC side RTM support
 *
 * The uRTM2.3 (rear transition module) has its own MMC; this file is the
 * AMC-side RTM control:
 *   - presence from RTM_PS# (PA8, low while the RTM is plugged and its
 *     handle is closed);
 *   - 12 V power sequencing with the uRTM2.3 startup policy: the RTM FPGA
 *     pulls a large inrush from the RTM MP3V3 rail if it is powered before
 *     the RTM controller configured its clocks, so the AMC waits a few
 *     seconds after RTM_PS# went low before enabling the 12 V, and it
 *     ignores the handle for a configuration window afterwards.
 */

#include "FreeRTOS.h"
#include "task.h"
#include "port.h"
#include "payload.h"
#include "rtm.h"
#include "ucd90xxx.h"
#include "utils.h"

extern ucd_data_t ucd_data[SDR_CH_COUNT];

/* AMC-side RTM timings (the RTM controller needs the 12 V a few seconds
 * after the handle closes; its clock configuration takes several more). */
#ifndef RTM_TURNON_DELAY_MS
#define RTM_TURNON_DELAY_MS   3000
#endif
#ifndef RTM_CONFIG_DELAY_MS
#define RTM_CONFIG_DELAY_MS  10000
#endif

static uint8_t rtm_12v_on;
static TickType_t rtm_ready_tick;

static bool rtm_ps_low(void)
{
  return gpio_read_pin(PIN_PORT(GPIO_RTM_PS_B), PIN_NUMBER(GPIO_RTM_PS_B)) == 0;
}

void rtm_init(void)
{
  rtm_12v_on = 0;
  rtm_ready_tick = 0;
}

bool rtm_present(void)
{
  return rtm_ps_low();
}

/* true while the RTM is allowed to reconfigure its clocks (the handle is
 * not interpreted during this window) */
bool rtm_config_window(void)
{
  return rtm_12v_on && ((int32_t)(xTaskGetTickCount() - rtm_ready_tick) < 0);
}

mmc_err rtm_overcurrent_check(void)
{
  /* RTM 12 V fault / management-power fault reported by the UCD */
  if (!ucd_get_gpio(&ucd_data[0], UCD_GPI_RTM_12V0_FLT_B_ID) ||
      !ucd_get_gpio(&ucd_data[0], UCD_GPI_RTM_MP_FLT_B_ID))
  {
    return MMC_IO_ERR;
  }
  return MMC_OK;
}

mmc_err rtm_enable_12v(void)
{
  if (!rtm_present()) {
    return MMC_IO_ERR;
  }
  if (rtm_12v_on) {
    return MMC_OK;
  }
  vTaskDelay(pdMS_TO_TICKS(RTM_TURNON_DELAY_MS));
  if (!rtm_present()) {
    return MMC_IO_ERR;   /* the handle was opened while waiting */
  }
  ucd_set_power(&ucd_data[0], UCD_RTM_12V_CURR_ID, 1);
  rtm_12v_on = 1;
  rtm_ready_tick = xTaskGetTickCount() + pdMS_TO_TICKS(RTM_CONFIG_DELAY_MS);
  log_printk("rtm", "RTM 12 V on (handle monitor in %u s)",
             RTM_CONFIG_DELAY_MS / 1000);
  return MMC_OK;
}

mmc_err rtm_disable_12v(void)
{
  if (!rtm_12v_on) {
    return MMC_OK;
  }
  ucd_set_power(&ucd_data[0], UCD_RTM_12V_CURR_ID, 0);
  rtm_12v_on = 0;
  rtm_ready_tick = 0;
  log_printk("rtm", "RTM 12 V off");
  return MMC_OK;
}

bool rtm_12v_is_on(void)
{
  return rtm_12v_on != 0;
}

/* handle monitor: RTM_PS# high after the configuration window = the handle
 * was opened -> the caller cuts the 12 V */
bool rtm_handle_removed(void)
{
  return rtm_12v_on && !rtm_config_window() && !rtm_ps_low();
}
