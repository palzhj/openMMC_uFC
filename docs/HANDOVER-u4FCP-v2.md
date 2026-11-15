# u4FCP-v2 MMC firmware — handover

Everything a new developer (or AI) needs to continue this tree.  Repo:
`openMMC_uFC` — an openMMC fork whose main product is the **u4FCP-v2** AMC
(Fast Control and Processing board, MicroTCA.4) and its **uRTM v2.3** rear
transition module.  The local branch `main` tracks `origin/master`
(`git@github.com:palzhj/openMMC_uFC.git`); plain `git push` fails because the
branch names differ — use `git push origin HEAD:master`.

Board-side sources live in `port/board/u4fcp-v2/`, shared drivers in `modules/`,
the controller port (STM32F1) in `port/ucontroller/stm32/stm32f1xx/`, and the
host tooling in `tools/`.

---

## 1. Hardware in one page

| item | value |
|---|---|
| MMC | **STM32F103CBT6**, 128 KB flash, 20 KB SRAM, Cortex-M3, 8 MHz crystal |
| IPMB-L | **I2C1**, slave address `0x70 + 2·slot` (AMC12 → `0x88`, AMC1 → `0x72`) |
| on-board I2C | **I2C2**, behind a `TCA9548A` at **0x70** (8 channels), 100 kHz |
| FRU EEPROM | **CAT24C512** at **0x54** (I2C2 ch7), 512-byte FRU image |
| power controller | **UCD90320** PMBus at **0x11** (I2C2 ch7): sequencing, enables, monitoring |
| clock crossbar | **ADN4604** at **0x48** (I2C2 ch2) — 16 in × 16 out |
| PLLs | 2 × **Si5345** at **0x68 / 0x69** (I2C2 ch2) |
| FMC VADJ | **TPL0102** digital pot at **0x56** (I2C2 ch7), default 1.8 V |
| JTAG switch | **TCA9554** at **0x39** (I2C2 ch4) |
| RTM (Zone 3) | IPMB bus on I2C2 ch5; uRTM: `TCA9548A` 0x71 ch7 → UCD90124A 0x5B + FRU 0x55 |

TCA9548A channel map (`port/board/u4fcp-v2/i2c_mapping.c`):

| ch | bus | ch | bus |
|---|---|---|---|
| 0 | FMC0 (EEPROM 0x50) | 4 | FPGA (TCA9554 0x39) |
| 1 | FMC1 (EEPROM 0x51) | 5 | **RTM** |
| 2 | CLOCK (ADN4604, Si5345) | 6 | DDR4 |
| 3 | FireFly | 7 | **POWER** (UCD, FRU EEPROM, TPL0102) |

`xI2CMasterWrite/Read/WriteRead(interface, addr, …)` take *interface* 0 = I2C1
(IPMB-L) and 1 = I2C2.  Always claim a device through
`i2c_take_by_chipid(CHIP_ID_*, &addr, &iface, timeout)` and release with
`i2c_give(iface)`; it walks the mux for you.  The RTM path needs an extra
`i2c_rtm_mux_select(iface)` for the second mux level (0x71 ch7).

---

## 2. Flash/RAM layout and boot chain

| region | range | size |
|---|---|---|
| `newboot` (first stage) | `0x08000000–0x08001FFF` | 8 KB |
| application | `0x08002000–0x08010DFF` | **0xEE00 = 60928 B** |
| staging (HPM image) | `0x08010E00–0x0801FBFF` | 60928 B |
| `fw_info` record | `0x0801FC00–0x0801FFFF` | 1 KB |

The image embeds the git tag/SHA (`g_GIT_TAG`, `g_GIT_SHA1`) generated from
`git describe` **at configure time** (`modules/GitSHA1.c.in` →
`.hw2/u4fcp/GitSHA1.c`): after checking out a different commit, re-run cmake
before building an image, otherwise the flashed firmware reports the old SHA
(`raw 0x32 0x02`).

