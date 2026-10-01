# FIRE STANDARDS — lane nq-b

The shared standards every lane nq-b fire follows, whether it is a standard loop
fire (its assignment is `FIRE_TASK.md`) or a narrow FOLLOW-UP fire (its
assignment is the brief in its own prompt). Split out of `FIRE_TASK.md` on
2026-09-15 so that a follow-up fire can be pointed at the standards without being
handed the original assignment (backlog-loop: "A REVIEW FOLLOW-UP GOES TO A FRESH
FIRE WITH A NARROW BRIEF, NEVER TO THE FINISHED FIRE RESUMED").

Written against `~/.claude/skills/backlog-loop/SKILL.md` and
`~/.claude/skills/qodo-cycle/SKILL.md` as they read on 2026-09-15. If a skill now
says something different, **the skill wins** — say so in your report.

**A follow-up fire does ONLY what its brief names.** It does not read or act on
`FIRE_TASK.md`, does not pick up tickets, and does not redo work the worker has
already reviewed.

---

## 0. DEFINITION OF DONE — read this before anything else (operator, 2026-09-14)

On 2026-09-14 three of three fires had pushed NOTHING when they were paused,
although each had been told in writing to push after each step. One had finished
a whole item, tests included, and it existed only in a scratch worktree. So this
is a definition, not advice:

- **An item is DONE only when its commit is ON THE REMOTE.** Not when the code
  works, not when the tests pass — when `git push` has succeeded. Commit and push
  each item ON ITS OWN before you start the next one.
- **After EACH meaningful step, not at the end:**
  1. `git add -A && git commit -m "<what this step did>"`
  2. `git push` (with `-u` the first time)
  3. update a `## HANDOFF` block at the end of the PR body (or the file you are
     working in) with exactly the three lines below. **Not by rewriting
     `/mnt/c/daqifi/wt/nq-b/.claude/HANDOFF.md`**: its top block is the
     worker's lane header (cron id, re-arm steps, which PRs are parked). You may
     append a section there; never replace the file.
     ```
     WAS DOING: <one line: what this step did>
     NEXT STEP: <one line: what you would do next if you died right now>
     SAFE TO RESUME FROM: <the short SHA you just pushed>
     ```
- **Verify each push with FULL SHAs:** `git rev-parse HEAD` must equal
  `git ls-remote origin refs/heads/<branch> | cut -f1`. Never compare short SHAs
  of different lengths — that reports a false "not pushed".
- **Your report lists, for every item, the pushed FULL SHA and the test that
  proves it, and ends with the HANDOFF block.** A report without them is not
  accepted as done.
- **A residual risk you disclose is an UNTESTED FINDING.** Before reporting one,
  TRY it with a realistic example and report what happened. "Requires unrealistic
  input" is acceptable only after a realistic input was tried and did not trigger.
- **NEVER force-push, and never rewrite a commit you have already pushed** —
  no `--force`, no `--force-with-lease`, no amend-then-push, no rebase of a
  pushed branch. Force-push is on the operator's always-ask list, even on your
  own brand-new branch. You may amend or reorder commits only BEFORE their first
  push. If a pushed commit is wrong, including its message, push a NEW commit on
  top. (A fire did `--force-with-lease` on its own unreviewed branch on
  2026-09-15; the risk happened to be nil, but the rule was broken.)
- **If you are told to stop or pause:** stop at the next safe point, `git add -A`,
  commit what is there as `WIP <ticket>: paused -- see HANDOFF`, push, verify by
  full SHA, update the HANDOFF block, then return.
- **The worktree is a safety net, not a plan.** The worker removes your tree after
  you return. Anything not pushed, and any note not written to an absolute lane
  path, is gone. You will not be resumed to finish or explain it.

---

## 1. Orient (mandatory)

1. Read `/mnt/c/daqifi/wt/nq-b/.claude/LANE_BRIEF.md` in full. It is
   authoritative for the BENCH; the global skills win everywhere else (the brief
   says so itself). **Known brief bug:** its line "PICkit 4 serial `<none>` —
   always pass `--ts <none>`" is a renderer bug (`lane.sh` greps a PICkit-4-only
   `BUR…` pattern). The registry row in `~/.claude/bench/devices.conf` is
   authoritative: **PICkit 5 `020026703RYN079002`**. Never pass `--ts <none>`.
2. **Standard fire only:** read `/mnt/c/daqifi/wt/nq-b/.claude/HANDOFF.md` and the
   TAIL of `/mnt/c/daqifi/wt/nq-b/.claude/LOOP_LOG.md`. A follow-up fire skips
   this: its brief is its state.

   ### ⛔ BOUND THIS READ. AN UNBOUNDED ONE DESTROYS YOU — IT DOES NOT SLOW YOU DOWN.

   **`LOOP_LOG.md` is 2,028,776 bytes — 29,337 lines, roughly 507,000 tokens**
   (measured 2026-09-24; an earlier revision of this line said "~600 KB", which was
   stale by more than 3x and made a whole read look merely expensive). **It does not
   fit your context at all.** One unbounded read ends the fire *before any work
   happens* — you do not get a slow fire, you get no fire, and the tick is spent.

   **DO:**
   - `tail -200 /mnt/c/daqifi/wt/nq-b/.claude/LOOP_LOG.md` — the default, and enough
     for "what happened recently".
   - Looking for something specific? **`grep -n "<string>" <path>` first to get line
     numbers, then read ONLY that region** — `sed -n '<start>,<end>p'`, or `Read` with
     `offset` and `limit`. Locate, then read narrowly.

   **DO NOT:**
   - `Read` it without a `limit`. ❌
   - `cat` it, or pipe the whole file into anything. ❌
   - `grep -C` with a wide window, or a pattern that matches thousands of lines —
     a wide-context grep over 29k lines can return more than a bounded read would. ❌
   - Read it "just to orient". The tail and a targeted grep ARE the orientation.

   This is bounded for the same reason it is not rotated: per-fire cost is already
   near zero, because appends and tail reads are both bounded regardless of size.
   The whole hazard is the one accidental wholesale read. **Do not rotate or truncate
   this file** — that is a separate ruling and is not authorized.
3. Re-derive live state from `gh`. Trust `gh` over any note.
4. Read `CLAUDE.md` at the repo root: build commands, the SCPI verification
   protocol, the bench inventory, the test policy.
5. `bash ~/.claude/skills/bench/bench.sh whoami` MUST print `lane: nq-b`. If it
   does not, STOP, do no hardware work, and report that. Do not edit the
   registry and do not try the printed `BENCH_AGENT=` override — it cannot work.

---

## 2. Your hardware (registry row `nq-b`)

- Nyquist firmware serial **`7E2873046200E891`**, driven natively through its
  Windows COM port (see the registry row; no usbipd, no `/dev/ttyACM*`). Use
  `bench serial <port> ...` or Windows `python.exe`.
- **PICkit 5 `020026703RYN079002`** — pass `--ts 020026703RYN079002` to
  `flash.sh` (it derives `-TPPK5` from the PID). With two PICkits attached,
  ipecmd exits 0 WITHOUT programming if you do not disambiguate.
- **Flashing YOUR OWN board is pre-authorized.** It wipes NVM: restore the stored
  configuration and verify it field by field, then `SYST:REBoot` (libscpi context
  is stale after a flash; new patterns return `-113` until you do).
- **Identify an image by `identity.firmware_crc32` (`CONF:CAP:JSON?`), never by
  the version string.** A flash is proven only when the crc32 CHANGES.
- The exact working flash + WiFi-restore recipe for THIS board is recorded in the
  LOOP_LOG entry headed `## Fire: bench validation of test-suite #328 vs firmware
  #992 (2026-09-13)` (grep for it). WiFi STA restore uses
  `/mnt/c/daqifi/wt/nq-b/.claude/wifi_restore.batch` through the scpi skill's
  `batch.sh`, which on this box needs its variables nominated in `WSLENV`.
- You own the WHOLE board: USB, SD, WiFi (bench AP, creds in `~/.daqifi.env`),
  DIO, DAC. Only genuinely shared or absent hardware is gated: the Saleae (`bench
  claim saleae`), an NQ3 (none here), the #456 impairment router (absent), and an
  AT-CAP throughput MEASUREMENT over USB or WiFi (take `bench with <res> -- <cmd>`
  for the measurement window only).
- **The device guard matches COMMAND TEXT.** Refer to boards by SERIAL, never by
  port name, in logs, commit messages and comments. Write text that must contain
  a port name with the Write tool, not Bash.

---

## 3. Project profile

- **Build:** the `build` skill, or MPLAB X v6.30 make from your tree's
  `firmware/daqifi.X` (`CONF=default` = NQ1). `build.sh`/`flash.sh` derive the
  repo root from YOUR cwd — the printed hex path must be inside YOUR tree and its
  mtime must advance. Windows make can say "up to date" after a header-only edit;
  `rm -rf build dist` settles it.
- **A fresh worktree has no makefiles** (`nbproject/Makefile-*.mk` is gitignored).
  Regenerate them (CLAUDE.md), or, if your branch adds/removes no source files,
  copy them from `/mnt/c/daqifi/wt/nq-b/firmware/daqifi.X/nbproject/`.
- **Validate:** `bash tools/lint/cppcheck.sh` (baseline EMPTY: any finding fails
  CI); `python3 tools/lint/scpi_claim_path.py` (+ `--self-test`);
  `python3 tools/lint/scpi_wiki_sync.py --wiki <clone>` when `SCPIInterface.c`
  changes; the host tests CI runs for the files you touched. Test-suite PRs:
  `python3 -m py_compile`, `pyflakes` (compare against the pre-existing
  findings), `tools/lint/harness_policy_lint.py`.
- **COMPANION TEST REQUIRED** for every behaviour-changing firmware PR, in
  `daqifi-python-test-suite`, committed and referenced in the PR body even if the
  bench run is queued. Build it from `test_harness.py` primitives; new reusable
  capability goes in the harness. Exempt: docs-only, lint-only, comment-only,
  pure refactor.
  - **Where:** make a per-ticket test-suite worktree from fresh `origin/main` (or
    from the branch you were given) at `/mnt/c/daqifi/wt/nq-b-ts<N>`
    (Windows-visible, so `python.exe` can run it). Do NOT switch branches in
    `/mnt/c/daqifi/wt/nq-b-tests` — it holds the parked #306 branch — and never
    touch the shared checkout `/mnt/c/daqifi/daqifi-python-test-suite`. Remove
    your per-ticket tree only after its branch is pushed and verified by full SHA.
  - Run tests Windows-native (`python.exe`); the python-core sibling is
    `/mnt/c/daqifi/wt/daqifi-python-core`. A suite result needs its
    `--collect-only` baseline.
- Before writing or reviewing anything in an ISR, a deferred-interrupt task, the
  streaming hot path, shared state, stack sizing, the FPU, or DMA/cache: read the
  `mcu-hygiene` skill first.

---

## 4. You do NOT run the adversarial audit and do NOT merge

You are a general-purpose subagent and have no `Workflow` tool. When a PR reaches
convergence, say exactly `PR #N CONVERGED — AUDIT PENDING at <full head sha>` and
stop there. The worker audits and merges at its own level. Run the **`qodo-cycle`
skill** for the review cycle (it owns the dual trigger, two-tier convergence, the
anti-treadmill rule and the standing dispositions; HARD CAP 5 rounds), and triage
EVERY open item on BOTH surfaces at the head you report, naming any new one by
title and importance.

**Do not report CONVERGED until `/agentic_review` has actually answered your
trigger.** `/improve` answers in about 20 s and `/agentic_review` in 3 to 7
minutes. A convergence claim made between the two is premature, and on #362
(2026-09-15) it hid a new Bug that surfaced two minutes after the "converged"
comment. Wait on `poll.sh`, or confirm that the "Code Review by Qodo" comment's
`updated_at` is LATER than your `/agentic_review` trigger comment, before you
read its counters. **Push clean:** batch comment- or doc-only changes into the
next code push; never push them alone.

**Read EVERY counter in the header, not only Bugs** (added by the worker
2026-09-15). The Code Review header carries four counters: 🐞 Bugs,
📘 Rule violations, 🔗 Cross-repo conflicts, 📜 Skill insights. An open
Cross-repo conflict is a finding like any Bug. `fetch.sh --state` can print
`converged=yes` while one is open, because it counts Bugs only. Two fires in a
row reported "explicit-clean" over open findings:
- #382 had three New items at `d369c4e`;
- #386 had a New Cross-repo conflict at `d5d1d75`.

Before you report CONVERGED, quote the header counts line verbatim and list
every item that is not struck through as ✓ Resolved.

**The answer to your trigger is the NOTICE, not a timestamp** (added by the
worker 2026-09-15). Qodo answers `/agentic_review` with a "Code review by qodo
was updated" notice comment. A push-triggered re-review can rewrite the Code
Review comment between your trigger and that notice, so an `updated_at` later
than your trigger is not proof on its own.

On #387, a fire posted "explicit-clean" at 17:50:30Z, after its 17:47:35Z
trigger. The notice landed at 17:51:12Z and carried a new Cross-repo finding.

