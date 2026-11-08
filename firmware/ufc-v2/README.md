# uFC-v2 prebuilt images (STM32F100CBT6, 128 KB)

- `normal/` — production image (FRU online, HPM.1, **no** UART output).
- `uart-debug/` — built with `-DUART_DEBUG=ON`; `printf` on USART1
  (PA9/PA10, 115200).

Flash with J-Link from this directory:

```bash
./flash-jlink.sh uart-debug     # computes the OpenBLT checksum automatically
# or, with the fixed value for the current image:
JLinkExe -CommanderScript flash.jlink
```

Both load `newboot.bin` at `0x08002000` and `openMMC.bin` at `0x08004000`;
`0x08000000-0x08001FFF` (OpenBLT) is **not** erased or written.

OpenBLT validates the newboot vector table with a 32-bit sum, and `newboot.bin`
carries the seed `0x55AA11EE` at `0x1D0`, so the real checksum must be written
to `0x080021D0` after `loadbin`. `flash-jlink.sh` computes it; `flash.jlink`
contains the value for the current image (`0xAFFEF27A`).

If the probe reports `Target voltage too low`: the script sets `VTREF 3300`,
but that applies only after a **J-Link power cycle** (unplug/replug the USB),
or wire the J-Link VTref pin to the board's 3.3 V + GND.
