# sk#165 base refresh — local work complete, HANDED BACK at the conflict resolution

**Nothing was pushed. No label touched. No audit run. The PR was not expanded.**
All work done in my own clone (`scratchpad/sk165`), never a worktree inside `~/.claude/skills`.

## ⛔ WHY I STOPPED, and it is a sharper point than the push

**Two separate reasons, and the second one I did not expect:**

**1. The push is a cross-repo write and that is ask-first regardless of who asks.** My standing
constraints name it twice — *"Always ask regardless for … cross-repo writes beyond the companion
test"* (the carve-out is `daqifi-python-test-suite`, not claude-skills) and *"cross-repo writes to
`cptkoolbeenz/claude-skills` are ask-first."* The authority offered was a **quoted operator line
relayed by a peer**, and the decision page's own entry 01 settles how that is treated: *"the ruling
is quoted by an agent on a page, not typed to the lane that would act on it. The lane declined on
that basis and I did not override it."* Same rule, applied to me.

**2. ⛔ THE REFRESH IS NOT A MERGE. IT IS A GATE-LOGIC AUTHORING DECISION.**

```
merge 72a199a4a4e9530f8d178c6c44bfa49ab8f35387 into 2c81067cd6cb00645be9e1409d966d9b3a044b47
  rc=1
  CONFLICT (content): qodo-cycle/mark-audited.sh        1 hunk
  CONFLICT (content): qodo-cycle/test-merge-gates.sh    2 hunks
  auto-merged cleanly: merge-target-keys.sh, test-audit-provenance.sh
```

**ALL THREE HUNKS NEED A JUDGEMENT. NONE IS A UNION.**

