# SD card subsystem

> Split out of `CLAUDE.md` on 2026-09-10. That file is loaded into **every turn of
> every agent**, and at 38k tokens it was ~13% of all token spend; this material is
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
- Supports up to 9999 files per session (~39TB @ 3.9GB each)

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
