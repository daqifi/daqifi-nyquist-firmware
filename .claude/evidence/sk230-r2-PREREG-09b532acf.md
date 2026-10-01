# PRE-REGISTERED SEAL — sk#230 round 2 @ `09b532acf291bb8341d08bcb4edf09564f0d291e`

On disk BEFORE the audit runs. nq-b, 2026-10-01. Base = merge-base with main
(`682caaba3deb6e678758194a1be0ce6e60e4d625`); `origin/main` is `bc1ec42a4c305aaf2ba58974cf1728a6eb97abbb`,
so **the branch base is genuinely stale** — established with the ancestry test CHAINED to a
confirmed-successful fetch (see the conflict note below).

## ⚠️ MY CONFLICT, STATED, AND THE BRIEF IS INVERTED BECAUSE OF IT

I wrote round 1's BLOCK **and prescribed this exact remedy** ("restore the backtick and both braces
to the sed cut set"). `09b532acf` is conv-fw's implementation of my prescription. **So asking
"is the bypass closed?" would be grading my own prescription** — the fix-verification shape the
round-N+1 rule exists to prevent.

**The brief is therefore inverted, per the coordinator: do not ask whether the bypass is shut. Ask
what the revert REINTRODUCES.** That question runs AGAINST my prior, so my stake pushes toward
finding problems rather than away from them.

## WHAT I ALREADY TESTED BEFORE SPENDING THE ROUND — so the engine finding it is NOT news

A **disclosed residual is an untested finding**, so I tested conv-fw's disclosure rather than
accepting it. Their comment (`merge-target-keys.sh:251-254`) states three of seven refusal arms are
"UNREACHABLE BY CONSTRUCTION".

**Method:** source `merge-target-keys.sh` with `$INPUT` set to hook JSON and read the `$RESTS` it
computes, placing each refused character AFTER the verb so it lands inside the `$RESTS` region.
**No candidate command is ever executed and the gate's merge path is never invoked.**

Ground truth read from `pre-merge-gate.sh:194` (NOT from the comment's prose):
`case "$RESTS" in *\"*|*\'*|*\\*|*\$*|*\`*|*\{*|*\}*)` — the seven are `"` `'` `\` `$` `` ` `` `{` `}`.

```
REFUSED CHAR   RESTS                  ARM STATUS
dquote  (")    < 99 --body X"Y>       REACHABLE
squote  (')    < 99 --body X'Y>       REACHABLE
backslash (\)  < 99 --body X\Y>       REACHABLE
dollar  ($)    < 99 --body X$Y>       REACHABLE
backtick (`)   < 99 --body X>         *** UNREACHABLE
obrace  ({)    < 99 --body X>         *** UNREACHABLE
cbrace  (})    < 99 --body X>         *** UNREACHABLE
```

**✅ THE DISCLOSURE IS EXACT: three arms, and precisely those three.** Tested, not accepted.

**✅ AND REQUIREMENT 1 IS VERIFIED AS A FREE SIDE EFFECT:** a backtick-wrapped verb and a
brace-grouped verb both yield **non-empty `RESTS` (`< 99>`)**, so the segment does reach `$CMD_M`
and detection fires. The revert does restore detection.

**✅ Their stated suite results reproduce exactly, run by me:**
`test-pre-merge-gate 109/0 · test-merge-gates 27/0 · test-prepush-gate 56/0 · test-gate-ux 7/0 ·
test-gate-wiring 135/0`, plus `test-twin-check 154/0` which they did not claim.

## ⛔ MY OWN FAILED SHARPENING, RECORDED BECAUSE THE FAILURE IS INSTRUCTIVE

I predicted the disclosure **understated** itself at five arms, reasoning that the cut set
`[;|&(){}\`]` contains **both parens**, so `(` and `)` should be stripped too. **The parens ARE
stripped — and no arm tests them.** I had inferred the seven from the comment's phrase *"seven
characters including backtick, `{` and `}`"* and guessed parens; the real four are `" ' \ $`.

> **A correct measurement (parens are stripped from `$RESTS`) joined to a wrong assumption about
> what the refusal tests would have produced a confident "five dead arms, the author undercounted."**
> The only thing that stopped it was reading the `case` statement instead of the comment that
> summarises it. **Same shape as quoting a grep hit without reading the enclosing function.**

## WHAT I EXPECT THE ENGINE TO MISS, AND WHY (the seal proper)

1. **That the dead-arm residual is DISCLOSED AND ALREADY TESTED.** The engine reads the diff; the
   comment is in the diff, so it will likely report the three dead arms as a finding. **That is not
   news and must not be scored as a new defect** — it is a known, documented, measured residual
   that cannot be fixed on that line.
2. **The stale base.** The branch does not contain `origin/main`. This needs the repo's base
   distribution, which is absent from the diff — the same subject-scope limit that hid the
   hardcoded-`main` assumption on sk#136.
3. **Whether the 4 NEW regression cases can actually fail.** ✅ **NOW MUTATION-TESTED BY ME, PRE-AUDIT
   — and the answer is a finding.** Re-narrowed the cut set to `[;|&()]` in a scratch copy
   (mutation-applied asserted by `diff` before trusting any result) and ran the suite:
   **control 109/0 → mutant 107/2**, failing with the exact bypass signature:
   ```
   FAIL detection survives: `gh pr merge 99`      want=2 got=0
   FAIL detection survives: { gh pr merge 99; }   want=2 got=0
   ```
   **The guard BINDS — but only 2 of the 4 cases fire.** The four shapes are
   `` `V 99` ``, `{ V 99; }`, `( V 99 )`, `$(V 99)`, and **conv-fw's own measurement table records
   `( V 99 )` and `$( V 99 )` as rc=2 under BOTH cut sets.** So by the author's own data two of the
   four cannot distinguish the restored set from the narrowed one.

   > ⭐ **The sharp part is not the count, it is the CAMOUFLAGE: all four print identically as
   > passing "detection survives" cases.** Nothing in the suite output distinguishes the two that
   > carry the guarantee from the two that cannot. The commit bills all four as "a guard against
   > re-narrowing"; its real strength is two. A future editor pruning "redundant" cases, or
   > re-narrowing while keeping the suite green, gets no signal — **and the precedent is in this
   > very file, where three dead arms went unnoticed because the suite exercised the quoted form.**

   Cheap remedy: split the loop so the two narrowing-sensitive shapes are named as the guard
   ("these fail rc=2→rc=0 under `[;|&()]`") and the other two are marked coverage-only.

   **So the question I most wanted the blind leg to reach is now answered pre-audit.** What I still
   expect it to miss is anything in the remaining 39-line diff that is NOT the cut set — I have
   examined the cut set, the refusal, and the new tests, and nothing else.

## WHAT WOULD CHANGE MY MIND ABOUT THE REVERT BEING CORRECT

A finding that the wide cut set breaks something **other** than the three refusal arms — any input
whose handling regressed between the narrowed and restored states and is not covered by the six
suites above. conv-fw's own corpus note is the reason to look: their 99 byte-identical case outcomes
missed this class entirely because every case put the metacharacter in an **argument**, never around
the **verb**.
