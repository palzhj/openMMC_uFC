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
      + [Application / newboot over UART (OpenBLT XCP)](#application--newboot-over-uart-openblt-xcp)
      + [Bootloader over SWD (J-Link)](#bootloader-over-swd-j-link)
   * [Debugging](#debugging)
   * [FRU information](#fru-information)
   * [IPMI custom commands](#ipmi-custom-commands)
   * [License](#license)

## Memory map

| Region | Address | Size | Notes |
|---|---|---|---|
| OpenBLT bootloader | `0x08000000` | 8 KB | **Never erase this region** |
| newboot | `0x08002000` | 8 KB | OpenBLT application, vector checksum at `0x080021D0` |
| Application | `0x08004000` | 56 KB | openMMC |
| Firmware staging | `0x08012000` | 56 KB | HPM.1 upload target |
| `fw_info` | `0x0801FF00` | 256 B | pending‑update flag (`0xAA…`) used by newboot |

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

### Application / newboot over UART (OpenBLT XCP)

The board ships with OpenBLT, which accepts an XCP session on USART1.
Build [BootCommander](https://github.com/feaser/openblt) and flash:

```sh
# application (base address 0x08004000 is encoded in the file)
BootCommander -s=xcp -t=xcp_rs232 -d=/dev/ttyUSB0 -b=115200 \
              -t6=500 -t1=500 build-ufc/out/openMMC.srec

# newboot goes to 0x08002000
BootCommander -s=xcp -t=xcp_rs232 -d=/dev/ttyUSB0 -b=115200 \
              -t6=500 -t1=500 build-ufc/out/newboot.srec
```

If the backdoor entry times out, power‑cycle the board while BootCommander is
connecting.

> [!WARNING]
> The application's first 1.5 KB are reserved for the OpenBLT vector table
> checksum (word at `0x080021D0` must stay `0x55AA11EE`). Do not relocate the
> newboot or erase its checksum slot.

### newboot / application over SWD (J-Link)

The repository ships a J-Link script that loads newboot and the application
without touching the bootloader region:

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
loadfile uart-debug/newboot.bin 0x08002000
loadfile uart-debug/openMMC.bin 0x08004000
r
g
q
```

`VTREF 3300` only takes effect after a J-Link power cycle (or with the J-Link
VTref pin wired to the board's 3.3 V).

> [!CAUTION]
> Never erase or reprogram `0x08000000–0x08001FFF` during normal firmware
> updates — it holds the OpenBLT bootloader.

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

## License

This project is distributed under the **GNU General Public License v3.0 or
later** — see [LICENSE](LICENSE).

It is a **research fork** of
[lnls-dig/openMMC](https://github.com/lnls-dig/openMMC); the uFC‑v2 board port
(`port/board/ufc-v2/`), the newboot/EEPROM/mux fixes and the FRU changes are
modifications made in 2026 for scientific use. Modifications are tracked in the
git history and noted where they are not obvious.

Bundled third‑party components (FreeRTOS, STM32 StdPeriph, CMSIS) keep their own
licenses — see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). The OpenBLT
bootloader is a separate program and is not included in this repository.
