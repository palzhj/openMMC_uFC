# Clocking description file (`ufc-clocking.json`)

A single JSON file describes the uFC clock tree: the **points** (device,
signal name, package pin, direction) and the **connections** between them,
plus the mutable 8V54816A routing. The toolbox can draw it, the validator
checks it, and the routing converts to the 16-byte switch matrix.

File: [tools/clocking/ufc-clocking.json](../tools/clocking/ufc-clocking.json)
Loader/validator: [tools/clocking/clocking.py](../tools/clocking/clocking.py)

The u4FCP boards carry their own files: `u4fcp-v2-clocking.json` (ADN4604,
Si5345 @0x68/0x69, clock store in the EEPROM) and `u4fcp-v1-clocking.json`.
The v2 file is **partial**: it defines the switch/PLL/store blocks and the 16
ADN4604 ports with their directions (from `u4FCPv2/readme/clock.png` and the
firmware `ADN4604_IN_*` defines), but the package pins and the net links are
still to be transcribed from the ADN4604 schematic page. The toolbox uses the
points for the mux table names/directions; the drawing falls back to the
built-in template without links.

## Structure

```json
{
  "format": "ufc-clocking",
  "version": 1,
  "meta":    { "board": "...", "sources": [...], "notes": [...] },
  "boards":  { "ufc": {"label": "uFC v2.0", "class": "board"} },
  "devices": { "mux": {"parent": "ufc", "class": "chip", "box": [190, 40, 280, 520], ...} },
  "points":  [ {"id": "mux.CLK11", "parent": "mux", "name": "CLK11", "pin": "69/70", "dir": "out"} ],
  "links":   [ {"a": "amc.TCLKA", "b": "mux.CLK2", "kind": "bidir"} ],
  "routing": { "mux.CLK11": {"source": "mux.CLK7", "term": true, "inverted": false} }
}
```

### Hierarchy: board → chip → port

| Level | Key | `parent` points to |
|---|---|---|
| **board** | `boards` | — |
| **chip / connector / oscillator** | `devices` | a board (`"parent": "ufc"`) |
| **port** | `points` | a device (`"parent": "mux"`) |

`devices[].class` is `chip`, `connector` or `oscillator` (the renderer colours
them accordingly). `devices[].box` = `[x, y, w, h]` is the canvas box used by
the toolbox drawer; **points with the same parent are drawn inside that
box** — all 16 CLK ports inside the 8V54816A box, the 18 K7 clocks inside the
Kintex box, etc.

### `devices`

| Field | Meaning |
|---|---|
| `parent` | owning board |
| `class` | `chip` / `connector` / `oscillator` |
| `label` | box label |
| `box` | `[x, y, w, h]` drawing box |
| `part` | device type / part number |
| `bus`, `mux`, `addr` | I²C location for programmable devices (`mux` is a list of `[address, channel]` stages) |
| `freq` | oscillator frequency (informational) |

### `points`

| Field | Meaning |
|---|---|
| `id` | `device.signal`, unique |
| `parent` | owning device (chip) |
| `device` | key into `devices` |
| `name` | signal name at the device |
| `capability` | what the point **can** be configured as (see below) |
| `signal` | net name (optional, e.g. `AMC_TCLKD`) |
| `pin` | package/connector pin (optional; P/N pairs as `"6/7"`) |
| `dir` | `in`, `out` or `inout` |

### `capability` — input / output / tristate

```json
"capability": {"in": true, "out": true, "tristate": true, "default": "in"}
```

| Key | Meaning |
|---|---|
| `in` | can be configured as an input |
| `out` | can be configured as an output |
| `tristate` | the output can be disabled (high‑Z / tri‑state) |
| `default` | safe/default configuration (`in`, `out` or `tristate`) |

The current `dir` must be allowed by the capability (the validator enforces
this). Examples from the file:

| Point | `dir` | capability |
|---|---|---|
| `mux.CLK0` (AMC TCLKD) | inout | in/out/tristate |
| `mux.CLK7` (156M) | in | in only |
| `mux.CLK9` (PLL in) | out | out/tristate |
| `mux.CLK12` (FMC clock) | inout | in/out/tristate |
| `k7.REFCLK_P` | in | in only |
| `s7.CLK_INOUT_P` | inout | in/out/tristate |

The toolbox uses the mux capability to decide which directions are legal in
the port table (CLK9‑11 output only, CLK7/8 input only, the rest in/out).

### `links`

| Field | Meaning |
|---|---|
| `a`, `b` | point ids |
| `verified` | `false` marks a link that still needs a schematic check (the validator warns) |

There is **no `kind`**: the arrow direction is derived from the points'
`dir`:

| `a.dir` | `b.dir` | drawing |
|---|---|---|
| out | in | arrow a → b |
| in | out | arrow b → a |
| inout (either end) | | line without arrow (bidirectional) |
| in/in | | dashed line, warning “no driver” |
| out/out | | dashed line, warning “two drivers” |

### `routing`

Only the 8V54816A output ports (`mux.CLK9` … `mux.CLK15`):

```json
"mux.CLK11": {"source": "mux.CLK7", "term": true, "inverted": false}
```

`source: null` = not routed. The conversion to the register byte is:

```
byte = 0x80 | source_port | (term ? 0x40 : 0) | (inverted ? 0 : 0x20)
```

so the example above is `0xE7`.

## CLI

```bash
python3 tools/clocking/clocking.py check  tools/clocking/ufc-clocking.json
python3 tools/clocking/clocking.py matrix tools/clocking/ufc-clocking.json
python3 tools/clocking/clocking.py dot    tools/clocking/ufc-clocking.json > clocking.dot
```

`check` prints errors (fatal) and warnings (unverified links, links without
a driver or with two drivers), `tree` prints
the board → chip → port hierarchy, `matrix` prints the 16 bytes that would be
written to `0x58`, `dot` renders the file as nested Graphviz clusters (board →
chip → ports; bidirectional links green, unverified dashed, routing orange).

The toolbox `System diagram` reads the same file and draws one box per
device, with its ports inside; if the file is missing it falls back to the
built-in layout.

## Data provenance

Pins come from the FPGA constraint files (`uFCv2/template/{S7,K7}/top.xdc`)
and the 8V54816A connector pin map ([CLOCK_SWITCH.md](CLOCK_SWITCH.md)); the
Si5345 and FMC points are named nets from the schematic. Links marked
`"verified": false` are the ones not yet confirmed against `UFCV2.pdf`.
