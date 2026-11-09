# openMMC — uFC‑v2 build guide

Firmware build and flashing notes for the **uFC‑v2** MMC
(STM32F100CBT6, Cortex‑M3 value line, 8 KB SRAM).

---

## 1. Prerequisites

| Tool | Version used / notes |
|---|---|
| `arm-none-eabi-gcc` | 14.2.1 (`gcc-arm-none-eabi`) |
| `cmake` | 4.2.3 (project requires ≥ 3.10) |
| `make` | GNU make |
| `openocd` or `st-flash` | only for programming/debugging |

The top-level `CMakeLists.txt` declares `cmake_minimum_required(VERSION 3.10)`,
so CMake 4.x configures without any extra flags (the old
`-DCMAKE_POLICY_VERSION_MINIMUM=3.5` workaround is no longer needed).

---

## 2. Configure

Out‑of‑source build, one directory per board:

```bash
cd openMMC_uFC
mkdir -p build && cd build

cmake .. \
  -DBOARD=ufc-v2
```

Notes:

- `-DBOARD=ufc-v2` selects `port/board/ufc-v2/`.
- The board revision is part of the board directory; no `-DVERSION` flag is used
  for the uFC family.
- Debug build: add `-DCMAKE_BUILD_TYPE=Debug`.
- Debug console: add `-DUART_DEBUG=ON` to route `printf` to USART1
  (PA9/PA10, 115200).
- Watchdog is disabled by default (`DISABLE_WATCHDOG=ON`); enable with
  `-DDISABLE_WATCHDOG=OFF`.
- The FRU can be changed **online** over IPMI with the normal image; no special
  build option is required (see §5).

---

## 3. Build

```bash
make -j
```

`make` re-runs CMake automatically if any `CMakeLists.txt` changed.
Use `make clean`, or delete the build directory, if anything looks stale.

---

## 4. Outputs (`build/out/`)

| File | Description | Flash address |
|---|---|---|
| `openMMC.elf` / `.bin` / `.srec` | application firmware | `0x08002000` |
| `newboot.elf` / `.bin` / `.srec` | bootloader (`newboot`) | `0x08002000` |
| `STM32F100CBT6_app.map` | application linker map | — |

---

## 5. Memory budget (uFC‑v2, release, `-Os`)

| Region | Used | Limit |
|---|---|---|
| Flash (`text + data`) | 21,664 B | 51,200 B (50 KB app region) |
| RAM (`.data` 200 + heap 5,632 + `.bss` 808 + stack reserve 512) | 7,152 B | 8,192 B |
| RAM free | 1,040 B | — |

The uFC‑v2 release build is forced to `-Os -ffunction-sections -fdata-sections`
(`port/board/CMakeLists.txt`). `ucHeap` is in `.noinit_RAM2`, a `NOLOAD` section,
so it costs no flash. `_Min_Heap_Size` is 0 (the C-library heap is unused; all
allocation goes through `pvPortMalloc`).

**Renode verification (1.17, STM32F103 platform + GDB):** boot reaches
`vTaskStartScheduler` with **1,224 B** heap free, creates all **5 tasks + the
idle task**, and `vTaskPayload` runs with **736 B** heap free. The platform has
no RCC model, so `SystemInit()` was skipped under GDB (emulator limitation, not
a firmware issue).

### Feature pruning (uFC‑v2)

- `FRU_NO_BUILDER` — the FRU image builder (`fru_editor.c`, `amc_fru.c`) is not
  compiled, but the storage read/write handlers stay, so the FRU can be changed
  **online** over IPMI. `-DFRU_READ_ONLY=ON` additionally removes the write path.
- `DISABLE_DIAGNOSTIC_CMDS` — the custom "get free heap" IPMI command is removed.
- FreeRTOS timers and the trace facility are disabled in the STM32 `FreeRTOSConfig.h`.
- HPM.1 is enabled for the MMC and bootloader components; the payload/FPGA
  component is stubbed with `IPMI_CC_ILLEGAL_COMMAND_DISABLED`.
- **LED task removed (uFC‑v2 only, `LED_NO_TASK`)** — `LEDUpdate()` drives the
  GPIO directly; the PICMG LED IPMI commands still work, but blink/lamp-test
  timing is not animated. Other boards keep the LED state machine.
