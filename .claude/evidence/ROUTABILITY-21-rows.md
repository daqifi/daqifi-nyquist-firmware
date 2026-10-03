# The 21 "unheld" rows — causal answers and the three-read routability check

**Read-only. No push, label, comment, close or merge anywhere.** claude-skills writes are
ask-first unconditionally for this lane and that grant does not exist.

## RESULT: 1 SURVIVOR OF 21 — and it survives as AUDIT-READY, not MERGE-READY

**`sk#211`** — twin-check: pin `--from-audit` to the head the audit examined (#66).
MERGEABLE/CLEAN, not draft, head `41fb613d3ea9`, **6 review threads / 0 unresolved**, no hold
stated by any lane in 18 comments, **and no audit verdict anywhere on the row — it has never been
audited.** I am causally clean on it: **0** repo-prefixed ledger hits.

> **So it is routable for an AUDIT, not for a merge.** "Never audited" is work, not a block — which
> is the only row of 21 where that is true.

## Causal answers — 6 of 21 are mine, graded not counted

| row | marker quoted | grade |
|---|---|---|
| **ts#352** | *"opened PR #352 … companion-test authoring (both repos)"* | **AUTHOR** |
| **ts#448** | *"## ts#448 PARKED BEFORE THE AUDIT — deliberate change of approach, operator's call"* | **my lane parked it** |
| **sk#230** | *"sk#230 gate-repair audit LAUNCHED (`wf_81434adb-625`)"* | **I ran its audit** |
| **sk#231** | *"sk#231 audit: DISCLOSED a direct stake BEFORE spending a round"* | **audited + direct stake** |
| **ts#351** | *"ts#351 read: hardware-blocked AND DEFECTIVE. The implied fix makes it worse BOTH ways"* | **prior written conclusion** |
| **ts#384** | *"Fire 49 is fixing it on ts#384"* — and *"Fire 49 spawned"* is my own numbering | **my subagent produced on it** |

**ts#384 is the one that nearly passed.** "Fire 49" reads like conv-fw-49, a peer session; it is my
own fire numbering, confirmed by *"Fire 49 spawned"* elsewhere in the same ledger. **A lane-name grep
would have cleared it and the attribution went the other way** — the grep-hit-misattribution trap,
where the quote is right and belongs to someone else, inverted.

**Clean on causation (disclosed reads only):** ts#334 (*"Queued but NOT started"* — I read it and
declined), ts#421 (8 hits, **all** census/watch-list entries), ts#408, ts#411, ts#412, sk#29, sk#95,
sk#113, sk#117, sk#145, sk#179, sk#211, sk#222, sk#232. sk#221's single hit is a parenthetical
count (*"(sk#221 has 2)"*).

## Why the other 20 fail, per row

| row | fails on | evidence |
|---|---|---|
| ts#421 | **read 1** | 10 threads, **1 unresolved AND current** at the live head |
| ts#334 | **read 2** + never audited | *"## Converged at `e693847` — **staged for bench, not merged**"*, label `needs-bench` |
| sk#232 | **read 3** | newest verdict *"round 4 at `a6d9e4e7c0c2…`: **gate BLOCK**"*; live head `8dc30245ba14…` → **STALE BLOCK**. ⭐ Independently **confirms nq-a's** stale call, same two SHAs. |
| sk#221 | **dependency — STOPPED** | newest verdict: *"The gate resolved `221` against the session's repository — a firmware worktree — found no firmware PR #221, and refused"*. **That is the cross-repo gate collision sk#165 fixes.** Per the brief I noted the dependency and stopped rather than reasoning into sk#165's content. |
| sk#222 | **duplicate + same dependency** | *"This PR's content is now **IDENTICAL to #221's** — the review transfers."* The INSTALLED-branch twin; one change against two bases, not two rows. |
| sk#95 | **prose park, in the TITLE** | *"gate markers: repo-local location **(PARKED at the round cap** — see the last comment)"* |
| sk#117 | **prose park, in the TITLE** | *"**DRAFT/PARKED**: mutate.sh containment hardening"* |
| ts#411 ts#412 sk#29 sk#113 sk#145 sk#179 | state | CONFLICTING/DIRTY (and ts#411/412 also DRAFT) |
| ts#352 ts#448 ts#351 ts#384 sk#230 sk#231 | **conflicted-out** | above |

## ⛔ WHAT I DID NOT EXAMINE — stated, because unexamined has twice been read as clean here

- **ts#408** — MERGEABLE/CLEAN, **DRAFT**, 0 causal hits. **I ran read 1 on nobody else's draft and
  did not run reads 2 or 3 on it at all.** It is the one row I can neither clear nor fail. **Not a
  survivor and not excluded — UNASSESSED.**
- **The 6 conflicted rows got no routability check**, only a causal answer. Their state is unknown
  to this pass.
- **`isOutdated` on sk#232**: 2 unresolved threads, both outdated. I did not establish whether
  claude-skills enforces conversation resolution — the firmware repo ignores `isOutdated` and would
  block, the test-suite repo cannot enforce it at all (403, no Pro). **Unknown for this repo.**
- **conv-fw holds a read-only triage of the same twelve.** I did not read it, deliberately, so this
  is an independent pass — **any disagreement between us is worth more than either answer**, and
  sk#232 is where to look first since I confirmed a stale call there.

## The delta: my six and your nine share exactly ONE row

**Mine (from `LOOP_LOG_worker.md:4700` ff):** ts#352 · ts#371 · ts#430 · ts#432 · ts#434 · ts#435.
**Yours:** ts#334 · ts#351 · ts#352 · ts#384 · ts#408 · ts#411 · ts#412 · ts#421 · ts#448.

**Only ts#352 overlaps.** So this is not drift in one population — they are **different six and nine**,
union 14. Five of mine left your set because they acquired labels or parks since (I classified
ts#430/432/434/435 as CURRENT BLOCK earlier today; ts#371 now carries `blocked:audit-findings`).
**The union is the right denominator and neither count alone is.**
