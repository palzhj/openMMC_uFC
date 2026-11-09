# u4FCP-v1

Same board as u4FCP-v2 (ADN4604 + 2× Si5345) but with the
**STM32F103C8T6** (64 KB flash, 20 KB SRAM, 1 KB pages) and an **AT24CM01**
EEPROM (1 Mbit = 128 KB, 256-byte pages).

The board sources are an **independent copy** of the v2 port (i2c/pin
mapping, SDR, FRU image, payload, OEM commands) so this port can diverge;
only the controller, the EEPROM part and the flash layout differ today.

## Flash layout (64 KB)

| region | range | size |
|---|---|---|
| newboot | `0x08000000 – 0x08001FFF` | 8 KB |
| application | `0x08002000 – 0x0800FBFF` | **55 KB** |
| *staging* | — | **none** (no OTA on this part) |
| fw_info | `0x0800FC00 – 0x0800FFFF` | 1 KB |

There is no room for an A/B staging image on a 64 KB part, so the v1 build
does **not** include the HPM update module; firmware is loaded with the
debugger. The clock configuration slots live in the EEPROM (`CLOCK_STORE_EEPROM`),
not in flash.

## EEPROM map (AT24CM01, 128 KB)

| range | use |
|---|---|
| `0x00000 – 0x001FF` | FRU (512 B) |
| `0x00200 – 0x032FF` | clock store: pointer 0x200, slots A/B at 0x300 / 0x1B00 (6 KB each) |
| `0x03300 – 0x0FFFF` | free (lower 64 KB) |
| `0x10000 – 0x1FFFF` | **firmware staging** (upper 64 KB, A16=1 → slave address 0x55) |

The AT24CM01 has 17 address bits: A16 is bit 0 of the I2C device address
(0x54 = lower 64 KB, 0x55 = upper 64 KB). The driver keeps 16-bit
addressing for the FRU/clock paths and exposes
`eeprom_24xx512_read32/write32` for the staging area. The 41 KB firmware
image fits in the upper 64 KB, which makes EEPROM-staged OTA possible
without a flash staging region.

The linkers are `port/ucontroller/stm32/stm32f1xx/linker/STM32F103C8T6_app.ld`
/ `_boot.ld`.

## Build

```bash
cmake -S . -B .hw2/u4fcp_v1 -DBOARD=u4fcp-v1 \
      -DTARGET_CONTROLLER=STM32F103C8T6 -DOPTIMIZE_SIZE=ON -DCMAKE_BUILD_TYPE=Release
make -C .hw2/u4fcp_v1 openMMC newboot -j4
```

Result: `newboot.bin` 3,524 B, `openMMC.bin` 45,164 B (fits the 55 KB
region at `-Os`).

Enabled vendor extensions: `EEPROM_ACCESS`, `I2C_ACCESS`, `CLOCK_PROGRAM`,
`CLOCK_STORE` (EEPROM), `FRU_USE_COMPILED` (FRU from `user_amc_fru.h`).
