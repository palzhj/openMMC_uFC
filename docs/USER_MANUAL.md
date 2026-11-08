# uFC‑v2 openMMC — User Manual

**Revision 1.0 — firmware v0.1.0 — 2026‑11‑07**

---

## 1. Overview

The uFC v2.0 is an AMC (Advanced Mezzanine Card) module whose MMC (Module
Management Controller) runs **openMMC**, an open‑source IPMI/IPMB firmware.

| Item | Value |
|---|---|
| MCU | **STM32F100CBT6** — Cortex‑M3 value line, 128 KB flash, 8 KB SRAM |
| Bootloader | **newboot** at `0x08000000` (8 KB) — first stage, applies staged updates |
| Application | openMMC at `0x08004000` (56 KB) |
| Update staging | `0x08012000` (56 KB) |
| Console | USART1, PA9 (TX) / PA10 (RX), **115200 8N1** |
| I²C | I²C2, PB10 (SCL) / PB11 (SDA), **100 kHz**, behind a TCA9548A mux |
| Debug | SWD on PA13/PA14 (JTAG disabled; PB3/PB4/PA15 are GPIO) |
| LEDs | blue PB4, green PB0, red PB5 (active low) |
| Power | 12 V payload rail; module power is not MMC‑switched |

The firmware implements IPMI/IPMB, hot‑swap, payload management, SDR and a
**104‑byte AMC FRU** stored in a 24AA025E48 EEPROM.

---

## 2. Quick start

```sh
# 1. build
cmake -S . -B build-ufc \
      -DBOARD=ufc-v2 \
      -DTARGET_CONTROLLER=STM32F100CBT6 \
      -DUART_DEBUG=ON \
      -DDISABLE_WATCHDOG=OFF \
      -DCMAKE_BUILD_TYPE=Debug
cmake --build build-ufc -j

# 2. flash newboot + application over SWD (J-Link)
cd firmware/ufc-v2 && ./flash-jlink.sh uart-debug

# 3. watch the console
picocom -b 115200 /dev/ttyUSB0
```

Expected first boot:

```
openMMC Starting!
Build date: Nov  7 2026 23:45:13
Version: v0.1.0
SHA1: 81d84b62fc96521c22f6696bec95ed1d182ff23f
[FRU][AMC] Asserting FRU information integrity
[FRU][AMC] Checking BOARD AREA record... Success!
[FRU][AMC] Checking PRODUCT AREA record... Success!
[FRU][AMC] Checking MULTIRECORD AREA records...
[FRU][AMC] FRU info is healthy!
Main Power on (not software controlled)
AMC Hotswap handle pressed!
Payload power good
Reset FPGA
```

---

## 3. Building from source

### 3.1 Prerequisites

| Requirement | Version |
|---|---|
| `arm-none-eabi-gcc` | 10+ (14.2.1 tested) |
| CMake | **≥ 3.10** (4.2.3 tested; no extra flags needed) |
| GNU make | any |
| `bin2hpm` (optional) | builds `.hpm` files for IPMI OTA updates |
| J‑Link software (optional) | SWD programming |

```sh
sudo apt install gcc-arm-none-eabi cmake make
```

### 3.2 Configure

Always build **out of source**:

```sh
cmake -S . -B build-ufc -DBOARD=ufc-v2 -DTARGET_CONTROLLER=STM32F100CBT6 \
      -DUART_DEBUG=ON -DDISABLE_WATCHDOG=OFF -DCMAKE_BUILD_TYPE=Debug
cmake --build build-ufc -j
```

### 3.3 Build options

| Option | Default | Meaning |
|---|---|---|
| `-DBOARD=` | `afc` | `ufc-v2` for this board (also `u4fcp-v2`, `urtm-v2`, …) |
| `-DTARGET_CONTROLLER=` | board‑specific | `STM32F100CBT6` for uFC |
| `-DUART_DEBUG=ON` | off | console on USART1 (PA9/PA10) |
| `-DDISABLE_WATCHDOG=OFF` | watchdog **off** | enable the IWDG (1 s timeout, fed every 666 ms) |
| `-DFRU_BUILD=ON` | off | one‑time FRU builder + EEPROM write (see §6) |
| `-DFRU_READ_ONLY=ON` | off | drop the FRU write path entirely |
| `-DCMAKE_BUILD_TYPE=Debug` | — | `-Og -g3` (use Release for `-O3`) |

### 3.4 Outputs

```
build-ufc/out/openMMC.elf  openMMC.bin  openMMC.srec
build-ufc/out/newboot.elf  newboot.bin  newboot.srec
```

`Version` / `SHA1` are taken from `git describe` + `git rev-parse` at configure
time and embedded in the image.

---

