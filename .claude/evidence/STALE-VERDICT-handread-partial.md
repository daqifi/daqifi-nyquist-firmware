# Stale-verdict hand read — partial, with the covered population stated

Pre-registered at `b856de3ffc5064debf6cf8a561dba5b98ef09e4a` **before** the population arrived.
**READ-ONLY: no label applied, no comment posted, nothing merged or opened.** nq-a's label attempt
was refused by its own permission gate and I treat that refusal as binding on me.

## COVERED POPULATION (stated exactly, not as a rate)

nq-a's suggested first triage: the NO-HEX rows. **Covered: ts#312, ts#371, ts#430, ts#432, ts#434,
ts#435, ts#441** (7 of the 8 identifiers nq-a listed under NO-HEX; `sk#131` NOT covered, no payload).
Plus a method-validation pass over my own 14 firmware payloads.
**NOT covered: the remaining 47 of the 55.**

⚠ nq-a wrote *"the 7 NO-HEX rows"* and then listed **8** identifiers (7 test-suite + sk#131).
Stating the discrepancy rather than picking one.

## ⛔⛔ THE NO-HEX BUCKET IS AN ARTIFACT OF ONE COMMENT, AND IT IS EMPTY OF STALENESS

**All seven rows' "newest audit posting" is the SAME comment**, posted 2026-10-01:

> `## conv-ts — applying `blocked:audit-findings`, citing this row's own text`

It contains **zero hex tokens** — and **it is a LABEL NOTICE, not a verdict.** It was matched
because the heading contains the string "audit", inside `blocked:audit-findings`. **That is trap 1,
mention-vs-act — nq-a's own named trap, firing on nq-a's detector across seven rows at once.**

The real verdicts sit **one comment earlier** and every one of them names a **full 40-hex head**:

| row | real verdict (heading, verbatim) | verdict head | live head | class |
|---|---|---|---|---|
| ts#430 | *"Audit round 2: BLOCK at `63ac3cd1…`"* | `63ac3cd1e1a9…` | same | **CURRENT** |
| ts#432 | *"Audit round 2: BLOCK at `392ad3e0…`"* | `392ad3e0aae7…` | same | **CURRENT** |
| ts#434 | *"Audit: BLOCK at `7360c99a…` — 3 confirmed"* | `7360c99ab455…` | same | **CURRENT** |
| ts#435 | *"Audit round 2: BLOCK (DEGRADED AGAIN) at `7c8200f4…`"* | `7c8200f43898…` | same | **CURRENT** |
| ts#441 | *"conv-ts round-1 audit fix at head `7e1a751d…`"* | `7e1a751d8bd7…` | same | **CURRENT** |

**5 CURRENT · 0 STALE · 1 needs further reading (ts#312) · 1 CONFLICTED-OUT (ts#371).**

### ⭐ THE MECHANISM, which is worth more than the five rows

> **A bulk label-notice sweep posted across many rows becomes the NEWEST COMMENT on all of them
> simultaneously. If its template contains the trigger vocabulary, it displaces every real verdict
> at once — fleet-wide, from a single comment type, on a single day.**

So this was never 7 independent unparseable citations. It is **one template × 7 rows**, and the
citations underneath were the most parseable in the entire corpus: unambiguous full 40-hex, bound to
the word "at", in a heading that states the gate outcome. **The bucket nq-a ranked as the compound
worst case is the cleanest one.**

**Consequence for the remaining 47:** the newest-comment heuristic is unreliable wherever a bulk
sweep has run, and three bulk sweeps are known — this label notice, the 2026-10-01 base refresh
(17 rows), and conv-fw's *"SKIPPING — already parked"* notices. **Any row touched by one needs the
newest comment chosen by speech act, not by timestamp.**

## ⭐⭐ TWO OF THE NINE KNOWN-STALE ROWS WERE MADE STALE BY TODAY'S SANCTIONED REFRESH

**ts#305 and ts#316** appear in both nq-a's hand-verified-stale 9 and my list of rows carrying the
2026-10-01 base-refresh comment. **The refresh comments print the transition themselves:**

```
ts#305   before 0c3a1ec5de2b591da8be2a92251d1219076c2f97
         after  b894cb26a8b5a55c8fe85b3da1e741b939d896d7
ts#316   before 1620a06369ffc0a90d567118678ea030520368c5
         after  eccc3aa07b2a270a079aaf462aae17580010cddf
```
and nq-a's cited verdict head is the **before** in both, with the **after** live.

**This is not a defect in the refresh.** The operator ruled that a park blocks *merging*, not
*refreshing*; the lane did it correctly and documented it to the byte. **The staleness is a
mechanical consequence of authorised hygiene.**

> **The stale-verdict backlog is partly MANUFACTURED BY SANCTIONED WORK — and the most
> machine-readable citation in a corpus whose whole problem is unreadable citations is the very
> comment that records the staleness.**

The remedy is therefore cheap and already 90% present: a refresh comment that prints before/after
**already is** the staleness record. It needs no extractor — it needs the row to say that the prior
verdict's pin is now historical. **And at least 15 more refreshed rows exist** (17 found, 12
identified), so this is a population, not two cases.

## ⚠ MY OWN METHOD FAILED THE VALIDATION PASS, IN TRAP 1, EXACTLY AS WARNED

Run over my 14 firmware payloads where I already knew the answers by hand, my automated
verdict-locator picked:

- **fw#1101** → *"## Hardware acceptance: **FAIL**"* — a **bench result**, not an audit verdict
- **fw#1110** → *"**SPLIT DECIDED. This PR becomes PR B**"* — a **scope decision**
- **fw#1137**, **fw#1130** → **park comments** that happen to recite a head (right answer, wrong reason)

**So the pre-filter cannot identify verdicts and I will use it only to LOCATE candidates, never to
classify.** This is the positive case for the hand read rather than an argument against it, and it
is the same failure as nq-a's — two independent detectors, same trap, because content type is what
both regexes and readers default to.

## CONFLICTS DECLARED — not classified by me

- **fw#1124** — its round-2 BLOCK is at `c9309decb0b0…` and the live head is `04dc1b02b7e3…`,
  **which is MY base-refresh push.** So it is almost certainly stale **and I caused the staleness.**
  Declared, not classified.
- **ts#371** — its real verdict reads *"PARKED (worker, lane nq-b)"*. **My lane parked it.**
  Declared, not classified.

⭐ **nq-a disclosed the identical shape on ts#415** (*"I caused its staleness … I am conflicted on
that row and excluded it from my nine"*). **With fw#1124 and ts#371 that is two lanes, three rows,
same mechanism: the lane doing hygiene creates the staleness it would then be asked to assess.**
Taken with ts#305/ts#316 above, **refresh-induced staleness is the dominant known cause, not a
footnote.**

## The len39 token

nq-a flags a 39-character hex inside the 55 and warns a non-match there is **corruption, not
staleness**. **Not encountered in the 7 rows I covered.** Still outstanding in the uncovered 47.

---

# ROUND 2 — hand-verifying nq-a's 13 stale CANDIDATES (detector output, not hand-verified)

Covered this round: **ts#312, fw#976, fw#991, fw#1013**. Live heads re-read at classification time.

## ✅ ts#312 — **STALE BLOCK** → `audit-stale`

Meets the pre-registered standard: both heads **and** the sentence that makes it a verdict.

> *"Final audit at `e1a53c6` — `gate: BLOCK`, and the PR stays parked"* (2026-09-10T20:28:55Z)

cited `e1a53c6` · live `88161000dbc50c89384c7281d1867b6b9f0e2a40`. **Supersession checked forward:**
six later comments, **none a verdict** — two hardware A/B results, a self-correction about a
frequency claim, a repeat-runs report, and the conv-ts label notice.

⚠ **A 7-HEX PREFIX COMPARE IS SOUND HERE AND WOULD NOT BE FOR THE OPPOSITE CALL.** 7 hex = 28 bits.
Differing prefixes **prove** the full SHAs differ, so STALE is sound. Equal prefixes do **not** prove
equality. **So every STALE call on a short SHA is sound and every CURRENT call on one is not** —
which means short-SHA CURRENT verdicts anywhere in this sweep are weaker than their STALE siblings,
and that asymmetry is invisible in a table that prints both the same way.

## ✅ fw#1013 — **STALE BLOCK** → `audit-stale`. nq-a's cited head is CORRECT.

Newest of three verdicts: `7ae8f0547946b3554364bfddd3d2650b2d8e4f34` BLOCK (2026-09-11T14:36:32Z);
live `93eaf7cf469878fecafbf4dbd78a510d0936cffa`. Independent read agrees with the candidate.

## ⛔ fw#976 — **STALE BLOCK**, but nq-a's CITED HEAD IS WRONG

**Verdict stands, citation corrected.** nq-a's own suspicion was right and the row says so outright:

> *"**What changed after that audit:** one commit, `578f32be8850dc603d57631229e2dfc083f277fa`
> **(the current head, == `headRefOid`)**"*

`578f32be8850` is the commit that landed **AFTER** the audit — self-labelled as such. It was bound
as the audited head only because "audit" sits in the sentence. **Mention-vs-act, seventh instance.**

**The real newest verdict** (2026-09-15T07:26:10Z): `1824ab15f26946868904d97b213bb32bbf6c3816`
BLOCK. Live: `d31f63c7a55f1b3d2f7f1225d8cd94bd597103c1`. → **STALE.**

⚠ **And nq-a's two messages disagree with each other about this row:** their citation-format sample
listed fw#976 as *"**Attestation:** head `1824ab15f269…`"* — **the right head** — while their
stale-candidate list cited `578f32be8850`. Same lane, same hour, same row, two SHAs. **A wrong
cited head in a stale report is worse than no report: it sends the reader to a commit that is not
the audit's, and the lookup succeeds.**

## ⚠ fw#991 — **AMBIGUOUS**, and it resists all four classes for two independent reasons

My proximity pattern found **zero** verdicts here — because the citation is in a **table cell** and a
proximity window cannot cross `|`. **nq-a warned me of exactly this and I built a proximity pattern
anyway.** Hand read:

```
| Audited head | 580e07140475161b5599b4d83cb353deaae451fb |
| Audited head | 590ecfd814295c448268223659ef9b07e4874b8c | = the live head |   <- nq-a's cite
live head NOW   a66aadb2baedf74fa98797600e154b652a102a70                        <- neither
```

**1. A CARRY-FORWARD PROOF IS PINNED TO A COMMIT *PAIR*, AND THIS ONE NO LONGER REACHES THE HEAD.**
The row makes the exemption argument properly and checkably:

> *"| Delta since the audited head | **docs only, and checkable:**
> `git diff 580e0714..2dd6e26220d0… --name-only -- '*.c' '*.h' '*.mk' Makefile 'tests/**'
> '.github/**'` returns **empty**"* … *"since the audited SHA touches no executable line, the prior
> audit still covers"*

**But the proof covers `580e0714..2dd6e262`, and the live head is a THIRD commit, `a66aadb2`.** So
the gap proven inert is **not the current gap.**

> ⭐ **A carry-forward exemption goes stale exactly the way a verdict does — and less visibly,
> because it LOOKS like compliance.** `--carry-forward-from` is EXEMPT only while the proven range
> ends at the live head; re-derive the range, never inherit the exemption.

**2. The row carries a PASS attestation and a "did not run" BLOCK simultaneously:**
```
| Adversarial audit verdict | **PASS**, `gateReason: clean`, 0 findings, provenance present, …
### The gate says BLOCK. Read this before merging.
gate: BLOCK  gateReason: "no adversary model in the fallback chain was available — audit did not run"
```
**An arbiter-less PASS is ABSENT, not a prior round.** Which of these is "the newest verdict" is not
decidable from the text alone, so the row is **AMBIGUOUS** and I am not forcing it into a class.

## Running totals over everything I have hand-read

| class | n | rows |
|---|---|---|
| **CURRENT** | 5 | ts#430 · ts#432 · ts#434 · ts#435 · ts#441 |
| **STALE BLOCK** → `audit-stale` | 3 | ts#312 · fw#976 · fw#1013 |
| **STALE PASS** → `needs-audit` | 0 | — |
| **names no SHA at all** | 0 | the NO-HEX bucket was an artifact |
| **CARRY-FORWARD (claimed, range expired)** | 1 | fw#991 |
| **AMBIGUOUS — could not conclude by hand** | 1 | fw#991 (same row, both reasons) |
| **CONFLICTED, declared not classified** | 2 | fw#1124 (I authored its live head) · ts#371 (my lane parked it) |

**Covered: 11 rows. NOT covered: 44 of the 55.** No stale PASS found yet — every stale row so far is
a BLOCK, which is the self-protecting direction.

---

# ROUND 3 — the stale-PASS-capable rows, prioritised as briefed

## ⛔⛔ FIRST: MY OWN ts#352 REPORT WENT STALE WITHIN THE HOUR

I measured ts#352's live head at `99be2a21673a0c2e1a8ba6e669205fe61b3d5eee` and reported to the
coordinator that it was mechanically mergeable there. **It is now `1fb9c01c142131f40fc5f65d45a3881d07d80b7e`.**
nq-a's cached figure was right and my fresher measurement is the stale one.

**So the failure this entire task is about happened to my own output, on the row I was asked to
audit, inside one hour.** Two consequences, both load-bearing:

1. **It retroactively validates declining that audit.** Had I run it, the verdict would have been
   pinned to `99be2a21` and **void before I could post it** — the 130s/258s drift class, not the
   74/195-minute one.
2. **The two open Qodo threads I found at `99be2a21` are unverified at `1fb9c01c`.** I am not
   carrying that finding forward without a re-read.

> **A staleness report is itself a timestamped artifact and decays at the same rate as what it
> measures.** Every head in this document is "as at the moment of classification" and nothing else.

## The dangerous class, hand-read — and it is NOT where the staleness is

| row | newest PASS-bearing verdict | verdict head | live head | class |
|---|---|---|---|---|
| **ts#447** | *"conv-ts — adversarial audit **PASS** at `d82d37e6…`, staged at READY TO LAND (not landed)"* | `d82d37e6e2bd…` | **same** | **CURRENT PASS** |
| **ts#436** | *"a clean audit PASS, a met condition, no blocking label"* | **none on any gate line** | `cfbb428b8536…` | **UNREADABLE** |
| **ts#449** | *"`gate: PASS`, clean"* | **none on any gate line** | `a749b24a7f0e…` | **UNREADABLE** |
| **fw#1137** | round-2 PASS | `4b8f3dbf98…` | **same** | **CURRENT PASS** |
| **ts#316** | — see below, **NOT a PASS** | | | **STALE BLOCK** |
| ts#464 | nq-a's flagged false positive | — | — | not a verdict |

**0 confirmed stale PASS. 2 current PASS. 2 PASS-claiming rows whose citation cannot be read.**

### ⭐⭐ AND THE DANGEROUS CLASS IS CONCENTRATED IN THE UNREADABLE BUCKET

**The only two rows claiming a clean PASS without a readable head are exactly the two in nq-a's
genuinely-unreadable 17.** ts#436 states the hazard about itself, verbatim:

> *"**This is the worst state a PR can be in:** a clean audit PASS, a met condition, no blocking
> label"*

So the rows where a stale PASS would do the most damage are the rows whose verdict head **cannot be
compared at all** — the same anti-correlation as the bare-`noProvenance` detector, which failed
hardest where the evidence was best. **An unreadable citation is not merely an inconvenience here;
it is concentrated on the class that licenses an unaudited merge.**

## ts#316 — nq-a's stale call CONFIRMED, its kind CORRECTED, and the row diagnosed itself

Newest real verdict (2026-09-11T06:13:30Z): *"**PARKED at the round cap — one verified blocking
finding, filed as #340**"* → **BLOCK-side, not PASS.** cited `1620a06369ff…` → live
`eccc3aa07b2a…`. **STALE BLOCK → `audit-stale`.**

⭐ And three weeks before any sweep, the row said it itself:
> *"**Worker gate: this is NOT converged — the clean verdict covers the PREVIOUS head**"*
> (2026-09-11T04:28:05Z)

**A row that detected its own stale verdict, stated it plainly, and was still counted as
unassessable by two detectors and one hand reader.**

## ⚠ MY KIND-TRIAGE INSTRUMENT FAILED TWICE, BOTH TOWARD THE DANGEROUS CLASS

**Failure 1 — a gate word needs the same binding as a SHA, and I only bound SHAs.** My first pass
called ts#316 a PASS because it matched **`SHAPE_HALF_MUTE_PASS`** — a Python test constant. `PASS`
inside an identifier is not a gate outcome. I wrote trap 3 ("bind every SHA to the adjacent word")
into my own pre-registration and **did not apply the same rule to the verdict word.**

**Failure 2 — and binding does not fix it: a ROW-LEVEL COUNT CANNOT ANSWER A NEWEST-COMMENT
QUESTION.** With the gate word properly bound, ts#316 still reads "PASS-bearing": 2 bound PASS **and**
3 bound BLOCK, because it genuinely went PASS at an earlier head and BLOCK at the cap. **Aggregating
over a row flattens the chronology** — nq-a's "grepping a thread flattens runs into one namespace",
arrived at from the opposite direction.

> **There is no cheap kind-triage. The kind must be read from the newest verdict comment, per row.**
> Three of my instruments failed on this task and all three are one family: **answering a
> per-comment, chronology-sensitive question with a row-level pattern.**

Both failures over-reported PASS — **the class I was told to prioritise**, so the error inflated the
apparent size of the dangerous bucket rather than hiding it. That is the safe direction for triage
and the wrong direction for a count.

## Running totals — 16 rows hand-read, 39 of 55 not covered

| class | n | rows |
|---|---|---|
| **CURRENT** | 7 | ts#430 · ts#432 · ts#434 · ts#435 · ts#441 · **ts#447 (PASS)** · fw#1137 (PASS) |
| **STALE BLOCK** → `audit-stale` | 4 | ts#312 · ts#316 · fw#976 · fw#1013 |
| **STALE PASS** → `needs-audit` | **0** | — |
| **UNREADABLE, PASS-claiming** | 2 | **ts#436 · ts#449** ← the dangerous bucket |
| **carry-forward, range expired** | 1 | fw#991 |
| **AMBIGUOUS** | 1 | fw#991 |
| **CONFLICTED, declared not classified** | 2 | fw#1124 · ts#371 |

**Every stale row found by hand is a BLOCK — the self-protecting direction.** PASS is ~10% of
verdict-bearing rows across 91 payloads, which is consistent with a structural reason: **a PASS row
gets merged, so it does not sit long enough to go stale.** Stated as a tendency, not a universal.

---

# ROUND 4 — the "genuinely unreadable 17", read first per the adopted inversion

## ⛔⛔ THIS BUCKET IS ALSO LARGELY AN ARTIFACT — OF A 40-HEX-ONLY CRITERION

nq-a's bucket is defined as *"no audit line carrying a 40-hex anywhere."* **That is not the same
predicate as "unreadable."** Of the 10 rows in it I could classify:

| row | citation found, bound to an audit word | len | live | class |
|---|---|---|---|---|
| **fw#1040** | `\| head \| 77b658dfe512bdce8944bdfb5d050363aadb8265` | **40** | same | **CURRENT (exact)** |
| **ts#429** | *"PARKED at `7c8401765431829ec15e0383479aa7fd9892881f`"* | **40** | same | **CURRENT (exact)** |
| fw#965 | *"anchored at the current head `5fffcb140`"* | 9 | `5fffcb140a5e…` | CURRENT? prefix only |
| sk#166 | *"Adversarial audit round 3 at `48017b5`"* | 7 | `48017b5a8641…` | CURRENT? prefix only |
| sk#167 | *"Adversarial audit round 4 at `52958ffe` — BLOCK"* | 8 | `52958ffef241…` | CURRENT? prefix only |
| sk#93 | `\| head \| 351f8a9` | 7 | `351f8a9f34aa…` | CURRENT? prefix only |
| ts#341 | *"Re-audit at `9a039b0` … PARKED at the cap"* | 7 | `9a039b004641…` | CURRENT? prefix only |
| **fw#704** | **none at any length** | — | `…` | **NAMES NO SHA** |
| **sk#131** | **none at any length** | — | `…` | **NAMES NO SHA** |
| **ts#437** | **none at any length** | — | `1b7f6f100111…` | **NAMES NO SHA** |

**7 readable · 3 genuinely nameless · ZERO STALE.**

**And two of the seven carry a FULL 40-hex — both in TABLE ROWS**, which is the blind spot nq-a
predicted in the same message that mis-bucketed them: *"the citation lives in a TABLE as often as in
prose, which is why a proximity-window regex fails."* **The warning and the error shipped together.**

> **"No 40-hex anywhere" is a property of the SEARCH, not of the row.** Five of these rows cite a
> head at 7-9 hex, bound to an audit word, perfectly readable. The bucket measured hex length and
> was reported as readability.

⚠ **AND I WILL NOT UPGRADE THE FIVE PREFIX MATCHES.** Per my own finding earlier in this document: a
prefix compare **proves inequality** and **cannot prove equality**. 7 hex = 28 bits. So these five
are **CURRENT-consistent, not CURRENT-proven**, and the asymmetry is the whole point — had any
differed I could have called STALE soundly. **Every short-SHA CURRENT in this sweep, mine included,
carries that caveat.**

## Two rows that are not audit-staleness cases at all

- **fw#704** — newest record is *"Parked — needs a DS18B20 + 4.7 kΩ pull-up on a DIO channel"*. A
  **hardware-procurement** park with no audit in it. Pairs with ts#97's identical condition, so this
  is a companion pair whose hold is a part nobody has. **An `audit-stale` label would misdescribe
  it.**
- **sk#131** — newest is *"Audit provenance, stated because a clean result is only clean if the
  auditor ran. The first codex invocation returned `findings: []` with `codex_exi[t]`…"* — a
  **degraded/void** run. **A void run has no head to be stale about**, which is a different absence
  again.

**So "names no SHA" is at least three distinct states**: never-audited (fw#704), audit-void
(sk#131), and verdict-without-citation (ts#437). The taxonomy's worst bucket is itself unpartitioned.

## ⛔ I AM CONFLICTED ON 7 OF THE 17 — INCLUDING THE ONE THAT MATTERS MOST

Graded, not counted. Involvement sweep with the **repo prefix required**, because a bare `#NNNN`
convicts across repos:

**CONFLICTED, declared, NOT classified:** fw#1115 (I hold its audit worktree) · sk#136 (19 prefixed
hits; a defect committed in my own shell) · ts#344 (34) · ts#436 (27) · ts#371 (my lane parked it) ·
ts#352 (prior written conclusion) · fw#901 (my own census formed a conclusion about its state).

> ⛔ **ts#436 is the single row in this bucket claiming a clean PASS — *"This is the worst state a PR
> can be in: a clean audit PASS, a met condition, no blocking label"* — and it is one I cannot
> classify.** The pool-exhaustion mechanism lands exactly on the highest-risk row in the
> highest-risk bucket. **That is not bad luck; it is the mechanism working as described.**

### ⚠ And a COUNT scored a DISCLAIMER as involvement

My first involvement pass flagged **11 of 17**. Grading the markers cut it to 7. The clearest case:
**fw#965's only prefixed mention is *"ownership query: fw#965 is NOT this lane's; released to
nq-c"*** — an explicit disclaimer of ownership, **counted as evidence of involvement.**

> **A lane-name count cannot distinguish a claim from a disclaimer, and a disclaimer is evidence of
> NON-involvement.** So counting over-convicts in a specific, knowable direction — and the fix is to
> grade the marker (CLAIM / AUDIT / CENSUS / DECLINED), never to count it.

## CUMULATIVE — 26 rows hand-read

| class | n | rows |
|---|---|---|
| **CURRENT (exact 40-hex)** | 9 | ts#430 · ts#432 · ts#434 · ts#435 · ts#441 · ts#447 (PASS) · fw#1137 (PASS) · fw#1040 · ts#429 |
| **CURRENT-consistent (prefix only, NOT proven)** | 5 | fw#965 · sk#166 · sk#167 · sk#93 · ts#341 |
| **STALE BLOCK** → `audit-stale` | **4** | ts#312 · ts#316 · fw#976 · fw#1013 |
| **STALE PASS** → `needs-audit` | **0** | — |
| **NAMES NO SHA** (≥3 distinct states) | 3 | fw#704 · sk#131 · ts#437 |
| **UNREADABLE, PASS-claiming** | 2 | ts#436 (conflicted) · ts#449 |
| **carry-forward, range expired / AMBIGUOUS** | 1 | fw#991 |
| **CONFLICTED, declared not classified** | **8** | fw#1115 · fw#901 · fw#1124 · sk#136 · ts#344 · ts#352 · ts#371 · ts#436 |

**Could not conclude even by hand: 3** (fw#991 ambiguous · ts#436 and ts#449 unreadable-PASS).
**Conflicted-out: 8.** **Still uncovered: ~21.**

**Zero stale PASS across 26 hand-read rows.** Every stale row is a BLOCK — the self-protecting
direction — and the only PASS-shaped risk in the fleet reduces to two rows whose heads cannot be
read, one of which my lane cannot assess.
