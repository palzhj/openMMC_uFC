/*
 * openMMC -- u4FCP-v2 AMC-side RTM management
 *
 * Zone 3 (the rear transition module) is powered and managed entirely by this
 * task; the payload state machine only publishes whether the AMC payload
 * rails are up.  The policy, in one place:
 *
 *   - a module plugged in (RTM_PS#, PA8, low) gets its 12 V as soon as the
 *     AMC payload rails are up, no matter what the module is;
 *   - the rail comes up RTM_TURNON_DELAY_MS (1 s) after RTM_PS# went low;
 *   - the RTM bus (I2C2 ch5) is only ever touched while rtm_12v_on is true and
 *     the rail switch was confirmed - an unpowered RTM clamps SDA/SCL through
 *     its ESD diodes and wedges the whole I2C2 bus.  When the rail cannot be
 *     switched, the module is parked without a single probe;
 *   - the RTM bus is not touched during RTM_FRU_SETTLE_MS (5 s) after the rail
 *     came up: that is the time the module's own MCU needs to finish its I2C
 *     configuration (and an unpowered I2C slave would clamp SDA/SCL through its
 *     ESD diodes, wedging the whole I2C2 bus - including the UCD writes that
 *     switch the rail on);
 *   - the FRU at 0x55 is then read a bounded number of times.  Only a uRTM
 *     v2.3 gets the FMC2/FMC3 SDR records and the handle / fault monitor;
 *     anything else (another board, or no answer at all) stays powered but is
 *     left alone: no more RTM bus access until it is unplugged and plugged
 *     again;
 *   - a uRTM that opens its handle or reports a power fault is switched off
 *     and stays off until it is re-plugged or the payload power cycles.
 *
 * The task runs once per second, which is also the sensor and monitor rate.
 *
 * @license GPL-3.0+ <http://spdx.org/licenses/GPL-3.0+>
 */

#include "FreeRTOS.h"
#include "task.h"
#include "event_groups.h"

#include "port.h"
#include "payload.h"
#include "rtm.h"
#include "rtm_fru.h"
#include "rtm_sensors.h"
#include "task_priorities.h"
#include "ucd90xxx.h"
#include "utils.h"

extern ucd_data_t ucd_data[SDR_CH_COUNT];

/* AMC-side RTM timings */
#define RTM_PERIOD_MS            1000    /* task period, also the monitor rate */
/* 12 V one second after RTM_PS# went low.  The module's own MCU then needs
 * about 5 s to finish its I2C configuration before its FRU answers - that is
 * RTM_SETTLE_MS below, not part of this delay. */
#define RTM_TURNON_DELAY_MS      1000    /* 12 V after RTM_PS# went low */
#define RTM_SETTLE_MS            5000    /* module MCU start-up after the 12 V */
#define RTM_PROBE_INTERVAL_MS    1000    /* between FRU identification tries */
#define RTM_PROBE_TRIES          3       /* tries before the module is left alone */
#define RTM_CONFIG_DELAY_MS      10000   /* uRTM clock configuration window */
#define RTM_HANDLE_CONFIRM       2       /* low RTM_INT_B passes before "open" */
#define RTM_POWER_TRIES          3       /* rail switch attempts before parking */

#define RTM_TASK_STACK           192     /* words */
#define RTM_TASK_PRIORITY        tskRTMSENSORS_PRIORITY

#define RTM_EVT_PAYLOAD_UP       (1u << 0)

/* The manager's own view of the module.  RTM_UNMANAGED is a parked state: the
 * module keeps its 12 V but is never addressed again. */
typedef enum {
  RTM_OFF = 0,        /* no power: nothing plugged, payload down, or blocked */
  RTM_WAIT_AMC,       /* plugged, waiting for the AMC payload rails */
  RTM_POWER,          /* plug-in delay before the switch is closed */
  RTM_SETTLE,         /* module MCU start-up window, bus untouched */
  RTM_IDENTIFY,       /* bounded FRU probes */
  RTM_MANAGED,        /* uRTM v2.3: monitors + handle/fault monitor */
  RTM_UNMANAGED,      /* another board: powered, left alone */
} rtm_state_t;

static EventGroupHandle_t rtm_evt;
static TaskHandle_t rtm_task;

