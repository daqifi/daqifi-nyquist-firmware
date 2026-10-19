# `account-lines.sh` — what it covers, and the **ceiling** it does not

Prepared 2026-10-19 for `cptkoolbeenz/claude-skills`. **Read this before trusting a clean result.**
Canonical script: `~/.claude/lanes/account-lines.sh`, sha256 `9bee3c46d22f7c6d`, 88 lines.

> ⚠ **The limitations are not optional documentation. Three separate rules about accumulator merges were
> published and refuted in one night, each because it read a SEMANTIC fact off a SYNTACTIC surface. A
> tool shipped without its ceiling becomes the fourth.**

## What it does

Given `<repo> <MAIN-rev> <BRANCH-rev> <RESULT-rev> <path>...` it reports, against the **merge base**:

| output | meaning |
|---|---|
| `LOST-FROM-MAIN n (of N)` | lines main added vs base, absent from the result |
| `LOST-FROM-BRANCH n (of N)` | lines the branch added vs base, absent from the result |
| `INVENTED n` | lines in the result present in **neither** parent |

Exit codes: **2** = refused · **0** = clean · **1** = lines to account for.

⭐ **READ THE DENOMINATORS, NOT THE EXIT CODE.** `LOST-FROM-MAIN 0 of 797` is the useful output — a
meaningful zero with a real denominator. **A nonzero result on a prose / same-line conflict is EXPECTED**
(both sides rewrote the same lines and your resolution superseded both). **A nonzero result on a cleanly
AUTO-MERGED file is the alarm.**

## ⛔ Design constraints that are load-bearing, each paid for

1. **THREE-WAY, NOT TWO-WAY.** A line absent from the result is a true loss only if the **merge base**
   had it and **neither parent deleted it**. The two-way form (parent vs result) fired **60 false alarms
   on a deletion-heavy refactor** (nq-a) and **11:1** on a docs conflict (nq-b). **A check that noisy is
   a check nobody runs**, which makes noise a correctness property, not an aesthetic one.
2. **READ GIT OBJECTS, NEVER WORKING-TREE FILES.** With `core.autocrlf=true` the working tree is CRLF
   and the object store is LF, so a working-tree comparison reports **every** line as changed. ⚠ And
   `.gitattributes` sets `eol` **per path** on this repo — `docs/MEMORY_ARCHITECTURE.md` is CRLF while
   `tests/host/Makefile` is LF. **Detect per file; never assume either.**
3. **REFUSE AN UNCOMMITTED RESULT.** Passing a pre-commit `HEAD` compares main's additions against the
   *branch* and reports every line main added as lost — **797 of 797** in the run that prompted the
   guard. A number that large is alarming enough to be acted on, so the tool **refuses** rather than
   printing it. Workflow: resolve → `add` → `commit` → **then** account against the merge commit.

## ⛔⛔ THE CEILING — the class this tool CANNOT see

**A line-level check cannot see equal totals computed from different payment sets.**

```
branch  STATIC_POOL_SIZE = (194*1024) -1024 -512 -512 -2048 -512   [#956, no #958]   = 194,048
main    STATIC_POOL_SIZE = (194*1024) -1024 -512 -512 -512 -2048   [#958, no #956]   = 194,048
union needs SIX terms                                                                = 193,536
```

Both sides print the same number; the **token multisets are also equal** (both sort to
`[512,512,512,1024,2048]`). **fw#1024 and fw#1106 both had this shape.** What settled them was a
**relink, reading `.bss.gPoolStorage` out of the image** — `0x2f400 = 193,536`.

> **This tool does not cover that class. Nothing line-level does.** The terms differ only in their
> PROVENANCE — what each one pays for — which lives in comment prose, not in the expression.

**And the prose is not a reliable registry of provenance**, measured across four shipped revisions of one
file: a fixed-size window **under**-reports (and missed the branch's **own** payment, a phantom violation
on a correct file, because the define moved from line 75 to 174); the delimited comment block
**over**-reports, because a *mention* is not a *payment*; and even a payment-specific predicate is
**incomplete on 2 of 4 revisions**, since only one revision's comment ever enumerates `#665`.

