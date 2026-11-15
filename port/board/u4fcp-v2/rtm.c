/*
 * openMMC -- u4FCP-v2 AMC side RTM support
 *
 * The uRTM2.3 (rear transition module) has its own MMC; this file is the
 * AMC-side RTM control:
 *   - presence from RTM_PS# (PA8, low while the RTM is plugged; it does NOT
 *     follow the handle);
 *   - RTM_HANDLE on RTM_INT_B (PA3, active low, driven by the uRTM).  The
 *     line is only valid once the RTM is powered and its MCU has configured
 *     the RTM clocks (it then goes silent): while the RTM is unpowered or
 *     still configuring, the line is low like an open handle.  The startup
 *     policy is therefore "power a few seconds after RTM_PS# went low, then
 *     wait RTM_CONFIG_DELAY_MS before interpreting RTM_HANDLE" - see
 *     rtm_handle_removed();
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
#include "rtm_fru.h"
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
static uint8_t rtm_ps_known;
static uint8_t rtm_ps_last;

static bool rtm_ps_low(void)
{
  return gpio_read_pin(PIN_PORT(GPIO_RTM_PS_B), PIN_NUMBER(GPIO_RTM_PS_B)) == 0;
}

/* The uRTM drives RTM_INT_B low when its handle is opened (and for its own
 * attention state); the line is an input on the AMC. */
static bool rtm_int_b_low(void)
{
  return gpio_read_pin(PIN_PORT(GPIO_RTM_INT_B), PIN_NUMBER(GPIO_RTM_INT_B)) == 0;
}

void rtm_init(void)
{
  rtm_12v_on = 0;
  rtm_ready_tick = 0;
  /* Sample RTM_PS# now so a module plugged in after this point is seen as an
   * edge by rtm_plugged_edge() and gets its 12 V (the boot sequence powers
   * the RTM that is already there through PAYLOAD_STATE_RTM_SETUP). */
  rtm_ps_known = 1;
  rtm_ps_last = rtm_ps_low() ? 1 : 0;
}

/* Identify the RTM from its FRU (see rtm_fru.c).  Unknown until the module
 * answers: the uRTM only exposes its FRU once the 12 V is on. */
bool rtm_is_urtm_v23(void)
{
  return rtm_fru_is_urtm_v23();
}

bool rtm_handle_trusted(void)
{
  return rtm_fru_type() != RTM_FRU_OTHER;
}

/* RTM_PS# edge detector.  A plugged-in module is a "new" module: drop the
 * identification so it is read again (an unplug/replug may be another board). */
bool rtm_plugged_edge(void)
{
  bool low = rtm_ps_low();
  bool edge = false;

  if (!rtm_ps_known) {
    rtm_ps_known = 1;
    rtm_ps_last = low ? 1 : 0;
    return false;
  }
  edge = (!rtm_ps_last && low);
  rtm_ps_last = low ? 1 : 0;
  if (edge) {
    rtm_fru_invalidate();
  }
  return edge;
}

bool rtm_present(void)
{
  return rtm_ps_low();
}

bool rtm_attention(void)
{
  return rtm_int_b_low();
}

bool rtm_alive(void)
{
  return rtm_ps_low() && !rtm_int_b_low();
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
  /* The uRTM needs its controller to configure the RTM clocks before the
   * FPGA draws its inrush; boards that identify as something else (e.g. the
   * LattePanda carrier) get the 12 V at once.  An unidentified module gets
   * the conservative delay. */
  vTaskDelay(pdMS_TO_TICKS(rtm_fru_type() == RTM_FRU_OTHER ? 0
                                                          : RTM_TURNON_DELAY_MS));
  if (!rtm_present()) {
    return MMC_IO_ERR;   /* the module was removed while waiting */
  }
  ucd_set_power(&ucd_data[0], UCD_RTM_12V_CURR_ID, 1);
  rtm_12v_on = 1;
  /* the uRTM's FRU answers only now: retry the identification immediately */
  rtm_fru_invalidate();
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

/* handle monitor: after the RTM configuration window a still-asserted
 * RTM_INT_B means the uRTM kept its handle open (it is running by then, so
 * the line is no longer just "no power"), or the RTM was unplugged -> the
 * caller cuts the 12 V.  Two consecutive calls ride out the contact bounce. */
bool rtm_handle_removed(void)
{
  static uint8_t confirm;

  if (!rtm_12v_on || rtm_config_window() || (rtm_ps_low() && !rtm_int_b_low())) {
    confirm = 0;
    return false;
  }
  if (confirm < 2) {
    confirm++;
  }
  return confirm >= 2;
}
