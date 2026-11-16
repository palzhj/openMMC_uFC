# u4FCP-v2 — open items (2026-11-10)

Status legend: **[blocked]** needs a document/hardware we do not have yet,
**[next]** clear work, **[opt]** nice to have.

## Clocking (ADN4604 / Si5345)

- [x] Transcribed the ADN4604 pin-level map from `u4FCP_V2.2.pdf` sheet 9
      into `tools/clocking/u4fcp-v2-clocking.json`: 32 ports (IN0..IN15,
      OUT0..OUT15) with package pins and nets, the PLL input/fb links
      (`PLL0_OUT`→IN9, `PLL1_OUT`→IN8, OUT6→PLL0, OUT7→PLL1), U85@0x68 and
      U61@0x69. The validator reports 36 points / 4 links / 0 errors.
- [x] **ADN4604 termination stays off** (2026-11-16): the firmware writes
      `0xF0 = 0x0F` and the part reads back `0x0F` (all four 50 Ω groups
      disabled, `0xFF = 0x04`, `0xFE = 0x02`).  Correct by design - every link
      is AC-coupled with the far end (AMC/FMC/FPGA/RTM) terminating; documented
      in `docs/CLOCK_SWITCH.md`.  A scope check of one live link is still
      worth doing before enabling outputs, but there is nothing to change.
- [ ] **[next]** Decide and capture the **real routing**. The stored preset
      (`tools/firmware/u4fcp-v2/clk-preset.bin`) and the compiled default
      are all outputs disabled (every output from input 8, disabled) -
      confirmed on AMC6: `0x90-0x97` read back `88 88 88 88 88 88 88 88`. Once
      the FPGA/FMC/LEMO/RTM clock consumers are known, load the matrix in the
      Clock tab and `Write to flash` (slot auto).
- [ ] **[opt]** ADN4604 equalizer (0x10/0x11, 0/12 dB) and map 1 / broadcast
      (0x81/0x82) controls in the Clock tab.
- [ ] **[opt]** Periodic runtime PLL-lock monitor (the lock is checked at
      boot, the GUI has an on-demand button).

## Firmware / platform

- [ ] **[next]** Watchdog supervisor: the IPMI, sensor and payload tasks
      should set heartbeats and the WatchdogTask should feed only when all
      are alive (today the feed is unconditional, so a hung task does not
      trip the IWDG).
- [x] **HPM upgrade + activation end-to-end** (2026-11-16): two consecutive
      OTAs on AMC6 (`e4ed725` → `ceb7266` → `e4ed725`, then `7e75f57`), SHA
      verified after each, sensors/FRU/clock store intact, no reboot loop.
      `tools/firmware/u4fcp-v2/ota.sh` automates it (pre-flight, upload,
      activate, SHA check); pass criteria in `docs/testing/OTA_CHECKS.md`.
- [ ] **[next]** OTA **failure** paths with deliberate injection
      (`docs/testing/OTA_CHECKS.md` §3).  Already covered: a wrong `finish`
      length (the board self-test does it - rejected with `0x81`, board alive)
      and the interrupted clock-store write.  Still to inject: aborted upload
      (initiate + blocks, no finish), oversized image, `activate` without
      staging.  AMC1 is still down from the 2026-11-15 OTA and needs the SWD
      recovery (`docs/testing/INCIDENT-2026-11-15-amc1-ota.md`).
- [ ] **[opt]** Reduce the 1.2 s clock-store apply (15 records, 1052
      registers) or keep the payload power-up timing as is.
- [ ] **[opt]** Review the SDR list against the final UCD90320 rail map
      (the FMC VADJ sensors and the module current are verified).

## RTM (rear transition module) management

The u4FCP-v2 supports an RTM (`URTM_V2_3.pdf` in `downloads/schematics`).
The board port owns the RTM control (`port/board/u4fcp-v2/rtm.c` +
`rtm_sensors.c`); the generic `modules/rtm.c` stays disabled:

