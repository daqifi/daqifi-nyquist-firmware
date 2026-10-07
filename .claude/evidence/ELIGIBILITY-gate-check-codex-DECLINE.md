# `gate-check-codex.py` review — **DECLINE**. Design choice 2 decides my own fix's disposition.

Produced 2026-10-07. Both channels run, controls verified both directions. **And one ground I was
about to declare turned out to be FALSE — that retraction is the more useful half of this file.**

## ⛔ THE GROUND THAT FIRES — sharper than "an artifact about one of my rows"

The coordinator predicted the conflict would reach me via ts#306. **It fires on fw#976, and not as a
subject-matter stake but as a stake in the specific design choice I am asked to ratify.**

`~/.claude/lanes/audit-fw976-b960c9245.json`:

```
head_sha                b960c92452130385dba3e2028e7c1d0bbe49a7c9
findings[0]             "Direct manager calls bypass occurrence accounting"
                        tools/lint/scpi_sd_arm_path.py : 1134 : medium
blind_findings[0]       "Account for direct manager calls before trusting the arm census"
                        tools/lint/scpi_sd_arm_path.py : 1134
truncated False · tools_degraded False · repo_path_matches_head True · 348794/348794
```

1. **`b960c9245` is a commit I authored and pushed** — *"docs: move FIRE_976.md off this branch"*,
   2026-10-07 12:56.
2. **Line 1134 is my own `:801` fix** (`_direct_update_problems()`), added in this round's
   `efba54c1f`. The artifact's sole finding, in both legs, is about code I added.
3. **Health is clean and findings are non-empty** → this artifact lands exactly on **arm 4 of the
   control matrix: `healthy + 1 finding → exit 3 UNREPRESENTABLE`.**

> ⛔⛔ **Design choice 2 is the choice that decides whether my own fix can ever clear.** conv-fw's
> verdict was *"your fix works for the spelling it targets… a materially different report from 'the
> fix doesn't work'"* — a **scoped, strictly-better** fix, which is precisely the shape choice 2
> declares unrepresentable. If "unrepresentable" stands, fw#976 needs a human. **My stake points
> toward REJECTING it**, i.e. toward the more permissive reading.
>
> **A reviewer's costly error is approving a gate that is too permissive. My stake points the same
> way. That is the matching-direction case, and it disqualifies.**

This is not attenuated relative to conv-fw's conflict — it is a **different and more specific** one:
conv-fw would be grading the instrument that reads *its own verdict*; I would be ratifying the design
rule that decides *whether my own fix is dispositionable at all.*

## ⚠⚠ THE GROUND I RETRACT — my own memory store FALSELY CONVICTED me

I was about to decline on a second, stronger-sounding ground: **that I authored the design principle
the instrument implements.** My `MEMORY.md` index carries:

> *"A degraded round BLOCKS only when the gate HAS A FIELD for it — 199 artifacts… **Gate on 'is it in
> the condition set', never on the verdict**"*

That **is** the principle (`absent primitive ⇒ UNEVALUABLE, never False`). But the file's own
frontmatter says:

```
authored_by:      conv-fw (bench-free convergence lane)
originSessionId:  43cc33e5-b310-4ef1-a98e-20707b91ced8      <- not mine (f5691ce5-…)
```

**conv-fw wrote it. I did not.** I read it in *my own* index and inferred authorship.

> ⛔⭐⭐ **THIS INVERTS A RULE I HAVE APPLIED ALL DAY.** The established rule is *"neither channel can
> CLEAR, both can CONVICT."* **A shared-store record can CONVICT FALSELY.** The memory directory has
> ~5 writers; `MEMORY.md`'s index lines carry **no attribution**; so a hit in "my records" is evidence
> the fact is *available to me*, **not** that I produced it. **The authority is the file's
> `authored_by` / `originSessionId` frontmatter, and nothing else.**

**Why this matters beyond one near-miss:** over-declining is not free. It had already removed three
lanes from this review; a fourth decline on a fabricated ground would have left only nq-c and made
the pool look structurally impossible when it is not. **A false conviction and a false clearance are
both errors**, and the direction that *feels* safe is the one I was about to commit. Cf. the same-day
rule that a decline built on inflated grounds is as unsound as a clearance built on a dead control.

## What I can still do, clearly bounded — for the coordinator to accept or refuse

The highest-severity item named is mechanical and carries no design judgement:

> **Does the implementation collapse `blind_findings` present-but-EMPTY into absent?** On fw#1013 it
> is `[]` and the blind leg genuinely ran, so **emptiness there is a RESULT, not an absence**, and only
> a MISSING key is unevaluable.

That is a question about code behaviour, answerable with **synthetic inputs** (`{"blind_findings": []}`
vs the key omitted), producing **no verdict about any row and no design ratification**. Same shape as
the oracle work. **I am not doing it unasked** — it is adjacent to a file I have just declined, and the
boundary between "test one predicate" and "review the instrument" is exactly the kind of line that
erodes silently.

**I am NOT doing** either half of the brief: not ratifying/rejecting the design, and not running the
gate on ts#446's artifact (I declined ts#446 earlier today on cited precedent plus a cross-PR stake —
`b0f65f38c`).

## Two factual notes offered without assessment

- ⚠ **Durability:** the four artifacts are caught by `.gitignore:6:/*`, a root-level deny-by-default
  catch-all — **not version-controlled.** Durable against a session ending, **not** against anyone
  cleaning `~/.claude`. I confirmed all four exist with distinct sizes. **Do not treat them as
  archival**, and note the coordinator said the same independently.
- ⭐ **On the control matrix:** the right attack is the one already named — *six arms is not six
  independent tests if they share a predicate.* Concretely: arms 2 (`truncated=true → BLOCK`) and 6
  (`coverage 40/100 → BLOCK`) both terminate in BLOCK, so a defect that forces BLOCK unconditionally
  passes both and would be invisible to the pair. The arm that discriminates against that is arm 1
  (`clean → 0`), which is therefore load-bearing for the whole matrix rather than merely the happy
  path. **Stated as a reading of the matrix's structure, not as a finding about the code**, which I
  have not examined.
