# SD card subsystem

> Split out of `CLAUDE.md` on 2026-09-10. That file is loaded into **every turn of
> every agent**, and at ~38k tokens it was ~13% of all token spend; this material is
> reference — needed when you work in this area, not on every task. **It is
> unchanged, not summarised.** Read it in full before changing anything it
> describes, and update it here rather than re-adding it to `CLAUDE.md`.

SPI arbitration against WiFi, automatic file splitting and its rotation semantics, and sector-aligned writes.

---

### SD is unavailable while streaming over WiFi (#589)

**SPI4 is time-multiplexed between the SD card and the WINC WiFi module, and
the arbitration is at TASK level, not per-transfer.** For the whole duration of
a WiFi streaming session `app_SDCardTask` parks in `APP_SD_STATE_SUSPENDED`
(`app_freertos.c:494-527`) and pumps **neither** `DRV_SDSPI_Tasks()` **nor**
`sd_card_manager_ProcessState()`. An SD operation armed in that window can
never be advanced to completion.

The predicate is `app_SDCard_SpiOwnedByWifi()` (`app_freertos.c:419-430`):
`IsEnabled && ActiveInterface == StreamingInterface_WiFi`, OR a WiFi
firmware update, OR the #589 jam quarantine. Note it is **streaming**, not
"WiFi is up" — SD:GET and SD:LISt over a WiFi TCP *control session* work
normally (`test_598_sd_get_over_wifi` covers exactly that).

**What a client sees.** Every SD command is refused with `-200` and
`SYST:DIAG:SPIBus?` reports `,SUSPENDED`; `SYST:LOG?` names the owner
(streaming / FW update / quarantine) and the remedy. It is a **gate, not a
fault** — it clears by itself on `SYST:STReam:STOP` with no re-enable and no
remount. `SYST:STOR:SD:ENAble` is deliberately NOT gated: it is the manual
escape hatch and the way a `QUARANTINED` state is cleared.