- `port/board/u4fcp-v2/CMakeLists.txt` has `# "RTM"` commented out, so
  `MODULE_RTM` is not set and the generic `modules/rtm.c` (rtm_manage_init,
  rtm_check_presence, rtm_enable/disable_payload_power) is never compiled —
  its SDR dev-locator/hotswap path does not fit the uRTM, which has its own
  MMC; the board-level `rtm.c` implements the presence/12 V policy instead;
- `RTM_MP3V3_EN` (`UCD_GPO_RTM_MP_EN_ID`, LGPO14) is **deliberately held low**
  by the RTM manager: the uRTM v2 mishandles `RTM_MODE` and would pull a large
  current from MP3V3 before its FPGA is configured, tripping the AMC's MP3V3
  over-current protection (see `docs/HANDOVER-u4FCP-v2.md` §7.1);
- the JTAG side is wired (`JTAG_RTM_EN_B`, `UCD_GPI_RTM_PS_B_ID`) and the
  RTM EEPROM bus exists (`I2C_BUS_RTM_ID` = I2C2 ch5, now enabled for the
  two-level mux path to the uRTM UCD90124A).

- [x] AMC-side RTM control in one manager task (`port/board/u4fcp-v2/rtm.c`,
      state machine OFF → WAIT_AMC → POWER → SETTLE → IDENTIFY →
      MANAGED/UNMANAGED): presence from RTM_PS# (PA8), 12 V after
      `RTM_TURNON_DELAY_MS` (1 s), FRU probed 5 s later, 12 V cut on unplug /
      handle removal / RTM fault / payload shutdown.  The handle is only judged
      after a 10 s configuration window (the uRTM drives RTM_INT# low while it
      boots).
- [x] The nine FMC2/FMC3 SDR records are **kept for the whole session** once
      the RTM has been seen plugged; when it is not running they report
      "reading unavailable" (scanning disabled, reading 0) instead of being
      deleted.  Deleting them made the MCH print `could not read sensor!`
      (0xC9): NAT-MCH caches the module SDRs and only re-reads them at a FRU
      activation (`shutdown <id>` + `fru_start <id>`) or an MCH cold reset -
      there is no rescan command (`sdrrep_dbg` only sets log levels).
      Verified: RTM off -> `show_sensorinfo 16` shows all 10 RTM rows with
      **0 error lines** and 0 values; RTM back -> live values again.
- [ ] **[opt]** The yellow **FMC VADJ PG LEDs** (`LED_VADJ_PG0/1`, UCD90320
      LGPO10/LGPO11, Pin_ID 65/66, balls M1/M2) are driven by the **UCD's own
      logic** (Logic GPO), not by the MMC - wiring is net -> 330R -> LED ->
      GND (pin high = lit).  The MMC must not configure those pins with
      `ucd_set_gpio()`: a GPIO_CONFIG write with Enable=1 takes them over from
      the UCD logic.  Currently they are dark because the UCD reports
      `GPIO_CONFIG 0x08` (Enable=0, input, no logic driving) - the UCD's stored
      configuration needs checking against the Fusion project
      (`u4FCPv2/template/u4FCPv2/POWER/*.tifsp`); nothing in this firmware
      drives them (verified over the whole history).
- [x] **RTM_HANDLE** comes from **RTM_INT_B (PA3, active low)**, monitored
      only after `RTM_CONFIG_DELAY_MS` (10 s) - i.e. after the uRTM's MCU has
      configured the RTM clocks and gone silent.  Measured on AMC12: RTM_PS#
      (PA8) stays low while the handle is open (it only says "plugged"),
      RTM_INT_B goes 1 -> 0 when the handle is opened, and it is also low
      while the RTM is unpowered/booting, so the line is only valid after the
      configuration window.  Opening the handle removes the FMC2/FMC3 sensors
      and cuts the RTM 12 V (2 s confirm); the RTM then stays **unpowered**
      (no automatic re-power: the uRTM has no management power of its own and
      the handle cannot be read while it is off).  Power is only given again
      on the next payload power cycle, whose `PAYLOAD_POWER_GOOD_WAIT` state
      checks RTM_PS# and powers the RTM if it is plugged.  Verified on AMC12 +
      uRTM2.3: pull -> 35 -> 26 sensors and 12 V -> 0 A, no retry; power cycle
      -> `RTM detected` -> 12 V on -> sensors and dump return after the
      configuration window.