### What DOES cover it — use these, not this tool

1. ⭐ **BLOB IDENTITY as the precondition** (nq-a). `git rev-parse <ref>:<path>` at base/P1/P2 — **three**
   SHAs, no prose:

   ```
   P1 == BASE       -> main is the only contributor   -> take P2 wholesale, correct by construction
   P2 == BASE       -> branch is the only contributor -> take P1 wholesale, correct by construction
   P1 == P2         -> same change both sides         -> nothing can be lost
   all three differ -> THE ONLY CELL THAT NEEDS A READ
   ```
   ⛔ **Do NOT include the merge blob in this test.** "Merge differs from both parents" is **entailed by
   any real union** and is therefore true of every correct resolution as well as every lossy one — zero
   information, and as a trigger it fires on every resolution anyone ever performs.
   ⚠ It dominates a `--name-only` bound, which fires when both sides made the **identical** change.
2. **THE COMPILED ARTIFACT**, where one exists — a relink and a `.bss`/section read. No comment drift,
   window geometry or naming convention can fake it.
3. **AN INVARIANT SPANNING BOTH CONTRIBUTIONS**, matched to the structure:

   | structure | invariant |
   |---|---|
   | budget table | arithmetic identity (`used + free` constant) + a contributor-union set |
   | target list / Makefile | cross-list: every prerequisite has an invocation **and** a `clean` entry |
   | enum / bitfield | the **ordinal**, not membership — a swap passes every name-set check |
   | accumulator | provenance, then the compiled size |

## ⛔ AND THE TWO WAYS A CHECK OF THIS KIND LIES

1. **A DISTINCT-SET UNION IS MULTIPLICITY-BLIND** (nq-a). On one `clean:` list **83 of 91 tokens repeat,
   one 63 times.** A dropped token was caught *only because it happened to have multiplicity 1* — any of
   the other 83 would have reported MISSING=NONE over a real loss. **Use per-token MAX-of-parents vs the
   result, never set membership.** And check the suffixed token **by identity, not count**: a count of 1
   is satisfied by *any* suffixed token.
2. ⛔⛔ **THE SETUP MUST ASSERT THE MUTATION APPLIED.** The first control written for this tool
   **false-passed**: its setup died on a python `SyntaxError`, the mutation never landed
   (*"nothing to commit, working tree clean"*), and the check then reported an **unmutated** file as
   clean. **A control whose setup fails silently reports success — strictly worse than no control.**
   ⭐ Related tell (conv-fw): *a count that varies across revisions in a way the diff does not explain is
   measuring the instrument, not the file.* Two phantom findings in one row came from extraction windows
   whose geometry moved with the revision.

## The mutation evidence for the ceiling, so it is not taken on faith

```
same-line conflict, contribution deliberately deleted from the resolution:
  CORRECT resolution   LOST-FROM-MAIN 4 · LOST-FROM-BRANCH 3 · INVENTED 4   RC=1
  MUTANT               LOST-FROM-MAIN 4 · LOST-FROM-BRANCH 3 · INVENTED 4   RC=1      ⛔ IDENTICAL
the spanning invariants on the same pair:
  CORRECT  used+free 523,800 ✅   contributor union {999,958,1094} present ✅
  MUTANT   caught ⛔
```

## Provenance of this document

Script and the ceiling: **nq-b**. Blob identity, the multiplicity blindness, the four-cell table and the
tautology correction: **nq-a**. The provenance rule and the compiled-artifact check: **nq-c**. The
search-space bound, the named failure class, and the ordinal case: **conv-fw**.

⭐ **Four corrections to one rule cluster in one night, three of them from peers, and every one arrived
from a lane that had RUN the rule on its own corpus — not one from anyone re-reading the reasoning.**
That is why the ceiling is written down rather than discovered again.
