# fw#976 — the seven unresolved threads, read. No round spent, no head moved.

Free read only: **no fix pushed, no audit run, no thread resolved, no label touched.** The
disposition decision waits on the authorisation-scope question either way.
Source read at the live head `d31f63c7a55f1b3d2f7f1225d8cd94bd597103c1`
(`tools/lint/scpi_sd_arm_path.py`, 2,456 lines).

## ⭐⭐ ALL SEVEN ARE ONE MECHANISM: THE CHECKER VERIFIES **PRESENCE**, NOT **CONSEQUENCE**

| line | finding | what it says the checker fails to do |
|---|---|---|
| **:920 HIGH** | Failed claims still reach storage arms | counts one claim call, never that its **false result terminates** the handler |
| :1122 | Refused storage commands report success | verifies the claim, never that the handler **checks the wrapper's Boolean and aborts** |
| :842 | Storage commands report success unarmed | reports only **>1** delegation — so **zero** is accepted (`return true;` body) |
| :1033 | Lost streaming arms can escape the gate | treats a captured verdict as sufficient, never that it **controls a refusal exit** |
| :1043 | Suspended storage work escapes checks | checks `SD_ClaimOrRefuse` precedes, never `SD_RefuseIfSuspended` |
| :694 | Format refusal can stay published | accepts any bare identifier ≠ `NULL` as a callback **without establishing it is callable** |
| :801 | Lost arbitration escapes checks | ⚠ **different shape** — a **discovery** gap, not a consequence gap |

**Six of the seven say the same thing**: *replacing a guarded call with a bare call keeps the gate
green.* `:920` is the **claim**-side spelling and `:1122` is the **wrapper**-side spelling — siblings,
not duplicates. `:694` is the same shape one level down: **a name is not a thing.**

## ⛔⛔ AND THE FILE ALREADY COUNTED THIS CLASS TO **FIFTEEN** AND STOPPED, DELIBERATELY

Its own docstring, section *"What moved to a host test, and why (#976)"*:

> *"This file used to ALSO assert that both writes fall INSIDE `SD_ArmOrRefuseWithCleanup()`'s claim
> AND run only on the refusal path — a claim about which BRANCH a write sits in, not just which call
> it sits between. **Three review rounds on #976 catalogued FIFTEEN separate ways an honest refactor
> of that one helper defeats a textual check of that shape**; four representative ones: **a write
> hoisted out of the failure guard so it runs on every path**; a release moved so the region it
> "protects" stops meaning anything; a ternary rewritten to an if/else that moves the branch's only
> `return` one level down; **a compound guard whose second term a regex swallows**. **Each round
> bought back correctness against the ONE mutation it was written for and left the next equivalent
> rewrite free to defeat it again.**"*

and at `:2076`: *"a plain early return reads clean here too. **The sha256 pin catches it.**"*
and: *"**#896 already tracks that this family of checker is a guard against honest regression, never
against a determined refactor**; #976 is the case where that limit was **actually reached on this
file**, not just theorised about, so the ordering moved to a tool that does not share it."*

> **So six of these seven are instances 16–22 of a class the file enumerated to fifteen and then
> removed from scope on purpose.** Each proposes exactly what the docstring says each prior round
> did: buy back one mutation and leave the next free. **Acting on them restarts a treadmill the row
> abandoned after three rounds.**

## ✅ THE REPLACEMENT COVERAGE IS REAL — VERIFIED, NOT INHERITED

A removed assertion is only out of scope if the thing it moved to exists. Checked at the live head:

| | |
|---|---|
| `tests/host/test_971_sd_arm_refusal_order.c` | ✅ **PRESENT, 30,057 bytes** |
| wired into the build | ✅ `ARM_BIN := run_971_tests` in `tests/host/Makefile` |
| the content-hash tripwire | ✅ implemented in that Makefile — *"it is a tripwire"*, *"an assertion here would give false comfort"* |
| `hash_function.py` | ✅ exists — **but at `tests/host/`, not `tools/lint/`** |

## ⛔ THE ONE GENUINE DEFECT I FOUND IS A STALE CITATION, AND IT IS THE CLASS BEING HUNTED

The docstring cites **`hash_function.py --self-test`** as what pins the sha256, in a context implying
`tools/lint/`. **`tools/lint/hash_function.py` does not exist at this head; `tests/host/hash_function.py`
does.** So the mechanism is present and the **path citation is wrong** — *"a comment, docstring or
printed message stating something the code no longer does."* Small, real, and fixable in one line.

## What this read does NOT establish

- **Whether `:801` is in scope.** It is the only one of the seven that is a **discovery** gap — the
  checker finds arm sites only via the two helpers and does not scan direct
  `sd_card_manager_UpdateSettings` calls. That is about **where the checker looks**, not about what a
  textual check can prove, so the fifteen-variants argument does **not** cover it. **It may be a real
  in-scope gap and I am not dispositioning it.**
- **Whether the host test actually models the claim-side ordering** the seven describe. I verified it
  exists, is sized, and is built. **I did not read its 30 KB** or run it — that would be the fix/audit
  round, which is not authorised.
- **Any disposition.** Six-of-seven-are-documented-scope is a *reading*, not a ruling.
