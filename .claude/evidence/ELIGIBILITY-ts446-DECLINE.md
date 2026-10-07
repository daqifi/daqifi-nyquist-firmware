# ts#446 round 4 — eligibility: **DECLINE**. Two independent grounds, and NEITHER is the firmware mechanism.

Produced 2026-10-07. Row: PR #446 at `5b93a55`, `daqifi-python-test-suite`. Both channels run,
controls verified in both directions, neither zero treated as a clearance.

⭐ **The coordinator's warning was correct and load-bearing:** my four firmware exclusions (fw#996,
fw#1110, fw#991, fw#1152) were all **disposition-authorship** created by censuses. ts#446 fires on
**two different mechanisms**. So the firmware pattern does **not** extend — and test-suite rows are
**not** clear by default either. Each row needs its own read.

## ⛔ Ground 1 — CITED PRECEDENT, and this round modifies the exact function I cited

The sixth key in my own eligibility progression (row → parent PR → parent issue → filename form →
symbol under test → **cited precedent**), firing exactly. From my own ledger, in a **fire brief**:

> *"Either the test sets a documented default itself, or `device_reset` is extended — and if
> extended, **additively, per ts#446's ruling on `ReliableSCPI.query()`**, for the same reason: a
> behaviour change to a shared primitive is a fleet-wide blast radius. Briefed to choose and justify,
> not to pick silently."*

I used ts#446's ruling as **governing fleet authority** and propagated it into a brief that steered
another row's work. And this round's `test_harness.py` hunks are:

```
@@ -702,6 +702,15 @@ def query(self, cmd, delay=1.0, on_sent=None):
@@ -736,6 +745,11 @@ def _query_raw(self, cmd, delay, on_sent):
@@ -746,16 +760,57 @@ def _query_raw(self, cmd, delay, on_sent):
```

> **I would be auditing changes to `ReliableSCPI.query()` — the very function whose prior ruling I
> have been enforcing on other rows.** If this round alters that contract, the enforcement I did
> elsewhere rested on a moving foundation, and I have a stake in the ruling I cited staying valid.

## ⛔ Ground 2 — a LIVE cross-PR stake, created by me HOURS AGO

I am the **fixer on ts#306** (pushed today). Its defect-2 fix — making a runtime SKIP distinguishable
from a PASS — is justified by citing `test_877_channel_truncation_twins.py:485` as the suite's
convention. I verified what is there:

```
test_877:485   return '===== %s: %d PASS, %d FAIL, %d SKIPPED =====' % (
                   GATE_LABEL, npass, nfail + nunj, nskip)
```

That **is** the three-term banner my fix was built to match. And ts#446 rewrites the hunk containing
it:

```
@@ -476,39 +502,209 @@ def control_already_guarded_verdict(...)     # original 476-514 -> 39 lines become 209
476 <= 485 <= 514   ->  INSIDE the rewritten region
```

> **ts#446 rewrites the region holding the convention that justifies my own just-pushed fix on another
> PR.** A clean verdict from me would be a verdict on whether my own fix's cited authority survives.

## ✅ What I checked and found NOT disqualifying — recorded for symmetry

**`test_harness.py` has NO line-range overlap** between the two PRs:

| | ts#446 | ts#306 (`077c694bd`) |
|---|---|---|
| hunks | `@@ -702`, `@@ -736`, `@@ -746`, `@@ -3539` | `@@ -1001`, `@@ -1641`, `@@ -1686` |
| region | `ReliableSCPI.query` / `_query_raw` / `probe_unreadable` | inside `class StreamingMeasurement` |

⭐ **And a HOMONYM that a grep alone would have mis-scored.** Searching `leaked` in ts#446's diff
returns six hits — but they are a **different concept**:

- ts#446: *"a reading **leaked** despite an unconfirmed `SYST:LOG:CLEar`"* — data escaping past a guard.
- ts#306: `StreamingMeasurement.leaked` — a sample-loss detection flag.

**Same word, same file, unrelated meanings.** The grep found them; only reading them separated them.
Had I scored on hit-count, this would have read as a second overlap it is not. *Grade the hits, never
count them* — fourth instance today.

## Two further, weaker kinds

- **Measurement + classification:** my ledger carries `| ts#446 | 790a15eb82 | ACTIVE | rc=0 self-test: 0 checks failed |`, and I classified it as fw#1137's companion (*"Companion pair in range: fw#1137 → ts#446"*).
- **Content assessment:** I evaluated and apparently refuted a claim that something *"Carries both ts#446 mitigations"*.

Neither is needed; both point the same way.

## ⭐⭐ The method finding — the disqualifying KIND varies per row, not just the channel

This morning's correction was that **which CHANNEL is decisive reverses per row** (records vs the
row). This is one level up:

> **Which KIND of entanglement disqualifies also varies per row.** A check tuned to the previous
> row's mechanism will miss a row disqualified by a different one. My firmware declines were
> disposition-authorship from censuses; ts#446 is **cited precedent** plus a **same-day cross-PR file
> stake**. Running "the census test" here would have returned clean.

Practical form: **enumerate all six (now seven) kinds every time**, and do not let the last row's
decisive key become the whole checklist. Cf. the eighth kind (routing-text contamination) found by the
coordinator — the list is still growing, so an enumeration is a floor, not a ceiling.

## Handover — what I read before stopping, usable by whoever takes it

- Files in the diff: `regression_gate.py`, `test_877_channel_truncation_twins.py`,
  `test_903_single_error_per_reject.py`, `test_harness.py`,
  `test_log_clear_verified_termination.py`, `tools/lint/harness_policy_offline_tests.txt`,
  `tools/lint/harness_policy_selftests.txt`. Patch is **85,445 bytes** — not a small round.
- `test_877` carries **8 hunks**, one expanding 39 lines to 209. This is a substantial rewrite of a
  file 34+ suite tests depend on for `KNOWN_VARIANTS` and the banner convention.
- ⚠ **`tools/lint/harness_policy_selftests.txt` and `harness_policy_offline_tests.txt` are both
  touched** — and a prior finding of mine is that **TWO manifests govern a suite test**
  (`regression_gate.py` + `harness_policy_selftests.txt`), so *a registered self-test is not a
  running self-test*. Both manifests plus `regression_gate.py` are in this diff; whoever audits should
  check the registration actually takes effect rather than that the line was added.
- **I did not grade the substance** — not the predicate-vs-location falsifier, not the
  `last_query_confirmed` contract argument, not whether the discriminating negatives bite. Declining
  before grading is the point.
