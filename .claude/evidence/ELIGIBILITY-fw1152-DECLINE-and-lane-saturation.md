# fw#1152 — eligibility: **DECLINE**. And both offered rows fire, so this lane is saturated on firmware.

Produced 2026-10-07. Row: `174324e7e8a86b26464cfb0d425e95a1031959fe`, MERGEABLE/CLEAN, labels
`Review effort 2/5` only (not parked). Title: *fix(scpi): CONF:ADC:USECal must validate the reload
before persisting the selector (#1148)*.

**Both channels run, neither zero taken as a clearance** — applying my own correction from fw#991 to
myself first, as asked.

## ⛔ Channel 1 — MY OWN RECORDS. 71 hits, and one is decisive.

The disqualifier is **disposition-authorship**, and it is sharper than the usual kind:

> `**Refresh would PRE-EMPT the pending decision (the fourth exclusion class):**`
> `- **fw#1152** — its park asks the operator to *authorise the merge path* OR *authorise one audit`
> `  round on the refreshed head*; **refreshing forecloses the first option.**`

**I invented that exclusion class and placed fw#1152 in it.** So I:

1. read and interpreted **both arms** of its park;
2. published that **refreshing this row forecloses an option the operator still held**;
3. and the row **has since been refreshed** (conv-fw, today, head now `174324e7e`).

> **That is not a bias inside the audit — it is a published prejudgment of the audit's
> PRECONDITION.** I am on record that this head's existence consumed an operator option. An auditor
> must be able to treat "is this the right head to audit" as open, and for me it is not.

Four further kinds, none of which I need but all of which point the same way:

- `LIKELY OK 1152 (already refreshed once successfully; its hardware result lives on…)` — my
  refresh-ability assessment.
- I used **fw#1152's audit artifact as data** in an instrument-validation study: *"nq-c's fw#1152 has
  `rawFindings=2`"*, *"fw#1152's five is the anomaly with that key"*. Measurement about its artifact.
- Its row appears in my DIRTY census and in two artifact-health classification tables.
- ⚠ My memory records that I *"walked into"* the **park-lives-in-a-comment** defect **on fw#1152** —
  i.e. I acted on this row without seeing its stated park. An action, not a reading.

⭐ One item is genuinely exculpatory and I record it for symmetry: I called it **"nq-c's fw#1152"**,
and a disclaimer counts as evidence of non-involvement. It does not survive the exclusion-class
finding, but a decline built only on hit-counts would have missed that the strongest hit is a
*judgement* and the weakest is a *disclaimer*. **Grade the hits; never just count them.**

## ⚠ `audit-fw1152-r3` stays UNDETERMINED — and SHA pairing FAILED here

The method that closed `audit-991n` an hour ago does **not** generalise:

```
audit-fw1152-r3                b97d92fd1 (detached HEAD)
agent-a2794f2feed3c9332        b97d92fd1 [fix/1148-usecal-selector-ordering] locked
agent-a44958032b613514f        b97d92fd1 [fix/1148-usecal-selector-ordering] locked
agent-aa8a78baa68b1232d        b97d92fd1 [fix/1148-usecal-selector-ordering]
```

Four trees share the SHA, but the three partners are **harness `agent-*` trees, which carry no lane
name** — so pairing identifies the *branch*, not the *lane*. **I cannot rule out having held a
round-3 audit tree on this row.** Weak contrary evidence only: my lane names per-PR trees
`nq-b-fwNNNN` (`nq-b-fw1020`, `nq-b-fw1099`, `nq-b-fw1124` exist) and there is **no `nq-b-fw1152`** —
but a negative cannot clear, by the rule above. **Correcting my own recommendation: pairing works
only when a partner is lane-named.**

## ⛔⛔ BOTH OFFERED ROWS FIRE — fw#996 was already declined

The fallback was fw#996. I declined it **earlier this session**: `b123fa810` — *"fw#996 DECLINED:
clean on the symbols, conflicted on the ROW — disposition-authorship is a seventh kind"*. So there is
no need to re-run it; the answer is on record.

## ⭐⭐ THE STRUCTURAL FINDING: the lane that did the CENSUS disqualified itself from the rows it surveyed

This is not a run of bad luck and it is not a personal failing. Count the declines: **fw#996,
fw#1110, fw#991, fw#1152** — and fw#976 only reachable as *fixer*. The common cause is visible in
every one of them:

| row | what disqualified me | artefact it came from |
|---|---|---|
| fw#996 | disposition-authorship | the unpark-conditions survey |
| fw#1110 | published unpark condition + spent round | the 14-row unpark survey |
| fw#991 | parked it, and parked its blocker | round-cap park decisions |
| fw#1152 | exclusion-class classification | the refresh-eligibility census |

> **Doing the fleet-wide measurement work IS what saturated this lane's eligibility.** Every census
> that classifies rows creates disposition-authorship on each row it classifies. My publication
> footprint already measured the scale — **124 numbers, 137 symbols** — and I read that as a *routing
> aid*. It is also a **map of where I can no longer be used.**

**This predicts the next failure, which is the point of writing it down:** whoever runs the next
cross-row census will be disqualified from the same rows in the same way, and nobody is tracking that
cost when the census is commissioned. **A census should be commissioned from a lane you are willing
to spend, or its output should be structured so classification is separable from judgement** —
listing a row's park arms verbatim is a reading; ranking rows into exclusion classes is a judgement,
and only the second one burns the lane.

## Process note — ⛔ a shared script path handed me ANOTHER LANE'S OUTPUT

Mid-read, `/tmp/temp.sh` returned output that began as mine and then became a different lane's
entirely (*"ELIGIBILITY, five kinds"*, a `jq` error, round-cap verdicts from 2026-09-11, a
`head MISMATCH … given=ff08f5ce9`). CLAUDE.md directs **every** lane to that one filename and ~5 run
concurrently, so a script can be replaced between the `cat >` and the `bash`. **The output was
plausible, same-format, and the boundary unmarked** — I nearly read a foreign `head MISMATCH` as a
finding about my row. Re-ran from the session scratchpad with a `### MARKER` first line, which is the
only cheap detector. Filed as a fleet hazard:
`feedback_TMP_TEMP_SH_IS_SHARED_and_a_script_can_be_REPLACED_between_write_and_execute.md`.
