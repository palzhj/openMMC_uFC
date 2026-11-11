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

## uRTM2.3 — complete tree behind the AMC’s channel 5

Measured with `tools/firmware/u4fcp-v2/rtm_scan.py` (addresses 0x08‑0x77).
Last verified: 2026-11-11, re-scan identical to the table below.

| path | devices |
|---|---|
| RTM bus (`0x70` ch5) | IO **`0x3C`**, **`0x3E`** (+ the muxes `0x70/0x71/0x72` ACK themselves) |
| `0x71` ch0 | FMC2 EEPROM `0x52` — **not populated** |
| `0x71` ch1 | FMC3 EEPROM `0x53` — **not populated** |
| `0x71` ch2 | **CLK MUX `0x49`**, **PLL2 `0x6A`** |
| `0x71` ch3 | **JTAG IO `0x3D`**, **TCA9548A `0x72`** |
| `0x71` ch4 | FireFly — not populated |
| `0x71` ch5 | FireFly — not populated |
| `0x71` ch6 | DDR3 — not populated |
| `0x71` ch7 | **24C512 `0x55`** (FRU, blank), **PMBus `0x5B`**, **pot `0x57`** |
| `0x72` ch0 (via `0x71` ch3) | **ReDriverIO `0x3F`**, **`0x76`** |
| `0x72` ch1 | **ReDriverIO `0x3F`**, **`0x76`** |
| `0x72` ch2 | **JTAG IO `0x3D`** |

Remark: `0x3C/0x3D/0x3E/0x3F` and the mux addresses also answer on paths that
do not route them — the uRTM buses are not strictly isolated (or a channel
select occasionally does not latch); the muxes and the RTM IO devices sit
upstream of the second mux, so a scan cannot hide them.

Read example (RTM FRU):
```
raw 0x32 0x20 0x01 0x02 0x70 0x05 0x71 0x07 0x55 0x02 0x00 0x00 0x10
```

## uRTM2.3 TCA9554 expanders — complete record

All five expanders with their register map (`0x00` input, `0x01` output,
`0x02` polarity, `0x03` config: bit = 1 → input).

| device | name | input | output | polarity | config | P7…P0 |
|---|---|---|---|---|---|---|
| `0x3C` | RTM IO (RTM bus) | `4b` | `0b` | `00` | `f0` | `IIIIOOOO` |
| `0x3E` | FMC IO (RTM bus) | `f3` | `ff` | `00` | `ff` | `IIIIIIII` |
| `0x3D` | JTAG IO (72ch2) | `7f` | `70` | `00` | `8f` | `IOOOIIII` |
| `0x3F` | ReDriverIO A (72ch0) | `0c` | `0c` | `00` | `00` | `OOOOOOOO` |
| `0x3F` | ReDriverIO B (72ch1) | `0c` | `0c` | `00` | `00` | `OOOOOOOO` |

Re-read them anytime with `tools/firmware/u4fcp-v2/rtm_dump.py`
(extend its tree with the `0x3F` entries for ch0/ch1).
