# u4FCP-v2 board test report — 2026-11-10

Board: u4FCP-v2 in **AMC12**, IPMB **0x88** (MCH 192.168.1.41, transit 0x82,
bus 0). Firmware: `openMMC v1.6.2-207`, watchdog-enabled default build
(`DISABLE_WATCHDOG=OFF`, STM32F1 IWDG 2 s). Harness:
[`tools/testing/board_selftest.py`](../../tools/testing/board_selftest.py);
raw self-test log:
[`TEST_REPORT-20261110-2231.md`](TEST_REPORT-20261110-2231.md) (24/24 PASS).

## 1. Flashing / reset startup / watchdog

| test | result | evidence |
|---|---|---|
| flash vectors | PASS | newboot `SP=0x20005000 PC=0x080007C1`; app `SP=0x20005000 PC=0x080032F9` |
| image sizes | PASS | newboot 4 472 B, app 55 608 B (HPM staging area ≈ 59.5 KB) |
| version / sha1 in the log | PASS | `openMMC v1.6.2-207`, `sha1 2cca869…` = the tree HEAD at flash time |
| reset startup (J-Link reset) | PASS | boot log complete: main power 0.05 s, FMC0 1.05 s, FMC1 2.05 s, VADJ 3.06 s, clock apply 4.65 s (1 106 ms), PLLs locked 4.76 s |
| IPMB answering after reset | PASS | the IPMI task starts right after the boot banner (~1 s); the payload sequence completes at 4.8 s, Get Device ID served throughout |
| IWDG programmed | PASS | `IWDG_PR=0x04` (÷64), `IWDG_RLR=0x04E2` (1250 → **2.0 s**), `SR=0` |
| watchdog recovery (no feeds) | PASS | feed loop disabled → **5 boots in 10 s** (a reset every ~2 s) |
| watchdog with feeds | PASS | single boot, 10/30 s IPMB replies, full power-up incl. the 1.2 s clock apply |

Note: a debugger halt is *not* a valid watchdog test — the J-Link sets
`DBGMCU_CR.IWDG_STOP`, which freezes the IWDG while the core is halted. The
no-feed build is the authoritative test.

## 2. Peripherals — normal path / fault path

| peripheral | normal path | fault path |
|---|---|---|
| FRU EEPROM (0x54) | 512 B read, header `01 00 00 01 08 10 00 e6` | addr **0x55** absent → `rsp=0xff` (no hang, bus recovered) |
| clock store (EEPROM) | status `valid=1 slot=0 len=3482 crc=0xee6efb3a` | — (see §4 atomicity) |
| UCD90320 (0x11) | PMBus `0x99` (MFR_ID) → `06` | addr **0x12** absent → `rsp=0xff` |
| ADN4604 (0x48) | map0 `0x90…0x97` → `88 88 88 88 88 88 88 88` | addr **0x49** absent → `rsp=0xff` |
| Si5345 #0/#1 (0x68/0x69) | status page 0x0C register 0x00 → `00 00` (no LOS/LOL/OOF flags); boot log confirms `PLL0 locked, PLL1 locked` | — |
| TPL0102 (0x56) | wiper read → `00` | addr **0x57** absent → `rsp=0xff` |
| TCA9554 (0x39) | output reg → `f0` (AMC JTAG selected, RTM/FMC0/FMC1_EN_B high = deselected) | addr **0x3A** absent → `rsp=0xff` |
| TCA9548A mux (0x70) | channels 0/2/4/7 all switch correctly | channel **3** (no device) → `rsp=0xff` |

Every fault path returns an IPMI error to the requester and the subsequent
normal accesses succeed (the I²C bus recovery runs on a failed transfer).

## 3. Communication — normal / erroneous / timeouts

| test | result | evidence |
|---|---|---|
| Get Device ID | PASS | `dev_id=0x8000 fw=1.01 ipmi=2.0 mfg=0x00315a` |
| unsupported command | PASS | `netfn=0x6 cmd=0x7f` → **0xC1** (Invalid command) |
| short Get FRU Info | PASS | → **0xC7** (Request data length invalid) |
| I²C read 1 / 20 / 31 B | PASS | exact lengths returned |
| I²C read 32 / 33 B | PASS | rejected (`read length must be 0..31`) — the IPMB frame limit |
| I²C zero-length write | PASS | accepted |
| absent target / bad mux channel | PASS | error code, no lockup (see §2) |
| MCH bridge disconnect | PASS | the MMC's IPMB is passive; a lost LAN link leaves the MMC idle, and the next request after reconnect is served |
| long-run polling (564 requests / 600 s) | PASS | 0 errors, 0 timeouts |

## 4. Power loss / firmware-upgrade failure

| test | result | evidence |
|---|---|---|
| boot cut during the FMC power-on (shutdown at ~3 s) | PASS | next `fru_start` re-runs the whole sequence; FRU and clock store intact |
| repeated interrupted boots | PASS | IPMB + store (`valid=1 len=3482`) + FRU (`SL02`, `u4FCP-v2`) verified afterwards |
| HPM upload aborted (5 blocks, no finish) | PASS | board keeps answering the next request; the next prepare resets the staging |
| HPM finish with a wrong length | PASS | `0x33` rejected (`rsp=0x81`), device ID unchanged, old firmware still running |
| clock store interrupted write (begin + 2 blocks, no finish) | PASS | status still `valid=1 slot=0 len=3482 crc=0xee6efb3a` — the A/B pointer only moves on a verified finish |

## 5. Buffer boundaries / soak

| test | result | evidence |
|---|---|---|
| EEPROM 0x01F0 (FRU end) | PASS | `ff…ff` |
| EEPROM 0x0200 / 0x0280 (pointer + copy) | PASS | both `55 46 43 50 05 00 00 00` (`UFCP`) |
| EEPROM 0x0300 / 0x1B00 (slot A/B start) | PASS | `55 46 43 43 01 00 20 00` (`UFCC`) |
| EEPROM 0x1AFF → 0x1B00 (slot boundary) | PASS | `ff` then the slot-B header — the auto-increment crosses cleanly |
| EEPROM 0x32FF / 0x3300 / 0xFFFC (end) | PASS | `ff…` |
| FRU 512 B | PASS | 512 B |
| clock slot length | PASS | 3 482 B (15 records, 1052 registers) |
| soak 600 s | PASS | 564 polls (device ID + sensor read each second), **0 errors**, heap constant `0x780c0000` |

## Notes / residual risks

- The management-power loss of the MMC itself cannot be injected remotely;
  the J-Link reset and the AMC payload power cycles cover the warm-reset and
  payload-restart paths.
- `-DDISABLE_WATCHDOG=ON` builds have no IWDG at all (debugging only).
- The IWDG cannot be stopped once started; a hung feed task resets the MMC.
- The HPM staging area is EEPROM-backed; an aborted upload leaves it dirty
  until the next `0x31 prepare`, which truncates it.
