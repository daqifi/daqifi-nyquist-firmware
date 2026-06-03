# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is the DAQiFi Nyquist firmware project - a multi-channel data acquisition device built on PIC32MZ microcontroller with FreeRTOS. The project consists of a USB bootloader and main application firmware.

## Build Instructions

### Prerequisites
- MPLAB X IDE (Microchip's development environment)
- XC32 Compiler for PIC32
- Harmony Configurator (MHC) for code generation

### Building Firmware Only
1. Open `firmware/daqifi.X` project in MPLAB X
2. In Project Properties, exclude all linker files from the build
3. Build and flash directly to the device

### Building Bootloader-Compatible Firmware
1. Open `firmware/daqifi.X` project in MPLAB X
2. In Project Properties, include `old_hv2_bootld.ld` linker script
3. Build the firmware
4. Flash the bootloader first: `bootloader/firmware/usb_bootloader.X`
5. Then flash the firmware using the Windows DAQiFi application or include it in bootloader Project Properties → Loading menu

### Building from Command Line (Windows with WSL/Linux)
1. Ensure MPLAB X is installed (typically in `C:\Program Files\Microchip\MPLABX`)
2. Clean build artifacts: `rm -rf firmware/daqifi.X/build/ dist/`
3. Build using MPLAB X's make utility:
   ```bash
   cd firmware/daqifi.X
   "/mnt/c/Program Files/Microchip/MPLABX/v6.25/gnuBins/GnuWin32/bin/make.exe" -f nbproject/Makefile-default.mk CONF=default build -j$(nproc)
   ```
   Note: `-j$(nproc)` uses all available CPU cores for faster builds
4. The output hex file will be in `dist/default/production/daqifi.X.production.hex` (approx 1MB)

Note: XC32 compiler is also available in Linux at `/opt/microchip/xc32/v4.60/bin/xc32-gcc`

### Compiler Optimization Level

The project builds with **-O3** globally, with per-file overrides where needed. This required patches to fix false positives and third-party incompatibilities:

#### Per-File Optimization Overrides

| File | Optimization | Reason | Permanent? |
|------|-------------|--------|-----------|
| `third_party/wolfssl/wolfcrypt/src/tfm.c` | -O3 with `-Wno-error=array-bounds` | GCC loses track of loop variable range after inlining in wolfSSL big-number math. Known third-party issue. | No — reevaluate after wolfSSL upgrade (currently pinned to v5.4.0) |

The previous `FreeRTOS_tasks.c -O1` override was replaced (issue #426) by defining `configLIST_VOLATILE volatile` in `FreeRTOSConfig.h` — the upstream-blessed back door for compilers that hoist/reorder stores in `listINSERT_END` at -O2+. The kernel now builds at the global -O3 level alongside the rest of the firmware.

#### Source Patches for -O2/-O3

| File | Patch | Reason | Permanent? |
|------|-------|--------|-----------|
| `libraries/scpi/libscpi/src/utils_private.h:49` | Added platform guard to `__attribute__((visibility))` | ELF visibility is meaningless on bare-metal PIC32, errors at -O3 with -Werror | No — reevaluate after libscpi upgrade |
| `Util/Logger.c:483` | `strncpy` → `memcpy` | `strncpy(dst, src, strlen(src))` never null-terminates; memcpy is what the code actually means (next line does manual null-term) | **Yes** — genuine bug fix |
| `services/wifi_services/wifi_serial_bridge_interface.c:68,80` | `__attribute__((noinline))` on `UARTReadGetBuffer` | GCC -O3 inlines 512-byte ring buffer read into 1-byte caller, triggers false `-Warray-bounds`. Function takes a mutex so inlining is counterproductive anyway. | No — reevaluate after XC32/GCC upgrade |

#### Known Linker Issue (Issue #271, informational)

The XC32 linker script (`p32MZ2048EFM144.ld`) uses a "best-fit allocator" for `.bss.*` sections. At O2+ with `-fdata-sections`, this can place variables at two addresses (`.sbss` GP-relative vs `.bss.*` best-fit), causing dual-address bugs. This was the original symptom that triggered investigation of the O2 FreeRTOS crash, but the actual fix landed elsewhere: defining `configLIST_VOLATILE volatile` in `FreeRTOSConfig.h` forces the kernel's list-item link fields to be volatile, preventing the reorder of `listINSERT_END` stores that was the real cause. With that macro defined, `FreeRTOS_tasks.c` builds cleanly at -O3 alongside the rest of the firmware. The linker script is left at Microchip default.

**When upgrading XC32 or third-party libraries**, try removing source patches 1 and 3 (under "Source Patches for -O2/-O3" above) and rebuild with -Werror. If the build passes clean, the patches can be deleted. The `configLIST_VOLATILE` define should be kept regardless of compiler version — it's the upstream FreeRTOS-blessed pattern, not a workaround.

### Static Analysis (cppcheck)

`tools/lint/cppcheck.sh` runs cppcheck on `firmware/src/` excluding
third-party (`third_party/`, `libraries/`, `config/`). The committed
baseline `tools/lint/cppcheck-baseline.txt` is the accepted finding
set; suppressions live in `tools/lint/cppcheck-suppress.txt`.

Run locally:

```bash
bash tools/lint/cppcheck.sh
```

CI gate: `.github/workflows/cppcheck.yml` runs on every PR push that
touches `firmware/src/**` or `tools/lint/**` and **fails the check
if the new output differs from the committed baseline** (added
findings = potential bugs; removed findings = baseline drift). The
runner is Ubuntu 24.04 (cppcheck 2.13.0) — same version we develop
against locally.

When the gate fails:
- New findings (`+` lines in the diff) → either fix the bug or, if
  it's a false positive, add a suppression to
  `tools/lint/cppcheck-suppress.txt` and regenerate the baseline.
- Removed findings (`-` lines) → someone fixed an existing finding;
  regenerate the baseline (`bash tools/lint/cppcheck.sh`) and commit
  the updated `cppcheck-baseline.txt`.

The current baseline is 2 style findings (both in
`firmware/src/services/wifi_services/wifi_serial_bridge.c` —
ergonomic, not bugs). The suppression file documents the
DioProbe.c array-bounds false positive and the FreeRTOS portmacro
FPU-guard `#error` (chip-specific macro that cppcheck doesn't see).

### Programming with PICkit 4 from Command Line
1. Connect PICkit 4 to the device
2. Use ipecmd to program the hex file:
   ```bash
   cd firmware/daqifi.X
   "/mnt/c/Program Files/Microchip/MPLABX/v6.30/mplab_platform/mplab_ipe/ipecmd.exe" \
     -TPPK4 -P32MZ2048EFM144 -M -F"dist/default/production/daqifi.X.production.hex" -OL
   ```
3. The device will be erased and programmed automatically
4. Look for "Program Succeeded" message

Command options:
- `-TPPK4`: Use PICkit 4 as programmer
- `-TS<serial>`: Pick a specific PICkit when multiple are connected
- `-P32MZ2048EFM144`: Target device (no `PIC` prefix)
- `-M`: Program mode
- `-F`: Hex file to program (must be Windows-style path; `/mnt/c/...` fails silently)
- `-OL`: Use loaded memories only

### Bench tool inventory (this dev station)
| Item | Identifier | Notes |
|------|-----------|-------|
| Primary PICkit 4 (this board) | `BUR184882598` | Pass `-TSBUR184882598` to ipecmd when multiple PICkits are attached |
| Secondary PICkit 4 | `BUR202272588` | On other board(s) — ignore unless re-targeting |
| MCU device target | `PIC32MZ2048EFM144` | Pass to ipecmd as `-P32MZ2048EFM144` (no `PIC` prefix; with the prefix you get exit 36 / "Unable to locate DFP") |
| Serial port (USB CDC) | Windows: `COM3` / `COM9` (stable per board) ; WSL: `/dev/ttyACMn` (**attach-order dependent — NOT stable**) | usbipd busid `2-4` (primary) / `2-3` (secondary); reattach via `powershell.exe -Command "usbipd attach --wsl --busid <BUSID>"` after each reboot/flash. **DO NOT assume `/dev/ttyACM0` maps to any particular board — verify by serial number (see below).** Stable identifiers across reboots are the Windows COM number and the per-unit serial; `/dev/ttyACMn` is assigned by Linux in the order usbipd attaches devices. |
| Bench primary device serial | `7E2898F46200E8A7` | Programmed by PICkit `BUR184882598`. Windows: **COM3**, busid **2-4**. Verify by `*IDN?` before issuing SCPI — never hardcode `/dev/ttyACM0`. |
| Bench secondary device serial | `7E28A4206200EAD1` | Programmed by PICkit `BUR202272588`. Windows: **COM9**, busid **2-3**. Verify by `*IDN?` before issuing SCPI — never hardcode `/dev/ttyACM1`. **⚠️ Shared with other agents — may not always be available; if `usbipd list` shows it as "Not shared" or it's attached elsewhere, do not commandeer it.  Fall back to the primary alone.** |
| Bench WiFi AP | SSID `Tesla` | Credentials in `~/.daqifi.env` (chmod 600) — never commit |
| Bench PC iperf2 | `C:\Users\User\Downloads\iperf-2.2.1-win64.exe` | Run `-s -p 5002 -i 1`; redirect stdout to `C:\temp\iperf2.log` for log-side correlation |

#### ⚠️ Device verification protocol — ALWAYS run before SCPI tests

Multiple DAQiFi boards may be attached to WSL simultaneously, and
**`/dev/ttyACMn` assignment is volatile**: Linux assigns numbers in the
order `usbipd` attaches devices, not by hardware identity.  Detach +
re-attach in a different order, and the same physical board moves from
`ACM0` to `ACM1` (or vice versa) without anything else changing.  The
**stable** identifiers across attach/detach/reboot cycles are the
**Windows COM number** (e.g. `COM3` for the primary) and the
**firmware-reported serial** in the third field of `*IDN?` (e.g.
`7E2898F46200E8A7`).  Always map back to one of those — never trust
`/dev/ttyACMn` alone.

This caused a >30-minute false bisect on 2026-05-06: every firmware
version "looked the same" because the test script was reading an
unflashed second board on `/dev/ttyACM0`.

**Verification steps before any SCPI work:**

1. Enumerate USB devices on the Windows side — the COM number is the
   stable identifier, and `usbipd list` shows which busid maps to
   which COM:
   ```bash
   powershell.exe -Command "usbipd list" | grep "04d8:f794"
   # Example output (bench layout):
   #   2-3   04d8:f794  USB Serial Device (COM9)   Shared/Attached
   #   2-4   04d8:f794  USB Serial Device (COM3)   Shared/Attached
   ls -la /dev/ttyACM*
   ```
2. For each `/dev/ttyACMn` currently attached, query the per-unit
   serial via `*IDN?` — the third field is the per-unit serial.
   Uses `picocom`, which is the canonical idiom throughout this
   document (no external helper scripts required):
   ```bash
   for dev in /dev/ttyACM*; do
     echo -n "$dev => "
     (echo -e "*IDN?\r"; sleep 0.5) \
       | timeout --foreground 2s picocom -b 115200 -q -x 1000 "$dev" \
       | tr -d '\r' \
       | grep -m1 '^DAQiFi,' || echo "<no response>"
   done
   # Possible mapping (varies by attach order — DON'T assume!):
   #   /dev/ttyACM0 => DAQiFi,Nq1,7E2898F46200E8A7,01-02   (COM3 / bench primary)
   #   /dev/ttyACM1 => DAQiFi,Nq1,7E28A4206200EAD1,01-02   (COM9 / bench secondary)
   ```
3. Match the third field of `*IDN?` to the **Bench primary device
   serial** (`7E2898F46200E8A7` — COM3) or **Bench secondary**
   (`7E28A4206200EAD1` — COM9) in the inventory table above.  Store the
   looked-up path in a variable (e.g. `DEV_PRIMARY=/dev/ttyACM0`) for
   the rest of the session rather than hardcoding `/dev/ttyACM0` — the
   mapping can change next time you re-attach.
4. If the wrong board is selected, re-target rather than detaching the
   other board. (Detaching usbipd devices that aren't yours is rude and
   can disrupt other workflows.)

> **Note about examples later in this file.**  Many of the SCPI /
> picocom / `usbipd` examples below this section use the literal
> `/dev/ttyACM0` as a stand-in for "the primary device" — substitute
> `"$DEV_PRIMARY"` (looked up by serial via step 2) when there's any
> ambiguity about which board is on which path.  The same applies to
> GEMINI.md.  Converting every example one-by-one is tracked separately;
> in the meantime, the verification protocol above is the authoritative
> way to resolve the mapping.

`*IDN?` returns `DAQiFi,Nq1,<per-unit serial>,01-02` — the streaming CSV
metadata header echoes the same serial.  Either source is fine for
disambiguation; `*IDN?` is faster (no stream start/stop overhead).

### Bootloader Entry
- Hold the user button for ~20 seconds until board resets
- Release button when LEDs light solid
- Hold button again until white LED blinks to enter bootloader mode

### Packaging Release Artifacts
For every prerelease and release, compress the production hex file into a zip labeled with the firmware revision:
```bash
zip -j "daqifi-nyquist-firmware-<VERSION>.zip" firmware/daqifi.X/dist/default/production/daqifi.X.production.hex
```
Example: `daqifi-nyquist-firmware-3.4.5b1.zip`. The version string comes from `FIRMWARE_REVISION` in `firmware/src/version.h`.

## Architecture Overview

### Component Hierarchy
```
Main Application (FreeRTOS Tasks)
├── USB Device Task (app_USBDeviceTask)
├── WiFi Task (app_WifiTask)
├── SD Card Task (app_SdCardTask)
├── Streaming Task
└── SCPI Command Processing
    
Hardware Abstraction Layer (HAL)
├── ADC (AD7173, AD7609, MC12bADC)
├── DAC (DAC7718)
├── DIO (Digital I/O)
├── Power Management (BQ24297)
└── UI (User Interface/LEDs)

Services Layer
├── Data Encoding (JSON, CSV, Protocol Buffers)
├── Communication (USB CDC, WiFi TCP Server, SCPI)
├── Storage (SD Card Manager)
└── Streaming Engine
```

### Key Data Structures

1. **Board Configuration** (`tBoardConfig` in `state/board/BoardConfig.h`)
   - Immutable hardware configuration
   - Defines available channels, modules, and capabilities
   - Three variants: NQ1, NQ2, NQ3

2. **Board Runtime Configuration** (`tBoardRuntimeConfig` in `state/runtime/BoardRuntimeConfig.h`)
   - Mutable runtime settings
   - Channel configurations, sampling rates, triggers

3. **Board Data** (`tBoardData` in `state/data/BoardData.h`)
   - Real-time acquisition data
   - Sample buffers and timestamps

### Communication Protocols

1. **SCPI Commands** - Standard Commands for Programmable Instruments
   - Entry point: `services/SCPI/SCPIInterface.c`
   - Modules: SCPIADC, SCPIDIO, SCPILAN, SCPIStorageSD
   - Used for device configuration and control
   - **SCPI Abbreviation Rule**: Commands can be abbreviated based on the capitalization in the full command. The abbreviated command must contain all letters that are in CAPS. For example:
     - `SYST:COMM:LAN:NETMode` can be abbreviated as `SYST:COMM:LAN:NETM`
     - `SYST:COMM:LAN:APPLy` can be abbreviated as `SYST:COMM:LAN:APPL`

#### Quiescence Rule — No SCPI queries during a benchmarked test

Send no SCPI to the device while a streaming or iperf2 run is in
progress — every query preempts the data path being measured (USB
SCPI = priority 7; TCP SCPI runs on the WifiTask itself).

The firmware preserves end-of-test stats in IDLE: `IPERF:STATs?`
returns `gCtx.last_stats` (frozen by `FinalizeStats`); `STR:STATS?`
survives across `StopStreamData` until the next start or
`STATS:CLEar`.

**Pattern:** `start → time.sleep(duration + margin) → single STATS query`.
Out-of-band visibility (Saleae, PC-side iperf2.log) for long runs;
never poll the device under test. Mirrored in the SCPI wiki.

#### SCPI Command Verification Protocol

**⚠️ CRITICAL: NEVER guess SCPI command syntax. ALWAYS verify first.**

**Mandatory Process Before Using ANY SCPI Command:**

1. **Search for the exact pattern** in `SCPIInterface.c`:
   ```bash
   grep -n "pattern.*<keyword>" firmware/src/services/SCPI/SCPIInterface.c
   ```

2. **Use the EXACT command** from the pattern definition - do not abbreviate unless testing abbreviations

3. **Verify parameter syntax** by reading the callback function implementation

**Common Mistakes to Avoid:**
- ❌ `SYST:STAR 5000` (guessed)
- ✅ `SYST:STR:START 5000` (verified from SCPIInterface.c:1521)

- ❌ `SYST:STOP` (guessed)
- ✅ `SYST:STR:STOP` (verified from SCPIInterface.c:1522)

- ❌ `SYST:STOR:SD:GET DAQiFi/file.csv` (unquoted path)
- ✅ `SYST:STOR:SD:GET "file.csv"` (quoted, verified from SCPIStorageSD.c)

**Verification Example:**
```bash
# Step 1: Search for streaming commands
grep -n "pattern.*Stream" firmware/src/services/SCPI/SCPIInterface.c
# Result: Line 1521: {.pattern = "SYSTem:STReam:START", .callback = SCPI_StartStreaming,},

# Step 2: Use exact command
device._comm.send_command("SYST:STR:START 5000")  # Correct!
```

**If you use a SCPI command without verifying it first, STOP immediately and verify the syntax.**

#### Stream-control namespace migration (round 3, see #311 / #324)

The streaming start/stop/query commands consolidated into the `SYST:STR:*` namespace, alongside the rest of the streaming family (`SYST:STR:FOR`, `SYST:STR:INT`, `SYST:STR:STATS?`, etc.). Legacy forms remain as aliases — both work:

| Canonical (preferred)     | Legacy alias (still works)   |
|---------------------------|------------------------------|
| `SYST:STR:START <freq>`   | `SYST:StartStreamData <freq>` |
| `SYST:STR:STOP`           | `SYST:StopStreamData`         |
| `SYST:STR:DATA?`          | `SYST:StreamData?`            |
| `SYST:USB:TRANSparent:MODE` | `SYST:USB:SetTransparentMode` |

New code, docs, and the wiki should use the canonical forms. Existing client libraries (`daqifi-python-core`, `daqifi-core` (.NET), `daqifi-java-api`, etc.) continue to work without changes; migrate them on their own schedule. The legacy aliases will not be removed without a separate, scheduled deprecation cycle (months out, with explicit comm to client maintainers).

#### SCPI Wiki Maintenance

**When adding, modifying, or removing SCPI commands, ALWAYS update the GitHub wiki SCPI reference page.**

The wiki lives in a separate repo:
```bash
git clone https://github.com/daqifi/daqifi-nyquist-firmware.wiki.git /tmp/daqifi-wiki
```

The SCPI command reference is in `01-SCPI-Interface.md`. Update the relevant tables:
- Command tables (add/remove/modify command rows)
- Response field tables (e.g., Streaming Statistics Response Fields)
- Register bit definition tables (OPER/QUES condition bits)
- Callback function names (must match actual firmware function names)

Commit and push wiki changes after updating.

2. **USB CDC** - Virtual COM port communication
   - Implementation: `services/UsbCdc/UsbCdc.c`
   - Handles command processing and data streaming

3. **WiFi** - Network communication
   - Manager: `services/wifi_services/wifi_manager.c`
   - TCP server for remote control
   - UDP announcements for device discovery

### Data Flow

1. **Acquisition Path**:
   - Hardware interrupts → HAL drivers → Sample buffers
   - ADC modules use DMA for high-speed acquisition
   - DIO uses interrupt-driven capture

2. **Streaming Path**:
   - Sample buffers → Encoder (JSON/CSV/ProtoBuf) → Output interface (USB/WiFi/SD)
   - Streaming engine manages buffer flow and encoding
   - Supports multiple simultaneous outputs

#### ADC Architecture & ISR Design

The PIC32MZ ADCHS peripheral has two types of ADC channels with different ISR strategies:

**Type 1 — Dedicated modules (simultaneous conversion):**
- NQ1 channels: ch4 (MODULE4), ch8 (MODULE0), ch10 (MODULE1), ch12 (MODULE2), ch14 (MODULE3)
- Each channel has its own SAR ADC module — all convert simultaneously on trigger
- **Batched ISR**: A single data-ready interrupt (CH3/MODULE3) reads all Type 1 results. CH3 is always triggered when any Type 1 channel is active, even if ch14 itself is disabled. This eliminates N-1 redundant ISR entries per sample. See Issue #277.
- Trigger: `ADCHS_ChannelConversionStart()` per enabled channel in `MC12b_TriggerConversion()`

**Type 2 — Shared MODULE7 (sequential mux scan):**
- NQ1 channels: ch0, ch1, ch2, ch3, ch5, ch6, ch7, ch9, ch11, ch13, ch15
- All share MODULE7 — channels scanned sequentially via analog multiplexer
- **Single EOS ISR**: `ADC_EOS_Handler` fires once after the entire scan completes
- Results read by `MC12bADC_EosInterruptTask` (deferred task, priority 9)
- Trigger: `ADCHS_GlobalEdgeConversionStart()` in `MC12b_TriggerConversion()`

**ISR flow per streaming timer tick:**
1. Streaming timer ISR (`Streaming_Defer_Interrupt`) fires → notifies deferred task
2. Deferred task calls `MC12b_TriggerConversion(MC12B_ADC_TYPE_ALL)`
3. Type 1: individual `ChannelConversionStart` + CH3 phantom trigger → single `ADC_DATA3_Handler` reads all
4. Type 2: `GlobalEdgeConversionStart` → MODULE7 scans → `ADC_EOS_Handler` → `MC12bADC_EosInterruptTask` reads all
5. Results stored via `ADC_ReadADCSampleFromISR()` → `BoardData_Set(BOARDDATA_AIN_LATEST)`

**Key files:**
- `config/default/interrupts.c` — `ADC_DATA3_Handler` (Type 1 batch), `ADC_EOS_Handler` (Type 2)
- `HAL/ADC/MC12bADC.c` — `MC12b_TriggerConversion`, `MC12b_WriteStateAll` (batch interrupt setup)
- `HAL/ADC.c` — `MC12bADC_EosInterruptTask`, `ADC_ReadADCSampleFromISR`

**Characterization results (O3, USB, zero-loss ceiling sustained for 60s, fullscale test pattern, NoCap benchmark mode):**

Current table = Session 22 (2026-04-24 overnight, post-#354 ADC stack fix + #356 SCPI-dispatch Option 2 decouple). #354 moved two ~5 KB stack locals off the ADC write path; #356 moved the TCP-SCPI dispatch from WDRV_WINC_Tasks onto app_WifiTask so WINC stays at 1024 words. Throughput is unchanged to +1 k on five USB configs (likely run-to-run at the edge, confirmed by 60 s endurance). No regressions.

**USB** (ceiling sweep 10s/step, endurance 60s at each ceiling):

| Config | PB Ceiling | PB KB/s | CSV Ceiling | CSV KB/s |
|--------|----------:|--------:|------------:|--------:|
| 1×T1 | 19,000 Hz | 240 | 19,000 Hz | 276 |
| 1×T1 OBDiag=OFF | **20,000 Hz** | 253 | **20,000 Hz** | 321 |
| 1×T2 | 19,000 Hz | 240 | 19,000 Hz | 287 |
| 3×T1 | 17,000 Hz | 364 | 15,000 Hz | 711 |
| 3×T2 | 17,000 Hz | 363 | 15,000 Hz | 716 |
| 5×T1 | 15,000 Hz | 457 | 13,000 Hz | 993 |
| 5×T2 | 15,000 Hz | 456 | 13,000 Hz | 1,002 |
| 5×T1 OBDiag=OFF | 17,000 Hz | 510 | 14,000 Hz | 1,102 |
| 8×T2 | 13,000 Hz | 566 | 10,000 Hz | 1,256 |
| 11×T2 | 11,000 Hz | 633 | 9,000 Hz† | 1,484† |
| 5T1+3T2 (8ch) | 13,000 Hz | 567 | 10,000 Hz | 1,257 |
| 5T1+5T2 (10ch) | 12,000 Hz† | 625† | 9,000 Hz | 1,349 |
| 5T1+11T2 (16ch) | 9,000 Hz | 727 | 7,000 Hz | 1,610 |

† Endurance leak at ceiling in Session 22 (PB 5T1+5T2 @ 12k: 3180 drops; CSV 11×T2 @ 9k: 1866 drops). Ceilings listed are highest clean in the 10s sweep; true sustainable endurance ceilings are 1 kHz lower for those configs.

**SD** (zero-drop over 60s, interface=2):

| Config | PB Ceiling | CSV Ceiling |
|--------|----------:|------------:|
| 1×T1 | 10,000 Hz | 9,000 Hz |
| 1×T1 OBDiag=OFF | **11,000 Hz** | 10,000 Hz |
| 1×T2 | 10,000 Hz | 9,000 Hz |
| 3×T1 | 8,000 Hz | 5,000 Hz |
| 3×T2 | 8,000 Hz | 5,000 Hz |
| 5×T1 | 7,000 Hz | 4,000 Hz |
| 5×T2 | 7,000 Hz | 4,000 Hz |
| 5×T1 OBDiag=OFF | 8,000 Hz | 4,000 Hz |
| 5×T1 OBDiag=ON | 7,000 Hz | 4,000 Hz† |
| 8×T2 | 6,000 Hz | 3,000 Hz |
| 11×T2 | 5,000 Hz | 2,000 Hz |
| 5T1+3T2 (8ch) | 6,000 Hz | 3,000 Hz |
| 5T1+5T2 (10ch) | 5,000 Hz | 2,000 Hz† |
| 5T1+11T2 (16ch) | 4,000 Hz | 2,000 Hz† |

† Endurance leak at ceiling in Session 22 (CSV 5×T1 OBD=ON @ 4k, 5T1+5T2 @ 2k, 5T1+11T2 @ 2k all leaked during the 60 s endurance). True sustainable ceilings are 1 kHz lower for those three configs.

**Session 22 highlights vs Sessions 20/21:**
- **#354/#356 throughput-safe.** 5 USB configs pick up +1 k ceilings post-merge (USB PB 3×T1, USB PB 5×T1 OBD=OFF, USB CSV 1×T1, USB CSV 1×T2, USB CSV 5×T1) — confirmed clean at 60 s endurance. Likely run-to-run at the edge rather than a real speedup.
- **SD CSV 8×T2 real +2 k gain** (3 k clean vs Session 20's 1 k). Biggest SD surprise; SD CSV 5T1+11T2 ceiling sweep also finds 2 k (was 1 k) but leaks endurance, so sustainable ceiling stays at 1 k.
- **SD single-channel PB -1 k** (10k/11k vs 11k/12k in Session 20). Session 20 numbers were run-to-run optimistic per own note; Session 22 figures are the reliable endurance values.
- **Session 22 endurance leaks:** USB PB 5T1+5T2 @ 12k, USB CSV 11×T2 @ 9k, SD CSV 5×T1 OBD=ON @ 4k, SD CSV 5T1+5T2 @ 2k, SD CSV 5T1+11T2 @ 2k. USB CSV 5T1+11T2 (was Session 21 endurance-leak at 7k) now clean.
- **No regressions.** #354 (ADC stack fix) + #356 (SCPI dispatch off WDRV_WINC_Tasks, WINC back to 1024-word stack) are merge-safe for throughput.

**Session 21 highlights (earlier, retained for context):**
- **Jitter eliminated at ≥10 kHz.** #335 idle-gate kills the 22 Hz / 270 µs WINC preemption — CV at 10 kHz drops 9.44% → 0.5%, p-p 326 µs → 8.8 µs. Saleae verified.

**Session 20 highlights (earlier, retained for context):**
- **T1/T2 parity achieved across every mode** — PB+T2 regression (#313) fully resolved.
- **SD PB +10-33%** — #312 regression fully resolved (not just partially as #314 showed).
- **SD PB OBDiag=OFF hits 12 kHz** (new SD high-water mark).
- **Zero regressions** on any config vs Session 18.

#### Voltage Output Precision

Systemwide configurable voltage precision via `StreamingRuntimeConfig.VoltagePrecision`. Applies to CSV streaming, JSON streaming, and SCPI voltage queries (`MEAS:VOLT:DC?`, `SOUR:VOLT:LEV?`).

**SCPI Commands:**
```bash
CONFigure:VOLTage:PRECision <0-10>   # Set precision
CONFigure:VOLTage:PRECision?         # Query current precision
CONFigure:VOLTage:SAVE               # Persist to NVM (survives reboot)
CONFigure:VOLTage:LOAD               # Load from NVM
```

| Value | Output | Example |
|-------|--------|---------|
| 0 | Integer millivolts | `1221` (fast path, uses `int_to_str`) |
| 4 | Volts, 4 decimal places | `1.2207` (NQ1 default, 12-bit ADC) |
| 6 | Volts, 6 decimal places | `1.220703` (NQ3 default, 18-bit AD7609) |
| 7 | Volts, 7 decimal places | `1.2207031` (NQ2 default, 24-bit AD7173) |

**Board-specific defaults** (`tBoardConfig.DefaultVoltagePrecision`): NQ1=4, NQ3=6, NQ2=7.

**NVM persistence**: Stored in `TopLevelSettings.voltagePrecision`. Saved via `CONF:DATA:SAVE`, loaded at boot from NVM. Falls back to board config default on first boot.

**Implementation:** `firmware/src/services/csv_encoder.c`, `firmware/src/services/JSON_Encoder.c`, `firmware/src/services/SCPI/SCPIInterface.h` (SCPI_ResultVoltage helper), `firmware/src/services/SCPI/SCPIADC.c`, `firmware/src/services/SCPI/SCPIDAC.c`

#### Streaming Statistics & Buffer Overrun Tracking

The streaming engine tracks data loss at every stage of the pipeline. Statistics are accumulated per streaming session (cleared on `StartStreamData`, preserved after `StopStreamData`).

**SCPI Commands:**
```bash
SYSTem:STReam:STATS?       # Query all statistics
SYSTem:STReam:STATS:CLEar  # Reset all counters
```

**Response fields from `SYSTem:STReam:STATS?`:**
| Field | Type | Description |
|-------|------|-------------|
| `TotalSamplesStreamed` | uint64 | Samples successfully queued from ISR |
| `TotalBytesStreamed` | uint64 | Total bytes encoded (offered to outputs) |
| `QueueDroppedSamples` | uint32 | Samples lost due to pool exhaustion or full sample queue (pool=700) |
| `UsbDroppedBytes` | uint32 | Data lost due to USB circular buffer full (16KB) |
| `WifiDroppedBytes` | uint32 | Data lost due to WiFi circular buffer full (14KB) |
| `SdDroppedBytes` | uint32 | Data lost due to SD write timeout/partial (8-64KB buf, 3 retries) |
| `EncoderFailures` | uint32 | Encoding attempts that returned 0 bytes with data available |
| `TimerISRCalls` | uint64 | Actual streaming timer ISR entry count this session (#265). Invariant: `TimerISRCalls == TotalSamplesStreamed + QueueDroppedSamples`. |
| `SampleLossPercent` | uint32 | `QueueDroppedSamples / (Total + Dropped) * 100` |
| `ByteLossPercent` | uint32 | `(USB + WiFi + SD dropped) / TotalBytesStreamed * 100` |
| `WindowLossPercent` | uint32 | Sliding-window sample loss % (0-100), updated every N samples |

**Distinguishing failure modes** with the new ISR counter:
- `TimerISRCalls < freq × duration` → timer is rate-limited (PIC32MZ ~90 kHz hardware ceiling)
- `TimerISRCalls == TotalSamples + QueueDropped` → every ISR is accounted for; nothing lost between ISR and deferred task (should always hold)
- `QueueDroppedSamples > 0` → sample pool exhausted (encoder/output too slow for the rate the timer is firing)
- `UsbDroppedBytes / SdDroppedBytes > 0` → encoder is fine but transport can't keep up

**Thread safety:** `TotalSamplesStreamed`, `TotalBytesStreamed`, and `TimerISRCalls` are 64-bit counters (safe for million-year sessions). The first two are protected by `taskENTER_CRITICAL`/`taskEXIT_CRITICAL` on each increment and during snapshot reads. Drop counters remain 32-bit (atomic on PIC32MZ). `TimerISRCalls` lives in a separate `static volatile uint64_t gTimerISRCalls` global, incremented in true ISR context (TIMER_3, priority 1) by a single writer (no critical section needed because same-source can't preempt itself); the snapshot read uses `taskENTER_CRITICAL` which raises the syscall priority above the kernel-managed ISR threshold and blocks the timer, making the non-atomic 64-bit read coherent.

**Session-end logging:** When streaming stops, if any data was lost during the session, a `LOG_E` summary is automatically written with sample counts, per-buffer byte drops, and loss percentage. Retrieve via `SYST:LOG?`.

**Implementation:** `firmware/src/services/streaming.c` (StreamingStats struct), `firmware/src/services/SCPI/SCPIInterface.c` (SCPI callbacks)

#### SCPI Status Registers (OPER & QUES)

The firmware populates the IEEE 488.2 `STATus:OPERation` and `STATus:QUEStionable` condition registers to reflect real-time device state during streaming.

**SCPI Commands:**
```bash
STATus:OPERation:CONDition?      # Current OPER condition bits (non-destructive)
STATus:OPERation?                # OPER event register (latched, clears on read)
STATus:OPERation:ENABle <mask>   # Set OPER event enable mask
STATus:OPERation:ENABle?         # Query OPER event enable mask

STATus:QUEStionable:CONDition?   # Current QUES condition bits (non-destructive)
STATus:QUEStionable?             # QUES event register (latched, clears on read)
STATus:QUEStionable:ENABle <mask>  # Set QUES event enable mask
STATus:QUEStionable:ENABle?      # Query QUES event enable mask

STATus:PRESet                    # Reset enable masks to default
```

**OPERation Condition Register Bits:**
| Bit | Value | Name | Description |
|-----|-------|------|-------------|
| 4   | 16    | Measuring | Streaming/measuring is active |
| 10  | 1024  | SD Logging | SD card logging is active |

Set on `StartStreamData`, cleared on `StopStreamData`.

**QUEStionable Condition Register Bits:**
| Bit | Value | Name | Description |
|-----|-------|------|-------------|
| 4   | 16    | Data Loss | Windowed sample loss >= 5% |
| 8   | 256   | USB Overflow | USB circular buffer overflow detected |
| 9   | 512   | WiFi Overflow | WiFi circular buffer overflow detected |
| 10  | 1024  | SD Overflow | SD write failure detected |
| 11  | 2048  | Encoder Fail | Encoder returned 0 bytes with data available |
| 12  | 4096  | Transport Down | All configured transports unhealthy >grace; streaming auto-stopped (#397). Cleared on next start. Grace tunable via `SYST:STR:CONSumer:GRACe <sec>` (5..300, default 60). LOG_E line `Streaming: all configured transports down >Ns — auto-stop` captures the stop reason. |

QUES bits are set in real-time during streaming and cleared when streaming stops. The QUES CONDition register reflects the *current* health; the QUES EVENt register latches transitions (clears on read).

**Windowed Data Flow Tracking:**
The `Data Loss` QUES bit (bit 4) uses a sliding double-buffer window to evaluate pipeline health independently of sample rate. Window size defaults to `clamp(frequency * 2, 20, 10000)` sample periods (~2 seconds of history). If the configured threshold percentage of samples in the window were dropped, the bit is set. The current window loss percentage is also reported as `WindowLossPercent` in `SYST:STR:STATS?`.

**Flow Window Configuration:**
```bash
SYSTem:STReam:LOSS:THREshold <pct>   # Set loss threshold 1-100% (default 5)
SYSTem:STReam:LOSS:THREshold?        # Query current threshold
SYSTem:STReam:LOSS:WINDow <samples>  # Set window size (0=auto, 20-10000=explicit)
SYSTem:STReam:LOSS:WINDow?           # Query current override (0=auto)
```

- **Threshold** takes effect immediately (next flow window check). Lower values (1-2%) for precision measurement; higher values (10-20%) for best-effort monitoring or noisy transports.
- **Window size** takes effect at next streaming start. Larger windows smooth out bursty loss; smaller windows detect faults faster. Auto mode (`0`) scales with frequency.
- Both settings survive across streaming sessions but reset to defaults on device reboot.

**Implementation:** `firmware/src/services/streaming.c` (gQuesBits, flow window), `firmware/src/services/SCPI/SCPIInterface.c` (OPER/QUES helpers, sync)

#### Streaming Frequency Capping

The firmware automatically caps the requested streaming frequency based on a three-constraint model validated against hardware benchmarks. The cap is applied silently — no SCPI error is returned, but a `LOG_I` message is written to the log buffer (retrievable via `SYST:LOG?`).

**Constraints (fitted to characterization data 2026-04-13):**
| Constraint | Limit | Formula |
|-----------|-------|---------|
| ISR ceiling | 13 kHz | Hard per-invocation overhead limit |
| Type 1 aggregate | 55 kHz total | `55000 / type1ChannelCount` (batched ISR) |
| Per-tick budget | Scales with channels | `110000 / (6 + totalEnabledChannels)` |

**Effective limit:** `min(ISR_MAX, TYPE1_AGG / type1Count, BUDGET / (OVERHEAD + totalEnabled))`

**Example caps vs measured ceilings:**
| Config | Cap | Measured | Utilization |
|--------|----:|--------:|-----------:|
| 1×T1 | 13,000 Hz | 13,800 Hz | 94% |
| 5×T1 | 10,000 Hz | 11,000 Hz | 91% |
| 1×T2 | 13,000 Hz | 16,200 Hz | 80% |
| 5T1+4T2 (9ch) | 7,333 Hz | 10,000 Hz | 73% |
| 16ch | 5,000 Hz | 6,400 Hz | 78% |

**Where capping is applied:**
- `SYSTem:STReam:START <freq>` — caps frequency before starting the timer
- `CONFigure:ADC:CHANnel` — recalculates cap when channels are enabled/disabled during streaming

**Diagnosing capped frequency:** Check `SYST:LOG?` for `"Frequency capped: X Hz -> Y Hz"` messages. The actual streaming frequency is stored in the runtime config and can be verified by checking sample timestamps.

**Implementation:** `firmware/src/services/streaming.h` (`Streaming_ComputeMaxFreq`), `firmware/src/services/SCPI/SCPIInterface.c`, `firmware/src/services/SCPI/SCPIADC.c`

#### Test Pattern Streaming Mode

Test pattern mode replaces real ADC values with synthetic data for deterministic regression testing and benchmarking. The real ISR timing, ADC triggering, pool allocation, and full encoding pipeline are preserved — only the sample Value field is overridden.

**SCPI Commands:**
```bash
SYSTem:STReam:TEST:PATtern <pattern>   # Set pattern (0=off, 1-6)
SYSTem:STReam:TEST:PATtern?            # Query current pattern (0=disabled)
```

**Pattern Types:**

| Pattern | Name | Value Generated | Use Case |
|---------|------|----------------|----------|
| 0 | Off | Real ADC data | Normal operation |
| 1 | Counter | `(sampleCount + channelId) % (adcMax+1)` | Integrity verification (PC can predict exact values) |
| 2 | Midscale | `adcMax / 2` | Consistent encoding size across CSV/JSON/ProtoBuf |
| 3 | Fullscale | `adcMax` | Worst-case ProtoBuf variable-length encoding |
| 4 | Walking | `(sampleCount * (channelId+1)) % (adcMax+1)` | Multi-channel visual verification |
| 5 | Triangle | Ramps 0→adcMax→0, period=2*(adcMax+1) | Realistic waveform, staggered per channel |
| 6 | Sine | 256-sample sine wave, scaled to [0, adcMax] | Realistic signal testing, 45deg phase offset per channel |

- `adcMax` = max raw ADC code = Resolution - 1 (4095 for MC12bADC 12-bit, 262143 for AD7609 18-bit)
- Runtime-only (not persisted to NVM, resets on reboot)
- Sample counter resets at each `StartStreamData` for deterministic sessions
- Works with all encoding formats (CSV/JSON/ProtoBuf) and output interfaces (USB/WiFi/SD)

#### Streaming Throughput Benchmarking

Two benchmark tools for measuring streaming pipeline throughput:

**1. Benchmark Mode** (`SYST:STR:BENCHmark`): Three levels, each isolating a different stage of the pipeline so you can locate the actual bottleneck. **Read this carefully — the right level depends on what you are trying to measure.**

```bash
SYSTem:STReam:BENCHmark <0|1|2>   # 0=OFF, 1=NOCAP, 2=PIPELINE
SYSTem:STReam:BENCHmark?           # Query current mode
```

| Level | Name | Frequency cap | ADC in loop | Encoder runs | Use when |
|---|---|---|---|---|---|
| **0** | OFF (normal) | Active (per-channel safe rate) | Yes (real conversions) | Yes | Production / data integrity testing |
| **1** | NOCAP | Bypassed (any rate up to 100 kHz) | Yes (real conversions) | Yes | Measuring **end-to-end** throughput including ADC overhead |
| **2** | PIPELINE | Bypassed | **NO — ADC entirely skipped** | Yes (synthetic data only) | Measuring **WiFi/USB/SD pipeline ceiling** with ADC overhead removed |

**Decision tree — which level to use:**

- **OFF (0)**: don't override. This is the normal cap-gated mode that protects real ADC accuracy.
- **NOCAP (1)**: when you want to know *"what does the system actually deliver under real-world conditions?"* — ADC contention, encoder load, output pipeline all in play. **This is what you compare to documented ceilings.**
- **PIPELINE (2)**: when you suspect the ADC is contributing to bottleneck and want to measure **just** the encoder + output pipeline. Bypasses `BoardData_Get(BOARDDATA_AIN_LATEST, ...)` and writes synthetic test-pattern values directly. **Use this to isolate WiFi/USB/SD pipeline cost from ADC cost.**

PIPELINE mode requires a non-zero test pattern (`SYST:STR:TEST:PATtern`) — it skips ADC entirely so it has nothing to encode otherwise. The streaming task automatically rejects PIPELINE if test pattern is 0.

Usage example — comparing ADC-cost vs ADC-free pipeline:
```bash
# Setup (same for both)
SYST:STR:TEST:PATtern 3             # Fullscale (deterministic)
SYST:STR:INT 1                      # WiFi
ENA:VOLT:DC 4,1                     # 1×T1
SYST:STR:STATS:CLE

# Test 1: NOCAP — full path including ADC
SYST:STR:BENCH 1
SYST:STR:START 5000
# wait 8 s, then stop, snapshot WifiTcpBytesSent
SYST:STR:STOP
SYST:STR:STATS?

# Test 2: PIPELINE — same rate, ADC bypassed
SYST:STR:STATS:CLE
SYST:STR:BENCH 2
SYST:STR:START 5000
# wait 8 s, then stop
SYST:STR:STOP
SYST:STR:STATS?

SYST:STR:BENCH 0                    # Restore normal
```

If PIPELINE wire rate >> NOCAP wire rate at the same Hz, ADC is contributing to the bottleneck (cache pressure, ISR load, mutex contention with the encoder task).

**Empirical NOCAP-vs-PIPELINE curve (1×T1 PB on Tesla AP, fullscale, 6 s):**

| Rate | PIPELINE wire | NOCAP wire | ADC cost |
|----:|--------------:|-----------:|---------:|
| 1 kHz | 50 KB/s | 50 KB/s | 0 % |
| 2 kHz | 95 KB/s | 95 KB/s | 0 % |
| 3 kHz | 134 KB/s | 135 KB/s | 0 % |
| 5 kHz | 211 KB/s | 211 KB/s | 0 % |
| **8 kHz** | **194 KB/s** | **70 KB/s** | **–64 %** |
| **12 kHz** | **33 KB/s** | **1 KB/s** + `qd=1793` | **–97 %** |

**Below the Tesla wire ceiling (~5 kHz × 1 ch ≈ 230 KB/s), ADC is invisible** — encoder + WiFi keep up without contention. **Above wire ceiling, ADC pipeline (ISRs, EOS task, BoardData mutex) takes enough CPU that the encoder/output stalls.** NOCAP saturates earlier than PIPELINE because of this. The "ADC cost" reported by simple side-by-side numbers depends entirely on whether you tested at saturation or below — if anyone reports ADC as "free" without specifying rate, treat it skeptically.

**Throughput-claim discipline:** when reporting wire-rate measurements, always state the benchmark level and the test pattern. A "5 kHz / 230 KB/s" number is meaningless without "(NOCAP)" or "(PIPELINE)" — they measure different things. PIPELINE numbers represent the **upper bound** for streaming work that doesn't read the ADC; NOCAP numbers represent the **realistic** ceiling for actual data acquisition.

**Frequency-cap interaction:** in NOCAP and PIPELINE modes the freq cap is bypassed and `SYST:StartStreamData` accepts up to 100 kHz. The PIC32MZ timer can fire that fast but everything downstream usually can't keep up — expect `QueueDroppedSamples > 0` (encoder/queue saturation) at very high rates.

**2. Self-Contained Throughput Test** (`SYST:STR:THRoughput`): Runs a complete benchmark internally — enables benchmark mode + test pattern, streams for the specified duration, stops, and returns all stats in one response.

```bash
SYSTem:STReam:THRoughput <freq>,<duration_sec>   # Run benchmark
```

Example: `SYST:STR:THR 5000,10` streams at 5kHz for 10 seconds.

**Note:** This command blocks the USB SCPI task for the duration. Use the Python test suite (`test_throughput_benchmark.py`) for reliable automated benchmarking.

**3. SD Write Benchmark** (pre-existing): Measures raw SD card write speed independent of streaming.

```bash
SYSTem:STORage:SD:BENCHmark <size_kb>,<pattern>  # Run (e.g., 1024,0)
SYSTem:STORage:SD:BENCHmark?                      # Query results: bytes,ms,bps
```

**Measured Throughput Ceilings (NQ1, test patterns, 2026-04-13):**

| Interface | Format | Channels | Max Zero-Loss Rate | KB/s |
|-----------|--------|----------|------------------:|-----:|
| USB | PB | 1 | 16,200 Hz | 168 |
| USB | PB | 8 | 10,800 Hz | 259 |
| USB | PB | 16 | 8,200 Hz | 336 |
| USB | CSV | 1 | 16,200 Hz | 247 |
| USB | CSV | 8 | 8,600 Hz | 999 |
| USB | CSV | 16 | 6,000 Hz | 1,418 |
| SD | CSV | 1 | 11 kHz | 172 |
| SD | CSV | 8 | 3 kHz | 408 |
| SD | CSV | 16 | 2 kHz | 546 |
| SD | PB | 1 | 13 kHz | 148 |
| SD | PB | 8 | 7 kHz | 185 |
| SD | PB | 16 | 7 kHz | 199 |
| SD | raw write | — | — | 665 |

**WiFi STA characterization (NQ1, fullscale test pattern, Session 23 — 2026-04-25):**

> ⚠️ **All Session 23 (and earlier Session 21/22) WiFi ceilings are inflated.** Captured before the #371 silent-loss accounting fix; `WifiDroppedBytes` was reading 0 even when the streaming task silently dropped up to 86 % of encoded bytes (the per-iteration `hasWifi = (wifiSize >= 128)` gate skipped the entire WriteBuffer call when the circular buffer fell below 128 bytes free). The DAQiFi firmware path through WINC1500 delivers ~200-280 KB/s sustained on Tesla AP at this RSSI/distance — but whether the wall is the AP, the WINC1500 module, our SPI clocking, or our send pipeline is **unverified**. Until the Microchip WINC1500 iperf demo is flashed and benchmarked on the same hardware + AP (#377), this is the "firmware-path ceiling on our hardware," not an AP property. "Ceilings" reported below are mostly hitting that wall but were measured as clean because the silent drops weren't counted.
>
> **Do not use these numbers** for capacity planning. Spot-check with truthful counter (post-#371): 1×T1 PB real ceiling is ~3 kHz (not 7), 5×T1 PB is ~2 kHz (not 3), 16ch is ~2.5 kHz (not 1).
>
> Retrospective A/B planned in #373 to determine which prior throughput PRs actually moved firmware-path wire rate vs which were measurement artifacts. iperf demo A/B planned in #377 to establish the true wire-rate ceiling. Session 24 numbers will replace this table after both audits.

Single-trial ceiling sweep, no endurance. Best wire rate = **183 KB/s = 1.5 Mbps** (CSV 1×T1 OBD=OFF @ 8 kHz). WINC1500 spec is 5–10 Mbps real TCP — **~25 % of available bandwidth, 3-7× headroom**. WiFi-side bottleneck is `WifiDroppedBytes` in every leak (pipeline up to encoder is clean; WINC SPI staging is the bottleneck).

**SPI4 SCK (WINC bus) measurement (Saleae Logic 8, 100 MS/s):**
- **Clock = 16.67 MHz** (60 ns period, ±10 ns sampling jitter).
- **Bus is idle 92.9 %** of the time during a sustained ~100 KB/s WiFi stream (4.64 s of every 5 s window). 123,674 idle gaps averaging 37.5 µs each.
- Implication: clock is *not* the bottleneck. Theoretical 16.67 MHz × 1 byte = ~2 MB/s on the bus alone; we use ~5 % of that. The remaining 93 % is host-side (task scheduling, callback serialization, buffer underrun) — see #361 / #362 / #363.
- SPI4 baud is shared with SD card constraints; do not change as a "WiFi fix" without SD validation.

| Config | PB Hz | PB KB/s | CSV Hz | CSV KB/s |
|--------|------:|--------:|-------:|---------:|
| 1×T1 | 7,000 | 89 | 5,000 | 108 |
| 1×T2 | 7,000 | 96 | 6,000 | 103 |
| 1×T1 OBD=OFF | **8,000** | 107 | **8,000** | **183** |
| 1×T1 OBD=ON | 7,000 | 99 | 6,000 | 115 |
| 3×T1 | 5,000 | 113 | 3,000 | 116 |
| 3×T2 | 5,000 | 118 | 3,000 | 157 |
| 5×T1 | 3,000 | 104 | 700 | 65 |
| 5×T2 | 3,000 | 105 | 1,000 | 108 |
| 5×T1 OBD=OFF | 4,000 | 139 | 1,000 | 93 |
| 5×T1 OBD=ON | 3,000 | 104 | 1,000 | 103 |
| 8×T2 | 2,000 | 102 | 1,000 | 146 |
| 11×T2 | 2,000 | 135 | 700 | 174 |
| 5T1+3T2 (8ch) | 2,000 | 98 | 500 | 87 |
| 5T1+5T2 (10ch) | 2,000 | 125 | 100 | 23 |
| 5T1+11T2 (16ch) | 1,000 | 89 | 500 | — † |
| 5T1+11T2 OBD=OFF (16ch) | 1,000 | 85 | 300 | 102 |

† Reconnect mid-rate-step reset the byte counter; firmware-side counters confirmed clean stream. **OBD=OFF is lower than OBD=ON at 16ch** (300 vs 500 Hz) — opposite of the single-channel pattern; warrants multi-trial verification.

**Benchmark variance:** With the improved SPS measurement methodology (blocking FastReader, sleep-duration denominator), run-to-run variance is near zero for identical firmware. Previous ~10-15% variance was caused by unreliable first-byte-time SPS calculation.

Update this table when new benchmark results are collected. Full benchmark history is version-controlled in `daqifi-python-test-suite/benchmarks/` (CSV files + CHANGELOG.md). Run `python test_throughput_benchmark.py <port> <duration>` and copy the output CSV to that directory.

**Implementation:** `firmware/src/services/streaming.c` (benchmark mode, gTestPattern, Streaming_GenerateTestValue), `firmware/src/services/SCPI/SCPIInterface.c` (SCPI callbacks)

### Board Variants

- **NQ1**: Basic variant configuration
- **NQ2**: Enhanced variant with additional features
- **NQ3**: Full-featured variant

Each variant has specific:
- Channel counts and types
- ADC/DAC configurations
- Power management settings
- UI configurations

#### Switching Between Board Variants

The firmware supports building for different board variants using MPLAB X configurations:

1. **In MPLAB X IDE**:
   - Right-click project → Set Configuration → Select "Nq1" or "Nq3"
   - Build the project

2. **From Command Line**:
   ```bash
   # Build NQ1 firmware
   "/mnt/c/Program Files/Microchip/MPLABX/v6.25/gnuBins/GnuWin32/bin/make.exe" \
     -f nbproject/Makefile-Nq1.mk CONF=Nq1 build -j4

   # Build NQ3 firmware
   "/mnt/c/Program Files/Microchip/MPLABX/v6.25/gnuBins/GnuWin32/bin/make.exe" \
     -f nbproject/Makefile-Nq3.mk CONF=Nq3 build -j4
   ```

3. **How It Works**:
   - Each configuration includes/excludes variant-specific files
   - `NqBoardConfig_Get()` returns the appropriate board configuration
   - `NqBoardRuntimeConfig_GetDefaults()` returns the appropriate runtime defaults
   - No code changes needed - just switch configuration and rebuild

4. **Variant-Specific Files**:
   - **NQ1**: `NQ1BoardConfig.c`, `NQ1RuntimeDefaults.c`
   - **NQ3**: `NQ3BoardConfig.c`, `NQ3RuntimeDefaults.c`

### MCU Reference: PIC32MZ2048EFM144

**Architecture**: MIPS32 M-Class (microAptiv), 200 MHz, 32-bit data bus
**Flash**: 2MB | **RAM**: 512KB | **L1 Cache**: 16KB I-cache + 4KB D-cache
**Package**: 144-pin LQFP
**Datasheet**: [DS60001320](https://www.microchip.com/en-us/product/PIC32MZ2048EFM144)
**Errata**: [DS80000663](https://ww1.microchip.com/downloads/aemDocuments/documents/MCU32/ProductDocuments/Errata/PIC32MZ-Embedded-Connectivity-with-Floating-Point-Unit-Family-Silicon-Errata-DS80000663.pdf) (Rev R, silicon A1/A3/B2)

#### Atomicity & Concurrency Rules

- **32-bit reads and writes are atomic** on the PIC32MZ bus. A simple `x = 0` or `return x` on a `uint32_t`/`uint16_t` does NOT need a critical section.
- **Read-modify-write** (`x |= bit`, `x &= ~bit`, `x++`, `x += n`) is NOT atomic — use `taskENTER_CRITICAL()`/`taskEXIT_CRITICAL()`.
- **64-bit operations** (`uint64_t` increment, struct copy) always need a critical section.
- **`volatile`** is needed when a variable is written by one task/ISR and read by another — it prevents the compiler from caching the value in a register. `volatile` alone does NOT make RMW atomic; you still need critical sections for `+=`, `|=`, etc.
- **`volatile` also applies to "set-once-then-shared" pointers**, not just frequently-written variables. Pattern: a `static T *gp = NULL;` set in `Init()` (before the scheduler) and then dereferenced from BOTH SCPI tasks and priority-1 ADC ISRs. At -O3 GCC may hoist/merge address loads or cache cross-context reads of the data because the pointer "never changes" and the data has no volatile qualifier.

  Two qualifier forms are relevant; **understand the difference and pick deliberately**:
  - **`volatile T * p;` (data volatile, pointer non-volatile)** — the strictly-correct fix: every `p->field` access is a volatile access, so the compiler cannot cache, reorder, or elide reads. **Required when `p->field` is RMW'd or read across task↔ISR boundaries.** Caveat: this propagates qualifier-mismatch errors through every API that takes a `T *` derived from this pointer (callers must also accept `volatile T *` or cast at boundaries). For deeply-nested struct types (e.g. `tBoardConfig`) this is a substantial refactor.
  - **`T * volatile p;` (pointer volatile, data non-volatile)** — *partial* fix: the pointer is reloaded each access, but `*p` may still be register-cached. Defeats some specific -O3 miscompiles by poisoning aliasing analysis enough, but is **not a strict concurrency guarantee** — it's a pragmatic optimization-barrier hammer. Requires empirical justification (a real miscompile observed and fixed by the qualifier) before it's worth applying.
  - **`volatile T * volatile p;`** combines both — strictest, but inherits the data-volatile cascade.

  Quick rules of thumb:
  1. If the data hanging off the pointer is RMW'd across contexts → use the data-volatile form (or `volatile` on the specific RMW'd field).
  2. If reads are simple non-atomic loads but you suspect -O3 caching → either form usually works empirically; prefer the data-volatile form when the cascade cost is acceptable.
  3. **`volatile` does NOT make multi-byte / RMW accesses atomic.** Fields RMW'd across contexts still need critical sections regardless of volatile.
  4. **Don't add `T * volatile` to set-once shared pointers speculatively.** Microchip's published best-practice list (Harmony Sync/Async drivers tech brief DS90003269A, etc.) prescribes critical sections + mutexes for cross-context shared data — NOT `volatile` for set-once pointers. Add the qualifier only when there's a concrete -O3 miscompile that goes away with it. The audit in `docs/SET_ONCE_POINTER_AUDIT.md` (2026-05-10) verified empirically by codegen A/B + multi-trial bench A/B that the qualifier IS observable in codegen — adds redundant `lw v0,-32xxx(gp)` pointer reloads and inhibits loop strength reduction at -O3 — but those reloads are correct-but-redundant: every consumer path in this codebase crosses opaque function-call boundaries (FreeRTOS API, Harmony PLIB, BoardData_Get/Set) which already force reload of non-volatile statics at `-O3` + no LTO + this XC32 v4.60 build. The two branches (volatile/no-volatile) produced the same encoder-failure baseline (~2 per 3.2M samples) under deterministic 5 kHz × 30 s × 5 trials + rapid-restart × 3 trials. The original #354 ch15 regression (the speculation that motivated #421) was actually fixed by a hardware TRGSRC register configuration (`96e7c840`), not by volatile semantics. **Important methodology lesson from that audit:** n=1 bench results are not enough to conclude a rate difference — initial 1-trial run looked like the qualifier was load-bearing (1 failure on novol vs 0 on main), but multi-trial showed main ALSO had ~1 failure per ~1.6M samples. Always multi-trial before concluding load-bearing-ness. Re-audit if LTO is enabled, if a hot-loop function call is replaced with `__attribute__((always_inline))`, or if the XC32/GCC version changes.
- Do not add unnecessary critical sections around plain 32-bit stores/loads — it adds interrupt latency for no benefit.
- **ISR context vs task context**: `taskENTER_CRITICAL()`/`taskEXIT_CRITICAL()` is for **task context only**. Inside an ISR, use `taskENTER_CRITICAL_FROM_ISR()`/`taskEXIT_CRITICAL_FROM_ISR()`. In our firmware, hardware ISRs (streaming timer, ADC EOS) immediately defer to FreeRTOS tasks via `xTaskNotifyGive()`/`ulTaskNotifyTake()`, so all sample processing and critical section usage runs in task context — never directly in ISR handlers.
- **FPU in RTOS tasks**: Any task that uses floating-point (`float` or `double`) **must** call `portTASK_USES_FLOATING_POINT()` at the start of its task function, before any FP operations. This tells FreeRTOS to save/restore the 32×64-bit FPU registers on context switches. Without this call, FPU register state will be corrupted when switching between tasks. Currently registered: `app_USBDeviceTask`, `app_WifiTask`, `app_PowerAndUITask`, `streaming_Task`. **Pure-integer (no FPU):** `_Streaming_Deferred_Interrupt_Task` (intentional — saves 32 × 64-bit register save/restore per context switch), `app_SDCardTask`, `lWDRV_WINC_Tasks` (WINC driver — also runs the UDP announce / discovery callback). **Code that runs in a pure-integer task MUST NOT read or cast `double`/`float` fields** — that compiles to FPU instructions which pick up garbage from whichever FPU-using task last preempted. PR #369 fixed one instance of this (`adcMax = (uint32_t)Resolution - 1` in the streaming deferred task, producing 0x80000FE6 instead of 4095). Defense: store config values that pure-int tasks need to read as integer types (`uint32_t`, etc), not `double` — see `MC12bModuleConfig.Resolution` / `AD7609ModuleConfig.Resolution` in `AInConfig.h` for the canonical pattern.
- **`BoardRunTimeConfig_Get()` never returns NULL** — it indexes into a static array initialized at boot. Do not add NULL checks on its return value; no existing SCPI callback checks it, and adding guards creates inconsistency for zero safety benefit. Qodo/automated reviewers will repeatedly suggest this — ignore it.

#### Cache & DMA

- **L1 data cache is write-back** — DMA buffers MUST use `__attribute__((coherent))` or be placed in KSEG1 (uncached) to avoid stale data.
- **Cache line size**: 16 bytes — DMA buffers sharing a cache line with other data will cause corruption. Use `__attribute__((aligned(16)))`.
- **No hardware cache coherency** — software must invalidate/flush cache around DMA transfers. Harmony drivers handle this for SPI, USB, etc.
- **Coherent attribute** (`__attribute__((coherent, aligned(16)))`) maps buffers to uncached address space — simplest solution for DMA buffers.

#### Clock Tree (as configured)

| Clock | Source | Frequency | Peripherals |
|-------|--------|-----------|-------------|
| SYSCLK | SPLL (24MHz POSC × 50 / 3 / 2) | 200 MHz | CPU, REFCLK |
| PBCLK1 | SYSCLK/2 | 100 MHz | Watchdog |
| PBCLK2 | SYSCLK/2 | 100 MHz | I2C, UART, SPI (default) |
| PBCLK3 | SYSCLK/2 | 100 MHz | Timers, OC, IC |
| PBCLK5 | SYSCLK/2 | 100 MHz | Flash, Crypto, USB |
| REFCLK1 | SYSCLK (passthrough) | 200 MHz | SPI4 master clock (MCLKSEL=1) |

SPI4 BRG formula with REFCLK1: `SPI_CLK = 200MHz / (2 × (BRG + 1))`

#### Hardware FPU

The PIC32MZ2048**EF**M144 has a hardware 64-bit double-precision FPU (Coprocessor 1). All floating-point arithmetic (`double` multiply, divide, add, convert) executes in hardware — no software emulation.

- **Compiler**: targeting `PIC32MZ2048EFM144` implicitly sets `__mips_hard_float = 1`
- **FreeRTOS**: `configUSE_TASK_FPU_SUPPORT = 1` in `FreeRTOSConfig.h`; saves/restores 32×64-bit FPU registers on context switches for tasks that call `portTASK_USES_FLOATING_POINT()`
- **Registered tasks**: `app_USBDeviceTask`, `app_WifiTask`, `app_PowerAndUITask`, `streaming_Task`.  `_Streaming_Deferred_Interrupt_Task`, `app_SDCardTask`, and `lWDRV_WINC_Tasks` are intentionally pure-integer — see the FPU bullet in "Atomicity & Concurrency Rules" above.
- **ADC voltage conversion**: `ADC_ConvertToVoltage()` uses native `mul.d`, `div.d`, `add.d` instructions

#### FreeRTOS Configuration

- **Tick rate**: 1000 Hz (1ms tick)
- **Preemptive** with time-slicing (equal-priority tasks round-robin)
- **Max priorities**: 10 (0=lowest, 9=highest)
- **Heap**: heap_4, 284KB (`configTOTAL_HEAP_SIZE`)
- **ISR stack**: 8192 bytes
- **Kernel interrupt priority**: 1 (lowest)
- **Max syscall interrupt priority**: 4 (ISRs at priority 5+ are never disabled by FreeRTOS)

#### Task Priority Map

| Priority | Task | Stack (words) | Peak Used | Notes |
|----------|------|--------------|-----------|-------|
| 9 | `_Streaming_Deferred_Interrupt_Task` | 512 | 214 | ISR deferral, sample collection (no FPU — uses Q16 LUT for sine pattern) |
| 9 | `MC12bADC_EosInterruptTask` | 160 | 80 | ADC end-of-scan deferred interrupt |
| 9 | `AD7609_DeferredInterruptTask` | 160 | 76 | AD7609 BSY pin handler |
| 7 | `app_PowerAndUITask` | 512 | 226 | UI + BQ24297 power, FPU |
| 7 | `app_USBDeviceTask` | 3072 | 1290 | SCPI callbacks use shared response buffer (see below) |
| 6 | `streaming_Task` | 1392 | 692 | Encodes PB/CSV/JSON + outputs, FPU (CSV/JSON at precision>0) |
| 5 | `app_SDCardTask` | 1024 | 468 | SD mount/write/read/list/delete |
| 2 | `app_WifiTask` | 1500 | 780 | WiFi state machine + TCP + SCPI-over-TCP dispatch (post-#353 Opt 2: microrl + libscpi + handlers all run here instead of on WDRV_WINC_Tasks) |
| 2 | `fwUpdateTask` | 128 | 62 | WiFi FW update (dynamic) |
| 1 | `lWDRV_WINC_Tasks` | 1024 | 320 | WINC1500 driver. PR #492 (#489 variant B): dropped 2→1 so `streaming_Task` (pri 6) preempts WINC by 5 levels and OSAL semaphores inside `WDRV_WINC_Tasks` do the yielding (Microchip reference pattern). Eliminated #491 BIMODAL catastrophic WiFi drops. |
| 1 | `lAPP_FREERTOS_Tasks` | 1500 | 1156 | Boot init (77% used) |
| 1 | `F_USB_DEVICE_Tasks` | 144 | 72 | USB device stack |
| 1 | `F_DRV_USBHS_Tasks` | 144 | 72 | USB hardware driver |

Stack sizes profiled under stress: 16ch@5kHz PB/CSV/JSON + SD file ops + WiFi TCP + power cycles. Sized at 2-3x measured peak. Query at runtime: `SYST:MEM:STACk?`

**Note**: SD directory listing uses iterative traversal with a bounded BSS-backed stack (`SD_CARD_MANAGER_MAX_LIST_DEPTH = 16`, ~4.3 KB total). Task stack usage is O(1) regardless of FAT32 tree depth. Trees deeper than 16 levels emit a diagnostic and skip the subtree.

**SCPI shared response buffer**: Any SCPI callback needing ≥256 B of scratch MUST use `SCPI_ResponseBuf_Take()` / `SCPI_ResponseBuf_Give()` rather than a stack local. The shared buffer is a single 2048-byte static in BSS guarded by a statically-allocated mutex (`configSUPPORT_STATIC_ALLOCATION=1`). Rationale: WifiTask's stack is only 1024 words; the TCP → microrl → libscpi path consumes ~808 words before the callback runs, so a large stack-local buffer inside a SCPI callback can overflow (issue #347). Callers that use the shared pattern keep SCPI stack depth below the measured 372-word WifiTask peak. Current users: `SCPI_SysInfoGet`, `SCPI_SysInfoTextGet`, `SCPI_GetCommandHistory`, `SCPI_Help`, `SCPI_StorageSDBenchmark`.

**Scheduling implications**: Capture tasks at priority 9 preempt everything to guarantee deterministic sample timing. The encoder at priority 6 preempts WiFi/WINC/background (priority 2) and SD (priority 5), but stays below USB (7) so SCPI commands remain responsive during streaming. SD task at priority 5 sits above background transports but below encoder — prevents encoder from starving SD writes when USB+SD both active. Encoder's `Streaming_WriteWithRetry` uses `vTaskDelay(1)` (not `taskYIELD()`) in its retry loop so lower-priority SD actually gets CPU to drain circular buffer (#312). See `docs/PIPELINE_TIMING.md` for measurements (PR #308, Sessions 7-17).

#### Known Silicon Errata (DS80000663R, verified against PDF pages 6-15)

All items verified against the actual errata document. Only issues affecting features we use are listed, with exact workaround text from Microchip.

| Issue | Module | Rev | Summary | Our Status |
|-------|--------|-----|---------|------------|
| #1 | Oscillator | All | **REFCLK cannot divide inputs >100 MHz.** | **Safe.** Errata workaround: "do not divide the SYSCLK and allow the destination peripheral (SPI) to divide it. Set RODIV and ROTRIM to 0." This is exactly our PR #219 approach (RODIV=0 passthrough). |
| #5 | Power-Saving | A1/A3 | Turning off REFCLK via PMD bits causes unpredictable behavior. | **Safe.** We never disable REFCLK via PMD. Not affected on B2/B3. |
| #6 | I2C | A1/A3 | Indeterminate I2C at >100 kHz and/or >500 bytes continuous. False collision detect, receive overflow, suspended transactions. All recoverable in software. | **Monitor.** I2C5 for BQ24297 at 100 kHz with mutex. Not affected on B2/B3 but good to know. |
| #8 | UART | All | **RX FIFO overflow → shift registers stop → UART loses sync.** Only recovery: toggle UART OFF/ON multiple times. | **Low risk.** Debug UART4 only. Workaround: ensure UART interrupt priority prevents RX overrun, or set URXISEL for earlier interrupt. |
| #9 | USB | All | USB won't function if USB PHY off in Sleep (USBSSEN=1). | **Safe.** Keep USBSSEN=0. |
| #16 | USB | All | No remote wake-up support (USBRIE in USBCRCON). | **N/A.** Inform host via USB descriptors. |
| #25/#26 | Crypto | All | Crypto DMA: no partial packets, no zero-length hash. | **N/A.** wolfSSL runs in software mode. |
| #27 | SPI | All | **SRMT bit falsely indicates TX complete** before last block shifts out. Does NOT affect Transmit Buffer Empty Interrupt (STXISEL=0). | **Safe.** Harmony SPI driver uses interrupts, not SRMT polling. |
| #37 | I2C | All | **SCL tLOW doesn't meet I2C spec at ≥400 kHz.** No workaround. | **Safe.** BQ24297 I2C at 100 kHz. Never use ≥400 kHz on this chip. |
| #38 | System Bus | B2/B3 | **Flash wait states at SYSCLK >184 MHz with ECC need 3 wait states** (not 2). <2% CPU impact with cache. | **Safe.** Verified: `PRECONbits.PFMWS = 3` and `ECCCON = 3` in `initialization.c:645-646`. |
| #39 | ADC | All | Excessive current through VREF- when external reference used and VREF- > AVss. | **Monitor.** Workaround: connect VREF- to AVss. Check NQ3 board schematic. |
| #40 | USB | All | FLUSH bit (USBIENCSRx<19>) doesn't flush TX FIFO properly. | **Safe.** Harmony USB driver: set FLUSH + clear TXPKTRDY simultaneously, repeat twice. |
| #42 | DMA | All | **DMA half-full interrupt can fire twice** when cleared at n/2 byte, re-triggers at (n/2)+1. | **Safe.** Workaround: clear CHDHIF along with CHBCIF. Harmony DMA driver handles this. |
| #44 | Timer2-9 | All | Timer match coinciding with sleep entry → interrupt may not fire. No workaround. | **Safe.** We never enter sleep during streaming. |
| #45 | Flash RTSP | All | Run-Time Self Programming of Configuration Words broken. No workaround. | **Safe.** NVM settings use regular flash pages, not config words. |

### Memory Considerations

#### Four Memory Regions

The firmware uses four distinct memory regions, each with different properties:

1. **Streaming Buffer Pool** (194KB static BSS, `firmware/src/Util/StreamingBufferPool.c`)
   - Single `static uint8_t gPoolStorage[]` array, partitioned at each stream start
   - Contains: USB circular buffer + WiFi circular buffer + encoder buffer + SD circular buffer + sample pool + free-list
   - Layout: `[USB circ | WiFi circ | encoder | SD circ | <align> | samplePool[] | nextFree[]]`
   - Re-partitioned at each `StartStreamData` based on active interfaces
   - Zero runtime malloc — all resizing is pointer arithmetic within the pool
   - Auto-balance: USB-only → USB=64KB, WiFi=min, ~585 samples
   - Query: `SYST:MEM:FREE?` → `SamplePoolCount`, `SamplePoolBytes`

2. **Coherent Pool** (124KB static, DMA-safe, `firmware/src/Util/CoherentPool.c`)
   - Single `__attribute__((coherent, aligned(16)))` array in KSEG1 (uncached)
   - Bump allocator with named partitions, reset and re-partitioned at each stream start
   - Contains: SD DMA write buffer + USB DMA write buffer + WiFi SPI staging buffer. All three auto-balanced at stream start.
   - Query: `SYST:MEM:FREE?` → `CoherentPoolTotal`, `CoherentPoolFree`

3. **FreeRTOS Heap** (75KB, cached, `configTOTAL_HEAP_SIZE` in `FreeRTOSConfig.h`)
   - heap_4 (best-fit with coalescence), allocated from `.bss`
   - Contains: task stacks, FreeRTOS TCBs/queues/mutexes, sample FreeRTOS queue
   - Streaming buffers and sample pool are NOT in heap (moved to Streaming Buffer Pool)
   - Query: `SYST:MEM:FREE?` → `HeapTotal`, `HeapFree`, `HeapMinEverFree`

4. **USB Coherent Struct** (~2KB static, `gRunTimeUsbSttings __attribute__((coherent))`)
   - USB CDC DMA read buffer (512B) embedded in coherent struct
   - DMA write buffer pointer points into coherent pool (auto-balanced at stream start)
   - Must remain coherent for USB hardware driver DMA compatibility

#### RAM Budget (PIC32MZ 512KB)

| Region | Bytes | Source |
|--------|------:|--------|
| Streaming Buffer Pool | 198,656 | Static BSS (194KB) |
| FreeRTOS Heap | 75,000 | Static BSS |
| Coherent Pool | 126,976 | Static coherent (KSEG1) |
| USB coherent struct | ~2,000 | Static coherent |
| Other BSS/data (globals) | ~30,000 | Static BSS |
| ISR stack | 8,192 | Linker-allocated |
| **Total used** | **~441,000** | |
| **Free (linker headroom)** | **~83,000** | |

#### Heap Allocation Map (75KB total, ~62KB used at boot)

| Consumer | Bytes | Source |
|----------|------:|--------|
| Task stacks (14 tasks) | ~37,500 | `xTaskCreate` (profiled, see Task Priority Map) |
| FreeRTOS TCBs, mutexes, kernel | ~5,000 | Kernel internals |
| Sample FreeRTOS queue | ~4,500 | `xQueueCreate` in `AInSample.c` |
| DIO sample queue | ~3,200 | `xQueueCreate` in `DIOSample.c` |
| WiFi event queue | ~480 | `xQueueCreate` in `wifi_manager.c` |
| Other queues/mutexes | ~3,000 | Various modules |
| **Total used** | **~62,000** | |
| **Free after boot** | **~13,000** | `xPortGetFreeHeapSize()` |

**Note**: Sample pool and circular buffers are in the Streaming Buffer Pool, NOT heap. `HeapMinEverFree` should stay above 0. Monitor via `SYST:MEM:FREE?`. Task stack health via `SYST:MEM:STACk?`.

#### Dynamic Sample Pool

The sample pool lives inside the Streaming Buffer Pool (static BSS). It is re-partitioned at each `StartStreamData` — the pool depth adjusts automatically based on how much space remains after USB/WiFi/encoder buffers are carved out. O(1) free-list allocation is preserved.

- **Default size**: 1100 samples (`DEFAULT_AIN_SAMPLE_COUNT` in `AInSample.h`)
- **Range**: 100–10000 samples (`MIN_AIN_SAMPLE_COUNT`–`MAX_AIN_SAMPLE_COUNT`)
- **Memory per sample**: depends on enabled channels (compact pool): 1ch=14 bytes, 4ch=26 bytes, 8ch=42 bytes, 16ch=74 bytes (element + 2-byte free-list entry). Stride computed at stream start from `AInSampleList_ElementSize(channelCount)`.
- **Resize**: `StreamingBufferPool_Partition()` re-carves the pool, then `AInSampleList_InitializeExternal()` swaps the memory pointers. FreeRTOS queue is reused (not reallocated) across sessions.
- **When resized**: At each `StartStreamData` via `SCPI_StartStreaming`
- **Typical values**: Boot=1100, USB-only auto-balance=~585, multi-interface=varies (see Auto-Balance table)
- **Peak usage**: Typically 2-4 samples (at 3kHz 16ch). Pool depth provides burst absorption headroom.

#### SCPI Dynamic Memory Configuration

All setters reject changes while streaming is active (`SCPI_ERROR_EXECUTION_ERROR`).
Settings take effect at next `StartStreamData`. Runtime-only — not NVM-persisted, reset on reboot.

```bash
SYSTem:MEMory:SD:BUFfer <bytes>       # Set SD circular buffer size
SYSTem:MEMory:SD:BUFfer?              # Query (default: 32768)
SYSTem:MEMory:WIFI:BUFfer <bytes>     # Set WiFi circular buffer size
SYSTem:MEMory:WIFI:BUFfer?            # Query (default: 14000)
SYSTem:MEMory:USB:BUFfer <bytes>      # Set USB circular buffer size
SYSTem:MEMory:USB:BUFfer?             # Query (default: 16384)
SYSTem:MEMory:ENCoder:BUFfer <bytes>  # Set encoder buffer size
SYSTem:MEMory:ENCoder:BUFfer?         # Query (default: 8192)
SYSTem:MEMory:SAMPle:POOL <count>     # Set sample pool depth (0=auto)
SYSTem:MEMory:SAMPle:POOL?            # Query (default: 1100)
SYSTem:MEMory:FREE?                   # Full memory diagnostics
SYSTem:MEMory:AUTO                    # Auto-balance for enabled interfaces
```

All USB, WiFi, encoder, and sample pool memory comes from the unified Streaming Buffer Pool (194KB static BSS). Setting any value carves it from the pool; remaining space goes to the sample pool. Setting any field to a non-zero value disables auto-balance for all fields.

**Setter Bounds:**

| Command | Min | Max | Constraint |
|---------|----:|----:|------------|
| `SD:BUFfer` | 4096 | 65536 | Must be multiple of 512 (sector alignment). SD circular buffer is in streaming pool. |
| `WIFI:BUFfer` | 1400 | 65536 | Min = SOCKET_BUFFER_MAX_LENGTH |
| `USB:BUFfer` | 2048 | 65536 | Min = STREAMING_USB_MIN |
| `ENCoder:BUFfer` | 1024 | 65536 | Encoder staging buffer. 8KB optimal for USB, 16KB helps SD throughput. |
| `SAMPle:POOL` | 0 or 100 | 2000 | 0 = maximize with remaining pool space |

**`SYST:MEM:FREE?` Response Fields:**

| Field | Description |
|-------|-------------|
| `HeapTotal` | Total FreeRTOS heap (75000) |
| `HeapFree` | Currently free heap bytes |
| `HeapUsed` | Currently used heap bytes |
| `HeapMinEverFree` | Lowest heap free since boot (high-water mark) |
| `CoherentPoolTotal` | Total coherent pool (126976) |
| `CoherentPoolFree` | Free coherent pool bytes |
| `SdCircularSize` | Current SD circular buffer partition size |
| `SamplePoolCount` | Current sample pool depth |
| `SamplePoolBytes` | Sample pool data memory (count × 208) |
| `SampleNextFreeBytes` | Free-list array memory (count × 2) |
| `SampleQueueBytes` | FreeRTOS queue overhead estimate |

**`SYST:MEM:AUTO` Algorithm:**
1. Detect active interfaces via `Streaming_ComputeAutoBuffers()` (USB, WiFi, SD)
2. Streaming pool circular buffers: active interfaces get compile-time defaults (USB=64KB, WiFi=32KB, SD=32KB), inactive get minimums (USB=2048, WiFi=1400, SD=512)
3. Encoder buffer: 16KB when SD active (larger writes reduce SPI overhead), 8KB otherwise
4. Coherent pool DMA buffers: all three DMA consumers (SD write, USB write, WiFi SPI staging) are auto-balanced from the 124KB coherent pool. Inactive interfaces get minimum (SD=512, USB=512, WiFi=2KB). Active interfaces split remaining space: SD gets 50%, USB gets 30%, WiFi gets 20%. Single-active interface gets everything.
5. Re-partition streaming pool with computed sizes; sample pool gets all remaining space
6. CoherentPool_Reset() + re-alloc for SD DMA, USB DMA, and WiFi SPI staging buffers
7. Apply immediately: swap buffer pointers + re-init sample pool

**Auto-balance at stream start:** When all `MemoryConfig` fields are zero (boot default), auto-balance runs automatically at each `StartStreamData`. Setting any field to non-zero disables auto mode. The `SYST:MEM:AUTO` response includes `SdDma=<n>`, `UsbDma=<n>`, `WifiDma=<n>`, and `Encoder=<n>` showing the auto-balanced sizes.

**Auto-Balance Buffer Sizing by Active Interface:**

The `StreamingInterface` enum exposes four combinations: `USB`, `WiFi`, `SD`, and `UsbAndSd`.  USB+WiFi and WiFi+SD are not selectable — WiFi is always solo (SPI bus is shared with SD; USB+WiFi was never wired into the interface enum).  Values below are for 16-channel `AInSampleList_ElementSize` (74 B + 2 B free-list = 76 B/sample).

| Buffer | USB only | WiFi only | SD only | USB+SD |
|--------|---:|---:|---:|---:|
| USB circular (stream pool) | 65,536 | 2,048 | 2,048 | 65,536 |
| WiFi circular (stream pool) | 1,400 | 98,304 | 1,400 | 1,400 |
| SD circular (stream pool) | 512 | 512 | 32,768 | 32,768 |
| Encoder (stream pool) | 8,192 | 8,192 | 16,384 | 16,384 |
| SD DMA write (coherent) | 512 | 512 | 124,368 | 77,922 |
| USB DMA write (coherent) | 124,368 | 512 | 512 | 46,958 |
| WiFi SPI staging (coherent) | 2,048 | 125,904 | 2,048 | 2,048 |
| Sample pool (slots @16ch) | ~1,618 | ~1,178 | ~1,921 | ~1,086 |

All DMA buffers (SD write, USB write, WiFi SPI staging) are auto-balanced from the 124KB coherent pool — `Streaming_ComputeAutoBuffers` distributes the remaining pool space across active interfaces using weighted shares (SD=5, USB=3, WiFi=2), so the full pool is allocated regardless of which interfaces are active.  Single-active gets the whole pool; multi-active splits proportionally.

**Implementation:** `firmware/src/Util/StreamingBufferPool.c` (unified pool), `firmware/src/Util/CoherentPool.c` (DMA pool), `firmware/src/services/streaming.c` (`ComputeAutoBuffers`), `firmware/src/state/data/AInSample.c` (`InitializeExternal`), `firmware/src/services/SCPI/SCPIInterface.c` (SCPI callbacks), `firmware/src/state/runtime/StreamingRuntimeConfig.h` (MemoryConfig struct), `firmware/src/config/default/driver/winc/dev/spi/wdrv_winc_spi.c` (WiFi SPI staging)

#### Other Memory Constraints
- DMA buffers must be cache-aligned and in KSEG1 (coherent pool or coherent attribute)
- Bootloader reserves memory at 0x9D000000
- Application starts at 0x9D000480 (after bootloader)
- USB struct must remain `__attribute__((coherent))` — moving DMA buffers to pool causes boot failure

### Key Files for Understanding the System

1. **Application Entry**: `firmware/src/app_freertos.c` - Main application tasks
2. **Board Config**: `firmware/src/state/board/BoardConfig.c` - Hardware definitions
3. **Streaming Engine**: `firmware/src/services/streaming.c` - Data flow control
4. **SCPI Interface**: `firmware/src/services/SCPI/SCPIInterface.c` - Command processing
5. **HAL Drivers**: `firmware/src/HAL/ADC.c`, `DIO.c`, `DAC7718/DAC7718.c` - Hardware interfaces

### Development Considerations

- All hardware access must go through HAL layer
- **Prefer Harmony PLIB / driver APIs over direct SFR writes** for hardware abstractability. Order of preference: (1) PLIB function (`EVIC_SourceEnable`, `ADCHS_*`, `GPIO_*`), (2) SFR bitfield accessors (`ADCCON2bits.SAMC = val`), (3) raw register writes only when no PLIB/bitfield path exists AND performance demands it OR SET/CLR atomic forms are needed — always comment *why*
- SCPI commands follow IEEE 488.2 standard
- Use FreeRTOS primitives for synchronization
- Respect board variant differences in runtime checks
- Protocol buffer definitions in `services/DaqifiPB/DaqifiOutMessage.proto`

### Debugging discipline

Hardware debugging produces confident-sounding writeups that don't survive a "how do you know that?" question unless every claim is tagged with its epistemic source. Mixing levels of evidence is how we get wedged on the wrong root cause.

**Tag every load-bearing claim:**

| Tag | Meaning | Example |
|---|---|---|
| **V** | Verified from primary source — vendor code, datasheet, errata. Cite file:line or doc number. | `m2m_hif.c:115 short-circuits hif_chip_wake when u8HifRXDone==1` |
| **E** | Empirical from a test we ran. Cite the test output + date. | `Variant C produced Bytes=0 across 4/4 trials, /tmp/iperf_simple.out 2026-05-01` |
| **I** | Inference / hypothesis linking V and E. *Mark explicitly. Always offer the experiment that would close the gap.* | `the V mechanism is plausibly the cause of the E symptom — confirm by instrumenting u8ChipSleep` |
| **X** | External authority — Microchip docs, errata, forum threads, third-party writeups. Cite URL or doc number. | `DS80000663 erratum #5 documents this PMD behavior` |
| **N** | Our own prior notes — code comments we authored, memory files, PR descriptions. **Not external authority.** | `the line-588 comment in iperf2.c (we wrote it in PR #393)` |

**Rules:**

1. **Verify before assuming.** When the user asks for a test, the first action is to confirm device state (firmware version, association, NVM) — don't assume it's the same as last session. Flashing wipes NVM. APs change state. Last week's measurement is historical.
2. **Don't say "known [hazard / bug / behavior]" without an X-class citation.** Use "observed" or "hypothesized" instead.
3. **Don't cite a code comment we authored as evidence for the claim that comment makes.** That's circular — the comment was someone's prior I or E, not independent confirmation.
4. **V + E + plausibility ≠ proof.** When the mechanism (V) and the symptom (E) match by inference (I), say so explicitly and state the experiment that would close the gap. Three Is do not equal a V.
5. **Single-trial empirical results are E, not V.** Two independent reproductions are stronger E. Still not V.
6. **A/B tests must keep the firmware build, AP, and time window adjacent.** Comparing today's measurement to a number from a different firmware build, different AP state, or three weeks ago is a different test, not a comparison. Re-measure the baseline on the same firmware in the same session.
7. **Don't extrapolate test methodology that produced the wrong answer.** If a measurement tool was wrong (e.g., #371 silent-loss accounting bug, USB CDC slow-reader), pre-fix numbers from that tool are not "the baseline minus a bug" — they are unreliable, full stop. Re-measure with the fixed tool.

**Before posting a debug summary or PR description, scan it and label each load-bearing claim. If anything labeled I is load-bearing for a decision, either upgrade to V/E/X or downgrade the conclusion.**

If the user pushes back with "is this verified?" the honest answer is to enumerate which parts are V/E/I/X/N — not to defend the conclusion as a whole.

See user-memory `feedback_debugging_discipline.md` for the originating incident and the personal version of this rule.

### DAC7718 Integration (NQ3 Board)

The NQ3 board variant includes a DAC7718 8-channel 12-bit DAC for analog output functionality:

#### Hardware Configuration
- **SPI Interface**: SPI2 at 10 MHz (configurable)
- **Control Pins**: 
  - CS (Chip Select): RK0 (GPIO_PIN_RK0)
  - CLR/RST (Reset): RJ13 (GPIO_PIN_RJ13) 
  - LDAC: Tied to 3.3V (no GPIO control)
- **Power Requirements**: Requires 10V rail (available in POWERED_UP state)
- **Channels**: 8 analog output channels (0-7)
- **Resolution**: 12-bit (4096 levels)

#### SCPI Commands for DAC Control
```bash
# Set DAC channel voltage
SOURce:VOLTage:LEVel 0,5.0        # Set channel 0 to 5.0V
SOURce:VOLTage:LEVel 5.0          # Set all channels to 5.0V

# Read DAC channel voltage  
SOURce:VOLTage:LEVel? 0           # Read channel 0 voltage
SOURce:VOLTage:LEVel?             # Read all channel voltages

# Channel enable/disable
ENAble:SOURce:DC 0,1              # Enable channel 0
ENAble:SOURce:DC? 0               # Get channel 0 enable status

# Configuration commands
CONFigure:DAC:RANGe 0,1           # Set channel 0 range
CONFigure:DAC:UPDATE              # Update all DAC outputs

# Calibration commands
CONFigure:DAC:chanCALM 0,1.0      # Set channel 0 calibration slope
CONFigure:DAC:chanCALB 0,0.0      # Set channel 0 calibration offset
CONFigure:DAC:SAVEcal             # Save user calibration values
CONFigure:DAC:LOADcal             # Load user calibration values
```

#### Integration Points
- **Board Configuration**: `state/board/AOutConfig.h` - Analog output configuration structure
- **SCPI Module**: `services/SCPI/SCPIDAC.c` - DAC command implementation  
- **HAL Driver**: `HAL/DAC7718/DAC7718.c` - Hardware abstraction layer
- **Runtime Config**: `state/runtime/AOutRuntimeConfig.h` - Runtime channel states

#### Power-Safe Initialization
The DAC7718 uses lazy initialization that:
1. Initializes global structures at startup (power-independent)
2. Defers hardware initialization until first DAC command when power is sufficient
3. Checks for POWERED_UP state (provides 10V rail) before hardware access
4. Gracefully handles power state transitions

### BQ24297 IINLIM Management

The firmware uses a timer-based state machine to manage input current limits (IINLIM) on the BQ24297 battery management IC, replacing the earlier DPDM-dependent approach.

#### State Machine
```
IDLE → WAIT_DPDM (1s min, 3s timeout) → WAIT_USB (5s) → SETTLED
```

- **OTG pin** driven LOW (output) so DPDM starts at conservative 100mA
- **WAIT_DPDM**: Waits for BQ24297 DPDM detection to complete (REG07 bit 7 clears)
- **WAIT_USB**: Sets 500mA, waits 5s checking `UsbCdc_IsConfigured()`
- **SETTLED**: USB host → 500mA; wall charger → 2000mA
- VBUS loss in any state resets to IDLE

#### SCPI Diagnostic Commands
```bash
SYST:POW:BQ:REGisters?     # Dump all BQ24297 registers (REG00-REG0A hex values)
SYST:POW:BQ:ILIM <0-7>     # Set IINLIM directly (0=100mA, 2=500mA, 6=2A, 7=3A)
SYST:POW:BQ:DPDM           # Force DPDM re-detection (WARNING: disrupts USB — see below)
SYST:POW:BQ:DIAGnostics?   # Comprehensive diagnostics dump (battery, registers,
                            # GPIO, IINLIM state machine, power state, VBUS level)
```

**Note:** `SYST:POW:BQ:DIAG?` calls `BQ24297_UpdateStatus()` which reads REG09 and clears latched fault flags as a side effect.

**WARNING:** `SYST:POW:BQ:DPDM` is diagnostic-only. It forces the BQ24297 to re-run D+/D- detection and prints the result (VBUS type from REG08), but does **not** update the status struct or reset the IINLIM state machine — no automatic current switching occurs. Use `SYST:POW:BQ:ILIM` to manually set IINLIM after forced DPDM. When connected via USB, DPDM temporarily resets IINLIM (potentially to 100mA), causing VBUS sag and USB disconnect. A physical cable replug is required to recover and restart the IINLIM state machine.

#### Implementation
- **State machine**: `HAL/BQ24297/BQ24297.c` — `BQ24297_ManageIINLIM()`
- **Caller**: `HAL/Power/PowerApi.c` — `Power_Tasks()` (~100ms interval)
- **USB tracking**: `services/UsbCdc/UsbCdc.c` — `UsbCdc_IsConfigured()`
- **SCPI commands**: `services/SCPI/SCPIInterface.c`
- **I2C mutex**: `BQ24297_Read_I2C()` and `BQ24297_Write_I2C()` are protected by a FreeRTOS mutex to synchronize access between PowerAndUITask and USBDeviceTask (both priority 7)

## Firmware Analysis Report

A comprehensive technical analysis of the firmware has been completed and is available in `FIRMWARE_ANALYSIS_REPORT.md`. This report provides:

### Critical Findings
- **Performance Bottleneck**: 700-byte buffer limitation constraining all communication channels
- **Safety Risk**: Shared SPI bus between WiFi and SD card without mutex protection
- **Architecture Issues**: Flat task priority structure causing scheduling inefficiencies
- **Memory Management**: Dynamic allocation in real-time streaming paths

### Key Recommendations
1. **IMMEDIATE**: Implement SPI bus mutex protection
2. **IMMEDIATE**: Replace shared buffer with per-channel buffers
3. **HIGH**: Restructure FreeRTOS task priorities for real-time performance
4. **HIGH**: Implement UDP streaming for high-throughput applications
5. **MEDIUM**: Optimize memory management and eliminate dynamic allocation from streaming

### Performance Projections
- **Current WiFi**: ~5-10 Mbps → **Optimized**: 50-80 Mbps
- **Current SD**: ~2-5 MB/s → **Optimized**: 15-25 MB/s
- **Current ADC**: ~1 kHz → **Optimized**: 10+ kHz sample rates

The report includes detailed technical analysis, code examples, and a prioritized roadmap for optimization. Reference this document before making performance-critical modifications to understand current limitations and optimization opportunities.

### SD Card File Splitting

**Feature:** Automatic file splitting prevents FAT32 4GB file size limit issues during long logging sessions.

**Configuration:**
```bash
# Set maximum file size (range: 1000 bytes to 4GB)
SYST:STOR:SD:MAXSize 2000000000    # 2GB limit
SYST:STOR:SD:MAXSize 0              # Use filesystem maximum (3.9GB default)
SYST:STOR:SD:MAXSize?               # Query current setting
```

**File Naming:**
- First file: Uses original name (e.g., `experiment.csv`)
- Split files: Sequential 4-digit numbering (e.g., `experiment-0001.csv`, `experiment-0002.csv`)
- Supports up to 9999 files per session (~39TB @ 3.9GB each)

**CSV Headers:**
- First file: Full CSV header (device info, column names with "ain" prefix)
- Split files: Data rows only (no headers) - simplifies merging

**Safety Features:**
- Default: 3.9GB limit (100MB below FAT32 maximum)
- Minimum: 1000 bytes (prevents rapid rotation)
- Circular buffer draining before rotation (zero data loss)
- Unconditional filesystem flush before file close

**Python Tools:**
- `download_sd_files.py` - Auto-detects and groups split files
- `analyze_split_files.py` - Validates integrity, merges parts

**Implementation:** `firmware/src/services/sd_card_services/sd_card_manager.c`

### SD Card Sector-Aligned Writes

The WRITE_TO_FILE state extracts data from the circular buffer in 512-byte sector-aligned chunks. This allows FatFS to use its fast path (`disk_write()` directly from the user buffer) instead of the per-sector `memcpy` + dirty-flag path. Measured improvement: ~55% throughput gain on SPI-mode SD cards (~500 KB/s vs ~320 KB/s with same benchmark method).

**Key design rules:**
- **Normal writes** (WRITE_TO_FILE loop): Wait for ≥512 bytes available, extract in sector multiples up to `WBUFFER_SIZE`
- **Drain paths** (rotation and UNMOUNT): Do NOT sector-align — must flush all remaining data including sub-sector tails
- **UNMOUNT_DISK**: Drains both pending writeBuffer and circular buffer before file close to prevent data loss on streaming stop

**Implementation:** `firmware/src/services/sd_card_services/sd_card_manager.c` (search for `SD_SECTOR_SIZE_BYTES`)

### Logging System

The firmware includes a two-layer logging system: compile-time ceilings control which log calls are compiled into the binary, and runtime levels (controllable via SCPI) control which actually execute. Logs are stored in a circular buffer and retrieved via SCPI.

**Log Levels:**

| Level | Macro | Value | Use Case |
|-------|-------|-------|----------|
| NONE | `LOG_LEVEL_NONE` | 0 | Disable all logging for a module |
| ERROR | `LOG_LEVEL_ERROR` | 1 | Unexpected failures, hardware errors, critical issues |
| INFO | `LOG_LEVEL_INFO` | 2 | State changes, significant events (connection, mode changes) |
| DEBUG | `LOG_LEVEL_DEBUG` | 3 | Verbose diagnostics, data flow tracing, development |

**Runtime Default:** All modules default to `LOG_LEVEL_ERROR` at boot. `LOG_I()` and `LOG_D()` calls are compiled in but gated by the runtime level check (~2 CPU cycles per call when disabled).

**SCPI Commands:**
```bash
SYST:LOG?              # Retrieve all log messages (clears buffer after dump)
SYST:LOG:CLEAR         # Clear log buffer without reading
SYST:LOG:TEST          # Add test messages (for verification)
SYST:LOG:LEVel <module>,<level>   # Set module log level at runtime
SYST:LOG:LEVel? [module]          # Query module level (omit for all modules)
SYST:LOG:LEVel:ALL <level>        # Set all modules at once
```

**Runtime Log Level Commands:**

Module names: `POWER`, `WIFI`, `SD`, `USB`, `SCPI`, `ADC`, `DAC`, `STREAM`, `ENCODER`, `GENERAL`
Level values: `0`=NONE, `1`=ERROR, `2`=INFO, `3`=DEBUG

```bash
SYST:LOG:LEV STREAM,2      # Enable INFO logging for streaming engine
SYST:LOG:LEV ENCODER,3     # Enable DEBUG logging for PB/CSV/JSON encoders
SYST:LOG:LEV? WIFI         # Query WiFi module level
SYST:LOG:LEV?              # Dump all modules with levels and ceilings
SYST:LOG:LEV:ALL 1         # Reset all modules to ERROR (default)
```

All modules are compiled at DEBUG ceiling (all LOG_E/LOG_I/LOG_D calls present in binary) and have full runtime control up to DEBUG. The response shows the actual level set and the ceiling.

**ISR-safe logging:** `LOG_E`/`LOG_I`/`LOG_D` are ISR-aware — they detect ISR context via FreeRTOS `uxInterruptNesting` and automatically route through a deferred queue (`xQueueSendFromISR` + drain task). No separate ISR macros needed. Caveat: format args (`%d`, `%u`, etc.) are ignored in ISR context to avoid `vsnprintf` on the ISR stack — use static strings in ISR handlers.

**One-shot suppression:** Two bitmask-based one-shot systems prevent log flooding from high-frequency errors:

| Macro | Bitmask | Reset When | Use Case |
|-------|---------|-----------|----------|
| `LOG_E_ONCE(bit, ...)` | `gLogOneShot` | `SYST:LOG?` / `SYST:LOG:CLEAR` | ISR context (8-entry deferred queue) |
| `LOG_E_SESSION(bit, ...)` | `gSessionOneShot` | `Streaming_ClearStats()` at stream start | Streaming engine per-sample errors |

Both use `volatile uint32_t` bitmasks (up to 32 call sites each). Bit indices defined in `LogOnceBit_t` and `LogSessionBit_t` enums in `Logger.h`. The `|=` is not critical-section-protected — worst case is one extra duplicate message per priority-crossing race. Also available: `LOG_I_ONCE`, `LOG_D_ONCE`, `LOG_I_SESSION`, `LOG_D_SESSION`.

Runtime-only — not NVM-persisted, resets to ERROR on reboot.

**Module Mapping:**

| Module | Files |
|--------|-------|
| POWER | PowerApi.c, BQ24297.c |
| WIFI | wifi_manager.c, wifi_tcp_server.c, WINC driver |
| SD | sd_card_manager.c |
| USB | UsbCdc.c |
| SCPI | SCPIInterface.c, SCPIADC.c, SCPIDAC.c, SCPIDIO.c, SCPILAN.c, SCPIStorageSD.c |
| ADC | ADC.c, AD7609.c |
| DAC | DAC7718.c |
| STREAM | streaming.c |
| ENCODER | NanoPB_Encoder.c, csv_encoder.c, JSON_Encoder.c |
| GENERAL | app_freertos.c, AInSample.c, CircularBuffer.c, others |

**Circular Buffer Behavior:**
- Capacity: 64 messages, 128 bytes each
- When full: Oldest message is dropped to make room for new
- Thread-safe: Protected by FreeRTOS mutex
- ISR-safe: Detects ISR context and skips buffering (prevents deadlock)

**Compile-Time Ceiling Override (Advanced):**

All modules default to `LOG_LEVEL_DEBUG` compile-time ceiling (all log calls in the binary). To strip all logging from a module at compile time (zero binary size), override in Logger.h or the project defines:
```c
#define LOG_LEVEL_WIFI LOG_LEVEL_NONE  // Strip all WiFi logging from binary
```

**Real-Time UART Logging (Development Only):**

For live debugging without USB, enable ICSP UART output:
```c
// In project defines or Logger.h:
#define ENABLE_ICSP_REALTIME_LOG 1
```
This outputs logs via UART4 on ICSP pin 4 (RB0) at 921600 baud. Must be disabled before release.

**Implementation:** `firmware/src/Util/Logger.c`, `firmware/src/Util/Logger.h`

## Development Notes

### Building with Linux/WSL

The project can be built using Microchip tools in WSL/Linux:

#### Prerequisites in WSL
- MPLAB X IDE v6.25: `/opt/microchip/mplabx/v6.25/`
- XC32 Compiler v4.60: `/opt/microchip/xc32/v4.60/`
- Java Runtime (for IPE): `sudo apt install default-jre-headless`
- USB tools (for device access): `sudo apt install usbutils`

#### Building from Command Line
1. Navigate to project directory:
   ```bash
   cd /mnt/c/Users/User/Documents/GitHub/daqifi-nyquist-firmware/firmware/daqifi.X
   ```

2. Regenerate makefiles:
   ```bash
   # Linux-side (often FAILS with "Device pack missing" — DFP path differs):
   prjMakefilesGenerator -v .

   # Windows-side via PowerShell from WSL (VERIFIED WORKING 2026-05-06):
   powershell.exe -Command 'cd "C:\Users\User\Documents\GitHub\daqifi-nyquist-firmware\firmware\daqifi.X"; & "C:\Program Files\Microchip\MPLABX\v6.30\mplab_platform\bin\prjMakefilesGenerator.bat" -v .'
   ```

   **⚠️ `nbproject/Makefile-*.mk` files are gitignored** (auto-generated from
   `configurations.xml` by MPLAB X). After `git checkout` to a different
   commit, the on-disk makefiles do NOT update — they keep referencing
   source files from whichever branch was last built. Symptom: build fails
   with `No rule to make target '../src/.../<file>.c'` for a file added or
   removed by the new commit. **Fix:** regenerate using the Windows-side
   PowerShell command above. This is critical when bisecting — every
   checkout needs fresh makefiles.

3. Clean build directories (if previous build failed):
   ```bash
   rm -rf build dist
   ```

4. Build the project:
   ```bash
   make -f nbproject/Makefile-default.mk CONF=default build -j4
   ```

5. Output hex file location:
   ```
   dist/default/production/daqifi.X.production.hex (approx 1MB)
   ```

#### Programming with PICkit 4

##### From WSL (Verified Working Method)
```bash
"/mnt/c/Program Files/Microchip/MPLABX/v6.30/mplab_platform/mplab_ipe/ipecmd.exe" \
  -TPPK4 -P32MZ2048EFM144 -M -F"dist/default/production/daqifi.X.production.hex" -OL
```

##### From Windows PowerShell or Command Prompt
1. Navigate to project directory:
   ```cmd
   cd C:\Users\User\Documents\GitHub\daqifi-nyquist-firmware\firmware\daqifi.X
   ```

2. Program the device:
   ```cmd
   C:\"Program Files"\Microchip\MPLABX\v6.25\mplab_platform\mplab_ipe\ipecmd.exe -TPPK4 -P32MZ2048EFM144 -M -F"dist\default\production\daqifi.X.production.hex" -OL
   ```

**Important**: The `-OL` flag is required for successful programming

2. For Linux builds, the hex file will be at the WSL path:
   ```
   \\wsl$\Ubuntu\mnt\c\Users\User\Documents\GitHub\daqifi-nyquist-firmware\firmware\daqifi.X\dist\default\production\daqifi.X.production.hex
   ```

### Device Testing and SCPI Communication

#### USB CDC Host Read Speed (Critical for Zero-Loss Streaming)

USB CDC streaming is **zero-loss when the host reads at >=1ms intervals**. Slow host-side reading (e.g., 500ms polling loops) causes the device's USB circular buffer to fill, triggering packet drops on the firmware side. This was proven by A/B testing:
- Slow reader (500ms): USB CSV 16ch@3kHz -> 1,088,768 bytes dropped
- Fast reader (1ms): USB CSV 16ch@3kHz -> 0 bytes dropped

The firmware pipeline is leak-free. All reported USB byte drops in benchmarks prior to this finding were caused by insufficient host read speed, not firmware limitations.

**Python test suite:** Always use `StreamingMeasurement` from `test_harness.py` for any streaming rate measurement. Never use wall-clock time around SCPI start/stop commands — command latency varies by ~1s and causes 10% SPS error.

```python
from test_harness import ReliableSCPI, StreamingMeasurement, device_reset

m = StreamingMeasurement(scpi, scpi._serial)
m.start(freq=5000, duration=10, is_csv=True)  # CSV: PC counts newlines
# m.start(freq=5000, duration=10, is_csv=False)  # PB: PC counts varint frames
# m.start(..., wait_for_serial=False)  # WiFi/SD: data not on serial

# Results (all PC-derived, zero firmware trust for rate calculation):
# m.pc_sps, m.pc_kbps, m.pc_samples_window, m.leaked, m.fw_stats
```

The measurement window is PC-controlled: waits for first data byte, times for exactly `duration` seconds, then sends StopStreamData **outside** the window. For WiFi/SD tests, pass `wait_for_serial=False` since data arrives via TCP/SD card, not serial.

For simple streaming tests that don't need rate measurement (e.g., verifying SCPI fields), use `FastReader` directly for serial drain at 1ms intervals.

**Measured USB throughput (with StreamingMeasurement):**
- USB CSV 16ch@3kHz: 795 KB/s, 0 drops
- USB CSV 16ch@5kHz: 907 KB/s, 0 drops (frequency-capped to 3.5kHz)

#### USB CDC Write-Side Latency (Why pyserial capture is bursty)

USB streaming data delivery to the host is inherently bursty due to the firmware's USB CDC write pipeline. This is NOT data loss — data arrives intact but with variable latency.

**Root cause chain:**
1. **ZLP (Zero-Length Packet) on packet boundaries:** `USB_DEVICE_CDC_Write()` uses `DATA_COMPLETE` flag, which appends a ZLP when transfer size is a multiple of the USB max packet size. WSL's usbipd may buffer packets until the ZLP arrives.
2. **Serialized DMA writes:** Only one USB DMA transfer can be in-flight at a time (`writeTransferHandle`). New data waits in the circular buffer until the previous transfer completes and `UsbCdc_FinalizeWrite()` runs.
3. **Circular buffer → DMA copy path:** Data flows: streaming encoder → circular buffer → `CircularBuf_ProcessBytes()` → DMA buffer copy → `USB_DEVICE_CDC_Write()`. The process callback only runs when no transfer is pending.

**Impact on pyserial (especially WSL):**
- `ser.in_waiting` may return 0 while data is in the USB/usbipd pipeline
- Polling `ser.in_waiting` in a tight loop is unreliable — data arrives in bursts
- First streaming session after port open usually works; subsequent sessions need 3+ seconds settle time between stop and start
- Sending any SCPI command during streaming forces a short USB packet, "flushing" buffered data

**Workarounds for test scripts:**
- **Best:** Use SD card download via `verify_test_patterns.py --download` for deterministic verification
- **USB streaming:** Use `FastReader` from `test_harness.py` (1ms background thread) — most reliable
- **Simple scripts:** Sleep 3+ seconds after `StartStreamData`, then bulk `ser.read(ser.in_waiting)` — NOT polling

#### Important Testing Notes

1. **Picocom — ONLY for Simple Non-Streaming SCPI Queries**

   **Do NOT use picocom for streaming, benchmarking, or any multi-step test sequence.** Always use the Python test suite (`test_harness.py` → `ReliableSCPI` + `FastReader`) instead. Picocom has critical limitations that cause device crashes, false test results, and USB disconnects:

   **Problem 1: `-x` timeout is idle-based, not hard timeout.** When the device is streaming (PB or CSV), picocom receives continuous data and the "exit after N ms of inactivity" condition never triggers. The process hangs indefinitely.

   **Problem 2: Killing picocom during streaming corrupts WSL serial state.** After `kill -9 picocom`, the serial port often becomes unresponsive (opens but no data flows). Requires physical USB cable unplug/replug to recover.

   **Problem 3: Rapid picocom open/close can crash the device.** Each picocom session toggles DTR/RTS. Rapid cycling during streaming or buffer resize can cause USB CDC state corruption (Windows Code 43: "USB device descriptor failed"). Requires reprogramming to recover.

   **Recommended approach for streaming tests:**
   - Use `daqifi-python-core` library (`pip install -e /tmp/daqifi-python-core`)
   - `NyquistDevice` class handles binary PB framing, serial buffering, and clean disconnect
   - For SCPI commands not wrapped by python-core, use `device._comm.send_command("SYST:MEM:FREE?")`
   - See `daqifi-python-test-suite` for comprehensive test examples

   **Picocom safe usage (non-streaming only):**
   ```bash
   # Good: simple command/response
   (echo -e "*IDN?\r"; sleep 0.5) | picocom -b 115200 -q -x 1000 /dev/ttyACM0 | tail -5

   # Bad: streaming — picocom will hang
   (echo -e "SYST:STR:START 1000\r"; sleep 5; echo -e "SYST:STR:STOP\r") | picocom ...

   # Never use 2>&1 with picocom
   ```

   **Recovery if picocom hangs or device crashes:**
   ```bash
   pkill -9 picocom; fuser -k /dev/ttyACM0; rm -f /var/lock/LCK..ttyACM0
   # If port unresponsive: physically unplug/replug USB cable
   # If Windows shows Code 43: reprogram device via IPE
   ```

2. **Always use Python test suite for automated/streaming tests**

   Shell scripts with `dd | strings | grep` or picocom pipes are unreliable for:
   - Binary ProtoBuf data (corrupts text parsing)
   - Stats queries after streaming (stale data in serial buffer)
   - Multi-step test sequences (timing-dependent)
   - Throughput benchmarking (need precise timing)

   Use `daqifi-python-test-suite` and `daqifi-python-core`:
   ```bash
   cd /tmp
   git clone https://github.com/daqifi/daqifi-python-core.git
   git clone https://github.com/daqifi/daqifi-python-test-suite.git
   pip install --break-system-packages -e ./daqifi-python-core
   ```

   For new SCPI commands not yet in python-core, use `device._comm.send_command()`.
   Add new test scripts to the test suite when building new features.

3. **Power State Required**: Always power up the device before attempting WiFi or DAC operations
   ```bash
   # Check power state (0=STANDBY, 1=POWERED_UP, 2=POWERED_UP_EXT_DOWN)
   (echo -e "SYST:POW:STAT?\r"; sleep 0.5) | picocom -b 115200 -q -x 1000 /dev/ttyACM0 | tail -5
   
   # Power up device for full functionality (enables 10V rail needed for DAC7718)
   (echo -e "SYST:POW:STAT 1\r"; sleep 1) | picocom -b 115200 -q -x 1000 /dev/ttyACM0 | tail-5
   ```

3. **Serial Buffer Timing**: When issuing multiple SCPI commands via picocom (especially `LISt?`, `LOG?`, or other commands that produce large output), wait long enough for the full response to be received before sending the next command. If the previous response hasn't fully drained from the serial buffer, the next command's picocom session will capture stale data mixed with the new response. For large responses (SD file listings, file downloads), use `sleep 5` or longer between commands. The `SYSTem:STORage:SD:GET` command is particularly sensitive — ensure the serial buffer is clean before issuing it.

4. **Use Exact SCPI Commands**: Don't guess command syntax. Check the actual command table in `SCPIInterface.c`
   ```bash
   # Correct commands (from SCPIInterface.c):
   SYST:COMM:LAN:SSIDSTR?    # Signal strength (not SSID:STR)
   SYST:COMM:LAN:NETTYPE?     # Network type (not MODE or NETWORK)
   SYST:COMM:LAN:ADDR?        # IP address (not ADDRESS or IP)
   ```

#### USB Device Access from WSL
1. **Initial device attachment** (from Windows PowerShell as admin):
   ```powershell
   usbipd list
   # Find DAQiFi device (e.g., "USB Serial Device (COM3)")
   usbipd attach --wsl --busid <BUSID>
   ```
   Note: May require `--force` if USBPcap filter is installed

2. **Reconnecting device from WSL side** (after PC sleep or device reprogramming):
   ```bash
   # List USB devices to find DAQiFi
   powershell.exe -Command "usbipd list"
   # Look for: 2-4    04d8:f794  USB Serial Device (COM3)
   
   # Attach from WSL
   powershell.exe -Command "usbipd attach --wsl --busid 2-4"
   
   # Wait for device to appear
   sleep 2 && ls -la /dev/ttyACM*
   ```

3. **Verify device in WSL**:
   ```bash
   lsusb | grep -i "04d8"
   # Should show: Microchip Technology, Inc. Nyquist
   
   ls -la /dev/ttyACM*
   # Should show: /dev/ttyACM0
   ```

#### Sending SCPI Commands
Use picocom for reliable serial communication:
```bash
# Query device identification
(echo -e "*IDN?\r"; sleep 0.5) | picocom -b 115200 -q -x 1000 /dev/ttyACM0 2>&1 | tail -20

# Common SCPI commands:
# WiFi configuration
(echo -e "SYST:COMM:LAN:SSID?\r"; sleep 0.5) | picocom -b 115200 -q -x 1000 /dev/ttyACM0 2>&1 | tail -20
(echo -e "SYST:COMM:LAN:SSID \"NetworkName\"\r"; sleep 0.5) | picocom -b 115200 -q -x 1000 /dev/ttyACM0 2>&1 | tail -20

# Power management
(echo -e "SYST:POW:STAT?\r"; sleep 0.5) | picocom -b 115200 -q -x 1000 /dev/ttyACM0 2>&1 | tail -20
(echo -e "SYST:POW:STAT 2\r"; sleep 0.5) | picocom -b 115200 -q -x 1000 /dev/ttyACM0 2>&1 | tail -20
```

Power states:
- 0: STANDBY (MCU on if USB powered, off if battery powered)
- 1: POWERED_UP (Board fully powered, all modules active)
- 2: POWERED_UP_EXT_DOWN (Partial power, external power disabled - low battery mode)

#### Windows Network Access
Check WiFi networks from WSL using Windows netsh:
```bash
# List all visible WiFi networks
powershell.exe -Command "netsh wlan show networks"

# Check current WiFi interface status
powershell.exe -Command "netsh wlan show interfaces"
```

#### DAQiFi WiFi Access Point
When powered up, the device creates an open WiFi access point:
- Default SSID: "DAQiFi-XXXX" (where XXXX is the last 4 hex digits of the MAC address, e.g. "DAQiFi-95A7")
- Default Hostname: Same as SSID (used for DHCP Option 12 network discovery)
- Authentication: Open (no password)
- The device must be in POWERED_UP state for WiFi to operate
- The MAC suffix is automatically applied whenever the SSID or hostname equals the bare default "DAQiFi" (including after factory reset and APPLY). User-customized values are preserved without suffix.

### Automated Testing Notes

#### Quick Device Recovery Script
Create a script to quickly reconnect the device:
```bash
#!/bin/bash
# reconnect_device.sh
echo "Reconnecting DAQiFi device..."
powershell.exe -Command "usbipd attach --wsl --busid 2-4"
sleep 2
if [ -e /dev/ttyACM0 ]; then
    echo "Device connected at /dev/ttyACM0"
else
    echo "Device not found, checking USB..."
    lsusb | grep -i "04d8"
fi
```

#### WiFi Configuration Test Sequence
1. **Power off**: `SYST:POW:STAT 0`
2. **Configure SSID**: `SYST:COMM:LAN:SSID "TestSSID"`
3. **Apply settings**: `SYST:COMM:LAN:APPLY`
4. **Power on**: `SYST:POW:STAT 1`
5. **Check networks**: `cmd.exe /c "netsh wlan show networks"`
6. **Save settings**: `SYST:COMM:LAN:SAVE`

#### Key Testing Insights
- WiFi initialization takes ~2-3 seconds after power up
- APPLY command needed to activate runtime settings
- SAVE command needed to persist to NVM
- Device may load saved NVM settings on power up, not runtime settings
- Power states: 0=STANDBY, 1=POWERED_UP, 2=POWERED_UP_EXT_DOWN

### Shell Script Wrapper for Device Commands

To avoid permission prompts with complex piped commands, create temporary shell scripts:

**IMPORTANT**: Always use the filename `/tmp/temp.sh` for test scripts. This allows permissions to persist once approved in `.claude/settings.local.json`.

```bash
#!/bin/bash
# Always use: /tmp/temp.sh
send_cmd() {
    local cmd="$1"
    local delay="${2:-0.5}"
    (echo -e "${cmd}\r"; sleep $delay) | picocom -b 115200 -q -x 1000 /dev/ttyACM0 2>&1 | tail -20
}

# Use the function
send_cmd "SYST:POW:STAT 0" 1
send_cmd "*IDN?"
```

Then execute with:
```bash
chmod +x /tmp/temp.sh
dos2unix /tmp/temp.sh  # Fix line endings if needed
/tmp/temp.sh
```

This approach:
- Avoids complex inline piped commands that trigger prompts
- Makes tests repeatable and easier to debug
- Uses consistent filename `/tmp/temp.sh` for persistent permissions
- Add to `.claude/settings.local.json` permissions: `"Bash(/tmp/temp.sh:*)"`

### Git Configuration
- Ignore line ending changes when reviewing diffs (Windows/Linux compatibility)
- ALWAYS test changes on hardware before committing
- Use descriptive commit messages that explain the problem and solution

### Commit Message Format

This project uses **Conventional Commits** format for all commit messages:

```
<type>(<scope>): <subject>

<body>

<footer>
```

#### Types
- `feat`: New feature
- `fix`: Bug fix
- `docs`: Documentation only changes
- `style`: Changes that don't affect code meaning (white-space, formatting)
- `refactor`: Code change that neither fixes a bug nor adds a feature
- `perf`: Performance improvement
- `test`: Adding missing tests or correcting existing tests
- `build`: Changes to build system or dependencies
- `ci`: Changes to CI configuration files and scripts
- `chore`: Other changes that don't modify src or test files

#### Examples
```
fix(power): enable BQ24297 OTG mode for battery operation

The device was powering off when USB disconnected due to insufficient
voltage for the 3.3V regulator. OTG boost mode provides 5V from battery.

Fixes #23
```

```
feat(scpi): add battery diagnostics to SYST:INFO command

Added comprehensive battery status information including charge state,
voltage, NTC status, and power-up readiness.
```

#### Guidelines
- Use present tense ("add" not "added")
- Use imperative mood ("fix" not "fixes" or "fixed")
- First line should be 72 characters or less
- Reference issues and PRs in the footer
- Breaking changes should be noted with `BREAKING CHANGE:` in the footer

## Continuous Testing and Automation

### Setting Up for Unprompted Testing

1. **Permissions Configuration** (`.claude/settings.local.json`):
   ```json
   {
     "permissions": {
       "allow": [
         "Bash(*)"
       ],
       "deny": []
     }
   }
   ```
   - Use wildcard `Bash(*)` for unrestricted testing
   - Avoids interruption prompts during test execution
   - Can be more restrictive for production use

2. **Device Connection from WSL**:
   ```bash
   # List USB devices
   powershell.exe -Command "usbipd list"
   
   # Attach DAQiFi device (usually 2-4)
   powershell.exe -Command "usbipd attach --wsl --busid 2-4"
   
   # Verify connection
   ls -la /dev/ttyACM0
   ```

3. **Programming Device from WSL**:
   ```bash
   # Using Windows IPE (more reliable than WSL passthrough)
   cmd.exe /c "C:\"Program Files\"\\Microchip\\MPLABX\\v6.25\\mplab_platform\\mplab_ipe\\ipecmd.exe -P32MZ2048EFM144 -TPPK4 -F\"C:\\path\\to\\firmware.hex\" -M -OL"
   
   # IMPORTANT: After programming, always reattach the device to WSL
   powershell.exe -Command "usbipd attach --wsl --busid 2-4"
   ```

### Real-Time Testing Guidelines

0. **Background Python: always `-u`, never wrap a long run in a tight `timeout`.**
   When launching a test-suite probe / bench script in the background and
   monitoring it from outside, **two failure modes bite repeatedly**:
   - **Buffering:** plain `python3 script.py > log 2>&1 &` is block-buffered
     when stdout isn't a TTY, so the log looks *empty* while the run is
     actually progressing — and if the process is killed (SIGTERM/timeout),
     the buffered output is **lost entirely**. Always run `python3 -u` (or
     set `PYTHONUNBUFFERED=1`) so lines stream live and survive a kill.
   - **Aggressive `timeout`:** wrapping a multi-trial/overnight run in
     `timeout 200 ...` kills it mid-run (bench runs are slower than you
     estimate — per-trial SCPI overhead alone is ~15-20s), and combined
     with buffering you get *nothing* back. Don't cap a real run with a
     short `timeout`. Instead launch with **no timeout** and watch via a
     PID-exit waiter (`while kill -0 $PID; do sleep N; done`) or by polling
     the live `-u` log / the CSV. Reserve `timeout` for genuinely-bounded
     one-shot probes, and even then size it generously.
   - The CSV/JSONL the probe fsyncs per row is the reliable progress
     signal; the stdout log is secondary. (Lesson logged after repeatedly
     tripping on buffering+timeout, 2026-06-02.)

0b. **Sustained high-rate USB streaming: prefer Windows-native Python
   over WSL.** The `ISR=-1` / unreadable-`STATS?` "wedge" seen under
   sustained high-rate USB streaming is a **WSL/usbipd passthrough
   artifact, not a device problem** (a single 12 kHz trial streams +
   reads cleanly; the wedge is cumulative across many back-to-back
   high-rate trials). Running `python.exe` (Windows) + pyserial against
   the device's **COM port (COM3 = bench primary)** bypasses usbipd
   entirely and avoids the wedge — no attach/detach, no recovery dance.
   For multi-hour / high-rate runs (the #524 matrix, endurance), prefer
   the Windows host; WSL `/dev/ttyACMn` is fine for low-rate /
   control-plane SCPI. The test-suite framework is mostly portable
   (pyserial; `fcntl`/SIOCINQ guarded; TcpDrain cross-platform) — pass
   `--port COM3`; the repo must be on `C:\...` for Windows python to run
   it. Distinct from the WSL-launched-python UDP-drop issue (that's UDP;
   serial/TCP on native Windows is fine). (User guidance 2026-06-03.)

1. **Avoid Echo Output to Windows**:
   - Don't echo test results - Claude should observe and report internally
   - Reduces prompting issues and improves test speed
   - Example: Instead of `echo "Test passed"`, observe results and summarize

2. **Efficient Test Commands**:
   ```bash
   # Good: Direct picocom command with minimal output
   (echo -e "*IDN?\r"; sleep 0.5) | picocom -b 115200 -q -x 1000 /dev/ttyACM0 2>&1 | tail -5
   
   # Avoid: Multiple echo statements that trigger prompts
   ```

3. **State Management for Testing**:
   - Always ensure device is in known state before tests
   - Power cycle if needed: `SYST:POW:STAT 0` then `SYST:POW:STAT 1`
   - Clear error queue: `SYST:ERR?` until "No error"
   - Abort ongoing operations: `ABOR`

4. **Test Automation Scripts**:
   - Create self-contained test scripts that manage state
   - Include setup, test, and cleanup phases
   - Return clear pass/fail status
   - Log results to files for post-analysis

### Testing Best Practices

1. **Device State Verification**:
   ```bash
   # Check device ready
   *IDN?  # Should return DAQiFi info
   
   # Check power state
   SYST:POW:STAT?  # 0=STANDBY, 1=POWERED_UP, 2=POWERED_UP_EXT_DOWN
   
   # Check errors
   SYST:ERR?  # Should be "No error"
   ```

2. **Common Testing Patterns**:
   - Input filtering: Send dangerous characters, verify they're filtered
   - Command execution: Send valid commands, verify responses
   - Error handling: Send invalid commands, check error messages
   - State persistence: Change settings, power cycle, verify retained

3. **Automated Test Execution**:
   - Use bash scripts with proper error handling
   - Capture output for analysis without displaying
   - Report summary results only
   - Example structure:
     ```bash
     #!/bin/bash
     # Setup
     device_setup() { ... }
     
     # Tests
     test_feature() { 
         result=$(send_command "...")
         [[ "$result" == "expected" ]] && return 0 || return 1
     }
     
     # Run tests and report
     device_setup
     test_feature && echo "PASS" || echo "FAIL"
     ```

### DAQiFi Test & API Repositories

The DAQiFi GitHub organization (https://github.com/daqifi) hosts companion libraries and test suites useful for firmware validation and regression testing:

#### Python
- **[daqifi-python-core](https://github.com/daqifi/daqifi-python-core)** — Python API library for DAQiFi Nyquist devices
  - `daqifi/` package: NyquistDevice class, channels, streaming, discovery, SCPI internals
  - `examples/`: basic_adc, basic_dac, basic_dio, device_discovery, streaming (async/basic/callback/context)
  - Dependencies: pyserial>=3.5, Python 3.7+

- **[daqifi-python-test-suite](https://github.com/daqifi/daqifi-python-test-suite)** — Comprehensive firmware test suite
  - `comprehensive_test.py` — YAML-based full test framework
  - `test_sd_card.py` — Interactive SD card testing menu
  - `test_usb_transfer_integrity.py` — USB data corruption testing
  - `test_5khz_single_channel.py`, `test_high_speed_rates.py` — ADC performance tests
  - `download_sd_files.py`, `analyze_split_files.py` — SD file split handling/validation
  - `analyze_csv_integrity_v3.py` — CSV data integrity analysis

#### .NET
- **[daqifi-core](https://github.com/daqifi/daqifi-core)** — .NET library (net8.0/net9.0) for DAQiFi devices
  - Communication layer: SCPI, Protobuf message parsing/production, Serial & TCP transport
  - Device control: DaqifiDevice, DaqifiStreamingDevice
  - Test project: 16 xUnit test classes (communication, transport, device, integration)

- **[daqifi-desktop](https://github.com/daqifi/daqifi-desktop)** — Windows desktop application (WPF, net8.0)
  - Full device control UI with live graphing
  - 4 test projects with xUnit

#### Other
- **[daqifi-java-api](https://github.com/daqifi/daqifi-java-api)** — Java API
- **[daqifi-node](https://github.com/daqifi/daqifi-node)** — Node.js package and NodeRED plugin
- **[daqifi-labview](https://github.com/daqifi/daqifi-labview)** — LabVIEW interface
- **[daqifi_nyquist_arduino](https://github.com/daqifi/daqifi_nyquist_arduino)** — Arduino interface
- **[daqifi-core-example-app](https://github.com/daqifi/daqifi-core-example-app)** — .NET example app

#### Using Python Test Suite for PR Validation
```bash
# Clone if not already present
git clone https://github.com/daqifi/daqifi-python-core.git
git clone https://github.com/daqifi/daqifi-python-test-suite.git

# Install dependencies
pip install -e ./daqifi-python-core

# Run comprehensive tests
cd daqifi-python-test-suite
python comprehensive_test.py

# Run SD card tests specifically
python test_sd_card.py
```

#### Key Test Scripts for Firmware Validation

| What to validate | Script | Duration |
|-----------------|--------|----------|
| Streaming throughput ceilings | `test_overnight_characterization.py` | ~2-4 hours |
| Quick ceiling check | `test_interface_ceilings.py` | ~30 min |
| Data loss counters (#295-297) | `test_silent_loss_observability.py` | ~3 min |
| Test pattern integrity | `verify_test_patterns.py --run-all` | ~10 min |
| A/B branch comparison | `test_ab_comparison.py` | ~20 min |
| Full device validation | `comprehensive_test.py` | ~5 min |

**Critical rule:** Any test that measures streaming rate MUST use `StreamingMeasurement` from `test_harness.py`. See the class docstring for usage. Wall-clock timing around SCPI start/stop is forbidden — it causes 10% measurement error from variable command latency.

### Known Issues and Workarounds

1. **USBPcap Filter**: Interferes with device passthrough
   - Use `--force` flag if needed
   - Or program from Windows side directly

2. **Line Ending Issues**: Windows creates CRLF in scripts
   - Fix with: `dos2unix script.sh` or use `sed -i 's/\r$//'`
   - Or create scripts directly in WSL

3. **Permission Prompts**: Even with wildcards, complex commands may prompt
   - Keep commands simple
   - Use script files instead of complex one-liners
   - Batch related commands together

4. **wolfSSL Must Stay at v5.4.0**: MCC Content Manager pushes wolfSSL v5.7.0 which **breaks the build** (confirmed Microchip bug — missing `types.h`, `WOLF_ENUM_DUMMY_LAST_ELEMENT` macro errors, `NO_BIG_INT` conflicts). See [Microchip forum thread](https://forum.microchip.com/s/topic/a5CV4000000249BMAQ/t397847). After any MCC session, revert wolfSSL changes:
   ```bash
   git checkout -- firmware/src/third_party/wolfssl/
   git clean -fd -- firmware/src/third_party/wolfssl/
   git checkout -- firmware/src/config/default/harmony-manifest-success.yml
   git checkout -- firmware/daqifi.X/nbproject/configurations.xml
   # Then regenerate Makefiles and remove references to deleted wolf files
   # (dilithium, kyber, xmss, lms, sphincs, sm2/3/4, hpke)
   ```
- pic32 tris convention is 1=input and 0=output
- when implementing new code/features remember that we have multiple configurations so we need to ensure we are keeping all the structs consistant as well as the handling functions, etc.
- always verify scpi command syntax - don't guess.
- we don't need to generate analysis docs or the like unless asked
- all errors should go through the error logging function and doesn't need to be sent out in the stream at all. if there is an
 error, the SCPI command should return the error through its own handling. then the user calls SYSTem:LOG? to know what the
error was.
- SCPI data visibility principle: prevent users from operating with improper settings (return SCPI errors for config problems like reading disabled channels), but maximize visibility into device health. When data is stale (e.g., monitoring channels frozen during OBDiag=0 streaming), show last-known values with age indicators rather than hiding them. Error on config problems, inform on stale data.