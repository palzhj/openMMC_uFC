# UCD90320 power sequencer — working notes

Sources: `docs/datasheets/UCD90320/` (datasheet `ucd90320.pdf`, PMBus command
reference `slvuaw9c.pdf`, FAQ `slua815b.pdf`, margining note `slva845a.pdf`).

## What it is

The UCD90320 is a **32-rail PMBus power sequencer / system-health controller**
at I²C address **0x11** on the uFC FRU bus (TCA9548A channel 0). It does not
regulate: it monitors voltages/currents/temperatures, sequences the rails and
drives the enables. All control goes through **PMBus commands with a PAGE
byte** — `PAGE` selects the rail, the following command applies to it.

## Commands that matter for bring-up

| Cmd | Name | Use |
|---|---|---|
| `0x00` | PAGE | select the rail (write 1 byte, page = rail) |
| `0x01` | OPERATION | `0x00` = off (immediate), `0x40` = soft off (sequenced), `0x80` = **on**, `0x60`/`0xA0`/`0xC0` = margin low/high |
| `0x02` | ON_OFF_CONFIG | whether OPERATION, the CONTROL pin, or both turn the rail on |
| `0x03` | CLEAR_FAULTS | clears the fault registers |
| `0x20` | VOUT_MODE | linear format; on the uFC `0x14` → exponent −12 (1 LSB = 244 µV) |
| `0x21` | VOUT_COMMAND | setpoint (12V0 is divided 2:1, so its setpoint is 6 V) |
| `0x8B` | READ_VOUT | measured voltage, linear16 (2 bytes, low byte first) |
| `0x79` | STATUS_WORD | fault summary (2 bytes) |
| `0x60/0x61` | TON_DELAY / TON_RISE | sequencing timing (configured in the tifsp) |
| `0x64/0x65` | TOFF_DELAY / TOFF_FALL | shutdown timing |

The full configuration (rail order, delays, limits, fault responses) lives in
`uFCv2/template/POWER/uFCv2.tifsp`; the device holds it in flash, so the host
only needs `PAGE` + `OPERATION` / read commands.

## Live state on the bench (uFC in AMC7)

Probed over the generic I²C command (`0x20`, mux 0x70 ch0, dev 0x11):

| Page | OPERATION | READ_VOUT | VOUT_COMMAND | Note |
|---|---|---|---|---|
| 0–15, 18, 19 | `0x00` | 0 V | 0 | off |
| 16 | `0x00` | **6.249 V** | 6.0 V | 12V0 through a 2:1 divider (~12.5 V) |
| 17 | `0x00` | **3.279 V** | 3.3 V | CLK_3V3 (management) |

`STATUS_WORD` reads `0xD015` on every page (fault summary while the rails are
off). So the sequencer is **inhibited**: 12 V and 3.3 V aux are present, all
sequenced payload rails are off — that is why the FPGA and the FMC/DDR supplies
are down.

## Turning rails on safely

1. Read `ON_OFF_CONFIG` for each page to confirm OPERATION is honoured.
2. `CLEAR_FAULTS` (0x03) to start from a clean status.
3. Write `OPERATION = 0x80` **per page** (or in the order the tifsp defines);
   the device applies the configured `TON_DELAY`/`TON_RISE` sequence itself.
4. Poll `STATUS_WORD` / `READ_VOUT` until the rails are good.
5. To stop: `OPERATION = 0x40` (soft off, sequenced) or `0x00` (immediate).

The same access is available from the Toolbox **I²C transfer** tab
(mux 0x70 ch0, device 0x11, write-then-read), so no firmware change is needed
for rail control — but enabling the payload rails is a hardware action and is
done only on request.
