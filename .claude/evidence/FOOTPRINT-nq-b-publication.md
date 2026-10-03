# nq-b publication footprint — for eligibility routing

**Read-only to produce. Purpose: let a router ask "who has NOT published on this subject?" BEFORE
assigning, instead of one decline at a time.** Produced 2026-10-03.

## ⛔ CORPUS CERTIFICATION AND ITS BOUNDS — read this before trusting any zero below

| file | mtime | latest internal stamp | state |
|---|---|---|---|
| `LOOP_LOG.md` | 2026-09-28 02:41 | **2026-09-28** | ⛔ **TRUNCATED** — 2.0 MB, holds **271** fire refs |
| `LOOP_LOG_worker.md` | 2026-10-03 12:46 | 2026-10-03 | current — holds **5** fire refs |
| `FINDINGS.md` | 2026-09-28 02:46 | **2026-09-28** | ⛔ **TRUNCATED**, same cutoff |
| `HANDOFF.md` | 2026-09-21 20:50 | **2026-09-21** | ⛔ **TRUNCATED**, earlier still |
| `.claude/evidence/` | — | 2026-10-03 | current, 66 files |
| subagent transcripts | — | **2026-10-01 01:24** | 1007 files, ends 10-01 |

**THREE OF FOUR LEDGER FILES ARE DEAD. Only the one I append by hand is live** — and it is the one
with 5 fire refs against the dead file's 271. **Currency and coverage are ANTI-CORRELATED on this
lane.**

**Controls run:** negative (`zzqq_no_such_token_zzqq`) → 0/0/0 ✅; positives dated today
(`680dd164` → worker 3, `ts#408` → worker 7 + evidence 1) ✅. So the live corpus discriminates.

**THE BOUNDS, stated so a gap is a limitation rather than a hole:**
1. **Fire activity 2026-09-28 → 2026-10-01** is in **no ledger** — it is only in subagent
   transcripts. Those cover it.
2. **Fire activity after 2026-10-01** would be in nothing — **but zero subagent transcripts are
   dated 2026-10-02 or later, so no fires were spawned in that window.** The gap is bounded empty,
   not unmeasured.
3. ⛔ **A ZERO FOR ANY SUBJECT WHOSE WORK FELL BETWEEN 09-28 AND 10-01 AND WAS DONE BY A FIRE
   RATHER THAN BY ME IS UNINFORMATIVE** unless the transcripts are also searched. Search them.

## ⛔ MY LEDGER WRITES BARE `#NNN` WITH NO REPO PREFIX

124 of the authorship hits below resolved to `?#NNN` — **my own records do not disambiguate which
of three repos a number belongs to.** So a number from this table is a *candidate* across
firmware / test-suite / claude-skills and must be confirmed against the row. Same class as the
`318352` false conviction, in my own corpus.

---

## 1. ROWS AND ISSUES I HAVE PUBLISHED ON — 124 distinct numbers

**Extraction:** lines carrying an authorship speech act — *posted a note · left a note · opened PR ·
audit LAUNCHED · verdict POSTED · attestation posted · disposition comment · Declined with
citation · I parked · pushed to* — never a mere mention. **Truncated to the 40 highest-traffic
subjects; the remaining 84 are single-marker rows.**

### Highest traffic (≥3 authorship markers)

| number | n | marker quoted |
|---|---|---|
| `#328` | 8 | flash/crc32 proving + WiFi STA restore |
| `#386` | 6 | *"Fire 22 … Took test-suite #383"* |
| `#398` | 6 | *"opened PR #398 at ee9144dd…"* |
| `#401` | 6 | *"fire 31 fixed #398's audit finding"* |
| `#1003` | 4 | ⛔ *"Declined with citation; **left a note on #1003**"* · *"**Posted a note on issue #1003**"* |
| `#399` | 4 | *"**Opened PR #399**"* |
| `#409` | 4 | *"audit launched; **filed #409**"* |
| `#424` | 4 | *"**opened PR #424**, shepherded"* |
| `#430` | 4 | *"**PR #430 opened**"* |
| `#876` | 4 | *"what's already **posted on #876**"* |
| `#1016` | 3 | *"**PARKED on #1016**, not merged, **verdict posted**"* |
| `#306` | 3 | *"**Posted the** …"* |
| `#363` | 3 | fire scope note |
| `#387` | 3 | *"**Worker note posted on #387**"* + issuecomment URL |
| `#404` | 3 | *"**PR #404 opened**, Qodo c…"* |
| `#405` | 3 | *"**Opened PR #405**"* |
| `#407` | 3 | *"test-suite #407 CONVERGED; **audit launched**"* |
| `#414` | 3 | *"**shipped #409 as PR #414**; audit launched"* |
| `#417` | 3 | *"filed ts#416, fixed it, **opened PR #417**"* |
| `#419` | 3 | *"**opened PR #419**, shepherded Qodo"* |
| `#992` | 3 | *"my diff read done, **audit launched** at `8d0a5e28b`"* |

### Also carrying authorship markers (2 each)

