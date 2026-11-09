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

## FRU EEPROM

The u4FCP board populates a **CAT24C512** (64 KB, 128-byte pages, 2-byte
addressing) at I2C address **0x54** (`CHIP_ID_EEPROM` in
`port/board/u4fcp-v2/i2c_mapping.c`), matching `modules/eeprom_24xx512.c`
(`EEPROM_PAGE_SIZE 128`). The emulated STM32F103 has I2C controllers but no
board devices, so with `RENODE_SIM` the FRU read is skipped: `fru_init()`
builds the runtime FRU and the boot continues.

Measured in Renode (12 KB heap):

```
[RAM] start        heap_free=11384
[RAM] fru          heap_free=11152
[RAM] sdr          heap_free=11104
[RAM] sensors      heap_free=7936
[RAM] payload      heap_free=6776
[RAM] ipmi         heap_free=4008
[RAM] board        heap_free=4008
```

The emulator then runs the FreeRTOS part (the hotswap task spams
"AMC Hotswap handle pressed!" because the emulated GPIOs read as pressed).

To emulate the bus properly, attach an I2C EEPROM model at 0x54 (Renode
ships `I2C.GenericI2cEeprom`, but it needs the right size/paging properties).
