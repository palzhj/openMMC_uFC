# uFC clock switch (8V54816A) — port map and register format

Source: `uFCv2/UFCV2.pdf`, sheet 5 (“Clock”), symbol `U67`.

Routing can be described with **Graphviz DOT**: `docs/clock_switch.dot` renders
the crossbar on its own, and `docs/clocking.dot` the merged system view used by
the toolbox — every port of the 8V54816A with its configuration (`in`/`out`,
source, termination, polarity and the matrix byte) in the context of the whole
clocking tree, the green edges being the active routes, with the device
addresses (`8V54816A @0x70 ch2`, `Si5345 @0x69`) and the active-route count in
the title:

![clock switch routes](clock_switch.png)

![uFC clocking system with port configuration](clocking.png)

## The device

Renesas **8V54816A** — a 16-port clock switch. It is **not** a register-indexed
device: every access is a **full 16-byte block transfer** (I²C address `0x58`,
`S_A1 = S_A0 = 0`). Reading the block returns the 16 port configuration bytes;

```
write: [slave addr + W] [port 0] [port 1] … [port 15]
read : [slave addr + R] [port 0] … [port 15]
```

(the MMC driver `modules/idt_8v54816.c` does exactly this, and the generic
command `0x20` can too: `bus=1, mux (0x70,2), dev 0x58, wlen=16`).

## Register format (one byte per port)

| Bit | Name | Meaning |
|---|---|---|
| 7 | Port I/O | 0 = input, 1 = output |
| 6 | Termination | 0 = off (high-Z), 1 = on (100 Ω) |
| 5 | Polarity | 0 = inverted, 1 = non-inverted |
| 4 | — | reserved, 0 |
| 3:0 | Source | for an output: the **input port number** that drives it |

Reset default is `0x00` for every port (all inputs, termination off).

## uFC port assignment (sheet 5)

| Port | Pins | Signal | Direction |
|---|---|---|---|
| CLK0 | 6/7 | `AMC_TCLKD_P/N` | **bidirectional** (TCLKD can be received or driven) |
| CLK1 | 10/11 | `AMC_TCLKC_P/N` | **bidirectional** |
| CLK2 | 15/16 | `AMC_TCLKA_P/N` | **bidirectional** |
| CLK3 | 19/20 | `AMC_TCLKB_P/N` | **bidirectional** |
| CLK4 | 31/32 | `AMC_FCLKA_P/N` | **bidirectional** (FCLKA can be received or driven) |
| CLK5 | 35/36 | `S7_CLK_INOUT_P/N` | **bidirectional** |
| CLK6 | 40/41 | `FPGA_CLK_INOUT_P/N` | **bidirectional** |
| CLK7 | 44/45 | `156M_P/N` (570BAB000544DG) | input |
| CLK8 | 56/57 | `PLL_OUT_P/N` (Si5345 output) | input |
| CLK9 | 60/61 | `PLL_IN_P2` (to Si5345 IN2) | **output** |
| CLK10 | 65/66 | `PLL_IN_P1` (to Si5345 IN1) | **output** |
| CLK11 | 69/70 | `PLL_IN_P0` (to Si5345 IN0) | **output** |
| CLK12 | 81/82 | `FMC0_GBTCLK_M2C_P/N` | **output** |
| CLK13 | 85/86 | `FMC0_CLK_M2C_P/N` | **output** |
| CLK14 | 90/91 | `FMC1_CLK_M2C_P/N` | **output** |
| CLK15 | 94/95 | `FMC1_GBTCLK_M2C_P/N` | **output** |

So ports 0–8 are the available clock **sources**, ports 9–15 are the **outputs**
(three Si5345 PLL inputs and four FMC clocks).

## Example matrices

| Use | Bytes 0…15 | Meaning |
|---|---|---|
| all inputs (reset) | `00` ×16 | switch transparent, no outputs |
| 156M → Si5345 IN0, PLL_OUT → FMC clocks | `00 00 00 00 00 00 00 00 00` then `07 07 07 08 08 08 08` | CLK9–11 = 156M (port 7, output), CLK12–15 = PLL_OUT (port 8, output) |
| FCLKA → Si5345 IN0, PLL_OUT → FMC | … `04` for the PLL input, `08` for the FMC clocks | |

An output byte for the examples above is `0x80 | source` (output, no
termination, non-inverted), e.g. `0x88` = output driven by port 8.

> The routing that the FPGA design expects is a board decision; the switch is
> just a crossbar. The MMC can read/write the whole matrix over IPMI so the
> payload does not have to own it.
