# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is the DAQiFi Nyquist firmware project - a multi-channel data acquisition device built on PIC32MZ microcontroller with FreeRTOS. The project consists of a USB bootloader and main application firmware.

## Build Instructions

Toolchain on this station: **MPLAB X v6.30** (`C:\Program Files\Microchip\MPLABX\v6.30`), **XC32 v4.60** (also at `/opt/microchip/xc32/v4.60` Linux-side). Older v6.25 paths in scripts/history are stale — v6.25 is uninstalled.

### In MPLAB X
- **Standalone (bench) build**: exclude all linker files in Project Properties; links app at `0x9D000000`. PICkit-flash only — **never ship** (clobbers the bootloader on customer devices).
- **Bootloader-compatible (release) build**: include `old_hv2_bootld.ld`, exclude `p32MZ2048EFM144.ld`; links app at `0x9D000480`. Flash the bootloader first (`bootloader/firmware/usb_bootloader.X`), then load firmware via the Windows DAQiFi app or the bootloader project's Loading menu.

### From the command line (WSL)
```bash
cd firmware/daqifi.X
"/mnt/c/Program Files/Microchip/MPLABX/v6.30/gnuBins/GnuWin32/bin/make.exe" \
  -f nbproject/Makefile-default.mk CONF=default build -j$(nproc)
# output: dist/default/production/daqifi.X.production.hex (~1 MB, standalone layout)
```
If a previous build failed, `rm -rf build dist` first.

**⚠️ Makefiles are gitignored** (`nbproject/Makefile-*.mk` are generated from `configurations.xml`). After checking out a commit that adds/removes source files, the stale on-disk makefiles fail with `No rule to make target '../src/.../<file>.c'`. Regenerate from Windows (the Linux-side `prjMakefilesGenerator` often fails with "Device pack missing"):
```bash
powershell.exe -Command 'cd "C:\Users\User\Documents\GitHub\daqifi-nyquist-firmware\firmware\daqifi.X"; & "C:\Program Files\Microchip\MPLABX\v6.30\mplab_platform\bin\prjMakefilesGenerator.bat" -v .'
```
This is critical when bisecting — every checkout needs fresh makefiles.

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

### Programming with PICkit 4 (ipecmd)

Preferred wrapper on this dev station: `bash ~/.claude/skills/flash/flash.sh [--build]` (user-local helper, not repo-tracked) — builds (optional), flashes, reattaches usbipd, verifies a `/dev/ttyACM*` node appears (node presence only — board *identity* still needs the device verification protocol below). Without the wrapper, use the direct invocation, which works anywhere:
```bash
"/mnt/c/Program Files/Microchip/MPLABX/v6.30/mplab_platform/mplab_ipe/ipecmd.exe" \
  -TPPK4 -P32MZ2048EFM144 -M \
  -F"C:\\Users\\<User>\\Documents\\GitHub\\daqifi-nyquist-firmware\\firmware\\daqifi.X\\dist\\default\\production\\daqifi.X.production.hex" -OL
```
Watch for "Program Succeeded". Flags: `-M` = program mode, `-OL` = use loaded memories only. Gotchas: `-P` takes the device **without** the `PIC` prefix (with it: exit 36 / "Unable to locate DFP"); `-F` needs a Windows-style path (`/mnt/c/...` fails silently); `-TS<serial>` selects a specific PICkit when several are attached; **every flash wipes NVM** (WiFi/calibration settings — restore via the scpi skill's `batch.sh` + the station-local `sta_setup.batch` recipe); after flashing, reattach to WSL (`usbipd attach --wsl --busid 2-4`); libscpi context is stale after flash — new SCPI patterns return `-113` until `SYST:REBoot`.

### Bench tool inventory (this dev station)
| Item | Identifier | Notes |
|------|-----------|-------|
| Primary PICkit 4 (this board) | `BUR184882598` | Pass `-TSBUR184882598` to ipecmd when multiple PICkits are attached |
| Secondary PICkit 4 | `BUR202272588` | On other board(s) — ignore unless re-targeting |
| MCU device target | `PIC32MZ2048EFM144` | Pass to ipecmd as `-P32MZ2048EFM144` (no `PIC` prefix — see ipecmd gotchas above) |
| Serial port (USB CDC) | Windows: `COM3` / `COM9` (stable per board) ; WSL: `/dev/ttyACMn` (**attach-order dependent — NOT stable**) | usbipd busid `2-4` (primary) / `2-3` (secondary); reattach via `powershell.exe -Command "usbipd attach --wsl --busid <BUSID>"` after each reboot/flash. **DO NOT assume `/dev/ttyACM0` maps to any particular board — verify by serial number (see below).** Stable identifiers across reboots are the Windows COM number and the per-unit serial; `/dev/ttyACMn` is assigned by Linux in the order usbipd attaches devices. |
| Bench primary device serial | `7E2898F46200E8A7` | Programmed by PICkit `BUR184882598`. Windows: **COM3**, busid **2-4**. Verify by `*IDN?` before issuing SCPI — never hardcode `/dev/ttyACM0`. |
| Bench secondary device serial | `7E28A4206200EAD1` | Programmed by PICkit `BUR202272588`. Windows: **COM9**, busid **2-3**. Verify by `*IDN?` before issuing SCPI — never hardcode `/dev/ttyACM1`. **⚠️ Shared with other agents — may not always be available; if `usbipd list` shows it as "Not shared" or it's attached elsewhere, do not commandeer it.  Fall back to the primary alone.** |
| Bench WiFi AP | SSID `Tesla` | Credentials in `~/.daqifi.env` (chmod 600) — never commit |
| Bench PC iperf2 | `C:\Users\User\Downloads\iperf-2.2.1-win64.exe` | Run `-s -p 5002 -i 1`; redirect stdout to `C:\temp\iperf2.log` for log-side correlation |

#### ⚠️ Device verification protocol — ALWAYS run before SCPI tests

**`/dev/ttyACMn` assignment is volatile** — Linux numbers devices in usbipd attach order, not hardware identity, so the same board can move between `ACM0`/`ACM1` across reattaches. The **stable** identifiers are the Windows COM number and the firmware serial (third field of `*IDN?`, format `DAQiFi,Nq1,<serial>,01-02`). Never trust `/dev/ttyACMn` alone (this caused a 30-minute false bisect on 2026-05-06 — every firmware "looked the same" because the script was reading an unflashed second board).

Before any SCPI work:

1. Map busid → COM on the Windows side, and list WSL ports:
   ```bash
   powershell.exe -Command "usbipd list" | grep "04d8:f794"   # 2-4=COM3 primary, 2-3=COM9 secondary
   ls -la /dev/ttyACM*
   ```
2. Query each port's serial and match it to the inventory table above:
   ```bash
   for dev in /dev/ttyACM*; do
     echo -n "$dev => "
     (echo -e "*IDN?\r"; sleep 0.5) | timeout --foreground 2s picocom -b 115200 -q -x 1000 "$dev" \
       | tr -d '\r' | grep -m1 '^DAQiFi,' || echo "<no response>"
   done
   ```
3. Store the result in a variable (`DEV_PRIMARY=/dev/ttyACMn`) for the session instead of hardcoding. Later examples in this file use the literal `/dev/ttyACM0` as a stand-in for "the primary device".
4. Wrong board selected? Re-target — don't detach boards that aren't yours (other workflows may be using them).

### Bootloader Entry
- Hold the user button for ~20 seconds until board resets
- Release button when LEDs light solid
- Hold button again until white LED blinks to enter bootloader mode

### Packaging Release Artifacts

The version string comes from `FIRMWARE_REVISION` in `firmware/src/version.h`.

#### 1. Build the production hex **bootloader-linked** (required for customer devices)

The release hex MUST be built with the **bootloader linker script**
`old_hv2_bootld.ld` (MPLAB X Project Properties → include `old_hv2_bootld.ld`,
exclude `p32MZ2048EFM144.ld`), **not** the default standalone build. The
bootloader-linked layout places the application at `0x9D000480` (leaving
`0x9D000000`–`0x9D000480` for the bootloader); the standalone build links the
app at `0x9D000000` and will **clobber the bootloader** if flashed on a customer
device. The standalone hex is for direct PICkit bench flashing only — never ship it.

Verify the candidate hex before publishing: lowest physical address `0x1D000000`,
exactly **408 bytes** in `[0x1D000000, 0x1D000480)` (the `.reset` vector), and the
bulk of code from `0x1D000480`. This matches every shipped release (v3.4.4 / v3.4.6b1 /
v3.5.0 are all identical in shape — only the top address grows with code size). The
`kseg0_program_mem` origin in the `.map` must read `0x9d000480`.

#### 2. Attach a raw **`.hex`** asset — REQUIRED for the Windows in-app updater

The desktop app's in-app firmware update (`daqifi-core`
`Firmware/GitHubFirmwareDownloadService.cs`) queries
`api.github.com/repos/daqifi/daqifi-nyquist-firmware/releases`, takes the newest
**non-draft, non-prerelease** release, and selects the main-firmware asset by
**`name.EndsWith(".hex")`**. A release with **no `.hex` asset is skipped entirely** —
the app keeps offering the previous release. The `.zip` is **ignored** by the main-
firmware updater (zip is only consumed for the *WiFi* firmware path,
`wifi-firmware-<tag>.zip`). The manual flash dialog likewise filters `*.hex`.

