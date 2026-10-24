# sk#236 — the three audit findings REPRODUCED and FIXED, with a control on every case

**My tool, my doc, my defect.** The HIGH is a case `LIMITATIONS.md:93` names *two sections before*
the ceiling I led with. Round 1 at `ea2ece4e3` is correct on all three.

## ⛔ The HIGH, reproduced to a clean bill

`show()` ended `| sort -u`, so all four sides were reduced to **distinct sets** before accounting. A
dropped **repeated** line is then invisible — and a repeated line is the normal case on this repo
(`83 of 91 tokens repeat, one 63 times`, my own measurement in that same doc).

⚠ **My first reproduction did not reproduce it.** I dropped `run_test t3`, a line with multiplicity 1 —
caught correctly, exit 1. The blind spot needs a line that **already exists in base**, which is exactly
the `STATIC_POOL_SIZE` shape. *A mutation of the wrong shape passes and reads as exoneration.* Second
attempt asserted the precondition before testing: `base 2 × '- 512U', main 3 ×` → duplicate armed.

```
v2, merge that dropped one real 512 B payment on a CLEANLY auto-merging path:
    MAIN   added 0 lines vs base            <- main added a line; the distinct set says 0
    LOST-FROM-BRANCH 0   (of 1 ...)
    ### ACCOUNTING-RC=0   (0 = no parent's addition was dropped)       EXIT=0
```
⛔⛔ **It printed the assertion that nothing was dropped, over a dropped payment.** On the same-line
variant, v2's output for the **lossy** and the **correct** resolution was byte-identical.

## The fix, and why it is a redesign rather than a one-liner

`sort -u` was load-bearing — `comm` needs sorted input. v4 drops `comm` for per-line **counts**
(`b/m/r/x` in base/main/branch/result) and two bounds:

```
dm = m-b ; dr = r-b
dm<0 or dr<0 : a side deliberately REMOVED copies -> that removal is a contribution,
               floor = target = max(0, b+dm+dr)          (no same-change reading to be ambiguous about)
otherwise    : floor  = b + max(dm,dr)   same-change reading
               target = b + dm + dr      independent-change reading
```
⭐ **And the label is sound only because of a second input: did git auto-merge the path?**
`git merge-tree --write-tree` stage lines answer it. On a **CLEAN** path the auto-merge is well-defined,
so a shortfall is human editing — **DEFINITE LOSS**. On a **CONFLICTED** path the resolution had to
rewrite text and a vanished line may be **superseded** by a differently-punctuated equivalent —
**UNACCOUNTED, needs a read**. ⛔ v3 claimed `x < floor` was sound unconditionally and so fired
identically on a correct resolution and a lossy one. **Its own control caught that.**

`x` between floor and target is **AMBIGUOUS** — the documented ceiling, now a **named band on the exact
lines** instead of a silence. It still cannot decide; it can now point.

## The matrix — 9 cases, each with the control that must NOT fire

| case | v2 | v4 |
|---|---|---|
| A dropped DUPLICATE payment, clean path | **0 CLEAN ⛔** | 1 DEFINITE LOSS ✅ |
| A′ correct resolution *(control)* | 0 | 0 ✅ |
| B equal payments, lossy | 1 | 3 needs a read |
| B′ equal payments, correct *(control)* | **1, identical to B ⛔** | 3 needs a read |
| C dropped UNIQUE line, clean path | 1 | 1 ✅ |
| D′ whitespace-only file that EXISTS | **1 FALSE ALARM ⛔** | 0 ✅ |
| E one side deleted, other left it alone | **1 FALSE ALARM ⛔** | 0 ✅ |
| F non-merge RESULT | 2 refused | 2 ✅ |
| G invented-only line | **0 CLEAN ⛔** | 4 ✅ |

B/B′ still share an exit code. **That is the ceiling, not a defect** — equal multisets from different
payment sets need provenance or a relink.

## The two MEDIUMs

- **`:85` INVENTED never touched `rc`** (assignments at `:64/:69/:79` only) → v4 gives it **exit 4**, so an
  injected line is no longer indistinguishable from clean while the doc publishes *"0 = clean."*
- **`:69` existence inferred from FILTERED content** → `[ -z "$x" ]` fired for a file that legitimately
  exists but is blank, and for a correct deletion. v4 asks git (`cat-file -e`) and resolves deletion
  **three-way by blob identity**: absent from the result is correct when one parent deleted it and the
  other left it byte-identical to base; a loss only when a surviving parent **modified** it.
  ⚠ Found while fixing: `git rev-parse <sha>:<deleted path>` does **not** fail — it printed
  `<commit-sha>:gone.md`, making a deleted file look modified. **`--verify --quiet` is required.**

## ⛔ Three defects I introduced while fixing three, each caught by a control

1. **`-v mode="$MODE"` never landed.** I guarded 5 of 6 python `.replace()` calls with
   `assert old in s`; the one I left bare matched nothing and silently did nothing. The output said
   `[ path]` with an empty mode. **An unasserted splice fails silently** — every later edit asserts
   presence *and* uniqueness.
2. **`max(m,r)` false-alarmed on a legitimately MODIFIED line** (`all: t1 t2`: base 1, main 0, branch 1).
   Fixed by respecting the sign of each delta.
3. **An apostrophe in a comment — *"one side's addition"* — closed the single-quoted awk program.** The
   patch now asserts no `'` remains inside it.
   ⚠ And `bash -n … && echo "syntax OK"` printed nothing while the matrix ran on regardless, producing
   **7 failures that were all one syntax error**. A guard whose failure only withholds a message is not a
   guard: `|| exit 1`.

## Provenance
Findings: the adversarial audit, both legs, round 1 at `ea2ece4e3`. Reproductions, the v4 redesign, the
auto-merge discriminator, and finding E: nq-b. The sponsor who missed it said so first and withdrew the
"Reviewed before sponsoring" heading — the four checks there were 88 lines, sha256, byte-identity and
`bash -n`, **not one of them about what the tool does.**
