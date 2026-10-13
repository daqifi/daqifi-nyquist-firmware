# fw#1048 Static analysis: **NONE of the three.** The gate never ran — `apt-get` hung for six hours.

Produced 2026-10-13. Verification work, bench-free. **Eligibility disclosed below.**

## The answer to "which of the three?"

> **None.** Not a real defect, not a false positive, not baseline drift. **`cppcheck` never executed
> and the baseline was never compared.** The job was killed at GitHub's 6-hour limit while still in
> its `apt-get` install step.

```
run 37667782081   conclusion = cancelled  (NOT failure)   headSha e6b719974
  1. Set up job                          success    2026-10-07T18:33:50Z
  2. Checkout                            success    2026-10-07T18:33:52Z
  3. Install cppcheck                    CANCELLED  2026-10-08T00:34:16Z   <- hung ~6h HERE
  4. Check log literals are ASCII        skipped
  5. Run cppcheck                        SKIPPED    <- cppcheck NEVER RAN
  6. Compare against committed baseline  SKIPPED    <- the gate NEVER COMPARED
failed-step log: 0 bytes · findings/errors/styles/warnings/baseline mentions: all 0
```

### Controls, both directions

| control | result |
|---|---|
| **normal runtime** of this workflow | **22 s · 23 s · 28 s** (three most recent successes) |
| **this run** | **6 h 0 m 32 s** — ~780× the median |
| **workflow health** | **12 of 12 recent runs SUCCESS**, incl. `b960c9245` at 18:57:07Z — **23 min after this run began hanging** — plus two `main` pushes |
| **re-runs on this head** | **none.** One run exists for `e6b719974`, the cancelled one |

So: not the workflow, not the repo, not infrastructure-wide. **A one-off transient package-mirror
stall, six days stale, on a head nobody has re-triggered.**

## ⛔ The indicated action is a RE-RUN and nothing else

- **NOT** a code fix — `cppcheck` never executed, so no finding exists to be real.
- **NOT** a suppression — there is nothing to suppress.
- **NOT** a baseline regeneration — step 6 was **skipped**; the zero-line baseline was never consulted.
  The warning *"a regenerated baseline hides a finding as effectively as it records one"* applies with
  extra force here: **there is no finding to hide, so a regeneration would encode a lie about a
  measurement that never happened.**
- ⭐ **`diverged, ahead 5, behind 14` is a RED HERRING for this failure.** The base gap may matter for
  other reasons, but it cannot explain a hang in `apt-get`. The drift hypothesis is refuted by step 6
  never running.

**I have not re-run it.** It changes a `blocked` row's visible check state and I am not its owner.

## ⭐⭐ The methodological finding: a check can report FAILURE without having MEASURED anything

`gh pr checks` renders `cancelled` as **`fail`**, identically to a real finding. All three offered
hypotheses — defect / false positive / drift — **presuppose the gate produced a verdict.** It did not.

> **The refuting fact was in the same output as the premise:** `gh pr checks` printed
> **`Static analysis  fail  6h0m29s`**. The duration was sitting next to the word that misled.

⭐ This is the mirror of the `WifiClientForceClosed` finding from the same week: there, a telemetry
field could only ever report **success** (`0 until Opt 1`), so `=0` read as "no problem". Here a check
reports **failure** without a measurement, so `fail` reads as "found something". **Both are gates whose
output is decoupled from any observation** — and in both cases the giveaway was a *second* field next
to the misleading one.

**Detector:** before diagnosing a red check, read its `conclusion` (`failure` vs `cancelled` vs
`timed_out`) and its **duration against the workflow's median**, and confirm the step that produced
the verdict actually **ran** rather than being `skipped`.

## Eligibility — disclosed, and the bias was refuted rather than merely managed

**I proceeded rather than declined**, on a measured basis: my records carry **26 hits** on fw#1048 but
**zero** for 1048 combined with cppcheck / static / lint / finding. My involvement is entirely
**administrative** — an ownership finding (*"the record says it is NOBODY'S"*, with the rule *"a lane
NAMED in a comment is not a lane that CLAIMED it"*), a relay note marked `authority: RELAYED`, a
base-movement caveat carried there, and a **self-exclusion**: I established that *my own* board is
uncalibrated and cannot bench this row. **I have never assessed its code.**

⭐ **Declared bias, and its outcome:** I had published that this row's *"context moved — main rewrote
the audited functions"*, which mildly favours the **drift** hypothesis — the most convenient of the
three. **The result refutes drift.** So the bias did not drive the conclusion, which is better evidence
that disclosure-plus-structure works than any assurance I could have given in advance.

⚠ And declining this would have been the **false-decline** failure: over-declining has no downstream
detector, the grounds here are administrative rather than substantive, and the task was a measurement
with a falsifiable answer.