Boot: `newboot` (own linker script `linker/STM32F103CBT6_boot.ld`) checks
`fw_info` for magic `0xAAAAAAAA`; if set it copies the staging area to the
application (`fw_type 0x02` = bootloader self-update) or over itself, erases
`fw_info`, then jumps to `0x08002000`.  The app therefore must keep linking at
`0x08002000`.  The clock configuration is **not** in flash: it lives in the
CAT24C512 (pointer `0x200`, slots A/B at `0x300`/`0x1B00`, 6 KB each).

**Flash budget is the dominant constraint.**  The image is ~60 KB for a 60928 B
region (~0.5–1 KB free).  Two lessons are baked into the tree:

- `.data` is linked with `AT(_sidata)` and `>RAM`, so its load address is
  *outside* the `>FLASH` region check.  `linker/STM32F103CBT6_app.ld` ends with
  an `ASSERT` that `_sidata + SIZEOF(.data) <= __AppFlash_end + 1`; without it
  a nearly full image silently spilled `.data` into the staging region.  Keep
  that assert and check the headroom after every feature.
- The Si5345 default tables are `__attribute__((packed))` (526 × 3 bytes each,
  1 KB saved); the RTM boot dump is behind `-DRTM_SENSORS_BOOT_DUMP=ON` (~850 B,
  off in production).  Both are deliberate — do not "clean them up".

RAM: 20 KB total, FreeRTOS heap `configTOTAL_HEAP_SIZE=12288`, the rest is the
main/exception stack.  Watch memory (`raw 0x32 0x01` returns the free heap).

---

## 3. Firmware architecture

Init order in `modules/main.c`: clock/LED → **i2c_init()** → `fru_init(FRU_AMC)`
→ `sdr_init()` → `ipmb_init()` (IPMB-L slave on I2C1, address from strapping)
→ `board_config()` (creates the board tasks) → scheduler.

Board port files (`port/board/u4fcp-v2/`):

| file | responsibility |
|---|---|
| `board_config.c` | `board_init()` (NVIC grouping!) and `board_config()` which starts `rtm_init()` |
| `payload.c` | the payload power state machine, clock apply, JTAG config, LEDs |
| `pin_mapping.c/.h` | STM32 pin table (`GPIO_RTM_PS_B` = PA8, `GPIO_RTM_INT_B` = PA3, …) |
| `i2c_mapping.c/.h` | bus/mux/chip tables and `i2c_rtm_mux_select()` |
| `sdr_list.c` | the AMC SDR records (`SDR_type_01h_t`), sensor numbers/names |
| `rtm.c/.h` | the **RTM manager task**: 12 V switch, identification, monitors, handle/fault policy, `RTM_RES` |
| `rtm_sensors.c/.h` | uRTM FMC2/FMC3 monitor access and dynamic SDR insert/remove (a library, no task) |
| `rtm_fru.c/.h` | one FRU read + classification (stateless; the manager owns timing and caching) |
| `ipmi_oem.c/.h` | board OEM IPMI commands (I²C transfer, GPIO) |
| `user_amc_fru.h` | compiled FRU fallback (used only if the EEPROM is invalid) |

Tasks: IPMB (IPMI request/response), payload (1 s state machine), sensors (UCD
poll every **5 s**, 40 ms access timeout), **RTM manager (1 s)**, HPM, watchdog
feed (666 ms).  The RTM manager is the only task that touches Zone 3 — the
payload task just publishes `rtm_payload_up()`.

Watchdog: IWDG from LSI /64, reload 1250 → **2 s** timeout.  `-DDISABLE_WATCHDOG=ON`
only for debugging (it cannot be stopped once started).

---

## 4. Power architecture and sequencing (the important part)

### 4.1 Who controls what

```
shelf PM ──12 V payload──► AMC ─┬─ UCD90320 (0x11) ─┬─ rail enables / monitoring
       (MCH, at M4)             │  CONTROL pin      │
                                │  ◄── GPIO_PMBUS_CTRL (MMC)
                                ├─ TPL0102 (0x56) ── FMC0/FMC1 VADJ voltage
                                ├─ ADN4604 + 2×Si5345 (main power, config lost each cycle)
                                └─ RTM 12 V rail (UCD channel, policy in rtm.c)

shelf MP ──3.3 V management──► MMC (always on, independent of payload power)
```

