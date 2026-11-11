# u4FCP-v2 — open items (2026-11-10)

Status legend: **[blocked]** needs a document/hardware we do not have yet,
**[next]** clear work, **[opt]** nice to have.

## Clocking (ADN4604 / Si5345)

- [x] Transcribed the ADN4604 pin-level map from `u4FCP_V2.2.pdf` sheet 9
      into `tools/clocking/u4fcp-v2-clocking.json`: 32 ports (IN0..IN15,
      OUT0..OUT15) with package pins and nets, the PLL input/fb links
      (`PLL0_OUT`→IN9, `PLL1_OUT`→IN8, OUT6→PLL0, OUT7→PLL1), U85@0x68 and
      U61@0x69. The validator reports 36 points / 4 links / 0 errors.
- [ ] **[next]** Confirm the ADN4604 termination by measurement: the firmware
      writes `0xF0 = 0x0F` (all four 50 Ω quadrants disabled). The schematic
      is AC-coupled everywhere with the far end (AMC/FMC/FPGA) providing the
      differential termination; check one link with a scope before changing.
- [ ] **[next]** Decide and capture the **real routing**. The stored preset
      (`tools/firmware/u4fcp-v2/clk-preset.bin`) and the compiled default
      are all outputs disabled (`0x08 ×16`, i.e. every output ← input 8,
      disabled). Once the FPGA/FMC/LEMO/RTM clock consumers are known, load
      the matrix in the Clock tab and `Write to flash` (slot auto).
- [ ] **[opt]** ADN4604 equalizer (0x10/0x11, 0/12 dB) and map 1 / broadcast
      (0x81/0x82) controls in the Clock tab.
- [ ] **[opt]** Periodic runtime PLL-lock monitor (the lock is checked at
      boot, the GUI has an on-demand button).

## Firmware / platform

- [ ] **[next]** Watchdog supervisor: the IPMI, sensor and payload tasks
      should set heartbeats and the WatchdogTask should feed only when all
      are alive (today the feed is unconditional, so a hung task does not
      trip the IWDG).
- [ ] **[next]** Successful HPM upgrade + activation end-to-end test (only
      the failure paths are covered: aborted upload, bad finish).
- [ ] **[opt]** Reduce the 1.2 s clock-store apply (15 records, 1052
      registers) or keep the payload power-up timing as is.
- [ ] **[opt]** Review the SDR list against the final UCD90320 rail map
      (the FMC VADJ sensors and the module current are verified).

## RTM (rear transition module) management

The u4FCP-v2 supports an RTM (`URTM_V2_3.pdf` in `downloads/schematics`).
The generic RTM module and the board signals are present, but the RTM path
is **switched off** in the u4FCP-v2 build:

- `port/board/u4fcp-v2/CMakeLists.txt` has `# "RTM"` commented out, so
  `MODULE_RTM` is not set and `modules/rtm.c` (rtm_manage_init,
  rtm_check_presence, rtm_enable/disable_payload_power) is never compiled;
- the port has **no `rtm.c`** (the afc-v3 port has the reference board file);
- the RTM presence path is stubbed: `RTM_PS_B` / `RTM_RES` / `RTM_INT_B`
  reads are commented in `board_config.c`, the RTM management-power enable
  (`UCD_GPO_RTM_MP_EN_ID`) is commented in `payload.c`, and the boot log
  prints `pwr : no RTM`;
- the JTAG side is wired (`JTAG_RTM_EN_B`, `UCD_GPI_RTM_PS_B_ID`) and the
  RTM EEPROM bus exists (`I2C_BUS_RTM_ID` = I2C2 ch5).

- [x] AMC-side RTM control in `port/board/u4fcp-v2/rtm.c`: presence from
      RTM_PS# (PA8), 12 V after `RTM_TURNON_DELAY_MS` (3 s), handle monitor
      only after `RTM_CONFIG_DELAY_MS` (10 s) so the RTM controller can
      configure its clocks, 12 V cut on handle removal / RTM fault / payload
      shutdown or reset.  Verified on AMC12 + uRTM2.3: `rtm : RTM detected`
      -> `rtm : RTM 12 V on (handle monitor in 10 s)`.
- [ ] **[next]** RTM FRU/hotswap reporting to the MCH (the uRTM's FRU EEPROM
      on I2C2 ch5, FRU_RTM + SDR 0xC0 hotswap record) — requires deciding
      whether the AMC or the RTM's own MMC owns the RTM FRU.
- [ ] Add the RTM rails/sensors to the SDR if the AMC MMC reports them
      (`12V0_RTM`, `RTM_PS#`, the RTM temperatures).
- [ ] Verify the RTM FRU read over I2C2 ch5 and the RTM JTAG (`RTM_EN_B`)
      with the RTM plugged (currently untested — no RTM in the crate).

## Toolbox

- [x] ADN4604 mux table (inputs | inversion | gap | outputs, termination),
      Read/Write, Write to flash, Update lock status.
- [x] FMC VADJ (TPL0102 @0x56 ch7) and JTAG target (TCA9554 @0x39 ch4)
      controls on the I²C tab.
- [ ] **[opt]** Clock tab: per-PLL table display/edit for both Si5345s at
      once (currently one PLL at a time via the selector).

## Process

- [ ] Run `tools/testing/board_selftest.py` + the manual checklist
      (`docs/testing/REGRESSION.md`) after every major revision; keep the
      generated report with the release notes.