- **Heap/task sizing (uFC‑v2 only)** — `UFC_V2` selects a 5,632 B heap and a
  96‑word idle stack in `FreeRTOSConfig.h`; `IPMB_*_TASK_STACK`,
  `IPMI_TASK_STACK` and `PAYLOAD_TASK_STACK` are overridden to 96/192 words
  (`task_priorities.h`). Other boards keep the 4 KB heap and original stacks.

### Online FRU update (no factory build)

The normal uFC‑v2 image keeps the storage read/write handlers but **not** the
FRU image builder (`FRU_NO_BUILDER`). The 24AA025E48 can therefore be written
at any time over IPMI while the production image is running.

Write the FRU from the MCH/host (target = the MMC's IPMB address, e.g.
`0x70 + 2*slot`; FRU ID 0 = AMC):

```bash
# Storage netfn 0x0A, Write FRU Data cmd 0x12 (ipmitool chunks it)
ipmitool -I lan -H <MCH> -A none -T 0x82 -m 0x20 -t <mmc_addr> \
  fru write 0 fru.bin

ipmitool ... fru read 0 fru.bin     # read back
ipmitool ... fru print 0            # human-readable
```

Raw form (one chunk per request; FRU ID, offset LSB, offset MSB, data…):

```bash
ipmitool ... raw 0x0A 0x12 0x00 0x00 0x00 <byte0> <byte1> ...
```

Notes:

- The first write to a blank EEPROM is allowed: `fru_size` starts at 0 and grows
  as data is written, capped at `FRU_MAX_SIZE=128`.
- Only `0x00–0x7F` is writable; the upper half holds the write‑protected EUI‑48.
  Keep the image ≤ 128 B (the uFC‑v2 template is 122 B).
- The builder is not in the image, so supply `fru.bin` from the host (read it
  back from a board that already has one, or generate it with a FRU tool).
- `-DFRU_READ_ONLY=ON` builds the smallest image with no FRU write path at all.


### FRU write/read test (Renode)

Build a test image:

```bash
cmake .. -DBOARD=ufc-v2 -DRENODE_TEST=ON
make -j
```

`RENODE_TEST` adds `modules/fru_test.c` and swaps the FRU EEPROM driver for a
RAM-backed one (`board_fru.c`). On boot, `fru_test_run()` (called after
`fru_init`) writes a 122-byte image through the real IPMI **Write FRU Data**
handler in 16-byte chunks, reads it back through the **Read FRU Data** handler,
checks `fru_size`, and verifies that a write past `FRU_MAX_SIZE` is rejected.
It sets `fru_test_result = 1` on success.

Run under Renode + GDB (STM32F103 platform, GDB server on 3333):

- load the ELF, set `SP` to `_estack` (`0x20002000`) and `PC` to `Reset_Handler`
  (address from `nm`);
- skip `SystemInit()` (Renode has no RCC model): set `VTOR = 0x08002000` and
  `PC = main`;
- break at `sdr_init` (the call after the test) and read `fru_test_result`
  (address from `nm`): `1` = pass.

Result for the current build: **PASS** (122 bytes written/read, size grown to
122, out-of-range write rejected).

The TCA9548A and 24AA025E48 protocol emulators (and their 13-case test suite)
live in `test/emulators/`; see `docs/datasheets/README.md`.

Limitation: this tests the IPMI handlers and the FRU size logic, not the I²C
transport — Renode 1.17 has no TCA9548/24AA025E48 models and its Python
peripheral does not implement I²C.


### Host-side `ipmitool` → MCH test (`ipmi_sim`)

This validates the `ipmitool` FRU command path (NetFn `0x0A`, cmd `0x12`/`0x11`)
against a simulated BMC/MCH. It does **not** involve the STM32 or the EEPROM.

The installed `/etc/ipmi/lan.conf` listens on `localhost:9001`; the
`ipmisim1.emu` file defines the BMC. For an FRU device 0 on the BMC, add
`mc_add_fru_data 0x20 0 128 data 0` and comment out the `lan_config_program`
line in the copied `lan.conf`.

```bash
ipmi_sim -c lan.conf -f ipmisim.emu -s state -n &
ipmitool -I lanplus -H 127.0.0.1 -p 9001 -U ipmiusr -P test -C 3 \
  fru write 0 fru.bin
ipmitool ... fru read 0 readback.bin
cmp -n 122 fru.bin readback.bin     # 122-byte prefix matches
```