- **The UCD90320 is a sequencer, not a regulator.**  It enables rails in the
  order configured in its own flash (rail order, delays and limits are set
  outside this repo), monitors V/I/T, reads fault/PG/PS GPIs and drives GPO
  enables.  The MMC only sets
  `OPERATION` per page and reads the results.
- **`GPIO_PMBUS_CTRL` is the master enable**: the UCD's CONTROL input.  Nothing
  sequences until `setDC_DC_ConvertersON(true)` drives it (log line
  `pwr : main power on`).  If rails stay off, check this pin first.
- **Rails the MMC controls** (`UCD_*_ID` in `payload.h`, one PMBus page each):
  `AMC_12V` (0), `RTM_12V`, `FMC0_12V_CURR`, `FMC0_VADJ`, `FMC0_3V3_CURR`,
  `FMC1_12V_CURR`, `FMC1_VADJ`, `FMC1_3V3_CURR`, `FPGA_0V85`, `FPGA_1V8`,
  `FPGA_0V9A`, `FPGA_1V2A`, `DDR_1V2T`, `DDR_1V2B`, `AMC_IO_3V3`.
- **GPOs used at runtime**: `UCD_GPO_FMC0_3V3AUX_EN_ID` (LGPO2) and
  `UCD_GPO_FMC1_3V3AUX_EN_ID` (LGPO7) enable the FMC 3.3 V aux; the VADJ rails
  are enabled through their own UCD pages.  `UCD_GPO_RTM_MP_EN_ID` (LGPO14) and
  `UCD_GPO_FMC*_PG_C2M_ID` (LGPO9/LGPO13) are **intentionally not driven**
  (commented out in `payload.c`: the RTM MP comes up with the AMC, PG_C2M is
  unused on this board).
- **GPIs that matter**: `UCD_GPI_RTM_PS_B_ID` (MAR18, RTM plugged),
  `UCD_GPI_RTM_12V0_FLT_B_ID` (MAR15), `UCD_GPI_RTM_MP_FLT_B_ID` (LGPO16),
  FMC presence/PG/fault pins, `UCD_GPI_OT_B_ID`, `UCD_GPI_FANFAIL_B_ID`.
- **VADJ** is two things: the *rail* (UCD page, on/off) and the *voltage* (the
  TPL0102 pot at 0x56, non-volatile with `vadj_program.sh --persist`).  The MMC
  only switches the rail; a factory board is 1.8 V (DAC 0).
- **The AMC +12 V rail is monitored through the board's divider**: the raw
  PMBus `READ_VOUT` of that page is not 12 V (a uFC page reads 6.25 V for a
  2:1 divider); the SDR scaling reports the real voltage.  Trust the sensor,
  and read the schematic before touching any scaling.
- **ADN4604 + Si5345 are on the payload main power** and lose their
  configuration every cycle, which is why the clock store is applied on every
  power-up (see §5).

### 4.2 Power-up sequence (1 s state-machine tick, `PAYLOAD_BASE_DELAY = 1000`)

| # | state | condition to advance | action | elapsed |
|---|---|---|---|---|
| 1 | `PAYLOAD_NO_POWER` | `PP_good` = AMC +12 V sensor inside critical thresholds (MCH switched the payload) | `setDC_DC_ConvertersON(true)` (CONTROL high), clear hotswap backend-power bits, enable sensor scanning | t0 |
| 2 | `PAYLOAD_POWER_GOOD_WAIT` | `DCDC_good` = IO +3.3 V inside thresholds | FMC0: `3V3AUX_EN` + `FMC0_VADJ` on, wait **1 s**; FMC1 same, wait **1 s**; log the VADJ actually programmed; `rtm_payload_up(true)` (the RTM manager powers/identifies Zone 3 in parallel); green LED; `config_jtag()` | t0 + 2 s |
| 3 | `PAYLOAD_STATE_CLK_SETUP` | `payload_clock_configure()` OK | apply the EEPROM clock store (15 records, 1052 registers, ~1.2 s) or the compiled defaults, +100 ms PLL lock; on failure stays here | + ~1.3 s |
| 4 | `PAYLOAD_STATE_FPGA_SETUP` | `FPGA_DONE` high (timeout **100 s** → warn + red LED blink) | `fpga_soft_reset()` (pulse `FPGA_RESET_B`, blue blink) | — |
| 5 | `PAYLOAD_FPGA_ON` | — | monitors `PP_good`/`DCDC_good`; payload is "up" | — |

