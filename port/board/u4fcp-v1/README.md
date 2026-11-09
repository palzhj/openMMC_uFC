# u4FCP-v1

Same board as u4FCP-v2 (ADN4604 + 2× Si5345, CAT24C512 FRU/clock EEPROM) but
with the **STM32F103C8T6** (64 KB flash, 20 KB SRAM, 1 KB pages).

The board sources are shared with the v2 port: `BOARD_PATH` points to
`../u4fcp-v2` (i2c/pin mapping, SDR, FRU image, payload). Only the
controller and the flash layout differ.

## Flash layout (64 KB)

| region | range | size |
|---|---|---|
| newboot | `0x08000000 – 0x08001FFF` | 8 KB |
| application | `0x08002000 – 0x0800FBFF` | **55 KB** |
| *staging* | — | **none** (no OTA on this part) |
| fw_info | `0x0800FC00 – 0x0800FFFF` | 1 KB |

There is no room for an A/B staging image on a 64 KB part, so the v1 build
does **not** include the HPM update module; firmware is loaded with the
debugger. The clock configuration slots live in the CAT24C512 EEPROM
(`CLOCK_STORE_EEPROM`), not in flash.

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