static rtm_state_t rtm_state;
static rtm_fru_type_t rtm_type;
static bool rtm_12v_on;
static bool rtm_blocked;        /* switched off on purpose (handle/fault) */
static bool rtm_plug_known;
static bool rtm_plug_last;
static uint8_t rtm_tries;
static uint8_t rtm_handle_confirm;
static TickType_t rtm_t_power;   /* plug-in delay start */
static TickType_t rtm_t_probe;   /* next FRU probe / end of the settle window */
static TickType_t rtm_ready_tick;/* end of the uRTM handle config window */
static bool rtm_mp_forced;       /* LGPO14 already driven low in this boot */

static bool rtm_ps_low(void)
{
  return gpio_read_pin(PIN_PORT(GPIO_RTM_PS_B), PIN_NUMBER(GPIO_RTM_PS_B)) == 0;
}

/* The uRTM drives RTM_INT_B low when its handle is open, and also while it is
 * not running.  Other RTMs drive the line from their own logic, so it only
 * means something on a module that identified as uRTM v2.3. */
static bool rtm_int_b_low(void)
{
  return gpio_read_pin(PIN_PORT(GPIO_RTM_INT_B), PIN_NUMBER(GPIO_RTM_INT_B)) == 0;
}

bool rtm_present(void)
{
  return rtm_ps_low();
}

bool rtm_12v_is_on(void)
{
  return rtm_12v_on;
}

void rtm_payload_up(bool up)
{
  if (rtm_evt == NULL) {
    return;
  }
  if (up) {
    xEventGroupSetBits(rtm_evt, RTM_EVT_PAYLOAD_UP);
  } else {
    xEventGroupClearBits(rtm_evt, RTM_EVT_PAYLOAD_UP);
    /* a new payload power cycle is a fresh chance for a blocked module */
    rtm_blocked = false;
  }
}

/* LGPO14 must never be raised: the uRTM v2 mishandles RTM_MODE and pulls a
 * large current from MP3V3 before its FPGA is configured, tripping the AMC's
 * MP3V3 over-current protection.  One write per boot is enough - the UCD keeps
 * the pin configured and its own default is "not driven". */
static void rtm_mp_keep_low(void)
{
  if (rtm_mp_forced) {
    return;
  }
  ucd_set_gpio(&ucd_data[0], UCD_GPO_RTM_MP_EN_ID, 0);
  rtm_mp_forced = true;
}

/* Returns true only when the UCD confirms the rail is on: the caller must not
 * touch the RTM bus otherwise. */
static bool rtm_power_on(void)
{
  uint8_t op = 0;

  /* RTM_MP3V3_EN (LGPO14 -> FPF2109 -> MP_RTM) is held LOW on purpose and
   * must never be raised.  The uRTM v2 mishandles the RTM_MODE signal: before
   * its FPGA is powered and configured it pulls a large current from MP3V3,
   * which trips the AMC's MP3V3 over-current protection and shuts the RTM's
   * management power down.  The classic "management power first" bring-up is
   * therefore abandoned: the RTM gets its 12 V a few seconds after RTM_PS#
   * went low (RTM_TURNON_DELAY_MS) and brings up its own management side.
   * The LattePanda carrier behaves the same way - its EEPROM/expander are
   * powered from its own 12 V side and answer with this pin low. */
  rtm_mp_keep_low();

  /* A UCD latches a page off when it declares a fault on it: OPERATION = 0x80
   * alone then leaves it in "soft off" (0x50) and the module never powers up.
   * Clear the page first, enable it, and check that the device took it (see
   * docs/UCD90320_NOTES.md). */
  ucd_clear_faults(&ucd_data[0], UCD_RTM_12V_CURR_ID);
  ucd_set_power(&ucd_data[0], UCD_RTM_12V_CURR_ID, 1);

  if (ucd_get_operation(&ucd_data[0], UCD_RTM_12V_CURR_ID, &op) && op != 0x80) {
    log_printk("rtm", "RTM 12 V not on (OPERATION 0x%02x), retrying", op);
    ucd_clear_faults(&ucd_data[0], UCD_RTM_12V_CURR_ID);
    ucd_set_power(&ucd_data[0], UCD_RTM_12V_CURR_ID, 1);
    if (ucd_get_operation(&ucd_data[0], UCD_RTM_12V_CURR_ID, &op) && op != 0x80) {
      log_printk("rtm", "RTM 12 V still off (OPERATION 0x%02x)", op);
      rtm_12v_on = false;
      return false;
    }
  }
  rtm_12v_on = true;
  return true;
}

