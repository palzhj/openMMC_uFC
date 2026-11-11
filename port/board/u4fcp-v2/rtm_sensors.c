/*
 * openMMC -- u4FCP-v2 AMC-side RTM FMC2/FMC3 monitor sensors
 *
 * The uRTM2.3 carries a UCD90124A PMBus monitor (0x5B) behind two I2C
 * switches: I2C2 -> TCA9548A 0x70 ch5 -> TCA9548A 0x71 ch7 -> 0x5B.
 *
 * The AMC MMC owns the two FMC sites' monitors: this module reads them once
 * per second, converts the readings to IPMI SDR units, and inserts/removes
 * the eight SDR records with the RTM presence (RTM_PS#), so the MCH sees
 * them only while the RTM is plugged.  The sensor numbers (0x1B-0x22) follow
 * the AMC's own sensors (0x01-0x1A), so the MCH lists AMC first, RTM last.
 *
 * Units: the PMBus readings are converted to integer milli-volts /
 * milli-amps (no float, the debug printf has no %f).  READ_VOUT is LINEAR16
 * with VOUT_MODE 0x14 (x2^-12); READ_IOUT is LINEAR11 (R = Y * 2^X,
 * Y = low 11 bits signed, X = high 5 bits signed).
 *
 * The readings are only scanned while the RTM 12 V is on: with the RTM
 * present but unpowered (or its 12 V cut by the payload state machine) every
 * FMC rail is 0 V and scanning them would flood the MCH with threshold
 * events.  Instead the sensors are marked "scanning disabled", which the
 * AMC sensor handler reports as not available.
 *
 * @license GPL-3.0+ <http://spdx.org/licenses/GPL-3.0+>
 */

#include "FreeRTOS.h"
#include "task.h"

#include "port.h"
#include "sdr.h"
#include "i2c.h"
#include "i2c_mapping.h"
#include "rtm.h"
#include "rtm_sensors.h"
#include "task_priorities.h"
#include "utils.h"

/* UCD90124A PMBus registers */
#define RTM_UCD_REG_PAGE        0x00
#define RTM_UCD_REG_VOUT_MODE   0x20
#define RTM_UCD_REG_READ_VOUT   0x8B
#define RTM_UCD_REG_READ_IOUT   0x8C

/* VOUT_MODE (0x20) is per PMBus page: the 12 V pages use 0x16 (x2^-10),
 * the 3.3 V / VADJ pages 0x14 (x2^-12). */
#define RTM_UCD_LINEAR16_DEFAULT_EXP   (-12)

/* page 0 is the RTM 12 V input rail (0.164 divider, x2^-10), not one of the
 * eight FMC monitors */
#define RTM_UCD_PAGE_12V        0

#define RTM_UCD_TIMEOUT_TICKS   20      /* 20 ms to win the bus */
#define RTM_SENSORS_PERIOD_MS   1000
#define RTM_SENSORS_TASK_STACK  192     /* words: only the boot dump uses printf */

/* An opened handle bounces: the group is only dropped after the RTM stayed
 * absent for this many polls. */
#define RTM_SENSORS_ABSENT_POLLS  2

/* Consecutive failed poll cycles before the monitors are marked unavailable */
#define RTM_SENSORS_FAIL_LIMIT     3

/* How long to wait for the RTM 12 V before dumping the monitors anyway
 * (the MP rails of the RTM answer even when the FMC rails are off). */
#define RTM_SENSORS_DUMP_WAIT_MS  10000

typedef struct {
  uint8_t page;                 /* PMBus PAGE */
  uint8_t reg;                  /* READ_VOUT / READ_IOUT */
  uint8_t sensor_type;          /* SENSOR_TYPE_VOLTAGE / SENSOR_TYPE_CURRENT */
  const char *name;             /* log name (the SDR records carry the full name) */
} rtm_monitor_t;

