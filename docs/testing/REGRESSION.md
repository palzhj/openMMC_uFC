# Board regression test plan

Run this suite **after every major revision** (firmware, toolbox protocol,
board port, clock/sensor changes). One automatic command covers phases 2–5;
phase 1 (flash, watchdog) is a short manual checklist because it needs the
J-Link.

## Test items

| id | area | item | how | pass criterion |
|---|---|---|---|---|
| T1.1 | flash | newboot + app vectors | J-Link `mem 0x08000000` / `0x08002000` | SP/PC are valid (`0x20005000` / `0x0800…`) |
| T1.2 | flash | version + sha1 in the boot banner | UART | matches the built tree |
| T1.3 | startup | reset → boot log | J-Link reset | full sequence: main power, FMC0/FMC1, VADJ, clock apply, PLL locked, payload done |
| T1.4 | watchdog | IWDG programmed | J-Link `mem 0x40003004` | `PR=4`, `RLR=0x4E2` (2 s) |
| T1.5 | watchdog | recovery | no-feed build | reset every ~2 s (≈5 boots/10 s) |
| T1.6 | watchdog | normal feed | default build | single boot, ≥30 s stable |
| T2.1 | FRU EEPROM | read + absent addr | automatic | 512 B, header `01000001…`; 0x55 → `0xff` |
| T2.2 | clock store | status | automatic | `valid=1`, len/crc match the stored preset |
| T2.3 | UCD90320 | MFR_ID + absent | automatic | `06`; 0x12 → `0xff` |
| T2.4 | ADN4604 | map read + absent | automatic | `88 88 …`; 0x49 → `0xff` |
| T2.5 | Si5345 ×2 | status read | automatic | page 0x0C/0x00 readable, no flags |
| T2.6 | TPL0102 | wiper + absent | automatic | read ok; 0x57 → `0xff` |
| T2.7 | TCA9554 | output reg + absent | automatic | readable; 0x3A → `0xff` |
| T2.8 | TCA9548A | channel switch + empty ch | automatic | ch0/2/4/7 ok; empty ch3 → `0xff` |
| T3.1 | IPMB | Get Device ID | automatic | `0x8000`, ipmi 2.0, mfg `0x00315a` |
| T3.2 | IPMB | unsupported command | automatic | `0xC1` |
| T3.3 | IPMB | short FRU request | automatic | `0xC7` |
| T3.4 | I²C transfer | lengths 1/20/31/32/33 | automatic | 1..31 exact; 32/33 rejected |
| T3.5 | I²C transfer | zero-length write | automatic | accepted |
| T3.6 | transport | lost link | manual | MMC idle, next request after reconnect served |
| T4.1 | power loss | boot cut (shutdown mid-boot) | manual (MCH) | next `fru_start` completes, FRU + store intact |
| T4.2 | upgrade | HPM finish with wrong length | automatic | rejected, board alive, old firmware |
| T4.3 | clock store | interrupted write | automatic | old slot still valid |
| T5.1 | boundaries | EEPROM 0x01F0/0x0200/0x0280/0x0300/0x1AFF/0x1B00/0x32FF/0xFFFC | automatic | headers `UFCP`/`UFCC` at the expected places |
| T5.2 | boundaries | FRU 512 B | automatic | 512 B |
| T5.3 | soak | 600 s polling | automatic | 0 errors, heap constant |
| T5.4 | sensors | monitoring stress, 240 s (1 session + 4 sessions) | `python3 tools/testing/sensor_stress.py --seconds 240 --workers 4` | 0 MMC completion-code errors, 35 sensors before/after, heap constant; note the crossed-response limitation below |

## Procedure

```bash
# 1. build the production image (watchdog is on by default)
cmake -S . -B .hw2/u4fcp -DBOARD=u4fcp-v2 -DTARGET_CONTROLLER=STM32F103CBT6 \
      -DCMAKE_BUILD_TYPE=Release
make -C .hw2/u4fcp openMMC -j

# 2. flash + check the vectors (T1.1) and the boot log (T1.2/T1.3)
sh tools/firmware/u4fcp-v2/jlink_program.sh

# 3. automatic suite (phases 2,3,4,5 + 5 min soak)
python3 tools/testing/board_selftest.py --soak 300

# 3b. sensor-monitoring stress (2 min sequential + 2 min with 4 sessions);
#     writes docs/testing/SENSOR_STRESS-<timestamp>.md
python3 tools/testing/sensor_stress.py --seconds 240 --workers 4

# 4. watchdog recovery (T1.4/T1.5): rebuild a test image with the feed call
#    in modules/watchdog.c commented out, flash, count the resets in 10 s,
#    then restore the source and reflash the production image.

# 5. power loss (T4.1): MCH telnet `shutdown 16`, `fru_start 16`, cut the
#    power again during the FMC power-on, `fru_start 16`; check the IPMB,
#    `raw 0x32 0x18` and `fru print`.
```

The automatic run writes `docs/testing/TEST_REPORT-<timestamp>.md` (a table of
all items with the observed values). Keep it next to the release notes; for a
release, run with `--soak 600` or longer.

## Known limitation: crossed responses with concurrent sessions

`tools/testing/sensor_stress.py` measured 0 crossed readings with one IPMI
session, but ~0.2-0.4 % of the requests receive *another* session's answer when
two or more sessions hit the MMC concurrently (the value and state bytes belong
to a different sensor).  The MMC answers everything correctly, the sensor table
and heap stay constant, and the failures are only in the response payload.

Likely cause: the IPMB slave receive path (`port/ucontroller/stm32/stm32f1xx/
stm32_i2c.c`) uses one `recv_msg` buffer filled by the I2C interrupt; a second
request arriving before the RX task copies the first overwrites it, so the
request (and its sequence number) is answered with the second request's data.
A double buffered receive (ISR fills the inactive buffer, marks it ready) should
remove it - re-run the stress tool to confirm (expect 0 / several thousand).

## Acceptance

- all **automatic** items PASS (the script returns 0);
- T1.1–T1.6, T3.6, T4.1 PASS during the manual part;
- no new warnings in the boot log;
- the heap value in the soak report equals the previous release's (no leak).
