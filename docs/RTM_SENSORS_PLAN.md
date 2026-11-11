# RTM FMC monitors as AMC SDR sensors — implementation plan

Decision: the **AMC MMC owns** the RTM's FMC2/FMC3 monitor sensors; the uRTM's
own MMC (openMMC `urtm-v2`) is not used for this.

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

VADJ currents (the 7th/8th monitor) need the corresponding current‑sense
pages confirmed in the Fusion project (IOUT_CAL_GAIN 556/476 mΩ rails).

## Firmware pieces

1. `port/board/u4fcp-v2/rtm_sensors.c/.h` — the bus path (0x70 ch5 +
   0x71 ch7 writes), the PMBus page/read, LINEAR16/11 conversion, and a
   `rtm_sensors_poll()` updating 8 `sensor_t` objects.
2. `sdr_list.c` — 8 × `SDR_type_01h` records (entity FMC2 / FMC3) inserted
   when the RTM is present, removed otherwise; `sdr_insert_entry` at the
   presence event (the existing `rtm_check_presence` / hotswap RTM path).
3. `sdr.c` — needs a `sdr_delete_entry()` (the current API only inserts) for
   the dynamic removal, or the sensors are marked non‑present and the MCH
   re‑scans.
4. A sensor task polling the RTM UCD every ~1 s, tolerant to the RTM being
   removed (NACK → mark the sensors unavailable, no event flood).
5. MCH side: after the insert/remove the SDR repository must be re‑read
   (module re‑insert / MCH SDR reload) — the MCH caches the SDR.

## Decided

- **entityID / instance**: all eight monitors are **merged as the RTM's
  sensors**: entity `0xC0`, instance `0x00` (one "RTM" group; the FMC2/FMC3
  site lives in the sensor name only).  The AMC's own sensors stay on
  `0xC1`/`0x00`.
- **sensor numbers**: `0x10-0x17`, one per monitor, fixed:
  `0x10` FMC2 12V I, `0x11` FMC2 3V3 I, `0x12` FMC2 VADJ V, `0x13` FMC2 VADJ I,
  `0x14` FMC3 12V I, `0x15` FMC3 3V3 I, `0x16` FMC3 VADJ V, `0x17` FMC3 VADJ I.

## Open points

 
- the SDR delete API (only insert exists today).
