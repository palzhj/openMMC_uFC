# Clocking description file (`ufc-clocking.json`)

A single JSON file describes the uFC clock tree: the **points** (device,
signal name, package pin, direction) and the **connections** between them,
plus the mutable 8V54816A routing. The toolbox can draw it, the validator
checks it, and the routing converts to the 16-byte switch matrix.

File: [tools/clocking/ufc-clocking.json](../tools/clocking/ufc-clocking.json)
Loader/validator: [tools/clocking/clocking.py](../tools/clocking/clocking.py)

## Structure

```json
{
  "format": "ufc-clocking",
  "version": 1,
  "meta":    { "board": "...", "sources": [...], "notes": [...] },
  "devices": { "mux": {...}, "pll": {...}, "k7": {...} },
  "points":  [ {...}, ... ],
  "links":   [ {...}, ... ],
  "routing": { "mux.CLK11": {"source": "mux.CLK7", "term": true, "inverted": false} }
}
```

### `devices`

| Field | Meaning |
|---|---|
| `part` | device type / part number |
| `bus`, `mux`, `addr` | I²C location for programmable devices (`mux` is a list of `[address, channel]` stages) |
| `freq` | oscillator frequency (informational) |

### `points`

| Field | Meaning |
|---|---|
| `id` | `device.signal`, unique |
| `device` | key into `devices` |
| `name` | signal name at the device |
| `signal` | net name (optional, e.g. `AMC_TCLKD`) |
| `pin` | package/connector pin (optional; P/N pairs as `"6/7"`) |
| `dir` | `in`, `out` or `inout` |

### `links`

| Field | Meaning |
|---|---|
| `a`, `b` | point ids |
| `kind` | `forward` (a is the driver) or `bidir` |
| `verified` | `false` marks a link that still needs a schematic check (the validator warns) |

Rules checked by the validator: `forward` needs `a.dir ∈ {out,inout}` and
`b.dir ∈ {in,inout}`; `bidir` needs at least one `inout` end; endpoints and
devices must exist.

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

`check` prints errors (fatal) and warnings (unverified links), `matrix`
prints the 16 bytes that would be written to `0x58`, `dot` renders the file
as a Graphviz diagram (bidirectional links are green, unverified ones dashed,
routing edges in orange).

## Data provenance

Pins come from the FPGA constraint files (`uFCv2/template/{S7,K7}/top.xdc`)
and the 8V54816A connector pin map ([CLOCK_SWITCH.md](CLOCK_SWITCH.md)); the
Si5345 and FMC points are named nets from the schematic. Links marked
`"verified": false` are the ones not yet confirmed against `UFCV2.pdf`.
