# I²C map — u4FCP‑v2 (AMC) + uRTM2.3 (RTM)

![I²C map](u4fcp-v2-i2c-map.png)

## u4FCP‑v2 — I2C2 bus (PB10/PB11, 100 kHz) behind a TCA9548A `@0x70`

Source: `u4FCPv2/readme/i2c.png`; channel numbers from the firmware
`i2c_mapping.c` (verified on the board).

| mux ch | bus | devices (address) |
|---|---|---|
| 0 | FMC0 | EEPROM `0x50` (`7'b1010_000`) |
| 1 | FMC1 | EEPROM `0x51` (`7'b1010_001`) |
| 2 | clock | **CLK MUX `0x48`** (`7'b1001_000`, ADN4604), **PLL0 `0x68`** (`7'b1101_000`), **PLL1 `0x69`** (`7'b1101_001`) |
| 3 | firefly | FireFly transceiver (the diagram draws the FPGA on 3 — firmware uses 4, confirmed by the JTAG read) |
| 4 | FPGA | FPGA KUP (no I²C address) + **JTAG IO `0x39`** (`7'b0111_001`) |
| 5 | **RTM** | see the RTM section below |
| 6 | DDR4 | (empty) |
| 7 | power | **24C512 `0x54`** (`7'b1010_100`), **PMBus `0x11`** (`7'b001_001`), **potentiometer `0x56`** (`7'b1010_110`) |

## uRTM2.3 — behind the AMC’s channel 5, its own TCA9548A `@0x71`

Source: `u4FCPv2/readme/i2c.png` (RTM side).

| device | address | function |
|---|---|---|
| **TCA9548A** | **`0x71`** (`7'b1110_001`) | the RTM’s 8‑channel mux |
| → ch0 | EEPROM `0x52` (`7'b1010_010`) | FMC2 EEPROM |
| → ch1 | EEPROM `0x53` (`7'b1010_011`) | FMC3 EEPROM |
| → ch2 | **CLK MUX `0x49`** (`7'b1001_001`), **PLL2 `0x6A`** (`7'b1101_010`) | RTM clocking |
| → ch3 | **IIC MUX `0x72`** (`7'b1110_010`) | the RTM FPGA bus: **ReDriverIO `0x3F`** (`7'b0111_111`), **JTAG IO `0x3D`** (`7'b0111_101`), ReDriver ×2 `7'b1111_x10`/`7'b1110_1x0` |
| → ch4 | FireFly transceiver |  |
| → ch5 | FireFly transceiver |  |
| → ch6 | DDR3 |  |
| → ch7 | **24C512 `0x55`** (`7'b1010_101`), **PMBus `0x5B`** (`7'b1011_011`), **potentiometer `0x57`** (`7'b1010_111`) | RTM power/FMC |

The AMC’s RTM channel also carries the RTM’s **IO `0x3E`** (`7'b0111_110`), the
**FMC IO `0x3E`** and the **RTM MCU** (no I²C address, it is the AMC’s peer
controller).

Addresses are the 7‑bit values drawn on the block diagram.  The RTM bus only responds when
`RTM_MP_3V3` (UCD `GPO_RTM_MP_EN`) is enabled — currently not done by the
AMC firmware.
