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