static void rtm_power_off(void)
{
  /* RTM_MP3V3_EN stays low at all times (see rtm_power_on()); force it once
   * per boot rather than on every pass, so the shared I2C bus is not loaded
   * with an unconditional UCD write every second. */
  rtm_mp_keep_low();

  if (!rtm_12v_on) {
    return;
  }
  ucd_set_power(&ucd_data[0], UCD_RTM_12V_CURR_ID, 0);
  rtm_12v_on = false;
  rtm_ready_tick = 0;
  log_printk("rtm", "RTM 12 V off");
}

/* uRTM only: after the clock configuration window a still-low RTM_INT_B means
 * the handle is open (two passes ride out the contact bounce). */
static bool rtm_handle_open(TickType_t now)
{
  if (rtm_ready_tick != 0 && (int32_t)(now - rtm_ready_tick) < 0) {
    rtm_handle_confirm = 0;
    return false;
  }
  if (!rtm_int_b_low()) {
    rtm_handle_confirm = 0;
    return false;
  }
  if (rtm_handle_confirm < RTM_HANDLE_CONFIRM) {
    rtm_handle_confirm++;
  }
  return rtm_handle_confirm >= RTM_HANDLE_CONFIRM;
}

/* uRTM only: the UCD's RTM 12 V / MP fault inputs (active low). */
static bool rtm_faulted(void)
{
  return !ucd_get_gpio(&ucd_data[0], UCD_GPI_RTM_12V0_FLT_B_ID) ||
         !ucd_get_gpio(&ucd_data[0], UCD_GPI_RTM_MP_FLT_B_ID);
}

static void rtm_switch_off(const char *why, rtm_state_t next)
{
  log_printk("rtm", "RTM 12 V off (%s)", why);
  rtm_power_off();
  rtm_sensors_unavailable();
  rtm_state = next;
}

static void rtm_step(void)
{
  TickType_t now = xTaskGetTickCount();
  bool plugged = rtm_ps_low();
  bool up = rtm_evt != NULL &&
            (xEventGroupGetBits(rtm_evt) & RTM_EVT_PAYLOAD_UP) != 0;

  /* A module appearing or disappearing is a new situation every time. */
  if (!rtm_plug_known) {
    rtm_plug_known = true;
    rtm_plug_last = plugged;
  }
  if (plugged != rtm_plug_last) {
    rtm_plug_last = plugged;
    rtm_blocked = false;
    log_printk("rtm", plugged ? "RTM plugged" : "RTM unplugged");
  }

  /* Nothing to manage: no module, AMC payload down, or the module was
   * switched off on purpose (open handle / fault). */
  if (!plugged || !up || rtm_blocked) {
    rtm_power_off();
    rtm_sensors_unavailable();
    rtm_state = (!plugged || rtm_blocked) ? RTM_OFF : RTM_WAIT_AMC;
    return;
  }

  if (rtm_state == RTM_OFF || rtm_state == RTM_WAIT_AMC) {
    log_printk("rtm", "RTM detected, 12 V in %u s",
               (unsigned)(RTM_TURNON_DELAY_MS / 1000));
    rtm_t_power = now;
    rtm_state = RTM_POWER;
  }

  switch (rtm_state) {
  case RTM_POWER:
    if ((int32_t)(now - rtm_t_power) >= (int32_t)pdMS_TO_TICKS(RTM_TURNON_DELAY_MS)) {
      if (!rtm_power_on()) {
        /* no confirmed rail: do NOT probe.  Retry, then park the module
         * unpowered - an unpowered RTM would wedge I2C2 on the first access. */
        if (++rtm_tries >= RTM_POWER_TRIES) {
          log_printk("rtm", "RTM 12 V could not be switched, module left unpowered");
          rtm_state = RTM_UNMANAGED;
          rtm_12v_on = false;
        } else {
          rtm_t_power = now;
        }
        break;
      }
      rtm_tries = 0;
      rtm_handle_confirm = 0;
      rtm_t_probe = now + pdMS_TO_TICKS(RTM_SETTLE_MS);
      rtm_ready_tick = now + pdMS_TO_TICKS(RTM_CONFIG_DELAY_MS);
      log_printk("rtm", "RTM 12 V on (identify in %u s, handle monitor in %u s)",
                 (unsigned)(RTM_SETTLE_MS / 1000),
                 (unsigned)(RTM_CONFIG_DELAY_MS / 1000));
      rtm_state = RTM_SETTLE;
    }
    break;

  case RTM_SETTLE:
    if ((int32_t)(now - rtm_t_probe) >= 0) {
      rtm_t_probe = now;
      rtm_state = RTM_IDENTIFY;
    }
    break;

  case RTM_IDENTIFY:
    if ((int32_t)(now - rtm_t_probe) < 0) {
      break;
    }
    if (!rtm_12v_on) {
      /* defensive: never touch the RTM bus with the rail off */
      log_printk("rtm", "no RTM probe while the 12 V is off");
      rtm_state = RTM_UNMANAGED;
      break;
    }
    rtm_type = rtm_fru_probe();
    if (rtm_type == RTM_FRU_URTM_V23) {
      log_printk("rtm", "FRU 0x55: uRTM v2.3");
      rtm_sensors_insert(&rtm_task);
      rtm_state = RTM_MANAGED;
    } else if (rtm_type == RTM_FRU_OTHER) {
      log_printk("rtm", "FRU 0x55: other RTM, no FMC monitors");
      /* a positively identified other board took the place of a uRTM: its
       * FMC2/FMC3 records no longer describe anything on Zone 3 */
      if (rtm_sensors_is_inserted()) {
        rtm_sensors_remove();
      }
      rtm_state = RTM_UNMANAGED;
    } else if (++rtm_tries >= RTM_PROBE_TRIES) {
      log_printk("rtm", "FRU 0x55: no answer, RTM left unmanaged");
      rtm_state = RTM_UNMANAGED;
    } else {
      rtm_t_probe = now + pdMS_TO_TICKS(RTM_PROBE_INTERVAL_MS);
    }
    break;

  case RTM_MANAGED:
    if (rtm_handle_open(now)) {
      rtm_switch_off("handle open", RTM_OFF);
      rtm_blocked = true;
    } else if (rtm_faulted()) {
      rtm_switch_off("power fault", RTM_OFF);
      rtm_blocked = true;
    } else if (rtm_12v_on) {
      rtm_sensors_poll();
    }
    break;

  case RTM_UNMANAGED:
  default:
    /* Not a board we manage: keep the 12 V, touch nothing. */
    break;
  }
}