Observed: `Fru Size 128 bytes`, `Size to Write 122 bytes`, write exit 0;
readback 128 bytes (122 written + 6 zero padding); 122-byte prefix matches.
The `Unknown FRU header version 0x00` warning is expected for a pattern image;
use a valid FRU image to exercise `fru print`.

This covers the host/MCH half. The STM32 FRU handler is covered by the
`RENODE_TEST` test above; the true MCH → I²C → STM32 → EEPROM chain needs
hardware.

---

## 6. Flash layout (STM32F100CBT6)

From `port/ucontroller/stm32/stm32f1xx/linker/STM32F100CBT6_*.ld`:

| Region | Start | End | Size | Contents |
|---|---|---|---|---|
| **newboot** (first stage) | `0x08000000` | `0x08001FFF` | 8 KB | do not erase/program |
| Bootloader (`newboot`) | `0x08002000` | `0x08003FFF` | 8 KB | VTOR `0x08002000` |
| Application (`openMMC`) | `0x08002000` | `0x0800E7FF` | 50 KB | VTOR `0x08002000` |
| FW update staging | `0x0800E800` | `0x0801AFFF` | 50 KB | new image |
| `fw_info` record | `0x0801FC00` | `0x0801FFFF` | 1 KB page (256 B record) | `version[3]`, `fw_type`, magic `0xAAAAAAAA` |

> **Flash size:** the layout reserves 128 KB, matching the STM32F100CBT6.
> The application (ends `0x080094A0`) and the update staging region
> (`0x0800E800–0x0801AFFF`), the clock region (`0x0801B000–0x0801FBFF`) and
> `fw_info` at `0x0801FC00` all fit.

Vector table word 0/1 (verified from the build; the low bit is the Thumb bit, symbols are `0x080026FC` / `0x080043DC`):

| Image | Initial SP | Reset_Handler |
|---|---|---|
| `newboot.bin` | `0x20002000` | `0x080026FD` |
| `openMMC.bin` | `0x20002000` | `0x080043DD` |

RAM (8 KB at `0x20000000`):

```
openMMC : .data 0x20000000-0x200000BF  ucHeap 0x200000C0-0x200016BF
          .bss  0x200016C0-0x200019E7  reserve 0x200019E8-0x20001BE7
          free  to 0x20002000 (1,040 B)
newboot : .data 0x20000000-0x20000003  .bss 0x20000004-0x20000023
          reserve 0x20000024-0x20000423
          free  to 0x20002000 (7,132 B)
```

Flash pages are 1 KB (`FLASH_PAGE_SIZE 0x400` on MD_VL).

⚠️ **The first 8 KB is newboot** (built from this tree). Never erase or
program `0x08000000–0x08001FFF` unless you are updating newboot: the STM32F1
resets through newboot there, which then starts the application at
`0x08002000`.

---

## 7. Flashing

### J-Link (bench)

```bash
JLinkExe -device STM32F100CB -if SWD -speed 1000 -CommanderScript flash.jlink
```

`flash.jlink`:

```
device STM32F100CB
si SWD
speed 1000
VTREF 3300
connect
loadfile newboot.bin 0x08002000
loadfile openMMC.bin 0x08002000
r
g
q
```

- `loadfile` erases only the sectors it writes; `0x08000000–0x08001FFF`
  (newboot) is untouched.
