# DAQiFi Nyquist Firmware v3.9.1

**Release date:** not yet released
**Status:** Candidate. Not tagged or published; tagging and publishing are the operator's.
**Baseline:** v3.9.0. Four commits on `main` since `v3.9.0`, besides this bump: three fixes and one documentation change.

3.9.1 is a **bug-fix release**. It has no new features and no SCPI command changes. It fixes two SD driver defects behind field write abandons, and makes `SYSTem:LOG?` keep a record it could not send.

---

## Fixes

### SD: a completed write was abandoned about 12 minutes into logging (#1217, PR #1252)
- **Cause.** The SD driver kept transfer handles in its aborted-transfer ring after those transfers had completed. DRV_SPI reuses handles, so a later transfer could get a handle still in the ring, and the driver then discarded that transfer's real completion. The 500 ms watchdog then abandoned a write that had in fact finished.
- **When it happened.** The ring was filled by every trip to STANDBY. Under 8 channels at 1 kHz SD logging, a handle comes back every 759 s of transfers. So a device that had been through STANDBY a few times abandoned a write roughly 12–13 minutes into a session, and the fault re-armed itself one reuse period later.
- **Fix.** The ring now holds only transfers that are still pending. A record is taken only while its transfer is pending, inside a critical section that masks the DMA completion. Stale entries are pruned before every submit, so a reused handle can never match.
- **Bench, before vs after (from the PR).** Main `f84151f8` abandoned 2 writes at t = 696 s, with a 506 ms worst-case write. This fix ran CLEAN: 0 abandons, worst case 42 ms, over the same 20-minute 8ch@1kHz soak with WiFi traffic.
- PR #1254 documents when the ring records a handle. It is documentation only.

### SD: chip select is held for each whole SD request (#1255, PR #1257)
- **Cause.** DRV_SPI raised the SD chip select after **every** transfer, so within one block write CS went high between the command, the data, the CRC and the data-response token. A WiFi (WINC) transfer on the shared SPI bus could run in those gaps. SPI-mode SD requires CS held for the whole command, response and data (SanDisk SD Card Product Manual v2.2, §3.3). The driver also never sent the one clock with CS high that the card needs before it releases MISO (§5.23.4).
- **Fix.** The SD driver holds CS for each whole request and sends one byte with CS high when it releases the bus. DRV_SPI gains a client CS hold, a no-CS transfer, and an exclusive-use admission check made under the queue lock.
- **Effect on WiFi.** It markedly reduces how often a WiFi TCP client is dropped while SD logs. From the PR, over a 12-minute 8ch@1kHz soak with a client polling every 0.5 s:
  - main: 11–14 drops;
  - with the hold: 0–1 drops in 6 of 7 runs.
- **What it does not fix.** It does **not** fix #1234 (WiFi TCP session stalls during SD logging); a first drop can still come within minutes. See Known issues.

### Logging: `SYSTem:LOG?` keeps a record it could not send (#1248, PR #1256)
- **Cause.** The dump removed each record from the log ring **before** writing it, and ignored how many bytes the transport took. Against a host that had stopped reading, `SYSTem:LOG?` dropped records and still reported success.
- **Fix.**
  - The dump now writes first, and removes a record only after it was written in full and is still the oldest.
  - On a short write the dump stops, keeps that record and everything after it, and the command returns **`-200 Execution error`**. The next `SYSTem:LOG?` returns the rest.
  - One-shot log messages are re-armed only after a complete dump.
- **Client-visible change.** `SYSTem:LOG?` can now return `-200`. A client that reads the error queue after a dump will see it when the dump was cut short; before 3.9.1 that case was silent loss.

---

## Validation

**Hardware: companion set, NOT the full release gate.**
- **Image under test:** the 3.9.1 **standalone** build, flashed by PICkit to one NQ1 bench board.
- **Image check:** the board's `CONF:CAP:JSON?` read back `firmware_crc32` **`F3CE3661`** and `firmware_rev` **`3.9.1`**. `tools/hexcrc.py` gives the same CRC for the built hex.
- **Test suite:** `daqifi-python-test-suite` at `74a748ea`, run on 2026-11-13. Each script was run directly with Windows-native Python. `release_gate.py` excludes the two soaks, so `--only` would not run them.

| test | covers | result |
|---|---|---|
| `test_1248_log_dump_short_write.py` | #1248 (its log dump also shows #1231's once-per-config line) | PASS: 0 late + 32 kept of 32, `-200` reported |
| `test_1217_sd_crc_on.py` | #1217 protected/fast SD modes | 8/8 PASS |
| `test_1217_stale_abort_ring.py` (20-min soak, 8 STANDBY seeds) | #1217 / PR #1252 | 5/5 PASS, CLEAN: 0 abandons, 48 994 SD writes, 0 errors, max write latency 146 ms |
| `test_1255_sd_cs_tcp_drops.py` (12-min soak, WiFi TCP client) | #1255 / PR #1257 | 6/6 PASS, CLEAN: 0 TCP drops, 0 SD write errors, max write latency 141 ms |

- **NVM:** every flash wipes NVM. It was snapshotted before the flash, then WiFi and voltage precision were restored, verified across `SYSTem:REboot`, and checked unchanged after the run.
- **Not run:** the full `release_gate.py` gate. 3.9.1's changes are confined to the SD driver, DRV_SPI and the logger, and the companion set covers exactly those paths.

**Release image.** The bootloader-linked hex was built with `tools/release/cut_release.sh --version 3.9.1`. Its layout checks passed:
- `kseg0_program_mem` origin `0x9d000480`;
- 408 bytes at `[0x1D000000, 0x1D000480)`;
- the bulk of the image from `0x1D000480`;
- nothing in boot flash or the upper flash panel;
- 76% of the lower panel used.

---

## Known issues and caveats

- **#1234, the WiFi TCP session stalls while SD logs, is still open.** #1255's chip-select hold reduces how often a client is dropped, but drops still occur.
- **#881, the WiFi datapath wedge in long soaks, is not addressed.**
- **The #1248 log ring:**
  - Overflow is still silent: on a full 64-entry ring, new entries from other tasks evict the oldest, including a record a short dump kept.
  - A one-shot message claimed by the dumping task during a dump is dropped, and stays claimed until the next complete dump.
- **#1255's own symptom** (an abandon at the data-response token) occurs about once per 450 minutes, so 12-minute runs cannot show whether it changed.
- **Unchanged since 3.9.0:** the "Sample queue resize skipped" line on the first stream after boot is #1231's once-per-config report, by design. A `diag368-fast` debug line still ships in the log.
- Everything in 3.9.0's Known issues still applies.