Typical boot log:

```
[   0.058] pwr  : main power on
[   1.058] pwr  : FMC0 power on
[   2.063] pwr  : FMC1 power on
[   3.066] jtag : AMC=1 RTM=0 FMC0=0 FMC1=0
[   4.645] clk  : apply from EEPROM preset: 15 records, 1052 registers (1106 ms)
[   4.756] clk  : PLL0 locked, PLL1 locked
[   4.760] boot : payload power-up done
```

`rtm : RTM detected, 12 V in 3 s` and `rtm : RTM 12 V on (identify in 5 s, handle
monitor in 10 s)` come from the RTM manager task, in parallel with the clock and
FPGA states — they no longer delay the payload sequence.

### 4.3 Power-down, quiesce and reset

- **Quiesce** (MCH `shutdown`/`fru … shutdown`, AMC handle opened): the
  payload task receives `PAYLOAD_MESSAGE_QUIESCE`, disables sensor scanning
  immediately (otherwise every rail produces a lower-critical event), and goes
  to `PAYLOAD_SWITCHING_OFF`.
- `PAYLOAD_SWITCHING_OFF`: `rtm_payload_up(false)` (the RTM manager cuts its
  rail) → FMC VADJ off → FMC 3.3 V aux off → `setDC_DC_ConvertersON(false)`
  (CONTROL low) → hotswap QUIESCED event → `PAYLOAD_QUIESCED`; when `PP_good`
  drops (MCH removed the 12 V) it returns to `PAYLOAD_NO_POWER`.
- `PAYLOAD_RESET` (payload reset request): same rails-off path, straight to
  `PAYLOAD_NO_POWER`.
- A **payload-only power cycle** (MCH `shutdown` + `fru_start`) keeps the MMC
  running: the log says `pwr : payload restart (MMC uptime N s, no MMC reset)`.
- An **MMC reset** does not power-cycle the payload; the MCH owns the shelf
  12 V.  That is why a module whose MMC is wedged can still be powered.

### 4.4 Monitoring, protection and the sensor model

- The **UCD poll task reads all 15 rails every 5 s** (V, I, T per channel) and
  fills the SDR sensors; `ucd_data[]` in `ucd90xxx.c` is the single mapping from
  SDR entry → PMBus page/command.  Thresholds come from the SDR records, so
  changing a limit means touching `sdr_list.c`, not the driver.
- The payload state machine does **not** read PMBus directly for its decisions:
  `payload_check_pgood()` looks at the `AMC +12V` sensor value and
  `dcdc_check_pgood()` at `IO +3.3V`, both against their SDR critical
  thresholds.
- `payload_sensors_scan(false/true)` disables/enables all non-hotswap sensors
  while rails are down — keep it that way when adding sensors.
- The uRTM-only UCD fault inputs (`RTM_12V0_FLT_B`, `RTM_MP_FLT_B`) are checked
  by the RTM manager and cut its 12 V (uRTM only, see §7); the FMC fault GPIs
  are not acted on by the MMC.
- The **hotswap sensor** reports the AMC M-state to the MCH; handle events and
  MCH commands arrive as payload messages.  The MMC never powers the shelf.
- Watchdog (2 s) is fed unconditionally today; a hung task does not trip it
  (`docs/TODO-u4fcp-v2.md` wants task heartbeats before feeding).

### 4.5 Debugging power problems

1. `ipmitool … sdr list` — are `AMC +12V`, `IO +3.3V`, FPGA/DDR rails sane?
   `Get Sensor Reading` of `HOTSWAP AMC` tells the M-state.
