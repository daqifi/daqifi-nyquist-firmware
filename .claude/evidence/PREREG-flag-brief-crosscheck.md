# PRE-REGISTERED — cross-artifact check: do the flags a script accepts and the flags a brief instructs agree?

On disk BEFORE measuring. nq-b, 2026-10-01. Read-only. **Counts and sites, no recommendation.**

This is the one class from the transfer census with **no shared-callee remedy**: `--push-base` lives
in 4 shell files and **0 of 4 briefs**, and prose cannot call a function.

## POPULATION — stated, because an unreachable population turns into a misleading zero

**The INSTALLED tree `~/.claude/skills` @ `6c6d16f`, dirty 0** — chosen deliberately: the question is
whether **an agent following a brief today** hits a flag the script **it actually runs** does not
accept. A branch would answer a different question.

```
scripts      70  *.sh          134  *.py
instructions  3  herd/briefs/*.md   +   50  */SKILL.md   =  53 documents
```
⚠ **`*.py` is included but extracted differently** (argparse, not `case` arms) and reported
separately. ⚠ The other 27 `.md` files are **excluded** — they are notes and references, not
instructions to an agent. Naming that: a reader may disagree with the boundary, so the count is
reported per-source.

**Lesson applied from the `EXIT_INCONCLUSIVE` row:** that claim was about ~150 **Python** files in a
**different repo**, and my 0-hits across 70 `.sh` files here would have been a **false refutation**
had I reported it as zero instead of UNVERIFIED. **So every zero below names the population it is a
zero over.**

## WHAT COUNTS AS A FLAG — two definitions, deliberately different

**ACCEPTED (by a script):**
- shell — a `--name` literal inside argument handling: a `case` arm, a `[ "$1" = "--name" ]` test,
  or a long-option list.
- python — `add_argument("--name")`.

**INSTRUCTED (by a brief):** a `--name` token appearing **on the same command as a script's name**.
⛔ **Not merely present in the document.** A brief mentioning `--json` is instructing `gh`, not a
local script; counting that would make every `gh`/`git` flag a finding.

## THE TWO DIRECTIONS

- **DIRECTION A — accepted but never instructed.** A capability agents are never told about. Low
  severity: the script works, the flag is unused.
- **DIRECTION B — instructed but NOT accepted.** ⛔ **An agent following the brief runs a command
  that cannot work.** This is the dangerous direction and the same shape as an impossible
  workflow-level prescription.

## ⛔ MY ERROR DIRECTION, AND WHAT IT BINDS ME TO

**I want DIRECTION B to be non-empty.** It is the interesting half, it would vindicate my
"needs something else" classification, and it arrives as a catch. **And the crude thing in my method
that produces exactly that error is a failed grep against shell argument parsing** — arg handling is
written a dozen ways (`case`, `getopt`, `shift` chains, `${1#--}` prefix stripping, pass-through to
another script), so **a flag I cannot find looks identical to a flag that is not accepted.**

> **PRE-COMMITTED: every DIRECTION B hit must be PROVEN by reading that script's argument parser and
> showing the flag is unhandled — including pass-through (`"$@"` forwarded to another script, which
> makes the flag accepted elsewhere).** A grep miss is not a finding.
>
> **And a non-empty DIRECTION B is the result I must distrust**, not the one to report as a catch.
> If B is empty after that work, that is the honest answer and I report it as such.

## REPORTING RULES, pre-committed

1. Per-direction counts **with the population named**, plus verbatim site for every B hit.
2. A third bucket **UNDETERMINED** for a flag whose acceptance I cannot settle by reading (dynamic
   dispatch, `eval`, forwarded `"$@"` into a script I cannot resolve). Folding these into B is the
   error this bucket prevents.
3. **No recommendation.** The remedy is structural and the operator's to fund.