static const rtm_monitor_t rtm_monitors[RTM_SENSORS_COUNT] = {
  { 2, RTM_UCD_REG_READ_IOUT, SENSOR_TYPE_CURRENT, "FMC2 12V I"  },
  { 4, RTM_UCD_REG_READ_IOUT, SENSOR_TYPE_CURRENT, "FMC2 3V3 I"  },
  { 3, RTM_UCD_REG_READ_VOUT, SENSOR_TYPE_VOLTAGE, "FMC2 VADJ V" },
  { 3, RTM_UCD_REG_READ_IOUT, SENSOR_TYPE_CURRENT, "FMC2 VADJ I" },
  { 5, RTM_UCD_REG_READ_IOUT, SENSOR_TYPE_CURRENT, "FMC3 12V I"  },
  { 7, RTM_UCD_REG_READ_IOUT, SENSOR_TYPE_CURRENT, "FMC3 3V3 I"  },
  { 6, RTM_UCD_REG_READ_VOUT, SENSOR_TYPE_VOLTAGE, "FMC3 VADJ V" },
  { 6, RTM_UCD_REG_READ_IOUT, SENSOR_TYPE_CURRENT, "FMC3 VADJ I" }
};

static TaskHandle_t vTaskRtmSensors_Handle;
static sensor_t *rtm_sensor[RTM_SENSORS_COUNT];
static int32_t rtm_milli[RTM_SENSORS_COUNT];  /* last reading: mV / mA */
static int8_t rtm_monitor_exp[RTM_SENSORS_COUNT];  /* VOUT_MODE used per monitor */

static bool rtm_scan_on;
static uint8_t rtm_absent_polls;
static uint8_t rtm_fail_count;      /* consecutive failed poll cycles */
static uint8_t rtm_fail_step;       /* last failing step, for the log */
static bool rtm_dump_pending;
static TickType_t rtm_dump_tick;

/* ------------------------------------------------------------------ */
/* PMBus format conversion                                             */
/* ------------------------------------------------------------------ */

/* LINEAR11: R = Y * 2^X -> milli-units (Y = low 11 bits signed, X = high
 * 5 bits signed).  The exponents seen on this UCD are -11..-16, so the
 * negative branch is the common one; the positive branch is clamped to keep
 * the product inside 32 bits. */
static int32_t linear11_milli(uint16_t raw)
{
  int32_t y = raw & 0x7FF;
  int8_t x = (int8_t)((raw >> 11) & 0x1F);

  if (y & 0x400) {
    y -= 0x800;
  }
  if (x & 0x10) {
    x -= 0x20;
  }

  if (x >= 0) {
    if (x > 7) {
      x = 7;
    }
    return y * (1000 << x);
  }
  if (-x > 16) {
    x = -16;
  }
  return (y * 1000) / (1 << (-x));
}

/* LINEAR16: value = raw * 2^exp -> milli-units (raw is unsigned) */
static int32_t linear16_milli(uint16_t raw, int8_t exp)
{
  if (exp >= 0) {
    if (exp > 20) {
      exp = 20;
    }
    return (int32_t)raw * (1000 << exp);
  }
  if (-exp > 16) {
    exp = -16;
  }
  return (int32_t)((((uint32_t)raw * 1000u) + (1u << (-exp - 1))) >> (-exp));
}

/* SDR unit conversion: the record reports value = (M * x + B * 10^Bexp) *
 * 10^Rexp, so one SDR LSB is M * 10^Rexp of the physical unit.
 * Returns the 8-bit reading (0..255) for the given milli-unit value. */
static uint8_t milli_to_sdr(const SDR_type_01h_t *sdr, int32_t milli)
{
  int8_t uR = (int8_t)((sdr->Rexp_Bexp >> 4) & 0x0F);
  int8_t r = (uR > 0x07) ? (int8_t)(uR - 0x10) : uR;
  int32_t lsb = (int32_t)sdr->M * 1000;

  for (; r > 0; r--) {
    lsb *= 10;
  }
  for (; r < 0; r++) {
    lsb /= 10;
  }
  if (lsb <= 0) {
    return 0;
  }

  if (milli <= 0) {
    return 0;
  }
  milli = (milli + lsb / 2) / lsb;   /* round, then clamp to the 8-bit range */
  if (milli > 0xFF) {
    milli = 0xFF;
  }
  return (uint8_t)milli;
}

