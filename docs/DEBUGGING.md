# uFC‑v2 bring‑up — debugging log and root‑cause analysis

This document records the complete bring‑up of the **uFC v2.0** AMC module
(STM32F100CBT6) from a non‑booting firmware to a verified, versioned release.
It is written so that the same class of failures can be recognised quickly on
other boards.

* **Board:** uFC v2.0, STM32F100CBT6 (Cortex‑M3 value line, 128 KB flash, 8 KB SRAM)
* **Boot chain:** OpenBLT `0x08000000` → newboot `0x08002000` → openMMC `0x08004000`
* **Bench:** N6705C CH4 (12 V), USART1 115200, OpenBLT XCP over UART, Renode
* **Toolchain:** arm‑none‑eabi‑gcc 14.2.1, CMake 4.2.3

---

## 1. Symptom → cause → fix overview

| # | Symptom | Root cause | Fix |
|---|---|---|---|
| C1 | Unsure which MCU was fitted | Part marking alone is unreliable | Read `DBGMCU_IDCODE` + flash‑size register in firmware |
| C2 | Board completely silent on the console | App never executed (see C2a–C2d) | — |
| C2a | Bootloader reported “jump” but nothing ran | Newboot startup lacked the OpenBLT vector‑table checksum word at `0x080021D0` | Added `0x55AA11EE` slot |
| C2b | Jump to app landed on a bogus address | `bootloader_jump_to_app()` used `*(app_start_addr+1)` (undefined behaviour); at `-Os` GCC loaded the stack pointer as the entry point | Volatile vector loads |
| C2c | App erased immediately after flashing | `fw_info` held the “pending update” magic `0xAAAAAAAA` with an empty staging area | Cleared `0x0801FF00–0x0801FFFF` to `0xFF` |
| C2d | Still no output after the jump was fixed | Deviations from the known‑good u4FCP port: `SystemInit` early‑return, `uart_send` TXE instead of TC, pre‑`pin_init` printf, `-Os` | Restored u4FCP parity (`-Og`, standard `SystemInit`, TC flush) |
| C3 | First I2C transaction to the TCA9548A NACKs | Mux needs several attempts right after `i2c_init()` | Retry the channel‑select write (50×, busy wait) |
| C4 | FRU write printed “Writing…” but the EEPROM kept old data | `eeprom_24xx02_write()` called `vTaskDelay()` from `fru_init()`, i.e. before the scheduler started; and waited 1 ms while the 24AA025E48 needs 5 ms | Busy 5 ms wait + NACK retry cap |
| C5 | FRU read invalid on a cold boot (“COMMON HEADER checksum”) | The whole bus *take* failed (`i2c_take_by_chipid`), not just the read; the retry only covered the read | Retry take+read together |
| C6 | With the watchdog enabled the app hung after the FRU check | Heap exhausted: only **200 B** free, so `vTaskStartScheduler()` could not create the idle task (~470 B) | Trim task stacks (IPMI/PAYLOAD 176, LED 96, watchdog 48) → 592 B free |
| C7 | FRU contained a Zone 3 record on a board without Zone 3 | Builder always emitted the MTCA.4 Zone 3 compatibility record, which also carried the end‑of‑list bit | `AMC_FRU_INCLUDE_ZONE3_RECORD` + retag EOL on the P2P record |
| C8 | CMake 4.2 refused to configure; builds dirtied the source tree; u4FCP target was the 64 KB part | `cmake_minimum_required(3.0.0)`; `GitSHA1.c` generated in‑source; wrong default controller | Minimum 3.10; generate into the build dir; default `STM32F103CBT6` |

---

## 2. Case studies

### C1 — Which MCU is on the board?

Reading the part marking is not sufficient (many STM32F103C8/CB parts share a
die). The firmware prints, before jumping to the application:

```
FLASH_KB=128 DEV_ID=10032 REV_ID=116
```

* `FLASH_KB` — `*(uint16_t*)0x1FFFF7E0`
* `DEV_ID` — `DBGMCU_IDCODE & 0xFFF` at `0xE0042000` (0x420 = STM32F10x value line)
* 128 KB flash + value line ⇒ **STM32F100CBT6**

### C2a — The OpenBLT vector‑table checksum

OpenBLT verifies the application's vector table using a checksum stored at
`BOOT_FLASH_VECTOR_TABLE_CS_OFFSET (0x1D0)` inside the vector table
(absolute `0x080021D0` for an app/newboot at `0x08002000`). The bootloader
overwrites that word after every update. A newboot built from a plain startup
file has no such slot, so OpenBLT refuses to start it.

