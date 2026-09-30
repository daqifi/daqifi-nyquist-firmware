# PRE-REGISTERED CRITERION — does a local drainer report CLEAN on an UNREADABLE probe?

On disk BEFORE opening any sampled file. nq-b, 2026-09-30, read-only. Commit named in the report.

## THE QUESTION — one per drainer

> **When the `SYST:ERR?` probe CANNOT BE READ, does this drainer report "clean" or "could not
> confirm"?**

Not "is it a duplicate" and not "does it return None." The harm, from `test_589`'s own docstring:
a drainer that gives up silently leaves a stale error in the queue, and **the next command is blamed
for it** — *"aborted blaming the fixture CRC for a `-350 Queue overflow` that setup had produced."*
That is a **false FAIL against correct firmware.**

## COMPARISON STANDARD (in-tree, not invented)

- `test_589_wifi_sd_concurrent.py::_drain_errors` — *"Returns True only if the device positively
  reported an empty queue."* **Correct.**
- `test_harness.py::drain_errors` — appends `ERR_UNREADABLE` on **every** exit that did not observe
  the terminating `0,"No error"`. **Correct, different contract.**

## BUCKETS

- ⛔ **CLEAN-ON-UNREADABLE** — an unreadable/empty probe is coerced (`or ''`, `or []`, bare `except`)
  or the bounded loop simply runs out, and the function's exit is **indistinguishable from a
  confirmed-empty exit**. Quote the predicate.
- **SIGNALS-COULD-NOT-CONFIRM** — the exit distinguishes gave-up from confirmed: a boolean/tri-state
  return, a sentinel appended, a raise, or an explicit caller-visible flag.
- **UNDECIDABLE** — cannot be settled by reading. **Own bucket. Never folded into either side.**
  *It has come back empty twice tonight; a non-empty value here is a better outcome than a forced call.*

## SECOND AXIS, REPORTED REGARDLESS OF THE RATIO

> **Does a VERDICT depend on the result?**

- **VERDICT-REACHING** — the return is bound and branched on toward a FAIL/abort/skip/PASS row.
- **EFFECT-ONLY** — called for its side effect; return unbound or discarded.

⛔ **A drainer can be CLEAN-ON-UNREADABLE and EFFECT-ONLY and still do harm** — the stale error it
leaves behind poisons a LATER read, which is exactly `test_589`'s recorded bench failure. So
EFFECT-ONLY is **not** a clean bill; it means the false verdict appears somewhere other than at this
call. I will report the two axes separately and not collapse them.

## SAMPLING RULE, stated with its residual bias

Population: every file defining a local drainer, enumerated at `05eb7f72` by four independent shapes
(`def _drain_errors`, `def *drain*`, inline `SYST:ERR?` loops, harness importers).

**Rule: sort the population by TEST NUMBER ascending, take every 4th.** Test numbers track age and
subsystem, so a leading block would sample the oldest tests and one or two subsystems only.

**Residual bias I accept and disclose:**
1. **Systematic sampling aliases** if the ordering has periodicity at stride 4. I have no reason to
   expect it and no way to rule it out.
2. Files with **no test number** (`test_sd_streaming_regression`, `test_config_sweep`, …) sort
   outside the numeric range and are sampled as their own trailing block — so the sample is
   stratified by *has-a-number*, not purely systematic.
3. **A drainer reached by none of my four shapes is outside the measurement**, and I will say so
   rather than imply coverage.

**STOPPING RULE: sample to a RATIO, not a classification.** If the first 10 come back **8 or more in
either direction, STOP and report.** The decision being bought is *"is this lopsided"* — the exact
count is not the purchase, and continuing past the answer would be spending without buying.

## CITATION

Quote the greppable predicate. Any line number is qualified by commit `05eb7f72`.