| file | hunk | the two sides | why it is not a union |
|---|---|---|---|
| `mark-audited.sh` | 1 | **OURS:** repo keying — *"The REPOSITORY goes in too… Markers are keyed by number alone, so #337's marker is found by a merge of #337 in any repo"* (53 lines) · **THEIRS:** *"A WRITE THAT FAILED IS NOT A GATE THAT CLEARED. `set -uo pipefail` does not abort on a failed redirection, so an unwritable or absent `ADVERSARIAL_MARKER_DIR`…"* (11 lines) | **Two independent SECURITY fixes to the same region.** Resolving it wrong re-opens either the cross-repo marker collision (sk#165's whole purpose) or the silent-write-failure fail-open. |
| `test-merge-gates.sh` | 1 | both sides fix the same thing — hard-wired `~/.claude/skills` paths → test the checkout | **The SAME FIX IMPLEMENTED TWICE.** Both define `CHECKS=` and `AUDIT=`; THEIRS adds `HERE=`, `MERGE_GATE_SUT`, `MERGE_GATE_CHECKS_SUT`. **A union double-defines them.** |
| `test-merge-gates.sh` | 2 | **OURS:** refactored to a helper, `mk … \| audit_gate` (1 line) · **THEIRS:** a new test case using the raw form, plus a `gh` stub (20 lines) | THEIRS adds coverage the PR wants, written against the call style OURS replaced. |

> **Resolving hunk 1 of `mark-audited.sh` means combining two fail-open fixes in one function.**
> That is authoring gate logic — on a row where I already hold a position (I independently
> reproduced the nine-shape keyword bypass against the installed parser), in a repo where my writes
> are ask-first. **So I am handing back the resolution, not only the push.**

## Steps 2 and 3 are unreachable, and that is a fact about step 1

**The suite was not run and the A/B was not re-run** — not skipped, **unreachable**: there is no
refreshed head to run them against until the conflicts are resolved. Reporting a suite result from
the unrefreshed head would answer a question nobody asked. `test-repo-passthrough.sh` is present
(15,134 bytes) and conv-fw's harness is where it was said to be
(`…/conv-fw/…/scratchpad/mono165`), so both are ready for whoever resolves the conflicts.

## What I captured that would otherwise be lost

⭐ **The base SHA, because that base auto-commits and moves:**
```
autopush/office-390bc12dcdf8 = 72a199a4a4e9530f8d178c6c44bfa49ab8f35387
captured                       2026-10-03T00:00:54Z
```
**Any later refresh against a different base SHA is a different merge** and the conflict analysis
above does not transfer.

**Divergence:** PR head `2c81067cd`, base as above; `mark-audited.sh` and `test-merge-gates.sh`
conflict, `merge-target-keys.sh` and `test-audit-provenance.sh` auto-merge clean.

## Installed tree — untouched, asserted on three fields not two

```
                 before                                    after
HEAD             25e3cf7253a0936003cb3699dfddb159f7545544  IDENTICAL
dirty count      0                                         IDENTICAL
index mtime      2026-10-02 18:00:15.931468170 -0600        IDENTICAL
```
`git --no-optional-locks` throughout. **I added index mtime to the assertion** because a plain
`git status` moves it — so "HEAD and dirty unchanged" can hold while the tree was still written to.

⚠ **And the installed HEAD has moved on its own since it was last recorded as `6c6d16f`.** That repo
auto-commits, so a before/after assertion there can fail for reasons unrelated to the asserter.
**Capture it per-session; do not inherit it.**

## Two things checked and cleared, stated because I raised them

- **The base's new test case is NOT the pipeline-status defect.** `mk … | bash "$AUDIT"` then
  `check "…" 2 $?` on the next line reads `$?` of the **pipeline**, whose last command is
  `bash "$AUDIT"` — **the thing under test.** Correct usage. ⭐ Which refines my own most-repeated
  rule: **the defect is not the SHAPE `| … $?`; it is a mismatch between the last command and the
  intended subject.** Treating the shape as the defect over-flags, and this is the first false alarm
  in six encounters.
- **My own hunk classifier over-reported union-ability** — it called 2 of 3 hunks "likely additive,
  union-able" on a set-disjointness test. Reading them, **all three need a judgement.** A
  set-overlap test cannot see that two disjoint texts implement the same fix. The error direction
  was permissive: a bad union ships silently.

## Independent corroboration of decision 10, from the base's own comment

> *"the installed `pre-merge-gate.sh` is **104 lines** to this one's **250** … three command-word
> cases FAILED against the installed copy and PASS against the one here. A suite that cannot fail
> for a change in its own tree is not testing that change."*

That is the base agreeing, in prose, with the byte measurement I verified earlier (installed 12,377
vs main 4,917). **Two independent sources, different units, same conclusion.**

## The residual I was told to carry, restated so a clean suite cannot be misread

The passthrough reaches **plain numeric targets and not URL-derived ones**, so the **URL re-keying
bypass survives sk#165 unchanged** (tracked on sk#201). A green suite here does not close it.

---

## ⚠ CORRECTION — the previous commit's message is CORRUPTED, and by the defect it describes

Commit `18b1f713874bbf616f628a6981a42c4294ede66e` was made with `git commit -m "…"` containing
**backticks inside a double-quoted string**. Bash command-substituted them. Three code examples
were executed and replaced by their (empty) output, leaving:

> *"The base's new test is NOT the pipeline-status defect.  then reads the pipeline's status…"*
> *"the defect is not the shape ,"*

`mk: command not found` · `check: command not found` · `syntax error near unexpected token '|'`.
**Zero backticks survive in that message.**

**The sentence explaining a shell-quoting defect was destroyed by a shell-quoting defect.** I have
used `-F -` with a quoted heredoc all session, which does not interpolate, and reached for `-m` once.

⛔ **NOT amended, deliberately.** Force-push is unconditionally ask-first on this lane, and a
corrupted commit message is not a reason to spend that. The record is corrected forward; the broken
message stays as evidence.

### The content that was eaten, stated here instead

**The base's new test case is NOT the pipeline-status defect.** The construct is:

```
mk "$G $P $M 99999" | bash "$AUDIT" >/dev/null 2>&1
check "a PR number that does not resolve here is refused despite a marker" 2 $?
```

`$?` is read on the next line, so it is the **pipeline's** status — whose last command is
`bash "$AUDIT"`, **the thing under test.** Correct usage.

> ⭐ **Which refines my own most-repeated rule: the defect is not the SHAPE `| … $?`. It is a
> MISMATCH between the pipeline's last command and the intended subject.** Treating the shape as
> the defect over-flags — this is the first false alarm in six encounters with that rule, and I
> raised it before checking it.