- [x] FMC2/FMC3 monitors as AMC-owned RTM sensors (`rtm_sensors.c/.h`,
      entity 0xC0/0x00, sensor numbers 0x1B-0x23 (right after the AMC's
      0x01-0x1A, so the MCH lists AMC first, RTM last): the UCD temperature
      first, then the eight FMC monitors, 1 s poll of the uRTM
      UCD90124A behind 0x70 ch5 -> 0x71 ch7, SDRs inserted/removed with the
      RTM presence).  Details and the verification evidence:
      `docs/RTM_SENSORS_PLAN.md`.  The boot dump needs the UART console
      (`/dev/ttyUSB0`, 115200).
- [ ] **[next]** Physical mount/remove test of the monitor group: the SDR
      insert/remove path was exercised with a bench build
      (`-DMODULES_FLAGS="-DRTM_SENSORS_SELFTEST"`, pretends the handle was
      opened for 30 s), but pulling the uRTM with a human at the crate is
      still open.  Note the NAT-MCH keeps the module SDR rows cached until it
      re-reads the module repository, so after an unplug it shows the eight
      names with a read error.
- [ ] **[next]** RTM FRU/hotswap reporting to the MCH (the uRTM's FRU EEPROM
      on I2C2 ch5, FRU_RTM + SDR 0xC0 hotswap record) — requires deciding
      whether the AMC or the RTM's own MMC owns the RTM FRU.
- [ ] **[opt]** Add the remaining RTM rail monitors if needed (`12V0_RTM`, the RTM
      temperatures) — the FMC2/FMC3 group covers pages 2-7 only.
- [ ] **[next]** Verify the RTM JTAG path (`RTM_EN_B`, TCA9554 on ch4) with
      the RTM plugged.  The RTM FRU read itself is already proven - it is how
      the uRTM v2.3 is identified (`0x70 ch5 -> 0x71 ch7 -> 0x55`).
- [x] **Bench matrix** (`docs/testing/RTM_CHECKS.md`), all three scenarios
      verified on AMC6 (2026-11-16): **no RTM** (manager OFF, no rail, no
      probes, no SDRs, bus healthy), **uRTM v2.3** (MANAGED, 35 SDRs, RTM
      readings live, 36 rows on the MCH after a FRU activation) and the
      **LattePanda carrier** (boots with BIOS auto power-on, ~1.5-1.7 A).
- [x] **LattePanda carrier boots** (2026-11-16): the Mu needed a power-button
      press; with the module's BIOS set to auto power-on it starts from the
      RTM 12 V and draws ~1.5-1.7 A (`RTM +12V Curr`).  Recorded in
      `docs/HANDOVER-u4FCP-v2.md` §7.1.

## Toolbox

- [x] ADN4604 mux table (inputs | inversion | gap | outputs, termination),
      Read/Write, Write to flash, Update lock status.
- [x] FMC VADJ (TPL0102 @0x56 ch7) and JTAG target (TCA9554 @0x39 ch4)
      controls on the I²C tab.
- [ ] **[opt]** Clock tab: per-PLL table display/edit for both Si5345s at
      once (currently one PLL at a time via the selector).

## Process

- [x] **Board self-test** (`tools/testing/board_selftest.py`): **36/36** on
      AMC6 with the uRTM v2.3 plugged (2026-11-16), report kept in
      `docs/testing/TEST_REPORT-20261116-1010.md`.
- [ ] **[next]** Manual part of `docs/testing/REGRESSION.md`: T1.4/T1.5
      (watchdog `PR=4`, `RLR=0x4E2`; a no-feed build must reset every ~2 s),
      T1.6 (>= 30 s stable), T3.6 (lost link) and T4.1 (payload cut mid-boot,
      the next `fru_start` completes with FRU + clock store intact).