## 4. Memory map and boot flow

| Region | Address | Size | Content |
|---|---|---|---|
| newboot | `0x08000000` | 8 KB | first‑stage bootloader (reset vector) |
| *(unused)* | `0x08002000` | 8 KB | free |
| app | `0x08004000` | 56 KB | openMMC |
| staging | `0x08012000` | 56 KB | firmware upload target |
| `fw_info` | `0x0801FF00` | 256 B | pending‑update flag used by newboot |

OpenBLT has been dropped; the CPU starts newboot directly from `0x08000000`.

Boot sequence:

1. newboot reads `fw_info`; if an update is pending it copies staging → app and
   clears the flag.
2. newboot jumps to the application (`0x08004000`).
3. openMMC initialises clocks, UART, I²C and the FRU, then starts FreeRTOS.

---

## 5. Flashing

### 5.1 Over IPMI (HPM.1 OTA) — recommended in a crate

Through a NAT‑MCH (double‑bridged; see the NAT‑MCH manual Appendix B.3):

```sh
ipmitool -I lan -H <mch-ip> -A none -T 0x82 -B 0 -t 0x7e -b 7 \
         hpm upgrade openMMC.hpm
ipmitool -I lan -H <mch-ip> -A none -T 0x82 -B 0 -t 0x7e -b 7 \
         hpm activate
```

* `-t 0x7e` is the MMC’s IPMB‑L address (AMC7) and `-b 7` the IPMB‑L bus;
  `-T 0x82 -B 0` is the carrier transit address/bus.
* Build the HPM image with `bin2hpm`:

  ```sh
  bin2hpm -c 1 -n -m 0x315A -p 0x0 -d 0 out/openMMC.bin -o out/openMMC.hpm
  ```

* `hpm upgrade` stages the image; `hpm activate` writes `fw_info` and resets.
  newboot then applies the image automatically and clears the flag.
* Check the target first with `hpm check`, `hpm compprop 0 1`,
  `hpm upgstatus`.

### 5.2 newboot / application over SWD (J‑Link)

A ready script ships in `firmware/ufc-v2/flash.jlink`:

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

```sh
cd firmware/ufc-v2 && JLinkExe -CommanderScript flash.jlink
# or: ./flash-jlink.sh uart-debug
```

`VTREF 3300` only takes effect after a J‑Link power cycle, or with the J‑Link
VTref pin wired to the board’s 3.3 V. newboot is linked at `0x08000000`;
no OpenBLT checksum fix‑up is needed.

## 6. FRU information

### 6.1 Device

* **24AA025E48** at I²C `0x50`, TCA9548A **channel 0**
* 2 Kbit total: lower 128 bytes writable, upper half factory EUI‑48 (read‑only)

### 6.2 Content (104 bytes)

| Area | Offset | Content |
|---|---|---|
| Common header | `0x00` | board `0x08`, product `0x28`, multirecord `0x48` |
| Board info | `0x08` | IHEP / uFC / SN `H01` / PN `HEPS`, mfg time = minutes since 1996‑01‑01 |
| Product info | `0x28` | IHEP / uFC / AMC / version `2.0` |
| Module current | `0x48` | AMC.0 record `16h` — 5.4 A |
| AMC P2P | `0x53` | AMC.0 record `19h` — 1 × Ethernet link, ports 4–7 (EOL set) |

There is **no Zone 3 record** — the uFC has no Zone 3 connector.

### 6.3 Programming / reprogramming

Build with `-DFRU_BUILD=ON`, flash, and power‑cycle once:

```
>FRU_WRITE_EEPROM flag enabled! Building FRU info...
	-Manuf time: 16226822
	-Serial Number: H01
	-Part Number: HEPS
>AMC FRU total size: 104 bytes
 Writing FRU info to EEPROM...
[FRU][AMC] FRU info is healthy!
```

To change the identity, edit `port/board/ufc-v2/user_amc_fru.h`
(`AMC_BOARD_*`, `AMC_PRODUCT_*`) and repeat.

Then rebuild the normal image **without** `-DFRU_BUILD` — the production
firmware reads the FRU and keeps the online IPMI write path.

### 6.4 Online write

With `FRU_NO_BUILDER` (default) the FRU can be written over IPMI using the
standard FRU device commands through the MCH.

---

## 7. I²C map

```
I2C2 (PB10/PB11, 100 kHz)
 └─ TCA9548A @0x70
     ├─ ch0: 24AA025E48 @0x50   FRU EEPROM
     └─ ch2: 8V54816    @0x58   clock generator
             SI5345A    @0x69   clock generator
```

