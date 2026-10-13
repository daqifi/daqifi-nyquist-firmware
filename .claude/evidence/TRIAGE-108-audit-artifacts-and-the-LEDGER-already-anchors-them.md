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

---

# ADDENDUM: the check I did NOT run, and a defect in my own backfill that a peer's care exposed

## 1. ⛔ I tested only the 15 LOAD-BEARING against the ledger. nq-c tested their round history too.

Their result (6 of 13 stale-open already durable, because the ledger held **all three rounds** for
#965 and #384) said my *"the 36 stale-open are a second retention case"* claim was **unmeasured.**
Run on my 36, unwrapping `.result` per the envelope finding:

```
23  LEDGERED at this head          <- already durable; my claim was wrong for two thirds of them
10  row ledgered, but NOT at this head
 3  ⛔ NO LEDGER ROW AT ALL        <- all three are ts#306
```

**Per-row round-history coverage:** fw#1110 2/2 · ts#407 **5/5** · ts#448 3/3 · ts#386, #430, #432,
#436, #442 all complete — while **ts#338 1/5 · ts#344 1/3 · ts#464 3/6 · ts#306 0/2.**

> **So the retention gap in my round history is 13 artifacts across FOUR rows, not 36 across
> fourteen** — and the only row with **no audit record whatsoever** was **ts#306**, which I had
> audited twice. That is precisely the case my own dispatcher names: *a missing line reads as "no
> audit ran".*

**Backfilled ts#306's two rounds** — and **two of its three artifacts are ONE round in two forms**:
`de8a512` appears as a wrapped full run (6 agents, 980,303 tok) *and* as a bare extraction written
**3 h later**, with identical head, producer, gate and counts. ⭐ **The bare extraction has LOST
`agentCount` and `totalTokens`** — so extraction is lossy relative to the envelope, which is an
argument for keeping the wrapped form. Recorded in the row rather than filed as two rounds.

## 2. ⛔⛔ MY BACKFILL PUT ONE MEASUREMENT UNDER ANOTHER'S NAME — nq-c declined to and said why

I wrote the envelope's `totalTokens` (368,274 / 272,389 / 434,502) into the **`subagent_tokens`**
column. nq-c hit the same choice and refused it: *"the envelope carries `totalTokens`, which is NOT
`subagent_tokens`; substituting it would put one measurement under another's name."*

**They were right and the convention proves it:** `unknown` is the established value in that column —
**5 rows already use it, including all four of conv-fw's own `artifact=` rows.** So a correct value
was available and I invented one instead.

**Corrected:** field 8 → `unknown` in all three rows, with the real number preserved **honestly
labelled** in the notes as `totalTokens=<n>(workflow-envelope-total,NOT-subagent_tokens)`. Verified by
byte-comparison: **275 other lines sha256-identical, exactly 3 lines changed, line count unchanged.**

> ⭐ **A plausible fill is worse than a blank, because a blank is visibly unknown and a fill is
> silently wrong.** The same discipline I applied to `round=?` I failed to apply one column over —
> *in the same row.* Cf. the stated-but-unapplied-caveat shape from earlier today.

## 3. ⛔ And my first pointer was a PLACEHOLDER that resolved to nothing

I wrote `artifact=~/.claude/.../audit-ts306-…json` — a literal `...` — for artifacts that, unlike the
other ten, **had never been copied out of my worktree.** A dangling pointer is worse than no pointer:
it asserts recoverability that does not exist. Copied the three ts#306 artifacts (`cmp`-verified,
originals in place) and repaired both rows; **0 placeholders remain.**

**Then verified every pointer in the whole ledger, not just mine:**

```
10 of 278 rows carry a pointer (3.6%)      resolving: 10/10      DANGLING: 0
   conv-fw 4/18 · nq-b 5/91 · nq-c 1/43 · nq-a 0/58 · conv-ts 0/33 · operator 0/35
CONTROL: a deliberately bogus pointer reports False -> the test can fail
```

**Three of six lanes, 3.6% of rows — but every existing pointer is live.** The convention works; it is
simply unadopted.

## 4. ⭐⭐ Three successive triages, each ~10× and each cheaper than the action it averted

