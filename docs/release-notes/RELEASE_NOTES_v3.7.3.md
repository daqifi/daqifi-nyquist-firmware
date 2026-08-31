# DAQiFi Nyquist Firmware v3.7.3

**Release date:** 2026-08-31
**Baseline:** v3.7.2

> **Published as a PRE-RELEASE.** The Windows in-app updater only offers the
> newest **non-prerelease** release carrying a `.hex`, so it will **not** offer
> this build — v3.7.2 stays the version it advertises. That is deliberate: this
> tag is for bench and beta validation. Flash it with a PICkit, or point the
> updater at the attached `.hex` manually. It will be promoted to a full release
> once soaked.

A narrow, single-subsystem point release that restores **SD card file
download**. Every fix here is in the SD / SCPI-storage / USB-reply path; nothing
else in the firmware is touched.

This release is deliberately **not** a snapshot of `main`. It is v3.7.2 plus a
curated set of SD-download fixes, cherry-picked so the release carries none of
the in-progress feature work (the #664 DIO-terminal peripheral family, the
252 MHz clock change, the streaming cap re-fits) that has landed on `main`
since. Those ship later, on their own validation. See "Scope and method" below.

## The headline fix

- **SD:GET silently returns no data after any non-SD stream (#703)** — the SD
  circular buffer in the streaming pool is dual-purpose: it is both the
  SD-logging write buffer *and* the scratch buffer `SYST:STOR:SD:GET` reads
  through. `Streaming_ComputeAutoBuffers` shrank it to a 512-byte stub whenever
  SD logging was not armed at stream start, and nothing restored it on stop.
  Since the read path needs at least 4096 bytes, the read computed a maximum of
  zero and bailed — returning **no data, no `__END_OF_FILE__` marker, and no
  error**. The host simply hung.

  The practical effect: **one USB-only streaming session permanently broke SD
  file download** until the device was power-cycled or an SD-armed stream
  happened to resize the pool back.

  Fixed by raising the floor to the read path's alignment requirement (with a
  `_Static_assert` tying the two constants together so they cannot drift apart
  again), making the read-bail and open-failure paths terminal rather than
  silent, and adding a synchronous pre-check so the failure is reported instead
  of hung on.

  Field diagnostic for the old behaviour: `SYST:MEM:SD:BUFfer?` returning
  **512** meant broken; 4096 or 32768 meant healthy. The log line was
  `[SD] Buffer too small for read`.

## The rest of the SD-download story

These landed as split-outs from the #703 investigation. They are included
because shipping #703 alone would leave the adjacent, already-diagnosed defects
in place.

