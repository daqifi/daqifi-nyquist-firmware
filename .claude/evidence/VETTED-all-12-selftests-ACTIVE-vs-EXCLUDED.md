# All 12 unregistered self-tests VETTED: **6 ACTIVE-eligible, 7 EXCLUDED** — and 207 device-free checks run nowhere today

Completes the authoritative count. Mechanical verification; **no position added to any row.**
Produced 2026-10-07.

**Rig** (the manifest's own documented standard): `git archive` of each PR head into a directory with
**no `daqifi-python-core` sibling**, run `python3 -B -S <file> --self-test < /dev/null`, 70 s timeout,
Linux python **3.12.3**. CI is python 3.12 on ubuntu-24.04 with **no `pip install` anywhere in the
workflow** — read, not assumed, which is why this rig *matches* CI rather than resembling it.

**Controls, both directions, on every one of the 10 trees** — applying my own lesson that a control
must come from the **same tree** as the target:

| control | role | result |
|---|---|---|
| `test_795_listing_name_sanity.py` (ACTIVE on main) | positive | **exit 0 on 10/10 trees** ✅ |
| `test_921_cap_terms.py` (EXCLUDED on main, import cause) | negative | **exit 1 on 10/10 trees** ✅ |

## ✅ ACTIVE-ELIGIBLE — 6 files, and their self-tests are NON-VACUOUS

| row | file | evidence |
|---|---|---|
| **main** | `test_658_spi_diag_guard.py` | exit 0 — *"7/7 device-free checks passed"* |
| ts#312 | `test_938_finder_stop_race.py` | exit 0 — *"76 checks OK"* |
| ts#374 | `test_1071_reboot_power_state.py` | exit 0 — *"26/26 checks passed"* |
| ts#384 | `test_920_json_precision4_cap.py` | exit 0 — *"PASS (0 of 33 checks failed)"* |
| ts#411 | `test_1069_dac7718_power_gate.py` | exit 0 — *"52 checks, 0 failed"* |
| ts#454 | `test_1144_capjson_calibration_bounded.py` | exit 0 — *"20/20 device-free checks passed"* |

**I checked non-vacuity explicitly**, because exit 0 with zero checks would be the
runtime-SKIP-scored-as-PASS shape: every one reports a real check count, and none attempts a device
connection in its self-test path.

> ⭐ **That is 207 device-free checks (76+26+33+52+20) that run NOWHERE today**, plus `test_658`'s 7.
> Exactly the `#409` failure mode — *"a regression in the logic they guard could pass every PR and
> every release gate silently."*

### ⚠ Two outputs are easy to misread, and a grep-based reader would flag them

- **ts#374** prints `FAIL teardown: SKIPPED every restore — could not confirm the live connection is
  the right board … (idn='STILL,WRONG')`. **Synthetic fixture**: it feeds a deliberately wrong IDN to
  prove the teardown refuses to write to an unverified board. 10 PASS/FAIL lines, 2 of them FAIL,
  summary **26/26 passed**, exit 0.
- **ts#312** prints `ABORT (part 3) … none conclusive; last: skip` twice — the inconclusive path under
  test. Summary **76 checks OK**, exit 0.

**Both are expected output of cases that exercise the failure path.** A self-test that exits 0 while
printing the literal strings `FAIL` and `ABORT` would be flagged by any log-scanning gate — same
substring/homonym class as the rest of today's findings. Worth knowing before someone reads a green
CI log and files a bug.

## ⛔ EXCLUDED — 7 files, two distinct causes

| row | file | cause | line |
|---|---|---|---|
| ts#287 | `test_889_ad7609_cal.py` | `ModuleNotFoundError: 'daqifi'` | 84 |
| ts#306 | `test_164_json_encoder_no_loss.py` | `'daqifi'` | 382 |
| ts#306 | `test_164_json_oversized_sample_stall.py` | `'daqifi'` | 227 |
| ts#400 | `test_1086_ain_range_torn_read.py` | `'daqifi'` | 1657 |
| ts#429 | `test_267_channel_timing_offsets.py` | `'daqifi'` | 109 |
| ts#342 | `test_1002_dac_usecal_stubs.py` | **`ModuleNotFoundError: 'serial'`** | 129 |
| ts#408 | `test_1003_compound_error_termination.py` | **`'serial'`** | 198 |

⭐ **Two causes, and both already have house precedent on main** — so the exclusion reasons can be
written in the established phrasing rather than invented:

- **`daqifi` (5 files)** — unconditional module-scope `from daqifi import NyquistDevice` behind a
  parent-sibling `sys.path.insert`. Main's wording for `test_728`: *"only resolves on a bench whose
  checkout happens to sit one directory level below a real daqifi-python-core sibling — true of this
  lane's worktree layout by coincidence, false of `actions/checkout@v4`'s bare checkout."*
- **`serial` / pyserial (2 files)** — unconditional module-scope `import serial`. Main's wording for
  `test_907`: *"This CI job installs no third-party packages at all (no requirements.txt step), so
  this fails even though it needs no daqifi-python-core sibling."*

## What this does to option (C)

Option (C) was *"one main-side commit pre-registering the 12 as EXCLUDED."* The vetting makes it
**better than that**: **6 ACTIVE + 7 EXCLUDED**, which not only clears 11 rows' red checks without
touching a branch but **also wires 207 currently-unrun device-free checks into CI**. Registering all
12 as EXCLUDED would have cleared the checks and left those 207 unrun — a green gate over logic
nothing executes, which is the defect `#409` exists to prevent.

**I am not ranking the paths or recommending (C)** — I assess none of these rows. This records what
each file is, with evidence, so whichever path is chosen has the split it needs.

## Bounds

- **n=1 per file.** Exit codes are deterministic for an import failure; the 6 passes are single runs.
- **Vetted at each PR's CURRENT head.** A head move re-opens the question for that row — the same
  staleness that created this situation.
- **`-S` excludes site-packages**, which is stricter than CI (CI has a bare ubuntu python with no
  `pip install`, so no third-party either). A file that needs *only* stdlib passes both;
  **`-S` cannot produce a false PASS**, only a false FAIL, and none of the 6 failed.
- **I did not read the 6 passes' self-test bodies** for whether their assertions are meaningful — only
  that they run device-free and report a non-zero check count. **Vetting is "safe to run in CI", not
  "the checks are good."** The manifest header asks for the former.
