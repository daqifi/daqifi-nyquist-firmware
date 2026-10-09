# Triage: the 13 PR-less branches — **5 live, 6 supersession candidates, 2 other. None cleanly deletable yet.**

A READING, not a ranking. No position added to any row — these branches have no PR by definition.
Produced 2026-10-09.

**Population established independently:** 59 remote branches · 399 distinct PR-head branches ·
**19 PR-less**, minus `main`, minus the 4 already-triaged `salvage/*` = **13**, matching the
coordinator's count exactly. Control: fw#976's live branch correctly excluded from the PR-less set.

## ✅ NOT DELETABLE — 5 open issues, and every one is DISCOVERABLE from its issue

| branch | issue | branch named in the issue? |
|---|---|---|
| `feat/675-bssid-pin` | **#675 OPEN** | ✅ body ×4 + comments ×3 — **and the issue TITLE names it** |
| `fix/832-csv-pure-t1-additive` | **#832 OPEN** | ✅ comment ×1 |
| `fix/981-sd-write-fault-injection-hook` | **#981 OPEN** | ✅ comment ×1 |
| `fix/995-cmdhistory-write-abort` | **#995 OPEN** | ✅ comment ×1 |
| `wip/1003-compound-message-error-framing` | **#1003 OPEN** | ✅ comment ×1 |

> ⭐⭐ **5 of 5. The preserve-and-say-on-the-ticket discipline WORKED.** I expected the
> half-documented-dependency shape (work on a branch the issue never mentions) and found the opposite:
> every preserved branch is reachable from its issue. Four have the pointer in a **comment** — a fire
> or worker adding it at preservation time, which is the backlog-loop rule being followed. #675 has it
> in the body *and* the title.

Negative control: `zzqq/no-such-branch` → 0 hits in #675's body, so the check discriminates.

## ⭐ 7 of 13 LABEL THEMSELVES as not-ready, in the commit subject

```
fix/981   "INTERRUPTED, NOT VALIDATED"
wip/1003  "INTERRUPTED, NOT VALIDATED"
wip/913   "UNVALIDATED, rescued from a killed fire"
fix/995   "fire stopped by a session rate limit, unreviewed"
perf/757  "-- HELD"
feat/675  "-- NOT for merge yet"
preserve/824  "found in an abandoned worktree"
```

**These are dead fires' remains, and they said so.** Combined with the discoverability result above,
the preservation half of the sweep rule is in good shape — the gap is that nobody has since *decided*
anything about them.

## ⛔ 6 supersession candidates (issue CLOSED) — and the largest is NOT cleanly superseded

| branch | issue | unique | files |
|---|---|---|---|
| `fix/689-sd-dir-bucketing-narrow` | #689 CLOSED | **18** | 4 |
| `salvage`-adjacent: `fix/1053-app-systeminit-scheduler-claim` | #1053 CLOSED | 1 | 11 |
| `fix/560-wifi-listener-selfheal` | #560 CLOSED | 2 | 5 |
| `perf/757-rotation-window-memsets` | #757 CLOSED | 1 | 1 |
| `preserve/824-uncommitted-followup` | #824 CLOSED | 1 | 3 |
| `wip/913-spi-yield-rescued` | #913 CLOSED | 1 | 3 |

### ⛔⛔ `fix/689` content-checked — 28 of 30 identifiers on main, **2 ABSENT**

```
#689  CLOSED 2026-08-20T10:12:08Z  reason=COMPLETED
      cross-referenced 783, 788, 589, 793   (another route completed it)
branch last commit 2026-08-13 — SEVEN DAYS BEFORE the issue closed

identifiers the branch adds, ≥15 chars:   30
  present on main:                        28
  ⛔ ABSENT from main:                      2
      sd_EnterBucketPathOnly
      sd_TargetExistsInBucket
```

Controls: positive (`sd_card_manager_UpdateSettings`) found on main ✅; negative
(`zzqqNoSuchSymbolZZ`) absent ✅.

**And the two absent symbols map onto the branch's final two commits:**

```
861ebdbce  fix(sd): re-check "target already exists" in EVERY bucket, not just the first
15d1ed2ee  fix(sd): stop the reuse probe at the first missing bucket
```

> **So `fix/689` is MOSTLY superseded and NOT ENTIRELY.** 18-ahead-of-main was misleading in the
> expected direction (28/30 landed), and the residue is not noise — it is precisely the last two
> refinements the branch was working on when it stopped.

⚠ **CRITICAL CAVEAT, and it is the coordinator's own:** **an absent IDENTIFIER is not an absent
BEHAVIOUR.** Main may implement the same every-bucket re-check inline, or under another name, via one
of PRs 783/788/793. **I have NOT established that main lacks the fix** — only that these two
identifiers do not appear. Closing that needs a behaviour read of main's bucket path, which is a
judgement about SD correctness and not a triage reading.

## Two with no issue reference

- **`feat/streaming-inisr-ring`** — 8 unique, 11 files, **last commit 2026-06-18 (the oldest by ~4
  months)**. Commit subjects reference **#525** and read as Qodo review passes 3-4. No branch-naming
  issue checked; it predates the preservation convention.
- **`test/fire-durability-probe`** — 5 commits, **1 file**, all `docs:` steps of a deliberate
  durability experiment plus a handoff. **Not a fix at all** — it is the artifact of a process probe,
  so "superseded" is the wrong question for it.

## Method notes

1. ⭐ **`ahead-of-main` misled exactly as predicted.** `fix/689` reads **18 ahead** and is 28/30
   superseded; `feat/streaming-inisr-ring` reads **8 ahead** and is four months stale. The number
   tracks the parent's distance from main, not the content's survival.
2. ⭐ **The content check that worked is identifier-level set difference against main**, with controls
   both directions — the same shape as the `{unenrolled names}` detector that settled fw#976. **Set
   difference on names, not counts of anything.**
3. ⛔ **Nothing here is recommended for deletion.** 5 are live, 1 is measured not-fully-superseded, 5
   are unchecked, and 2 are the wrong shape for the question. **Recording state, not disposition** —
   and per the standing asymmetry, deletion stays available after one more check while the information
   is already usable.
