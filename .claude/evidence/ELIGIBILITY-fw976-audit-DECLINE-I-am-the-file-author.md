# fw#976 fix audit — **DECLINE.** I am the file's three most recent authors.

Produced 2026-10-13. Asked to check my record on `tools/lint/scpi_sd_arm_path.py` rather than the row.
**I am not clear on that file; I am its last three committers.**

```
9dfc09d75  2026-10-09 12:11  close the parenthesized-callee gap at the direct-update site (#976)   MINE
efba54c1f  2026-10-07 12:41  catch direct sd_card_manager_UpdateSettings() arm sites (#801)        MINE
d55d5dbd8  2026-10-07 12:29  qualify hash_function.py's path in scpi_sd_arm_path.py               MINE
578f32be8  2026-09-15        (prior lane) bounded 5-shape call contract
```

At the live head, symbols I introduced: **`_direct_update_problems` ×11**, **`MANAGER_UPDATE_CALL` ×9.**

> ⛔ **And the requester's own framing makes it worse, not better:** *"the already-closed
> parenthesised-call defect was a SHAPE problem, solvable by parsing the call expression."* **I closed
> that defect**, at `9dfc09d75`, four days ago. So the fix under audit layers on mine, in my file,
> with my commit as the baseline it is measured against. **A fixer cannot audit the increment on his
> own increment.**

## ⛔⛔ AND A CONTAMINATION SURFACE I AM FLAGGING RATHER THAN LEAVING IMPLICIT

I hold a detailed, *published* analysis of this exact file from my two fix rounds — on `lane/nq-b`,
in `.claude/evidence/`: that `_call_positions` is consulted at **10 sites across 5 functions**, that
`account_occurrences` is at **1**, that `_accounting_problems` uses **both**, that `compiler_view` is
already wired into the accounting path, and that **8 of the 9 names** the weak helper serves were
already enrolled.

**That is the fixer's framing of the file.** It is *not* in fw#976's tree, so an auditor diffing the
branch will not see it — **but if it is routed to the auditor as input, the auditor inherits my map of
where to look**, and an independent audit becomes a guided one. Same shape as the `FIRE_976.md`
removal, one remove further out: not inside the audited artefact, but inside the record the auditor
might be handed.

**Not my call to make.** Routing it is a judgement about audit independence and I am the conflicted
party. Flagging it so the decision is explicit rather than a default. ⭐ Note the asymmetry: the map
would make the audit *faster* and *less independent*, and only one of those is the point.

## What I am NOT doing

- Not auditing. Not scoping the fix. **Not sending the auditor my file map**, for the reason above.
- **One substantive note I will state once and not elaborate**, because it is the requester's own and
  already in their brief: a *discarded verdict* is a **dataflow** property, and the only part of that
  tool which already does dataflow is the single `UpdateSettingsForStreamingLog` verdict-consumption
  check. Anyone scoping from *"we closed the last one by parsing the call expression"* will
  under-scope it — and **a grep cannot find a missing behaviour**, which is why a mutation settled it
  both previous times.

## Pool

conv-fw is **fixer** (disclosed: round-10 auditor of record and applied the park label). nq-a is out
(rounds 8 and 9). **I am out (last three commits in the file).** → **nq-c or conv-ts.**

⚠ And I remain **four days stale on the bench** with `batch.sh` changed under me (claude-skills
`2c11ba7`) and untested on my board — so I am not a candidate for any bench half of this either until
that is re-verified.