The startup file gets 44 zero words + `0x55AA11EE` appended, which places
`0x55AA11EE` exactly at `0x080021D0`. Verified with `objdump`:

```
080021d0:  ee11aa55
```

### C2b — Undefined behaviour in the jump

```c
/* broken */
app_sp = *(uint32_t*)app_addr;
app_pc = *(uint32_t*)(app_addr + 1);   /* wrong: (addr+1), not vector[1] */
```

At `-O3`/`-Og` the compiler happened to produce working code; at the uFC‑v2's
`-Os` it loaded the **stack pointer** as the jump target. Fixed with explicit
volatile vector reads:

```c
volatile uint32_t *vec = (volatile uint32_t *)app_addr;
uint32_t app_sp = vec[0];
uint32_t app_pc = vec[1];
__set_MSP(app_sp);
((pfun)app_pc)();
```

The same latent bug exists in the sibling u4FCP tree, which only worked because
of its optimisation level. **Lesson:** never rely on a pointer cast that
violates effective type + alignment rules; read vectors explicitly.

### C2c — `fw_info` pending‑update flag

`0x0801FF00` is a 256‑byte `fw_info` block; magic `0xAAAAAAAA` at
`0x0801FF04` means “update pending”. If the staging area is empty, newboot
erases the application and then has nothing to copy. Clearing the block to
`0xFF` (`clear_fwinfo.srec`) restored normal booting.

### C2d — Parity with the known‑good port

After the jump and checksum were fixed the application still produced no
output. Comparison with the working u4FCP port exposed four self‑inflicted
deviations, all introduced while debugging:

| Deviation | Why it broke |
|---|---|
| `SystemInit` early‑return when the PLL was already locked | Masked a failure path and skipped the vector‑table offset setup in some boots |
| `uart_send` waiting for TXE instead of TC | Bytes were lost when the board was powered down mid‑transmission |
| printf before `pin_init` | Clocks/GPIO not configured yet |
| `-Os` | Exposed the jump UB above |

Reverting to the reference port's behaviour (`-Og`, standard `SystemInit`, TC
polling) made the application run. **Lesson:** change one variable at a time;
when a known‑good reference exists, diff against it before inventing fixes.

### C3 — TCA9548A first‑transaction NACK

A direct scan (channel select + zero‑length write per device) showed:

```
[I2C] TCA9548A mux @0x70: NACK (tries=13)
[I2C] 24AA025E48 FRU @0x50 (ch0): ACK
[I2C] 8V54816 @0x58 (ch2): ACK
[I2C] SI5345A @0x69 (ch2): ACK
```

The mux’s first writes consistently NACK; the later channel selects succeed,
proving the mux is present. Fixed by retrying the channel‑select write inside
`i2c_set_mux_bus()` (50 attempts, short busy wait, no `vTaskDelay` because it
can run before the scheduler).

### C4 — The FRU write that silently did nothing

`fru_init()` is called from `main()` **before** `vTaskStartScheduler()`.
`eeprom_24xx02_write()` used `vTaskDelay(pdMS_TO_TICKS(1))` between pages:

* `vTaskDelay()` before the scheduler is not valid, and
* 1 ms is far below the 24AA025E48 internal write cycle (5 ms).

Result: writes were NACKed and retried forever, the app stopped after
`Writing FRU info to EEPROM…`, and the EEPROM kept its factory content.

Fix: busy‑wait 5 ms per page and cap the NACK retries:

```c
for (volatile uint32_t d = 0; d < 60000; d++) { }
```

### C5 — Cold‑boot FRU read failures

The production image validated the FRU on the first I²C access after reset and
reported a bad header checksum. Instrumenting `eeprom_24xx02_read()` showed:

```
[EEPROM] take=0 addr=0x50 if=0 rx=0
```

The **bus take** failed (`i2c_take_by_chipid` → mux select), so the read was
never attempted. The retry loop originally wrapped only the read, inside the
`if (take)` guard. Restructured to retry the take+read together, which fixed
the cold‑boot validation.

### C6 — Heap budgeting with the watchdog

Enabling `MODULE_WATCHDOG` added a 60‑word task and TCB. `xPortGetFreeHeapSize()`
probes showed:

```
after watchdog_init : 5280
after payload_init  : 2680
before ipmi_init    : 2680
before scheduler    :  200   ← idle task cannot be created → silent hang
```

