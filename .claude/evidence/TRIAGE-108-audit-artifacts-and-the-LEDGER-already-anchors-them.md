# The retention question collapses from 191 to **3** — because a durable 272-row ledger already anchors the rest

Produced 2026-10-13, running nq-c's head-currency discriminator on my own 108 artifacts at their
explicit request (*"worth you running the same split on your 108 rather than taking my ratio"*).
**All actions here are on my OWN artifacts and append-only; no other lane's files were modified.**

## 1. The split, and nq-c's ratio holds

```
108 artifacts in /mnt/c/daqifi/wt/nq-b/.claude/
 ├── 15  LOAD-BEARING   row OPEN *and* artifact at the LIVE head   (5 of them failed runs)
 ├── 36  stale-open     row OPEN, artifact at a superseded head     = round HISTORY
 ├──  0  unanchorable
 └── 57  HISTORY        row MERGED/CLOSED        (sampled 6/6 -> all MERGED, incl. all 5 bare numbers)
```

**10 non-error load-bearing of 108 = 9.3%; nq-c's was 3/47 = 6.4%.** Same order, independently
derived. Extrapolated over the 191, the fleet's load-bearing set is **a dozen to twenty files** — so
*"preserve 191"* was the wrong frame, as nq-c said.

⛔ **But my first pass said SEVEN, and the error was mine.** I planned a fallback — *the head is in the
filename* — and did not implement it, so 24 artifacts went into an `OPEN-but-NO-head_sha` bucket that
**looked like a finding about the artifacts and was a defect in my reader.** Implementing it moved 8
into LOAD-BEARING: **the unimplemented version undercounted the set it existed to protect by more than
half**, i.e. it would have under-preserved. ⭐ *An instrument's own gap, presented as a property of
the thing measured* — the week's shape, third instance today.

## 2. ⭐⭐ THE FINDING THAT CHANGES THE ANSWER: a durable registry already exists

`~/.claude/lanes/audit-ledger.tsv` — **272 rows back to 2026-09-15**, real schema:

```
utc  lane  repo#pr  round  head  run_id  agents  subagent_tokens  gate  confirmed/plausible/refuted
```

by lane: **nq-b 86 · nq-a 58 · nq-c 42 · operator 35 · conv-ts 33 · conv-fw 18.**

> **A ledger row carries the ANCHORED VERDICT — row, round, head, gate, and the finding counts — and
> it lives outside every worktree.** So *"this artifact is the only record of an open row's current
> verdict"* is **false wherever a row exists at that head.** Testing all 15 load-bearing artifacts
> against it: **7 are already ledgered at their exact head.** What dies with a worktree is not the
> verdict, it is the **detail** — the findings text and the arbiter's reasoning.

**So the genuinely-only-record set is THREE: ts#338, ts#339, ts#344.**

⛔ **Two instrument errors on the way, both mine, both the kind I have filed before:**
1. I first grepped the ledger for `ts464` — **my filename convention** — and got **0**, on a row I
   audited 7 times. The ledger spells it `daqifi/daqifi-python-test-suite#464`, which returns **4**.
   *Searching by what I call a thing instead of what it asserts.*
2. The ledger's `head` field is **7 chars in 265 rows and 9 in 7 rows**; I compared full 9-char heads
   with `grep -F` and got structural zeros. **A short-vs-full SHA comparison — the exact thing my own
   definition-of-done forbids.** Fixed with prefix matching in both directions.

## 3. ⛔⛔ AND THE THREE WERE A "THIRD SCHEMA" THAT ISN'T: the artifact is NESTED IN A WORKFLOW ENVELOPE

My reader reported them as missing `repo`, `gate`, `head_sha` and `confirmed`, with only
`agentCount / logs / result / summary / totalTokens / totalToolCalls / workflowProgress`. **That was a
nesting-depth error.** The whole standard artifact sits inside **`.result`**:

```
12 of 108 are WORKFLOW-WRAPPED        audit fields recoverable from .result:  12 / 12
filename head corroborated by the payload:  8 / 12        filename-only:  4 / 12
every wrapped one: gate=BLOCK (11) or PASS (1); arbiter.verdict = "keep_fixing" where present
```

> **I classified three artifacts LOAD-BEARING on a head and a gate read out of a FILENAME I wrote
> myself** — a self-assigned label standing in for a measurement. Unwrapping corroborated 2 of those 3
> heads in the payload and left ts#339's on the filename alone. **The conclusion survived; the method
> did not.**

⭐ **DIRECTLY ACTIONABLE FOR THE VALIDATOR, and it is the same family as fw#965's blocker.** The
coordinator reports its validator *"assumes `findings[]` and refuses outright"* on
`audit-965-r3.json`'s `confirmed[]/plausible[]` shape. **There is a second axis: the ENVELOPE.** A
validator must **unwrap `.result` before dispatching on schema**, or 12 of my 108 refuse for a reason
that has nothing to do with their contents. Shapes seen so far:

| axis | variants |
|---|---|
| schema | `findings[]` · `confirmed[]`/`plausible[]` (`adversarial-audit.js`) |
| envelope | bare · wrapped in the Workflow run result (`.result`, alongside `logs`/`totalTokens`) |

## 4. Actions taken — all append-only, all on my own records

- **10 non-error load-bearing artifacts copied** to `~/.claude/lanes/audit-<row>-<head>-nq-b.json`,
  `cmp`-verified byte-identical, **originals left in place** (10/10 present after).
- **The 3 unledgered rows BACKFILLED**, because my own dispatcher's rule is that a missing ledger line
  *"reads as 'no audit ran'"* — and these are three BLOCK verdicts at heads that are **still live**:

```
#338  round 6  0547bc559  BLOCK  3/0/0   agents 3  368,274 tok   head CORROBORATED
#339  round ?  2580c332   BLOCK  1/0/0   agents 3  272,389 tok   head FILENAME-ONLY
#344  round ?  2ca2c8209  BLOCK  4/1/0   agents 4  434,502 tok   head CORROBORATED
```

  Every value **measured, never inferred**: round 6 is quoted from ts#338's own `arbiter.summary`;
  the other two say nothing, so they carry **`round=?`**, not a guess. `utc` is the file mtime and
  **says so in the row**. Each row is tagged `BACKFILLED-2026-10-13-by-nq-b` with its head provenance
  and `envelope=workflow-wrapped(.result)`. Verified: **273 → 276 lines**, 11 fields each (matching
  conv-fw's existing rows), **no other lane's row count changed**, and all three now resolve.
- **Adopted conv-fw's `artifact=` + `sha256=` pointer convention**, which was used on **4 of 272 rows,
  all one lane** — now 7. ⚠ **That is the remaining systemic gap: 265 rows record a verdict with no
  path to the artifact behind it.** A fleet convention used by one of six lanes.

## 5. What I did NOT do, and why

- **No bulk commit of the other 97.** ~1.5 MB of JSON onto a `/mnt/c` repo whose **`git gc` is HELD by
  operator ruling** is unreclaimable bloat. (11 of my artifacts are already git-tracked, because
  `.git/info/exclude` only affects UNTRACKED files — so the path exists; it just should not be taken
  at scale.)
- **Nothing touched in another lane's worktree**, and nothing deleted anywhere.
- ⚠ **The 36 stale-open are NOT worthless** — nq-c's point, and it stands: they are complete round
  HISTORIES for live rows (ts#464 alone has 5), and round COUNT has been load-bearing here before,
  when a row's own most-explicit figure read *"1 of 5"* and was stale by three rounds. **Two retention
  cases with different values — the anchored verdict and the round history — and bulk framing hides
  the difference.** Not mine to decide for four lanes.
