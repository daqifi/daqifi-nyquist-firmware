# The reflog check on my fw#976 exclusion: **0 of 4, with a passing control — a FALSE CLEAR**

Produced 2026-10-13, answering a direct ask: run the per-worktree reflog check and say whether it
confirms or refutes my fw#976 exclusion. **It does neither. It clears me of commits I am
unambiguously responsible for.**

## The measurement

```
git -C /mnt/c/daqifi/wt/nq-b reflog --date=short
  entries 141 · span 2026-09-08 → 2026-10-13          (healthy, long)
  CONTROL: my newest lane/nq-b commit 9fb5c084f  -> 1 hit  ✅ instrument live

  9dfc09d75  -> 0        efba54c1f  -> 0
  d55d5dbd8  -> 0        b960c9245  -> 0
```

Those four commits are the **entire basis** of my exclusion, and all four are **confirmed ancestors
of the live head**. A working instrument, a passing positive control, and a confident zero on the
thing it was pointed at.

## ⛔⛔ Where the record actually lives — three distinct mechanisms, all confirmed

```
agent-a0f05824999e0e24c  (6 entries)   efba54c1f  "commit:"   d55d5dbd8  "commit:"
agent-a6cab3e0bc1d9c1d5  (4 entries)   9dfc09d75  "commit:"
                                       b960c9245  "checkout: moving from worktree-agent-… to b960c924521…"
agent-a846f8745eb2cb4d0                GONE — its reflog died with it
```

1. **DELEGATION.** 3 of 4 were created by **my fires, in their own worktrees.** The reflog records the
   worktree that moved the ref — and **the loop's architecture is precisely that the lane does not
   move the ref; the fire does.** So for any lane using the fire pattern, this instrument records the
   fire and never the lane. **It is blind to the fleet's normal working mode by construction.**
2. **PLUMBING, AND A MISATTRIBUTION.** `b960c9245` I built **from my own worktree** with
   `commit-tree` plus a separate `GIT_INDEX_FILE`, pushing `$NEW:refs/heads/…` directly — no local
   ref moved. ⚠ I predicted it would leave no entry anywhere; **it leaves one in a DIFFERENT
   worktree, as a `checkout:`**, because a later fire checked it out.
   > **So the reflog attributes that commit to the worktree that CONSUMED it, not the one that
   > CREATED it.** That is worse than a blind spot — a reader asking "who made `b960c9245`?" finds a
   > fire's reflog and the fire did not make it.
3. **MORTALITY.** `agent-a846f8745eb2cb4d0` is already gone, taking ts#306's commit record with it —
   the stated limit, arriving within four days rather than eventually.

## ⭐⭐ The inversion that matters most

**My narrative self-report is MORE accurate than the mechanical measurement.** I said *"I am that
file's three most recent authors"*; the reflog says I touched none of them. I have spent this session
insisting on measurement over summary, and here the summary wins.

> **"Prefer the measurement over the self-report" has a precondition: the measurement's coverage must
> include the MECHANISM by which the thing happened.** The reflog covers exactly one mechanism — *a
> ref moved in this worktree*. It does not cover delegation, ref-less plumbing, or a deleted
> worktree, and those are three of this fleet's normal modes.

**This does NOT undermine the conviction it produced elsewhere.** A peer found its own disqualifier
this way — a base-refresh merge it had no memory of making. That stands: the instrument **convicts
reliably**. What my result kills is any **clearance** drawn from it. So *"convicts reliably, clears
nobody"* is right, and the reason is sharper than *"absence may mean the record was deleted"*:

> **The two commonest ways this fleet creates commits — a fire in its own worktree, and plumbing that
> moves no ref — BOTH leave the lane's own reflog empty.**

## How to use it, stated so it cannot be read as a clearance

- ✅ **A HIT is strong evidence** of involvement the lane may not remember. Run it for that.
- ⛔ **A ZERO is worth nothing** without also checking: every `agent-*` worktree the lane has spawned
  (and whether any have been removed), and any commit created by plumbing.
- ⚠ **A `checkout:` entry is not an authorship claim.** Only `commit:` / `merge` / `rebase` entries
  indicate the worktree produced the object; `checkout:` means it merely visited.

**My fw#976 exclusion stands, on the unchanged basis:** `git log` shows those three commits as the
file's most recent, their SHAs were verified against `ls-remote` when my fires reported them, and my
symbols (`_direct_update_problems` ×11, `MANAGER_UPDATE_CALL` ×9) are live in the file. **The reflog's
disagreement is a defect in the reflog's coverage, not evidence about me.**
