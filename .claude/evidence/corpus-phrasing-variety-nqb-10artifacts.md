# PHRASING VARIETY of the arbiter's merge relation — nq-b's pre-registered 10-artifact corpus

Measured 2026-10-01 for conv-fw. **Raw counts and verbatim strings only; no classification.**
Population: the same pre-registered 10 artifacts (`ls *.json` in this dir = 10, matching the
registered 10 — the pre-registration predicate was "every `.json`, no filtering", so the disk
enumeration *is* the predicate). argv built from that list, never a directory.

Script: `scratchpad/phrasing_variety.py`. conv-fw's two regexes were **copied verbatim** from their
`arbiter-corpus-measure.py:204` and `:209` and not "improved":

```python
RE_INDEXED = r'(?:same defect as|twin of)\s+index\s+(\d+)'      # their :204
RE_OTHER   = r'\b(same defect|duplicate of|twin)\b'             # their :209
```

## ⛔ HEADLINE: THEIR CANONICAL REGEX SCORES ZERO ON 23 OF 23

| measure | result |
|---|---|
| rationales total | 23 |
| matched `RE_INDEXED` (the back/fwd measurement) | **0** |
| fell through to `RE_OTHER` (`nOtherMergeLang`) | 5 |
| matched neither | 18 |
| cite a finding ordinal, by mechanical rule `(?:index\|#)\s*\d+` | 15 on 5 of 10 rows |
| …after removing my own confirmed over-match (below) | **14 on 4 of 10 rows** |
| conv-fw's `nOtherMergeLang` unsoundness flag fired on | **2 of 10 rows** (ts326=4, sk199=1) |

**`nBackRefs = nFwdRefs = 0` across all ten rows does not mean the arbiter did not merge.** It
merged in at least 14 rationales. Neither of the two phrasings `RE_INDEXED` tests for —
*"same defect as index N"*, *"twin of index N"* — **occurs even once in this corpus.**

## ⛔⛔ AND THE UNSOUNDNESS FLAG UNDER-REPORTS BY THE SAME MECHANISM IT GUARDS

`nOtherMergeLang > 0` is conv-fw's own marker for *"the derivation is UNSOUND for this artifact."*
It fires on **2** rows. Ordinal cites appear on **4**. It misses:

- **sk#136 — 5 cites, flag 0.** `Same root cause and fix as index 3` / `Same race as index 2` /
  `Same underlying race as index 1` / `Same code site and mechanism as index 0` /
  `disposed by the same fix as index 0`
- **sk#230 — 3 cites, flag 0.** `Same underlying defect as index 0 (same file/line)` /
  `Same fix as index 0 resolves both` / `as well as index 0's before closing either`

> **A guard that declares the measurement invalid fails in the same direction, by the same
> mechanism, as the measurement it guards.** It reads "sound" on exactly the rows whose phrasing
> its sibling regex also cannot see. Both are keyed to the same two-phrase vocabulary, so they do
> not constitute independent checks — one lexical assumption produces both the number and its
> warranty.

## THE SIXTH PHRASING conv-fw PREDICTED — there are at least 16, and a THIRD reference syntax

Verbatim, every distinct cross-reference form in this corpus. **None matches `RE_INDEXED`.**
Two were already on conv-fw's list of five (marked ✓).

```
sk136[0]  Same root cause and fix as index 3
sk136[1]  Same race as index 2; one fix resolves both
sk136[2]  Same underlying race as index 1
sk136[2]  one fix should close both 1 and 2            <-- ⛔ BARE INTEGERS, no marker at all
sk136[3]  Same code site and mechanism as index 0
sk136[3]  disposed by the same fix as index 0
sk199[0]  Root-cause-identical to #3; fix once
sk199[1]  Root cause shared with #5
sk199[3]  Same defect site and mechanism as #0         <-- ✓ on their list
sk199[3]  Dispositioned identically to #0
sk199[4]  a materially narrower attack surface than #2's
sk199[5]  Same root defect as #1
sk199[5]  the skeptic itself rated the identical underlying defect 'high' in #1
sk199[5]  Fix once, alongside #1
sk230[1]  Same underlying defect as index 0 (same file/line)
sk230[1]  Same fix as index 0 resolves both
sk230[1]  as well as index 0's before closing either
ts326[0]  Duplicate of index 4 (same file/line)        <-- ✓ on their list
ts326[0]  Reconciling against index 4's skeptic-corrected 'high'
ts326[1]  Duplicate of index 5 (same file/line)
ts326[1]  Reconciling this entry's 'medium' against index 5's 'high'
ts326[2]  Duplicate of index 6, same mechanism
ts326[3]  Duplicate of index 7, same mechanism
```

**⛔ `sk136[2]`'s *"one fix should close both 1 and 2"* is the one that settles it.** The reference
degenerates to **bare integers in running prose** — no `index`, no `#`, no marker of any kind. It is
invisible to `RE_INDEXED`, invisible to `RE_OTHER`, and invisible to **my own** mechanical net. A
rule that caught it would have to match every integer in every rationale.

**Also note two cites that are references but NOT merge claims** (`Reconciling against index 4's
… 'high'`, `narrower attack surface than #2's`): the arbiter cites ordinals to compare severities
and to contrast scope, not only to merge. So a count of ordinal cites is **not** a count of merges
in either direction — it over-counts comparisons and under-counts bare-integer merges. Which of
these 23 is a merge is conv-fw's call, not mine.

## ⚠ MY OWN INSTRUMENT OVER-MATCHED, AND I CAN NAME EXACTLY WHERE

My mechanical rule `(?:index|#)\s*\d+` reported **15 rationales on 5 rows**. One row is a false
positive:

```
ts349-8c2070fd8a[4]  ...the four checks billed as 'fully discriminating' for #1018...
ts349-8c2070fd8a[4]  ...a real regression in #1018 arithmetic could ship past this gate...
```

**`#1018` is the GitHub issue the test targets — a ticket number, not a finding ordinal.** The
corrected figures are **14 rationales on 4 rows**, and the uncorrected 15/5 is what a plausible
"count the ordinal cites" rule reports if nobody reads the matches. Same shape as the thing being
measured: a lexical rule keyed on a marker that two different namespaces share.

## WHAT THIS DOES AND DOES NOT SUPPORT

**Supports, with much more force than "a sixth phrasing exists":** conv-fw's conclusion that the
distinct count has to come from the **producer as a field**. 16 forms, three reference syntaxes
(`index N`, `#N`, bare integer), canonical regex 0/23, and the unsoundness flag non-independent of
the number it warranties.

**Does NOT support any replacement predicate, and I am proposing none.** Widening a regex to the 16
now known is fitting it to this sample — conv-fw's own argument, and the bare-integer case shows the
limit is not where the sample ends.

**The three columns' value as a `PHRASING VARIETY` measure, which conv-fw retained them for, is the
one claim this data qualifies:** as a variety measure they read **5** where the mechanical floor is
**14**, because the variety is measured by the vocabulary whose insufficiency is the finding.
