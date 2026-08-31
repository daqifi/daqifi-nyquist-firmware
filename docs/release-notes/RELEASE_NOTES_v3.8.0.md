# DAQiFi Nyquist Firmware v3.8.0

**Release date:** 2026-08-31
**Baseline:** v3.7.2 (last full release). Supersedes the **v3.7.3 pre-release**,
whose 15 cherry-picked SD/cap fixes are all included here.

The largest release in the 3.x line: **97 merged PRs**. Two things make it a
minor-version bump rather than a patch.

First, the **DIO terminal becomes a general-purpose instrument bus.** A Nyquist
can now drive a user SPI master, a user UART, a user I2C bus, programmable
clock outputs, hardware input capture, and hardware edge/pulse counters — on
the same terminal that previously only did digital I/O. Closing epic #664.

Second, the **CPU runs at its rated 252 MHz from boot** (#741), and the
streaming rate caps have been re-fitted against that clock with a
freeze-aware, drop-honest methodology.

Alongside those: hardware analog threshold alarms, mDNS/DNS-SD network
discovery, CRC32-protected settings, and a large cross-transport concurrency
hardening series that closes a class of races between USB and WiFi SCPI.

---

## New features

### The DIO terminal peripheral family (epic #664)

Each is opt-in, disabled by default, and releases its pins when disabled.

- **User SPI1 master (#665)** — `SYST:COMM:SPI:CONFig|ENAble|TRANsfer?`.
  Single-command frame ceiling is ~238 B, and that limit is now documented
  honestly rather than discovered at runtime (#695).
- **User UART (#16)** — `SYST:COMM:UART:CONFig|ENAble|INVert|WRITe|READ?|COUNt?`,
  with a 300-baud floor (#700).
- **User I2C (#15)** — `SYST:COMM:I2C:ENAble|SEGment|FREQuency|SCAN?|TRANsfer?`,
  routed through the PCA9516A hub. **The terminal has no pull-ups** — the
  attached device must supply them and be powered, or `SCAN?` returns empty.
- **Programmable clock outputs (#668)** — `DIO:CLOCk:CONFig|ENAble|?` (REFCLKO).
- **Input capture (#666)** — `DIO:MEASure:FREQuency?|PERiod?|PWIDth?|DUTY?`.
  The phantom capture latched at arm time is discarded (#760).
- **Edge events + pulse totalizers (#667)** — `DIO:EVENt:*`, `DIO:COUNter:*`.
  Timestamps are valid from boot because TMR6 free-runs (#731).

> **#761 also matters to fielded devices:** the firmware no longer sets
> `IOLOCK`/`PMDLOCK`. Those one-way locks made the whole DIO-terminal family
> unreachable on a device that had already booted once.

### Measurement and acquisition

- **Hardware analog threshold alarms (#670)** — `CONF:ADC:THREshold[?|:CLEar]`,
  using the ADCHS digital comparators. Trip detection is via the ISR trip
  count, not the `DCMPED` status bit.
- **Railed-channel reporting (#814)** — a clipped sample is now visible as
  clipped instead of silently reading full-scale.
- **Streaming timebase exposed (#730)** — in both `SYSInfoPB` and
  `CONF:CAP:JSON?`, so a host can convert sample timestamps without guessing.
- **Catch-up counter (#821)** — `CatchUpSamples` in `SYST:STR:STATS?` makes
  deferred-task backlog (and therefore possible value/stamp skew) visible.
  It is an upper bound and is **not** counted as loss.

### Networking

- **mDNS / DNS-SD responder (#345)** — the device advertises
  `_daqifi._tcp.local`, so hosts can discover it by name instead of by IP.
- **mDNS diagnostics + self-heal (#58)** — `SYST:COMM:LAN:MDNS?` returns a JSON
  health blob (`Active`, `RecvArmed`, `Rx`, `Match`, `Resp`, `ArmFail`, `Heal`,
  `LastArmRc`, …), and a deaf receive socket re-arms itself.

### Storage

- **SD working directory (#799)** — `SYST:STOR:SD:DIRectory[?]` makes the
  working directory readable and settable, and `SD:LISt?` no longer moves it.
- **Subdirectory bucketing (#689)** — a long logging session no longer builds
  one huge directory, avoiding the FatFs O(N) create wedge.
- **Every busy refusal names the SD state and mode (#782)** — a `-200` now
  tells you *why*.

### Data format and diagnostics

- **Compact CSV (#619)** — `SYST:STR:FORmat 3`: one leading `timestamp` column
  instead of a per-channel `ain<N>_ts`. Opt-in; formats 0/1/2 unchanged.
- **Build fingerprint (#833)** — `CONF:CAP:JSON?` now carries `firmware_crc32`,
  a CRC32 of program flash excluding the settings region. Two builds of the
  same release string are finally distinguishable in measurement data.
- **CRC32 settings integrity (#306)** — settings are checksummed, and wolfSSL is
  severed from the save path.
- **Sample-pool visibility (#828/#834)** — `SamplePoolPartitioned` and
  `SamplePoolClampedSlots` expose the difference between slots carved and slots
  usable, which was previously visible only as a log line.

---

## Performance

- **252 MHz SYSCLK from boot (#741)** — `SPLLCON` is runtime-writable even
  though the config words are not, so the PLL is raised at boot. PBCLK1/2/3/5
  land at 84 MHz.
- **Core halts in the idle hook (#513)** — lower power when idle.
- **Batch-pop encoding (#662)** — the encoder handles N samples per wake
  instead of one.
- **Fixed-point CSV voltage column (#819)** — replaces `snprintf` on the hot
  path.
- **Cap re-fits at 252 MHz** — USB CSV transport (#562), USB JSON transport
  fitted from the bench instead of the old ×0.5 guess (#529), and the pure-T1
  single-channel CSV additive cap re-fitted **at voltage precision 4**, the
  precision NQ1 actually ships (#832).
- **FreeRTOS kernel now ships at -O3 (#791)** — a dead per-file override had
  been compiling `FreeRTOS_tasks.c` with **no `-O` flag at all**. Safe because
  `configLIST_VOLATILE volatile` is defined, which is the upstream-blessed
  pattern for this.

---

## Fixes

### Streaming correctness

- **Deterministic per-tick timestamp (#717)** — duplicate `msg_time_stamp`
  values are gone; stamps are now `baseTS + N × periodTicks`.
- **Priming tick suppressed (#707/#745)** — no more partial first frame or
  phantom drop at session start.
- **Runtime defaults are self-consistent and legal (#732)** — a fresh device's
  defaults now pass its own cap and channel-mapping checks.
- **The session owns its encoder header (#870)** — not inherited from the last
  stop.
- **Mid-stream SD-logging stop is reported (#752)** — in `OPER` and in `STATS`.
- **Over-high caps lowered where they caused at-cap silent loss** — pure-T1 PB
  (#714/#715) and USB+SD CSV 1ch (#719/#721).

### SD card

- **Rotation is no longer lossy.** Four fixes compose here: data encoded while
  a rotation opens the next file is kept (#757), the drain is bounded so
  splitting survives high throughput (#822/#823), each split file's header is
  written directly at open instead of through the ring (#824), and a failed
  rotation-drain write is counted instead of reported as success (#838).
- **SD commands are refused immediately while WiFi owns SPI4 (#589)** — with a
  reason, instead of waiting out a 10 s timeout and blaming a large directory.
- **`SD:LISt?` is read-only (#797)** — it no longer creates the directory it
  reads, tells the host where the listing ended (#794), and drops entries whose
  names FAT could not have stored (#795).
- **Disabling the SD card no longer strands streaming on SD (#759).**
- **Teardown survives a concurrent file open (#782) and a racing state store.**
- **A stalled SD close is reported, not swallowed (#783).**

### SCPI robustness and cross-transport safety

USB SCPI (priority 7) and WiFi SCPI (priority 2) run concurrently. A 19-PR
series closes the resulting races by giving configuration changes an explicit
claim, taken **before** parsing:

- Session starts are serialized across transports (#850); a `START` no longer
  publishes the interface or rate until it is accepted (#848); a stop is not
  lost to another transport's pre-arm window (#861); OPER/QUES updates are
  serialized (#852).
- Cap-input setters cannot land on an armed START (#847), the cap is
  re-validated at arm time (#844), and a START whose channel mapping went stale
  is refused (#846/#868).
- `SYST:STR:START 0` performs a complete stop, not three of five steps (#860).
- `CONF:ADC:RANGe` takes the config claim rather than racing START (#873).
- SD-manager claiming is atomic rather than check-then-set (#835), and is taken
  before SD logging is armed (#837/#851).
- `SYST:STOR:SD:BENCHmark` is refused while a stream is running (#854).
- A CI lint gate now requires every `SYSTem:MEMory:*` setter to take the claim
  (#863), hardened twice after it was found asserting things it never
  established (#864/#899).

### SCPI input validation

- **An unrecognised stream format is rejected with `-224` (#801)** — it used to
  silently select JSON.
- **A channel argument is validated before the `uint8_t` cast, at all nine
  sites (#877).**
- **An integer parameter must be a whole decimal token (#880).**
- **A malformed optional argument no longer dispatches the other command form
  (#874).**
- **A not-implemented command no longer explains itself in the data stream
  (#808)** — errors go to `SYST:LOG?`, never into the stream.
- **`scpi_printf` truncation is loud and detectable (#744).**

### Other

- **NQ3 reports `analog_in_res` (#697)** — hosts were scaling AD7609 values 4×
  low.
- **WINC serial-bridge `READ_BLOCK` cursor advances (#755)** — large bridge
  reads terminate.
- **All device text is ASCII, gated in CI (#787).**

---

## Build and tooling

- `Nq1` configuration deleted — `default` **is** the NQ1 build (#774), and the
  libscpi include path was missing from every C32 config, which made both `Nq1`
  and `Nq3` unbuildable (#771).
- `Nq3` now builds at -O3 with the same `tfm.c` override as `default` (#771).
- Release guards: the MPLAB project is checked for a releasable linker-script
  state at PR time (#764), `p32MZ2048EFM144.ld` exclusion is asserted before the
  flip (#767), and a release hex carrying boot-flash records is rejected (#764).
- The SCPI command table is gated against the wiki reference in CI (#805/#809).
- A build failure now guards `SYS_FS_FSTAT.lfname` ever overlapping `FILINFO`
  (#811).

---

## Upgrade notes

- **Flashing does not preserve NVM.** WiFi credentials and calibration must be
  restored after a PICkit flash. The in-app updater path is unchanged.
- **`CONFigure:VOLTage:PRECision` is now rejected while streaming**, as are
  `CONF:ADC:RANGe` and the `SYSTem:MEMory:*` setters. A client that changed
  these mid-session must now stop first. This is deliberate: those are cap
  inputs, and letting them move mid-session is how a session ends up above its
  own admitted rate.
- **Anything merging split SD files must skip the `#`-prefixed header lines of
  continuation files.** Every split file carries its own header — that has been
  true for some time, but the documentation previously said the opposite.

---

## Known issues and follow-ups

Every PR in this release was reviewed pre-tag. Nothing found was release-blocking; these
are the items worth knowing about, all ticketed.

**Coordinate with the desktop app.** The new end-of-listing marker (#794/#796) is filtered
by `Daqifi.Core` **v1.7.0**, but daqifi-desktop still pins **1.4.0**. Until that pin moves,
the desktop SD browser shows a `__END_OF_LIST__` entry
(daqifi-desktop#835).

**SD**
- The stop-time (`UNMOUNT`) drain still discards a failed write uncounted — the surviving
  twin of the rotation fix #838 (#912). Error path only.
- `SYST:STOR:SD:MAXSize` issued mid-session truncates the active log and returns OK
  (#915). Pre-existing; use it before starting a session.
- `SD:LISt? "<dir>"` (operand form) prints paths that `SD:GET` will not resolve — the
  failure is a silent empty transfer (#914). Use `SD:DIRectory` to change directory
  first; the no-operand form is unaffected.

**DIO terminal**
- `SYST:COMM:SPI:TRANsfer?` busy-spins at the SCPI task's priority with no yield (#913).
  Avoid long low-baud SPI or UART transfers while streaming; the cost is visible in
  `QueueDroppedSamples`.

**Streaming**
- `DIO:PORt:ENAble` is not guarded against mid-session changes, so toggling it mid-stream
  changes the CSV column count after the header has been sent.
- The JSON transport caps (#529) were fitted at the precision a device actually boots with
  (**0** — see #910, where the declared default of 4 is never applied). Explicitly raising
  precision to 4+ costs roughly 10% more encoder time, against a fit margin of about the
  same size, so a 10ch/16ch JSON session at cap and raised precision may drop.

**Settings**
- A **downgrade** to firmware older than #306 cannot read CRC32-format settings and will
  fall back to factory defaults. Upgrading is safe: existing MD5-format settings are read
  and migrated on the next save.
