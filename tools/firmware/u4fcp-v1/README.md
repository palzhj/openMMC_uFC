# u4FCP-v1 prebuilt images (STM32F103C8T6, 64 KB flash, 20 KB SRAM)

- `normal/` — production image (no UART output).
- `uart-debug/` — built with `-DUART_DEBUG=ON`; `printf` on USART1
  (PA9/PA10, 115200).

Build (the 64 KB part needs `-Os`):

```bash
cmake -S . -B build-u4fcp-v1 -DBOARD=u4fcp-v1 \
      -DTARGET_CONTROLLER=STM32F103C8T6 \
      -DOPTIMIZE_SIZE=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-u4fcp-v1 -j
```

Flash with J-Link from this directory:

```bash
./flash-jlink.sh uart-debug     # writes newboot and the application
# or, with a fixed image:
JLinkExe -CommanderScript flash.jlink
```

## Layout (64 KB)

| region | range | size |
|---|---|---|
| newboot | `0x08000000 – 0x08001FFF` | 8 KB |
| application | `0x08002000 – 0x0800FBFF` | 55 KB |
| staging | — | none in flash |
| `fw_info` | `0x0800FC00 – 0x0800FFFF` | 1 KB |

There is no flash staging area on this part (two 41 KB images don't fit in
64 KB): the update staging is designed to use the **upper 64 KB of the
AT24CM01** EEPROM (`0x10000–0x1FFFF`, slave address 0x55), which is not
implemented yet. The clock configuration lives in the same EEPROM
(pointer `0x200`, slots A/B at `0x300`/`0x1B00`).

`newboot` is linked at `0x08000000` with its vector table at the reset vector,
so no checksum fix-up is required; both `loadfile` commands write only their
own bytes — never erase the first 8 KB unless updating newboot.

If the probe reports `Target voltage too low`: the script sets `VTREF 3300`,
but that applies only after a **J-Link power cycle** (unplug/replug the USB),
or wire the J-Link VTref pin to the board's 3.3 V + GND.
