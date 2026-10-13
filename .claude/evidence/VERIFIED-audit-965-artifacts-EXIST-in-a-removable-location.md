# `audit-965-r*.json` is NOT missing — all three rounds exist, in a location the fleet deletes

Produced 2026-10-13 from idle time, verifying a peer's absence claim before it became a work item.
**Read-only; I did not copy, move or modify anything in another lane's worktree.**

## The claim, and the measurement

> *"`audit-965-r*.json` is not on this box. Recovering or regenerating that artifact is real work with
> a real blocker."*

```
/mnt/c/daqifi/wt/nq-c/.claude/audit-965-r1.json   10,113 B   valid JSON · 46 top-level keys
/mnt/c/daqifi/wt/nq-c/.claude/audit-965-r2.json   23,331 B   valid JSON · 46 top-level keys
/mnt/c/daqifi/wt/nq-c/.claude/audit-965-r3.json   15,858 B   valid JSON · 46 top-level keys
```

**All three rounds. Complete, parseable, same 46-key schema as the durable artifacts.**
Controls: positive (`audit-fw976*.json` in `~/.claude/lanes` → 1, the search works); negative
(`audit-zzqq*` → 0).

> ⭐ **Regeneration would have cost up to THREE audit rounds for artifacts that already exist.**
> Recovery is a copy.

### ⛔ Why the claim was made, and it is this week's recurring shape

`~/.claude/lanes/` → **0 matches**, and that zero is *correct for that location*. The durable
artifacts live there; these do not. **A location-scoped zero was reported as a whole-box absence** —
the same error as my own `HAL/spi.c` grep for a branch that touches `HAL/UserSpi/UserSpi.c`, and the
same as a `2538017^` zero that needed a positive control *at that revision*.
**An absence claim carries the scope of its search, and the scope is the part that gets dropped.**

## ⛔⛔ THE ACTUAL FINDING: three rounds of audit evidence for an OPEN row sit in a removable location

fw#965 is **OPEN / MERGEABLE / CLEAN** — *"fix(scpi): WIFI:FINd?'s arm must observe the #861 stop-race
guard, and say so when it does not run."*

Its three audit artifacts are inside **a lane worktree's `.claude/`**, not in `~/.claude/lanes/`. The
four artifacts in `~/.claude/lanes/` were already established as **not version-controlled** (caught by
`.gitignore:6:/*`, a root-level deny-by-default) and therefore *"durable against a session ending, not
against someone cleaning `~/.claude`"*.

> **These three are strictly worse: a worktree is a thing this fleet removes as routine
> housekeeping.** I have personally recorded a subagent worktree disappearing **within four days**,
> taking a row's commit record with it — and the standing sweep rule instructs lanes to remove fire
> trees.

**Not my call to move them** — another lane's worktree, and copying would create a second source of
truth with no pointer between them. **Reported so nq-c or the coordinator can decide.**

## My eligibility on fw#965, pre-disclosed rather than discovered later

- **The `audit-965` worktree remains UNDETERMINED.** SHA pairing on `5fffcb140` finds **one tree, no
  lane-named partner** — so the method fails here exactly as it did on `audit-fw1152-r3`, confirming
  my own correction that *pairing resolves ownership only when a partner is lane-named*.
- ⚠ **Weak contrary evidence, stated as an inference and not a measurement:** all three round
  artifacts are in **nq-c's** `.claude/`, which is evidence nq-c ran those rounds — the same reasoning
  that resolved `audit-991n`. **It does not establish who holds the `audit-965` tree.**
- **My own ledger hits on 965 are verifications of CLAIMS ABOUT the row** — *"the comment asserts #965
  is still open and unmerged … BOTH TRUE"*, and *"VERIFIED CLEAN: the false #965 claim … has been
  corrected"* — plus an artifact-health classification table. **Not assessments of its content.**

> **So: probably clear on fw#965's substance, with the `audit-965` tree question OPEN.** If the
> duplicate-fingerprint work is routed to me, that tree must be resolved first — and **I cannot
> resolve it**, because the only instrument that would is self-administered and this tree is not mine
> to introspect. Flagging now rather than at routing time.

---

# AMENDMENT, same day: the gap is CLOSED, my ELIGIBILITY is RESOLVED, and **my own urgency framing was WRONG**

## 1. ✅ The durability gap closed in ~4 minutes, and the fix is sound

nq-c acted on the report. Measured independently, not taken on their word:

```
~/.claude/lanes/audit-965-r{1,2,3}-nq-c.json   mtime 2026-10-13 20:56:52Z  (×3, ms apart)
sha256 vs the worktree originals               r1/r2/r3 all IDENTICAL
originals in nq-c/.claude/                     mtime 2026-09-17, UNTOUCHED
CONTROL: the four long-standing lanes/ artifacts   mtime 2026-10-07  -> not a clock artefact
```

⭐ **And this retro-validates my zero.** The lanes/ copies postdate my search by minutes, so *"0 in
`~/.claude/lanes/`"* was true when measured. **A measurement that a later fix invalidates was not
wrong.** `cp -n` + `cmp`-verified, originals kept, and a pointer filed naming both locations — so
neither copy can become the only known one.

⚠ **Destination characterisation kept honest by both sides: BETTER, NOT SAFE.** `~/.claude/lanes/` is
still caught by `.gitignore:6:/*` — durable against a session ending and against a worktree sweep, not
version-controlled, not safe from anyone cleaning `~/.claude`. **Neither location is archival.**

## 2. ⛔⛔ MY OWN CORRECTION: "removable by the standing sweep rule" IS FALSE for these files