- **SD:GET / CRC / DELETE clobbered the SD logging filename (#724)** — an SD
  file operation overwrote the shared filename state, so the *next* SD logging
  session truncated the file that had just been downloaded. Silent data loss.
- **SD:BENCHmark clobbered the SD logging filename (#728)** — the same class of
  defect on the benchmark path.
- **SD:BENCHmark failed with `-200` after any non-SD stream (#738)** — the
  circular-buffer drain was not bounded by the runtime write-buffer size, so a
  dormant-partitioned pool could not drain.
- **SD:GET returned zero bytes with a clean error queue (#747)** — the
  directory-qualified path `SD:LISt?` prints did not round-trip back into
  `SD:GET`. The listing's own output is now accepted verbatim.
- **SD:GET degraded to zero bytes after heavy SD activity (#750)** — the USB
  reply chunk is now clamped to 1024 bytes, which sits below the smallest floor
  any partition can produce, and the wait path is terminal rather than
  open-ended. This also closes the constant
  2049-byte short read seen in #703's own reproduction, so the headline fix is
  only complete with this commit present.
- **USB self-deadlock while a SCPI SD reply waited (#780)** — USB writes are now
  pumped while a SCPI SD reply is outstanding, so a large reply cannot deadlock
  against its own transport.
- **SD:LISt? had no stall abort (#754)** — a non-draining peer could hold the
  SD subsystem for the whole directory traversal. The listing path now honours
  the transfer-abort flag, as `SD:GET` already did.
- **Silent failure mid-transfer (#725)** — a `SYST:STOR:SD:GET` that fails part
  way through now emits `__TRANSFER_ERROR__` instead of going quiet, so a host
  can distinguish a failed transfer from a short file. **Client note:** hosts
  that scan the reply stream should treat `__TRANSFER_ERROR__` as a terminal
  sentinel alongside `__END_OF_FILE__`. The sentinel covers **read-side**
  failures; a peer-stall abort that has already sent partial data still ends
  with no terminator at all, so a host timeout remains necessary. That case is
  unchanged from v3.7.2 and behaves identically on `main`.

## Streaming caps lowered where they were silently dropping data

Three enforced maximum streaming rates were **too high** — the device accepted
them and then lost data at its own stated limit, with no error. They are now
re-fitted below their measured zero-loss ceilings.

| interface / format | was | now | measured true ceiling |
|---|--:|--:|--|
| USB ProtoBuffer, 1×T1 | 19,340 | **15,799** | 16,900 |
| USB ProtoBuffer, 3×T1 | 17,564 | **14,263** | 15,350 |
| USB ProtoBuffer, 5×T1 | 16,087 | **12,998** | 14,075 |
| SD ProtoBuffer, 1×T1 | 9,852 | **7,900** | 8,600 |
| USB+SD CSV, 1 channel | 8,000 | **6,500** | — |

The SD case was the most concrete: at the old 9,852 cap a 100-second run
silently discarded **2.57 MB**. The USB pure-T1 curve was ~14 % above its real
ceiling across the board.

**These changes only ever lower a cap.** No configuration gains headroom, and
nothing that streamed cleanly before will be refused now — the rates being
removed are ones the device could not actually sustain without loss.

**Client note:** since #524 an over-cap `SYST:STR:START` is a hard `-222`
rejection rather than a silent clamp. A client that hard-codes or caches a
maximum rate in the ranges above will now see `-222` where it previously got an
accepted-but-lossy session. Re-read `CONF:CAP:JSON?` (`current_max_rate_hz`)
rather than assuming the old numbers.

Only the ProtoBuffer pure-Type-1 and USB+SD CSV single-channel terms move; every
other cap, including all armed-scan (Type-2 / OBDiag) cells, is unchanged and
was already at-cap validated.

## Also included

- **ADC channel-index off-by-one (#720)** — an off-by-one in the `SCPIADC.c`
  channel index bound checks. Small, self-contained, and in the same
  input-validation family as the v3.7.2 sweep.

## Release-tooling hardening (no firmware change)

These commits touch only `tools/release/` and CI. They cannot affect the shipped
binary, and they enforce — rather than merely document — the layout rules that a
release hex must satisfy.

- Assert the standalone linker script is excluded before the bootloader flip
  (#767).
- PR-time guard that the MPLAB project is in a releasable linker-script state
  (#765).
- Reject a release hex carrying boot-flash records (#764) — a standalone-linked
  hex silently loses those records through the customer updater, which is
  exactly how bench and field builds diverged historically.

## Scope and method

Cherry-picked onto `v3.7.2` rather than cut from `main`. This was forced, not
stylistic: the shortest linear prefix of `main` containing the #703 fix also
contains the in-progress edge-events work and the whole new DIO-terminal
peripheral family, none of which has completed hardware validation.

Verification performed on the release branch:

- All fifteen commits apply onto v3.7.2 with a single trivial conflict (one
  enumerator in `Logger.h`, an adjacency artifact only).
- Clean build at **-O3** with **-Werror**.
- Change footprint is **10 firmware files**, all in the SD / SCPI / USB paths.
  No project-configuration changes and no new source files.
- No commit merged to `main` after these fifteen corrects any of them — verified
  both by issue/PR cross-reference and by checking every symbol they introduce
  for later modification.

## Upgrading

No configuration migration. No SCPI command was removed or renamed; the only
protocol-visible change is the new `__TRANSFER_ERROR__` sentinel on a failed
`SD:GET`, which previously produced silence.

**Settings are reset by this update.** Flashing via PICkit erases NVM, as
always — but so does the in-app updater. Verified end to end on hardware
(2026-08-30): a device on v3.7.2 with `CONFigure:VOLTage:PRECision` saved as 7
reported 0 after a successful in-app update to v3.7.3, and the reported firmware
revision advanced correctly to 3.7.3 precisely because settings reload from
factory defaults. Expect to re-enter WiFi credentials and re-apply any saved
calibration or precision setting after updating, by either route.

This is not new in v3.7.3 — it is how the update path already behaves — but an
earlier draft of these notes claimed the in-app path preserved NVM, which the
hardware test disproved.
