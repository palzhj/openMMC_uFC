# Documentation archive

Dated snapshots of the project documentation, taken when the docs are revised
so an earlier state can always be recovered without walking the git history.

| snapshot | contents |
|---|---|
| `2026-11-10/` | top-level README, clock/CONFIG/SWITCH, USER_MANUAL, I2C_ACCESS, DEBUGGING, UCD90320_NOTES, the toolbox and per-board firmware READMEs, build/readme — after the ADN4604/clock-store/GUI refresh |
| `MANIFEST-2026-11-10.txt` | file list and sizes of that snapshot |
| `openMMC_uFC-docs-2026-11-10.tar.gz` | the same files, packed |

Files inside a snapshot directory are stored flat, with the original path's
separators replaced by `__` (e.g. `docs__CLOCK_SWITCH.md`).

To take a new snapshot, copy the current documents here under a new date (and
add a manifest + tarball) — the file list to copy is in the manifest of the
previous snapshot.