2. `ipmitool … raw 0x32 0x20` (generic I²C transfer, I2C2 ch7 → 0x11) or the
   Toolbox *I²C transfer* tab: PMBus `PAGE` (`0x00`), `OPERATION` (`0x01`,
   `0x80` = on, `0x40` = soft off), `READ_VOUT` (`0x8B`), `STATUS_WORD`
   (`0x79`), `CLEAR_FAULTS` (`0x03`).  Details in `docs/UCD90320_NOTES.md`
   (written for uFC-v2, same command set).
3. UART log: `pwr`/`clk`/`jtag`/`boot`/`rtm` tags, the boot log above.
4. If nothing sequences: check `GPIO_PMBUS_CTRL` (UCD CONTROL), then whether
   the MCH has the slot at M4 (`HS 0xx AMC` sensor) and how much power the FRU's
   module-current record asks for (`fru_program.sh --curr`).

---

## 5. Clock subsystem (config lives in the EEPROM)

- ADN4604 (0x48) crossbar + Si5345 #0/#1 (0x68/0x69).  The store lives in the
  CAT24C512: pointer page `0x200`, slot A `0x300`, slot B `0x1B00`, 6 KB each,
  FRU multirecord format with A/B atomic updates (`docs/CLOCK_CONFIG_FORMAT.md`).
- Applied in `PAYLOAD_STATE_CLK_SETUP` on **every** payload power-up; a blank
  store falls back to the compiled defaults (`default_0/1-Registers.h`, packed,
  see §2).
- Host side: `clk_program.sh --apply`, `program_clock.py`, `build_clk_preset.py`,
  commands `0x16/0x17` (program/status) and `0x18/0x19/0x1A/0x1B/0x1C`
  (store status/apply/begin/block/finish), plus the Toolbox Clock tab.
- The shipped preset has all ADN4604 outputs disabled (input 8 selected) — the
  real routing is still an open item (`docs/TODO-u4fcp-v2.md`).

---

## 6. IPMI surface

Standard: Device ID, Get Device SDR Info / Reserve / Get SDR / Get Sensor
Reading / thresholds, FRU inventory (read + write, backed by the CAT24C512),
hot-swap sensor, SEL, watchdog.

OEM, NetFn **0x32** (`modules/ipmi.h`):

| cmd | meaning |
|---|---|
| `0x01` | MMC free heap |
| `0x02` | 20-byte git SHA1 of the running build (**use this to verify what is flashed**) |
| `0x12/0x13` | EEPROM write/read by word offset (FRU EEPROM) |
| `0x14/0x15` | EEPROM write/read with explicit I²C address (probes first) |
| `0x16/0x17` | clock programming start / status |
| `0x18/0x19` | clock store status / apply |
| `0x1A/0x1B/0x1C` | clock store begin / block / finish |
| `0x20` | generic I²C transfer (up to 3 mux stages) |

NetFn **0x30**: `0x00` I²C transfer, `0x04` GPIO (mode 0 = port read).

HPM.1 on NetFn **0x2C** (group extension `0x00`): components **0 = bootloader,
1 = MMC, 2 = payload**; commands `0x2E` capabilities, `0x2F` properties, `0x31`
initiate, `0x32` upload block (**≤ 20 payload bytes** over IPMB), `0x33`
finish, `0x35` activate.

Sensors (AMC block, sensor numbers `0x01–0x1A`): hot-swap, AMC +12 V, the
FPGA/DDR/IO voltage–current pairs, TEMP UCD, FMC0/FMC1 currents and VADJ
voltages — 26 records.  With a uRTM v2.3 plugged, nine more
(`0x1B–0x23`, entity `0xC0` instance 0) are inserted dynamically; see §7.

---

## 7. RTM support (two boards, one AMC)

Zone 3 may carry either the **uRTM v2.3** (UCD90124A monitor, own MMC, FRU
0x55 behind `0x71 ch7`) or the **RTM Carrier for LattePanda Mu v1** (x86
carrier, own MMC on the RTM bus, FRU 0x55 directly on `0x70 ch5`, no UCD).

Rules (all of them in the RTM manager task, `rtm.c` + `rtm_fru.c` +
`rtm_sensors.c`; `payload.c` only signals that the AMC rails are up):

