# uFC-v2 prebuilt images (STM32F100CBT6, 128 KB)

- `normal/` — production image (FRU online, HPM.1, **no** UART output).
- `uart-debug/` — built with `-DUART_DEBUG=ON`; `printf` on USART1
  (PA9/PA10, 115200).

Flash with J-Link from this directory:

```bash
./flash-jlink.sh uart-debug     # writes newboot and the application
# or, with the fixed value for the current image:
JLinkExe -CommanderScript flash.jlink
```

Both load `newboot.bin` at `0x08000000` and `openMMC.bin` at `0x08002000`;
`0x08000000-0x08001FFF` (newboot) is **not** erased or written.

`newboot` is linked at `0x08000000` with its vector table at the reset vector,
so no checksum fix-up is required; `flash.jlink` writes both images directly.

If the probe reports `Target voltage too low`: the script sets `VTREF 3300`,
but that applies only after a **J-Link power cycle** (unplug/replug the USB),
or wire the J-Link VTref pin to the board's 3.3 V + GND.