/* ------------------------------------------------------------------ */
/* UCD90124A access                                                    */
/* ------------------------------------------------------------------ */

/* Take the bus and walk both mux levels.  On failure the bus is released. */
static bool rtm_ucd_take(uint8_t *i2c_interface, uint8_t *i2c_address)
{
  if (!i2c_take_by_chipid(CHIP_ID_RTM_UCD, i2c_address, i2c_interface,
                          RTM_UCD_TIMEOUT_TICKS)) {
    return false;
  }
  if (!i2c_rtm_mux_select(*i2c_interface)) {
    /* the RTM (or its 0x71 switch) is not answering */
    i2c_give(*i2c_interface);
    return false;
  }
  return true;
}

static bool rtm_ucd_write_reg(uint8_t i2c_interface, uint8_t i2c_address,
                              uint8_t reg, uint8_t value)
{
  uint8_t buf[2] = { reg, value };
  return xI2CMasterWrite(i2c_interface, i2c_address, buf, 2) == 2;
}

static bool rtm_ucd_read_byte(uint8_t i2c_interface, uint8_t i2c_address,
                              uint8_t reg, uint8_t *value)
{
  if (xI2CMasterWriteRead(i2c_interface, i2c_address, &reg, 1, value, 1) != 1) {
    return false;
  }
  return true;
}

static bool rtm_ucd_read_word(uint8_t i2c_interface, uint8_t i2c_address,
                              uint8_t reg, uint16_t *value)
{
  uint8_t buf[2] = { 0, 0 };

  if (xI2CMasterWriteRead(i2c_interface, i2c_address, &reg, 1, buf, 2) != 2) {
    return false;
  }
  *value = (uint16_t)(((uint16_t)buf[1] << 8) | buf[0]);   /* little endian */
  return true;
}

/* Read the eight monitors in one bus session.  Returns false as soon as the
 * RTM path NACKs; the caller then keeps the last readings. */
static bool rtm_ucd_read_monitors(int32_t *milli)
{
  uint8_t i2c_interface = 0;
  uint8_t i2c_address = 0;
  uint8_t page = 0xFF;
  uint8_t mode = 0;
  uint16_t raw;
  int8_t page_exp = RTM_UCD_LINEAR16_DEFAULT_EXP;
  bool ok = false;

  if (!rtm_ucd_take(&i2c_interface, &i2c_address)) {
    rtm_fail_step = 1;   /* no bus / no ACK from 0x70 ch5 or 0x71 ch7 */
    return false;
  }

  for (uint8_t i = 0; i < RTM_SENSORS_COUNT; i++) {
    if (rtm_monitors[i].page != page) {
      if (!rtm_ucd_write_reg(i2c_interface, i2c_address,
                             RTM_UCD_REG_PAGE, rtm_monitors[i].page)) {
        rtm_fail_step = 3 + i;   /* PAGE write */
        goto out;
      }
      page = rtm_monitors[i].page;

      /* VOUT_MODE follows the selected page, read it once per page change */
      page_exp = RTM_UCD_LINEAR16_DEFAULT_EXP;
      if (rtm_ucd_read_byte(i2c_interface, i2c_address,
                            RTM_UCD_REG_VOUT_MODE, &mode)) {
        page_exp = (int8_t)(mode & 0x1F);
        if (page_exp & 0x10) {
          page_exp -= 0x20;
        }
      }
    }
    if (!rtm_ucd_read_word(i2c_interface, i2c_address,
                           rtm_monitors[i].reg, &raw)) {
      rtm_fail_step = 10 + i;   /* register read */
      goto out;
    }
    rtm_monitor_exp[i] = page_exp;
    milli[i] = (rtm_monitors[i].reg == RTM_UCD_REG_READ_VOUT)
               ? linear16_milli(raw, page_exp)
               : linear11_milli(raw);
  }

  ok = true;

out:
  i2c_give(i2c_interface);
  return ok;
}

