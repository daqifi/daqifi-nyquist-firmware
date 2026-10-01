# The 25 open test-suite rows lacking the `parked` label

**Read-only. Nothing changed on any board.** Produced 2026-10-01 by lane nq-b.
Pre-registration pushed **before** any condition text was read:
`.claude/evidence/PREREG-ts-25-not-parked-labelled.md` at commit `96eadc203199f413584a6915d422e4e961b23360`.

All 25 fetched, payload-verified against `.comments` with retries. **0 UNDETERMINED.**
(ts#312 needed a second attempt after a `503 Service Unavailable`.)

---

## ⛔ BOTH PRE-REGISTERED PREDICTIONS HIT NUMERICALLY AND ARE REFUTED CAUSALLY

| | predicted | actual | letter | mechanism |
|---|---|---|---|---|
| **A** — of the 8 no-label rows, ≥4 state no condition | ≥4 of 8 | **exactly 4** | ✅ hit | ❌ **refuted** |
| **B** — of the 17 labelled rows, ≤3 state none | ≤3 of 17 | **exactly 3** | ✅ hit | ❌ **refuted** |
| **C** — ≥1 of 25 based on something other than `main` | ≥1 | **0 of 25** | ❌ **refuted** | premise was wrong repo |

**I am leading with the refutation because that is the direction my pre-registration says I am
biased against.** I predicted the condition-less rows would be **parks whose authors did not bother
to state a condition** — that is the claim that would have made my selector finding important. It
is wrong. Every condition-less row in this population is a row that **was never parked at all.**

> **PARK-presence tracks condition-presence. The LABEL is a third, independent thing.**
> Across both populations, **67 of 67 parked rows state a condition** — 49 of 49 labelled, and
> 18 of 18 parked rows among these 25. **"A park with no stated condition" may be an empty
> category in this repo.** What the label fails to track is *whether a row is parked*, not
> *whether it has a condition*.

So my earlier "one act of care" explanation was **right about the correlation and wrong about the
cause**, and the corrected version is stronger: the care that writes a condition is the care that
**parks**, which is a separate act from applying the label.

⚠ **Prediction C was refuted by a premise I was given, not by my reasoning** — `sk#165`/`sk#230`
are `cptkoolbeenz/claude-skills` rows and `autopush/*` is a skills-repo branch pattern. No
test-suite enumeration could find that. **The by-product is better than the prediction: 74 of 74
open test-suite rows are based on `main`** — a complete clean negative over the whole repo. The
dead-trunk exposure is a claude-skills question.

---

## Classification — all 25 (`*` = one of the 8 with no labels at all)

| category | n | rows |
|---|---|---|
| **PARKED, condition present** | **13** | #312 #323 #341 #371 #405 #408\* #411\* #415 #421\* #430 #432 #434 #448\* |
| **PARKED, condition in NEED-form** | **5** | #342 #386 #446 #450 #460 |
| **NEVER PARKED** — no park speech act | **6** | #326 #334 #352\* #384\* #412\* #435 |
| **explicit NON-CLAIM** | **1** | #351\* |

**Field set:** `baseRefName` = `main` for all 25. 23 CLEAN / 2 DIRTY (ts#411, ts#412 — carried as
its own axis). 19 ready / 6 draft.

**And "unlabelled" was my own mislabel, corrected before measuring:** 17 of 25 carry a *different*
actionable label — 12 `blocked:audit-findings`, 4 `blocked:operator-decision`, 2 `needs-bench`.
Only **8** carry none. The care-to-label was present; it produced a different label.

---

## The quoted pass — the 5 NEED-form rows

Required by the pre-registration: any row I might classify as condition-less gets its passages
quoted rather than counted. **All five state a condition. None is condition-less.**

- **ts#342** — *"…it is the next unit on this PR. **I am not merging this until it lands.**"*
- **ts#386** — *"**This PR is PARKED at the 5-round cap.**"* The cap is the condition; it needs an
  explicit authorisation to exceed.
- **ts#450** — *"…is **HELD, not merged**. Under the co-merge convention a firmware PR does not
  land until its companion is audited clean… **A fresh fix round follows on this PR.**"*
- **ts#446** — see below, it is half of a pair.
- **ts#460** — see below, it is the most actionable row in the set.

### ⭐ ts#446 ↔ fw#1137 IS A PAIR DOCUMENTED FROM BOTH ENDS, CONSISTENTLY

ts#446: *"[#1137] remains **audited PASS and HELD by the co-merge convention, waiting on this
PR**"* and *"Not merging. The firmware side, #1137, **passed its own audit and is deliberately
being held**."*

fw#1137 (from my own firmware pass): *"**Unpark condition … ts#446 resolving.** Not 'when
convenient', not 'when main settles.'"*

**Both ends name each other and they agree on the direction** — ts#446 resolves, then fw#1137
lands. **Not circular.** This is the direct counterexample to the ts#330 ↔ fw#996 case, where
eleven cross-references existed with **zero inside either hold**. Here the dependency is inside
both holds and in the same orientation. **This pair is how it should look.**

⚠ And it sharpens my fw#1137 finding rather than changing it: fw#1137 is DIRTY and its PASS sits on
the live head, so the refresh that would clear DIRTY voids the PASS — **and now there is a second
reason not to touch it**, because its companion is still open and the co-merge convention means the
head would have to survive until ts#446 lands.

### ⭐ ts#405's CONDITION NAMES A ONE-LINE GENERAL FIX, NOT JUST A WORKAROUND

Quoted because it reframes what this row needs:

> *"**What clears it:** a **test-suite-rooted session** merging it (everything is prepared and the
> marker is live), **or the one-line gate fix** — pass the guarded command's `--repo`/`GH_REPO`
> into that lookup, **which clears this and every future cross-repo merge.**"*