So every full release that should be flashable in-app MUST attach a raw `.hex`
(any name ending `.hex`; the proven shape is v3.4.4's `DAQiFi_Nyquist.hex`):

```bash
cp firmware/daqifi.X/dist/default/production/daqifi.X.production.hex \
   "daqifi-nyquist-firmware-<VERSION>.hex"
```

#### 3. Also zip it (archival / manual distribution)

```bash
zip -j "daqifi-nyquist-firmware-<VERSION>.zip" firmware/daqifi.X/dist/default/production/daqifi.X.production.hex
```
Example: `daqifi-nyquist-firmware-3.5.0.zip`.

#### 4. Publish with both assets

```bash
gh release create v<VERSION> --repo daqifi/daqifi-nyquist-firmware --target main \
  --title "..." --notes-file RELEASE_NOTES_v<VERSION>.md --latest \
  "daqifi-nyquist-firmware-<VERSION>.hex" "daqifi-nyquist-firmware-<VERSION>.zip"
```
The updater deterministically picks the `.hex`; the `.zip` stays for archival/manual use.

**Verify before declaring the release done:** `gh api repos/daqifi/daqifi-nyquist-firmware/releases`
→ confirm the new tag is the newest **non-draft, non-prerelease** release that **has a `.hex` asset**.
Otherwise the in-app updater will silently keep offering the prior version.

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

2. **USB CDC** - Virtual COM port communication
   - Implementation: `services/UsbCdc/UsbCdc.c`
   - Handles command processing and data streaming

3. **WiFi** - Network communication
   - Manager: `services/wifi_services/wifi_manager.c`
   - TCP server for remote control
   - UDP announcements for device discovery

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

The PIC32MZ ADCHS peripheral has two classes of ADC channels with different read strategies. (This section reflects the post-#541 architecture; #292's "T1 reads in the EOS task" topology and the older "CH3 batch ISR" topology described in earlier revisions are both gone. Full hardware-semantics record with FRM/datasheet citations: `docs/ADC_HW_SEMANTICS.md`.)

**Type 1 — Dedicated modules (simultaneous conversion):**
- NQ1 channels: ch4 (MODULE4), ch8 (MODULE0), ch10 (MODULE1), ch12 (MODULE2), ch14 (MODULE3)
- Each channel has its own SAR ADC module — all convert simultaneously on the streaming-timer hardware trigger (TMR5 match, #282); conversion completes ~1.3 µs later
- **Streaming reads (#541 D-A)**: the deferred streaming task reads `ADCDATAx` directly, gated on the per-input `ARDY` flag (sets at conversion end, clears on read — fresh-per-tick by construction, no interrupt dependency). A not-ready miss leaves that channel's validMask bit 0 for the tick and bumps the `T1ArdyMisses` stat (expected ~0; <0.1% under pool saturation)
- **Idle reads**: `MC12bADC_EosInterruptTask` still reads T1 into `BOARDDATA_AIN_LATEST` between sessions so `MEAS:VOLT:DC?` works; while streaming the deferred task refreshes LATEST itself
- The per-channel `ADC_DATA0-4` ISR handlers are stubs (#292 removed the batch ISR; ~5 µs entry/exit per tick was the T1 bottleneck)

**Type 2 — Shared MODULE7 (sequential mux scan):**
- NQ1 channels: ch0, ch1, ch2, ch3, ch5, ch6, ch7, ch9, ch11, ch13, ch15 (+8 internal monitoring channels; the dead temp sensor AN44 is never scanned — erratum 18)
- **Dynamic scan list (#541 D-B)**: `ADCCSS1/2` is rebuilt at every stream start = enabled T2 user channels ∪ (monitoring channels if OBDiag=1), applied via the FRM-documented `TRGSUSP`→`UPDRDY` online-update sequence; the idle list (all public T2 + monitoring) is restored at stream stop. Mid-stream `CONF:ADC:CHANnel` / `CONF:ADC:OBDiag` / `CONF:ADC:SAMC` are **rejected** so the session list can't go stale (#116)
- **Results**: per-channel data-ready ISRs (priority 1) read T2 user values into LATEST; the end-of-scan (EOS) interrupt fires once per completed full scan and wakes `MC12bADC_EosInterruptTask` (priority 9), which during streaming reads **monitoring channels only**
- **Scan-rate bound (#541 D-C)**: three distinct hardware limits gate the scan, all folded into the cap. (1) **Scan-busy bound**: retriggering the scan while in progress is documented-undefined (FRM §22.3.2) — this was #539, where EOS stopped firing above ~4.6 kHz and scanned data froze; the cap bounds the tick period to `1.1 × (N_active × (SAMC+2+14) × TAD7 + 6 µs)` (n=19 silicon anchor: timer→EOS 216 µs + 4,500 Hz clean; TAD7 = 100 ns, all terms read live). (2) **EOS-rate ceiling** (`ADC_EOS_RATE_MAX_HZ` = 10,400): independent of scan length, driving the end-of-scan interrupt/task machinery sustained above ~11.5–12 kHz **kills the USB peripheral outright** (enumerated-but-CDC-dead or off the bus; hardware reset required). Silicon anchors: an n=1 scan — in-spec for retrigger, T_busy 17 µs ≪ 83 µs period — wedges at 12,000 Hz on the plain *admitted* path, clean 10,000 × 60 s; n=7 clean at 11,500, wedge at 11,750; 10,400 soak-proven 120 s+. Pre-#541 firmware could never hit this: the static 19-input scan put EOS to sleep (#539) above 4.6 kHz, keeping its rate out of the fatal zone — dynamic CSS exposed it. (3) **Aggregate ADC-event-rate ceiling** (`ADC_EVENT_RATE_MAX_PER_S` = 60,000): each enabled T2 *user* channel fires a per-conversion data-ready ISR on top of the per-scan EOS; the combined event rate `f × (nUserT2 + 1)` is USB-fatal around ~66–72k events/s (11×T2 OBDiag=0 wedged at 6,000 Hz *admitted* = 72k/s, clean 5,500 × 60 s; 60k is 120 s-proven). Monitoring channels have no data-ready ISRs and don't count. Mechanism root-cause for both fatal limits: #545. Effective examples: 1×T2 OBDiag=0 → 10,400 (EOS-bound; was transport 15,000); 1×T1 OBDiag=1 → 10,400; 11×T2 OBDiag=0 → 5,000 (event-bound; was tick-budget 6,470); 16ch OBDiag=1 → ~4.2 kHz (busy-bound). OBDiag=1 visibly lowers `CONF:CAP` — honest physics, monitoring rides the same scan. T1-only OBDiag=0 arms no scan and is bound by none of the three (verified clean to 18 kHz NOCAP)

**Flow per streaming timer tick:**
1. TMR5 match hardware-triggers Type 1 conversions + the MODULE7 scan (when armed); the timer ISR (`Streaming_Defer_Interrupt`) notifies the deferred task
2. Deferred task builds the sample: T1 via ARDY-gated direct `ADCDATAx` reads; T2 from `BOARDDATA_AIN_LATEST` (written by the T2 data-ready ISRs)
3. EOS fires at full-scan completion → `MC12bADC_EosInterruptTask` reads monitoring channels (OBDiag=1 sessions and idle)
4. Software-trigger path (`MC12b_TriggerConversion`) remains for idle polling and non-HW-trigger modes

**Key files:**
- `services/streaming.c` — deferred task T1 direct read, session CSS rebuild, scan-bound cap term
- `HAL/ADC/MC12bADC.c` — `MC12b_ComputeScanList` / `MC12b_ApplyScanList` / `MC12b_ScanMaxFreq` / `MC12b_DrainType1Results`, `MC12b_TriggerConversion`, hardware-trigger config
- `HAL/ADC.c` — `MC12bADC_EosInterruptTask` (monitoring + idle T1 reads)
- `config/default/interrupts.c` — T2 per-channel data-ready handlers, `ADC_EOS_Handler`

**Characterization results (O3, fullscale test pattern, NoCap benchmark mode):**

Current table = **Session 24 (2026-05-28 overnight + 2× targeted retry, 400 s endurance).** Methodology change vs Session 22: where Session 22 used a fresh 10 s ceiling sweep + 60 s endurance soak, Session 24 uses 400 s endurance soaks with iterative haircut from prior-night ceilings (12.5 % per pass, repeated until zero drops). Result: **substantially more conservative** numbers than Session 22 in many cells — these are *verified-safe steady-state rates* the device sustains for 400 s+ without losing a single byte. Fresh 10 s sweeps will still find higher rates that hold short-term; treat Session 22 as "burst ceiling" and Session 24 as "soak ceiling." Source CSVs: `daqifi-python-test-suite/benchmarks/overnight_20260528_0642.csv` + `_0827_boardE8A7.csv` + `_1604_retry3.csv`. Test-suite SHA: `22302ba` on `feat/full-stats-capture`.

> **This is descriptive endurance characterization, not the firmware cap.** These soak ceilings are an empirical record of what the device sustains across many configs (incl. OBDiag variants, OBDiag here = on-board diagnostics monitoring); the rate the firmware actually *enforces* is fitted separately — see **"Streaming Frequency Capping"** below, whose **"Fit basis (normative — the zero-loss sweep subset…)"** table is the canonical 1/5/10/16-ch subset the `Streaming_TransportMaxFreq` coefficients derive from. The two use different methods (soak-with-haircut vs zero-loss sweep escalator) and so report different numbers by design. Authoritative dataset for both: `daqifi-python-test-suite/benchmarks/`.

**USB** (400 s endurance soak, KB/s from `pc_kbps`):

| Config | PB Hz | PB KB/s | CSV Hz | CSV KB/s |
|--------|----------:|--------:|----------:|--------:|
| 1×T1 OBDiag=OFF        | 16,500 | 182 | 17,000 | 256  |
| 1×T1 OBDiag=ON         | 14,500 | 159 | 14,000 | 222  |
| 1×T2                   | 14,500 | 159 | 16,000 | 250  |
| 3×T1                   | 15,000 | 224 | 13,000 | 612  |
| 3×T2                   | 15,000 | 224 | 13,000 | 613  |
| 5×T1 OBDiag=OFF        | 16,000 | 303 | 13,000 | 968  |
| 5×T1 OBDiag=ON         | 14,000 | 265 | 11,000 | 869  |
| 5×T2                   | 14,000 | 266 | 11,000 | 868  |
| 8×T2                   | 12,000 | 300 |  9,000 | 1,140 |
| 11×T2                  | 11,000 | 341 |  8,000 | 1,386 |
| 5T1+3T2 (8ch)          | 12,000 | 299 |  9,000 | 1,139 |
| 5T1+5T2 (10ch)         | 11,000 | 315 |  8,000 | 1,266 |
| 5T1+11T2 OBDiag=ON (16ch)  |  9,000 | 368 |  6,000 | 1,518 |
| 5T1+11T2 OBDiag=OFF (16ch) | 11,000 | 449 |  7,000 | 1,798 |

Best wire rate observed: **USB CSV 5T1+11T2 OBD=OFF @ 7 kHz → 1,798 KB/s**.

**SD** (400 s endurance soak, interface=2, post-format clean card):

| Config | PB Hz | CSV Hz |
|--------|----------:|----------:|
| 1×T1 OBDiag=OFF        | 10,000 |  9,000 |
| 1×T1 OBDiag=ON         |  9,000 |  8,000 |
| 1×T2                   |  9,000 |  8,000 |
| 3×T1                   |  8,000 |  5,000 |
| 3×T2                   |  8,000 |  5,000 |
| 5×T1 OBDiag=OFF        |  8,000 |  4,000 |
| 5×T1 OBDiag=ON         |  7,000 |  3,000 |
| 5×T2                   |  7,000 |  3,500 |
| 8×T2                   |  6,000 |  2,500 |
| 11×T2                  |  6,000 |  1,500 |
| 5T1+3T2 (8ch)          |  6,000 |  2,000 |
| 5T1+5T2 (10ch)         |  6,000 |  1,500 |
| 5T1+11T2 OBDiag=ON (16ch)  |  5,000 |  1,000 |
| 5T1+11T2 OBDiag=OFF (16ch) |  5,000 |  1,500 |

**Why Session 24's USB cells read lower than Session 22's:** methodology, not regression — 400 s endurance + iterative haircut converges several kHz below a 10 s burst ceiling. Earlier session-by-session deltas (Sessions 18–23: jitter elimination via the #335 idle-gate, T1/T2 parity #313, SD PB recovery #312/#314, per-PR throughput-safety checks for #354/#356) are recorded in `daqifi-python-test-suite` `benchmarks/CHANGELOG.md` and the project memory files — consult those for history rather than this document.

> **⚠️ Caveat post-#541 (v3.6.0):** the T1 rows in these tables (and any pre-#541 measurement) were captured while T1 *values* froze above ~4.6 kHz — throughput numbers stand, but the streams carried stale data. The enforced caps were re-validated at-cap on the new read path (2026-06-12: USB 28/28, WiFi 18/18 cells, 120 s soaks, zero loss).

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
| `QueueDroppedSamples` | uint32 | Samples lost due to pool exhaustion or full sample queue (pool defaults 1100, re-partitioned per session) |
| `UsbDroppedBytes` | uint32 | Data lost due to USB circular buffer full (16KB) |
| `WifiDroppedBytes` | uint32 | Data lost due to WiFi circular buffer full (14KB) |
| `SdDroppedBytes` | uint32 | Data lost due to SD write timeout/partial (8-64KB buf, 3 retries) |
| `EncoderFailures` | uint32 | Encoding attempts that returned 0 bytes with data available |
| `TimerISRCalls` | uint64 | Actual streaming timer ISR entry count this session (#265). Invariant: `TimerISRCalls == TotalSamplesStreamed + QueueDroppedSamples`. |
| `SampleLossPercent` | uint32 | `QueueDroppedSamples / (Total + Dropped) * 100` |
| `ByteLossPercent` | uint32 | `(USB + WiFi + SD dropped) / TotalBytesStreamed * 100` |
| `WindowLossPercent` | uint32 | Sliding-window sample loss % (0-100), updated every N samples |

**Distinguishing failure modes** with the ISR counter (#265):
- `TimerISRCalls < freq × duration` → timer is rate-limited (PIC32MZ ~90 kHz hardware ceiling)
- `TimerISRCalls == TotalSamples + QueueDropped` → every ISR is accounted for; nothing lost between ISR and deferred task (should always hold)
- `QueueDroppedSamples > 0` → sample pool exhausted (encoder/output too slow for the rate the timer is firing)
- `UsbDroppedBytes / SdDroppedBytes > 0` → encoder is fine but transport can't keep up

**Thread safety:** `TotalSamplesStreamed`, `TotalBytesStreamed`, and `TimerISRCalls` are 64-bit counters (safe for million-year sessions). The first two are protected by `taskENTER_CRITICAL`/`taskEXIT_CRITICAL` on each increment and during snapshot reads. Drop counters remain 32-bit (atomic on PIC32MZ). `TimerISRCalls` lives in a separate `static volatile uint64_t gTimerISRCalls` global, incremented in true ISR context (TIMER_5 — the 32-bit TMR4/5 streaming-timer pair's vector, priority 3, ≤ max-syscall 4) by a single writer (no critical section needed because same-source can't preempt itself); the snapshot read uses `taskENTER_CRITICAL` which raises the syscall priority above the kernel-managed ISR threshold and blocks the timer, making the non-atomic 64-bit read coherent.

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

The firmware computes a maximum safe streaming frequency as the `min()` of **ADC/ISR**, **per-interface/per-format TRANSPORT**, and (when a shared scan is armed) the three **scan bounds** documented in the ADC Architecture section above (#541: scan-busy, EOS-rate ≤ 10,400 Hz, aggregate-event ≤ 60,000 events/s ÷ (nUserT2+1)). Since #524 the cap is a **HARD limit**: `SYST:STR:START <freq>` above the cap is **rejected** with SCPI `-222` plus a `LOG_E` detail line stating the achievable max — the rate is never silently changed. Clients pre-validate against `current_max_rate_hz` (`CONF:CAP:JSON?` — equals this cap) or handle the error. Mid-stream `CONF:ADC:CHANnel` / `OBDiag` / `SAMC` are likewise **rejected** (#116/#541). `SYST:STR:BENCHmark 1/2` bypasses the cap — bench use only; the scan bounds protect against documented-undefined / USB-fatal ADC operation (#544).

**Effective limit:** `min(ISR_MAX 16000, 55000/type1Count, 110000/(6+totalEnabled), TransportMax(interface, encoding, n), ScanBounds(scan list, SAMC))`

**Transport term** (`Streaming_TransportMaxFreq`, streaming.h — #524): single-channel special-cased + `A/(B+n)` for n≥2 ("F3"), fitted ≤ the measured zero-loss ceilings (tightness 86–100%). JSON uses the CSV coefficients ×0.5 (uncharacterized — conservative; emits 2–3× CSV bytes/sample; measuring it is a tracked #524 follow-up).

| interface | single (n=1) PB / CSV | A/(B+n) PB | A/(B+n) CSV |
|-----------|----:|----:|----:|
| USB    | 15000 / 15000 | 180000/(10+n) | 34000/(1+n) |
| WiFi   | 5175 / 4675   | 139000/(30+n) | min(20000/(2+n), 3050) |
| SD     | 9000 / 7500   | 150000/(15+n) | 42000/(12+n) |
| USB+SD | 8000 / 8000   | 66000/(6+n)   | 15000/(0+n) |

**Fit basis (normative — the zero-loss sweep subset the F3 coefficients derive from, Hz):**

| interface/fmt | 1ch | 5ch | 10ch | 16ch |
|---|--:|--:|--:|--:|
| USB PB | 15000 | 12000 | 9000 | 7000 |
| USB CSV | 15000 | 6000 | 6000 | 2000 |
| WiFi PB¹ | 5175 | 4000 | ~3600 | — |
| WiFi CSV¹ | 4675 | 2857 | 2000 | 1250 |
| SD PB | 9000 | 7500 | 6000 | 5000 |
| SD CSV | 7500 | 2500 | 3000 | 1500 |
| USB+SD PB | 8000 | 6000 | 5250 | 3000 |
| USB+SD CSV | 8000 | 3000 | 1500 | 1000 |

¹ WiFi basis = the 2026-06-11 honest-scan walk-down soaks (#540), **not** the original #524 sweep — every earlier WiFi basis was inflated ~1.5× by the #537 scan-skip bug (device did less work per tick) and, before #371, by silent uncounted drops. WiFi caps are worst-night-observed by policy (link varies ~1.5× night-to-night); AIMD (#523, parked) is the long-term answer. All WiFi/USB caps re-validated 120 s at-cap on the v3.6.0 read path (46/46 cells, zero loss), and ceiling probes (2026-06-12) measured the remaining transport-fit headroom: USB T1-only +13–29%, WiFi +25–35% (good-night), SD +6–20% — see `benchmarks/541_adc_read_path/SILICON_ANCHORS.md`.

**ADC cost (synthetic PAT3 fullscale vs real PAT0), representative:** USB PB 1ch 25000→15000 (−40%), 16ch 10000→7000 (−30%); SD PB 1ch 12000→9000 (−25%). Below the transport ceiling the ADC is ~free; above it, ADC ISR/EOS load (pri-9 EOS-task wakeups preempting the pri-6 encoder) competes with the encoder.

**History in one line each:** #520 introduced the WiFi-only budget term; #524 generalized it to all interfaces and closed a format-blind hole (high-channel CSV capped above its true ceiling = silent loss); #107 removed the legacy 1 kHz T2 mux throttle (scan never overruns to ≥40 kHz — though #541 later bounded scan *interrupt* rates, which is a different limit); #540 derated WiFi to the honest-scan soak basis; #541/#543 added the three scan bounds.

**Full data + traceability:** `daqifi-python-test-suite` `benchmarks/524_streaming_characterization/` (F3 fit + 3-run matrix), `benchmarks/107_t2_scan_characterization/` (18-pass T2 scan matrix), `benchmarks/atcap_*.csv` (soak validations), `benchmarks/541_adc_read_path/` (scan-bound silicon anchors).

**Implementation:** `firmware/src/services/streaming.h` (`Streaming_ComputeMaxFreq`, `Streaming_TransportMaxFreq`), `firmware/src/services/streaming.c` (`Streaming_ComputeMaxFreqForConfig*`), `firmware/src/HAL/ADC/MC12bADC.c` (`MC12b_ScanMaxFreq`), `firmware/src/services/SCPI/SCPIInterface.c` / `firmware/src/services/SCPI/SCPIADC.c` (enforcement).

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

**1. Benchmark Mode** (`SYST:STR:BENCHmark`): Three levels, each isolating a different stage of the pipeline so you can locate the actual bottleneck.

```bash
SYSTem:STReam:BENCHmark <0|1|2>   # 0=OFF, 1=NOCAP, 2=PIPELINE
SYSTem:STReam:BENCHmark?           # Query current mode
```

| Level | Name | Frequency cap | ADC in loop | Encoder runs | Use when |
|---|---|---|---|---|---|
| **0** | OFF (normal) | Active (per-channel safe rate) | Yes (real conversions) | Yes | Production / data integrity testing |
| **1** | NOCAP | Bypassed (any rate up to 100 kHz) | Yes (real conversions) | Yes | Measuring **end-to-end** throughput including ADC overhead |
| **2** | PIPELINE | Bypassed | **NO — ADC entirely skipped** (bypasses `BoardData_Get(BOARDDATA_AIN_LATEST)`; encoder fed synthetic values directly) | Yes (synthetic data only) | Measuring **WiFi/USB/SD pipeline ceiling** with ADC overhead removed |

Use **NOCAP** for "what does the system actually deliver?" (the level documented ceilings are compared against) and **PIPELINE** to isolate encoder+transport cost from ADC cost (if PIPELINE ≫ NOCAP at the same Hz, the ADC path is contributing). PIPELINE requires a non-zero `SYST:STR:TEST:PATtern` (rejected otherwise — there's no ADC data to encode). A/B the two at the same rate with `SYST:STR:STATS:CLE` between runs; restore `BENCH 0` afterward.

> **⚠️ #544 hazard (post-#541):** NOCAP/PIPELINE bypass the scan-rate bounds too. Driving an armed scan past them (EOS rate ≳ 11.5 kHz, or aggregate events ≳ 66 k/s) is documented-undefined and observed **USB-fatal** (hardware reset required). Bench use only; respect the scan bounds manually when sweeping scanned configs.

**Empirical NOCAP-vs-PIPELINE curve (1×T1 PB on Tesla AP, fullscale, 6 s):**

| Rate | PIPELINE wire | NOCAP wire | ADC cost |
|----:|--------------:|-----------:|---------:|
| 1 kHz | 50 KB/s | 50 KB/s | 0 % |
| 2 kHz | 95 KB/s | 95 KB/s | 0 % |
| 3 kHz | 134 KB/s | 135 KB/s | 0 % |
| 5 kHz | 211 KB/s | 211 KB/s | 0 % |
| **8 kHz** | **194 KB/s** | **70 KB/s** | **–64 %** |
| **12 kHz** | **33 KB/s** | **1 KB/s** + `qd=1793` | **–97 %** |

**Below the Tesla wire ceiling (~5 kHz × 1 ch ≈ 210–230 KB/s), ADC is invisible** — encoder + WiFi keep up without contention. **Above wire ceiling, ADC pipeline (ISRs, EOS task, BoardData mutex) takes enough CPU that the encoder/output stalls.** NOCAP saturates earlier than PIPELINE because of this. The "ADC cost" reported by simple side-by-side numbers depends entirely on whether you tested at saturation or below — if anyone reports ADC as "free" without specifying rate, treat it skeptically.

**Throughput-claim discipline:** when reporting wire-rate measurements, always state the benchmark level and the test pattern. A "5 kHz / 230 KB/s" number is meaningless without "(NOCAP)" or "(PIPELINE)" — they measure different things. PIPELINE numbers represent the **upper bound** for streaming work that doesn't read the ADC; NOCAP numbers represent the **realistic** ceiling for actual data acquisition.

**Frequency-cap interaction:** in NOCAP and PIPELINE modes the freq cap is bypassed and `SYST:StartStreamData` accepts up to 100 kHz (the timer ISR itself tops out ~90 kHz — see the `TimerISRCalls` failure modes above). Everything downstream saturates far earlier — expect `QueueDroppedSamples > 0` (encoder/queue saturation) at very high rates.

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

For current measured ceilings, use the **fit-basis table** in "Streaming Frequency Capping" above (the normative zero-loss subset the enforced caps derive from) and the Session-24 soak tables in "ADC Architecture" — older synthetic-pattern snapshots have been retired from this file.

**WiFi characterization — lessons that survive (the Session-23 table itself was retracted):**

- Pre-#371 WiFi numbers (Sessions 21–23) are **inflated and unusable**: `WifiDroppedBytes` read 0 while the streaming task silently dropped up to 86 % of encoded bytes (`hasWifi = (wifiSize >= 128)` skipped the whole WriteBuffer call under back-pressure). Never trust a pre-#371 WiFi figure; the current WiFi caps come from the 2026-06-11 honest-scan walk-down basis (#540) plus the 2026-06-12 T1 re-validation.
- The firmware path through WINC1500 sustains roughly 200–340 KB/s on the bench AP — a fraction of the module's 5–10 Mbps spec. **SPI4 clock is not the bottleneck** (measured 16.67 MHz, bus idle 92.9 % during a sustained stream — Saleae); the gap is host-side send pipelining (#361/#362/#363). SPI4 baud is shared with the SD card — never change it as a "WiFi fix" without SD validation.
- WiFi link capacity varies ~1.5× night-to-night; static caps are worst-night-observed by policy, runtime AIMD (#523, parked) is the way to harvest good-link headroom.

Full benchmark history (CSVs + CHANGELOG.md) is version-controlled in `daqifi-python-test-suite/benchmarks/` — that repo is the authoritative record; update it, not this file, when new results are collected.

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
   "/mnt/c/Program Files/Microchip/MPLABX/v6.30/gnuBins/GnuWin32/bin/make.exe" \
     -f nbproject/Makefile-Nq1.mk CONF=Nq1 build -j4

   # Build NQ3 firmware
   "/mnt/c/Program Files/Microchip/MPLABX/v6.30/gnuBins/GnuWin32/bin/make.exe" \
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
**Errata**: [DS80000663](https://ww1.microchip.com/downloads/aemDocuments/documents/MCU32/ProductDocuments/Errata/PIC32MZ-Embedded-Connectivity-with-Floating-Point-Unit-Family-Silicon-Errata-DS80000663.pdf) (Rev R, silicon A1/A3/B2/B3)

#### Atomicity & Concurrency Rules

- **32-bit reads and writes are atomic** on the PIC32MZ bus. A simple `x = 0` or `return x` on a `uint32_t`/`uint16_t` does NOT need a critical section.
- **Read-modify-write** (`x |= bit`, `x &= ~bit`, `x++`, `x += n`) is NOT atomic — use `taskENTER_CRITICAL()`/`taskEXIT_CRITICAL()`.
- **64-bit operations** (`uint64_t` increment, struct copy) always need a critical section.
- **`volatile`** is needed when a variable is written by one task/ISR and read by another — it prevents the compiler from caching the value in a register. `volatile` alone does NOT make RMW atomic; you still need critical sections for `+=`, `|=`, etc.
- **Set-once-then-shared pointers** (`static T *gp` set in `Init()`, dereferenced from tasks and ISRs): qualifier forms differ — `volatile T *p` (data-volatile) is the strictly-correct fix when `p->field` is RMW'd or read across task↔ISR boundaries, but cascades through every API taking a derived `T *`; `T * volatile p` (pointer-volatile) only forces pointer reloads and is **not** a concurrency guarantee. Rules: (1) cross-context RMW'd data → data-volatile (or volatile on the specific field); (2) `volatile` never makes RMW atomic — critical sections still required; (3) **don't add qualifiers speculatively** — Microchip's guidance (DS90003269A) prescribes critical sections/mutexes, not volatile, for shared data. The empirical audit (`docs/SET_ONCE_POINTER_AUDIT.md`, 2026-05-10) found the qualifier observable in codegen but redundant in this codebase: every consumer path crosses opaque call boundaries (FreeRTOS/PLIB/BoardData) that already force reloads at -O3 with no LTO on XC32 v4.60; volatile/no-volatile A/B'd to identical failure baselines. Re-audit if LTO, `always_inline` in hot loops, or a compiler change lands. Methodology lesson from that audit: **never conclude load-bearing-ness from n=1 bench trials** — multi-trial both branches first. (Related history: the #354 ch15 regression that motivated the #421 volatile speculation was actually fixed by a hardware TRGSRC register configuration, `96e7c840` — not by qualifiers.)
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
- **Registered / pure-integer task lists**: see the FPU bullet in "Atomicity & Concurrency Rules" above.
- **ADC voltage conversion**: `ADC_ConvertToVoltage()` uses native `mul.d`, `div.d`, `add.d` instructions

#### FreeRTOS Configuration

- **Tick rate**: 1000 Hz (1ms tick)
- **Preemptive** with time-slicing (equal-priority tasks round-robin)
- **Max priorities**: 10 (0=lowest, 9=highest)
- **Heap**: heap_4, 75KB (`configTOTAL_HEAP_SIZE`)
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

**SCPI shared response buffer**: Any SCPI callback needing ≥256 B of scratch MUST use `SCPI_ResponseBuf_Take()` / `SCPI_ResponseBuf_Give()` rather than a stack local. The shared buffer is a single 2048-byte static in BSS guarded by a statically-allocated mutex (`configSUPPORT_STATIC_ALLOCATION=1`). Rationale: the TCP → microrl → libscpi path consumes ~800 words of WifiTask stack before the callback runs, so a large stack-local buffer inside a SCPI callback can overflow — issue #347 hit exactly this when WifiTask was 1024 words (it's 1500 now, peak 780; the headroom assumes callbacks stay off large stack locals). Current users: `SCPI_SysInfoGet`, `SCPI_SysInfoTextGet`, `SCPI_GetCommandHistory`, `SCPI_Help`, `SCPI_StorageSDBenchmark`.

**Scheduling implications**: Capture tasks at priority 9 preempt everything to guarantee deterministic sample timing. The encoder at priority 6 preempts WiFi/WINC/background (priority 2) and SD (priority 5), but stays below USB (7) so SCPI commands remain responsive during streaming. SD task at priority 5 sits above background transports but below encoder — prevents encoder from starving SD writes when USB+SD both active. Encoder's `Streaming_WriteWithRetry` uses `vTaskDelay(1)` (not `taskYIELD()`) in its retry loop so lower-priority SD actually gets CPU to drain circular buffer (#312). See `docs/PIPELINE_TIMING.md` for measurements (PR #308, Sessions 7-17).

#### Known Silicon Errata (DS80000663 Rev R, silicon revs A1/A3/B2/B3; verified against PDF pages 6-15)

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
| Task stacks (per Task Priority Map + idle/timer daemon) | ~37,500 | `xTaskCreate` (profiled) |
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
| `SAMPle:POOL` | 0 or 100 | 10000 (`MAX_AIN_SAMPLE_COUNT`) | 0 = maximize with remaining pool space. `CONF:CAP` JSON still advertises max 2000 — stale firmware-side constant, the setter accepts 10000. |

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
| `SamplePoolBytes` | Sample pool data memory (count × per-channel-count stride) |
| `SampleNextFreeBytes` | Free-list array memory (count × 2) |
| `SampleQueueBytes` | FreeRTOS queue overhead estimate |

**Auto-balance** (`Streaming_ComputeAutoBuffers()`): active interfaces get compile-time circular-buffer defaults (USB=64KB, WiFi=96KB — `STREAMING_WIFI_WIFI_ONLY`, #497 — SD=32KB), inactive get minimums; encoder is 16KB when SD is active (larger writes reduce SPI overhead), 8KB otherwise; the three coherent-pool DMA buffers (SD write, USB write, WiFi SPI staging) split the 124KB coherent pool by weighted shares (SD=5, USB=3, WiFi=2 — single-active gets everything); the sample pool gets whatever stream-pool space remains. When all `MemoryConfig` fields are zero (boot default), this runs automatically at each `StartStreamData`; setting any field non-zero disables auto mode for all fields. The `SYST:MEM:AUTO` response reports the computed `SdDma=`, `UsbDma=`, `WifiDma=`, `Encoder=` sizes.

**Auto-Balance Buffer Sizing by Active Interface:**

The `StreamingInterface` enum exposes four combinations: `USB`, `WiFi`, `SD`, and `UsbAndSd` — WiFi is always solo (SPI bus shared with SD; USB+WiFi was never wired into the enum). Values below are for 16-channel `AInSampleList_ElementSize` (74 B + 2 B free-list = 76 B/sample).

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

See user-memory `feedback_debugging_discipline.md` (station-local, not in this repo) for the originating incident and the personal version of this rule.

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
(in `daqifi-python-test-suite`)
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

Two-layer logging: compile-time ceilings control which log calls exist in the binary (all modules compile at DEBUG ceiling — every `LOG_E`/`LOG_I`/`LOG_D` is present), runtime levels control which execute (~2 CPU cycles per call when disabled). All modules boot at ERROR. Levels: `0`=NONE, `1`=ERROR, `2`=INFO, `3`=DEBUG. Module names: `POWER`, `WIFI`, `SD`, `USB`, `SCPI`, `ADC`, `DAC`, `STREAM`, `ENCODER`, `GENERAL`. Runtime-only — resets to ERROR on reboot.

**SCPI Commands:**
```bash
SYST:LOG?              # Retrieve all log messages (clears buffer after dump)
SYST:LOG:CLEAR         # Clear log buffer without reading
SYST:LOG:TEST          # Add test messages (for verification)
SYST:LOG:LEVel <module>,<level>   # e.g. SYST:LOG:LEV STREAM,2
SYST:LOG:LEVel? [module]          # With module: level only; no arg: all modules with level+ceiling
SYST:LOG:LEVel:ALL <level>        # Set all modules at once
```

**ISR-safe logging:** `LOG_E`/`LOG_I`/`LOG_D` are ISR-aware — they detect ISR context via FreeRTOS `uxInterruptNesting` and automatically route through a deferred queue (`xQueueSendFromISR` + drain task). No separate ISR macros needed. Caveat: format args (`%d`, `%u`, etc.) are ignored in ISR context to avoid `vsnprintf` on the ISR stack — use static strings in ISR handlers.

**One-shot suppression:** Two bitmask-based one-shot systems prevent log flooding from high-frequency errors:

| Macro | Bitmask | Reset When | Use Case |
|-------|---------|-----------|----------|
| `LOG_E_ONCE(bit, ...)` | `gLogOneShot` | `SYST:LOG?` / `SYST:LOG:CLEAR` | ISR context (8-entry deferred queue) |
| `LOG_E_SESSION(bit, ...)` | `gSessionOneShot` | `Streaming_ClearStats()` at stream start | Streaming engine per-sample errors |

Both use `volatile uint32_t` bitmasks (up to 32 call sites each). Bit indices defined in `LogOnceBit_t` and `LogSessionBit_t` enums in `Logger.h`. The `|=` is not critical-section-protected — worst case is one extra duplicate message per priority-crossing race. Also available: `LOG_I_ONCE`, `LOG_D_ONCE`, `LOG_I_SESSION`, `LOG_D_SESSION`.

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

**Circular buffer:** 64 messages × 128 bytes, mutex-protected; oldest message dropped when full.

**Compile-time ceiling override:** `#define LOG_LEVEL_WIFI LOG_LEVEL_NONE` (in Logger.h or project defines) strips a module's log calls from the binary entirely.

**Real-time UART logging (development only):** `#define ENABLE_ICSP_REALTIME_LOG 1` outputs logs via UART4 on ICSP pin 4 (RB0) at 921600 baud. Must be disabled before release.

**Implementation:** `firmware/src/Util/Logger.c`, `firmware/src/Util/Logger.h`

## Bench Testing & Device Access

Build/flash mechanics live in **Build Instructions** at the top of this file — they are not repeated here.

### USB access from WSL (usbipd)

```bash
powershell.exe -Command "usbipd list"                      # find busid (2-4 = COM3 primary)
powershell.exe -Command "usbipd attach --wsl --busid 2-4"  # first attach needs admin PowerShell; --force if USBPcap is installed
sleep 2 && ls -la /dev/ttyACM*                              # verify; lsusb shows 04d8:f794 "Nyquist"
```
Re-run the attach after every flash, PC sleep, or device reboot — then run the **device verification protocol** (above) before any SCPI. Useful Windows-side checks: `netsh wlan show networks` / `show interfaces` via `powershell.exe`.

### Talking SCPI to the device

Three sanctioned paths, in order of preference:

1. **Python test suite + python-core** — mandatory for anything streaming, multi-step, or measured. `NyquistDevice` handles PB framing and clean disconnect; for unwrapped commands use `device._comm.send_command(...)`. Clone both repos to `/tmp` and `pip install --break-system-packages -e ./daqifi-python-core`. Shell pipelines (`dd | strings | grep`, picocom pipes) are unreliable for binary PB, post-stream stats, multi-step sequences, and timing.
2. **`~/.claude/skills/scpi/`** — `scpi.sh` for one-shot queries; `batch.sh` for stateful sequences (per-command `SYST:ERR?` checking, `?expected` verify directives, `${VAR}` secrets from `~/.daqifi.env`).
3. **picocom — simple non-streaming queries ONLY:**
   ```bash
   (echo -e "*IDN?\r"; sleep 0.5) | picocom -b 115200 -q -x 1000 /dev/ttyACM0 | tail -5
   ```
   Never for streaming or multi-step tests: its `-x` timeout is idle-based (a stream never goes idle → hangs forever); killing it mid-stream corrupts WSL serial state (recovery: `pkill -9 picocom; fuser -k /dev/ttyACM0; rm -f /var/lock/LCK..ttyACM0`, else physical replug); rapid open/close toggles DTR/RTS and can crash the device's CDC (Windows Code 43 → reprogram). Never use `2>&1` with picocom. Allow long drain time between large-response commands (`LISt?`, `LOG?`, `SD:GET` — `sleep 5+`) or the next query captures stale data.

For ad-hoc multi-command bench scripts, write to **`/tmp/temp.sh`** (the filename is allowlisted in `.claude/settings.local.json` — the untracked per-machine Claude Code override; add the entry there if your station doesn't have it), `chmod +x`, `dos2unix` if needed.

**Device state basics:** power states `0`=STANDBY, `1`=POWERED_UP (full power — required for WiFi and the DAC's 10 V rail), `2`=POWERED_UP_EXT_DOWN (low-battery). Before tests: known state (`SYST:POW:STAT 0/1` cycle if needed), drain the error queue (`SYST:ERR?` until `0,"No error"`), `ABOR` any pending operation. Don't guess SCPI syntax — see the verification protocol above (e.g. it's `SSIDSTR?`, `NETTYPE?`, `ADDR?` — not `SSID:STR`/`MODE`/`IP`).

**WiFi config sequence:** `ENAbled 0` → set `NETType`/`SECurity`/`SSID`/`PASs` → `ENAbled 1` → `APPLy` → wait ~20 s → verify `ADDR?` → `SAVE` (persists to NVM — without it a reboot reverts; **every flash wipes NVM regardless**). The WiFi module itself takes ~2–3 s to initialize after power-up. AP-mode defaults: open AP `DAQiFi-XXXX` (MAC suffix auto-applied to the bare default name; user names preserved), hostname = SSID (used for DHCP Option 12 network discovery), needs POWERED_UP.

### USB CDC behavior every test must respect

- **Host must read fast.** ≥1 ms read cadence = zero loss; a 500 ms poller dropped 1 MB in the same 16ch@3kHz A/B that a 1 ms reader survived clean. The firmware pipeline is leak-free — host-side reading is almost always the culprit in "USB drop" reports. Use `FastReader` (background drain) for anything streaming.
- **Delivery is bursty by design** (ZLP boundaries + one DMA transfer in flight + circular→DMA copy): `in_waiting` can read 0 while data sits in the usbipd pipeline; don't tight-poll it; allow 3+ s settle between sessions in naive scripts. Any SCPI command mid-stream "flushes" a short packet.
- **Rate measurements:** ONLY via `StreamingMeasurement` (`test_harness.py`) — PC-controlled window opened at first data byte, closed before STOP; wall-clock around SCPI start/stop adds ~10 % error, and the older first-byte-time SPS calculation caused ~10–15 % run-to-run variance (fixed by blocking FastReader + sleep-duration denominator — variance now near zero). `is_csv=True` counts rows; WiFi/SD pass `wait_for_serial=False`. Reference points: USB CSV 16ch@3kHz = 795 KB/s, 0 drops.

### Long-running bench runs

- **Always `python3 -u`** (or `PYTHONUNBUFFERED=1`) for background runs — block-buffered stdout looks empty and is lost on kill. Never wrap a real run in a tight `timeout` — per-trial SCPI overhead alone is ~15–20 s, so runs outlast naive estimates; reserve `timeout` for genuinely-bounded one-shot probes and size it generously. Watch the PID or the per-row-fsynced CSV instead (the CSV is the reliable progress signal).
- **Sustained high-rate USB → run Windows-native** (`python.exe` against COM3, repo cloned under `C:\`): the cumulative ISR=-1/unreadable-STATS wedge under back-to-back high-rate trials is a WSL/usbipd artifact. WSL `/dev/ttyACMn` is fine for low-rate/control-plane SCPI. (Separately: WSL-launched python silently drops inbound UDP — use PowerShell-native tooling for UDP tests.)
- Don't echo test results to the console for their own sake — observe, then report a summary.

### Git Configuration
- Ignore line ending changes when reviewing diffs (Windows/Linux compatibility)
- ALWAYS test changes that affect device behavior on hardware before committing (docs/CI/lint-only changes are exempt)
- Use descriptive commit messages that explain the problem and solution

### Commit Message Format

**Conventional Commits**: `<type>(<scope>): <subject>` + body + footer. Types: `feat`, `fix`, `docs`, `style`, `refactor`, `perf`, `test`, `build`, `ci`, `chore`. Imperative present tense ("fix", not "fixed"); subject ≤ 72 chars; reference issues/PRs in the footer; `BREAKING CHANGE:` footer when applicable.

```
fix(power): enable BQ24297 OTG mode for battery operation

The device was powering off when USB disconnected due to insufficient
voltage for the 3.3V regulator. OTG boost mode provides 5V from battery.

Fixes #23
```

### How we test (policy)

All **durable regression tests** — firmware regression, MCP integration, client SDK validation, throughput characterization — live in **`daqifi-python-test-suite`**. The full policy is in [`docs/HOW_WE_TEST.md`](https://github.com/daqifi/daqifi-python-test-suite/blob/main/docs/HOW_WE_TEST.md) in that repo (private, see `docs/README.md` for access); the short version:

1. **Anything you'd run again** — regression checks, ceiling sweeps, endurance soaks, integration validation — lives in `daqifi-python-test-suite`. Firmware-internal unit tests (boot self-tests, cppcheck, `mdb`-driven device tests) stay in the firmware repo.
2. **Build tests from modular pieces** in `test_harness.py` (`StreamingMeasurement`, `ReliableSCPI`, `FastReader`, `build_result_row`, `format_sd_card`, `derive_targets_from_csv`, etc.). New utilities go in the harness, not your script. New scripts that re-implement existing primitives get flagged in review.
3. **A test run = a recipe.** Either YAML (`comprehensive_test.py` pattern) or Python script with CLI flags (`test_overnight_characterization.py` pattern). Both are valid.
4. **Every run logs its complete recipe + version triplet** to a sidecar `<script>_<timestamp>.meta.json`: firmware `*IDN?` + firmware version + test-suite git SHA (`test_harness.collect_run_metadata`). Two runs with the same triplet should produce comparable CSVs; that's the **repeatability key**.
5. **Every benchmark emits the canonical CSV shape** documented in [`docs/CSV_SCHEMA.md`](https://github.com/daqifi/daqifi-python-test-suite/blob/main/docs/CSV_SCHEMA.md). New `SYST:STR:STATS?` firmware fields flow through automatically — no script changes needed.

**Carve-out for ad-hoc bench exploration.** Quick interactive commands — `picocom` one-liners, single SCPI queries, a throwaway `/tmp/temp.sh` to validate a hunch before writing the real test — are fine and have their own conventions documented in the "Bench Testing & Device Access" section above. The policy here applies when the question is "will this break if I change X next month?" — that answer lives in a versioned script in `daqifi-python-test-suite`, not in your shell history.

When you're touching the firmware in a way that needs a regression check, your first move is to find or write a script in `daqifi-python-test-suite` that exercises it. Don't put **regression** bench-validation logic in the firmware repo (the carve-out above is for one-off exploration only, not for tests that should keep running). Don't reproduce harness primitives.

### DAQiFi Test & API Repositories

Companion repos at https://github.com/daqifi:

| Repo | What it is |
|---|---|
| **daqifi-python-core** | Python API (`NyquistDevice`, streaming, discovery; pyserial ≥3.5) |
| **daqifi-python-test-suite** | The firmware test suite — `test_harness.py` primitives, `comprehensive_test.py`, characterization/regression scripts, `benchmarks/` data |
| **daqifi-core** / **daqifi-desktop** | .NET library (SCPI/Protobuf/Serial+TCP) / Windows WPF app (in-app firmware updater) |
| daqifi-java-api · daqifi-node · daqifi-labview · daqifi_nyquist_arduino · daqifi-core-example-app | Other client bindings/examples |

**Key validation scripts** (clone both Python repos to `/tmp`, `pip install -e ./daqifi-python-core`):

| What to validate | Script | Duration |
|-----------------|--------|----------|
| Real-ADC value liveness + cap boundaries (#539/#541) | `test_541_adc_value_liveness.py` (needs the 2 V bench rig) | ~10 min |
| Streaming throughput ceilings | `test_overnight_characterization.py` (`--at-cap --walk-down` for soak validation) | hours |
| Quick ceiling check | `test_interface_ceilings.py` | ~30 min |
| Data loss counters | `test_silent_loss_observability.py` | ~3 min |
| Test pattern integrity | `verify_test_patterns.py --run-all` (`--download` = deterministic SD-card verification, immune to USB burstiness) | ~10 min |
| A/B branch comparison | `test_ab_comparison.py` | ~20 min |
| Full device validation | `comprehensive_test.py` | ~5 min |

**Critical rule:** any test that measures streaming rate MUST use `StreamingMeasurement` from `test_harness.py` (see USB CDC section above).

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

### Standing Rules

- PIC32 TRIS convention is 1=input and 0=output
- When implementing new code/features remember we have multiple configurations (NQ1/NQ3) — keep all the structs consistent as well as the handling functions, etc.
- Always verify SCPI command syntax — don't guess (see the verification protocol above).
- When writing about timing/cadence, use concrete units with a source reference (e.g. "called every ~100 ms from `Power_Tasks()`"), avoid bare "every tick" unless the RTOS tick is truly meant, and distinguish "function is called" from "function performs I2C/SPI/network work".
- Don't generate analysis docs or the like unless asked.
- All errors go through the error logging function and are never sent out in the stream. On error, the SCPI command returns the error through its own handling; the user calls `SYSTem:LOG?` to learn what the error was.
- SCPI data visibility principle: prevent users from operating with improper settings (return SCPI errors for config problems like reading disabled channels), but maximize visibility into device health. When data is stale (e.g., monitoring channels frozen during OBDiag=0 streaming), show last-known values with age indicators rather than hiding them. Error on config problems, inform on stale data.