1. **The RTM bus is never touched while the module is unpowered** — an
   unpowered module clamps SDA/SCL through its ESD diodes and wedges I2C2
   (including the UCD writes that switch the rail on).  This was a real failure
   mode; see the commit `5336217` message.
2. **The 12 V follows RTM_PS#** (PA8, low = plugged).  The RTM manager powers
   it as soon as the payload rails are up (`rtm_payload_up(true)`) and on every
   RTM_PS# high→low edge; the payload state machine has no RTM state.
   Delay: `RTM_TURNON_DELAY_MS` = 3 s for every RTM (uRTM inrush protection).
   Unplugging cuts the 12 V.  `RTM_RES` (PA2) is asserted while the module is
   unpowered and released once the rail is up — an RTM that is never released
   stays in reset and never enables its own supplies.
3. **Identification** reads the FRU at 0x55 (uRTM path first, then the bare RTM
   bus), checks the common-header checksum and the product area: product
   `uRTM`, version `2.3`.  It starts `RTM_FRU_SETTLE_MS` (5 s) after the rail is
   up, gets `RTM_FRU_PROBE_TRIES` (3) attempts, then a module that stays silent
   is **left unmanaged** (no further RTM bus access) until it is re-plugged.
   The result is logged once (`rtm : FRU 0x55: uRTM v2.3` /
   `other RTM, no FMC monitors` / `no answer, RTM left unmanaged`).
4. **RTM SDR records only for an identified uRTM v2.3** (`0x1B–0x23`, nine
   monitors from the UCD90124A at 0x5B: UCD temperature, FMC2/FMC3 12 V, 3V3
   and VADJ currents, VADJ voltages).  Removed again if the module turns out to
   be another board.
5. **`RTM_INT#` (handle) and the uRTM-only 12 V/MP fault pins act only on a
   positively identified uRTM v2.3**; for anything else RTM_PS# alone decides.

### 7.1 The RTM 12 V rail (u4FCP-v2 hardware path)

```
12V0 ─► U70 TPS24750 (EN ◄ RTM_12V0_EN ◄ UCD EN3)
                       (FLTb ─► UCD MAR15, PGb ─► UCD DMON7, IMON ─► UCD AMON10)
     ─► 12V0_RTM ─► Zone-3 A1/B1
MP_3V3 ─► FPF2109 (ON ◄ RTM_MP3V3_EN ◄ UCD LGPO14) ─► MP_RTM ─► module EEPROM/expander
```

