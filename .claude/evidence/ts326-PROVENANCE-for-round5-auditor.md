# ts#326 — PROVENANCE NOTE for whoever audits round 5

Written by **nq-b**, who is **CONFLICTED on this row in both directions** and has declined the
round-5 auditor role. **This is authorship disclosure and scoping, not a quality claim.** Nothing
here argues that my lane's commits are correct — that is the question you are being asked, and I am
the wrong party to answer it.

## WHY YOU ARE GETTING THIS

Under the sole shared identity every commit on this row reads `Chris Lange`, so **the row's
authorship is not answerable from the repository.** It lives only in lane ledgers. Two lanes were
told they were "clean on ts#326" and both were wrong. This note exists so you do not pay to
rediscover that.

## ⛔ TWO CORRECTIONS TO THE BRIEF, measured from the PR rather than from anyone's notes

**1. MY LANE OWNS SEVEN COMMITS, NOT THREE.** The brief described my involvement as "rounds 2-3".

**2. THE LIVE HEAD IS ALREADY PAST THE STATED BASELINE.** The brief says to wait for nq-c to push and
names `1ab8edc17` as the pre-fix baseline. `1ab8edc17` is **two commits back**; the live head
`93a009bb164a` landed `2026-10-01T14:05:07Z` carrying *"fix(gate): #980 tests must emit the gate
summary AND declare requires=nq3"* — which looks like the very fix the baseline was to precede.
**Confirm the sequencing with nq-c before pinning anything.**

Live state: `MERGEABLE / UNSTABLE` (a check is failing or pending), label `blocked:audit-findings`.

## AUTHORITATIVE COMMIT ORDER (from the PR, oldest first; verified on retry after an empty first read)

```
 1  bdf5336cecc4  09-11  #980 companion -- DAC7718 power precondition on every call
 2  40d06ab5e902  09-11  device-level companion for #980's all-channel latch fix
 3  5e441e108bff  09-28  four review findings -- three transport defects
 4  fe8add69f0cb  09-30  Merge origin/main
 5  a5f2ce826a88  09-30  four classes at every call site, not seven point fixes   <-- HEAD I AUDITED
 6  ad1bb42fa03f  10-01  finding 1 -- restore_bench sends a device-rejected command   | MY
 7  b4e501989a9d  10-01  finding 2 -- discover_channels folds unreadable into absent  | LANE
 8  8d7f401f9b60  10-01  finding 3 -- exit code can go stale vs the final verdict     | (7
 9  450f246dfff5  10-01  independent-source findings 5 & 6 -- power-up evidence       | commits,
10  afd7dbe2a6c9  10-01  finding 4 -- teardown restores voltage before power is up    | 6-12)
11  48f45e46a663  10-01  round-2 -- the validating double was case-sensitive          |
12  1ab8edc170e4  10-01  round-3 -- the double accepted channels the device refuses   |
13  93a009bb164a  10-01  fix(gate) -- emit the gate summary AND declare requires=nq3  <-- LIVE, NOT MINE
```
**Commits 1-5 and 13 are not my lane's. Commits 6-12 are.** Commit 5 is both nq-c's work and the head
I audited, which is where the interleaving bites.

## MY ROUND-1 AUDIT, AND WHAT IT ACTUALLY SAID

```
artifact  .claude/evidence/audit-ts326-a5f2ce826a-BLOCK-4findings.json   (on lane/nq-b)
base      05eb7f72f5f2e15102dc9ff7f61d1da767515bfc
head      a5f2ce826a8857408729425c03e1075aea2d732c
gate      BLOCK      rawFindings 8  ->  4 DISTINCT dispositions
```
⚠ **`rawFindings: 8` is EIGHT RAW, FOUR DISTINCT.** The arbiter merged them in prose
(*"Duplicate of index 4/5/6/7"*), so indices 4-7 are restatements of 0-3. **If you count 8 you will
double every severity.**

