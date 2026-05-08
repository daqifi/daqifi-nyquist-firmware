# #406 / #421: 5 Type-2 ADC channels stream zeros when WiFi is associated

Diagnostic scripts and Makefile patchers used during the
investigation of the "CH5/6/7/8/11 stream zeros" symptom. See
`docs/406_O3_INVESTIGATION.md` for the full writeup.

## Symptom recap

When streaming 16 channels with WiFi STA associated to an AP
(regardless of TCP traffic), CH5/6/7/8/11 (Type 2 / MODULE7 mux
scan) report zero. ISR fires exactly once per session for those
five vectors, then never again. CH24/CH38 (also scan-list,
different vectors) fire normally.

When WiFi is in AP mode (post-flash, NVM wiped) or disabled,
all 16 channels work.

## Bench expectations

These scripts assume the 2026-05 bench inventory in `CLAUDE.md`:

- WSL device: `/dev/ttyACM0` (verify with `find_bench_device.sh`
  if multiple boards attached)
- Bench primary serial: `7E2898F46200E8A7` (busid 2-4 / COM3)
- WiFi AP for STA tests: `Tesla` (creds in `~/.daqifi.env`)
- Device IP after STA association: `192.168.1.160`

## Scripts

### Device selection

- **`find_bench_device.sh`** — non-invasive busid → /dev/ttyACMn
  mapping via `/sys/devices/platform/vhci_hcd.0/status`. Always
  use this before tests instead of hardcoding `ttyACM0`.

### Streaming repro

- **`idn.py PORT`** — single `*IDN?` to verify SCPI is alive.
- **`probe.py PORT`** — step-by-step state probe (power, channel
  enable, format, interface, error queue).
- **`stream_test.py PORT`** — single 16-channel USB stream, 4 s,
  reports `rows_seen` and `nz_per_row[:5]`.
- **`stream_test10x.py PORT [N]`** — N=10 trials USB streaming.
  Reuses one serial session across trials.
- **`wifi_stream_test.py IP [N]`** — N trials of 16-channel
  streaming over TCP (port 9760). Uses one socket for setup, then
  fresh socket per trial.
- **`usb_with_wifi_active.py PORT [N]`** — USB streaming while
  WiFi is associated (no TCP client). The smoking gun: this is
  10/10 broken when WiFi connected, 10/10 working when not.
- **`wifi_diag.py IP PORT_USB`** — single WiFi trial + USB-side
  `SYST:LOG?` dump of the `diag421` SFR / ISR-count diagnostics
  emitted from `streaming.c::Streaming_Start/Stop`.
- **`test_sfr_dump.sh`** — one-shot build + flash + 16-ch stream
  + log dump for the production firmware.

### WiFi setup

- **`sta_setup.batch`** — driven by `~/.claude/skills/scpi/batch.sh`
  with `--env ~/.daqifi.env`. Restores Tesla STA credentials
  after a flash (which always wipes NVM). Outputs the device
  IP when association completes.

  ```bash
  bash ~/.claude/skills/scpi/batch.sh \
      tools/diagnostics/406_zero_channels/sta_setup.batch \
      /dev/ttyACM0 --env ~/.daqifi.env
  ```

  ⚠️ `batch.sh` echoes the resolved command including the
  password. Keep terminal output local.

### Optimization-level bisect

These mutate `firmware/daqifi.X/nbproject/Makefile-default.mk`
(gitignored), then build/flash/test in one shot. Useful for
bisecting which file at which -O level fixes / breaks the
symptom. **Must run with WiFi associated** to actually exercise
the bug — see investigation doc.

- **`bisect_olevel.sh "-O2"`** — set ADC.c (line 2407) to a
  given -O level.
- **`pin_file_olevel.sh LINE_NO -O1 [obj_path]`** — pin any
  Makefile compile rule at a chosen -O level, by line number.
  Looks up the obj path argument to delete the .o for clean
  rebuild.
- **`bisect_o3_flag.sh "-fno-ipa-icf"`** — keep ADC.c at -O3
  but add a single `-fno-*` flag.

Line numbers known so far (production rules; debug = -O3 fixed):

| File | Line | obj path |
|------|------|----------|
| `firmware/src/HAL/ADC.c` | 2407 | `firmware/daqifi.X/build/default/production/_ext/659825273/ADC.o` |
| `firmware/src/HAL/ADC/MC12bADC.c` | 2353 | `.../_ext/1127389162/MC12bADC.o` |
| `firmware/src/config/default/interrupts.c` | 2269 | `.../_ext/1171490990/interrupts.o` |

Re-derive line numbers if Makefile is regenerated:

```bash
awk '/<filename>/ && /MP_CC/ && !/__DEBUG/ {print NR": "$0; exit}' \
    firmware/daqifi.X/nbproject/Makefile-default.mk
```

## Recovering from a wedge

USB CDC and WINC can wedge under repeated stream/stop cycles
or interrupted python sessions:

```bash
# Detach + reattach USB
powershell.exe -Command "usbipd detach --busid 2-4"
sleep 2
powershell.exe -Command "usbipd attach --wsl --busid 2-4"
sleep 3
ls -la /dev/ttyACM*

# If still wedged: full reflash (wipes NVM)
bash ~/.claude/skills/flash/flash.sh --build
sleep 3
bash ~/.claude/skills/scpi/batch.sh \
    tools/diagnostics/406_zero_channels/sta_setup.batch \
    /dev/ttyACM0 --env ~/.daqifi.env
```

## Diagnostic instrumentation in firmware

Two pieces of source-controlled instrumentation feed these
scripts:

1. **`firmware/src/config/default/interrupts.c`** — per-channel
   `gAdcIsrCount_5/6/7/8/11/24/38` volatile uint32_t counters
   incremented in each `ADC_DATAn_Handler`.
2. **`firmware/src/services/streaming.c`** — `LOG_E` dumps of
   `ADCCSS1/2 / ADCGIRQEN1/2 / ADCTRG1/2/3 / ADCCON1 / ADCANCON
   / IEC2 / ADCDSTAT1` at `Streaming_Start` (post-trigger-config)
   and `Streaming_Stop` (pre-revert), tagged `diag421-start:`
   and `diag421:` respectively.

Retrieve via `SYST:LOG?` after a stream session.
