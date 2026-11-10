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
