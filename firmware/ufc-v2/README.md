# uFC-v2 prebuilt images (STM32F100CBT6, 128 KB)

- `normal/` — production image (FRU online, HPM.1, **no** UART output).
- `uart-debug/` — built with `-DUART_DEBUG=ON`; `printf` on USART1
  (PA9/PA10, 115200).

Flash with J-Link from this directory:

```bash
JLinkExe -CommanderScript flash.jlink
```

The script loads `newboot.bin` at `0x08002000` and `openMMC.bin` at
`0x08004000`. `0x08000000–0x08001FFF` (OpenBLT) is **not** erased or written.

If the probe reports `Target voltage too low`: the script sets `VTREF 3300`,
but that applies only after a **J-Link power cycle** (unplug/replug the USB),
or wire the J-Link VTref pin to the board's 3.3 V + GND.