**Before the fix**, each command instead armed an operation nothing would pump,
waited out its full `WaitForCompletion` timeout (**10.38 s measured** for
`SD:SPACe?`), and returned `-200` with a hint blaming *"directory too large
(#689: clear the card)"* — which is wrong and reproduces on a freshly
formatted card. The first such command also left the state machine parked at
`DEINIT`, so every later one failed instantly while reporting a state that
looked wedged. **If you are debugging an older image and see that pattern,
this is what it is — do not chase #689.**

**If you add a new SD SCPI command**, it must (a) call `SD_RefuseIfSuspended()`
before arming and (b) check `sd_card_manager_UpdateSettings()`'s return via
`SD_ArmOrRefuse()`. The guard closes the common case; the return check is what
makes a lost race visible instead of a false success — `FORmat`, `CRC` and
`GET` all return OK without waiting, so an unchecked arm reports success having
queued nothing.

### Abandoned writes and CRC (#1217)

An abandoned CMD24 or CMD25 leaves the card waiting for data, and it stores whatever is clocked next as that sector. `#1217` found FAT sectors full of SPI commands and WINC traffic this way. What makes the driver abandon a write in the field is still unknown, but the damage needs nothing else: on the bench SanDisk SP32G in the default mode, one CMD24 abandoned after a good R1 (a bench-only injection) left the sector holding the mount's CMD17 retries, the CMD10 detect poll and CMD0, the same pattern as the reported sectors without WiFi involved (2026-11-09).

**Protection is opt-in: `SYST:STOR:SD:PROTect 1`.** The default, `0`, runs as before #1217: CRC off and CMD25 multi-block writes. Protected mode costs about two thirds of the SD write throughput, and its per-sector bus lock starves a WiFi control session during SD logging (#1234), so it is not the default. The setting changes only while SD is disabled (`-221` otherwise), takes effect by re-initializing the card, and resets to `0` at boot.

**No mount may start while a change is pending**, or the driver's re-init lands under the mounted volume. The driver drops the card at its first detect poll with no I/O in progress or queued (`DRV_SDSPI_CrcModePending` is true until then), and three SCPI handlers keep the rest:

- `SD:PROTect` takes the manager claim and, in one critical section, checks `enable` and makes the request. It then waits (2 s cap) until the driver has dropped the card, or returns `-200` if the SD task is not running the driver (standby, WiFi streaming).
- `SD:ENAble 1` sets `enable` only inside a critical section and only with no change pending. If one is pending, it waits up to 2 s and then returns `-200`. This keeps the two commands safe when they arrive on different transports (USB and TCP).
- `SD:SPACe?`, the one mount that runs with SD disabled, refuses with `-200` if a change is pending when it holds the claim.

`tests/host/check_1217_mount_gate.py` pins all three in the real source. `SYST:STOR:SD:INFO?` reports whether the card is running with CRC on and how many resyncs have fired since boot, as its last two fields.

In both modes the driver resyncs and re-sends a write in doubt. With `PROTect 1` it also turns CRC on. All of this is in `driver/sdspi/src/`:

- **CRC is on (`PROTect 1`).** Media init sends CMD59 with argument 1, every command frame carries a computed CRC7, and every data block a computed CRC16 (`drv_sdspi_crc.h`). A captured block fails its check and is not programmed. Every block is written as its own CMD24: a CRC-rejected block inside a CMD25 left the bench card unresponsive until power-cycled.
- **A write in doubt is resynced before any other command (both modes).** `writeInDoubt` is set before a CMD24's or CMD25's command frame is sent and cleared only once the card is provably out of the data phase. While it is set, `lDRV_SDSPI_CommandSend` runs `lDRV_SDSPI_WriteResyncTasks` instead of sending anything. It survives aborts and `DRV_SDSPI_ReleaseBus`. For a CMD24 the resync clocks a `0xFE` token, 512 `0xFF` bytes and a `0xFFFF` CRC. With CRC on, a waiting card rejects that block. With CRC off it programs it, so the sector holds `0xFF` instead of bus traffic, and the re-send below rewrites it. For a CMD25 (CRC off only) it clocks 514 `0xFF` bytes with no `0xFE`, then the stop-tran token `0xFD`: a card waiting for a token ignores the `0xFF` bytes and stops, and one part-way through a block finishes it into a sector the re-send rewrites. A `0xFE` is left out because a card that took it as a token would program the filler past the request. The function's comment covers what each card state does with the bytes.
- **The write in doubt is re-sent (both modes).** `DRV_SDSPI_TASK_READ_WRITE_ABORT` sends the same CMD24 again from the request it still holds, up to `DRV_SDSPI_WRITE_IN_DOUBT_RETRIES` (2) times per request. The resync runs first, so the sector ends with the intended data whatever state the card was in. A CRC-rejected block is re-sent the same way. A resync that fails spends the rest of the request's budget, so a broken bus is reported after one attempt instead of holding the SPI lock through three. CMD24s with CRC on resume at the first unconfirmed block, since each CMD24's busy ending confirms its block. A CMD25 is re-sent **whole**, from the request's first block (`cmd25BlocksDone` rewinds `blockStart`, `buffer` and `nBlocks`): a block's busy ending inside a CMD25 does not prove it was programmed, because a card reports a programming failure on a later block's token and only ACMD22 says how many blocks landed (SD Physical Layer Simplified Specification v6.00, 7.2.4). Resuming after the last busy could complete a request with an earlier sector never written (codex audit of the follow-up PR). That includes a CMD25 that lost only its stop token: reporting that as an error cost data on the bench, where the #1031 path split the file, and in the unmount flush the file lost its last 64 KB although every block was on the card (2026-11-10).
- **Every CMD25 resync ends with CMD12.** A card that rejected a block of a CMD25 (CRC-error or write-error data-response token) ignores data and tokens, the stop-tran token included, until CMD12 (spec 7.3.3.1), and the driver may have lost the very token that said so. So after the stop-tran token and eight idle bytes (enough to complete the CMD63 frame `0xFD` starts in a card that was not in a write), the resync sends CMD12 and waits out its busy. A card in any other state is out of the write by then and answers CMD12 with an illegal-command R1 that nobody reads. A reject is logged with cause `reject`, and the request is re-sent whole or reported failed like any other abandon.
- **`DRV_SDSPI_ReleaseBus` ends the request in flight.** It is called on the SD task's graceful-shutdown timeout and by the #925 leak unwind. For a write this cannot happen in the shipped firmware: FatFs's `disk_write` and the media manager drive every write to completion synchronously on the SD task, and every `ReleaseBus` caller runs on that task between those calls. The one asynchronous request is the media manager's sector-0 read at mount. It logs `SD: bus released with a request in flight` if that ever changes. A request it interrupts completes with an error on resume and sends nothing more: it no longer owns the card, and detection restarts under it. A write in doubt is then left to the resync and is not re-sent. Found by the adversarial audit on PR #1237: continuing the request could free it mid-retry (a NULL dereference), or leave its CMD24 frame to go out later under the detect FSM's CMD0.

In both modes, **CS goes high on every abort**, before the exclusive lock is released, and in the completion handler before the aborted-transfer early return.

**What you will see:**

- `SD: card rejected CRC enable (CMD59); not mounted, cannot protect it (#1217)` (`PROTect 1` only): the card refused CRC and is deliberately not mounted, so SD commands report no usable card. `PROTect 0` mounts it. Logged once until `SYST:LOG?` or `SYST:LOG:CLEar` re-arms it.
- `SD: write in doubt, re-sending blk=N n=B (try T, cause=C)`: a write was abandoned or rejected and is being sent again. `blk` is the address the re-sent command carries (the LBA on an SDHC card, a byte address on a standard-capacity one) and `n` the blocks still to write (for a CMD25, always the whole request). `cause` is what abandoned the attempt: `xfer` (DRV_SPI reported a transfer error), `xfer-timeout` (the #567 watchdog: a completion never came), `spi-add` (DRV_SPI refused to queue a transfer), `timer`, `r1` (the card answered the write command with an error), `cmd` (the command did not complete: no R1, a bus error, or a failed resync), `token` (no valid data-response token came back), `reject` (the card rejected the block: a CRC error or write error token), `busy` (busy past 250 ms) or `release` (`DRV_SDSPI_ReleaseBus`).
- `SD: write failed blk=N n=B tries=T cause=C`: every try failed and the write is reported failed. One line per failed request, naming the last attempt's cause. This and the line above are how a field log says what abandons writes.
- `SD: bus released with a request in flight (state=S)`: see `DRV_SDSPI_ReleaseBus` above. For a write it means the reasoning there no longer holds.
- `SD: write in doubt resynced (type=1 token=0x..)`: the bus was resynced after a write in doubt. With CRC on, a token with low bits `0x0B` proves the card really was waiting. With CRC off, low bits `0x05` (accepted) mean it was waiting and programmed the `0xFF` block. `0xFF` means it was not waiting. Type 1 is CMD24, type 2 CMD25.
- `SD: write in doubt resync failed; retrying (#1217)`: the attempt failed and the command in hand failed with it. The flag stays set, so the next command tries again. Commands, CMD0 included, are held back until one succeeds, so a card stuck busy reads as "no card" until it lets go or is power cycled. Logged once until `SYST:LOG?` or `SYST:LOG:CLEar` re-arms it.

**A rejected block is re-sent, then an error (`PROTect 1`).** A bit error on the bus used to store a bad sector silently. Now the block is re-sent up to twice, and only if every try fails does the write fail, taking the normal failed-write path (#1031).

**Read data CRC is still not checked.** The card sends it and the driver discards it.

**Host coverage:** `tests/host/test_1217_abandoned_write.c` runs the real driver sources against a modelled card.

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
- Split files: Sequential numbering, **not** zero-padded (e.g., `experiment-1.csv`, `experiment-2.csv`) — `generateFilename` formats `"%s/%s-%u%s"` (`sd_card_manager.c`)
- Split index ceiling: 9999 (`SD_CARD_MANAGER_MAX_SPLIT_FILES`). In practice
  the directory-bucket ceiling below is reached first.

**Bucket subdirectories (#689):** ⚠️ **a long session's continuations are not
all in the configured directory.** FatFs file-create is O(directory size), so a
session that piled hundreds of parts into one directory eventually took longer
than the SD op timeout to create the next one and wedged the manager. The
manager therefore caps each directory at 64 files
(`SD_CARD_MANAGER_MAX_DIR_FILES`, `sd_card_manager.c`):

- **Bucket 0 is the configured directory itself.** A session that never fills
  it has exactly the pre-#689 layout, with no new paths.
- Once that directory holds 64 files, new files go into the subdirectories
  `P001`, `P002`, … `P064` under it (`sd_BuildBucketPath`, `"%s/P%03u"`). The
  names are 8.3-clean on purpose: one directory entry each, so the parent
  stays small too.
- **The count includes files already on the card**, not just this session's.
  On a card that already holds 64 files in the configured directory, the very
  first continuation lands in `P001`.
- The ceiling is bucket 0 plus `P001`–`P064`, 65 × 64 = 4,160 files. Past it,
  the SD manager stops logging cleanly instead of wedging
  (`SD_REFUSE_BUCKETS_EXHAUSTED`). `SYST:STReam:START` is then refused, and
  `SYST:LOG?` says *"every directory bucket is full - use a different directory
  or clear the card"*.

**Downloading a bucketed file with `SYST:STOR:SD:GET`:** the operand is
resolved as `<configured directory>/<operand>`, so a file inside a bucket needs
its **bucket-relative path**: `SYST:STOR:SD:GET "P001/experiment-70.csv"`. The
fully-qualified form the device prints (`"<directory>/P001/experiment-70.csv"`)
also works, because one leading copy of the configured directory is stripped
(`SD_StripConfiguredDir`, `SCPIStorageSD.c`). The **bare name**
(`"experiment-70.csv"`) does **not** search the buckets. It opens
`<directory>/experiment-70.csv`, which does not exist.

**What a failed SD:GET looks like.** A missing or unopenable file is not a
hang and not a silent success, but the stream alone does not tell you it
failed:

- The reply is **zero bytes followed by the normal `__END_OF_FILE__`
  terminator** (#703). Python clients wait for exactly that marker, so a
  different terminator here would turn a wrong answer into a hang.
- The failure is reported as a **deferred SCPI error (#914)**: `-256 "File
  name not found"` for a missing file or directory, `-257` for an invalid name,
  `-250` for unusable storage (`SdAsyncErrorForFsError`). It is latched at the
  open and pushed to the error queue at the next command boundary (`SCPI_DrainDeferredSdError`, `SCPIInterface.c`). It is
  **queue-only**: it is never written into the stream, because writing it there
  corrupted the *next* download's leading bytes.
- **So read `SYST:ERR?` after every SD:GET that returned zero bytes.** An empty
  file and a missing file produce the same bytes on the wire; only the error
  queue tells them apart. A tool that reports "0 bytes, no error" without
  having read `SYST:ERR?` has not established that the file was empty.

**CSV Headers:** ⚠️ **every split file carries its own header, including
continuations.** The bullet here used to say "Split files: Data rows only (no
headers) - simplifies merging". That is wrong and has been for some time —
measured 2026-08-21 by downloading rotated files from the bench: `s2.csv`,
`s2-1.csv` and `s2-2.csv` each begin `# Device: Nyquist 1` followed by ~1,279
data rows.

The behaviour is deliberate on the firmware side, though **which task writes
the header changed in #824**. It is now rendered once per session by the
streaming task (`Streaming_BuildSdFileHeader`, into a 512 B static) and written
by the **SD task** straight into each newly opened file with `SYS_FS_FileWrite`
(`Streaming_GetSdFileHeader`, called from `OPEN_FILE`). Before #824 it was
pushed through the SD circular buffer whenever a `gSdFileWasReady` latch went
false→true, which is what forced the rotation to reset that buffer and made
rotation lossy — see the ⚠️ under Safety Features. Anything merging split files
must SKIP the `#`-prefixed lines of continuations rather than assuming they are
absent.

- First file: full CSV header (device info, column names with "ain" prefix),
  written **inline by the encoder** — the file is open before the encoder runs,
  so the SD task has nothing to write yet, which is also why a **protobuf**
  session's first file carries no `sd_metadata` (files 2..N do; pre-existing
  #196 gap)
- Split files: the same header, written at file open, then data rows

**Safety Features:**
- Default: 3.9GB limit (100MB below FAT32 maximum)
- Minimum: 1000 bytes (prevents rapid rotation)
- Circular buffer draining before rotation, AND the buffer keeps accepting
  encoder output across the file-open window (#757). The drain covers what was
  already buffered; the second half covers what arrives while the next file is
  being created. Before #757 only the drain existed, so every rotation
  discarded the packets encoded during the open — measured ~14,000 bytes per
  25 s at 2 kHz with `MAXSize 20000`, growing with directory occupancy because
  the FatFs create is O(N) in it. The encoder gate is
  `sd_card_manager_IsBufferAccepting()`; note that transport health
  deliberately still uses `IsWriteReady()`, or a stuck open would read as
  healthy.
- ⚠️ **Rotation used to discard ~3.6 KB per split; #824 removed that.** The
  pre-close drain is still bounded to a *snapshot* of the circular buffer
  (#822/#823) — draining the live level livelocks against the producer above
  ~340 KB/s, which stopped rotation outright (11ch @2 kHz produced one 8.7 MB
  file against a 20 KB `MAXSize`) — so bytes the encoder appends *during* that
  drain are still buffered when the old file closes. What changed is where they
  can go. They used to be unsaveable: not the old file (that is the livelock),
  and not the new one either, because the next header was about to travel
  through the same ring and they would have landed ahead of it. So the rotation
  reset the buffer and `Streaming_ReportSdDiscard()` counted the loss into
  `SdDroppedBytes` — measured 277,248 bytes across 76 rotations at 16ch CSV
  @2 kHz with `MAXSize=20000`. #824 writes the header directly into the new
  file at open instead, so byte 0 is guaranteed by call order, the reset is
  gone, and those bytes simply become the new file's first data rows. Rotation
  now contributes nothing to `SdDroppedBytes`; what remains there is genuine
  buffer-full loss. (`test_824_rotation_no_discard.py` measures this as an A/B
  — same config with and without rotation — because the counter aggregates both
  and a bare `== 0` would fail at any saturating shape.)
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
