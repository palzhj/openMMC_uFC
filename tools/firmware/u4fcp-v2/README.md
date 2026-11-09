# u4FCP-v2 prebuilt images (STM32F103CBT6, 128 KB flash, 20 KB SRAM)

- `normal/` — production image (no UART output).
- `uart-debug/` — built with `-DUART_DEBUG=ON`; `printf` on USART1
  (PA9/PA10, 115200).

Build:

```bash
cmake -S . -B build-u4fcp-v2 -DBOARD=u4fcp-v2 \
      -DTARGET_CONTROLLER=STM32F103CBT6 -DCMAKE_BUILD_TYPE=Release
cmake --build build-u4fcp-v2 -j
```

Flash with J-Link from this directory:

```bash
./flash-jlink.sh uart-debug     # writes newboot and the application
# or, with a fixed image:
JLinkExe -CommanderScript flash.jlink
```

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