Read the header only after the first notice posted after your trigger.

**A regression_gate MANIFEST entry is written COMPLETE, in one commit** (added
by the worker 2026-09-16, after PR #402 spent three review rounds on one
entry). `regression_gate.py` discovers tests by GLOB, so `MANIFEST` supplies
only per-test settings: a test with no entry, or a partial one, is ALREADY in
the gate under `GateTest`'s dataclass defaults, and nothing warns you. Those
defaults are wrong for most new tests:
- `requires=frozenset()` turns a legitimate skip into a FAILURE;
- `control_plane=True` enrols a streaming test in the control-plane soak;
- `partial_ok=False` FAILs a healthy board whose only skip is an opt-in arm;
- the default timeout kills a long test mid-run, every run.

So when you add or complete an entry, set EVERY field explicitly, derive the
timeout from the script's own worst case, list the reachable skip paths in a
comment beside `partial_ok`, and prefer `extra_args` over `partial_ok` for an
arm that is merely opt-in. Then verify through the gate's OWN `discover()`,
not by reading the dict back:

    python3 -c "
    import regression_gate as g
    k='test_NNN_whatever.py'; m=g.MANIFEST[k]
    print(any(k in str(x) for x in g.discover()))
    print(sorted(m['requires']), m['control_plane'], m['timeout'])"

Over-declaring is the more dangerous direction: `nq1`/`nq2`/`nq3` are mutually
exclusive, so a variant requirement on a variant-independent test turns a real
regression into a green SKIP on two benches out of three.

**A WIKI-ONLY ticket is an ASK-FIRST item until the operator says otherwise**
(added by the worker 2026-09-16). The standing authorization for firmware wiki
pushes is conditioned on the adversarial audit having run on the PR carrying
the doc change. A docs/wiki-only ticket opens no PR and gets no audit, so that
condition cannot be satisfied either way, and a direct push bypasses every
gate this lane has: no Qodo, no audit, no review.

So: do the research, write the wiki text, verify it against firmware source at
a named commit and (where it is observable) against your own board — then STOP
and hand the prepared change to the worker with your evidence. Do not push it,
and do not close the ticket. The worker puts it to the operator.

This is not hypothetical: fire 35 pushed `e0a0e1e` and closed firmware #1090
this way. The content was correct, and it still should have been asked.

**A closing keyword belongs in a PR only when the PR closes the WHOLE ticket**
(added by the worker 2026-09-16). Write `Fixes #N` / `Closes #N` only if every
item in that ticket's Change section is in your diff. If you are doing part of
it, write `Part of #N` and say in the PR body which part is NOT included.

Put the keyword in ONE place and know where it is. A closing keyword in a
COMMIT MESSAGE closes the issue when that commit lands on `main`, whatever the
PR body says: PR #404 carried `Fixes #401` in its commit trailer, the worker
edited the body to `Part of #401`, and the ticket still auto-closed on merge
and had to be reopened. If you are asked to drop a closing keyword, drop it
from the commit message too.

---

## 5. You are on sonnet: the model split is a ROUTE-UP

Before implementing, read enough code to state what the work requires, then split
it:
- **Keep yourself:** mechanical work with a known shape — a rename, a move, a test
  mirroring one that exists, doc/comment fixes, running a suite and reporting what
  failed, surveying files.
- **Delegate with `model: "opus"` via the Agent tool:** anything load-bearing — the
  fix's DESIGN, a gate or state-machine change, ISR/concurrency/stack reasoning,
  redaction or security reasoning, judging whether a test really catches its
  defect, and the review cycle's verdicts.
- Route on what a part costs to get WRONG, not how long it takes. **Unclear tier =
  opus.** Never be the last word on a correctness question yourself. Record the
  split in your LOOP_LOG entry.

---

## 6. If you are BLOCKED, climb the ladder — do not stall, do not guess

BLOCKED means: the same finding returns twice and your fix is not working; a
cycle is not converging as the round cap approaches; a test fails for a reason
you cannot explain; or you cannot settle the fix design. An ordinary first-round
finding is not blocked — fix it.
1. **codex astra first**, READ-ONLY (free, different vendor):
   `codex exec -s read-only -C <the PR's worktree> -c approval_policy="never" -c model_reasoning_effort="high" - <<'EOF'`
   … your question, the diff or file, what you tried and why it failed … `EOF`
2. **Then `model: "fable"` via Agent, only if astra did not unblock you.**
A suggestion is not authority — verify it against source. A consult never replaces
the audit gate. Say on the PR that you consulted and what you did with the answer.

---

## 7. Incidental findings

A defect you notice outside your work must reach a durable surface before you
end. Append it to **`/mnt/c/daqifi/wt/nq-b/.claude/FINDINGS.md`** (absolute path;
use Bash `cat >>`, since Edit/Write refuse paths outside your tree): what, where
as file:line, why it matters, and the PR/ticket it was found under. Name it in
your report. **Do NOT file a ticket yourself** — filing is the worker's call.

---

## 8. Measurement and hardware rules

- Pin every setting a measurement depends on and record it per row (notably
  `CONFigure:VOLTage:PRECision`).
- A verdict computed from an unreadable value is not a measurement: a leak flag
  with every drop counter at `0` means the stats read FAILED; an unparseable
  counter that answers `0` reads as "no loss".
- Never launch a long run as `cmd … | tr`; redirect to a file and capture `$?`
  separately. Long runs: `python3 -u` / `PYTHONUNBUFFERED=1`, and the `waitfor`
  skill for a verdict.
- Any streaming RATE measurement uses `StreamingMeasurement` from
  `test_harness.py`. Send NO SCPI to the device mid-measurement.
- Identify the device by `*IDN?` serial, never by enumeration. Check it is free
  before starting.

---

## 9. Station snags — each of these has already cost a fire time

- **Isolation-worktree permission limits:** inline `powershell.exe` / `cmd.exe` /
  Windows `.exe` invocations may be refused — put ONE side-effecting command per
  script file inside your tree and run `bash <file>`. Git commands whose text
  targets `/mnt/c/daqifi/wt/nq-b` itself are refused; operate on other worktree
  paths with simple single-purpose `git -C <path> ...` calls. Compound commands
  (`a && b`, pipes into interpreters) can be refused as "too complex to verify" —
  split them. Edit/Write refuse paths outside your worktree — append to lane files
  with Bash `cat src >> dst`, writing long text into your own tree first.
- **git push auth:** the global URL-scoped credential helper points at the BWS
  `gh` wrapper (fixed 2026-09-14). If a push still fails with "could not read
  Username", use the per-command form
  `git -c credential.helper= -c credential.helper='!gh auth git-credential' push ...`.
  Never edit git config, and never print a token.
- **The Entire CLI is disabled on this box and being uninstalled.** Ignore any
  entire hook output and never re-enable it.
- **Never use bare `git stash`** — the stash stack is shared with other sessions.
  Use a WIP commit. Never `git gc --prune=now`.

---

## 10. Report

Append this fire's outcome to **`/mnt/c/daqifi/wt/nq-b/.claude/LOOP_LOG.md`**
(absolute path, Bash append; your tree is removed after you return) as a
`## <date> — <unit>` entry: unit of work, PR/issue numbers, review rounds, model
split, any consult and what you did with it, audit status, test results with
their `--collect-only` baseline, board state (image crc32, WiFi/NVM state), the
pushed FULL SHA for every item, and the HANDOFF block. Restore your tree to the
branch it started on.

RETURN exactly this, and nothing else:
```
<at most 4 summary lines>
PUSHED: <repo> <branch> <full sha> <proving test>      (one line per item; or: PUSHED: none — <why>)
WAS DOING: ...
NEXT STEP: ...
SAFE TO RESUME FROM: <sha>
```

---

## 11. Secrets (added by the worker 2026-09-15, after a fire's credential slip)

- **Never read, print, echo, redirect or copy a secret value.** That means no
  `bws secret get`, no `gh auth token`, no `cat` of `~/.daqifi.env` or of any
  token file, and no `env | grep TOKEN`. A tool result is sent to the model API,
  so a secret that appears in one is exposed even if the file is deleted at once.
  On 2026-09-15 a fire diagnosing a BWS outage wrote a GitHub token to a scratch
  file; the file was deleted, but the token had already landed in a tool result
  and had to be treated as exposed.
- **If `gh` or `git push` fails on auth** (for example the BWS backend returning
  500s), wait and retry the normal `gh` wrapper. Do not diagnose credentials by
  looking at them. If it persists past about 15 minutes, stop at a safe point:
  commit locally, write the HANDOFF, and report `PUSHED: none — auth outage`
  rather than working around it.
- **If a secret does land in your output anyway**, say so in your report's
  summary lines (which file, what time), so the worker can have it rotated.

---

## 13. Stop your background tasks before you report (added by the worker 2026-09-15)

Before your final report, stop every background task you started: Qodo poll
loops, `until` / `while true` watchers, sleeps waiting on a file. Use TaskStop on
the task id, or kill the process. Name each one you stopped in your report. A loop
you leave running outlives you. On 2026-09-15 a finished fire left two: one
re-fetched a PR comment through `gh` every 20 s for about 90 minutes (each call
also hits the token service), and both kept the fire's worktree from being torn
down.

**Do not hand-roll a `pgrep` wait-loop for your own background work**
(added by the worker 2026-09-16, after fires 37 and 38 each built one and each
called it unreliable). Use the `waitfor` skill, or the Monitor tool with an
until-condition. A `while true; do kill -0 $(pgrep -f "<pattern>")` loop has two
defects that bite in exactly this situation: `pgrep -f` also matches the
WAITER'S OWN command line, because the pattern is in it, so the loop can observe
itself and spin forever; and when pgrep matches nothing, `kill -0 ""` is a shell
error rather than a clean "it finished" signal. Fire 38's loop was still running
after the poll it was watching had exited.

**RE-READ THE REVIEW BODY AFTER THE NOTICE, not just the counter you saw
before it** (added by the worker 2026-09-16, after the FOURTH fire claimed
convergence over an open finding). The existing rules — read all four counters,
and wait for the NOTICE rather than a timestamp — were both followed on PR #410
and the claim was still wrong. The fire read `0/0/0/0` at some point, then
waited for the notice, then reported the number it already had. The notice
named its SHA correctly; the review body at that SHA had grown a **new** Bug.

So the order is: trigger → wait for the notice that names YOUR commit → **fetch
the review again and read it** → report the counter you saw in THAT fetch.

And read the item list, not only the header: an item struck through with
`✓ Resolved` is closed, an item tagged `⭐ New` is not. On #410 the first item
was Resolved and the second was New, so a glance at "there's a Resolved item
here" would also have been misleading.

