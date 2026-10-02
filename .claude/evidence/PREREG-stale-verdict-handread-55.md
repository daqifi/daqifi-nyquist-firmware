# PRE-REGISTRATION — hand-reading the 55 unassessable rows for stale audit verdicts

**Written and pushed BEFORE the population list arrives and before any row is read.** An unpushed
pre-registration is not a pre-registration.

**READ-ONLY. No label will be applied, no comment posted, nothing merged or opened.** nq-a's label
attempt was refused by its own permission gate; **I treat that refusal as binding on me too.**
Applying these labels needs an operator grant, and routing around another lane's permission refusal
is not something I will do regardless of who asks. I produce counts and a list; the application
stays with the operator.

## The taxonomy — GIVEN, not invented by me

| class | wants | note |
|---|---|---|
| **stale PASS** | `needs-audit` | head changed since the last audit, prior findings closed |
| **stale BLOCK** (`keep_fixing` counts as BLOCK) | `audit-stale` | |
| **names no SHA at all** | neither — report the count | a *separate and worse* bucket |
| **carry-forward EXEMPT** | nothing | `mark-audited.sh --carry-forward-from` legitimately names an earlier commit with the gap proven inert — **complying, not stale** |
| **AMBIGUOUS** | nothing | added by me: a row I cannot conclude on even by hand |

## ⛔ THE DIRECTION OF MY OWN ERROR, AND WHAT I OWE FOR IT

**I want to find stale rows.** A pass that returns "9 already known, nothing new" makes the task
look unnecessary; a pass that finds more validates it. That biases me toward **calling rows STALE**,
and toward resolving ambiguity *into* a class rather than leaving it ambiguous — because a large
AMBIGUOUS count reads like I could not do the job.

**The coordinator pre-empted exactly that:** *"I would rather have it large and true than small and
tidy."* So:

1. **Every STALE call must quote BOTH heads** — the one the verdict names and the live `headRefOid`
   — **and quote the sentence that makes the comment a verdict**, not just the SHA. A row I cannot
   quote that way is AMBIGUOUS, not stale.
2. **Every CURRENT call needs only the two heads**, no speech-act quote. The asymmetry is deliberate:
   CURRENT is the direction I am not biased toward.
3. **AMBIGUOUS is a legitimate terminal state and I will not minimise it.** If the count is large,
   that is the finding — it measures the citation grammar, which is the thing actually being
   assessed.
4. **I will check carry-forward BEFORE calling anything stale**, since that class is
   indistinguishable from stale on the SHA comparison alone and the error would be an accusation.

## Method — and how each of the six named traps is handled

**"The newest comment that IS a verdict"** is the whole difficulty. A verdict comment must
**assert a gate outcome about a named head**. Three things that contain audit vocabulary and are
*not* verdicts:

- a **pointer** (*"Code review … updated up to the latest commit"*)
- a **label notice** (*"applying `blocked:audit-findings`"*)
- a **correction or retraction** about another comment (*"Correction to the comment above: the merge
  did not proceed"*)

| trap | handling |
|---|---|
| **1. mention vs act** | require a gate-outcome assertion + a head; read the comment's own heading, not a keyword hit. **This is the discuss-vs-is category and no pattern closes it — it is why this is a hand read.** |
| **2. no verdict KINDS from any sweep** | kind is re-read from the body/heading on my side. I have asked nq-a not to send kinds and will not use them if sent. |
| **3. md5 vs short SHA** | bind every SHA to its adjacent word; prefer full 40-hex; **never infer from a 7-char token.** A 12-hex token with `md5`/`crc32` beside it is not a commit. |
| **4. `updated_at` is the pointer edit** | the body's own SHA marker is the only authority for which commit a verdict describes. Timestamps order comments; they never date an analysis. |
| **5. thread state is not evidence** | `isOutdated` tracks the diff, `isResolved` is cleared by a push. Neither enters the classification. |
| **6. anchor every grep to a delimited field** | no bare numbers over TSVs — `318352` convicting conv-ts is the standing example. |

**Supersession:** a verdict can be withdrawn by a later comment. So "newest verdict" is the newest
comment that asserts a gate outcome **and has not been retracted by anything after it** — which
means reading forward past the candidate, not stopping at it.

**SHA comparison:** against the live `headRefOid` re-read at classification time, compared as **full
40-hex**. Never short-vs-short, never short-vs-long.

## Triage order, pre-registered because I may not finish 55

**Stale-PASS-capable rows first.** The asymmetry is the coordinator's and it is correct: **a stale
BLOCK is self-protecting; a stale PASS licenses an unaudited merge.** So rows whose newest verdict
asserts PASS/clean are read before rows asserting BLOCK/`keep_fixing`.

**If I stop early I will state the covered population exactly** — which rows were read, which were
not — rather than reporting a rate over an unstated denominator. A partial pass with a named
denominator is usable; a percentage over "most of them" is not.

## What I will report

The four counts, the newly-found stale rows with verdict kind and **both** heads, the no-SHA count,
the carry-forward count, and **the number of rows I could not conclude on even by hand.** That last
number is the honest measure of the citation grammar and I am committing in advance not to shrink it.

## What would make me stop and hand back

- If nq-a cannot supply the population, I report it unavailable rather than rebuild the extractor
  that was correctly abandoned. **A reconstructed population would be the broken instrument wearing
  a hand-read label.**
- If I find I am involved in a row (authored, audited, or hold a prior written conclusion about it),
  I declare it and do not classify it — the ts#352 case established that the row-side check clears
  me while my own ledger convicts me, so I check my ledger per row, not just the row.
