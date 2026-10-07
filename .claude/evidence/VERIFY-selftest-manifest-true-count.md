# Authoritative count: **11 rows / 12 files**, not 21/24 — and the gap is NOT mainly grep over-reporting

Mechanical verification of a peer's claim. **No position added to any row.** Produced 2026-10-07.

**Oracle:** `harness_policy_lint.py` at **`origin/main` = `bd4c5b3701a86170bf20dc325220fd1972320b64`**,
imported and called directly — `has_self_test_flag()` and `_parse_selftest_excluded()`, the lint's own
functions, never a reimplementation. **No lint was run inside any branch** (a branch-local run uses an
older lint in which this check does not exist and returns a false green).

## The answer

| | rows | files |
|---|---|---|
| conv-ts's screen (grep-based, flagged as an upper bound) | 21 | 24 |
| **authoritative (oracle + both manifest forms, per branch)** | **11** | **12** |
| plus a **pre-existing** violation on `main` itself, no PR's fault | — | **1** |

**The 12 true violations** — declares `--self-test` by the oracle, registered in **neither** form:

```
ts#287  test_889_ad7609_cal.py                   ts#400  test_1086_ain_range_torn_read.py
ts#306  test_164_json_encoder_no_loss.py         ts#408  test_1003_compound_error_termination.py
ts#306  test_164_json_oversized_sample_stall.py  ts#411  test_1069_dac7718_power_gate.py
ts#312  test_938_finder_stop_race.py             ts#429  test_267_channel_timing_offsets.py
ts#342  test_1002_dac_usecal_stubs.py            ts#454  test_1144_capjson_calibration_bounded.py
ts#374  test_1071_reboot_power_state.py
ts#384  test_920_json_precision4_cap.py
```

**Pre-existing on `main`:** `test_658_spi_diag_guard.py` — the only one of main's **151** root test
scripts that declares `--self-test` and is in neither form. This is the `test_658` already known to
fail `harness-policy` on unrelated rows.

## ⭐ THE DECISIVE EXTERNAL CONTROL — CI's own arithmetic agrees

ts#306's live `harness-policy` run reports:

> *"self-tests: **18 listed** for CI, **3 unmentioned**, 0 dead, 0 mismarked, 0 excluded entr(ies) no
> longer resolving…, 0 active/excluded conflict(s), 0 malformed exclusion(s)"*

- **18 listed** — matches my independent count of main's ACTIVE entries: **18** ✅
- **3 unmentioned** — I derive **1 (main's `test_658`) + 2 (ts#306's two files) = 3** ✅

**An external instrument I did not build, agreeing on two numbers.** That is the control the whole
measurement rests on, and it is stronger than any self-check I could construct.

## ⛔ The gap is NOT mainly grep over-reporting — the stated reason explains ~1 file

The requested measurement was how much the literal-`--self-test` grep over-reports. **Measured: one
file.**

| mechanism | effect |
|---|---|
| grep matches prose, not a declaration | **1 file** — ts#338 `test_streamrate_readback_repro.py` (mentions the flag, never declares it) |
| manifest's **commented-exclusion** form ignored | **3 files** — ts#343, ts#344, ts#438 (policy explicitly accepts these) |
| checking **main's** manifest instead of the **branch's** | would give **31 files / 29 PRs** — *overshoots* 24 |

**GREP says 32 declare; the ORACLE says 31.** So the 24→12 gap is not the grep.

> ⚠ **I did not reproduce 21/24 and I will not guess at it.** No single mechanism I tested produces
> it: ACTIVE-only + branch gives 15/14, both-forms + branch gives 12/11, main's manifest gives 31/29.
> 21/24 sits between them. **Reporting "the true count is 12" is sound; claiming to have diagnosed
> conv-ts's method would be the summary-instead-of-source error.** Their own "upper bound" label was
> accurate and honestly given.

## ⛔⛔ MY OWN FIRST PASS WAS INFLATED BY 3, AND MY METHOD WAS WRONG IN THE SAME WAY I WAS ASKED TO CORRECT

1. **First pass: 15 files / 14 PRs** — I parsed only ACTIVE entries. The lint's docstring says a
   script must be named *"either as an ACTIVE entry … **or as a full-line comment recording why it is
   excluded**"*, and there is a separate loader (`load_selftest_excluded`) for exactly that. **Three
   files were deliberately-documented exclusions that satisfy the policy, and I was calling them
   violations.** Caught by reading what the lint *accepts*, not by assuming my parser matched it.
2. ⭐ **Then I approximated `_parse_selftest_excluded` with a regex** — in a task whose entire premise
   is *"the lint's own detection logic is the oracle, not a grep."* I replaced it with the real
   function; they agreed (3 and 3, 0 malformed), **so the number was right and the method was wrong.**

> **The instruction was followed where it was salient (the main predicate) and silently dropped where
> it was not (the secondary one).** A rule applied to the obvious case and not the adjacent one is how
> a correction fails to transfer — same shape as the grep it was replacing.

## Validation chain

- **72** open test-suite PRs enumerated (`--limit 200`; `gh pr list` silently caps at 30) — **matches
  conv-ts's 72 exactly**, so the population is agreed.
- **66** added top-level `test_*.py` instances across **59** PRs. **0 fetch failures.**
- Oracle controls, **7/7 both directions**: argparse declaration ✅, `sys.argv` membership ✅,
  `sys.argv` via local var ✅, docstring-only ✗, comment-only ✗, help-string-only ✗, no mention ✗.
  The three negatives are precisely the forms a grep over-reports.
- **Sanity:** 0 files oracle-TRUE but grep-FALSE — impossible by construction, confirming no inversion.
- Subdirectory `test_*.py` additions: **0**, so my top-level filter excluded nothing.
- `main` manifest: 18 ACTIVE, 5 EXCLUDED, **0 malformed**.

## Two findings beyond the count

1. ⭐ **The manifest is outpaced ~1.7× by work in flight.** `main` registers **18**; added files
   across open PRs declare **31**. Even fully converged, the manifest would need to roughly double.
2. ⛔ **ts#306's `harness-policy` check is FAILING, and 2 of the 12 violations are its files** —
   `test_164_json_encoder_no_loss.py` and `test_164_json_oversized_sample_stall.py`. **I am that row's
   fixer and I am disclosing this against my own work.** It was outside the authorised round (the
   confirmed findings were the truncation budget and SKIP-as-PASS, not the manifest), so the fix round
   correctly did not touch it — **but the row cannot merge with a red check**, and this is a cheap,
   mechanical fix (vet each script, then one active entry or one `EXCLUDED:` comment each). Whoever
   holds the round should know before the audit finds it.