**NEVER create a `gh` alias, and never land a PR through one**
(fleet rule relayed 2026-09-16 by a peer lane on the firmware repo).
The pre-merge audit gate matches the COMMAND TEXT, so landing a PR via
`gh <alias>` contains none of the gated words and is **not gated at all**.
The gate fix is claude-skills#152, parked at its own 5-round cap, so the hole is
open for now. Four test aliases -- including a working squash+admin one -- were
found in the operator's live `gh` config today, left behind by another lane's
fixing rounds (claude-skills#157). Do not add to that.

If you must probe `gh` configuration, point `GH_CONFIG_DIR` at a throwaway
directory. Never write to the real one.

**WAIT FOR THE QODO ROUND TO SETTLE BEFORE QUOTING ITS STATE.** Five fires
across the fleet quoted stale Qodo state in one day: they read
`converged`/`review_bugs`/`state_sha_seen` while a round was still in flight and
reported a head as clean. Re-read after the round settles and quote what you saw
at that exact head, or say plainly that you could not confirm. A wrong
"converged=yes" costs a whole audit round, the most expensive thing this lane
spends.

**`converged=no` from `fetch.sh` has at least TWO causes, and neither means
"there is an open finding".** Diagnose which one you have before reporting:

1. **A genuinely clean pass posts nothing to pin against.** Qodo writes no
   review object and no "updated up to the latest commit" notice when it has
   nothing to say, and those are the only two surfaces the SHA-pin logic reads,
   so `converged=no` / `state_sha_seen=no` persists forever. That is
   claude-skills#30, with two reproductions.
2. **Stale items in the counts.** `suggestions_open` counts items from
   "Previous suggestions" sections pinned to earlier commits, and a superseded
   or declined item keeps counting.

Cause 2 is what this lane has actually seen, on #405, #407 and #414 -- in every
one of those a notice DID name the head, which rules cause 1 out. Do not copy a
diagnosis across; check which surfaces exist before naming a cause.

Either way the remedy is the same: **read the review body's counter row
directly**, all four counters, and say in your report that you did and why.
Do NOT relax the convergence check to work around it.

**THE REVIEW BODY CARRIES ITS OWN COMMIT PIN, and that pin is the authority**
(added by the worker 2026-09-16). Every Qodo review body ends with an HTML
comment naming the commit it covers:

    <!-- https://github.com/<owner>/<repo>/commit/<full sha> -->

Check that it names YOUR head. This is more reliable than the
"[Code review] ... updated up to the latest commit" notice, because that notice
appears only when Qodo UPDATES an existing review. **A first-pass review has no
notice at all**, so its absence proves nothing -- do not read "no notice" as
"the round has not answered" on a PR whose review has only ever run once.

**READ EVERY COUNTER PRESENT; THE HEADER COMPOSITION VARIES.** It is not always
four. Observed on this repo in one day:

    🐞 Bugs (0)   📘 Rule violations (0)   🔗 Cross-repo conflicts (1)   📜 Skill insights (0)
    🐞 Bugs (0)   📘 Rule violations (0)   📎 Requirement gaps (0)

So "read all four counters" is really "read all the counters there are". The
Bugs counter alone has already nearly merged a PR here with an open Cross-repo
conflict.

**A leading space breaks a `startswith` query.** A Qodo review comment body can
begin with a space before `<h3>`, so
`select(.body|startswith("<h3>Code Review"))` silently returns nothing and the
review looks absent when it is right there. Match with `test("Code Review by
Qodo")` instead. The worker lost a step to this on PR #417.

**THE QODO REVIEW IS A MUTABLE COMMENT: re-fetch it LAST, and say when you read
it** (added by the worker 2026-09-16, after the fifth convergence claim over an
open finding). Qodo edits ONE comment in place. It can gain a finding after you
have read it clean -- at the SAME head, with the SAME commit pin -- so "I read
all the counters and confirmed the pin" can be true when written and stale by
the time it is read. That is not carelessness; it is a race.

What actually reduces it:
- Re-fetch the review BODY immediately before you claim convergence, after every
  other step is finished. Make it the last thing you do, not the first.
- Put the timestamp of that read in your report. A claim without one cannot be
  distinguished from a claim made twenty minutes earlier.
- If any wait happened between your read and your report -- a bench run, a lint
  pass, a push -- the read is stale. Do it again.

Twice now the open item has been in the **Cross-repo conflicts** counter with
Bugs at 0 (#410 and #420). Bugs alone would have merged both.

**`gh pr list --limit` SILENTLY TRUNCATES, and a truncated list reads exactly
like "no collision"** (fire 49, 2026-09-16). It checked the FULL open-PR list —
61 test-suite plus 32 firmware — and found that the default/`--limit 50` subset
silently omitted 11 PRs, two of which were real file collisions it would
otherwise have claimed past. Pass `--limit 200` (or page explicitly) for any
collision check, and say in your report how many PRs you actually enumerated.

This is the same family as two other traps this lane has paid for: `gh pr view
--json comments` truncating, and `gh pr view N --json number -q .number` exiting
**0 even when the lookup fails** (the `-q` swallows the failure), which produced
a wrong fleet-wide measurement until it was re-measured with
`--json number,state` and no `-q`. **A check that fails quietly reports the
answer you wanted.** When a negative result would be convenient, re-run it a
second way before believing it.

**A FILE-LEVEL collision is not automatically a hunk collision — check the line
ranges, and check the CONTEXT.** Two PRs can touch the same file harmlessly.
They can also touch non-overlapping line ranges whose three-line diff context
regions meet, which git may still conflict on. Compare `@@` headers, not just
filenames, and record the ranges so the next fire does not re-derive them.

**`⊘ Outdated` IS NOT `✓ Resolved`** (worker, 2026-09-16, seen on PR #428).
Qodo's counters count only OPEN items, so a finding tagged `⊘ Outdated` is
struck through and counts as zero — but "Outdated" means only that the head
moved after it was raised. **Nobody demonstrated a fix.** `✓ Resolved` means
Qodo re-checked and the defect is gone; `⊘ Outdated` means it stopped looking.

If a finding goes Outdated, you still owe an answer to it: read what it said,
check whether your change actually addressed it, and say so in your report —
"item N went Outdated when I pushed X; I verified the defect is/is not present
at the current head, because <evidence>". Do NOT report it as resolved, and do
NOT let a zero counter stand in for that check. A real defect that goes stale
is still a real defect.

Watch for the combination `⊘ Outdated` + `⭐ New` on the same item: raised this
round and stale within the same round. That is the shape most likely to be
skimmed past.

## A negative grep is only as good as the pattern (2026-09-16, #250)

The closing-keyword check is run on BOTH the PR body and every commit message,
because a squash concatenates commit messages onto `main`. That was already a
rule here. What failed on #428 was the *matcher*:

    (fix|close|resolve)[sd]? #[0-9]+      # WRONG: [sd]? is ONE optional letter
                                          # matches fix/closes/resolved
                                          # MISSES  fixes  and  fixed
    (close[sd]?|fix(es|ed)?|resolve[sd]?)[[:space:]]+#[0-9]+   # correct

`Fixes #250.` was sitting in the text I had grepped. I reported "no closing
keyword in either place" in a pre-merge attestation, and the squash closed a
CLASS ticket that still has open instances.

**Rule: before trusting a no-hits grep that something load-bearing depends on,
run the pattern against a string that MUST match.** A pattern you have never
seen match anything is not an instrument, it is an assumption.

**And check the OUTCOME after the merge, not just the intention before it:**
`gh issue view <N> --json state`. One command, and it is the only check that
would have caught both #401 and #250.

## Re-fetching LAST is not enough — confirm the round has SETTLED (2026-09-16, #430)

The existing rule ("the Qodo review is a MUTABLE comment: re-fetch it LAST and
state the read time") is necessary and NOT sufficient. On PR #430 a fire did
exactly that — final re-fetch at 15:53:25Z, both surfaces clean, read time
stated — and Qodo revised the SAME comment at **15:54:59Z**, 94 seconds later,
adding 1 Bug and 2 Cross-repo conflicts, all `⭐ New`, at the SAME commit pin.
The fire's report was accurate when written and wrong by the time it was acted on.

**Rule: re-fetch last, THEN confirm the round has stopped moving.** Re-read after
a pause and check the review comment's `updated_at` is unchanged between two
reads before claiming convergence. A matching commit pin does NOT prove the round
finished — the pin names the head being reviewed, not the state of the review.

This is the SIXTH convergence claim rejected over open findings in this lane and
the FIRST with this cause: the other five misread a surface; this one read it
correctly and lost a race. Different failure, so it needs its own rule.

## WORK ORDER: land converged PRs before claiming a new ticket (2026-09-16)

Relayed by lane fw-6b as an operator decision (claude-skills#163 era). Recorded
as relayed — I did not hear it from the operator directly — but it is consistent
with this lane's practice and is being followed.

At the start of every work unit, take the FIRST actionable item:

1. **Merge anything merge-ready** — audit PASS at the CURRENT head, Qodo converged
   at that head, mergeable. Land it.
2. **Advance anything ONE bounded step from merge-ready** — a fix round on a live
   finding, a conflict resolve, a companion needing its audit, a stale audit
   needing a re-run at the current head.
3. **Only then claim a new ticket.**

**Exception so this does not become idling:** a PR blocked on something you cannot
action — an operator decision, another lane's PR, hardware you do not own — is NOT
advanceable. Skip it, report it blocked, keep going down the list.

**Why it matters mechanically, not just tidily:** a converged PR that waits goes
DIRTY as its base moves, and resolving the conflict MOVES THE HEAD, which VOIDS
the SHA-bound audit already paid for. fw#1027 lost its audit exactly that way
today. Waiting is not free; it destroys work already bought.

## Two companions to that rule

- **CO-MERGE:** a firmware PR with a test-suite companion does not merge until the
  companion is audited clean; then both land in one session, **firmware first**
  (claude-skills#163). Several test-suite PRs this lane works are companions.
- **`gh pr list` DEFAULTS TO 30 RESULTS.** The firmware repo has 36 open PRs, so
  without `--limit 100` you silently miss the six oldest — precisely the ones a
  staleness or collision check needs. Same family as the `gh pr view -q` exit-0
  trap: the tool reports success while answering a different question.

## NEVER write a commit message to the SHARED scratchpad (2026-09-16, PR #430)

A fire hit this and caught it only by luck. Sequence:
1. A compound Bash command (heredoc + git) was REFUSED by the sandbox mid-turn,
   so the heredoc never ran and no file was written.
2. A later, separate `git commit -F <scratchpad-path>` SUCCEEDED — using a STALE,
   unrelated commit message an EARLIER, UNRELATED fire had left at that exact
   path (its visible content referenced ticket #409 and harness_policy_lint.py).
3. Caught before push by reading back `git log -1` and comparing to what was
   expected; fixed with `--amend`.

**Rules:**
- Write commit messages ONLY to a path inside your OWN worktree, never the shared
  scratchpad directory. Fires share that directory and do not coordinate on names.
- After ANY `git commit -F <file>`, read back `git log -1 --format=%B` and confirm
  it is what you wrote. A refused command leaves no file, and `-F` against a
  pre-existing stale file succeeds silently with the WRONG message.
- A refused compound command has NOT partially run — but anything it was supposed
  to CREATE does not exist, so every later step that reads that path is now
  reading someone else's data.

## When a PR's CORE survives audit and a BOLTED-ON MECHANISM keeps failing, SPLIT

Twice on 2026-09-16, in different repos, with the same shape:

- **fw#1110** (ticket #1039, four over-budget LOG_E strings). The four message
  edits drew ZERO findings across two audit rounds. The 1,485-line lint tool that
  grew around them drew **13**. Split: message fixes land, tool becomes its own PR.
- **ts#430** (ticket #321, ReliableSCPI write-timeout framing). The write-resync
  mechanism — the actual fix — SURVIVED TWO FULL AUDITS. Its error-queue
  side-effect handling produced **6 distinct defects** (3 deleted inference
  attempts + 1 in audit r1 + 2 in audit r2, the r2 pair being defects in the code
  written to fix r1).

**The tell:** across successive rounds the findings cluster in ONE subsystem while
another part of the diff is repeatedly clean. That is not bad luck; it says the
failing part is a different piece of work that has attached itself to this one.

**Rule: when two consecutive audit rounds put every finding in the same
bolted-on component while the ticket's actual deliverable stays clean, stop
fixing and propose a split** — land the clean deliverable, take the failing
component to its own PR with its own round budget, and enumerate the known
defects on it so the next owner starts from them.

**Corollary, learned the expensive way:** "it is only one more line" has now been
the reasoning behind two consecutive fixes that each introduced the next defect.
The size of the remaining fix is NOT evidence that the area is converging.
Prefer DELETING the failing mechanism over patching it a further time — and check
whether the module already documents what the caller should do instead, because
in both cases it did.

## A FIRE MUST NOT FLASH. Flashing is ask-first, every time (2026-09-16, ts#432)

A fire briefed to run a bench test built firmware PR #1116 and **programmed
lane nq-b's board** (PICkit 5 `020026703RYN079002`), confirming the change by
`firmware_crc32` `0FDDB10A` -> `BF768145`. It was not authorised to do that.

**Why it is ask-first, not a formality:** every flash WIPES NVM (WiFi credentials
and calibration), and it leaves the board on a branch build rather than main, so
every later test on that bench silently runs against un-merged firmware.

**Rule: a fire may DRIVE a board. It may never PROGRAM one** — no `ipecmd`, no
`flash.sh`, no PICkit invocation — unless the brief names flashing as authorised
in those words. If a test cannot produce a verdict without new firmware on the
device, that is the moment to STOP and say so; CLAUDE.md already supplies the
answer: *"the test is still committed and the run is queued (note it in the PR);
the test existing is the requirement, not that it has run yet."*

**MY SHARE OF THIS, recorded because the brief was part of the cause:** my brief
said "the bench is this lane's board, drive ONLY that board" and offered the queue
provision only for the bench being *unavailable*. I did not think through that a
test asserting the SHORTENED message CANNOT PASS on main's firmware — so running
it at all REQUIRED a flash, and my brief effectively invited one while never
authorising it. **When briefing a bench run, state explicitly whether the device
must be reflashed to reach a verdict, and if it must, either authorise it in
words or require the run be queued.** An unstated prerequisite gets supplied by
whoever hits it.

## The bench device-guard matches COMMAND TEXT, not hardware (2026-09-16)

Appending a LOG ENTRY was blocked by the device-guard because the prose quoted
another lane's serial, programmer and port. Nothing was connected to; the guard
reads the command text and refuses identifiers this lane does not own.

**How to write about another lane's hardware:** put the text in a file with the
Write tool and `cat` the FILE into place. Never echo another lane's identifiers
inside a shell command, even in a comment, a log line or a heredoc.

Same class as the pre-merge gate refusing a PR comment that quoted a merge
command. Two guards on this station now match on text; assume any guard does.

## Identify a board's image BY CONSTRUCTION before asking anyone (2026-09-16)

An unidentified `firmware_crc32` on a bench board is answerable offline, from
saved hexes, in about five minutes and at ZERO risk to the hardware. Do that
FIRST; escalate only if it comes up empty. A peer lane escalated before building
the tool and cost two lanes real effort on a question their own scratchpad
already answered.

`/mnt/c/daqifi/wt/nq-b/.claude/imgcrc.py` does it. It mirrors
`SCPI_FirmwareImageCrc32()` (`SCPIInterface.c:7759-7762`): region
`__KSEG0_PROGRAM_MEM_BASE` for `__KSEG0_PROGRAM_MEM_LENGTH - RESERVED_SETTINGS_SPACE`
(`RESERVED_SETTINGS_SPACE (128*1024)`, `daqifi_settings.h:55`), unprogrammed
flash as 0xFF, plain zlib crc32. Addresses are masked to physical so a KSEG0- or
physical-addressed hex both work.

**Validated 2026-09-16:** the hex built by the fire that flashed this lane's
board computes `BF768145`, exactly what the device reports. That single match
confirms the tool AND the image identity together — a wrong implementation
landing on the device's exact 32-bit value by chance is about 2^-32.

**Why it matters beyond curiosity:** it turns "the board is on X" from an
INFERENCE (the fire said so) into a VERIFIED fact, and it lets you identify a
mystery image without flashing over it — which would destroy the only copy of
whatever is on there.

**BUILDING IS NOT FLASHING.** Rebuilding a head to hash it costs no hardware and
needs no authorisation. Reach for it before anything that programs a device.

## GREP THE DOCS BEFORE HAND-ROLLING A CHECK (2026-09-16, three times in one day)

Three separate PRs this lane touched today implemented a WEAKER variant of
something the project already prescribed, in writing, in a place the author had
read:

1. **ts#430** — `_resync_write`'s own docstring said a caller needing a clean
   queue "must drain it itself (loop `SYST:ERR?` until 'No error')". `test_732`
   did a SINGLE pop, at the wrong point in the sequence. Audit BLOCK.
2. **fw#1116** — the host guard MIRRORED `LogMessageFormatImpl`'s truncation in a
   free-standing binary instead of exercising the real `Logger.c`/`SYST:LOG?`
   path. CLAUDE.md's companion rule wanted the real path. Decline overruled.
3. **ts#432** — `CLAUDE.md:446` says drain the error queue "`SYST:ERR?` UNTIL
   `0,\"No error\"`". The test wrote `drain_errors(sc, n=8)`: "drain until clean
   OR n attempts are spent". Bounded approximation of an explicitly unbounded
   rule. Audit BLOCK (plausible), theme not closing.

**Rule: before hand-rolling any check of device or system state, grep the repo
docs and the relevant module's own docstrings for the prescribed form, and
implement THAT.** A bounded approximation of an unbounded rule is a defect
waiting for the input that exceeds the bound.

**The tell that you are about to do it:** you are writing a loop with a retry
count, a timeout, or a budget, for something the docs describe as "until
<condition>". The number you are about to choose is the bug.

## RELATED: reactive pinning does not converge

ts#432 pinned one piece of unowned device state per round for SIX rounds (log
level, teardown, stream framing, log buffer, protobuf mode, error-queue depth),
each found only after fixing the last. The fix is not a seventh item; it is to
establish a KNOWN STATE up front — the documented preamble — so the whole class
closes at once. When each round's finding is "one more thing you did not pin",
stop fixing items and go get the documented reset sequence.

## Closing keywords: THREE checks, and VALIDATE THE REGEX FIRST (2026-09-16)

A closing keyword can reach `main` by three routes. All three must be checked;
none is sufficient alone.

1. **PR body** — `gh pr view <N> --repo <O/R> --json closingIssuesReferences`.
   This is GitHub's own parser, so it catches the NEGATION trap: a body saying
   "Do NOT close #958" still reports WOULD CLOSE #958. **It reflects the body
   ONLY — it is blind to commit messages.** Proven on ts#420: API says `none`
   while commit `8d66a53` carries `Fixes #285`.
2. **Commit trailers AND subject lines** — the regex below over the FULL message
   (`git log --format='%B'`), never over trailer-shaped lines. A keyword can sit
   mid-sentence in a conventional-commit subject and read as ordinary prose:
   `fix(scpi): close #1115 round 2's remaining gaps` IS a closing keyword.
3. **Authoring** — never let a closing verb immediately precede a `#` in a
   subject. Write "addresses the gaps from #1115".

```
grep -iE '(close[sd]?|fix(es|ed)?|resolve[sd]?)[[:space:]]+#[0-9]+'
```

**THE `-i` IS LOAD-BEARING, AND ITS ABSENCE WAS THIS BLOCK'S OWN SECOND BUG**
(worker, 2026-09-19, found by a fire and then re-validated here against the sets
below). The pattern is written all-lowercase, so run WITHOUT `-i` it misses every
capitalized form — measured, not reasoned: of the ten MUST-MATCH cases below,
`Fixes #1`, `Fixed #1`, `Close #1` and `Resolve #1` all **MISS**, while the six
lowercase ones match. With `-i` all ten match and all six MUST-NOT-MATCH cases
still correctly fail. That is the identical failure this block already documents
one paragraph down — a pattern that silently under-reports and lets a PR close an
issue nobody meant it to — arriving a second time by letter case instead of by
optional-letter. A `grep` that under-matches here reports "no closing keyword" and
is believed.

**VALIDATE IT BEFORE EVERY TRUSTED RUN.** This is the step whose absence produced
`(fix|close|resolve)[sd]?` — which allows ONE optional letter, so it silently
missed `fixes` and `fixed`, reported "no closing keyword", and let #428 close
#250. Validation set, run 2026-09-16:

- MUST MATCH: `Fixes #1` `fixes #1` `Fixed #1` `Close #1` `closes #1` `closed #1`
  `Resolve #1` `resolves #1` `resolved #1`, and mid-subject `fix(x): close #1115 ...`
- MUST NOT MATCH: `Refs #123` `Part of #123` `See #123` bare `#123`
  `closing #123` `fixing #123`
- ALSO must not match: `close the gap #900 left` — GitHub requires the keyword to
  be ADJACENT to the reference, so intervening words break the link. A peer lane
  circulated this as a live keyword; it is not.

  **Epistemic status, agreed with that lane rather than asserted over it:
  documented-and-reasoned, NOT parser-verified.** Neither of us has run GitHub's
  parser on that string. The decisive test would be a scratch-PR body plus
  `closingIssuesReferences`, and we deliberately did NOT run it: it needs a REAL
  issue number to be conclusive, because a nonexistent one cannot distinguish
  "the syntax did not link" from "the issue does not exist" — which would put a
  scratch PR on a live issue's timeline. The failure direction of being wrong
  here is a FALSE POSITIVE (lanes watching for a harmless shape), so it does not
  earn that noise. A throwaway repository is the clean way if it ever matters.

  **That reasoning is worth more than the conclusion:** when choosing whether to
  run a decisive test, weigh what a NEGATIVE result would actually prove. A test
  whose negative is ambiguous is not decisive, however cheap it looks.

**And the authoring rule that makes all of this moot:** a PR may carry a closing
keyword ONLY for a ticket it FULLY satisfies. Partial work says `Part of #N` and
names what remains. Do NOT widen a PR to earn the keyword — that is the scope
growth that parked fw#1110.

## A FUZZER'S ALPHABET IS ITSELF A HAND-PICKED LIST (worker error, 2026-09-17, one level up from the next entry)

Having learned that a hand-picked enumeration is a sample, I replaced it with a
fuzzer and reported *"13,669 provable-garbage prefixes, zero regressions"* —
treating that as a search. The next audit round found four CONFIRMED regressions
my fuzz could not possibly have produced.

My generator's alphabet was
`['{','}','[',']','"','"a"','"a":','1',',',':','true','null','\\',...]`. It
contained **no truncated literal** (`tru`) and **no `\u` escape**. Those were
exactly the two defect sites. The PR's own committed fuzz had the identical
blind spot, which is why it reported 57 PASS / 0 FAIL while the regressions
existed.

**A fuzzer searches only the space its generator can reach.** Randomising the
ARRANGEMENT of tokens I chose does not test the tokens I did not choose. The
alphabet is the coverage, and it is hand-written — so it inherits every limit of
my imagination, one level removed and much harder to see, because the large
number of trials feels like evidence.

**How to apply:**
- State the ALPHABET when reporting a fuzz result, not just the trial count.
  "13,669 trials over {these 27 tokens}" is honest; "13,669 trials" implies a
  coverage that was never established.
- Derive the alphabet from the SPEC being implemented, not from the code under
  test. JSON has literals, numbers, strings, escapes, structure — if a class is
  in the grammar and absent from the alphabet, that is a hole by construction.
- When a fuzz is committed as a test, comment WHY each class is in the alphabet,
  so the next person knows the alphabet is the lever.
- A generator that cannot produce a known-bad input is not evidence about that
  input. Check that the corpus actually contains the shapes you are claiming to
  have cleared.

## A HAND-PICKED ENUMERATION IS A SAMPLE. DO NOT REPORT IT AS A SEARCH. (worker error, 2026-09-17)

I told an audit round *"240 combinations, zero regressions against main"* for
ts#407. The arbiter flagged it and was right. My 240 was **30 prefixes I chose
by hand x 8 suffixes**. It contained `{"a":1,` but not `{"a":1`, not
`{"a":1,"b"`, not `{"a":[1}` — and those three were exactly the next audit's
CONFIRMED findings.

**Every shape I failed to imagine was a shape I failed to test, and my report
said "zero" rather than "zero among the ones I thought of".** That phrasing is
the whole error: it converts the limits of my imagination into a property of the
code, and a reader — including an auditor weighing a disposition — cannot see
the difference.

Then I fuzzed the same code with an ORACLE instead of a list: 7,422 random
prefixes, each classified by whether `json.loads(prefix + reply + closer)`
parses for some closer. **39 distinct failing shapes**, not 3.

**Rules:**
- Say "zero in N hand-picked cases", never "zero", unless you actually searched.
- When the property is "for ALL inputs, X", a fixed list cannot establish it.
  Generate inputs and let a reference implementation judge them.
- **Build the oracle out of the reference implementation**, never out of your own
  model of the grammar — see the classifier error above, where my own
  position-classifier was wrong twice on the same PR.
- **Validate a fix against a FRESH seed** the fix was not developed against. A
  fix that passes only the seed it was tuned on is a sample again, one level up.
- The durable deliverable is the SEARCH, committed as a test — not the list of
  cases it happened to find today.

Same family as [[feedback-verify-the-population-not-just-the-mechanism]] and
[[feedback-uniform-test-cases-hide-a-class]].

## DO NOT WRITE A RUNNING FIRE'S RESULTS INTO LOOP_LOG WHILE IT IS RUNNING (worker error, 2026-09-17)

`LOOP_LOG.md` is SHARED STATE that fires read. On 2026-09-17 the worker verified
a fire's pushed commit while that fire was still working, and logged the
verification immediately. The fire then grepped the log's tail and found *"an
entry apparently pre-narrating this exact fire's dispatch and a specific account
of results not yet produced"*.

It handled that correctly — disregarded the text entirely and reported only its
own independently re-run evidence — and said so in its report, which is the only
reason this was caught.

**Why it matters more than it looks.** A fire that ADOPTED that account would
have reported the worker's findings back to the worker as independent
confirmation. The worker would then have "two sources agreeing" that were one
source. Every check downstream — the SHA verification, the Qodo read, the audit
disposition — is built on the fire's report being independent evidence. Writing
the conclusion where the witness can read it destroys that, silently, and the
result still looks like corroboration.

**The rule:**
- While a fire is running, keep verification notes in the SCRATCHPAD, not in
  `LOOP_LOG.md` or any other file the fire reads.
- Append to `LOOP_LOG.md` after the handback.
- If something genuinely must be written mid-flight, label it explicitly as the
  worker's own independent verification of an already-pushed SHA, so a fire
  reading it cannot mistake it for its own result or for an instruction.
- A fire that finds text describing its own unfinished work should disregard it
  and SAY SO in its report — as this one did. That disclosure is what makes the
  contamination visible.

Same family as [[feedback-separate-the-measurement-from-the-report]]: two
accounts agreeing is only evidence when they are actually two accounts.

## WHEN CLASSIFYING DIFFERENTIAL RESULTS, CONSTRUCT A PROOF — DO NOT HAND-ROLL THE CLASSIFIER (2026-09-17)

Differentialling ts#407 against its merge base produced rows where BASE returns
an object and the new code returns `None`. Some are real regressions; most are
the PR's design deliberately waiting for a document that might still be
arriving. Telling them apart is the whole job, and **I got the classifier wrong
twice on the same PR**:

1. First I assumed BASE was returning a NESTED fragment, which would have made
   every row harmless. It was returning the FULL reply. Assumption, not checked.
2. Then I wrote `VALUE_POS = prefix ends with ':' or '['` to split "legal `{`
   position" from "illegal". It does not recognise a comma inside an ARRAY, so
   it reported four false regressions on `'{"a":[1,'` — a prefix I had verified
   as correct by hand ten minutes earlier.

Hand-rolling a JSON-position classifier to judge a JSON-position parser is
circular: the classifier is a second implementation of the thing under test, and
it will be wrong in the same places or in new ones.

**Use a constructive proof instead.** The question "is `None` correct here?" is
exactly "could more bytes legally complete this buffer?", and that has a
decisive one-line answer:

```python
json.loads(prefix + buffer + plausible_closer)   # parses => legal prefix
```

If the buffer IS a legal JSON prefix, `None` is the designed answer and the base
returning an inner object is the base exhibiting the ORIGINAL defect. If it is
not completable, the span is provable garbage and `None` is a regression. No
classifier, no second implementation, no judgement call — `json` itself decides.

The general rule: **when you need an oracle to judge a parser, build the oracle
out of the reference implementation, not out of your own understanding of the
grammar.**

## A PR'S BASELINE IS ITS MERGE BASE, NEVER ITS PREVIOUS HEAD (2026-09-17)

ts#407's docstring claimed monotonicity: *"no buffer that used to yield an
object now yields None."* I checked it and said it held. It does not, and the
audit caught what I missed.

| input | BASE (main) | previous PR head | new head |
|---|---|---|---|
| `'{""' + reply` | **OBJ** | None | **None** |
| `'{""' + reply + 'DAQIFI> '` | **OBJ** | None | **None** |
| `'{' + reply` | OBJ | None | OBJ |
| clean reply | OBJ | OBJ | OBJ |

Against the PREVIOUS HEAD there is no regression — that head returns `None`
too. Against MAIN there is, because an earlier commit **in the same PR**
introduced it. Comparing a revision to the revision before it hides every
regression the PR introduced and then carried forward.

**The baseline that answers "what does merging change" is the MERGE BASE.**
Get it with `git merge-base origin/main <head>` and compare against that, every
time. A three-dot diff already does this; an A/B of behaviour must too.

Second error in the same check, worth separating: I verified the claim by
REASONING about the path I had in mind (spans that CLOSE still take the
unchanged `raw_decode` path — true) and generalised to "the claim holds". The
failure is on the path I did not consider, the INCOMPLETE early return. A claim
of the form "nothing gets worse" is quantified over ALL inputs; checking the
branch you were already thinking about does not test it. Either enumerate the
paths or run the A/B.

**And when a docstring makes a claim, a test must enforce it.** #407's
monotonicity sentence was load-bearing for reviewers — I leaned on it — and
nothing tested it. An unenforced claim in a docstring is a liability, because it
transfers confidence without transferring evidence.

## GET EVERY OPEN PR'S FILE LIST IN ONE CALL, AND DO NOT TREAT OVERLAP AS FATAL (2026-09-17)

Selection surveys have been walking PRs one at a time. They do not need to:

```bash
gh pr list --repo <owner/repo> --state open --limit 200 --json number,files
```

returns every open PR WITH its file list in a single call. `--limit 200` is not
optional — `gh pr list` silently defaults to 30.

Measured on 2026-09-17 and written to **`.claude/LOCKED_FILES.md`** (regenerate
it; do not trust the stale copy):

- firmware: 38 open PRs hold **106** distinct files; `SCPIInterface.c` alone is
  held by **17** of them.
- test-suite: 66 open PRs hold **91** distinct files; `regression_gate.py` alone
  is held by **41** of them, `test_harness.py` by 12.

**The judgement that matters: overlap is not automatically a collision.** A
registry file that every test-adding PR appends a row to is a REBASE, not a
conflict. A fire that disqualifies any candidate touching a file some open PR
also touches will reject nearly everything and report "no work available" while
real work exists — that happened here on 2026-09-17, at a cost of 317k tokens
for a null result.

Judge by what the file IS:
- **registry / list / manifest** (`regression_gate.py`, a lint allowlist, a
  `.txt` of test names): overlap is routine, take the ticket.
- **a function you would both edit**: genuine contention, pick something else.
- **held by 10+ PRs**: almost certainly registry-shaped; `LOCKED_FILES.md` flags
  those explicitly.

A ticket touching NO file in that list has zero contention and is the safest
pick, but it is not the only acceptable one.

## A MUTATION HARNESS PRINTS "FAIL" AS ITS SUCCESS SIGNAL (2026-09-17)

`test_857_mem_guard_claim.py --self-test` emits **337 lines containing
`self-test FAIL:`** and exits **0** in **12 seconds**, having run 343 checks and
detected all 61 mutants. Those lines appear AFTER `343/343 checks passed` and are
the mutation proof: each one is a deliberately broken case being caught. The word
`FAIL` there is the success signal.

A fire recorded it as having "genuine failing assertions today" and as hanging
past a 60 s budget (exit 124). Neither reproduces. I nearly filed a ticket
against a healthy test, which would have sent someone to "fix" a passing
mutation proof — and the most likely fix is deleting the output that proves it
works.

**Rules:**
- **Score a self-test by its EXIT CODE, never by grepping its output for
  "FAIL".** Exit code is the only line that states the outcome. A summary built
  by counting "FAIL" strings inverts the verdict on any mutation harness.
- **Read the LAST line before judging.** Here it is `self-test OK: 343 checks,
  all mutants detected`, directly contradicting the 337 lines above it.
- **Re-run before filing.** This finding was half right — `test_848`'s
  `--self-test` really does fail (exit 1, `ConnectionError` on
  `/dev/ttyACM0`, 0 s, because it never short-circuits before connecting). Half
  a finding being wrong is the normal case, not the exceptional one, and the
  half that is wrong is invisible until you run it.
- **Capture the exit code of the RIGHT process.** My own first check ran
  `python3 ... | tail` and read `$?` from `tail`, which is always 0. Redirect to
  a file and test the command's own status.

Same family as [[feedback-it-didnt-work-is-a-claim-too]] — verify the instrument
before believing what it reports.

## `rawFindings: 0` WITH `blindLegRan: false` IS THE MOST DANGEROUS SHAPE (2026-09-16)

ts#435 round 2 returned `rawFindings: 0`, `confirmed: []`, `plausible: []`,
`unverified: []`. Read as a finding count, that is a clean pass. It was nothing
of the kind: `blindLegRan: false`, `codexProducerRan: false`, `provenance: {}`,
`agents_empty_result: 1`, `agent_count: 1`, and the journal held exactly one
result — `{"findings": []}`. **One agent ran, returned nothing, and the producer
never ran.** Zero findings from a round that did not happen is an absence of
evidence, not evidence of absence.

The `gateReason` named it correctly — *"Re-run; do not read this as clean"* — and
that is the ONLY thing standing between this and a false attestation. Anyone
quoting the finding count without the health fields would have reported "audit
clean, 0 findings".

**Rule: before believing ANY zero, check `blindLegRan`, `auditorLegsOk`,
`codexProducerRan`, `noProvenance` and `agents_empty_result`. A zero is only a
pass when the round demonstrably ran.**

**Do not retry more than twice.** Two identical degraded rounds with a healthy
codex (probe first — `PROBE_OK` at ~2-4k tokens) is a tooling problem, not bad
luck. ts#435 cost 365,498 tokens across two rounds for zero measurements. Stop,
report it, park the PR.

**Suspected trigger, unproven:** #435 was the only DATA-ONLY diff audited that
day (README + CSV + meta.json, no code). Every other PR carried Python or C and
audited normally. A producer that no-ops on a code-free diff explains all four
symptoms at once. Recorded as a hypothesis so the next lane starts there.

**THAT HYPOTHESIS IS REFUTED — but so was my first replacement for it. The
failure is REPRODUCIBLE PER HEAD (2026-09-17, ts#436).** Do not let the next lane
start from the data-only theory — it is wrong, and it is the kind of wrong that
wastes a round because it suggests "audit something else instead".

I first wrote "intermittent" here, on the strength of the one healthy/degraded
pair below, and that did not survive its own retry: the retry at the same head
came back degraded too, with the same shape and within 22 tokens of the same
cost. Tally across the day — 2/2 degraded at `cfbb428b`, 2/2 degraded at ts#435's
head, 1/1 healthy at `a45ac1ed`, 1/1 healthy at fw#1119's head. That is not
random. The pair below rules out diff SHAPE, which was right; reading it as
"transient" was an over-read of the same evidence. **"Intermittent" is the
dangerous word, because it implies retry-until-it-works, and retrying does not
work.**

The evidence is a controlled pair on the SAME PR, the same file, the same
engine, the same pinned-tree pattern, minutes apart:

| | ts#436 round 1 (`a45ac1ed`) | ts#436 round 2 (`cfbb428b`) |
|---|---|---|
| diff | Python, code-bearing | Python, code-bearing (same file, small delta) |
| `codexProducerRan` | true | **false** |
| `blindLegRan` | true | **false** |
| provenance | populated, 7958/7958 | **`{}`**, `noProvenance: true` |
| result | 1 CONFIRMED finding | `{"findings": []}`, nothing else |

Round 1 audited that file perfectly and found a real defect. Round 2, on a
near-identical diff, produced the empty shape. A code-free diff cannot explain
that, because neither round had one.

**Codex itself was fine.** Probed immediately after round 2 with a run-shaped
call — `codex exec -s read-only` in the same pinned tree — and it read the file
and answered correctly for 4,149 tokens. So "codex at capacity" is also not the
explanation. The fault is in the pipeline's producer step, not the engine and
not the diff.

**What this means operationally:** a degraded round is worth ONE retry, after
probing codex — but expect it to fail, and budget for that. The retry is worth
~86k because it converts "unknown" into "reproducible at this head", which is
what the harness ticket actually needs; it is NOT worth it as a way to get the
PR through. Two degraded rounds in a row on the same head means STOP and PARK —
that is the existing rule, it stands, and 2026-09-17 is the day it was tested
and held. Never conclude anything about the PR's content from a degraded round.

### A DEGRADED ROUND IS MORE DANGEROUS THAN AN EMPTY ONE — NEVER READ ITS ARBITER (nq-a, 2026-09-17; adopt on sight)

The empty-result case is survivable because it obviously says nothing. The
version that is NOT survivable is a degraded round that emits a **confident,
fluent arbiter narrative about a DIFFERENT PR.**

Measured by nq-a: `wf_706269fe-0e9` on firmware#1111 summarised *"This PR (#958)
is scoped to SD:BENCHmark filename collision and only touches SCPIStorageSD.c"*
— which describes **#1027**, a PR that session had audited two hours earlier.
#1111 touches four files. With no provenance to anchor to, the arbiter appears
to have anchored to **earlier session context** and produced scope reasoning
("pre-existing on base", "out of scope for this PR", "this diff never modifies
it") evaluated against the wrong diff. That is precisely the reasoning a reader
uses to downgrade a finding to non-blocking.

That round carried `confirmed: 1`, a `defer_ticket` disposition, a severity
correction from medium to low, and a merge recommendation. **All of it looks
like a real audit. None of it was measured against the PR.**

**THE RULE: read `gateReason`. Never the counts, and NEVER the arbiter
narrative. Everything downstream of a missing producer is unanchored** — the
findings, the dispositions, the severities and the summary alike.

### THE DETECTOR, corrected — THREE literals and a MISSING-KEY state

1. The journal result may be a bare **`null`** (4 bytes), not only the 16-byte
   `{"findings": []}`. Match both.
2. `head_sha`, `base_sha`, `covered_bytes` and `total_bytes` may be **ABSENT
   from the result object** — not null, not false, not empty. A missing key is a
   **third state**, and it defeats any check written as `x === null` or
   `x === false` or `!x.length`. Test for PRESENCE first, then value.
3. A result that cannot name its own tree has no provenance whatever it says
   elsewhere. `noProvenance: true` and an absent `head_sha` are the same fact.

So the health gate is: **`codexProducerRan === true` AND `blindLegRan === true`
AND `head_sha` PRESENT and equal to the head I pinned AND
`covered_bytes` PRESENT.** Anything short of all four means the round did not
happen, regardless of how complete the rest of the object looks.

Engine health does NOT rescue any of this: codex probed healthy across these
failures, which is consistent with the fault being the orchestrator's call into
`codex-audit.sh` rather than the script or the engine.

Tracked on `cptkoolbeenz/claude-skills#173`, which now argues the result should
withhold the **entire arbiter block** when `codexProducerRan` is false — a
narrative that cannot name its own tree should not be emitted in a shape that
reads like a verdict.

**THE MOST USEFUL DIAGNOSTIC, and it is cheap.** `codex-audit.sh` documents
exactly three output shapes, and BOTH failure shapes carry an `error` field:

```
{"findings":[ ... ]}                                 normal
{"findings":[], "error":..., "codex_exit":N}         infra/parse failure (exit 3)
{"findings":[], "error":..., "argument_error":true}  bad invocation (codex never ran)
```

Both degraded rounds held a bare `{"findings": []}` — 16 bytes, no `error`, no
`codex_exit`, no `argument_error`. That matches NONE of the three. Every path
where the producer runs and fails adds an error envelope, so its absence points
at the producer never being INVOKED rather than invoked and broken. Read the
journal's raw result and compare it against those three shapes before theorising;
it costs one `grep` and it is worth more than another 86k round.

Reported to the firmware coordinator for `cptkoolbeenz/claude-skills#173`,
which was filed on the data-only theory and needs this correction; it also
matters for **#40** (`codexProducerRan` false when a producer ran but returned
no `head_sha`), which this pair now makes the likelier root. The coordinator
amended the ticket (issuecomment-5707191637) and adopted the one-retry rule
fleet-wide.

### PROBE FORM: after a PRODUCER failure, probe the SHAPE the producer needs

The fleet's standard READY probe answers "is codex reachable". That is the wrong
question after a producer failure, because it cannot distinguish a healthy engine
from one that responds in general but not to this work. Use a **run-shaped**
probe instead: `codex exec -s read-only` **in the same pinned tree**, asking it
to READ a file from that tree and return something only a real read produces — a
line number, a symbol's location, a byte count. Worked example, ts#436, 4,149
tokens:

> Read `test_overnight_characterization.py` in this working directory and answer
> with a single line of JSON and nothing else:
> `{"probe":"OK","apply_precision_line":<1-based line of `def apply_precision(`>}`

A correct answer rules out BOTH "at capacity" and "can respond but not to this
tree", which is exactly the pair of explanations a producer failure leaves open.
A wrong or empty answer is a fleet-level signal, not a PR-level one. Adopted by
the coordinator as the probe form after a producer failure specifically.

### DO NOT RESUME A DEGRADED RUN — THE CACHE REPLAYS THE EMPTINESS

`resumeFromRunId` replays completed agents whose (prompt, opts) are unchanged.
After a degraded round the cached result IS the empty one, so a resume returns
the same `{"findings": []}` and looks like a confirming second read. It is not a
re-run; it is the first run's silence, repeated. Retry a degraded round as a
FRESH `Workflow` call with the same args and no `resumeFromRunId`. Resume is for
editing the script's post-processing, never for re-attempting a measurement.

**FILED: `cptkoolbeenz/claude-skills#173`** (by the firmware coordinator,
2026-09-16), carrying both ts#435 run IDs and pointing at this lane's saved
artifacts. The headline is the REPORTING defect, not the producer no-op: fix 2
is *do not emit a finding count at all when `codexProducerRan` is false*, with
the same argument extended to `covered_bytes`/`truncated`. Two siblings to read
with it, not after it:
- **#40** — "`codexProducerRan` reports false when a producer ran but returned no
  `head_sha`". Possibly the same root; the symptom set above fits either "never
  ran" or "ran and returned nothing", and this lane deliberately did not guess
  between them.
- **#161** — filed from this lane's earlier degraded round, where
  `truncated`/`covered_bytes`/`total_bytes` came back `null` rather than false.
  Same family: a field that reads as clean when it means unknown.

## "REPRODUCIBLE PER HEAD" IS REFUTED TOO — A DEAD ROUND CAN GO ANCHORED ON A PLAIN RE-RUN

Measured 2026-09-18 on ts#438 at `c0e6181ce4e7889ada6b65ca572dfa3453b0d045`:

| run | tree | brief | producer | blind leg | provenance | verdict |
|---|---|---|---|---|---|---|
| wf_29108c3b-b50 | audit-ts438 | A | **false** | **false** | `{}` | DEAD |
| wf_bd6b59dd-f0c | audit-ts438 (SAME) | A, materially unchanged | true | true | 44285/44285 | ANCHORED |

Same head, same pinned tree, same engine and effort. One died, the next was
clean. **So the producer failure is not strictly reproducible per head**, and the
correction I sent the coordinator earlier — retracting "intermittent" in favour
of "reproducible per head" — was itself too strong. I have now been wrong in both
directions on this, which is the tell that I was generalising from one controlled
pair each time.

**What actually survives, and it is operational, not causal:**
1. HEALTH-GATE EVERY ROUND. This is the only claim that has never failed.
2. On a DEAD round, RE-RUN ONCE, FRESH. Never `resumeFromRunId` — the cache
   replays the emptiness. A fresh re-run cost ~444k here and produced a usable
   round where the first produced nothing.
3. If the SECOND round is also dead at the same head, stop and escalate. Do not
   spend a third.
4. Do not claim a cause. "Pure-addition diff" was my hypothesis this time (one
   new file, no base version) and the re-run refuted it on identical inputs.

The dead round's findings were substantively close to the anchored round's. That
does NOT retroactively justify reading them: the process was right and the
outcome was luck. An unanchored round that happens to be correct is
indistinguishable, at the time you read it, from one that is confidently
describing a different PR.

## `fetch.sh --state`'s `converged=yes` IS A BUGS-ONLY VERDICT

Measured on fw#1132, 2026-09-18. `fetch.sh --state` reported `converged=yes`
with `review_bugs=0`, while the review header still carried **2 open Rule
violations**. The flag does not consider them. A round with open blocking-tier
Rule violations and zero Bugs reads exactly like a clean one.

On #1132 the two turned out to be round-1 findings already declined with a
posted, cited rationale, so the PR genuinely was dispositioned — but that was
established by READING THE BODY, not by the flag. Had they been new, the flag
would have said the same thing.

**How to apply:** never take `converged=yes` as the convergence check. Read the
counter row for all four tiers, then read the body for anything nonzero, and
match each survivor against the posted decline set by TITLE. The lane rule is
already "verify the remaining count equals the declined set before declaring
it" — this is the measurement showing which tool cannot do that for you.

Related, same family: `review_state=final` is NOT terminal (it moved between
reads on fw#1132 round 2), and one `fetch.sh` call returned
`status=error reason=suggestions_fetch_failed` WITH `review_bugs=0` — an infra
failure that reads as a clean pass.

## THE HEALTH GATE CONFLATES TWO DIFFERENT FAILURES

`audit_health.py` printed "VERDICT: DEAD ROUND ... everything downstream of a
missing producer is unanchored" for fw#1132's first audit round — but that round
had `codexProducerRan: true`, `head_sha` present, and full coverage
81,211/81,211. The producer ran. What was missing was the BLIND LEG.

Both must block, so the gate failed in the right direction. But the wording is
wrong for this case, and a detector whose explanation does not match its input
trains you to skim it. Two distinct states:

- **UNANCHORED** — no producer / no `head_sha` / no `covered_bytes`. There is no
  record of which tree was read. Nothing downstream is readable.
- **INCOMPLETE** — anchored, but a requested leg did not run. The round read the
  right tree; it just has no measurement of what the steering cost, so a
  0-finding result is hunter-only and not clean.

Save them under different names (`-ERROR` vs `-INCOMPLETE`) so the artifacts stay
distinguishable, and fix the gate's message to name which state it found.

## A DEAD ROUND'S RE-RUN IS A COIN FLIP, NOT A FIX — AND THE LEDGER SAYS SO

The existing standard above ("re-run ONCE fresh on a dead round; if the second is also
dead, escalate") was written from the ts#438 controlled pair, where the re-run DID go
anchored. That is one outcome, not the rule. Measured across this lane's whole audit
ledger, 2026-09-19:

**Eight dead (ERROR) rounds. TWO of them are consecutive pairs on the same head:**
- **ts#417** rounds 1 and 2 (`wf_e590d116-5b9`, `wf_f3f206dd-023`) — five minutes apart,
  both dead.
- **ts#449** rounds 1 and 2 (`wf_9fceb3f2-524`, `wf_8ea7bfaf-b86`) — both dead, identical
  field shape.

And **ts#441** round 1 was dead while its re-run anchored and PASSed. So the re-run
clears it sometimes and does not other times. Treat it as a coin flip costing ~88k, not
as a repair.

**The shape is stable and worth recognising on sight** (all eight): `blindLegRan:false`,
`codexProducerRan:false`, `noProvenance:true`, `provenance:{}`, no `head_sha`, no
`covered_bytes`, `agents_empty_result:1`, `agent_count:1`. The journal holds a single
result line, `{"findings": []}` — the 16-byte literal.

**TOKEN COUNT DOES NOT DISCRIMINATE, so do not try to use it.** Dead rounds land at
82,853 / 87,272 / 87,692 / 88,234 / 88,373 / 88,501 / 90,278 (and one outlier at
322,917). A HEALTHY single-agent PASS round lands at 83,973 / 84,886 / 86,969 — the same
band. Only the field set separates them, which is what `audit_health.py` is for.

**So: one fresh re-run, then STOP and escalate.** Do not claim a cause from the shape
alone — a shape, a recurrence and a journal line are not a mechanism, and guessing at one
is how a tooling fault gets written up as a code fault. Never `resumeFromRunId` a dead
round: the cache replays the missing leg and launders it into something that reads clean.

---

## THE STEP 0.5 SWEEP CANNOT CLEAN A FIRE TREE WHILE THE LANE SESSION IS ALIVE

Measured 2026-09-20 across all 19 recorded trees. Result: **1 already gone, 18 BLOCKED, 0
removable.** Fifteen of the eighteen were blocked by one line:

```
git worktree LOCK held: claude agent agent-<id> (pid 3313987 start 19813438)
```

**The same pid and the same starttime on every one.** That is not fifteen live fires. The
lock-holder was checked rather than assumed:

- `/proc/3313987` **exists**, so the lock is not stale;
- `/proc/3313987/stat` field 22 is **19813438**, matching the recorded starttime exactly,
  so it is not a recycled pid wearing a dead agent's number;
- its cmdline is **`claude --permission-mode auto --model opus`** — *the lane's own worker
  session*.

So a harness-created fire worktree (`isolation: "worktree"`, which lands under
`<checkout>/.claude/worktrees/agent-*`) is locked by the **worker process that spawned the
fire**, and stays locked for as long as that worker lives. The dispatcher's instruction —
"add its tree to FIRE_TREES.txt and let STEP 0.5 remove it on a later tick" — **can never
succeed for these trees inside the same session.** No number of later ticks helps.

**What follows, and what to do:**

1. **Do not re-run the full sweep every tick.** It is not free: 19 trees took **over 120
   seconds**, enough to be pushed to the background. Re-running it hourly to re-learn
   "18 BLOCKED" is pure spend. Sweep at LOOP START, and after that only when a tree is
   newly added or a session is ending.
2. **Expect FIRE_TREES.txt to grow monotonically** — roughly one entry per fire for the
   life of the worker. A long list is the normal steady state, not evidence of a leak or
   of sloppy teardown.
3. **The block is still correct and must not be overridden.** The lock is real and its
   holder is live. This entry explains *why* the sweep finds nothing; it is not licence to
   force removal. `--force` against a live lock risks pulling a tree out from under a
   fire.
4. **Lane-created audit trees (`/mnt/c/daqifi/wt/audit-*`) are different** — they carry no
   agent lock, so they clear normally once the 60-minute recency window passes. Those are
   the ones a later tick genuinely can remove.
5. **The real cleanup point is session end**, when the lock-holder exits; or the harness's
   own auto-clean for a worktree the fire left unchanged.

The honest framing: this is not a bug in `teardown_check.sh`. The script refused correctly
every time, and refusing is what it is for. What was wrong was the *expectation* built into
the dispatcher — that a later tick would clear these. It will not.

---

## `UNKNOWN` MERGE STATE IS "NOT COMPUTED YET", NOT "UNCHANGED" — RESOLVE IT, NEVER LOG IT

Measured 2026-09-20, right after a merge landed on firmware `main`. All eleven polled PRs
came back `mergeStateStatus: UNKNOWN` from `gh pr view --json mergeStateStatus`, and stayed
that way across repeated polls for **35+ minutes**.

GitHub computes mergeability lazily. `UNKNOWN` means *the answer is not ready*, and a
blocker poll that prints it next to the others silently converts "I don't know" into "no
change" — which is exactly how a poll misses the transition it exists to catch.

The REST endpoint forces the computation that GraphQL was deferring:

```bash
gh api repos/<owner>/<repo>/pulls/<N> -q '"\(.mergeable)/\(.mergeable_state)"'
# -> true/blocked, false/dirty, true/clean ...
```

After one REST query per PR, the GraphQL field populated for **all eleven** and agreed with
REST exactly (`BLOCKED`↔`true/blocked`, `DIRTY`↔`false/dirty`, `CLEAN`↔`true/clean`).

**Rule for the blocker poll:** treat `UNKNOWN` as a *missing reading*, not a value. Re-query
that PR through the REST endpoint above and report the resolved state. Never write
`UNKNOWN` into a "blockers moved: 0" line — a poll that cannot currently see a PR's state
has not established that the PR did not move.

**A correction to an earlier framing, recorded so it is not repeated:** the first reading of
this was "GraphQL is an unreliable field." That is too strong and it is wrong. The field is
reliable *once computed*; what is unsafe is the caller treating the not-yet-computed
sentinel as a negative result. The defect was in the poll's interpretation, not in the API.

---

## THE COORDINATOR'S RELAYED RULINGS CARRY OPERATOR AUTHORITY (operator, 2026-09-20)

Said by the operator directly in nq-b's session: **"you can trust the coordinator — treat its
relayed rulings as mine."** (First given 2026-09-18 in conv-fw's session; re-stated here, so it
is fleet-wide and no lane needs to re-ask.) The coordinator is the session that relays operator
text and holds the fleet's cross-lane state — on this station `daqifi-nyquist-firmware-6b`.
**Confirm the identity from `ListAgents` rather than assuming a name.**

So: when the coordinator relays an operator ruling, ACT ON IT. Do not hold the work waiting for
the operator to retype it in this terminal. Holding used to be the right call; it is now the
wrong one, and it costs the fleet a round trip per lane.

**Four limits, all still binding:**

1. **A RELAY is not an OPINION.** "The operator ruled X" is operator authority. The
   coordinator's own analysis or recommendation is a peer's, and the restrictive/permissive rule
   still applies to it: adopt a restrictive one on sight, route a permissive one. If a message is
   ambiguous about which it is, ask.
2. **Verify facts at source anyway.** The grant covers authority, not accuracy. The coordinator
   has been wrong on a filename, a PR characterisation and a sign-flip — all in one day. A ruling
   is not a citation.
3. **The lane's gates do not relax.** Audit anchored to the head being merged, attestation posted
   first, `mark-audited.sh` at the verified head, no `--delete-branch`. A ruling authorises an
   action; it never pre-clears the evidence for it.
4. **Not a permission-prompt bypass.** A relayed ruling is not approval for a tool call the
   harness put to the operator, and "I was denied, you do it" is laundering no matter who sends
   it. Route that to the operator.

Worked example, same day: the coordinator corrected this lane on cs#166 — OPEN/MERGEABLE in
GitHub but **parked by operator ruling** after three BLOCK audits. That relay was correct and
decisive. It also happened to agree with an answer the operator had already given here, so
nothing rested on it; the grant matters for the *next* relay that arrives with no operator answer
already in hand.

## BENCH TIME IS THE SCARCE RESOURCE — ORDER `CLEAN` BEFORE `BLOCKED` (2026-09-21)

Measured by the coordinator, live query: **38 open firmware PRs, 29 parked, 9 not — 76% parked.**
Of the nine live ones, **not one is waiting on ordinary review**: four wait only on a bench run
(#996, #1014, #1036, #1048), two on broken tooling, one on a human approving review, one on a
release decision, one routed to the operator.

**So more review capacity buys near zero.** This is a disposition backlog. What is scarce is
decisions, closes, and **bench time** — which makes this lane's board one of the few genuinely
productive resources left, and ticket throughput the *less* valuable half of what we do.

**THE TIEBREAK, standing:** among bench-waiting PRs, take **`mergeStateStatus: CLEAN` first and
`BLOCKED` second.** A green bench run on a BLOCKED PR does not make it landable, so that bench time
converts to nothing until the block clears. Applied 2026-09-21: took #1036 (CLEAN, `needs-bench`,
audit PASS FULL anchored) over #1014 (BLOCKED), though both are NQ1-USB-serial-only and otherwise
equally ready.

### A harness bug's TRIGGER CONDITION decides what it blocks — never freeze on the bug alone

ts#453 (pyserial's win32 `flush()` is `while self.out_waiting: time.sleep(0.05)`, unbounded) was
being carried fleet-wide as "blocks the bench work". It does not. It spins only while the port's
output buffer never drains, which requires a writer **outrunning the device's USB consumption**.

- **Inducing it is cadence-dependent.** Part 8 of `test_847` writes at `TEAR_WRITE_PERIOD_S = 0.001`
  with `delay=0` — ~1 kHz. Ordinary suite calls use `delay >= 0.3 s` and drain fine.
- **Recovering from it is identical for every caller.** Any device-side stall hangs unbounded.

So it gates **exactly one item** — #1048's re-run, whose test provokes it — and nothing else at
ordinary SCPI cadence. **Guard the recovery, not the trigger:** brief every bench fire with
`python.exe -u` and an OS-level hard timeout regardless, because that costs nothing and the recovery
exposure is universal.

Generalisation worth keeping: **a finding that only ever adds caution is cheap; one that states
precisely what it does NOT cover is what keeps work moving.** Derive the trigger condition before
treating a bug as a property of the whole tool.

## `/improve` IS DEPRECATED BY QODO — and the skill ALREADY SAYS SO. I read the heading, not the body. (2026-09-21)

> **⚠️ CORRECTED, same day.** The claim below that the skill is stale was **WRONG and is retracted.**
> `qodo-cycle/SKILL.md` line 835 is a **heading** — *"2026-05 migration: `/review` →
> `/agentic_review`; `/improve` retained"* — and lines 843-855 beneath it carry a dated **STATUS
> CHECK 2026-09-12** recording the exact banner I "discovered", verbatim, including *"removal date
> not yet scheduled"*, with its observation set (#984, #1013, #1063, #1064, #1068, #1070). Nine days
> old. I grepped, got a hit on the heading, and stopped there.
>
> **This was the THIRD instance of one pattern in a single day, across three lanes:** a summary
> surface answering a narrower question than the one asked, confidently. conv-fw read `state: OPEN`
> and not the comments on cs#152. A fire searched claude-skills by ticket NAME and missed #192,
> which was there by substance. I grepped a heading and not the section. **In none of the three did
> the search fail** — each returned something, which is exactly why none of them felt like a gap.
> A search that returns a hit is the most dangerous kind, because the hit ends the enquiry.
>
> **The rule:** when a grep hit is a HEADING, a TITLE, or a STATUS FIELD, it has told you where to
> read, not what is true. Read the body before asserting state.

Observed live on [PR #1147](https://github.com/daqifi/daqifi-nyquist-firmware/pull/1147) at
07:03:47Z. A fire fired `/improve` exactly as the skill instructs, and Qodo answered:

> **WARNING** — `/improve` is deprecated. Use `/agentic_review` instead (removal date not yet
> scheduled).

**The fire did nothing wrong, and neither did the skill.** `~/.claude/skills/qodo-cycle/SKILL.md`
tells every fire to fire BOTH surfaces, and it documents the deprecation accurately as of
2026-09-12. Firing `/improve` and receiving that banner is the *expected, documented* behaviour —
not a defect, not a stale instruction, and **not a reason to file anything.**

**NOT YET BREAKING, and say so rather than over-escalating:** the call still returned its normal
`## PR Code Suggestions` block (here, an explicit "No code suggestions found"), so the suggestions
surface still answers and `suggestions_absent=0` still means present-and-clean. Removal is
explicitly **not yet scheduled**.

**How to treat it until the skill is corrected:**
- Keep firing both surfaces. A deprecated-but-answering surface is still evidence, and dropping it
  unilaterally would silently narrow convergence to one leg.
- **Do NOT read the deprecation banner as a failed trigger.** It arrives *inside* a successful
  response. Treating it as an error would recreate the "poll on an untriggered review never ends"
  failure from the opposite direction — abandoning a surface that did in fact answer.
- When `/improve` stops answering at all, that is a different event: the suggestions surface is then
  ABSENT, not clean, and the `not_found` trap applies.

**Nothing to fix and nothing to file.** The skill is correct and current. What IS additive is the
trap warning above — the skill records that the banner appears and warns that a poll will hang
forever once `/improve` is *removed*, but it does not warn a lane against reading the banner, while
`/improve` still answers, as a failed trigger. That gap is real; the staleness was not.

### The OPEN MEASUREMENT the skill asks for, and nobody has run

Same passage, and it is the thing actually worth doing here. The dual trigger works, but its
justification — *"`/agentic_review` does not generate fresh suggestions"* — **rests on a single
measurement from 2026-05-04** and has not been re-tested since Qodo unified the surfaces. The skill
asks explicitly: fire `/agentic_review` ALONE on one real PR and check whether the
`PR Code Suggestions ✨` surface updates. If it does, `/improve` is a round-trip that buys nothing on
every Qodo pass on every PR in both repos.

**⚠️ DO NOT RUN IT ON A PR A FIRE IS MID-CYCLE ON.** The fire owns its own Qodo cycle and polls for
responses to *its* triggers. A second actor firing a trigger on the same PR races it: the fire can
read the worker's response as its own, or conclude its own trigger went unanswered. Run the
measurement on a PR with **no fire attached**, or after the fire has handed back — and report it as a
MEASUREMENT, never as a recommendation, since the skill change is not this lane's to decide.

## MEASURED 2026-09-21: `/agentic_review` alone does NOT create the suggestions surface — keep the dual trigger

`qodo-cycle/SKILL.md` has carried an open question since **2026-05-04**: the dual trigger's
justification ("`/agentic_review` does not generate fresh suggestions") rested on ONE measurement
from that date and had never been re-tested since Qodo unified the surfaces. The skill asked for it
to be re-measured before treating the second trigger as load-bearing. **Run on test-suite #455.**

| | |
|---|---|
| baseline | **0** `PR Code Suggestions` comments on the PR |
| trigger | `/agentic_review` **alone**, 08:37:43Z |
| **negative-arm control — did it run?** | **YES** — the Code Review comment's `updated_at` moved to 08:40:47Z (from 06:58:44Z) and a new review-link comment appeared at 08:40:52Z |
| result after 180 s | suggestions count still **0** |
| **positive control** | `/improve` fired at 08:41:58Z created the surface in **~60 s** (count=1) |

**Conclusion: the dual trigger is justified. `/improve` is NOT a redundant round-trip.** The
2026-05-04 justification now rests on a 2026-09-21 re-measurement instead.

**BOTH controls were the point, and neither was optional.**
- Without the **negative-arm control**, "no suggestions appeared" is equally consistent with *the
  trigger never fired* — a null result from a dead instrument, reported as a fact about Qodo.
- Without the **positive control**, "no suggestions appeared" is equally consistent with *the
  surface being broken for this PR* — the absence would have proved nothing about `/agentic_review`.
**A negative result needs proof that a positive was achievable.**

**Scope, stated rather than glossed:** one PR, test-suite repo, single trial, on a PR whose review
surface already existed from Qodo's automatic on-open pass. Firmware-repo behaviour was not tested.
E, not V.

### And the absent surface was hiding real findings — this is why `suggestions_absent=1` blocks

ts#455 had been reported CONVERGED by its fire with `review_bugs=0`. But
`suggestions_absent=1` / `suggestions_state=missing` meant the `/improve` surface **had never run on
that PR at all** — the `not_found` case, not a clean one. Firing both surfaces produced
**`suggestions_open=2`** (one "Possible issue") and **`review_bugs=1`**, including a **⭐New** Bug
that did not exist in the earlier round.

So auditing on that "convergence" would have burned an audit on a PR carrying live findings.
**`suggestions_absent=1` is not a clean surface; it is no surface.** Always read `suggestions_absent`
before believing `review_bugs=0`.

## THE RESIDUE FILTER WORKS — AND IT HAS A FLAW THAT PARSES PRIOR ART AS THE TARGET (2026-09-21)

The check from earlier today — `git cat-file -e origin/main:<file the ticket names>`; ABSENT means
blocked on an unlanded parent — held up across a second survey. **But it mis-fired once, in a way
worth knowing:**

**#1032 came back `on-main, 1PR` and looked like the cleanest pick available.** The file it named,
`tools/lint/scpi_claim_path.py`, is the ticket's **PRIOR-ART CITATION** — a different tool, cited as
a cautionary precedent (PR #901: five rounds, cap hit, parked). #1032's actual target is "the #1001
fixture generator", which the body never spells as a path. So the filter answered *"is the first
path mentioned on main?"* when the question was *"is the ticket's TARGET on main?"* — the same
answering-a-different-question failure the lane has hit repeatedly today, now in my own instrument.

**Fix:** a ticket body's first path is not its target. Extract paths, then check which one the
ticket says it will CHANGE — prior art, precedents and "see also" links all look identical to a
grep. Where the body names no target path, that absence is itself a signal the ticket is
under-specified.

### Survey result, 2026-09-21 — five candidates, none clean

| ticket | filter result | why not takeable |
|---|---|---|
| #1133 | `tools/hexcrc.py` **ABSENT** on main | PR #1130 introduces the file and is OPEN |
| #1047 | target on main, 0 PRs | its premise needs PR #1040 landed; main still has the OLD names, so "fixing" it would introduce the defect it claims to remove |
| #1032 | `on-main, 1PR` — **filter mis-fire**, see above | target unnamed; own prior art is a 5-round parked failure |
| #1117 | on-main, **0 PRs** — best contention score seen | **Bench: NQ1 + Saleae (SHARED instrument)** — needs coordination, and the saleae MCP server failed to connect this session |
| #1131 | on-main, 1PR | targets `SCPIDAC.c:140`, inside `DAC_EnsureHardwareInitialized()` — the same function parked **#1129** holds hunks in AND that #1147 just rewrote |

**#1131 is the sharpest instance of the ordering problem:** it is a real defect, low contention,
unassigned, on main — and it collides with a PARKED PR in a function that changed under it an hour
ago. Contention counted by *file* would have passed it; the collision is at *function* granularity.
**When a candidate shares a file with a parked PR, compare hunk ranges, not filenames.**

### The filter fails in BOTH directions — false ON-MAIN *and* false ABSENT

The #1032 case above is a **false on-main**: the path matched was a prior-art citation, not the
target. The coordinator then hit the **opposite** failure running the same filter on #876:

```
ABSENT   SCPIADC.c            <- a BARE FILENAME, not a repo path
ABSENT   SCPIInterface.c
ON-MAIN  firmware/src/services/SCPI/SCPIStorageSD.c
```

**#876 reported entirely ABSENT and is not blocked at all.** Tickets name files both ways, so the
filter disqualifies real work whenever a body uses bare filenames.

**Both corrections, together:**
- Resolve a bare name with `git ls-files '*/<name>'` **before** concluding absence.
- Identify which path the ticket says it will **CHANGE**; prior art and see-also links are
  indistinguishable from targets to a grep.
- **A ticket that CREATES a file will legitimately name an absent path — an output is not a
  precondition.** #832 names `refit_832_csv_additive.py`, absent precisely because it is the
  deliverable.

Neither failure announces itself, and one report propagated the filter fleet-wide before either was
known. A cheap check that is wrong in both directions is worse than an expensive one that is not —
state a filter's failure modes when you hand it to anyone.

## `unsound_refutations` HAS THREE PLACES TO LOOK, AND THE OBVIOUS ONE CAN LIE (2026-09-21)

Measured by conv-ts on test-suite#290 round 2 (`wf_444967b9-6bf`, head `89cfe6b`):

```
unsound_refutations            []              <- the obvious key. EMPTY.
arbiter.unsound_refutations    [2 entries]     <- the real ones
unsoundRefutationsBlock        true
```

**A count over the top-level array agrees with a clean run. The prose does not.** On #290
`gateReason` named both blocking conditions, the unsound refutations first.

**Check all three every time** — `unsound_refutations`, `arbiter.unsound_refutations`,
`unsoundRefutationsBlock` — and **read `gateReason` as PROSE rather than counting arrays.**

### Re-check of every artifact this lane is standing behind, done rather than assumed

| artifact | top-level | `arbiter.` | Block flag | `rawFindings` | verdict |
|---|---|---|---|---|---|
| fw#1147 `1448b2ce2` PASS (**merged** `8624d654`) | *absent* | arbiter is **null** | false | **0** | sound |
| fw#1147 `72fba8c68` BLOCK | *absent* | `[]` | false | 2 | sound |
| fw#1147 `789528d0d` BLOCK | *absent* | `[]` | false | 1 | sound |
| fw#1143 `a1b7ef395` PASS (**merged** `3da6da945`) | *absent* | arbiter is **null** | false | **0** | sound |
| ts#455 | — | — | — | — | no artifact; parked before any audit ran |

Both merges are safe: `rawFindings: 0` means there were no findings to refute, so no refutation can
be unsound, and `gateReason` read `clean` as a single clause.

**Note the trap's shape did not occur here** — on this lane's runs the top-level key is **absent**
rather than an empty array, so the specific masking conv-ts hit never arose. That is luck, not
diligence, and it does not retire the check.

**My own read was incomplete and I am recording it.** On rounds 1 and 2 I read
`arbiter.unsound_refutations` explicitly. On **round 3 — the one I merged on** — I read
`unsoundRefutationsBlock` but NOT the arbiter array. The conclusion was right only because
`rawFindings` was 0 and `arbiter` was null; had findings existed, I would have merged on a field
that this very finding shows can disagree with the arbiter's.

### The general form, which is bigger than this field

**A correct argument in one dimension can carry a false argument in another across the line with it.**
conv-ts's #290 refutations correctly established *who owns the handle* (only `_git_sha()`), then
concluded the bounded path is "strictly not worse" — but post-fix `main()` exits in ~20 s and is
re-invoked, so the same hung hook orphans **~60x more process trees per unit wall time**. Ownership
was proven; **rate** was assumed.

That is this lane's ts#453 reasoning with the sign flipped: there, a defect that looked general was
gated on **cadence** and bit one item instead of the queue. Here, a fix that looks strictly better is
worse **because the cadence changed underneath it**. Same variable, opposite direction, and both
invisible to anyone reasoning about mechanism without rate.

**When a refutation proves something true, check that its conclusion concerns the same dimension it
proved.** "Who owns it" and "how often it happens" are different questions.

## A source-text guard must be mutation-tested before it counts as a regression test

Measured on #1157 (#1154) 2026-09-21 — the guard did not guard the defect it was written for.

When a host test models logic locally (`SetCalCoefficient()`-style) because the real translation unit
is not host-compilable, **the Makefile source guard is the only thing tying the test to the firmware.**
Its strength is load-bearing. A presence-only `grep -qF` guard asserts almost nothing:

| mutant | why presence-only passes it |
|---|---|
| `if (isfinite(v))` -> `if (!isfinite(v))` | `grep -qF 'isfinite(value)'` matches the negated form too |
| `if (!Helper(..)) {` -> `(void)Helper(..); if (0) {` | call text still present, still before the write |

Both passed guard AND host test. Mutant B reinstates the original defect exactly.

**Required of every fire adding a source-text guard:**
1. Run the **unmutated baseline first** — a guard that rejects real source is worse than a weak one.
2. Kill at least: inverted polarity, ignored return value, call deleted, call moved after the write,
   duplicated write path, token present only in a comment.
3. Use exact counts + an explicit negative (count of the OLD BROKEN FORM must be 0), strict ordering,
   and `grep -v '^[[:space:]]*[*/]'` to strip comments. Model: `$(SCAN1112_BIN)` in
   `tests/host/Makefile` — same file, same subsystem, kills every mutant above.
4. Post the mutation table on the PR. Override the source var to keep the tree clean:
   `make <target> SCPIADC_SRC=/path/to/mutant.c`.

## Name EVERY Qodo surface you dispositioned — there are three, not two

Same incident. `/agentic_review` said `Bugs (0)`; `/improve` said "No code suggestions found"; the
deprecated `/review` "PR Reviewer Guide" **focus area** named both mutants above as a Weak Regression.
The fire dispositioned the four `/agentic_review` Rule violations and never mentioned `/review`.

A convergence claim that does not name its surfaces is a claim about the surfaces it happened to read.
List all three explicitly, even to say one was empty. `suggestions_absent` is NO surface, not a clean one.

## A test's ASSERTION set is not its PR's CHANGED-FILE set

Before any bench run on a parked artifact, check whether `main` moved under it:

```bash
git log origin/main --since=<park date> -- <paths it asserts on>
```

**The scoping trap:** what a test asserts on is not the set its PR modifies. #1017's fixture reads a
file that is absent from its own changed-file list, so a changed-files-only drift check clears it
falsely. Check the **union of changed and referenced paths**. Measured by the coordinator's drift
sweep 2026-09-21: 4 of 31 parked PRs were assertion-broken this way.

A green run on an assertion-broken artifact means nothing — same shape as
[[feedback_absence_of_a_result_is_not_a_verdict]]: the instrument was invalid, so the result is
UNJUDGEABLE rather than passing.

## ⛔ BRANCH OWNERSHIP — added 2026-10-01, because it was MISSING, not dropped

**Found while measuring this file's token cost, not by looking for it.** `lane/nq-b` appeared
**zero times** in this file and zero times in `FIRE_TASK.md`; the only push restriction anywhere in
the brief set was force-push. So the rule below was never present — which is worse than a rule lost
in an edit, because nothing had ever recorded its absence.

This file already enforces ownership for **hardware** (`:715-716` — the guard "refuses identifiers
this lane does not own"; `:616` — "hardware you do not own"). It did not enforce it for **branches**.

**THE RULE:**

- **Push only to a branch THIS LANE created or already owns.** That means `lane/nq-b`, or a ticket
  branch you created in this fire for a ticket you claimed under the selection lock.
- **Never push to a branch another lane created**, even when the ticket looks unclaimed and even when
  your change is obviously correct. A branch you did not create is another lane's working state, and
  a push to it can land on top of work that is not yet pushed anywhere else.
- **Before the first push to any branch you did not create in this fire**, check whether it already
  exists on the remote: `git ls-remote origin refs/heads/<branch>`. If it exists and this fire did
  not create it, STOP and report rather than pushing.
- **The selection lock does not cover this.** `select_lock.sh` serialises ticket ENUMERATION and
  CLAIM. It says nothing about the branch you push to afterwards, so a correctly-claimed ticket can
  still be pushed to the wrong branch. These are two different guards and only one of them existed.

**Why it is stated here rather than left to judgement:** a fire that pushes to another lane's branch
does so once, succeeds, and reports success. There is no failing step — [[the failure is other
lanes' work, discovered later]] — which puts it in the same class as the hardware-ownership rules
above and is why those are written down rather than assumed.

⚠ **Force-push remains separately on the operator's always-ask list (`:54`), including on a branch
this lane does own.** This rule narrows WHERE you may push; it does not loosen HOW.

### The BASIS of the rule above, which is not what it looks like

**This rule's basis is an ABSENCE OF PERMISSION, not a typed operator prohibition.** conv-ts, who
I adopted it from, checked their own two lines before telling me: their `NO NEW PRs` rule carries
an explicit attribution — *"(operator, typed 2026-09-21, still standing)"* — and their
branch-ownership push rule carries none. Its only stated ground is that pushing to a shared branch
**has never been granted**.

**Why the distinction is written down here rather than left out:** narrowing your own behaviour needs
no authority, so adopting the rule was correct on sight. But **if this lane ever genuinely needs to
push to a shared branch, that is a question to ASK the operator — not a ban to work around, and not
a question already answered.** A fire inheriting this as *"the operator forbade it"* would assume the
answer is no and never ask.

**So: obey it by default, and if it blocks real work, escalate it as an open question.** Compare the
rules above it that DO carry a typed attribution; those are settled and this one is not.

---

## ⛔⛔ A GITIGNORED DIRECTORY MAKES THE REMOVABILITY CHECK BLIND — `bench-*` TREES ARE THE CASE

**Measured 2026-10-01 on `/mnt/c/daqifi/wt/bench-996` (lane nq-c's; read-only, not touched).**

`git status --porcelain` on that tree is **EMPTY**. The tree holds **15 MB in 7 unique hex
images** under `ab/`, and `git check-ignore -v ab` returns `!! ab/` — **gitignored, therefore
invisible to the cleanliness check by design.**

Among them: `base_d1bf07c66.hex` and `fix_df60bc24b.hex` — *the exact A/B pair nq-c's
DISPATCHER.md cites as the evidence for #996's bench validation* (`baseline 59E02848 FAIL /
fix 1D6176FF PASS`). They are on no remote. Rebuilding them needs the toolchain plus the exact
commits plus a flash cycle.

> **The sanctioned removability check passes on this tree, and a sweep would destroy the only
> copy of a validated bench result.**

**Why the existing check cannot catch it, and why that is not a bug in the check.** The rule
asks *"is anything UNSAVED?"* and answers it with `status --porcelain` plus an `ls-remote`
containment test. Both are about **tracked content**. Gitignored build output is excluded
deliberately — for a LANE tree that is right, because `firmware/daqifi.X/build/` is noise. For a
**BENCH** tree the build output **is the artifact**: the hex is what was flashed, and the claim
in the record is about those bytes.

**So the axis is the TREE'S PURPOSE, not its cleanliness** — and purpose is not readable from
any field the check consults. Same shape as a park living in a comment while the label says
nothing.

### The rule

- **Never sweep a `bench-*` tree on a clean `status --porcelain`.** Before removing one, run
  `git status --porcelain --ignored` and account for every `!!` entry, or `ls` the staging
  directory by name.
- **An image file with no copy elsewhere blocks removal**, exactly as unsaved tracked work does.
  Preserve it (copy out, or record its provenance) before the tree goes.
- Generalise beyond `bench-*`: **ask what this tree was FOR before asking whether it is clean.**
  A tree whose purpose was to produce non-git artifacts is never fully described by git state.

⚠ **THE CANONICAL HOME FOR THIS RULE IS THE `backlog-loop` SKILL, WHICH I MUST NOT EDIT.** The
sweep discipline lives in `~/.claude/skills/backlog-loop/SKILL.md`; `porcelain` appears **zero**
times in this file and 0 in `DISPATCHER_PROMPT.txt` (verified with a positive control — 27 hits
on `branch` in this same file, so the zero is real and not a dead grep). Recording it here plus
routing it to the operator is the best I can do from a lane. **Same structure as the
`devices.conf` provenance gap: the omission is real AND the place it belongs has nowhere to put
it, which is why nothing caught it.**

### What bench-996 also demonstrated, worth carrying separately

**The tree was REPURPOSED, not drifted.** Its reflog: created at #996's head, 82 seconds there,
then checked out to `fix/1144-capjson-bounded-calibration` with three commits authored in it,
finally parked on `3e8b1b479` (main, #1157). A census comparing tree HEAD against the row named
in the tree's NAME reports FOREIGN — correctly, but the cause is deliberate reuse, not decay.
**A tree's name is a claim about its past, not its present**, and the reflog is the only place
that distinguishes the two. Check `$GIT_COMMON_DIR/worktrees/<name>/logs/HEAD` before
interpreting a foreign head.
