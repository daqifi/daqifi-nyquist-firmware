
# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is the DAQiFi Nyquist firmware project - a multi-channel data acquisition device built on PIC32MZ microcontroller with FreeRTOS. The project consists of a USB bootloader and main application firmware.

## Where the rest of it went (read this before assuming something is missing)

`CLAUDE.md` is loaded into **every turn of every agent**. At 38k tokens it was
about **13% of all token spend** on this project — paid again on every turn, by
every fire, whether or not the task touched the material. On 2026-09-10 the
reference material moved into `docs/`. **Nothing was deleted or summarised**; the
text is unchanged, and each file says so at the top.

What stayed here is what a task needs *regardless of area*: how to build and
flash, which board is which, how to verify a SCPI command before using it, the
test policy, the debugging-evidence rules, and the standing rules.

| Working on… | Read |
|---|---|
| streaming, the ADC read paths, frequency caps, loss counters, benchmarks | [`docs/STREAMING_AND_ADC.md`](docs/STREAMING_AND_ADC.md) |
| concurrency/atomicity, the clock tree, task priorities, FPU, silicon errata | [`docs/MCU_REFERENCE.md`](docs/MCU_REFERENCE.md) |
| buffers, the sample pool, heap/RAM budgets, `SYSTem:MEMory:*` | [`docs/MEMORY_ARCHITECTURE.md`](docs/MEMORY_ARCHITECTURE.md) |
| SD logging, file splitting, SPI arbitration with WiFi | [`docs/SD_SUBSYSTEM.md`](docs/SD_SUBSYSTEM.md) |
| the `SYSTem:MEMory:*` claim-path gate, stream-control aliases | [`docs/SCPI_REFERENCE.md`](docs/SCPI_REFERENCE.md) |
| optimization overrides, -O3 source patches, vendored-library patches | [`docs/BUILD_AND_TOOLCHAIN.md`](docs/BUILD_AND_TOOLCHAIN.md) |
| the DAC7718 or the BQ24297 charger | [`docs/PERIPHERALS.md`](docs/PERIPHERALS.md) |
| the logging system | [`docs/LOGGING.md`](docs/LOGGING.md) |
| cutting a release | [`docs/RELEASE_PROCESS.md`](docs/RELEASE_PROCESS.md) |
| NQ1/NQ2/NQ3 differences, switching build configurations | [`docs/BOARD_VARIANTS.md`](docs/BOARD_VARIANTS.md) |

**If you change behaviour these files describe, update them there.** Re-adding
that material here re-imposes the cost on every agent on every turn, which is the
whole reason it moved.
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