```
191  artifacts "at risk"        <- the framing that would have motivated a retention POLICY
 ~25  load-bearing              <- 10/108 and 3/47, independently derived in two lanes
  ~4  SOLE record               <- 1 nq-c, 3 nq-b ... and all four are now ledgered
```

> **The bulk framing was never wrong about the COUNT, only about what the count MEANT.** At no point
> was a policy needed. The answer was **a handful of rows in a TSV that already existed** — and the
> decisive move each time was *opening the thing we were reasoning about*. nq-c: *"I assumed, and I
> assumed because I had never opened it."*

⚠ **One limit on my method, from nq-c:** their two unanchorable artifacts are `-ERROR` runs with **no
head in the payload and no SHA in the filename**, so my filename-SHA fallback would not rescue them —
correctly, since an errored run carries no verdict to preserve. **The 24→8 recovery does not
generalise to error artifacts.**

---

# SECOND ADDENDUM: the fallback I PUBLISHED is 100% effective on my corpus and 0% on the next lane's

nq-c's census had dropped one file to a regex (`audit-(?:fw|ts|sk)?(\d{3,4})` matches `sk`, then `\d`
fails on `ills`, so `skills165` fell into **no bucket at all**). Running their preventive — *enumerate
the forms before writing the pattern* — on my own set exposed a worse defect in **my** work.

## The measurement

```
my published fallback  ^audit-(?:fw|ts|sk|skills)?\d+-([0-9a-f]{7,40})(?:[-.]|$)

  over nq-b's 108 artifacts :  108 / 108   (100%)      head position = <hex sha>      (107 + 1 oddity)
  over nq-c's  47 artifacts :    0 /  47   (  0%)      head position = r<N>           (46 + 1 r2-rerun)
```

> **The two lanes' naming conventions are MUTUALLY EXCLUSIVE in the head position.** `audit-965-r1.json`
> has no head in its filename at all — `r1` is a *round number*. So the fallback is not *"mostly
> portable"*; it is **perfect on the corpus it was derived from and inert on the next one**, and it
> fails in the **under-recovering** direction: no head → reported *unanchorable*.

**I published it to the coordinator and in this file as a general method.** ⭐ **A pattern carries its
corpus's conventions invisibly, and publishing it as a method hands over the conventions without the
corpus.**

⚠ **And my stated limit was too narrow, which is worse than no limit.** I told nq-c the fallback *"does
not generalise to error artifacts"*. True but trivially so: **it does not generalise to their naming at
all.** They accepted the narrower framing because it was consistent with their two error cases.
**A limit stated too narrowly reads as a surveyed boundary** — the reader cannot tell it was never
surveyed. Same shape as the stated-but-unapplied caveat, one level up.

## ⭐ Why my census survived the bug that bit theirs — and it was not the pattern

```
forms present in my set :  audit-ts 86 · audit-fw 15 · audit- 7     (no sk/skills at all)
my pattern vs skills165 :  MATCHES -- Python's alternation backtracks 'sk' -> 'skills'
                           but ONLY because I happened to list 'skills'; luck of listing, not design
the check that actually protected me:
  files on disk 108  ==  bucketed 108  ==  distinct bucketed 108     never-classified: 0
```

> **The partition summing to the population is what made a silent drop impossible** — and it is the
> cheap, pattern-independent guard. nq-c found their missing row precisely because their recount
> disagreed with `ls` by one. **Assert the partition sums to the population; do not trust the pattern.**

## Corrected scope of the fallback, for anyone who reads the method above

- ✅ Sound for artifacts named `audit-<row>-<hex sha>[-TAG].json` (nq-b's convention).
- ⛔ **Returns nothing for `audit-<row>-r<N>.json` (nq-c's convention) — 0 of 47.** For those the head
  must come from the payload: `head_sha`, or `.result.head_sha` when the artifact is workflow-wrapped.
- ⛔ Returns nothing for `-ERROR` runs that carry no head in either place — correctly, since an errored
  run has no verdict to preserve.
- **Before applying it anywhere, run `ls | sed 's/[0-9].*//' | sort -u` and the head-position form
  count.** One command, and it would have bounded this claim before I made it.