I wrote that a worktree is *"a thing this fleet removes as routine housekeeping"* and cited the fire-tree
sweep. I then censused every audit artifact on the box — scope stated: `~/.claude/lanes/`,
`~/.claude/**` (d≤2), `/mnt/c/daqifi/wt/*/.claude/` (d≤2), pattern `audit*.json`:

| location | count | sweepable? |
|---|---|---|
| `~/.claude/lanes/` | **7** | no |
| `~/.claude/audits/` | **8** | no |
| `nq-b` · `nq-c` · `conv-ts` · `conv-fw` worktrees | **108 · 47 · 33 · 3 = 191** | ⛔ **NO — all four are LANE trees** |
| `agent-*` / `audit-*` fire trees | **0** | yes, but there are none |

> **Every one of the 191 sits in a LANE worktree, and my own dispatcher forbids removing another lane's
> tree outright** (*"Only trees THIS lane created. Never touch nq-a\*, nq-c\*, other lanes' agent-\* or
> audit trees…"*). **The mechanism I named does not reach these files.** The real exposure is lane
> RETIREMENT plus `.git/info/exclude:21:.claude/audit-*.json`, which is slower and quieter than a sweep.

⭐ **Control, because a 0 needs one:** 57 `agent-*`/`audit-*` dirs exist, 6 have a `.claude/`, and those
6 hold 6 `.json` files — so fire trees **were in scope and could have matched.** The zero is a finding,
not a vacuum. (Weakly: 6 of 57, so it bounds little beyond "not where artifacts accumulate".)

⛔ **And the shape of my error is the exact lesson the same document was filed to teach.** I wrote
*"an absence claim carries the scope of its search, and the scope is the part that gets dropped"* — and
in the same commit **applied the fire-tree sweep rule outside its scope** to lane trees. A
**stated-but-unapplied caveat converts an oversight into an apparent judgement**; here the caveat was not
merely unapplied, it was *the document's thesis*. **Writing a rule down does not run it.**

⚠ **Direction of the error: I OVERSTATED urgency.** Same direction as my `HAL/spi.c` miss. The finding
survives — 191 artifacts outside version control and outside the lanes store is real — but it is a
**retention** problem, not an imminent-deletion one, and I told two peers otherwise.

### An unexpected positive: a durable path already exists and I have used it

```
tracked audit*.json under nq-b/.claude/ :  11   (git ls-files)
.git/info/exclude:21                    :  .claude/audit-*.json
```

**11 of my own artifacts are in version control**, despite the exclude rule — so committing them is an
established, available path. ⚠ **I am not bulk-committing the other 97.** ~1.5 MB of JSON onto a `/mnt/c`
repo whose **`git gc` is HELD by operator ruling** is unreclaimable bloat, and a retention policy for
four lanes' artifacts is not a lane's unilateral call. **Recorded for the operator; no action taken.**

## 3. ✅ ELIGIBILITY RESOLVED — `audit-965` is **nq-c's**, and I verified it myself

nq-c supplied **head-sequence correlation**, and I reproduced it rather than accepting it:

```
audit-965 reflog, oldest first      nq-c's artifacts' head_sha
  ee43eb0c8                   <->   r1  ee43eb0c8130e7a556d7540cc51a53073aa29a09
  6248da5e4                   <->   r2  6248da5e42016e9d1110db479b2c26e700e92458
  5fffcb140                   <->   r3  5fffcb140a5e58248e96622f7473065d544a60a8
three heads · three artifacts · same order · one evening (2026-09-17)
my session id f5691ce5 in that tree:  rc=1, 0 files   [POS control rc=0/1 file ✅ · NEG rc=1 ✅]
tree state: porcelain 0 · .claude/ holds only settings.json
```

⭐ **The method's requirement is strictly WEAKER than SHA pairing**, which is why it worked where pairing
failed: pairing needs a **lane-named partner tree at the same SHA**; correlation needs only **one artifact
per head in any reachable lane's tree.** And it strengthens with rounds — one head could be coincidence,
three in sequence on one evening is not. ⚠ Same channel limits: it **convicts and clears nobody by
itself**, it dies with the worktree, and it only reads trees you can reach. Here it clears me only because
it positively attributes the tree to someone else *and* my own fingerprint is absent with live controls.

**So my fw#965 position: CLEAR on the tree; my ledger hits (`:4560-4561`, `:4679`, `:8150`) are
verifications of CLAIMS ABOUT the row and an artifact-health table, not assessments of its content.** I
can take the duplicate-fingerprint question. Not asserting exhaustiveness — per the standing limitation,
eligibility here remains a self-report.

## 4. ⭐ A plausible eligibility heuristic that is FALSE — flagged by nq-c, confirmed in the reflog

```
9640a7719  2026-09-17 21:00:40  revert: Revert "fix(scpi): push a SCPI error for the finder's … arm refusal (#965)"
6248da5e4  2026-09-17 21:01:26  commit: docs(scpi): record why a finder arm refusal pushes no SCPI error (#965)
```

> **A `revert:` and a `commit:` were authored inside a tree named `audit-`.** So *"it is an audit tree,
> therefore read-only, therefore holding it is not fixer-side"* is false at least here. **Check the
> reflog's VERBS, not the directory's name** — the same discriminator that separates a true positive from
> a false clear in the reflog channel generally.

⚠ **And it compounds a known defect:** reflog-in-a-lane-named-tree is a real attribution channel the
commit object cannot supply (every commit here is a lane-blind `Claude Code`), and the loop's own
*"remove the fire tree"* hygiene **destroys it**. **Tidy lanes erase their own authorship trail.** My own
fire tree vanishing within four days is that defect, not carelessness.
