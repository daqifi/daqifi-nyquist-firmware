# Audit of decision-page items 05, 04 and 10 — the overlap neither prior auditor checked

Page: `https://claude.ai/artifact/759exLyXGSBnPevPP9Gnwu` (v7, 15 decisions).
Audited 2026-10-01 by lane nq-b. **Read-only. No labels, no comments, nothing on any board.**
`~/.claude/skills` was never touched by a git command — installed sizes read with `stat` only.

| item | verdict |
|---|---|
| **05** — the round-cap unit | ⛔ **FALSE AS STATED** (2 of 4, not 4) — **but its conclusion survives on better evidence** |
| **04** — sk#165 file counts | ✅ **CONFIRMED exactly** |
| **10** — installed-vs-main byte counts | ✅ **CONFIRMED exactly, all four figures** |

---

## ⛔ DECISION 05 — "four of which carry its unpark sentence *word for word*" IS FALSE. IT IS TWO.

The claimed shared sentence: *"an explicit operator instruction for one fix round plus one audit
round."* Tested against payloads already fetched and payload-verified, **and against each PR's BODY
as well**, so a body-level occurrence could not hide:

| row | comments (markdown-tolerant) | body | verdict |
|---|---|---|---|
| **fw#1094** | **2** | 0 | ✅ states it verbatim, twice |
| **ts#415** | **1** | 0 | ✅ states it verbatim |
| **fw#1106** | **0** | 0 | ❌ **and explicitly disclaims having one** |
| **ts#425** | **0** | 0 | ❌ **and explicitly records that it has none** |

**fw#1106 does not merely lack the sentence — it says so.** The passage a reader might mistake for
its condition is flagged on the row itself: *"the disclosed fix direction (**not itself an unpark
condition**, but the shape the eventual fix round would need to take)."*

**ts#425 is more explicit still:** *"### ⚠ Unpark condition: NOT RECORDED — **No unpark condition
was stated when this PR was parked, and I am not inventing one.** Absent, not none."*

### ⭐ BUT THE CONCLUSION SURVIVES, AND THE REAL EVIDENCE IS STRONGER THAN THE STATED EVIDENCE

The page uses the shared sentence to argue *"why it is one ruling"*. That argument is unnecessary.
**ts#425 derives the same requirement independently and says so:**

> *"…the cap-versus-gate arithmetic that parked ts#400 and ts#415 … the round-4 findings at
> `69455df2` are open, one round of five remains, and reaching a merge from here needs **two**
> rounds — so **any unpark necessarily involves a decision to go past the cap, exactly as ts#400 and
> ts#415 spell out for themselves.**"*

**A row that independently derives the same requirement is better evidence of a common cause than a
copied sentence is** — a shared sentence is equally consistent with one author reusing a phrase.
fw#1106's park is likewise the cap-versus-gate deadlock by its own arithmetic (*"A fix push would be
round 5, and the audit that the merge gate itself requires would be round 6 — past the cap"*).

**So the fix to the page is narrow:** replace *"four of which carry its unpark sentence word for
word"* with *"two state its unpark sentence verbatim; two more derive the same requirement from
cap-versus-gate arithmetic, one of them explicitly declining to state a condition at all."* The
decision's scope, its 12-row reach, and `Releases` are unaffected by this correction.

### ⚠ One instrument note, and it is the inverse of a case from earlier tonight

My first test was exact substring matching and **it missed ts#415**, because the row writes
*"one fix round plus one audit round"* with the emphasis inside the sentence —
`**one fix round plus one audit round**` — and begins it *"An"* rather than *"an"*. A
markdown-tolerant pattern found it.

**That is the opposite of conv-ts's case**, where markdown bold was wrongly blamed for a regex miss
whose real cause was a `[^.]` span hitting a filename's dot. Both facts are true and they are not in
tension: **bold does not defeat a regex with wildcards; it does defeat exact substring matching.**
The lesson is not "bold is harmless" or "bold breaks things" — it is that the *matcher class*
decides, so name the matcher before blaming the text.

