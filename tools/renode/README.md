# Renode simulation

`u4fcp.resc` runs the u4FCP-v2 application (`BOARD=u4fcp-v2`) on an emulated
STM32F103 (128 KB flash, 20 KB SRAM) with the UART captured to
`/tmp/renode_uart.txt`.

```bash
cmake -S . -B .hw2/u4fcp_sim -DBOARD=u4fcp-v2 \
      -DTARGET_CONTROLLER=STM32F103CBT6 -DCMAKE_BUILD_TYPE=Release -DRENODE_SIM=ON
make -C .hw2/u4fcp_sim openMMC -j4
renode --disable-gui --hide-monitor --plain tools/renode/u4fcp.resc
cat /tmp/renode_uart.txt
```

Expected output (Renode):

```
openMMC Starting!
Build date: ...
Version: ...
[RAM] start        heap_free=11384 stack_unused=520
```

`RENODE_SIM` makes `SystemInit()` only relocate the vector table (VTOR =
0x08002000) and return; Renode has no RCC model, so the normal clock setup
would spin forever on `RCC_CR`/`RCC_CFGR`.

## Limitations

The emulated STM32F103 has I2C controllers but **no board devices**, so the
FRU read from the EEPROM at I2C 0x50 stalls before the scheduler starts
(the firmware polls the bus with no timeout). Attaching an AT24xx model or a
Python I2C stub is needed to run past `fru_init()`; the rest of the
firmware (UART, RAM diagnostics, FreeRTOS once started) runs as on hardware.