**⚠️ Makefiles are gitignored** (`nbproject/Makefile-*.mk` are generated from `configurations.xml`), so a fresh clone has none — run the regen below (or open the project once in MPLAB X) before the first `make`. Likewise after checking out a commit that adds/removes source files: the stale on-disk makefiles fail with `No rule to make target '../src/.../<file>.c'`. Regenerate from Windows (the Linux-side `prjMakefilesGenerator` often fails with "Device pack missing"):
```bash
powershell.exe -Command 'cd "C:\Users\User\Documents\GitHub\daqifi-nyquist-firmware\firmware\daqifi.X"; & "C:\Program Files\Microchip\MPLABX\v6.30\mplab_platform\bin\prjMakefilesGenerator.bat" -v .'
```
(The repo path is this station's; the inner command also runs directly in Windows PowerShell. On a different checkout, substitute the path — or from WSL at the repo root, derive it with `cd "$(wslpath -w firmware/daqifi.X)"`.) This is critical when bisecting — every checkout needs fresh makefiles.

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

The current baseline is **empty** — `tools/lint/cppcheck-baseline.txt`
is a 0-line file, so any new finding fails the gate. (An earlier
revision of this file said "2 style findings, both in
`firmware/src/services/wifi_services/wifi_serial_bridge.c`"; those were
fixed and the baseline regenerated. Verified 2026-08-21 against
cppcheck 2.13.0, the CI version.) The suppression file documents the
DioProbe.c array-bounds false positive and the FreeRTOS portmacro
FPU-guard `#error` (chip-specific macro that cppcheck doesn't see).

### Programming with PICkit 4 (ipecmd)

Preferred wrapper on this dev station: `bash ~/.claude/skills/flash/flash.sh [--build]` (user-local helper, not repo-tracked) — builds (optional), flashes, reattaches usbipd, verifies a `/dev/ttyACM*` node appears (node presence only — board *identity* still needs the device verification protocol below). Without the wrapper, use the direct invocation, which works anywhere:
```bash
"/mnt/c/Program Files/Microchip/MPLABX/v6.30/mplab_platform/mplab_ipe/ipecmd.exe" \
  -TPPK4 -P32MZ2048EFM144 -M \
  -F"C:\\Users\\User\\Documents\\GitHub\\daqifi-nyquist-firmware\\firmware\\daqifi.X\\dist\\default\\production\\daqifi.X.production.hex" -OL
```
(Station path shown — works pasted into Windows PowerShell/CMD too. From WSL on another checkout, derive it: `-F"$(wslpath -w firmware/daqifi.X/dist/default/production/daqifi.X.production.hex)"`.)
Watch for "Program Succeeded". Flags: `-M` = program mode, `-OL` = use loaded memories only. Gotchas: `-P` takes the device **without** the `PIC` prefix (with it: exit 36 / "Unable to locate DFP"); `-F` needs a Windows-style path (`/mnt/c/...` fails silently); `-TS<serial>` selects a specific programmer when several are attached; **`-TP` selects the programmer FAMILY and does not fall back** — `-TPPK4` for a PICkit 4 (USB `PID_9012`), `-TPPK5` for a PICkit 5 (`PID_9036`); point it at the wrong family and ipecmd simply fails to find the tool, so `flash.sh` derives it from the programmer's PID and callers pass only `--ts`; **every flash wipes NVM** (WiFi/calibration settings — restore via the scpi skill's `batch.sh` + the station-local `sta_setup.batch` recipe); after flashing, reattach to WSL (`usbipd attach --wsl --busid 2-4`); libscpi context is stale after flash — new SCPI patterns return `-113` until `SYST:REBoot`.

### Bench tool inventory (this dev station)
| Item | Identifier | Notes |
|------|-----------|-------|
| Lane registry (authoritative) | `~/.claude/bench/devices.conf` | Box-local; one row per lane (COM, firmware serial, PICkit), enforced by the bench device-guard; lane identity = worktree name. Snapshot of 2026-09-08 below. |
| Primary PICkit 4 | `BUR184882598` | Lane **nq-a**; pairing PROVEN 2026-09-08 (`flash/pairing-proof.sh nq-a`), re-run after re-cabling. Pass `-TS<serial>` with more than one PICkit attached, or ipecmd silently programs nothing. |
| Secondary PICkit 4 | `BUR202272588` | Not attached since 2026-09-08. |
| Lane nq-b (added 2026-09-08) | Nyquist **COM8** serial `7E2873046200E891` + **PICkit 5** `020026703RYN079002` | Pairing PROVEN 2026-09-08; now on v3.8.0, crc32 `9CF57ADD`. **Its `*IDN?` serial read `0` until it was reflashed** — the serial is `DEVSN1:DEVSN0` silicon (`BoardConfig.c:37`), not NVM, so a `0` means *this firmware does not read DEVSN*, never that the board lacks identity. Reflash before concluding anything from a `0`. **Factory cal is identity, i.e. this board is UNCALIBRATED** — do not use it for accuracy work. |
| MCU device target | `PIC32MZ2048EFM144` | Pass to ipecmd as `-P32MZ2048EFM144` (no `PIC` prefix — see ipecmd gotchas above) |
| Serial port (USB CDC) | Windows **COM7** = primary (was COM3). No usbipd here, so no `/dev/ttyACMn`: use Windows `python.exe` or `bench serial` (asserts DTR). | The board has no USB iSerial: **verify by the `*IDN?` serial**, never by port number. |
| Bench primary device serial | `7E2898F46200E8A7` | Lane nq-a, COM7, PICkit `BUR184882598`; demo unit, its lane may flash it. |
| Bench secondary device serial | `7E28A4206200EAD1` | Not attached since 2026-09-08 (nor `7E28517F62010292`, `7E2837886201026A`); re-add as `nq-c` onward after a pairing proof (`nq-b` is taken, see the row above). |
| Bench WiFi AP | SSID `Tesla` | Credentials in `~/.daqifi.env` (chmod 600) — never commit |
| Bench PC iperf2 | `C:\Users\User\Downloads\iperf-2.2.1-win64.exe` | Run `-s -p 5002 -i 1`; redirect stdout to `C:\temp\iperf2.log` for log-side correlation |

#### ⚠️ Device verification protocol — ALWAYS run before SCPI tests

**Never identify a board by port number alone.** The stable identifier is the firmware serial (third field of `*IDN?`, `DAQiFi,Nq1,<serial>,01-02`); a port number is an enumeration artefact. Getting this wrong caused a 30-minute false bisect on 2026-05-06 — every firmware "looked the same" because the script was reading an unflashed second board.

**Which steps apply depends on whether your box has usbipd — check the inventory table above, it is per-box.**

**Boxes WITHOUT usbipd** (this station as of 2026-09-08: no bridge, no `/dev/ttyACMn`, boards on COM ports):

1. List ports and identify each natively — `bench ports`, then `bench serial COMn '*IDN?'` per port (it asserts DTR; the CDC does not answer without it).
2. Match each serial against the lane registry `~/.claude/bench/devices.conf`, and drive only the board your lane owns. `bench whoami` prints what you own.
3. A serial of `0` means *this firmware does not read DEVSN*, not that the board has no identity — reflash before concluding anything (see the nq-b row above).

**Boxes WITH usbipd** (`/dev/ttyACMn` exists; numbering follows attach order, so the same board moves between `ACM0`/`ACM1`):

1. Map busid → COM and list WSL ports:
   ```bash
   powershell.exe -Command "usbipd list" | grep "04d8:f794"
   ls -la /dev/ttyACM* 2>/dev/null || echo "no ttyACM nodes — attach may still be in progress"
   ```
2. Query each port's serial and match it to the inventory table above:
   ```bash
   for dev in /dev/ttyACM*; do
     [ -e "$dev" ] || { echo "No /dev/ttyACM* ports found."; break; }
     echo -n "$dev => "
     (echo -e "*IDN?\r"; sleep 0.5) | timeout --foreground 2s picocom -b 115200 -q -x 1000 "$dev" \
       | tr -d '\r' | grep -m1 '^DAQiFi,' || echo "<no response>"
   done
   ```
3. Store the result in a variable (`DEV_PRIMARY=/dev/ttyACMn`) for the session instead of hardcoding. Later examples in this file use the literal `/dev/ttyACM0` as a stand-in for "the primary device".

**Either way:** wrong board selected? Re-target — never detach or drive a board that is not yours; another lane may be using it, and the bench device-guard will refuse you.

### Bootloader Entry
- Hold the user button for ~20 seconds until board resets
- Release button when LEDs light solid
- Hold button again until white LED blinks to enter bootloader mode

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
   - **SCPI Abbreviation Rule**: each node has exactly **two** legal spellings — the **full** node, or the node **truncated at its first lowercase letter**. Nothing in between. Per node, `UPPERCASElowercase` accepts `UPPERCASElowercase` and `UPPERCASE` only:
     - `SYST:COMM:LAN:NETMode` → `SYST:COMM:LAN:NETM` ✅ (short form)
     - `SYSTem:STReam:STOP` → `SYST:STR:STOP` ✅ (`STOP` is all-caps, so it has only ONE spelling — itself)
     - ⚠️ **A node with NO lowercase has exactly one legal spelling.** `SYSTem:COMMunicate:LAN:APPLY` is registered all-caps (`SCPIInterface.c`), so `APPL` is **`-113`**, not a short form. An earlier revision of this file used `APPLy → APPL` as the worked example here, which does not match what the firmware registers — verified on the device 2026-08-21. Check the pattern before abbreviating.
     - `CONFigure:ADC:OBDiag` → `CONF:ADC:OBD`, `CONF:ADC:OBDiag`, `CONFigure:ADC:OBD` … ✅ (each node independently full or short)
     - `CONFigure:ADC:OBDiag` → `CONFig:ADC:OBDiag` ❌ **`-113 Undefined header`** — `CONFig` is neither `CONFigure` nor `CONF`
     This is **stricter than "must contain all the CAPS"**, which a previous revision of this file stated: that phrasing also permits `NETMod` and `CONFig`, which the device rejects. The authority is `matchPattern` in `firmware/src/libraries/scpi/libscpi/src/utils.c:478` — `compareStr(full) || compareStr(short)`, where `compareStr` (`utils.c:347`) requires the lengths to be **equal**, so a partial prefix matches neither arm. `tools/lint/scpi_wiki_sync.py` implements this rule and pins it in `--self-test`.

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

#### SCPI Command Surface — keep it lean

**Before adding a new SCPI command, try to extend an existing one.** The
command table is a maintenance and documentation surface (every entry needs
a wiki row, a callback, client-library awareness); an ever-growing list is
a liability. Order of preference when adding capability:

1. **Add a parameter value to an existing setter.** A new mode almost always
   fits an existing command as another enum value. *Example (#158/#270):
   raw/no-calibration output shipped as `CONFigure:ADC:USECal 2` (0=factory,
   1=user, 2=raw) rather than a separate `CONF:ADC:RAWmode` — one command,
   backward-compatible (0/1 unchanged), no new table entry.*
2. **Add an optional parameter** to an existing command (e.g. an extra
   channel-index or flag argument) rather than a parallel command.
3. **Group under an existing namespace** (`SYST:...:`, `CONF:ADC:...`) so
   related functionality is discoverable together.
4. **Only add a brand-new command** when the capability is genuinely
   orthogonal to everything present and can't be expressed as a value/arg.

When you do extend a command, keep old values' behavior identical (append
new values at the end — don't renumber), and update the wiki row to document
the full value set. This preference is the user's standing rule (2026-07-06):
"keep our command list from blowing up."

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

#### SCPI Wiki Maintenance

**When adding, modifying, or removing SCPI commands, ALWAYS update the GitHub wiki SCPI reference page.**

This is now **enforced in CI** (`.github/workflows/scpi-wiki-sync.yml` →
`tools/lint/scpi_wiki_sync.py`), because writing it down was not enough: an
audit in August 2026 found the two had drifted in *both* directions — 37
shipped commands with no wiki entry at all (including the entire
`DIO:EVENt:*` and `DIO:COUNter:*` families), and 11 documented commands that
were not registered, two of them the **old names of a renamed pair**
(`SYSTem:DIOProbe:ASSign` → `MODE`/`ROUTe`), so following the reference
returned `-113`.

The gate runs only when `SCPIInterface.c` or the checker changes. It compares
the *registered* patterns (comments stripped — the file has 16 commented-out
`.pattern` entries that are **not** shipped) against the wiki, accepting either
the full or the caps-only abbreviated form. A wiki row for an unregistered
command is allowed only if the row says `NOT IMPLEMENTED` and why. Run it
locally with:

```bash
git clone https://github.com/daqifi/daqifi-nyquist-firmware.wiki.git /tmp/daqifi-wiki
python3 tools/lint/scpi_wiki_sync.py --wiki /tmp/daqifi-wiki
```

Because the wiki is a separate repo, the wiki edit cannot ride in the same PR —
push it before merging, which is precisely the step this gate exists to catch.

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

### Key Files for Understanding the System

1. **Application Entry**: `firmware/src/app_freertos.c` - Main application tasks
2. **Board Config**: `firmware/src/state/board/BoardConfig.c` - Hardware definitions
3. **Streaming Engine**: `firmware/src/services/streaming.c` - Data flow control
4. **SCPI Interface**: `firmware/src/services/SCPI/SCPIInterface.c` - Command processing
5. **HAL Drivers**: `firmware/src/HAL/ADC.c`, `DIO.c`, `DAC7718/DAC7718.c` - Hardware interfaces

### Development Considerations

- All hardware access must go through HAL layer
- **Read the PIC32 Family Reference Manual (FRM) BEFORE writing any peripheral
  register code — this MCU is complicated and the DFP header alone is not
  enough.** The device pack header gives you bit *names and positions* but NOT
  the *semantics*, enable-ordering, routing requirements, or the gotcha `Note:`
  callouts — and those are exactly where bring-up dies. The FRM section for each
  peripheral is authoritative; grep its actual text, don't infer from register
  width. Fetch the relevant section PDF from
  `ww1.microchip.com/downloads/.../ReferenceManuals/` (e.g. the 12-bit HS SAR
  ADC is **DS60001344E**), `pdftotext` it, and read the operation + register
  descriptions in full. Cite FRM `DS#`/register/bit for load-bearing claims
  (per the Debugging-discipline V-tag rule). Concrete #670 lessons the header
  hid and only the FRM revealed: the digital comparator needs **`DCMPGIEN`**
  (interrupt-enable, bit 6) set separately from `ENDCMP` — without it the event
  fires but no interrupt; the in-window condition is **`IEBTWN`**, not `IEHILO`
  (which is only "DATA < DCMPHI"); comparator/filter inputs are **limited to
  AN0–31** (`ADCCMPENx` is 32-bit by spec, not just by register width); and a
  result only reaches the comparator "if configured to use data from this
  particular sample" — a routing requirement invisible in the headers.
- **Prefer Microchip's own drivers, PLIBs, device packs, and reference code over
  rolling your own — always check what Harmony/MCC/the DFP already provides
  FIRST.** Our MCU has subtle, undocumented-in-headers sequencing that the vendor
  code already gets right; a hand-rolled register sequence will miss a routing
  or enable step (see #670). Order of preference: (1) a **Harmony driver / PLIB
  function** (`ADCHS_*`, `EVIC_*`, `GPIO_*`, `DRV_*`) — check `plib_*.h` and the
  Harmony framework/examples for the peripheral, and consider whether an MCC
  reconfigure would generate the feature before hand-rolling it; (2) **SFR
  bitfield accessors** (`ADCCON2bits.SAMC = val`) when no PLIB path exists;
  (3) **raw register writes** ONLY when no PLIB/bitfield path exists AND
  performance demands it OR SET/CLR atomic forms are needed — and then only
  after reading the FRM for that peripheral, always commenting *why* and citing
  the FRM. When you must hand-roll (no-MCC modules like REFO/comparator), still
  read the FRM init sequence first and mirror the vendor example's ordering.
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

**WiFi config sequence** (`NETType`: **1 = STA, 4 = AP** — `wifi_manager.h`; 0 is invalid and the setter rejects it, silently leaving the previous mode. `SECurity`: 0 = open, **3 = WPA with passphrase**)**:** `ENAbled 0` → set `NETType`/`SECurity`/`SSID`/`PASs` → `ENAbled 1` → `APPLY` (all-caps; `APPL` is `-113`) → wait ~20 s → verify `ADDR?` → `SAVE` (persists to NVM — without it a reboot reverts; **every flash wipes NVM regardless**). The WiFi module itself takes ~2–3 s to initialize after power-up. AP-mode defaults: open AP `DAQiFi-XXXX` (MAC suffix auto-applied to the bare default name; user names preserved), hostname = SSID (used for DHCP Option 12 network discovery), needs POWERED_UP.

### USB CDC behavior every test must respect

- **Host must read fast.** ≥1 ms read cadence = zero loss; a 500 ms poller dropped 1 MB in the same 16ch@3kHz A/B that a 1 ms reader survived clean. The firmware pipeline is leak-free — host-side reading is almost always the culprit in "USB drop" reports. Use `FastReader` (background drain) for anything streaming.
- **Delivery is bursty by design** (ZLP boundaries + one DMA transfer in flight + circular→DMA copy): `in_waiting` can read 0 while data sits in the usbipd pipeline; don't tight-poll it; allow 3+ s settle between sessions in naive scripts. Any SCPI command mid-stream "flushes" a short packet.
- **Rate measurements:** ONLY via `StreamingMeasurement` (`test_harness.py`) — PC-controlled window opened at first data byte, closed before STOP; wall-clock around SCPI start/stop adds ~10 % error, and the older first-byte-time SPS calculation caused ~10–15 % run-to-run variance (fixed by blocking FastReader + sleep-duration denominator — variance now near zero). `is_csv=True` counts rows; WiFi/SD pass `wait_for_serial=False`. Reference points: USB CSV 16ch@3kHz = 795 KB/s, 0 drops.
- **A CDC "wedge" from rapid serial open/close is almost always a HOST/method artifact, not firmware — verify before chasing it.** Realistic churn (DTR asserted, as pyserial / any real client does) runs clean (bench-verified 2026-07-19: 15 cycles, 0 errors, device healthy before+after). The apparent "wedge" (Write → *"semaphore timeout"*) shows up when opening *without* asserting DTR (`.NET SerialPort` defaults `DtrEnable=false`) or through the WSL/usbipd reconnect handshake — a fresh reflash + one careful DTR-asserted probe recovers it, no firmware change. The one genuine firmware heap-leak wedge (**#684**: WINC HIF semaphore leaked ~88 B per `SYST:COMM:LAN:POWer` toggle → `vApplicationMallocFailedHook` at `freertos_hooks.c:137`, ~round 87) is **fixed in #685** — hardware-verified 2026-07-19: `HeapFree` dead-flat across 24 `LAN:POWer` toggles (a leak would decline ~88 B/toggle). **Before investigating any CDC-wedge report, `gh pr list --state all --search` the symptom first** — this exact wedge was re-derived from scratch for hours in one session because that check was skipped. mdb can read the crash state on a *DEBUG_RUN* build on your lane's board (`-p 0`; take the port from the inventory table above or `bench whoami`, never a remembered number), but the debug image no longer fits RAM without a ~4 KB `STATIC_POOL_SIZE` trim.

### Long-running bench runs

- **Always `python3 -u`** (or `PYTHONUNBUFFERED=1`) for background runs — block-buffered stdout looks empty and is lost on kill. Never wrap a real run in a tight `timeout` — per-trial SCPI overhead alone is ~15–20 s, so runs outlast naive estimates; reserve `timeout` for genuinely-bounded one-shot probes and size it generously. Watch the PID or the per-row-fsynced CSV instead (the CSV is the reliable progress signal).
- **Sustained high-rate USB → run Windows-native** (`python.exe` against your lane's COM port — take it from the inventory table above or `bench whoami`, never a remembered number — with the repo cloned under `C:\`): the cumulative ISR=-1/unreadable-STATS wedge under back-to-back high-rate trials is a WSL/usbipd artifact. On a box that HAS usbipd, WSL `/dev/ttyACMn` is fine for low-rate/control-plane SCPI; on a box without it there is no such node and the native path is the only path. (Separately: WSL-launched python silently drops inbound UDP — use PowerShell-native tooling for UDP tests.)
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

**RULE (user standing rule, 2026-07-06): every firmware PR ships with a
regression test in `daqifi-python-test-suite`.** When you open a PR that
changes device behavior, a companion test that exercises the new behavior
(and would fail on the old firmware) must land in the suite — committed and
referenced in the PR body. Prefer building it from `test_harness.py`
primitives; if the test needs a new reusable capability, add it to the
harness (not a one-off in the script) so the next PR reuses it. Exempt:
docs-only, lint-only, comment-only, and pure-refactor PRs with no behavior
change. If bench hardware is occupied when the PR opens, the test is still
committed and the run is queued (note it in the PR); the test existing is
the requirement, not that it has run yet.

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