---

## ✅ DECISION 04 — CONFIRMED EXACTLY

sk#165 (`cptkoolbeenz/claude-skills`), *"gates: pass the named repository into the PR lookup (#125)"*:

- **7 files** — exactly 7, as claimed.
- **All four named files present**: `pre-merge-gate.sh`, `mark-audited.sh`, `merge-target-keys.sh`,
  `test-repo-passthrough.sh`.
- **`test-repo-passthrough.sh` is `262+/0-`** — the "262-line test file" figure is exact.
- Base `autopush/office-390bc12dcdf8` (not main) ✓ and head `2c81067c` ✓, both as the page states.

Full manifest: `mark-audited.sh` 88+/4− · `merge-target-keys.sh` 250+/11− · `pre-merge-gate.sh`
66+/2− · `test-audit-provenance.sh` 63+/0− · `test-merge-gates.sh` 61+/20− ·
`test-pre-merge-gate.sh` 25+/2− · `test-repo-passthrough.sh` 262+/0−.

⚠ **My own counter was the thing that was wrong here.** An awk pass over `gh pr diff` reported 263
added lines for that file, because `/^\+/` also matches the `+++ b/<path>` header. The page's 262 is
correct and my check was off by exactly that one line. **A diff-line counter must exclude the `+++`
and `---` headers**, and the authoritative source is the files API's `additions`, not a hand count.

---

## ✅ DECISION 10 — CONFIRMED EXACTLY, ALL FOUR FIGURES

Main's sizes taken **from the remote** (`gh api .../contents/...?ref=main`), installed sizes by
`stat -c %s` only — **no git command ran in that tree**, so its index mtime did not move.

| file | main | installed | page claims |
|---|---|---|---|
| `qodo-cycle/pre-merge-gate.sh` | **4,917** | **12,377** | 12,377 → 4,917 ✅ |
| `qodo-cycle/merge-target-keys.sh` | **15,895** | **19,343** | 19,343 → 15,895 ✅ |

**`_mtk_classify`: 0 occurrences in main, 2 in installed** — absent from main, as claimed ✅.

**Control run so the zero is not a dead grep:** a string that must appear in both files returns 3
hits in each. The main copy was fetched and decoded to 15,895 bytes, matching the API's reported
size, so the file I grepped is the file the figure describes.

---

## ⛔⛔ A SELF-CORRECTION THIS AUDIT FORCED: MY OWN PUBLISHED FIGURE WAS WRONG

I reported **"49 of 49 parked test-suite rows state a condition"** and generalised it to **"67 of 67
parked rows"**, calling *"a park with no stated condition"* a possibly-empty category. **ts#425
refutes it — and my own classifier counted ts#425 as condition-present**, because the heading
`Unpark condition` matched and **I never read its value, which is `NOT RECORDED`.**

> **That is the `noProvenance: false` error exactly: I matched the FIELD NAME and treated it as the
> CLAIM.** I have written that rule down twice and reproduced it inside the instrument I built after
> writing it.

Swept all three populations (88 rows) for the negated form, with fw#1094 as a negative control that
correctly stays silent. **Three rows carry an unpark-condition heading whose value admits absence,
and none has a positive condition elsewhere: ts#406, ts#425, ts#437.**

**Corrected figures: 46 of 49, and 64 of 67.**

### The corrected law is better than the one it replaces

> **Every parked row either states a condition or explicitly states that it has none.**
> 64 of 67 state one; the 3 remaining *document the absence* — *"Absent, not none"*, *"I am not
> inventing one"*. So **"a park with no stated condition" is not empty — but "a park with an
> UNDOCUMENTED absence" may be.**

And those three are **best practice, not a defect**: a park that records having no condition is
strictly more useful than one that invents a plausible-sounding one, and it is the same
absent-versus-none distinction this whole night has turned on. ts#425 applying it to itself, in
those words, is the cleanest instance of it anywhere in the three repos.