- The rail is **UCD Rail #2 = PMBus page 1** (`UCD_RTM_12V_CURR_ID`), per
  `u4FCPv2/template/u4FCPv2/POWER/u4FCPv2_with_current_limitation.tifsp`:
  `ON_OFF_CONFIG` = **CONTROL and OPERATION**, enable pin **L10 EN03**,
  `IOUT_OC` 4.5 A, all fault responses `Ignore`.  `OPERATION = 0x80` is the
  correct way to switch it (GPIO_PMBUS_CTRL / the UCD CONTROL pin is only the
  AMC's own master enable).
- **The rail has no voltage sense and no PG monitor** (`READ_VOUT` and `DMON7`
  read nothing useful).  Only the current monitor (AMON10) is real — do not
  conclude "rail is dead" from `READ_VOUT = 0`.
- Check EN3 from the host (verified on AMC6): select the pin with
  `raw 0x32 0x20 0x01 0x01 0x70 0x07 0x11 0x01 0xFA 0x1A 0x00`, then read
  `0xFB`.  Bit 3 (`0x08`) is the pin level: high while `OPERATION` page 1 is
  `0x80`, low for `0x00`/`0x40`.  UCD pin indices: `0-23` MAR1-24, `24-54`
  EN1-31, `55-70` LGPO1-16, `71-78` DMON1-8.
- **The rail itself can be checked from the host**: `DMON7` (pin 78) is wired
  to the hotswap's `PGb` (`RTM_12V0_nPG`) but is *not assigned* in the UCD
  project, so it reads 0 by default.  Enable it as an input first
  (`raw 0x32 0x20 … 0xFA 0x4E 0x00` then `0xFB 0x08 0x00`), then read `0xFB`:
  bit 3 = 1 while `OPERATION` page 1 is 0x00 and **0 (power good) while it is
  0x80** — measured on AMC6, 2026-11-15.  That proves the 12 V reaches the
  connector without a DMM.
- Carrier power chain (`RTM_Carrier_for_LattePanda_Mu_v1.pdf`, sheets 1/12):
  `12V0_RTM` -> `U28 TPS2121RUXR` power mux (`IN1`/`IN2` with `OV1`/`OV2`
  thresholds, `PR1` priority) -> carrier `12V0` -> LattePanda Mu and the
  `LTM4671`.  The LTM4671 `RUN_3V3`/`RUN_3V3SSD`/`RUN_5V0` come from the
  module's own **`POW_EN`** and `RUN_0V9` from **`POW_0V9_EN`** (0 Ω links), so
  the carrier's 3V3 only appears after the module starts.  If the module never
  starts, nothing on the carrier draws: check the mux output, the OV dividers
  on `OV1`/`OV2` against the actual rail (this shelf's 12 V reads 13.1 V on the
  UCD, ~9 % high) and the module's power button.
- If EN3 toggles and `PGb` reports good but the rail current stays at 0 A, the
  MMC's job is done: the module is not drawing.  Suspects are then the
  module's own power path (TPS2121 source select, LTM4671 `RUN_3V3` chain,
  LattePanda power button).  For the u4FCP side the parts are U70
  (TPS24750RUV: `EN` 33, `GATE` 25, `OV` 1, `FLTb` 30, `IMON` 2, `OUT`
  6-13/19/20, sense R254/R82 2 mΩ).
- Remote cross-check that the module really draws nothing: `show_pm` on the MCH
  console gives the per-slot current.  AMC6 with the LattePanda carrier and
  AMC1 without any RTM both sit at **4.9 A**, so the RTM contributes ~0 even
  with EN3 high — the rail is not delivering (measured 2026-11-15, AMC6).

---

## 8. Host access and tooling

Through the MCH (NAT-MCH 2.23 at 192.168.1.41):

```sh
B="-I lan -H 192.168.1.41 -A none -T 0x82 -B 0 -t 0x88 -b 0"   # AMC12
ipmitool $B mc info
ipmitool $B sdr list
ipmitool $B fru print
ipmitool $B raw 0x32 0x02          # running git SHA1
ipmitool $B raw 0x32 0x18          # clock store status
```

MCH quirks that bite:

- it **caches the module SDR repository** and only re-reads it on a slot
  activation/hot-swap — after adding/removing sensors re-activate the module;
- the **FRU is read once at boot** by the MMC — reboot after programming it;
- the SEL is RAM-only and its clock is unset (entries show 1970);
- `ipmitool hpm upgrade` cannot be used through the double bridge (block size
  from the LAN MTU); use `tools/uFC_toolbox/hpm_upload.py` (20-byte blocks).

Tooling in `tools/firmware/u4fcp-v2/`:

| script | purpose |
|---|---|
| `program.sh` | build + flash + FRU + clock preset + VADJ, `--sn` last; `--rtm` also programs the uRTM FRU |
| `jlink_program.sh` | newboot + app over SWD, clears `fw_info` |
| `fru_program.sh` / `program_fru.py` | FRU from `fru_template.bin`, serial + mfg time, `--curr A` sets the PICMG module current (0.1 A steps) |
| `fru_template.py` | edit/generate/verify/dump the FRU template (`fru_template.txt`) |
| `clk_program.sh`, `program_clock.py` | write/apply the clock store |
| `vadj_program.sh`, `program_vadj.py` | FMC0/FMC1 VADJ |
| `rtm_fru.py` | read/verify/write the uRTM FRU (0x55 behind `0x70 ch5 → 0x71 ch7`) |
| `build_clk_preset.py` | rebuild `clk-preset.bin` from the compiled clocking |
| `rtm_power.py`, `rtm_scan.py`, `rtm_dump.py` | RTM debug helpers |

