# OTA (HPM.1) acceptance checklist

Firmware update over IPMI through the MCH.  HPM.1 runs on NetFn **0x2C**
(PICMG group extension `0x00`), components **0 = bootloader, 1 = MMC (this
image), 2 = payload** — the payload component is not upgradable here.

The upload writes the **staging** area (`0x08010E00`, 60928 B); `newboot`
copies it over the application on `activate` and clears `fw_info`.  There is
**no read‑back verification** of the staged image: `finish` only checks the
size.  A bad staging copy means the module comes back in `newboot`, which does
not speak IPMI — see `docs/testing/INCIDENT-2026-11-15-amc1-ota.md`.

---

## 0. Pre‑flight (do not skip)

| check | command | pass |
|---|---|---|
| clean tree | `git status --short` | empty |
| built from a **clean** tree | `rm -rf /tmp/clean && mkdir /tmp/clean && git archive HEAD \| tar -x -C /tmp/clean` then configure/build there | image **byte‑identical** to `.hw2/u4fcp/out/openMMC.bin` |
| image fits the slot | `arm-none-eabi-size .hw2/u4fcp/out/openMMC.elf` | text+data ≤ 60928 |
| image under test | `strings out/openMMC.bin \| grep -m1 -E '^[0-9a-f]{40}$'` | matches `git log -1` |
| module answers | `ipmitool … raw 0x06 0x01` | completion 0x00 |
| sensors sane | `ipmitool … sensor list` | 26 (AMC only) / 35 (with uRTM) |
| own FRU readable | `ipmitool … raw 0x32 0x15 0x54 0x00 0x00 0x08` | `01 00 00 01 …` |
| clock store | `ipmitool … raw 0x32 0x18` | valid, expected record count |
| backups | `program_clock.py --dump`, FRU image | stored outside the module |

A long‑lived build directory once produced an image 608 B larger than a clean
build of the same commit, and that image did not boot (AMC1 incident).  Compare
sizes before every OTA — but note the images are **not byte‑reproducible across
different build paths**: `configASSERT`/`PRINT_ERR_LINE` embed `__FILE__`, so a
build from `/tmp/...` is systematically smaller than one from
`/home/iheper/work/openMMC_uFC/...` (~15 B per embedded path).  Compare the
*symbol sizes* (`arm-none-eabi-nm -S`) or build both in the same path.

## 1. Upload

```sh
cd tools/firmware/u4fcp-v2
./ota.sh --slot 6 --mch 10.19.198.31          # build, upload, activate, verify
./ota.sh --slot 6 --mch 10.19.198.31 --check  # pre-flight only
```

`ota.sh` builds the image (unless `--no-build`/`--bin-dir`), refuses images
larger than 60928 B, warns when the image SHA differs from `HEAD`, uploads via
`tools/uFC_toolbox/hpm_upload.py`, waits for the MMC and compares the reported
SHA1 with the image's.  The raw command it wraps:

```sh
B="-I lan -H <mch> -A none -T 0x82 -B 0 -t <0x70+2*slot> -b 0"
python3 tools/uFC_toolbox/hpm_upload.py .hw2/u4fcp/out/openMMC.bin \
        --mch <mch> --target <0x70+2*slot> --transit 0x82 --bus 0 --component 1
```

AMC6 → slot 6 (`0x7C`), AMC1 → slot 1 (`0x72`), AMC12 → slot 12 (`0x88`).
Note the MCH address: this crate is `10.19.198.31`, the scripts default to
`192.168.1.41`.

| stage | expected |
|---|---|
| capabilities `0x2E` | non‑empty, bit for the component set |
| properties `0x2F` | component 1 present, selector 0 |
| initiate `0x31` | `00` (or `C0` = in progress, continued by the tool) |
| blocks | ~3000 × 20 B, **≈5 min** (~95 ms/block), progress every 200 blocks |
| finish `0x33` | `00` — anything else means the image was rejected (keep the old one) |
| activate `0x35` | `00`; the MMC reboots into the staged image |

## 2. Post‑checks (wait ~10 s)

| check | expected |
|---|---|
| `raw 0x32 0x02` | the SHA of the image just uploaded |
| boot | module answers IPMB again, `raw 0x06 0x01` = `00 …` |
| sensors | the same count as before, all `ok` |
| FRU | `fru print` shows the same serial/part |
| clock store | `raw 0x32 0x18` valid, same contents |
| I²C health | AMC FRU read + UCD sensors stable for ≥1 min (no wedge) |
| no reboot loop | read `raw 0x32 0x02` again after 30 s; identical, and the MMC uptime implied by the logs keeps growing |
| RTM | manager in the state matching Zone 3 (no module → `OFF`, carrier → `UNMANAGED`, uRTM → `MANAGED`) |

The MCH caches the module SDR repository: if the sensor set changed, re‑activate
the slot (`shutdown <fru>` + `fru_start <fru>`).

## 3. Failure paths (must leave a working module)

| injected failure | expected |
|---|---|
| upload aborted before `finish` | old image keeps running; staging garbage ignored |
| `finish` with a wrong length | rejected; old image keeps running |
| image larger than the staging area | `finish` rejected at initiate/finish |
| `activate` with no valid staging | module reboots into the old image |
| payload power lost during upload | staging invalid, old image intact |
| payload power lost during the bootloader copy | `newboot` retries on the next boot; if it cannot recover, the module stays in the bootloader → **SWD reflash** |
| MMC reset mid‑upload | staging invalid, old image intact |

## 3a. Result of the first end‑to‑end run (AMC6, 2026‑11‑16)

| step | result |
|---|---|
| `e4ed725` → `ceb7266` (60188 B) | uploaded in 285 s, `finish 00`, `activate 00`, SHA read back `ceb7266`, 26 sensors, FRU and clock store intact |
| `ceb7266` → `e4ed725` (60284 B) | uploaded in 290 s, `finish 00`, `activate 00`, SHA read back `e4ed725`, 26/26 sensors ok, FRU serial TA8, clock store unchanged, SHA still `e4ed725` after 30 s (no reboot loop) |
| `e4ed725` → `7e75f57` via `ota.sh` (60284 B) | 260 s, verified automatically: SHA `7e75f57`, 26 sensors, FRU TA8 |
| RTM after the OTA | manager `UNMANAGED` (carrier), `rtm_12v_on = 1`, MP3V3_EN (pin 69) `0x03`, EN3 (26) `0x08`, `PGb` (78) `0x00` good, `FLTb` (14) high, carrier `0x55` ACK, `RTM_RES` low |

The image really is replaced by the HPM path (SHA changes and comes back), and
the module survives two consecutive OTAs.

## 4. Recovery when the module goes silent

The MMC runs from management power, so a payload power cycle does **not** reset
it.  Recover over SWD:

```sh
cd tools/firmware/u4fcp-v2 && ./jlink_program.sh   # newboot + app, fw_info cleared
```

Keep a clean‑build recovery image outside the tree (`docs/testing/INCIDENT-…`
used `.hw2/recovery/`).  Prefer SWD for any module you cannot reach physically.