With `configMINIMAL_STACK_SIZE = 96` words the idle task needs ~470 B. Stacks
were trimmed (IPMI/PAYLOAD 176, LED 96, watchdog 48), giving **592 B** free and
a clean boot with the watchdog running.

**Lesson:** on an 8 KB‑SRAM part, treat the FreeRTOS heap as a hard budget and
measure it, don’t estimate.

### C7 — Zone 3 record on a board without Zone 3

The uFC has no Zone 3 connector, but `amc_fru_info_build()` always appended the
MTCA.4 “Zone 3 Interface Compatibility” record (PICMG record ID `30h`), and
that record was the one carrying the **end‑of‑list** bit.

Fix, in two parts:

1. `AMC_FRU_INCLUDE_ZONE3_RECORD` (default 1, set to 0 for uFC).
2. When the record is omitted, set EOL on the point‑to‑point record and
   recompute its header checksum — the EOL bit is covered by that checksum:

```c
void fru_multirecord_set_eol( uint8_t *rec, uint8_t eol )
{
    fru_multirecord_area_header_t *hdr = (fru_multirecord_area_header_t *) rec;
    hdr->eol = eol ? 1 : 0;
    hdr->header_chksum = 0;
    hdr->header_chksum = calculate_chksum( (uint8_t *)hdr, sizeof(*hdr) );
}
```

FRU shrank from 122 to **104 bytes**; the multirecord chain now ends at the P2P
record.

### C8 — Build system

* **CMake 4.2 refused** `cmake_minimum_required(VERSION 3.0.0)`; bumped to
  **3.10** so no `-DCMAKE_POLICY_VERSION_MINIMUM` workaround is needed (3.10
  also avoids the future‑removal warning for policies < 3.10).
* **`GitSHA1.c` was generated into the source tree**, dirtying the checkout on
  every configure. Moved to `CMAKE_BINARY_DIR`; each build now embeds its own
  hash and the tree stays clean.
* **u4FCP v2 default controller** was `STM32F103C8T6` (64 KB); the carrier uses
  the **STM32F103CBT6** (128 KB). Added `STM32F103CBT6_{app,boot}.ld` and
  changed the default.

---

## 3. Verification method

| Layer | Method |
|---|---|
| Boot chain | UART capture across a bench power cycle (N6705C CH4) |
| Jump target | `objdump -s` on the vector table, newboot console prints of SP/PC |
| Image layout | `arm-none-eabi-readelf -l`, linker map files |
| I²C | Dedicated ACK scan with channel selection before each device |
| FRU | Builder self‑check + cold‑boot validation of the production image |
| RAM | `xPortGetFreeHeapSize()` probes around each init step |
| Emulation | Renode (STM32F103 platform) for the minimal test app |
| Reproducibility | Clean out‑of‑tree builds, git SHA embedded in the image |

---

## 4. Lessons learned

1. **Optimisation level can expose UB.** A cast‑based vector read worked at
   `-O3`/`-Og` and broke at `-Os`.
2. **RTOS APIs are illegal before the scheduler starts.** `vTaskDelay`,
   blocking semaphore takes and heap allocation all have pre‑scheduler
   caveats; use busy waits in `main()`.
3. **EEPROM writes need the datasheet cycle time** (5 ms here), not a “delay
   that looks reasonable”.
4. **First I²C transaction after init is not reliable** on this board; retry at
   the right level (whole take+read, not just the read).
5. **Heap is a budget, not a guess** — measure it with the real task set.
6. **Diff against a known‑good reference** before inventing fixes; half the
   bring‑up time was spent undoing debugging hacks.
7. **Keep the bootloader region sacred.** `0x08000000–0x08001FFF` is never
   erased by the update flow.
8. **Document the artifact layout** (boot/newboot/app/staging/fw_info) — it
   explains most “app doesn’t start” symptoms.

---

## 5. Residual risks / open items

* The mux’s first transaction still NACKs at the hardware level; the retry
  hides it. Root cause (bus start timing after `vI2CConfig`) is not fully
  investigated.
* Task stacks were trimmed to fit the heap; long‑term stack usage under IPMI
  stress has not been profiled (stack‑overflow hook is enabled, so an overflow
  would be reported).
* The Renode FRU test has not been re‑run against the final code.
* CMSIS is the older ARM license (see `THIRD_PARTY_NOTICES.md`); revisit if the
  project ever becomes commercial.
