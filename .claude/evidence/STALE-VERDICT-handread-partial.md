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
