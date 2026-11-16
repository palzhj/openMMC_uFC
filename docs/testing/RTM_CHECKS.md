# RTM acceptance checks — no module / uRTM v2.3 / carrier

Run after any change to `rtm.c`, `rtm_fru.c`, `rtm_sensors.c` or the payload
hand‑off.  Two of the three scenarios need someone at the crate; the "carrier"
one is the state AMC6 is in today.

Build/flash first (`tools/firmware/u4fcp-v2/jlink_program.sh`), then note the
image under test with `ipmitool … raw 0x32 0x02`.

Manager state (if you have SWD) — the addresses move between builds:

```sh
arm-none-eabi-nm .hw2/u4fcp/out/openMMC.elf | grep -E "rtm_(state|12v_on|blocked|mp_forced)"
# 0 = OFF, 1 = WAIT_AMC, 2 = POWER, 3 = SETTLE, 4 = IDENTIFY,
# 5 = MANAGED, 6 = UNMANAGED
```

---

## A. No RTM plugged (`RTM_PS#` high)

| check | expected | how |
|---|---|---|
| manager state | `RTM_OFF` (0), `rtm_12v_on` = 0 | SWD symbols |
| 12 V rail | never commanded: `OPERATION` page 1 = 0x00, `PGb` (pin 78) = 1 (not good) | `raw 0x32 0x20 … 0x70 0x07 0x11 0x01 0xFA 0x4E 0x00` then `0xFB` |
| MP3V3 | LGPO14 (pin index 69) driven low once (`rtm_mp_forced` = 1) | pin read |
| RTM bus | **no** traffic to 0x70 ch5: no 0x55, no 0x71 | the manager never calls `rtm_fru_probe()` while `plugged == false` |
| SDRs | 26 records only; **no** 0x1B–0x23, no entity 0xC0 | `ipmitool … sdr list` / `sensor list` |
| shared bus | AMC FRU read and UCD sensors stay healthy (no wedge) | `raw 0x32 0x15 0x54 0 0 8` repeatedly, `sensor list` |
| log | `rtm : RTM unplugged` once if it was previously plugged | UART |

## B. uRTM v2.3 plugged (`RTM_PS#` low, FRU says uRTM/2.3)

