# RTM FMC monitors as AMC SDR sensors — implementation notes

Decision: the **AMC MMC owns** the RTM's FMC2/FMC3 monitor sensors; the uRTM's
own MMC (openMMC `urtm-v2`) is not used for this.

Implemented in `port/board/u4fcp-v2/rtm_sensors.c/.h` (polling task) and the
board SDR list, with the generic `sdr_delete_entry()` added to `modules/sdr.c`
for the dynamic removal.

## Data source

uRTM2.3 UCD90124A `0x5B` behind `TCA9548A 0x70 ch5` -> `TCA9548A 0x71 ch7`
(pages from `uRTMv2.3_with_current_limitation.tifsp`):

The VADJ voltage and current **share one page** (READ_VOUT 0x8B and
READ_IOUT 0x8C on the same PAGE), so the 8 monitors come from 6 pages:

| page | monitors | reads |
|---|---|---|
| 2 | FMC2 12 V current | 0x8C LINEAR11 |
| 3 | **FMC2 VADJ voltage + current** | 0x8B LINEAR16 + 0x8C LINEAR11 |
| 4 | FMC2 3V3 current | 0x8C LINEAR11 |
| 5 | FMC3 12 V current | 0x8C LINEAR11 |
| 6 | **FMC3 VADJ voltage + current** | 0x8B + 0x8C |
| 7 | FMC3 3V3 current | 0x8C LINEAR11 |

READ_VOUT is LINEAR16 with `VOUT_MODE` 0x14 (x2^-12, read at every poll and
used for the conversion); READ_IOUT/READ_TEMPERATURE are LINEAR11
(R = Y * 2^X, Y = low 11 bits signed, X = high 5 bits signed).

## Firmware pieces

1. `port/board/u4fcp-v2/i2c_mapping.c/.h` — `CHIP_ID_RTM_MUX_1` (0x71),
   `CHIP_ID_RTM_UCD` (0x5B) and `i2c_rtm_mux_select()`; `I2C_BUS_RTM_ID`
   (0x70 ch5) is now enabled.  A poll session takes the I2C2 semaphore once
   and walks both mux levels, so the AMC's 5 s UCD90320 poll cannot switch
   the mux underneath a register read.
2. `port/board/u4fcp-v2/rtm_sensors.c/.h` — the 1 s poll of the eight
   monitors in integer units (mV / mA, no `%f`), the boot dump
   (`rtm_sensors_dump()`), and the insert/remove of the SDRs with the RTM
   presence.  A failed poll is retried once immediately; the monitors are only
   marked unavailable after 3 consecutive failed cycles, so a single NACK does
   not flap the MCH view.  Readings are only scanned while the RTM 12 V is on
   (the payload cuts it on shutdown); with the rails off the sensors are
   disabled, which the AMC sensor handler reports as not available.
3. `port/board/u4fcp-v2/sdr_list.c` — the 8 `SDR_type_01h_t` records and the
   `SDR_RTM_FMC_UCD[]` table in sensor-number order.
4. `modules/sdr.c` — `sdr_insert_entry_full()` (explicit sensor number and
   entity instance), `sdr_delete_entry()`, `find_sensor_by_number()`, and the
   IPMI handlers now report the SDR's own sensor number (byte 7) and resolve
   Get Sensor Reading / Get Sensor Threshold by sensor number instead of by
   record ID.  The `sensor_t` list link/unlink is done in a critical section
   because the RTM group is inserted/removed at run time.
5. `port/board/u4fcp-v2/board_config.c` — `rtm_sensors_init()` creates the
   task and inserts the group before the scheduler starts when the RTM is
   already plugged in.

## Decided

- **entityID / instance**: all eight monitors are **merged as the RTM's
  sensors**: entity `0xC0`, instance `0x00` (one "RTM" group; the FMC2/FMC3
  site lives in the sensor name only).  The AMC's own sensors stay on
  `0xC1`/`0x6C`.
- **sensor numbers**: `0x20-0x27`, one per monitor, fixed:
  `0x20` FMC2 12V I, `0x21` FMC2 3V3 I, `0x22` FMC2 VADJ V, `0x23` FMC2 VADJ I,
  `0x24` FMC3 12V I, `0x25` FMC3 3V3 I, `0x26` FMC3 VADJ V, `0x27` FMC3 VADJ I.
  The earlier plan said `0x10-0x17`, but those numbers are already used by the
  AMC's own UCD90320 sensors (verified with `ipmitool sdr list` on AMC12:
  0x10 "FMC1 +12V Curr" ... 0x17 "DDR +1.2VT Curr").  IPMI sensor numbers have
  to be unique per IPM controller and Get Sensor Reading addresses by number
  only, so the RTM group starts after the AMC's 0x01-0x1A instead.
- **SDR record IDs** are not the sensor numbers: the group is appended to the
  repository (record IDs 27-34 with the current AMC sensor list) and removed
  from the tail again, which keeps the record IDs contiguous for the MCH.
- **SDR parameters** (`M`, `R`, units, thresholds, hysteresis, event masks)
  are the ones of the AMC's own FMC0/FMC1 records: 12 V / 3.3 V current
  32 mA/LSB, VADJ current 64 mA/LSB, VADJ voltage 16 mV/LSB; the names follow
  the same style ("FMC2 +12V Curr", "FMC2 VADJ", ...).

## Verified on AMC12 + uRTM2.3 (2026-11-11)

- boot log after the RTM 12 V comes on (integer units):

  ```
  rtm : 8 FMC2/FMC3 monitors added (sensors 0x20-0x27, entity 0xc0/0)
  rtm : UCD 0x5B: VOUT_MODE 2^-12, RTM 12 V on
  rtm : FMC2 12V I 281 mA      rtm : FMC2 VADJ V 1798 mV
  rtm : FMC2 3V3 I 0 mA        rtm : FMC2 VADJ I 31 mA
  rtm : FMC3 12V I 250 mA      rtm : FMC3 VADJ V 1801 mV
  rtm : FMC3 3V3 I 171 mA      rtm : FMC3 VADJ I 0 mA
  ```

  every value matches a direct read of the UCD pages (`rtm_power.py`).
- `ipmitool -t 0x88 sdr list`: 34 sensors, the eight RTM ones at record ID
  27-34 with sensor number 0x20-0x27 and entity 0xC0/instance 0x00
  ("PICMG Rear Transition Module").
- dynamic removal: removal at ~2 s after the presence drop, the eight records
  vanish from the repository (26 sensors), and a re-insert adds them back with
  record IDs 27-34 and fresh readings.  Exercised with a bench build
  (`-DRTM_SENSORS_SELFTEST`, not part of the production image) that pretends
  the handle was opened for 30 s; a physical unplug test is still open.
- MCH (NAT-MCH 2.23.6) `show_sensorinfo 16` lists the group with entity
  0xC0/0x00 and the right values; while the sensors are removed it keeps the
  (cached) rows and reports "could not read sensor" (completion code 0xC9)
  until it re-reads the module SDR.

## Open points

- **MCH SDR cache**: the NAT-MCH keeps the module's SDR rows until it re-reads
  the module repository (e.g. after a slot re-activation), so after an RTM
  unplug it shows the eight names with a read error instead of dropping them.
  A physical mount/remove test with the MCH shell (`show_sensorinfo 16`) is
  still to be done.
- The `0x71` ch7 segment is shared with the uRTM FRU EEPROM (0x55) and the
  VADJ pot (0x57); no other AMC task currently selects it, so the 1 s poll
  leaves the mux there.  If the AMC ever reads the RTM FRU, both users must
  re-select the channel (the current code always writes the channel byte).
