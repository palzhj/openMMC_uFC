# Third-party notices

This repository is a **research fork** of
[openMMC](https://github.com/lnls-dig/openMMC) (GPL-3.0+) targeting the uFC‑v2
board. The project as a whole is distributed under the **GNU General Public
License v3.0 or later** — see [LICENSE](LICENSE).

The following third-party components are included in the source tree and remain
under their own licenses. Their license texts are kept next to the code.

## FreeRTOS kernel

- Location: `FreeRTOS/`
- License: **MIT** (Copyright (C) Amazon.com, Inc. or its affiliates / Real Time
  Engineers Ltd.)
- The MIT permission notice is reproduced in the header of the kernel source
  files (e.g. `FreeRTOS/tasks.c`).

## STM32F10x Standard Peripheral Library

- Location: `port/ucontroller/stm32/stm32f1xx/Libraries/STM32F10x_StdPeriph_Driver/`
- License: **ST SLA0044** ("Ultimate Liberty Software License Agreement",
  Rev 5 / February 2018)
- Full text: `port/ucontroller/stm32/stm32f1xx/Libraries/STM32F10x_StdPeriph_Driver/LICENSE.txt`
- Copyright (c) 2011–2012 STMicroelectronics.

## CMSIS Cortex-M3 Core Peripheral Access Layer

- Location: `port/ucontroller/stm32/stm32f1xx/Libraries/CMSIS/`
- License: **ARM Limited End User Licence Agreement LEC‑PRE‑00425‑V2.0**
  (Cortex Microcontroller Software Interface Standard)
- Full text: `port/ucontroller/stm32/stm32f1xx/Libraries/CMSIS/License.doc`
- Copyright (C) 2009 ARM Limited. All rights reserved.
- Note: this is the older CMSIS license (not Apache‑2.0). It permits use,
  copying and modification for CMSIS‑compliant products provided the notices
  are preserved, grants no ARM trademark rights, and is subject to U.S. export
  control law.

## Documents

Vendor datasheets and PICMG/Intel specifications are **not** part of this
repository. Locally downloaded copies (some marked *CERN — for internal use
only / external distribution prohibited*) are excluded via `.gitignore` and are
not present in the git history.
