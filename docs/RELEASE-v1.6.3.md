# openMMC uFC fork — release v1.6.3

Release date: 2026-11-11. Base: upstream openMMC `v1.6.2` + 217 commits of
board support for the uFC‑v2, u4FCP‑v1 and u4FCP‑v2 AMC modules
(STM32F100/F103).

## Highlights

- **u4FCP‑v2 support**: STM32F103CBT6 port, FRU + clock store in the
  CAT24C512 EEPROM, UCD90320 sequencing, ADN4604 clock crosspoint, two
  Si5345 PLLs, TPL0102 FMC VADJ, TCA9554 JTAG selector, HPM OTA.
- **u4FCP‑v1 support**: the first‑generation board (8V54816A switch, one
  Si5345 at 0x69, AT24CM01 EEPROM with the upper 64 KB used for staged
  firmware).
- **Clock configuration store**: A/B FRU‑format slots in the EEPROM (or
  flash on the uFC), uploaded over IPMI (`0x32 0x18/0x19/0x1A/0x1B/0x1C`),
  applied on every payload power‑up (the ADN4604/Si5345 are on the 12 V
  rail), atomic pointer update. The shipped preset programs the two
  ClockBuilder PLL tables and leaves all ADN4604 outputs disabled.
- **ADN4604**: full port map transcribed from the u4FCP_V2.2 schematic
  (IN0..IN15 / OUT0..OUT15 with pins), `docs/CLOCK_SWITCH.md`.
- **Watchdog**: STM32F1 IWDG implemented (LSI ÷64, 2 s timeout, fed every
  666 ms) and **enabled by default** (`-DDISABLE_WATCHDOG=ON` to opt out).
- **Payload**: clock configuration (EEPROM preset or compiled defaults),
  VADJ trim, JTAG selection, dmesg‑style boot log, payload‑restart marker.
- **Sensors/SDR**: payload sensor gating on quiesce/shutdown (no MCH sensor
  flood), state byte per the IPMI spec, FMC VADJ after each FMC power‑up.
- **Toolbox**: FRU/clock/I²C/OTA tabs, ADN4604 mux table, FMC VADJ and JTAG
  controls, PLL lock status, template‑driven register maps, standalone
  binaries for Windows and Linux.
- **Tests**: `tools/testing/board_selftest.py` + the regression plan
  (`docs/testing/REGRESSION.md`), 36 automatic checks and the manual
  flash/watchdog/power‑loss checklist.
- **Fixes** worth noting: clock‑store record checksum loop (uint8 overflow),
  pointer copy moved out of slot A, PLL1 NVM address, 1048/1052 register
  table alignment, FRU EEPROM timeouts, UCD read hang, J‑Link failure
  reporting.

## Programming

```bash
cmake -S . -B .hw2/u4fcp -DBOARD=u4fcp-v2 -DTARGET_CONTROLLER=STM32F103CBT6 \
      -DCMAKE_BUILD_TYPE=Release
make -C .hw2/u4fcp openMMC -j
tools/firmware/u4fcp-v2/program.sh U4FCP2-0001   # flash + FRU (+ VADJ/clock)
```

## Validation

The release image was tested on AMC12 through the MCH bridge:
device ID, FRU, clock store (slot 0, 3482 B, CRC `0xee6efb3a`), both PLLs
locked, ADN4604 map `0x88`×8 / TX `0x00`×16, FMC0/FMC1 VADJ 1800 mV, JTAG
expander `output=0x7F config=0x0F`, and the regression suite (36/36, 600 s
soak, heap constant).

Open items are tracked in [`TODO-u4fcp-v2.md`](TODO-u4fcp-v2.md).
