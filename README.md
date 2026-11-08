# openMMC — uFC‑v2 (STM32F100CBT6)

Open Source modular IPM Controller firmware, ported to the **uFC v2.0** AMC
module.

This tree is a fork of [lnls-dig/openMMC](https://github.com/lnls-dig/openMMC)
targeting the STM32F100 value‑line MCU. All board support for the uFC‑v2 is
under `port/board/ufc-v2/`.

| Item | Value |
|---|---|
| MCU | **STM32F100CBT6** — Cortex‑M3 value line, 128 KB flash, 8 KB SRAM |
| Board | uFC v2.0 AMC module |
| Debug UART | USART1 (PA9/PA10), 115200 8N1 |
| I2C | I2C2 (PB10/PB11), 100 kHz, through a TCA9548A mux |
| SWD | PA13/PA14 (JTAG disabled, SWD kept) |

## Table of Contents
- [openMMC — uFC‑v2 (STM32F100CBT6)](#openmmc--ufcv2-stm32f100cbt6)
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
| *(unused)* | `0x08002000` | 8 KB | free (previously newboot while OpenBLT was used) |
| Application | `0x08004000` | 56 KB | openMMC |
| Firmware staging | `0x08012000` | 56 KB | HPM.1 upload target |
| `fw_info` | `0x0801FF00` | 256 B | pending‑update flag (`0xAA…`) used by newboot |

OpenBLT has been dropped: the CPU starts **newboot** directly from `0x08000000`
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

```sh
cmake -S . -B build-ufc \
      -DBOARD=ufc-v2 \
      -DTARGET_CONTROLLER=STM32F100CBT6 \
      -DUART_DEBUG=ON \
      -DCMAKE_BUILD_TYPE=Debug
cmake --build build-ufc -j
```

Options:

| Option | Meaning |
|---|---|
| `-DBOARD=ufc-v2` | uFC v2.0 board port |
| `-DTARGET_CONTROLLER=STM32F100CBT6` | MCU and linker scripts |
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

The repository ships a J-Link script that loads newboot and the application:

```sh
cd firmware/ufc-v2 && JLinkExe -CommanderScript flash.jlink
```

It is equivalent to:

```
device STM32F100CB
si SWD
speed 1000
VTREF 3300
connect
loadfile uart-debug/newboot.bin 0x08000000
loadfile uart-debug/openMMC.bin 0x08004000
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
ipmitool -I lan -H <mch> -A none -T 0x82 -m 0x20 -t <target> raw 0x32 0x01
```

### Commit hash read

Returns the 20‑byte git SHA‑1 embedded at build time:

```sh
ipmitool -I lan -H <mch> -A none -T 0x82 -m 0x20 -t <target> raw 0x32 0x02
```

Clock switch configuration is **not used** on the uFC‑v2: the 8V54816 and
SI5345A clock devices are mapped on the I2C mux but the board firmware does not
program a clock crosspoint.

## Documentation

| Document | Contents |
|---|---|
| [docs/USER_MANUAL.md](docs/USER_MANUAL.md) | Hardware, build, flashing, FRU, IPMI, troubleshooting, recovery |
| [docs/DEBUGGING.md](docs/DEBUGGING.md) | Bring-up log: every failure, root cause and fix |
| [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) | Third-party components and their licenses |

## License

This project is distributed under the **GNU General Public License v3.0 or
later** — see [LICENSE](LICENSE).

It is a **research fork** of
[lnls-dig/openMMC](https://github.com/lnls-dig/openMMC); the uFC‑v2 board port
(`port/board/ufc-v2/`), the newboot/EEPROM/mux fixes and the FRU changes are
modifications made in 2026 for scientific use. Modifications are tracked in the
git history and noted where they are not obvious.

Bundled third‑party components (FreeRTOS, STM32 StdPeriph, CMSIS) keep their own
licenses — see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). OpenBLT is no
longer used: the first‑stage bootloader (newboot) is part of this repository.