| audit idx | finding | site | agreed severity | closed by |
|---|---|---|---|---|
| 0 | Teardown sends invalid per-channel voltage commands | `test_980_..._all_channel_latch.py:182` | **high** | `ad1bb42fa03f` |
| 1 | Discovery excludes configured channels whose setpoints are unknown | `:294` | **high** | `b4e501989a9d` |
| 2 | Changing the verdict in `finally` leaves the successful exit code intact | `:579` | medium | `8d7f401f9b60` |
| 3 | Power-cycle cleanup restores voltage while the rail is down | `power_cycle_reinit.py:349` | medium | `afd7dbe2a6c9` |

⛔ **OFF-BY-ONE WARNING: audit index + 1 = the "finding N" in the commit messages.** Audit index 0 is
commit-message "finding 1". This is exactly the mis-mapping a scoping pass makes; I am stating it
because I nearly made it writing this note.

**Plus `450f246dfff5` closes "independent-source findings 5 & 6" — NOT from my audit.** Two findings
reached this row from a source other than my round-1 artifact, so my artifact is not a complete
inventory of what has been raised.

## ⛔ WHAT MY LANE **REFUTED** RATHER THAN FIXED — THE THREE THINGS MOST WORTH YOUR SCRUTINY

These are the places where my lane **declined to change code**, which is where a conflicted author is
least trustworthy and you should look hardest.

**1. The fire's disclosed residual (before commit 10).** Claim: a configured-but-never-commanded
channel returns a CONFIRMED SCPI error via a `Timestamp < 1` gate, citing `SCPIDAC.c`'s
`SCPI_DACVoltageGet`. My lane refuted it. **Verify independently** — `SCPIDAC.c:765-766` reads
`singleVoltage = (pSample != NULL) ? pSample->Voltage : 0.0;` with no never-commanded gate.

**2. Round-2 finding 3 — REFUTED, and my lane recorded that applying it would have been HARMFUL.**
Claim: `int(float(parts[0]))` is more permissive than firmware because libscpi's integer parse
refuses `2.7`. On the **setter** path `SCPIDAC.c:381-384` narrows a **double** twice
(`double -> int -> uint8_t`), so `2.7,1.0` really does reach channel 2 and the double **mirrors**
firmware. Tightening it would make the double reject a command the device **accepts and acts on**.
⚠ **My lane's own ledger calls this "the third time on this row a fix would have reintroduced the
class it was closing"** — so this row has a history of corrective changes causing the defect, which is
the single most useful thing to know before proposing another.

**3. The two-sided criterion, amended by my lane.** nq-a proposed *"the double exists to be at least
as strict; anywhere it's looser is a hole."* My lane amended it to: **the double must MATCH firmware's
acceptance set, not bound it from one side** — because anywhere it is STRICTER is a false-failure
generator. **This is a criterion my lane changed while authoring fixes judged by it.** Treat it as a
claim to re-derive, not a premise.

## WHAT I CANNOT SUPPLY, AND WILL NOT GUESS

⛔ **I do not have conv-ts's six root causes.** They were referenced to me but never listed, so I
**cannot** map them against commits 6-12 — and inventing the mapping is exactly the error this note
exists to prevent. **Ask conv-ts for the six, then map them against the table above.** My artifact
covers 4 distinct findings at commit 5's head; `450f246` names 2 more from an independent source;
conv-ts's six may overlap any, all, or none of those.

## SCOPING SUGGESTIONS, offered as a conflicted party

- **A fix-verification range over commit 13 alone would miss the interleaving entirely.** On sk#136
  a fresh audit from the merge-base surfaced two HIGHs that a fix-range could not have reached.
- **But measure the range before pinning it.** I produced a VOID artifact on sk#230 by setting base to
  the merge-base of a 176-commit-divergent branch: 269 files, +93,068, codex timed out at 12.2%
  coverage. Run `git rev-list --count <base>..<head>` and compare `total_bytes` against
  `gh pr diff 326 | wc -c` before trusting any artifact.
- **Pass `repo` explicitly.** `adversarial-audit.js:77` defaults it to `ORG/REPO`, which satisfies its
  own shape regex at `:84`, so an omitted `repo` audits whatever `repoPath` points at and returns a
  confident PASS. Measured on a real artifact.
- **The row is `MERGEABLE / UNSTABLE`** — resolve what is unstable before reading any gate as clean.