/* Read the RTM 12 V input rail (page 0, x2^-10, 0.164 divider).  It is not
 * one of the eight SDR sensors, but it tells whether the RTM input is up. */
static bool rtm_ucd_read_rail_12v(int32_t *milli)
{
  uint8_t i2c_interface = 0;
  uint8_t i2c_address = 0;
  uint8_t mode = 0;
  uint16_t raw;
  int8_t exp = RTM_UCD_LINEAR16_DEFAULT_EXP;
  bool ok = false;

  if (!rtm_ucd_take(&i2c_interface, &i2c_address)) {
    rtm_fail_step = 1;
    return false;
  }

  if (!rtm_ucd_write_reg(i2c_interface, i2c_address,
                         RTM_UCD_REG_PAGE, RTM_UCD_PAGE_12V)) {
    rtm_fail_step = 3;
    goto out;
  }
  if (rtm_ucd_read_byte(i2c_interface, i2c_address,
                        RTM_UCD_REG_VOUT_MODE, &mode)) {
    exp = (int8_t)(mode & 0x1F);
    if (exp & 0x10) {
      exp -= 0x20;
    }
  }
  if (!rtm_ucd_read_word(i2c_interface, i2c_address,
                         RTM_UCD_REG_READ_VOUT, &raw)) {
    rtm_fail_step = 10;
    goto out;
  }
  *milli = linear16_milli(raw, exp);
  ok = true;

out:
  i2c_give(i2c_interface);
  return ok;
}

/* ------------------------------------------------------------------ */
/* SDR insertion / removal                                             */
/* ------------------------------------------------------------------ */

bool rtm_sensors_is_inserted(void)
{
  return rtm_sensor[0] != NULL;
}

/* First sensor number of the group: RTM_SENSORS_FIRST_NUM unless an AMC
 * sensor (or a future one) already uses it - then the block moves up by
 * eight, so the numbers stay unique (IPMI requirement). */
static uint8_t rtm_sensors_first_free_number(void)
{
  uint8_t base = RTM_SENSORS_FIRST_NUM;

  while ((uint16_t)base + RTM_SENSORS_COUNT <= 0xFF) {
    bool free_block = true;

    for (uint8_t i = 0; i < RTM_SENSORS_COUNT; i++) {
      if (find_sensor_by_number((uint8_t)(base + i)) != NULL) {
        free_block = false;
        break;
      }
    }
    if (free_block) {
      return base;
    }
    log_printk("rtm", "sensor numbers 0x%02x-0x%02x already used, moving the RTM group",
               base, base + RTM_SENSORS_COUNT - 1);
    base = (uint8_t)(base + RTM_SENSORS_COUNT);
  }
  return base;
}

bool rtm_sensors_insert(void)
{
  uint8_t first_num;

  if (rtm_sensors_is_inserted()) {
    return false;
  }

  first_num = rtm_sensors_first_free_number();

  for (uint8_t i = 0; i < RTM_SENSORS_COUNT; i++) {
    rtm_sensor[i] = sdr_insert_entry_full(TYPE_01,
                                          (void *)SDR_RTM_FMC_UCD[i],
                                          &vTaskRtmSensors_Handle, 0,
                                          CHIP_ID_RTM_UCD,
                                          (uint8_t)(first_num + i),
                                          RTM_SENSORS_INSTANCE);
    /* not available until the first reading of a powered RTM */
    sensor_disable(rtm_sensor[i]);
    rtm_milli[i] = 0;
  }

  rtm_scan_on = false;
  rtm_absent_polls = 0;
  rtm_fail_count = 0;
  rtm_dump_pending = true;
  rtm_dump_tick = xTaskGetTickCount();

  log_printk("rtm", "%d FMC2/FMC3 monitors added (sensors 0x%02x-0x%02x, entity 0x%02x/%u)",
             RTM_SENSORS_COUNT, first_num, first_num + RTM_SENSORS_COUNT - 1,
             RTM_SENSORS_ENTITY_ID, RTM_SENSORS_INSTANCE);
  return true;
}