The condition is a **disjunction**, and its second arm is not a workaround for this row — it is a
one-line change that retires the whole class. Also note it touches the **`GH_REPO` lookup**, which
is already a recorded merge-gate bypass vector, so the fix and a known hazard are the same line.

⚠ **And it is phrased "What clears it", not "Unparks when".** A heading-shaped test keyed on
`unparks when` finds nothing here. I caught it only because `what clears` was in the net —
**another synonym, and one nobody had listed.**

**ts#341** likewise states its condition under a clear heading: *"**What would unpark this PR,
operator's call:** 1. A cap exception for one more round… 2. A waiver to merge on the current
`BLOCK`, with #353 tracking the residue."*

### ⛔ ts#460 — AN OPERATOR RULING TO MERGE, UNEXECUTED FOR SEVEN DAYS

*"Acting on the relayed operator ruling **'merge ts460'**. Everything technical re-checked at
`aa6933f1ef40f52167e04a794d715a234e2b3c8f`, **seven days after** t[he ruling]"* … *"Re-verified
clean at this head, gate marker created — but **this lane structurally cannot merge it**, and that
is worth recording rather than retrying."*

**The condition is already satisfied and the blocker is not the row.** A ruling exists, the head is
clean, the gate marker is created, and the lane cannot execute. This is the ts#405 seven-day shape
**with an operator decision already made** — which makes it strictly more actionable than any park
in either population. It carries `blocked:operator-decision`, which is now the wrong label: the
operator already decided.

---

## ⚠ An instrument defect this population exposed and the 49 could not

My need-form tier **fired on Qodo bot prose** — *"The achieved-rate gate **requires** at least 1000
Hz by default"* (ts#352) — and on a lane **declining** to act (ts#351: *"not taking this row…not a
lane claim…no label is applied and no gate is marked"*, six non-claim markers).

**A condition detector tuned on parked rows is miscalibrated on unparked ones.** The 49 were almost
entirely lane-authored park comments; these 25 include rows whose comment stream is mostly bot
output — **ts#412 has `lane=0`, every comment a bot's.** The fix is to filter to lane-authored
comments and require a park **speech act**, not park vocabulary. That is the discuss-vs-is category
again, and it was invisible in the first population because the first population had no bots in it.

## What this pass did not do

No judgement on whether any condition is satisfied, reasonable or cheap — except where the row
states it itself, as ts#460 does. No labels, no comments, nothing on any board.