- If the probe reports `Target voltage too low`, set `VTREF 3300` (or wire the
  J-Link VTref pin to the board's 3.3 V + GND) and **power-cycle the J-Link** —
  the fixed VTref applies only after a probe power cycle.

### ST-Link / OpenOCD (alternative)

```bash
# ST-Link
st-flash --reset write build/out/newboot.bin 0x08002000
st-flash --reset write build/out/openMMC.bin 0x08002000

# OpenOCD
openocd -f interface/stlink.cfg -f target/stm32f1x.cfg \
  -c "init" -c "reset halt" \
  -c "program build/out/newboot.bin 0x08002000 verify" \
  -c "program build/out/openMMC.bin 0x08002000 verify" \
  -c "reset run" -c "shutdown"
```

**Do not pass `-DDEBUG_PROBE=...`** for uFC‑v2: `probe/openocd.cmake` only
knows `lpc17xx` and aborts configuration for STM32, so the
`program_boot` / `program_app` / `program_all` targets are unavailable.

> ⚠️ Keep `0x08000000–0x08001FFF` (newboot) intact unless updating it. Do not
> run a full-chip erase (`flash erase_sector 0 0 last`).

---

## 8. Board selection

| Command | Board | MCU |
|---|---|---|
| `-DBOARD=ufc-v2` | **ufc-v2** | STM32F100CBT6 |
| `-DBOARD=u4fcp-v2` | u4fcp-v2 (legacy) | STM32F103CBT6 |

Select the uFC board by its name; there is no `-DVERSION` flag for it.

Use a **separate build directory per board**: `BOARD` is cached.

---

## 9. Rebuilding after a code change

```bash
cd build
make -j
```

That is all that is normally needed. If you add/remove files or change CMake
options, re-run `cmake ..` with the same arguments (or let `make` do it).

---

## 10. Adding code

| Change | Where |
|---|---|
| New board source file | add to `port/board/ufc-v2/CMakeLists.txt` (`PROJ_SRCS`) |
| New optional module | add name to `TARGET_MODULES` in the board `CMakeLists.txt`, add an `if(";${TARGET_MODULES};" MATCHES ";NAME;")` block in `modules/CMakeLists.txt` that appends the source and `-DMODULE_NAME` |
| Pins / GPIO | `port/board/ufc-v2/pin_mapping.h` (+ `PIN_CFG_LIST`) |
| I²C buses/chips | `port/board/ufc-v2/i2c_mapping.c` / `.h` |
| FRU fields/records | `port/board/ufc-v2/user_amc_fru.h` (keep ≤ 128 B) |
| Payload state machine | `port/board/ufc-v2/payload.c` |
| MCU clock / heap | `port/ucontroller/stm32/stm32f1xx/` |

---

## 11. uFC‑v2 hardware summary

**I²C (I2C2, PB10/PB11):**

```
TCA9548 @0x70 (A2=A1=A0=0)
 ├─ ch0 : 24AA025E48 @0x50   FRU EEPROM (128 B writable)
 └─ ch2 : 8V54816    @0x58   clock switch
          SI5345A    @0x69   clock generator
```

**Pins:**

| Signal | Pin |
|---|---|
| LED blue / red / green | PB4 / PB5 / PB0 |
| GA_TEST / GA0 / GA1 / GA2 | PB3 / PA15 / PA12 / PA11 |
| `DESKTOP_EN` | PC13 |
| `AMC_MODULE_HANDLE` (hot‑swap) | PA0 |
| `PMBUS_ALERT_B` | PB8 |
| `FPGA_DONE` | PC15 |
| `FPGA_RST_B` | PA1 |
| `FPGA_PROG_B` | PC14 |

> PB3/PB4/PA15 are JTAG pins. `pin_init()` disables JTAG while keeping SWD
> (`GPIO_Remap_SWJ_JTAGDisable`). Do not enable full JTAG on this board.

**Modules compiled:** `SDR`, `PAYLOAD`, `HOTSWAP_SENSOR`, `EEPROM_24XX02`, `FRU`, `BOARD_CONFIG`.

---

## 12. Gotchas

- **`modules/GitSHA1.c` is regenerated on every configure.** This tree has no
  `.git`, so the embedded commit hash becomes `GIT-NOTFOUND` (and the IPMI
  commit‑hash readout returns that). Back the file up or `git init` the tree
  if the hash matters.
- **FRU:** the clock configuration record is intentionally omitted on uFC‑v2 to
  fit the 128‑byte EEPROM half. Set `AMC_FRU_INCLUDE_CLOCK_RECORD 1` only with a
  larger (≥256 B writable) FRU EEPROM.
- **8 KB SRAM:** adding modules/tasks can overflow RAM at link time. uFC‑v2 uses
  a 5,632 B heap (`UFC_V2` in `FreeRTOSConfig.h`) and 96/192‑word task stacks;
  reduce `configTOTAL_HEAP_SIZE` or trim modules if the link fails.
- The EEPROM driver writes in 8‑byte chunks, which is safe for the
  24AA025E48's 16‑byte pages.

---

## 13. Known limitations

- STM32 flashing is not integrated into `probe/openocd.cmake` (LPC17xx only).
- The old `BOARD=u -DVERSION=...` alias was removed; uFC boards are selected by
  name (`-DBOARD=ufc-v2`).
- The new `uRTM‑v2` board directory is not wired into `BOARD_RTM` selection and
  its `CMakeLists.txt` appends the wrong module variable
  (`RTM_8SFP_MODULES`).
- The `newboot` application is built here and occupies the first 8 KB (see §6).