void rtm_sensors_remove(void)
{
  if (!rtm_sensors_is_inserted()) {
    return;
  }

  /* remove in reverse: the group was appended to the SDR list, so this
   * always frees the tail and keeps the record IDs contiguous */
  for (uint8_t i = RTM_SENSORS_COUNT; i-- > 0; ) {
    sdr_delete_entry(rtm_sensor[i]);
    rtm_sensor[i] = NULL;
  }

  rtm_scan_on = false;
  rtm_absent_polls = 0;
  rtm_dump_pending = false;

  log_printk("rtm", "RTM removed, FMC2/FMC3 monitors removed from the SDR");
}

/* ------------------------------------------------------------------ */
/* Boot log dump                                                       */
/* ------------------------------------------------------------------ */

void rtm_sensors_dump(void)
{
  int32_t milli[RTM_SENSORS_COUNT] = { 0 };
  int32_t rail_12v = 0;

  if (!rtm_present()) {
    log_printk("rtm", "no RTM, no FMC2/FMC3 monitors to read");
    return;
  }

  if (!rtm_ucd_read_monitors(milli)) {
    log_printk("rtm", "UCD 0x5B not answering (0x70 ch5 -> 0x71 ch7)");
    return;
  }

  if (rtm_ucd_read_rail_12v(&rail_12v)) {
    log_printk("rtm", "UCD 0x5B: RTM 12 V input %d mV, RTM 12 V %s",
               (int)rail_12v, rtm_12v_is_on() ? "on" : "off");
  } else {
    log_printk("rtm", "UCD 0x5B: RTM 12 V input not readable, RTM 12 V %s",
               rtm_12v_is_on() ? "on" : "off");
  }

  for (uint8_t i = 0; i < RTM_SENSORS_COUNT; i++) {
    if (rtm_monitors[i].sensor_type == SENSOR_TYPE_VOLTAGE) {
      /* the LINEAR16 exponent is per page: print the one that was used */
      log_printk("rtm", "%s %d mV (2^%d)", rtm_monitors[i].name,
                 (int)milli[i], rtm_monitor_exp[i]);
    } else {
      log_printk("rtm", "%s %d mA", rtm_monitors[i].name, (int)milli[i]);
    }
  }
}

/* ------------------------------------------------------------------ */
/* Polling task                                                        */
/* ------------------------------------------------------------------ */

/* Have the RTM's own FMC VADJ regulators come up?  (The uRTM UCD sequences
 * its rails after the 12 V is enabled, so the first reading can still be
 * zero.) */
static bool rtm_rails_up(const int32_t *milli)
{
  for (uint8_t i = 0; i < RTM_SENSORS_COUNT; i++) {
    if (rtm_monitors[i].sensor_type == SENSOR_TYPE_VOLTAGE && milli[i] > 500) {
      return true;
    }
  }
  return false;
}

/* Push a good reading into the SDR units of the sensors */
static void rtm_sensors_update(const int32_t *milli)
{
  for (uint8_t i = 0; i < RTM_SENSORS_COUNT; i++) {
    sensor_t *sensor = rtm_sensor[i];
    SDR_type_01h_t *sdr = (SDR_type_01h_t *)sensor->sdr;

    rtm_milli[i] = milli[i];
    sensor->readout_value = milli_to_sdr(sdr, milli[i]);
    sensor_state_check(sensor);
    check_sensor_event(sensor);
  }
}

#ifdef RTM_SENSORS_SELFTEST
/* Bench self-test of the dynamic removal/insert path (not built into the
 * production image): pretend the uRTM handle was opened between 60 s and
 * 90 s after the scheduler started, so the SDR removal and re-insert can be
 * exercised without a human at the crate. */
static bool rtm_present_selftest(void)
{
  uint32_t uptime = (uint32_t)(xTaskGetTickCount() / configTICK_RATE_HZ);

  return !(uptime >= 60u && uptime < 90u);
}
#endif

