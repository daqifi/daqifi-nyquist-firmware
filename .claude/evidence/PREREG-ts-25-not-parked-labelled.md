# PRE-REGISTRATION — the 25 open test-suite rows that lack the `parked` label

**Written and pushed BEFORE reading any condition text.** An unpushed pre-registration is not a
pre-registration; nq-c caught me on exactly that, so this lands on the remote first.

## ⚠ FIRST CORRECTION, TO MY OWN TERM, BEFORE ANY MEASUREMENT

I called these "the 25 unlabelled rows" and **that is wrong**. They lack the `parked` label; **17
of 25 carry a different actionable label.** The population splits:

| sub-population | n | rows |
|---|---|---|
| `blocked:audit-findings` | 12 | 312 · 323 · 326 · 342 · 371 · 386 · 415 · 430 · 432 · 434 · 435 · **341** |
| `blocked:operator-decision` | 4 | 405 · 446 · 450 · 460 |
| `needs-bench` | 2 | 334 · **341** (341 carries both) |
| **NO LABELS AT ALL** | **8** | **351 · 352 · 384 · 408 · 411 · 412 · 421 · 448** |

**EXHAUSTIVE POPULATION (25, enumerated before the spec exists):**
`312 323 326 334 341 342 351 352 371 384 386 405 408 411 412 415 421 430 432 434 435 446 448 450 460`

## THE HYPOTHESIS UNDER TEST

My finding from the 49 labelled rows was: *the `parked` label selects for authors careful enough
to label, and that is the same care that writes the condition — so "0 of 49 undocumented" is a
property of the selector.* The coordinator and I both expect the complement to be worse.

**That naive form is now untestable on this population, because 17 of 25 were labelled.** The
care-to-label was present; it just produced a different label. So the prediction has to split:

- **PREDICTION A — the 8 with NO labels at all are where missing conditions concentrate.**
  I predict **≥ 4 of those 8** state no condition in any form (no `unparks when`, no need-form, no
  synonym).
- **PREDICTION B — the 17 with a `blocked:*` or `needs-bench` label mostly DO state a condition.**
  I predict **≤ 3 of 17** state none.
- **PREDICTION C — `baseRefName`.** The 49 were 100% `main`. sk#165 and sk#230's dead-trunk problem
  is said to live here. I predict **at least 1 of the 25** is based on something other than `main`.

## ⛔ THE DIRECTION OF MY OWN ERROR, AND WHAT I OWE BECAUSE OF IT

**I want prediction A to be confirmed** — it is my own methodological finding and a hit makes it
important. That biases me toward **over-classifying rows as "no condition"**, because each such row
strengthens a result I authored. The error direction is therefore: *too many rows filed as
undocumented.*

**So I bind myself to extra work in that direction only:**

1. **For every row I classify as "NO CONDITION", I must QUOTE the full text of its most
   condition-like comment** and show there is no need-form and no synonym. A bare count is not
   acceptable for that class. Rows I classify as *having* a condition need no such quote — the
   asymmetry is deliberate, because that is the direction I am not biased in.
2. **I must run the synonym net before concluding absence**, including `HELD`, `on hold`, `do not
   merge`, `not merging`, `needs`, `requires`, `waiting on`, `blocked on`, `re-derive` — sk#222 said
   HELD and no detector caught it.
3. **A row with ZERO comments is UNDETERMINED, not "no condition".** The condition may be in the PR
   BODY, which this instrument does not read. I must state which rows fall there rather than
   counting them as hits.
4. **If prediction A fails** (≤ 3 of 8), I report that plainly as a refutation of my own selector
   finding's strong form, in the same message, without reaching for a rescue.

## WHAT WOULD FALSIFY THE SELECTOR FINDING ENTIRELY

If the 8 no-label rows state conditions at roughly the same rate as the 49 parked rows (100%
stated, 80% explicit), then **label-presence does not track condition-presence**, and the whole
"one act of care" explanation is wrong — the 49's health would need another cause. I will say so.

## WHAT THIS PASS WILL NOT DO

No judgement on whether any condition is satisfied, reasonable, or cheap. No labels, no comments,
nothing on any board. UNDETERMINED over resolved. Full field set per row including `baseRefName`
and `mergeStateStatus`, with DIRTY carried as its own axis rather than absorbed.
