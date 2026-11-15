# Incident 2026-11-15 — AMC1 (SL03) went silent after an OTA

Status: **open**, module needs a physical reflash.  AMC12 was not affected.

## Timeline (all times +08:00)

| time | event |
|---|---|
| 19:41 | first OTA of AMC1 to `954c265` (60384 B, HPM component 1, target `0x72`): `finish 00`, `activate 00`, MMC came back, `raw 0x32 0x02` = `954c265…`, 26 sensors, FRU and clock store intact |
| 22:33 | OTA of AMC1 to `c7d774f` (60576 B, same procedure): `finish 00`, `activate 00` |
| 22:34 | AMC1 stops answering IPMB: `raw 0x06 0x01` → `rsp=0xc3` timeout; AMC12 answers normally |
| 22:36 | MCH still lists `HS 005 AMC1 0x10 ok` (same reading as the working AMC12) |
| 22:42 | `chassis power cycle` on the MCH (`System Power: on`, restore policy `always-on`) |
| 22:43 | AMC12 is back, AMC1 still times out |
| 22:45 | MCH no longer lists a hotswap row for AMC1 — the module is unmanaged from the MCH's point of view |

## Evidence

```
AMC1   raw 0x06 0x01  -> Unable to send RAW command (rsp=0xc3): Timeout
AMC12  raw 0x06 0x01  -> 00 80 01 01 02 29 5a 31 00 00 00     (bridge is fine)
MCH    sdr list       -> HS 016 AMC12 0x10 ok   (AMC1 row gone after the cycle)
```

The MMC runs from management power, so a chassis (payload) power cycle does not
reset it — the module stayed silent across the cycle.

## What was written to the module

`c7d774f` image, 60576 B, built in the long-lived `.hw2/u4fcp` build tree.
A **clean** build of the same commit (`git archive HEAD` into an empty tree) is
**59968 B** — i.e. the long-lived tree did not reproduce a clean build
(608 B difference, cause unexplained; both images have the RTM boot dump
compiled out).  The image was therefore not bit-reproducible, which is one of
two plausible causes:

1. the staged image was damaged between the HPM upload and the bootloader copy
   (the upload path has no read-back verification; `finish` only accepts the
   size), so newboot's copy check failed and the module stays in the
   bootloader, which does not speak IPMI; or
2. the application itself does not boot (crash before IPMB init → IWDG loop).

The RTM code is unlikely to be the cause: with no module in Zone 3, RTM_PS# is
high, so the new code never touches the RTM bus and never enables its 12 V.

## Follow-up (2026-11-16)

The 608 B size difference is most likely **not** a build-tree problem: images
embed `__FILE__` (via `configASSERT`/`PRINT_ERR_LINE`), so a build in
`/tmp/clean` (28-char paths) is systematically smaller than one in
`/home/iheper/work/openMMC_uFC` (43-char paths) — ~15 B per embedded path, 7+
paths.  A fresh `.hw2` rebuild reproduces the long-lived size exactly, and the
OTA path itself was verified twice on AMC6 (`ceb7266` → `e4ed725` →
`ceb7266`…), see `docs/testing/OTA_CHECKS.md`.  So the failure is more likely
the unverified staging copy or the image itself than the build directory.

## Recovery

The module needs a physical reflash; a payload power cycle does not help and
would not either if the staged copy is bad (newboot would retry the same
image).

Recovery image: a **clean** build of `c7d774f`, parked in
`.hw2/recovery/{newboot.bin,openMMC.bin,openMMC.elf}` (59968 B).  Or rebuild:

```sh
rm -rf /tmp/clean && mkdir /tmp/clean && git archive HEAD | tar -x -C /tmp/clean
cmake -S /tmp/clean -B /tmp/clean/b -DBOARD=u4fcp-v2 \
      -DTARGET_CONTROLLER=STM32F103CBT6 -DCMAKE_BUILD_TYPE=Release
make -C /tmp/clean/b newboot openMMC -j
```

SWD (probe on the module; writes newboot at 0x08000000, the app at
0x08002000 and clears `fw_info`, so a half-finished staged update cannot
replay):

```sh
tools/firmware/u4fcp-v2/jlink_program.sh --bin-dir "$PWD/.hw2/recovery"
```

Then verify, over the MCH bridge:

```sh
B="-I lan -H 192.168.1.41 -A none -T 0x82 -B 0 -t 0x72 -b 0"
ipmitool $B raw 0x32 0x02     # expect the SHA1 of the flashed build
ipmitool $B sdr list          # 26 sensors, no FMC2/FMC3 rows
ipmitool $B fru print         # serial SL03
```

If the probe is not on the module, pulling it (or cutting the shelf's
management power) resets the MMC; if the staged image is the problem, the
module will then still need the SWD flash above.

## Precautions for the next OTA

- Build the image from a **clean** tree and check it reproduces the expected
  size before uploading (`git archive HEAD | tar -x -C …`); do not reuse a
  long-lived build directory for an image that goes over the air.
- The application region is 60928 B; the linker `ASSERT` in
  `linker/STM32F103CBT6_app.ld` now refuses an image whose `.data` load image
  would spill into staging, but it cannot catch a damaged transfer.
- Keep physical (SWD) access to the target before starting an OTA — this
  module had none, and recovery now needs a site visit.
- Optionally split the update: `hpm_upload.py --no-activate`, compare the
  component properties, and only then `ipmitool … raw 0x2c 0x35 0x00`.  That
  does not verify the staged bytes, but it keeps the running image until
  someone has confirmed the module is reachable.