| Chip | Address | Channel | Purpose |
|---|---|---|---|
| TCA9548A | `0x70` | — | 8‑channel I²C switch |
| 24AA025E48 | `0x50` | 0 | FRU EEPROM (128 B writable) |
| 8V54816 | `0x58` | 2 | clock device (not configured by this firmware) |
| SI5345A | `0x69` | 2 | clock device (not configured by this firmware) |

The mux may NACK its first transactions right after reset; the driver retries
automatically.

---

## 8. LEDs and indicators

| LED | GPIO | Meaning |
|---|---|---|
| Blue | PB4 | FPGA reset activity / lamp test |
| Green | PB0 | general status |
| Red | PB5 | fault / hot‑swap |

All LEDs are **active low**; the LED task is enabled in this build.

---

## 9. IPMI custom commands

| Command | Netfn | Cmd | Returns |
|---|---|---|---|
| Get free heap | `0x32` | `0x01` | u32 free bytes, little‑endian |
| Get commit hash | `0x32` | `0x02` | 20‑byte git SHA‑1 |

```sh
ipmitool -I lan -H <mch> -A none -T 0x82 -m 0x20 -t <target> raw 0x32 0x02
```

---

## 10. Troubleshooting

| Symptom | Likely cause | Action |
|---|---|---|
| No console output at all | Wrong baud, TX/RX swapped, UART_DEBUG not built, or app not running | Check 115200 8N1; verify `-DUART_DEBUG=ON`; flash a known‑good image |
| newboot prints `jump app=…` but nothing else | App vector table invalid | Reflash the application (J‑Link or OTA) |
| App starts then resets immediately | `fw_info` pending flag with empty staging | Clear `0x0801FF00–0x0801FFFF` to `0xFF` |
| `COMMON HEADER checksum` error | FRU blank or unreadable | Reprogram FRU (`-DFRU_BUILD=ON`); check I²C |
| `FRU info is healthy!` never appears | I²C mux/bus fault | Verify `0x70` ACKs; check SCL/SDA pull‑ups |
| Board reboots every ~1 s | Watchdog enabled but tasks starved | Check heap usage, stack sizes; rebuild with `-DDISABLE_WATCHDOG=ON` to confirm |
| `Target voltage too low` (J‑Link) | VTref not detected | Power‑cycle the J‑Link after setting `VTREF 3300` |
| `hpm upgrade` times out | MMC not responding on IPMB‑L | Check `-T 0x82 -B 0 -t 0x7e -b 7`; verify the MCH sees AMC7 |
| `Version: …-NOTFOUND` | Built outside a git checkout | Build from the repository root |

---

## 11. Recovery (unbricking)

1. **Application won’t boot, newboot still healthy**
   Flash the application over SWD (§5.2) or via IPMI OTA (§5.1).
2. **newboot corrupt**
   SWD‑program `newboot.bin` at `0x08000000`.
3. **FRU corrupted**
   Build with `-DFRU_BUILD=ON`, flash, power‑cycle once, rebuild production.

---

## 12. Maintenance and upgrades

* A firmware update only rewrites the application (or staging); the bootloader
  is untouched.
* HPM.1 upgrades over IPMI are supported by the `HPM` module and can be used
  from an MCH with `ipmitool`/`bin2hpm`; see the project README.
* Keep the board revision and the FRU identity in sync when swapping modules.

---

## 13. Versioning and release

* Tags follow `vMAJOR.MINOR.PATCH`; the console shows `Version` (git describe)
  and `SHA1` (full commit).
* A production release should be built from a **clean** checkout at a tag, with
  `-DFRU_BUILD` off and the watchdog enabled.

---

## 14. Licensing

This firmware is distributed under the **GNU General Public License v3.0 or
later** (see `LICENSE`). It is a research fork of
[lnls-dig/openMMC](https://github.com/lnls-dig/openMMC).

Third‑party components keep their own licenses — see
[THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md):

* FreeRTOS — MIT
* STM32F10x StdPeriph — ST SLA0044
* CMSIS — ARM LEC‑PRE‑00425‑V2.0

---

## 15. Appendix — repository layout

```
port/board/ufc-v2/          board port (pins, I2C map, FRU data, payload)
port/board/ufc-common/      shared uFC code (LED, IPMB address, FRU wiring)
port/ucontroller/stm32/     STM32F1xx port, linkers, startup
modules/                    openMMC modules (IPMI, FRU, SDR, EEPROM, …)
firmware/ufc-v2/            prebuilt images + J‑Link script
docs/DEBUGGING.md           bring‑up log and root‑cause analysis
docs/USER_MANUAL.md         this document
```

**Document history**

| Rev | Date | Changes |
|---|---|---|
| 1.0 | 2026‑11‑07 | First release: uFC‑v2 port, verified FRU, watchdog, v0.1.0 |