static void vTaskRtm(void *Parameters)
{
  TickType_t xLastWakeTime = xTaskGetTickCount();

  (void)Parameters;

  for (;;) {
    rtm_step();
    vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(RTM_PERIOD_MS));
  }
}

void rtm_init(void)
{
  rtm_evt = xEventGroupCreate();
  if (rtm_evt == NULL) {
    /* without it rtm_payload_up() cannot tell us the payload is up, so the
     * RTM would simply never be powered - log rather than fail silently */
    log_printk("rtm", "no heap for the RTM event group, RTM stays off");
  }
  rtm_state = RTM_OFF;
  rtm_type = RTM_FRU_UNKNOWN;
  rtm_12v_on = false;
  rtm_blocked = false;
  rtm_handle_confirm = 0;
  rtm_tries = 0;
  rtm_ready_tick = 0;
  rtm_t_probe = 0;
  rtm_mp_forced = false;

  /* RTM_RES stays LOW: the uRTM MCU reads this line to know that it is
   * running as an RTM (v1.6.3 left it at the pin table's default, also low,
   * and the uRTM v2.3 works that way).  Drive it explicitly so the intent is
   * visible and nothing can raise it later. */
  gpio_set_pin_low(PIN_PORT(GPIO_RTM_RES), PIN_NUMBER(GPIO_RTM_RES));

  /* Sample RTM_PS# now so a module plugged in later is seen as an edge. */
  rtm_plug_known = true;
  rtm_plug_last = rtm_ps_low();

  rtm_sensors_init();

  if (xTaskCreate(vTaskRtm, "RTM", RTM_TASK_STACK, NULL, RTM_TASK_PRIORITY,
                  &rtm_task) != pdPASS) {
    log_printk("rtm", "no heap for the RTM task");
  }

  /* Create the RTM SDR records as soon as a module is plugged, i.e. before
   * the MCH reads this module's SDR repository.  The MCH fills that cache
   * when the AMC activates (a few seconds after power-on), which is well
   * before the RTM is powered (12 V after RTM_TURNON_DELAY_MS) and identified
   * through its FRU, and it only re-reads on a FRU activation - records
   * created at identification time therefore stayed invisible for the whole
   * session.  Until the UCD90124A answers they report "reading unavailable";
   * the manager removes them again if the module turns out to be another
   * board (RTM_IDENTIFY -> RTM_FRU_OTHER). */
  if (rtm_present()) {
    rtm_sensors_insert(&rtm_task);
  }
}