static void vTaskRtmSensors(void *Parameters)
{
  TickType_t xLastWakeTime = xTaskGetTickCount();

  (void)Parameters;

  for (;;) {
    bool present = rtm_present();

#ifdef RTM_SENSORS_SELFTEST
    present = present && rtm_present_selftest();
#endif

    if (present) {
      rtm_absent_polls = 0;

      if (!rtm_sensors_is_inserted()) {
        rtm_sensors_insert();
      }

      if (rtm_12v_is_on()) {
        int32_t milli[RTM_SENSORS_COUNT];
        bool ok;

        /* one immediate retry: a single NACK on the shared I2C2 bus (the AMC
         * UCD is polled from another task) must not hide the monitors */
        ok = rtm_ucd_read_monitors(milli) || rtm_ucd_read_monitors(milli);

        if (ok) {
          rtm_fail_count = 0;
          if (!rtm_scan_on) {
            for (uint8_t i = 0; i < RTM_SENSORS_COUNT; i++) {
              sensor_enable(rtm_sensor[i]);
            }
            rtm_scan_on = true;
          }
          rtm_sensors_update(milli);

          /* the boot dump waits for the FMC rails to come up, so the log
           * shows real currents and not the zeroes of the power-up ramp */
          if (rtm_dump_pending && rtm_rails_up(milli)) {
            rtm_sensors_dump();
            rtm_dump_pending = false;
          }
        } else {
          if (rtm_fail_count < RTM_SENSORS_FAIL_LIMIT) {
            rtm_fail_count++;
          }
          /* the UCD keeps NACKing: report the monitors as not available
           * instead of leaving stale readings in the MCH (rtm_scan_on is
           * cleared so this is logged once per outage) */
          if (rtm_fail_count >= RTM_SENSORS_FAIL_LIMIT && rtm_scan_on) {
            for (uint8_t i = 0; i < RTM_SENSORS_COUNT; i++) {
              sensor_disable(rtm_sensor[i]);
            }
            rtm_scan_on = false;
            log_printk("rtm", "UCD 0x5B not answering (step %u), monitors unavailable",
                       rtm_fail_step);
          }
        }
      } else if (rtm_scan_on) {
        /* the RTM 12 V went away: keep the SDRs, stop scanning (no events) */
        for (uint8_t i = 0; i < RTM_SENSORS_COUNT; i++) {
          sensor_disable(rtm_sensor[i]);
        }
        rtm_scan_on = false;
      }

      if (rtm_dump_pending &&
          (xTaskGetTickCount() - rtm_dump_tick) >= pdMS_TO_TICKS(RTM_SENSORS_DUMP_WAIT_MS)) {
        /* the RTM never powered its FMC rails: dump what the MP side shows */
        rtm_sensors_dump();
        rtm_dump_pending = false;
      }
    } else if (rtm_absent_polls < RTM_SENSORS_ABSENT_POLLS) {
      rtm_absent_polls++;
      if (rtm_absent_polls >= RTM_SENSORS_ABSENT_POLLS) {
        rtm_sensors_remove();
      }
    }

    vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(RTM_SENSORS_PERIOD_MS));
  }
}

void rtm_sensors_init(void)
{
  for (uint8_t i = 0; i < RTM_SENSORS_COUNT; i++) {
    rtm_sensor[i] = NULL;
    rtm_milli[i] = 0;
  }
  rtm_scan_on = false;
  rtm_absent_polls = 0;
  rtm_dump_pending = false;

  if (xTaskCreate(vTaskRtmSensors, "RTM Sensors", RTM_SENSORS_TASK_STACK,
                  (void *)NULL, tskRTMSENSORS_PRIORITY,
                  &vTaskRtmSensors_Handle) != pdPASS) {
    log_printk("rtm", "no heap for the RTM sensor task");
  }

  /* Insert before the scheduler starts when the RTM is already plugged, so
   * the MCH sees the sensors from its first SDR read (the task handles the
   * insert/remove for everything after that). */
  if (rtm_present()) {
    rtm_sensors_insert();
  }
}