| check | expected | how |
|---|---|---|
| 12 V | on **1 s** after PS# seen low; `PGb` = 0 (good), `FLTb` (pin 14) = 1 | pins 26/78/14 |
| FRU | at 0x55 via `0x71 ch7`, product `uRTM`, version `2.3` | manager log `rtm : FRU 0x55: uRTM v2.3` |
| state | `RTM_MANAGED` (5) | SWD |
| SDRs | nine records 0x1B–0x23 inserted (entity 0xC0 inst 0); MCH needs a slot activation to show them | `sdr list` after `shutdown`/`fru_start` |
| monitors | FMC2/FMC3 12 V, 3V3, VADJ readings update once per second | `sensor list`, `show_sensorinfo <fru>` on the MCH |
| handle window | first **10 s** after power: RTM_INT# ignored | — |
| handle open | after the window, 2 consecutive low passes → `rtm : RTM handle open, 12 V off`; stays off until re‑plug or payload cycle; SDRs kept, readings unavailable | open the handle |
| fault | `MAR15`/`LGPO16` low → `rtm : RTM power fault, 12 V off` | force a fault if possible |
| unplug | 12 V off, SDRs kept (readings unavailable) | pull the module |
| MP3V3 | LGPO14 (pin index 69) low throughout (**never** raised — uRTM v2 `RTM_MODE` would trip the AMC's MP3V3 OC) | pin read |
| RTM_RES | PA2 low throughout (RTM‑mode indication for the uRTM MCU) | `mem32 0x4001080C` bit 2 |

## C. LattePanda carrier (blank FRU) — current AMC6 state

| check | expected | observed 2026‑11‑15/16 |
|---|---|---|
| 12 V | on 1 s after PS#, `PGb` = 0 | ✔ |
| FRU | 0xFF → `RTM_FRU_OTHER` → `rtm : FRU 0x55: other RTM, no FMC monitors` | ✔ (state `UNMANAGED`) |
| SDRs | none added; a previously inserted uRTM group is removed | ✔ |
| bus | carrier `0x55`/`0x3C` answer, no wedge | ✔ |
| load | 0 A — the Mu never asserts `POW_EN`, so the carrier's LTM4671 stays off | open (power‑button check) |

### Result: scenario A on AMC6 (2026‑11‑16, carrier removed)

| check | measured |
|---|---|
| `RTM_PS#` (PA8) | **high** — reads high with nothing plugged (the module was what pulled it low) |
| manager | `rtm_state = 0` (OFF), `rtm_12v_on = 0`, `rtm_blocked = 0`, `rtm_mp_forced = 1` |
| 12 V rail | `PGb` (pin 78) = `0x08` **not good**, `OPERATION` page 1 stays off; no `rtm : RTM 12 V on` in the log |
| MP3V3_EN | pin 69 `0x03` — driven low once at boot |
| `RTM_RES` | PA2 low (`ODR = 0xA002`) |
| SDRs | 26 AMC records; no 0x1B–0x23, no FMC2/FMC3 rows; MCH shows 27 rows incl. the hotswap |
| shared bus | AMC FRU read 3/3 identical, UCD sensors updating, no wedge |
| stability | state and pins unchanged over a 10 s watch (no spurious power) |

### Result: scenario B on AMC6 + uRTM v2.3 (2026‑11‑16)

| check | measured |
|---|---|
| 12 V | on after 1 s, `PGb` (pin 78) = `0x00` good, `FLTb` clear, `RTM +12V Curr` ≈ 1.2–1.3 A |
| identification | manager `rtm_state = 5` (**MANAGED**), `rtm_12v_on = 1` |
| SDRs | 35 records in the MMC (26 AMC + 9 RTM, 0x1B–0x23, entity 0xC0/0x00) |
| RTM readings | `RTM TEMP UCD` 25.0 °C, `FMC2/FMC3 +12V` 0.45/0.26 A, `+3.3V` 0.00/0.19 A, `VADJ` 1.79/1.81 V — all `ok` |
| MCH view | after `shutdown 10` + `fru_start 10` it re‑read the repository and lists **36 rows** (1–26 AMC, 27–35 RTM, 36 hotswap) |
| MP3V3 / RTM_RES | low throughout |

**The MCH keeps a cached copy of the module's SDR repository.** When the RTM
group appears (or disappears) the MCH shows the old set until a FRU activation:
`shutdown <fru>` + `fru_start <fru>` (or an MCH cold reset).  The re‑read is
slow — the table fills in over ~1–2 min, so a count taken immediately after
`fru_start` is short (31 of 36 in one run) and is not a fault.

## Notes from the first run (AMC6, 2026‑11‑16)

- **“No RTM” cannot be simulated electrically**: driving `RTM_PS#` (PA8) high as
  a push‑pull output does not make it read high — the module ties that line
  hard to ground, so the pin only sources current and the manager keeps seeing
  “plugged”.  Unplug the module instead (simulated attempt: PA8 high for 3.5 s,
  manager stayed `UNMANAGED`, rail stayed on; pin restored to input afterwards).
- **EN pins cannot be trusted through `GPIO_CONFIG`**: reading `0xFB` for
  `EN3` (pin 26) returns `0x03`/`0x08`/`0x0B` depending on how the pin was last
  addressed.  Use the hotswap `PGb` (pin 78) as the rail indicator.
- Scenario C ran clean: 12 V on with `PGb` good, carrier `0x55` answering,
  manager `UNMANAGED`, no bus wedge.

## Cleanup after a session

- Re‑flash the production image; `raw 0x32 0x02` matches `git log -1`.
- If the UCD was poked by hand (pins/OPERATION), re‑flash **and** power‑cycle the
  AMC so the UCD reloads its project defaults.
