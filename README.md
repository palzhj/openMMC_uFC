# openMMC — AMC MMC firmware (uFC‑v2 · u4FCP‑v1 · u4FCP‑v2)

Open Source modular IPM Controller firmware, ported to the **uFC v2.0** and
**u4FCP v1/v2** AMC modules.

This tree is a **research fork** of
[lnls-dig/openMMC](https://github.com/lnls-dig/openMMC) targeting the STM32F1
family. Each board has its own port under `port/board/`; the low-level drivers
(`modules/`, `port/ucontroller/`) are shared and take the device addresses from
the board's mapping. The board ports, the newboot/EEPROM/mux fixes and the
FRU changes are modifications made in 2026 for scientific use, tracked in the
git history and noted where they are not obvious.

| board | MCU | flash / SRAM | board EEPROM | clock store | flash map (KB) | build |
|---|---|---|---|---|---|---|
| **uFC‑v2** | STM32F100CBT6 | 128 KB / 8 KB | 24AA025E48, 2 KB @0x50 | flash 19 KB | 8 / 50 / 50 / 19 / 1 | `-DBOARD=ufc-v2` |
| **u4FCP‑v1** | STM32F103C8T6 | 64 KB / 20 KB | AT24CM01, 128 KB @0x54 | EEPROM | 8 / 55 / — / 1 | `-DBOARD=u4fcp-v1` |
| **u4FCP‑v2** | STM32F103CBT6 | 128 KB / 20 KB | CAT24C512, 64 KB @0x54 | EEPROM | 8 / 59.5 / 59.5 / 1 | `-DBOARD=u4fcp-v2` |

Common to all boards: debug UART on USART1 (PA9/PA10, 115200 8N1), the
clock/FRU I²C bus behind a TCA9548A mux, SWD on PA13/PA14. The u4FCP
adds an ADN4604 crosspoint + up to two Si5345 PLLs and keeps both the FRU
and the clock configuration in the EEPROM; the uFC uses an 8V54816A
switch + one Si5345 and stores the clock configuration in flash.

## Table of Contents
- [openMMC — AMC MMC firmware](#openmmc--amc-mmc-firmware-ufcv2--u4fcpv1--u4fcpv2)
   * [Memory map](#memory-map)
   * [Installation](#installation)
   * [Compilation](#compilation)
   * [Programming](#programming)
      + [Firmware update over IPMI (HPM.1 OTA)](#firmware-update-over-ipmi-hpm1-ota)
      + [newboot / application over SWD (J-Link)](#newboot--application-over-swd-j-link)
   * [Debugging](#debugging)
   * [FRU information](#fru-information)
   * [IPMI custom commands](#ipmi-custom-commands)
   * [Documentation](#documentation)
   * [License](#license)

## Memory map

| Region | Address | Size | Notes |
|---|---|---|---|
| newboot (first stage) | `0x08000000` | 8 KB | boots straight from the reset vector, applies staged updates, jumps to the app |
| Application | `0x08002000` | 50 KB | openMMC |
| Firmware staging | `0x0800E800` | 50 KB | HPM.1 upload target |
| Clock configuration | `0x0801B000` | 19 KB | 1 KB pointer + 9 KB slot A + 9 KB slot B (FRU records) |
| `fw_info` | `0x0801FC00` | 1 KB | pending‑update flag (`0xAA…`) used by newboot |

This is the **uFC‑v2** layout (STM32F100CBT6, clock store in flash). The
u4FCP boards keep the clock configuration in their EEPROM instead, which
frees the flash clock region:

| board | flash map | clock store in |
|---|---|---|
| uFC‑v2 (F100CBT6) | 8 / 50 / 50 / **19** / 1 KB | flash `0x0801B000` |
| u4FCP‑v1 (F103C8T6, 64 KB) | 8 / 55 / — / 1 KB | AT24CM01 EEPROM (same offsets); staging in the EEPROM upper 64 KB |
| u4FCP‑v2 (F103CBT6) | 8 / **59.5** / **59.5** / 1 KB | CAT24C512 EEPROM (0x200 pointer, slots 0x300/0x1B00, 6 KB each) |

Build any board with `-DBOARD=<ufc-v2|u4fcp-v1|u4fcp-v2>` and the matching
`-DTARGET_CONTROLLER` (F100CBT6 / F103C8T6 / F103CBT6).

The clock region is used by the **clock configuration store** (`-DCLOCK_STORE=ON`):
a 1 KB pointer page plus 9 KB A/B FRU-format slots; the MMC applies the valid
slot at power‑on (switch matrix + Si5345 table) and the toolbox can upload a
new slot over IPMI (`0x1A/0x1B/0x1C`, status `0x18`, apply `0x19`). See
[docs/CLOCK_CONFIG_FORMAT.md](docs/CLOCK_CONFIG_FORMAT.md).

The CPU starts **newboot** directly from `0x08000000`
and no vector‑table checksum is involved.

## Installation

Packages needed to build:

- **gcc-arm-none-eabi**
- **cmake**

```sh
sudo apt install gcc-arm-none-eabi cmake
```

## Compilation

Configure out of the source tree (requires **CMake ≥ 3.10**; CMake 4.x works as-is):

uFC‑v2:

```sh
cmake -S . -B build-ufc \
      -DBOARD=ufc-v2 \
      -DTARGET_CONTROLLER=STM32F100CBT6 \
      -DUART_DEBUG=ON \
      -DCMAKE_BUILD_TYPE=Debug
cmake --build build-ufc -j
```

u4FCP‑v1 / u4FCP‑v2:

```sh
cmake -S . -B build-u4fcp-v1 \
      -DBOARD=u4fcp-v1 -DTARGET_CONTROLLER=STM32F103C8T6 \
      -DOPTIMIZE_SIZE=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-u4fcp-v1 -j

cmake -S . -B build-u4fcp-v2 \
      -DBOARD=u4fcp-v2 -DTARGET_CONTROLLER=STM32F103CBT6 \
      -DCMAKE_BUILD_TYPE=Release
cmake --build build-u4fcp-v2 -j
```

Options:

| Option | Meaning |
|---|---|
| `-DBOARD=ufc-v2\|u4fcp-v1\|u4fcp-v2` | board port under `port/board/` |
| `-DTARGET_CONTROLLER=STM32F100CBT6\|STM32F103C8T6\|STM32F103CBT6` | MCU and linker scripts |
| `-DOPTIMIZE_SIZE=ON` | compile with `-Os` (needed for the 64 KB v1) |
| `-DUART_DEBUG=ON` | printf console on USART1 |
| `-DFRU_BUILD=ON` | one‑time FRU builder + EEPROM write (see [FRU](#fru-information)) |
| `-DDISABLE_WATCHDOG=OFF` | enable the IWDG (default in CMake is *disabled*, to aid debugging) |

Outputs: `build-ufc/out/openMMC.{elf,bin,srec}` and `newboot.{elf,bin,srec}`.
The image embeds the git tag and commit (`Version:` / `SHA1:` on the console).

## Programming

### Firmware update over IPMI (HPM.1 OTA)

Over IPMB through a NAT‑MCH (double‑bridged, see the NAT‑MCH manual Appendix B.3):

```sh
ipmitool -I lan -H <mch-ip> -A none -T 0x82 -B 0 -t 0x7e -b 7 \
         hpm upgrade openMMC.hpm
ipmitool -I lan -H <mch-ip> -A none -T 0x82 -B 0 -t 0x7e -b 7 \
         hpm activate
```

The HPM image is built from the application binary with `bin2hpm`:

```sh
bin2hpm -c 1 -n -m 0x315A -p 0x0 -d 0 out/openMMC.bin -o out/openMMC.hpm
```

`hpm upgrade` stages the image, `hpm activate` writes the `fw_info` record and
resets the MMC; newboot then copies the staging area into the application and
clears the flag. Both steps are fully automatic.

### newboot / application over SWD (J-Link)

The repository ships a J-Link script per board that loads newboot and the
application (same addresses, only the `device` line differs):

```sh
cd tools/firmware/ufc-v2    && JLinkExe -CommanderScript flash.jlink   # STM32F100CB
cd tools/firmware/u4fcp-v1  && JLinkExe -CommanderScript flash.jlink   # STM32F103C8 (64 KB)
cd tools/firmware/u4fcp-v2  && JLinkExe -CommanderScript flash.jlink   # STM32F103CB
```

It is equivalent to:

```
device STM32F100CB
si SWD
speed 1000
VTREF 3300
connect
loadfile uart-debug/newboot.bin 0x08000000
loadfile uart-debug/openMMC.bin 0x08002000
r
g
q
```

`VTREF 3300` only takes effect after a J-Link power cycle (or with the J-Link
VTref pin wired to the board's 3.3 V).

## Debugging

SWD with OpenOCD and GDB (ST‑Link, J‑Link or CMSIS‑DAP):

```sh
openocd -f interface/jlink.cfg -f target/stm32f1x.cfg
```

```sh
arm-none-eabi-gdb out/openMMC.elf
(gdb) target remote localhost:3333
(gdb) monitor reset halt
(gdb) load
(gdb) monitor reset run
```

The board keeps SWD only — PB3/PB4/PA15 are used as GPIO, so do not enable full
JTAG.

## FRU information

The FRU EEPROM is a **24AA025E48** at I2C `0x50` (TCA9548A channel 0), 128
writable bytes. The uFC has **no Zone 3** connector, so the FRU contains only:

- board info area — IHEP / uFC / SN `H01` / PN `HEPS`
- product info area — IHEP / uFC / AMC / version `2.0`
- multirecord area — AMC module current (5.4 A) and AMC point‑to‑point record

That is **104 bytes**, well inside the 128‑byte writable half.

Production images read the FRU and keep the online IPMI write path
(`FRU_NO_BUILDER`). To (re)program it, build with `-DFRU_BUILD=ON` and boot
once — the builder writes the FRU and verifies it:

```
>FRU_WRITE_EEPROM flag enabled! Building FRU info...
>AMC FRU total size: 104 bytes
[FRU][AMC] FRU info is healthy!
```

## IPMI custom commands

### Get free heap memory

Returns the free FreeRTOS heap in bytes (u32, little‑endian):

```sh
ipmitool -I lan -H <mch> -A none -T 0x82 -B 0 -t 0x7e -b 7 raw 0x32 0x01
```

### Commit hash read

Returns the 20‑byte git SHA‑1 embedded at build time:

```sh
ipmitool -I lan -H <mch> -A none -T 0x82 -B 0 -t 0x7e -b 7 raw 0x32 0x02
```

### Raw EEPROM access (opt‑in, `-DEEPROM_ACCESS=ON`)

Write / read the FRU EEPROM (24AA025E48, 256 bytes, mux channel 0):

```sh
# read:  [addr_lo addr_hi length] -> data
ipmitool ... raw 0x32 0x13 0x00 0x00 0x08

# write: [addr_lo addr_hi data...]   (max 30 data bytes)
ipmitool ... raw 0x32 0x12 0x70 0x00 0x5A 0xA5
```

`length` is 1…32 bytes; the address range is checked against the 256‑byte
EEPROM. Writes are raw — they can corrupt the FRU, use with care.

Clock switch configuration is **not used** on the uFC‑v2: the 8V54816 and
SI5345A clock devices are mapped on the I2C mux but the board firmware does not
program a clock crosspoint.

## Documentation

| Document | Contents |
|---|---|
| [docs/USER_MANUAL.md](docs/USER_MANUAL.md) | Hardware, build, flashing, FRU, IPMI, troubleshooting, recovery |
| [docs/I2C_ACCESS.md](docs/I2C_ACCESS.md) | Manual for the universal I²C access over IPMI (EEPROM, PMBus, clocks, mux chains) |
| [docs/CLOCK_SWITCH.md](docs/CLOCK_SWITCH.md) | uFC 8V54816A clock-switch port map (from the schematic) and register format |
| [docs/DEBUGGING.md](docs/DEBUGGING.md) | Bring-up log: every failure, root cause and fix |
| [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) | Third-party components and their licenses |

## License

This project is distributed under the **GNU General Public License v3.0 or
later** — see [LICENSE](LICENSE).

It is a **research fork** of
[lnls-dig/openMMC](https://github.com/lnls-dig/openMMC); the uFC‑v2, u4FCP‑v1
and u4FCP‑v2 board ports (`port/board/ufc-v2/`, `port/board/u4fcp-v1/`,
`port/board/u4fcp-v2/`), the newboot/EEPROM/mux fixes, the FRU changes and the
clock‑configuration store (ADN4604/Si5345, flash or board EEPROM) are
modifications made in 2026 for scientific use. Modifications are tracked in the
git history and noted where they are not obvious.

Bundled third‑party components (FreeRTOS, STM32 StdPeriph, CMSIS) keep their own
licenses — see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). The first‑stage bootloader (newboot) is part of this repository.
