# v3.4.7b1 - Streaming Ceiling, WiFi Resilience & Long-Run Stability

## Highlights

This release is the result of two months of focused work on the three
subsystems that customers rely on most: the streaming pipeline, the WiFi
stack, and the boot/init path. Nearly 100 PRs landed against `main`
between v3.4.6b1 and v3.4.7b1. The headline outcomes:

**Streaming ceilings raised across every interface.** Hardware ADC trigger
synchronization (#282), batched Type-1 ISRs (#277), per-task FPU
correctness, smaller and better-shaped circular buffers, and a new
multi-in-flight WiFi send path (#370) combine to push USB, WiFi, and SD
ceilings higher than v3.4.6b1 at every channel count. Pipeline jitter at
≥10 kHz is effectively eliminated by the new WINC idle-gate (#335) — CV
at 10 kHz drops from 9.44% to 0.5%, peak-to-peak from 326 µs to 8.8 µs
(measured via Saleae, Session 21).

**WiFi survives the failure modes that used to wedge it.** Bad-password
APPLY now backs off cleanly (#416), APPLY-during-active-TCP closes the
listening and connected sockets in the right order (#435/#438/#440), the
stuck-`isConnected` state-machine wedge that required a power cycle is
recovered by a single SCPI APPLY (#467/#468), and rapid APPLY-during-
REINIT is gated rather than deadlocked (#425/#434). A new in-firmware
iperf2 module (#381/#393/#403) gives us wire-rate truth on the WINC1500
path and the diagnostics to debug remaining state leaks.

**Long-run reliability hardened from boot to last sample.** Defensive
zero-init for BSS regions kseg0 best-fit alignment (#412/#409/#410),
configLIST_VOLATILE in `FreeRTOSConfig.h` drops the FreeRTOS_tasks.c -O1
workaround (#426/#432) — the kernel now builds at the global -O3, and
USB silent-loss accounting (#372/#448) plus self-heal + auto-stop on
dead transports (#397/#457) close the last gaps where streaming could
drift away from its counters. NVM auto-power-up (#454/#462) eliminates
the manual `SYST:POW:STAT 1` step for field-deployed loggers that
reboot under USB power.

## Added

### NVM auto-power-up on USB connect (#454/#462)

New opt-in SCPI surface; default off so existing behavior is preserved:

```
SYSTem:POWer:AUTOOn 0|1       # runtime
SYSTem:POWer:AUTOOn?
SYSTem:POWer:AUTOOn:SAVE      # persist to NVM
SYSTem:POWer:AUTOOn:LOAD
```

When enabled, the device auto-transitions `STANDBY → POWERED_UP`
whenever VBUS is present — at boot or on cable insertion mid-session.
Battery-only operation is unaffected. A per-session latch prevents
re-promote after a manual `SYST:POW:STAT 0` while USB stays plugged in.

### In-firmware iperf2 client + server (#381/#393/#403)

TCP and UDP iperf2 module embedded in firmware, exposing wire-rate
truth on the WINC1500 path independent of MCU encode/decode pipeline:

```
SYST:WIFI:IPERF:TCPClient <ip>[,<port=5001>][,<dur_s=10>]
SYST:WIFI:IPERF:UDPClient <ip>[,<port=5001>][,<dur_s=10>]
SYST:WIFI:IPERF:TCPServer [<port=5001>]
SYST:WIFI:IPERF:UDPServer [<port=5001>]
SYST:WIFI:IPERF:TXBLast <port>,<duration_s>      # #399 raw-rate workaround
SYST:WIFI:IPERF:STATs?
SYST:WIFI:IPERF:STOP
SYST:WIFI:IPERF:DIAGnostics?                     # WINC HIF + send-pipeline diagnostics
SYST:WIFI:IPERF:MAXPending <0..4>                # #399 throttle (0=default, required)
SYST:WIFI:IPERF:AUTOReset <0|1>                  # #399 auto-HRESet on stop (required)
```

`TXBlast` tx-mode (#403) bypasses the iperf2 framing for maximum-rate
write testing. Auto-HRESet between trials probabilistically clears
stuck WINC SDK state — see Known Limitations for the underlying iperf2
HIF state leak (#399, mitigated, not root-caused).

### Self-healing streaming with auto-stop on dead transports (#397/#457)

The streaming engine now monitors per-interface health. If all
configured transports report unhealthy continuously beyond a grace
window, streaming auto-stops with a `LOG_E` capture instead of
silently producing zero bytes forever:

```
SYST:STR:CONSumer:GRACe <sec>      # 5..300, default 60
SYST:STR:CONSumer:GRACe?
```

QUES bit 12 (Transport Down) latches the auto-stop event for clients
polling status registers. Cleared on next `SYST:STR:START`.

### Capability rollup schema V1 (#343)

Forward-looking single-source-of-truth capability discovery:

```
CONFigure:CAPabilities:JSON?       # full schema rollup (~11 KB JSON)
CONFigure:CAPabilities:APIVersion? # fast pre-parse compat probe (now: 2)
```

Per-channel `signal_type` (extensible: `voltage`, `temperature`,
`pressure`, ...), UCUM units, enumerated ranges, scaling and calibration
models. Streaming block carries supported encodings, transports, cap
formula constants, and buffer-size ranges. Storage, power, transports,
and triggers blocks complete the first-phase scope.

Three redundant subset queries removed: `CONFigure:ADC:MAXFreq?`,
`CONFigure:CAPabilities:AIN?`, `CONFigure:CAPabilities:DIO?` — their
data is now exclusive to the JSON rollup. Schema version bumped 1 → 2.

### Hardware ADC trigger synchronization (#282/#286)

Timer4/5 hardware-match triggering for ADC conversions, replacing the
previous deferred software trigger. Throughput improvement
+12-39% on streaming configs vs the pre-#286 baseline (PR #286 body;
exact channel-count sweep in Session 20 characterization).

### Batched Type-1 ADC ISRs (#277/#278)

Single CH3 trigger reads all enabled Type-1 (dedicated module) channels
in one ISR entry. Eliminates N-1 redundant interrupts per sample.
Measured +15-18% on 16-channel configurations (PR #278 body).

### Per-module ADC acquisition-time tuning (#329)

```
CONFigure:ADC:SAMC:DEDicated <ticks>     # Type-1 dedicated modules
CONFigure:ADC:SAMC:DEDicated?
CONFigure:ADC:SAMC:SHARed <ticks>        # Type-2 shared MODULE7 mux
CONFigure:ADC:SAMC:SHARed?
```

For users who need to trade ADC settling time against sample rate at
the edge of the cap envelope.

### Onboard-diagnostics gating (#287)

```
CONFigure:ADC:OBDiag 0|1         # skip rail-monitoring scans during
                                 # streaming (default 1 = enabled)
```

OBDiag=0 yields measurably higher ceilings on single-channel configs
(USB CSV 1×T1: 16,200 Hz → 20,000 Hz, +24%; SD CSV 1×T1: 9k → 10k);
trade-off is loss of live rail-voltage updates. Stale rail values
remain visible with age indicators per the SCPI data-visibility
principle.

### Configurable voltage precision (#210)

Systemwide voltage decimal-precision control affecting CSV streaming,
JSON streaming, and SCPI voltage queries:

```
CONFigure:VOLTage:PRECision <0-10>
CONFigure:VOLTage:PRECision?
CONFigure:VOLTage:SAVE | LOAD
```

Board-specific defaults: NQ1=4 decimals, NQ3=6, NQ2=7. Precision 0
emits raw integer millivolts via fast `int_to_str` path. NVM-persisted.

### Runtime per-module log levels (#240)

```
SYSTem:LOG:LEVel <module>,<level>     # POWER, WIFI, SD, USB, SCPI,
                                      # ADC, DAC, STREAM, ENCODER,
                                      # GENERAL × 0..3
SYSTem:LOG:LEVel:ALL <level>
SYSTem:LOG:LEVel?
```

All modules ship at compile-time DEBUG ceiling and runtime default of
ERROR. INFO/DEBUG can be enabled on-demand without a rebuild. Runtime-
only — resets to ERROR on reboot.

### Streaming test patterns + benchmark modes (#216/#234/#257/#264)

```
SYSTem:STReam:TEST:PATtern <0-6>     # 0=off, 1=counter, 2=midscale,
                                     # 3=fullscale, 4=walking,
                                     # 5=triangle, 6=sine
SYSTem:STReam:BENCHmark <0|1|2>      # OFF / NoCap / Pipeline
SYSTem:STReam:THRoughput <freq>,<dur>
```

Deterministic regression and benchmarking. Pattern values override only
the sample `Value` field — real ISR timing, ADC triggering, pool
allocation, and the full encoding pipeline are preserved. PIPELINE mode
bypasses ADC reads to isolate encoder + output cost. See CLAUDE.md
"Streaming Throughput Benchmarking" for the full decision tree.

### Streaming statistics and ISR overrun tracking (#266/#319)

`SYSTem:STReam:STATS?` extended with `TimerISRCalls`, per-stage drop
counters (USB / WiFi / SD), and a `WindowLossPercent` field from the
sliding-window flow-health tracker. `STATS:CLEar` resets all counters.
Every SCPI execution error is now also captured to the log buffer.

### Stream control SCPI alias series (#311/#322/#323/#324)

Canonical `SYST:STR:*` namespace consolidates all streaming control.
Legacy aliases remain — existing client libraries continue to work
unchanged. See Upgrade notes.

### DIO debug probe framework (#301/#307)

Compile-time and runtime debug probes for pipeline timing measurements
via Saleae. Ten standard pipeline probes (ISR entry, encoder enter/
exit, transport writes, etc) plus six ad-hoc compile-time slots.
Caps-only SCPI under `SYST:DIOProbe:*` — `MODE`, `ROUTe`, `CLEar`,
`CLEar:ALL`, `PIPELine`, `LIST?`.  Probe-to-DIO routing is now
runtime-flexible (#408). Living measurement record:
`docs/PIPELINE_TIMING.md`.

### SD card abort + format-status (#211/#212)

```
SYSTem:STORage:SD:ABORT
SYSTem:STORage:SD:FORmat?     # query format state/progress
```

Long file transfers can now be cancelled without power-cycling.
`FORmat?` lets clients poll long-running format operations.

### SD write metrics in streaming stats (#225)

Per-session SD write byte counters and drop counters now surfaced in
`SYST:STR:STATS?`, completing the per-transport observability picture
alongside USB and WiFi counters.

### iperf2 diagnostics SCPI (#393/#403)

`SYST:WIFI:IPERF:DIAGnostics?` exposes the iperf2 state machine and
WINC send-pipeline state.  Fields: `Mode`, `DataSock`, `ListenSock`,
`PendingTx`, `AbortPending`, `BytesConfirmed`, `LastSendRc`,
`SendErrCount`, `WincState`, `FreeTcpSockets`, `FreeUdpSockets`
(see `SCPI_Iperf2_Diag` for the canonical field list).  This is the
instrumentation that backed the multi-in-flight Step C work and the
auto-HRESet decision.

## Changed

### WiFi auto-balance circular buffer 32 KB → 64 KB (#364)

When WiFi is the active streaming interface, the auto-balanced
circular buffer size doubles. Measured Session 22 single-channel WiFi
PB ceilings reflect this baseline; the buffer is a burst absorber, not
a wire-rate raiser. 64 KB is empirically the sweet spot — 128 KB
regresses 4-5 sentinel configs.

### Multi-in-flight WINC sends (#370)

WINC SPI send queue depth N=1 → N=4 (Step C of the #362 program).
Measured +109% wire rate on Tesla-saturated 16-channel CSV at 2 kHz
(see PR #370). USB and SD paths are unaffected.

### Dynamic WINC idle-gate (#335)

`lWDRV_WINC_Tasks` is paced only when streaming is active on a
non-WiFi interface, eliminating the 22 Hz / 270 µs preemption that
showed up as jitter on USB/SD streaming. CV at 10 kHz USB CSV drops
9.44% → 0.5% (Session 21, Saleae-verified).

### Streaming timer prescaler 1:256 → 1:8 (#303) + off-by-one (#302)

Higher timer resolution at low rates plus a one-tick correction in the
period calculation. Together these tighten timer jitter at all rates
and remove a small (sub-microsecond) systematic phase offset.

### USB CDC behavior on stop (#264)

Streaming task now issues a USB CDC flush on `SYST:STR:STOP` so
client-side reads complete cleanly without waiting for the next
unrelated SCPI command to flush the pipeline.

### Streaming engine `Running` flag semantics (#379/#458)

`Running=1` now reflects "timer actually firing" rather than "start
requested" — pre-existing race where SCPI clients could see Running=1
during the brief gap before the timer hardware actually fired is
closed.

### `*IDN?` SN field populated (#436/#441)

The `*IDN?` response now includes the real per-unit silicon serial in
the SN field. Generic `DAQiFi,Nq1,0,01-02` previously carried no
per-unit identifier — `*IDN?` is now sufficient for board ID where it
wasn't before.

### Stream control SCPI namespace consolidation (#311/#322/#323/#324)

Canonical and legacy forms (both work — see Upgrade notes):

| Canonical              | Legacy alias              |
|------------------------|---------------------------|
| `SYST:STR:START`       | `SYST:StartStreamData`    |
| `SYST:STR:STOP`        | `SYST:StopStreamData`     |
| `SYST:STR:DATA?`       | `SYST:StreamData?`        |
| `SYST:USB:TRANSparent:MODE` | `SYST:USB:SetTransparentMode` |

STATS-family renamed to eliminate single-letter short forms; TESTpattern
and LOGging similarly normalized. Legacy aliases will not be removed
without a scheduled deprecation cycle (months out, with explicit
client-maintainer comms).

### SCPI input buffer 256 → 512 bytes (#263)

Larger commands no longer truncate mid-parse. Notably matters for the
new capability JSON rollup and longer batched command sequences.

## Fixed

### WiFi reconnect resilience after bad-password APPLY (#416/#446)

Bad-password APPLY used to leave WiFi in an unrecoverable state when
followed by a good-password APPLY — only a power-cycle recovered.
Reconnect backoff now applies to initial-association failures, not
just runtime disconnects, so the retry path doesn't accumulate
state.

### Stuck `isConnected` WiFi wedge (#467/#468)

After rapid auth-fail cycles, the WINC driver's internal
`pCtrl->isConnected` could remain `true` despite the firmware's
`STA_CONNECTED` flag being 0. Subsequent `IPUseDHCPSet` returned
`REQUEST_ERROR` → "Error setting DHCP" → ERROR → REINIT loop forever.
Fix: broaden `BSSDisconnect` gate in REINIT path B from `STA_CONNECTED`
to `STA_CONNECTED || STA_STARTED` so the chip-level disconnect runs
unconditionally when stale state may exist. Same widening applied to
STA→AP mode-switch path.

### APPLY-during-REINIT gated (#425/#434)

Rapid back-to-back APPLY calls — or APPLY-during-active-TCP — could
race the WiFi state machine into a deadlock at `wifi_manager.c:761`
recoverable only by power-cycle. APPLY now refuses (with SCPI error +
LOG_E) while a previous REINIT is still in flight.

### TCP/UDP socket teardown on STA reconfigure (#435/#438/#440)

Closing TCP server, TCP client, and UDP discovery sockets before
issuing a `BSSReconnect` for STA reconfigure. The follow-up (#440)
broadens the HardReset divert to bound listening sockets so the
divert fires for the actual socket count rather than just the
expected primary. Mutex-safe `CloseClientSocket` (#439) protects
against cross-task socket-state races.

### STA_CONNECTED flag drift reconciliation (#382/#451)

When the WINC driver and our state machine disagreed about connection
state (one of the two #382 sub-bugs), the firmware now reconciles
toward the driver's view rather than getting stuck on its own flag.

### One-client-per-port TCP policy (#452/#453)

Previously a second connecting client could displace a working
connection mid-stream. New policy: the listening socket politely
rejects a new connect attempt while an existing client is active on
the same port.

### Auth-fail false-positive log suppressed during APPLY teardown (#423/#447)

The intermediate disconnect event fired during APPLY teardown used to
log as `Auth-Failed`, polluting `SYST:LOG?` output during routine
reconfiguration. Suppressed when the disconnect is part of an active
teardown.

### USB silent-loss accounting (#372/#448)

Streaming `hasUsb` predicate replaced with `ActiveInterface` gate.
Previous predicate could silently skip USB writes when conditions
briefly degraded, accounting for zero in `UsbDroppedBytes`. Now every
skipped write is counted.

### Streaming timer ISR off-by-one (#302) and prescaler resolution (#303)

See Changed section.

### Type-2 channel scan registration (#406/#421/#422)

`TRGSRC=3` now enrolls Type-2 channels in the MODULE7 scan list. The
original symptom (#406) was a Type-2 channel missing samples after a
specific reconfigure sequence; #421 audited the broader set-once
pointer hazard class but landed as a documentation/audit conclusion
(see `docs/SET_ONCE_POINTER_AUDIT.md`), not a code change.

### NQ3 ADC union safety + T2 muxed-cap SCPI error (#139/#232/#449)

NQ3's union storage for ADC channel state could be accessed via the
wrong arm in rare reconfigure paths; explicit per-arm initialization
added. Muxed-cap configurations on Type-2 channels now return a proper
SCPI error instead of silently capping.

### ADC rail monitoring with `IsDataValid` gate removed (#460/#461)

The gate was rejecting valid samples during a short transient window
after enable, leaving rail-voltage queries stale longer than necessary.
Gate removed; staleness now bounded by the ADC's own ready signal.

### Streaming `Running` flag race (#379/#458)

See Changed section.

### Circular buffer producer/consumer counter split (#122/#276/#325)

Split single uint32 head/tail into separate producer and consumer
counters to eliminate a sub-microsecond race on the wrap boundary
where a partial RMW could expose a stale length to the consumer.
NULL-guards added to defensive call sites.

### Defensive zero-init for BSS / kseg0 best-fit (#406/#409/#410/#412)

Some BSS regions placed in kseg0 best-fit by the linker were not
covered by the standard `_bss_start..._bss_end` zero loop on cold
boot. Added explicit zero-init for the affected regions in `main`
before `SYS_Initialize`. Mitigates a rare crash class observed in
post-reset boot loops; the underlying issue is well-bounded but the
zero-init defense is permanent.

### Capabilities query — board configuration `Resolution` type (#466)

`MC12bModuleConfig.Resolution` and `AD7609ModuleConfig.Resolution`
changed from `double` to `uint32_t` in `AInConfig.h`. A pure-integer
task reading a `double` field could pick up garbage FPU state from
whatever was last in the FPU registers. Exposure was the UDP
discovery announce packet's `analog_in_res` field. PR #369's
hardcoded constants in the streaming hot path remain as defense in
depth.

### SCPI input buffer overflow (#100/#263) and UDP discovery sprintf overflow (#41/#46/#260)

Buffer size + sprintf-format hardening. See PR bodies for symptom
specifics.

### WiFi power guard + CONF queries + DHCP values (#93/#97/#261)

WiFi configuration SCPI now refuses operations that require power-up
when the device is in STANDBY, returning a SCPI error rather than
silently doing nothing. New CONF queries expose DHCP-assigned values.

### SD file open race before streaming (#222)

`SYST:STR:START` to SD now waits for the file-open completion before
arming the timer, eliminating a small window where the first samples
could be written to a not-yet-open handle.

### SD graceful shutdown on power-down + WiFi SPI contention (#218)

When the device transitions to standby with an SD file open and a
WiFi transfer in flight, the shutdown order now serializes correctly
so neither subsystem sees a half-finished SPI transaction.

### Encoder hot-path O(N) channel lookup (#268/#269/#272)

Replaced per-sample linear channel-config lookup with index-based
access. Measurable in the per-tick budget at high channel counts.

### CoherentPool quiesce before resize (#255/#256)

Buffer resize now stops all DMA consumers before re-partitioning the
coherent pool, eliminating a race where a partial DMA could corrupt
post-resize buffers.

### #353 SCPI-over-TCP / WINC stack-overflow chain (#347/#348/#350/#354/#356)

The compounding root cause behind the v3.4.6b1 SCPI-over-TCP wedge:

- #347/#348: `SCPI_SysInfoPB` 4 KB stack-local exceeded the WifiTask
  1024-word stack
- #350: shared SCPI response buffer (2 KB static, mutex-guarded) so
  no callback needs a large stack-local
- #354 root cause: `ADC_WriteChannelStateAll` 5 KB stack-locals (two
  of them) overflowed the WINC task's 4 KB stack via SCPI-over-TCP
- #356 Option 2: SCPI dispatch decoupled from WDRV_WINC_Tasks onto
  app_WifiTask, restoring WINC to its 1024-word baseline (peak 316)

Post-merge: SCPI-over-TCP runs on its own task stack with measured
peak well below ceiling. WINC stack untouched. Throughput unchanged
to +1k on five USB configs (Session 22 vs Session 20).

### Streaming pipeline jitter and throughput (#308/#309/#310)

Five firmware interventions across 19 PIPELINE_TIMING sessions: SD
priority elevation, encoder retry yield strategy (#312/#314 follow-up),
prescaler tightening, and ISR-deferred-task scheduling tuning. USB
CSV +11-32% / USB PB T1 +8-20% in Session 17 vs Session 1 baseline.

### Streaming task encoder retry uses `vTaskDelay(1)` (#312/#314)

Replaces `taskYIELD()` so lower-priority SD task actually gets CPU to
drain its circular buffer. SD CSV 16ch ceiling fully recovered;
SD PB 1×T1 +10%.

### MCU SD circular-buffer + DMA balance (#247)

SD circular buffer moved into the streaming pool; DMA buffers (SD
write, USB write, WiFi SPI staging) auto-balanced from the 124 KB
coherent pool based on active interfaces.

### CSV writer raw-counts path + JSON field-name compaction (already in v3.4.3, refined in #210)

Voltage-precision = 0 in #210 routes CSV through the fast integer
path. JSON field names normalized at proto3 migration time (#237).

### Sample pool compaction (#264)

Per-sample memory footprint reduced from fixed 208 bytes to a
channel-count-dependent stride (1ch=14 B, 4ch=26 B, 8ch=42 B, 16ch=74 B).
Pool depth at fixed budget scales accordingly.

## Internal

**Build / CI / kernel:** #275 enables global `-O3` with FreeRTOS@O1 override
(now obsolete — see #426/#432) · #298 BUSL-1.1 license + NOTICE · #352
MPLAB X project metadata sync (DioProbe + Util headers) · #414 cppcheck
static-analysis CI gate · #426/#432 `configLIST_VOLATILE` in
`FreeRTOSConfig.h`, drop FreeRTOS_tasks.c -O1 clamp — kernel now builds
at global -O3 alongside the rest of the firmware · #459 parallelize
cppcheck (`-j nproc`, 10m → 23s).

**Refactors and renames (behavior-preserving):** #316 (BQ:FAULTCLear →
BQ:FAULT:CLEar) · #320 audit `taskENTER_CRITICAL_FROM_ISR` + remove
dead HeapList infrastructure (#294) · #337 DIO HAL + probe delegation
pair · #339 `StreamingInterface_All` → `StreamingInterface_UsbAndSd`
(name reflects actual semantics).

**Observability and instrumentation:** #226 clear `lastSendSize` after
callback reads it (eliminates false-positive double-count) · #223
WiFi TCP send-delivery tracking · #237 proto3 migration + encoder
optimization · #242 per-error one-shot LOG suppression macros · #244
add `LOG_E` / `LOG_I` / `LOG_D` to silent failure paths across SD,
USB, WiFi, ADC · #259 callback-driven WiFi sends + optimized flush
threshold (#253) · #265/#266 timer-ISR overrun counters · #290 stale
data indicators + EOS interrupt control (#288) · #293 move T1 result
reads from ADC_DATA3 ISR to EOS task (#292) · #295/#296/#297/#299
silent-loss instrumentation pass (queue, encoder, transport) · #319
log every SCPI execution error via central helper (#262) · #369
WiFi-pipeline diagnostics (alongside the (uint32_t)double cast fix).

**Documentation and audits:** #321 Session 20 refresh · #338 Session 21
refresh · #357 Session 22 refresh · #424 rip out #421 diag
instrumentation post-merge · #431 atomicity rules + bench protocol +
Makefile caveat added to CLAUDE.md · #433/#444 set-once pointer
volatile audit conclusions (see `docs/SET_ONCE_POINTER_AUDIT.md` —
audit found the qualifier is observable in codegen but not load-
bearing for correctness on this XC32 v4.60 build).

**Test infrastructure:** #408 runtime-flexible DIO probe routing ·
in-tree iperf2 module (#381/#393/#403, listed under Added; the
client+server harness pieces are internal scaffolding).

**Chores:** #442 commit `checkpoint_remote` in project settings ·
#473 bump `FIRMWARE_REVISION` to `3.4.7b1`.

## Test coverage

- **Bench characterization Session 22** (2026-04-24 overnight, post-#354
  ADC stack fix + #356 SCPI-dispatch Option 2 decouple) — full USB and
  SD ceiling sweep at 60 s endurance. Five USB configs picked up +1 k
  ceilings vs Session 20; SD CSV 8×T2 picked up +2 k. No regressions.
  Detailed tables in CLAUDE.md.
- **WiFi characterization Session 23** (2026-04-25) — single-trial
  ceiling sweep, no endurance. Captured pre-#371 silent-loss accounting
  fix and reflects measurement artifact, not true ceilings. CLAUDE.md
  flags this explicitly with a strikethrough advisory; Session 24
  numbers (post-#372/#448) will replace.
- **Manual MED-tier functional tests** per release tracker #464: SCPI
  alias smoke (`SYST:StartStreamData` and `SYST:STR:START` both work),
  #454 NVM auto-on toggle + save + reboot, #416 bad-pw backoff timing,
  #425 rapid APPLY refusal, #467 reproducer recovery in <5 s.
- **#467 reproducer** verified post-merge on NQ1 `7E2898F46200E8A7`:
  pre-fix wedged at 192.168.1.1 with `Error setting DHCP` storm,
  post-fix recovered cleanly within 5 s on first restore APPLY.
- **cppcheck baseline:** 2 style findings, unchanged from v3.4.6b1.
  Both in `wifi_serial_bridge.c` (ergonomic, not bugs).

## Known limitations

- **#463 — USB streaming ceilings 3-5 kHz below Session 22 baseline on
  current main.** Hypothesis split between firmware regression and test
  harness drift; cheap A/B planned. Documented as the only release-
  blocker candidate in #464.
- **#399 / #401 — iperf2 HIF state leaks and WINC1500 wedging.** WINC
  send path can stall after a defined sequence of connection-setup
  failures; auto-HRESet between trials probabilistically clears the
  stuck state (#403). Root cause is plausibly a WINC SDK chip-wake /
  sleep refcount asymmetry — hypothesized, not proven; tracked for the
  v3.4.8+ window.
- **WiFi optimization items deferred to v3.4.8+:** #361 (WINC SPI
  staging), #362 (multi-in-flight further steps beyond #370's Step C),
  #376 (decouple drain into dedicated task — needs DMA SPI to ship a
  net win, not just shift the bottleneck), #378 (related WINC-side
  diagnostics).
- **SD-focused overnight sweep not run this release cycle.** Last
  night's overnight (`20260515_2318`) was USB+WiFi only; SD path was
  validated via characterization Session 22 and the targeted MED-tier
  tests but no fresh endurance pass exists post-#449.

---

## Upgrade notes

- **Client libraries continue to work unchanged.** All legacy SCPI
  command forms remain as aliases (#311/#322/#323/#324). Canonical
  `SYST:STR:*` namespace is documented in the wiki SCPI reference
  (`01-SCPI-Interface.md`); migrate on your own schedule.
- **NVM auto-power-up is off by default.** Existing devices upgrading
  from v3.4.6b1 see no behavior change unless `SYST:POW:AUTOOn 1` is
  explicitly set and `SYST:POW:AUTOOn:SAVE` issued. To opt in:

  ```
  SYST:POW:AUTOOn 1
  SYST:POW:AUTOOn:SAVE
  ```

- **NVM is wiped on every IPE flash.** WiFi credentials, voltage-
  precision, calibration, and the new `AUTOOn` setting all live in
  program flash on the PIC32MZ. Restore via your usual configuration
  batch after each flash.
- **If you encountered the #467 wedge symptom on v3.4.6b1 in the field**
  (`Error setting DHCP` log floods, requires `SYST:POW:STAT 0/1` to
  recover), upgrade to v3.4.7b1 — a single SCPI APPLY now recovers
  cleanly without a power cycle.
- **Schema version 2 capability rollup is a breaking change for clients
  that depended on the removed subset queries** (`CONF:ADC:MAXFreq?`,
  `CONF:CAP:AIN?`, `CONF:CAP:DIO?`). Use `CONF:CAP:JSON?` and parse
  the corresponding fields. The `CONF:CAP:APIVersion?` probe returns
  `2` post-upgrade.
- **`*IDN?` SN field is now populated.** Clients that previously
  identified boards by streaming-metadata-header `# Serial` line can
  continue to do so, but `*IDN?` is now sufficient on its own.

---

**Full Changelog:** https://github.com/daqifi/daqifi-nyquist-firmware/compare/v3.4.6b1...v3.4.7b1
