# u4FCP-v2 prebuilt images (STM32F103CBT6, 128 KB flash, 20 KB SRAM)

The images are **build outputs**, not committed files (`.gitignore` excludes
`*.bin`). Build them with:

```bash
cmake -S . -B .hw2/u4fcp -DBOARD=u4fcp-v2 \
      -DTARGET_CONTROLLER=STM32F103CBT6 -DCMAKE_BUILD_TYPE=Release
make -C .hw2/u4fcp openMMC -j
# -> .hw2/u4fcp/out/newboot.bin  (~4.5 KB)
# -> .hw2/u4fcp/out/openMMC.bin  (~54 KB)
```

`uart-debug/` (if present) is the same image with `-DUART_DEBUG=ON`
(`printf` on USART1 PA9/PA10, 115200); `normal/` is the production build.
`jlink_program.sh` looks for these images in `uart-debug/`, `normal/` next to
the script and otherwise falls back to the local build tree
(`.hw2/u4fcp/out`); `--bin-dir DIR` overrides it.

Flash with J-Link from this directory:

```bash
./flash-jlink.sh uart-debug     # writes newboot and the application
# or, with a fixed image:
JLinkExe -CommanderScript flash.jlink
```

## Programming a brand-new board (production flow)

The bring-up is split in two steps, plus a convenience wrapper:

| script | does |
|---|---|
| `jlink_program.sh` | newboot + application over SWD, `fw_info` cleared |
| `fru_program.sh <serial>` | FRU with the unique serial (+ optional clock store) |
| `clk_program.sh` | write/capture the clock configuration preset |
| `program.sh <serial>` | both flash + FRU in one go |

```bash
# 1. flash a blank board (SWD)
./jlink_program.sh --bin-dir uart-debug

# 2. in the crate (MCH at 192.168.1.41, board in AMC12): FRU with the serial
./fru_program.sh U4FCP2-0001
./fru_program.sh U4FCP2-0001 --slot 7 --mch 192.168.1.41

# 3. clock configuration preset (capture once from a programmed board)
./clk_program.sh --dump clk-preset.bin         # from a working board
./clk_program.sh clk-preset.bin --apply        # write it, apply now

# or everything in one command:
./program.sh U4FCP2-0001 --clock-store clk-preset.bin

# or both, when the J-Link and the crate are connected at the same time
./program.sh U4FCP2-0001
```

What the steps do:

1. **jlink_program.sh** — `newboot.bin` at `0x08000000`, `openMMC.bin` at
   `0x08002000`, and `fw_info` (`0x0801FC00`, 8 bytes) cleared to `0xFFFFFFFF`
   so the bootloader boots the application instead of a stale staging image.
2. **fru_program.sh** — waits for the MMC at `0x70 + 2*slot` through the MCH
   bridge, then writes the FRU. The MMC serves its FRU from the CAT24C512
   (`FRU_USE_COMPILED` is OFF; `user_amc_fru.h` is only the factory fallback
   used when the EEPROM holds no valid FRU, so the board still boots in the
   crate with a blank EEPROM and reports the factory values).
   `program_fru.py` builds the image from `fru_template.bin`
   (header + board/product areas + the 100‑byte multi‑record area with the
   point‑to‑point/clock/module‑current records), sets the board and product
   serials to the argument and the manufacturing time to now, writes the
   512‑byte image into the CAT24C512 at `0x54`, then reads it back and
   verifies the serials and checksums.
4. **Clock store** (optional) — `program_clock.py --upload` writes a clock
   store image captured from a programmed board.

Preparation for a batch:

```bash
# the preset lives in the repository:
#   tools/clocking/u4fcp-v2-clk-preset.bin
# capture/refresh it from a known-good board:
python3 program_clock.py --dump tools/clocking/u4fcp-v2-clk-preset.bin --slot 12
# per board:
./clk_program.sh tools/clocking/u4fcp-v2-clk-preset.bin --apply --slot 2
# or together with the FRU:
./program.sh U4FCP2-0001 --clock-store tools/clocking/u4fcp-v2-clk-preset.bin
```

`fru_config.json` holds the FRU field values (manufacturer, names, part
numbers, version, asset tag, module current); edit it once for the product.
The manufacturing time can be forced with `--mfg-time "2026-11-10 01:30"`.

Note: the FRU and clock‑store steps need the board reachable through the
MCH's IPMB‑L — for a bare bench board without an MCH, flash over SWD and
program the FRU later in the crate (`--no-flash`).

## Layout (128 KB)

| region | range | size |
|---|---|---|
| newboot | `0x08000000 – 0x08001FFF` | 8 KB |
| application | `0x08002000 – 0x08010DFF` | 59.5 KB |
| staging | `0x08010E00 – 0x0801FBFF` | 59.5 KB |
| `fw_info` | `0x0801FC00 – 0x0801FFFF` | 1 KB |

The clock configuration is stored in the CAT24C512 EEPROM
(pointer `0x200`, slots A/B at `0x300`/`0x1B00`, 6 KB each, slave 0x54), not
in flash — that is why the app/staging regions are 59.5 KB instead of 50 KB.
The application image is ~49 KB at `-O3`.

`newboot` is linked at `0x08000000` with its vector table at the reset vector,
so no checksum fix-up is required; both `loadfile` commands write only their
own bytes — never erase the first 8 KB unless updating newboot.

If the probe reports `Target voltage too low`: the script sets `VTREF 3300`,
but that applies only after a **J-Link power cycle** (unplug/replug the USB),
or wire the J-Link VTref pin to the board's 3.3 V + GND.