`#1063` *(re-audit launched)* · `#1110` *(**"Posted the PR B framing on #1110"**)* · `#1129`
*(**"PR #1129 opened"**)* · `#1139` *(**"PR #1139 opened"**)* · `#1147` *(Qodo r3 disposition)* ·
`#289` · `#293` · `#305` *(**"Pushed ts #306 … Evidence comm[ent]"**)* · `#333` · `#352`
*(**"Posted the disposition as one PR comment"**)* · `#356` · `#360` *(**"audit LAUNCHED"**)* ·
`#368` · `#371` *(**"opened PR #371"**)* · `#378` *(**"Opened PR #378"**)* · `#382`

**+84 further numbers with a single authorship marker — not listed. That is where I truncated.**

### ⛔ The four already-established disqualifiers, for cross-reference
- **ts#352** — I **opened** it; *"companion-test authoring (both repos)"*
- **ts#448** — *"ts#448 PARKED BEFORE THE AUDIT"* — my lane parked it
- **sk#230** — *"sk#230 gate-repair audit LAUNCHED (`wf_81434adb-625`)"*
- **sk#231** — *"audit: DISCLOSED a direct stake BEFORE spending a round"*
- **ts#421** — I pushed `680dd1648`; eligibility **withdrawn**
- **ts#408 / issue #1003** — published, declined

---

## 2. SYMBOLS I HAVE PUBLISHED ANALYSIS ABOUT — 137 distinct

**This is where authorship actually lives.** Extraction: a backticked identifier on a line that
also carries a source citation (`file.c:NNN`, *verified at source*, *V-check*) — analysis, not a
mention. **Top 28 by frequency; the tail of 109 is single-mention and not listed.**

`updated_at` 13 · `EN_5_10V_Val` 8 · `powerState` 6 · `sampleStart` 3 · `SCPI_WriteWithRetry` 3 ·
`drain_errors` 3 · `objStart` 2 · `Streaming_Stop` 2 · `Streaming_DrainSessionSampleQueues` 2 ·
`SCPI_ErrorPush` 2 · `created_at` 2 · **`WifiTcpBytesSent` 2** · `Confirmed` 2 ·
**`TcpServerFlush` 2** · `SCPI_ERROR_EXECUTION_ERROR` 2 · `SCPI_ADCChanCalmSet` 2 ·
`SCPI_RejectCfgClaim` 2 · `probe_only` 2 · `device_reset` 2 · `run_test_session` 2 · `repoPath` 2 ·
`restore_bench` 2 · `prior_power` 2 · `idn_variant` 2 · `StreamingBufferPool_Partition` 1 ·
`StreamingBufferPool_GetSamplePool` 1 · `InitializeExternal` 1

⚠ **This count UNDERSTATES the ts#421 case** and shows why the symbol key matters: a plain
`grep -c` for `TcpServerFlush` over `LOOP_LOG.md` gives **5** and `WifiTcpBytesSent` gives **6**,
while this citation-anchored extractor gives 2 each. **The anchored form is the stricter, better
authorship signal; the plain count is the better conflict TRIPWIRE.** Use the plain count to
decide whether to look, and the anchored form to decide what it means.

---

## 3. AUDIT WORKTREES — only ONE is establishable, and that is itself the finding

| tree | my ledgers | my evidence | subagent files | verdict |
|---|---|---|---|---|
| **`audit-1115`** | 1 | 1 | 45 | ⛔ **MINE** — declared; fw#1115 is ts#408's parent |
| `audit-1036` | 0 | 0 | **89** | **UNDETERMINED** |
| `audit-991n` | 0 | 0 | 83 | **UNDETERMINED** |
| `audit-965` | 0 | 0 | 41 | **UNDETERMINED** |
| `audit-fw1152-r3` | 0 | 0 | 1 | **UNDETERMINED** |
| `audit-996-conv` | 0 | 0 | 20 | conv-fw's, by name |
| `bench-996` | 10 | 0 | 20 | **nq-c's** — established earlier; my 10 hits are today's attribution work |

⛔ **THE SUBAGENT COLUMN DEFEATS ITSELF.** `audit-1036` scores **89** to `audit-1115`'s 45, with
zero ledger and evidence hits — because my transcripts contain `git worktree list` dumps that name
**every** registered tree. **That column cannot distinguish HOLDING a tree from SEEING it in a
listing**, so only the ledger/evidence columns are usable.

> **So I cannot enumerate which audit worktrees are mine.** `audit-1115` is establishable; four
> others are genuinely undetermined. This is the disqualifier class that is invisible to any text
> search of a row — and my own records cannot close it either.

## How a router should use this

1. **Take the subject, not the row** — parent issue and symbol first.
2. **A hit in §1 or §2 means ASK ME, not "nq-b is out"** — grading is the second step and a
   disclaimer counts as evidence of non-involvement (*"fw#965 is NOT this lane's"* was once scored
   as involvement).
3. **A zero here is only as good as the bounds above.** Anything whose work fell 09-28→10-01 and
   was done by a fire needs the transcripts searched too.
4. **`#1003` and the error-emission chain are block-conflicted** — conv-fw and I disqualified
   independently, so treat the whole compound-error-line subject as having no assessor among us.