Host GUI: `tools/uFC_toolbox/` (Tkinter, PyInstaller; needs `ipmitool` at
runtime; `hpm_upload.py` in the same directory does OTA).

Build:

```sh
cmake -S . -B .hw2/u4fcp -DBOARD=u4fcp-v2 \
      -DTARGET_CONTROLLER=STM32F103CBT6 -DCMAKE_BUILD_TYPE=Release
make -C .hw2/u4fcp newboot -j      # 4472 B
make -C .hw2/u4fcp openMMC -j      # ~60 KB
make -C .hw2/u4fcp full_binary -j  # + out/openMMC_full.bin
```

Options: `EEPROM_ACCESS`, `I2C_ACCESS`, `CLOCK_PROGRAM`, `CLOCK_STORE`,
`RTM_SENSORS_BOOT_DUMP`, `DISABLE_WATCHDOG`, `FRU_USE_COMPILED`.

**Always build the image from a clean tree** before flashing/OTA — a
long-lived build directory once produced an image 608 B larger than a clean
build of the same commit, and that image did not boot (see
`docs/testing/INCIDENT-2026-11-15-amc1-ota.md`).

---

## 9. Testing assets and what has been verified

- `docs/testing/REGRESSION.md` — regression matrix; `TEST_REPORT-*.md`,
  `SENSOR_STRESS-*.md` — results.
- `tools/testing/sensor_stress.py` — hammers SDR/sensor reads through the MCH,
  checks SDR stability, bounds, hot-swap state and heap; writes a Markdown
  report.
- `tools/testing/board_selftest.py`, `test/emulators/` (I2C emulators for the
  EEPROM + mux, host-side unit tests), `RENODE_SIM` build option for Renode.
- Verified on hardware (AMC12 + uRTM v2.3, 2026-11-11): 35 sensors, RTM group
  dynamic insert/remove, clock store apply, VADJ, FRU/`--curr`, OTA of
  component 1.
- Verified on AMC6 (2026-11-15): LattePanda carrier — rail comes up, FPGA
  boots, 26 sensors, no RTM SDRs.
- Verified on AMC1 with `954c265` (no RTM): 26 sensors, no `FMC2/FMC3`, FRU
  and clock store intact.

Open items are tracked in `docs/TODO-u4fcp-v2.md` (watchdog supervision,
successful HPM end-to-end, real clock routing, ADN4604 termination, periodic
PLL monitor).

---

## 10. Current state (2026-11-15, end of session)

- Firmware feature tip: the RTM manager series on top of `5336217` ("power the
  module from RTM_PS# alone") — one task for Zone 3, `RTM_RES`, the SDR removal
  for a swapped module.  `git log --oneline origin/master..HEAD` is the
  authoritative list; rebuild and check `raw 0x32 0x02` against it.
- **AMC1 (SL03) is down**: an OTA of `c7d774f` built from a long-lived tree was
  activated and the MMC never answered again; a chassis power cycle did not
  help (the MMC runs from management power).  It needs a SWD reflash of a clean
  image — `docs/testing/INCIDENT-2026-11-15-amc1-ota.md`, recovery image in
  `.hw2/recovery/`.  AMC12 is healthy and runs `8dc4fdb`.

---

## 11. Practical advice for continuing

- Read `docs/RTM_SENSORS_PLAN.md` before touching RTM code — it records why the
  design looks the way it does (bus wedge, inrush delays, SDR numbering, MCH
  caching).
- Change the firmware in small steps, always build both targets and check the
  size; the linker `ASSERT` protects `.data`, but keep ≥ 300 B of headroom.
- Verify on hardware with `raw 0x32 0x02` (SHA1 of the build), `sdr list`,
  `fru print`, and the UART log; the MCH caches SDRs, so re-activate a slot
  after changing the sensor set.
- Prefer SWD for a board you cannot physically reach: an OTA that goes wrong
  leaves the module dark with no remote recovery (see the incident).
- Commit style: `rtm:`, `payload:`, `toolbox:`, `tools:`, `docs:` prefixes, one
  topic per commit, body explaining *why* (hardware evidence welcome).
