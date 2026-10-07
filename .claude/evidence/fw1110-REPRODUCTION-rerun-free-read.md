# fw#1110 — I re-ran the reproduction. **FIXED is CORRECT. All three address legs are INERT.**

Free read, delivered under a standing DECLINE (`ELIGIBILITY-fw1110-DECLINE.md`, `06e4033f0`). This is
a reproduction, not a verdict. Read-only at `7b1443084d7896296ae3d914ede3a631e8386975` and its parent
`fada72a5f01defe4283d7c4717e75f5f94c391d7`. Produced 2026-10-07.

**Direction note, so this is weighable:** my stake on this row biases me *against* `PREMISE-FALSE` —
toward findings being real. I am confirming a fix **is** real, which runs against my own bias. That
makes this result more trustworthy than a BLOCK from me would be, not less.

---

## ✅ The conclusion holds — confirmed by PREDICATE, not by address

The commit message claims *"bound the three unannotated %s LOG_E sites"*. Three annotations is
exactly what it adds:

| file | annotations at `fada72a5f` | at `7b1443084` |
|---|---|---|
| `SCPIADC.c` | 0 | **1** |
| `sd_card_manager.c` | 0 | **2** |
| `SCPIInterface.c` | 4 | 4 *(unchanged — see below)* |

And the retired baseline message resolves live: all three surviving call sites of
`LOG_E("Cannot start SD logging - SD suspended: %s\r\n", …)` — at `SCPIInterface.c:5044`, `:5364`,
`:5588` — each carry `/* log_budget: max=76 */` immediately above. Negative control
(`zzqq_no_such_string_zzqq`) → 0, so the search discriminates.

**So: FIXED. I reach it independently and I do not cite nq-a for it.**

---

## ⛔ But all three cited addresses are checks that CANNOT FAIL

The differential — run nq-a's own address legs against the **parent**, the head that still had the
defect:

| cited site | at `fada72a5f` (**broken**) | at `7b1443084` (**fixed**) | differential |
|---|---|---|---|
| `SCPIInterface.c:4832` | `uint64_t stored = pRunTimeStreamConfig->Frequency;` | *identical* | ⛔ **none** |
| `SCPIInterface.c:5397` | `*` | *identical* | ⛔ **none** |
| `SCPIStorageSD.c:1872` | `/* log_budget: max=8,8 */` | *identical* | ⛔ **none** |

**Every leg reports "absent" at the broken head too.** The lines hold unrelated code and an unrelated
annotation; `LOG_E` was never going to be there in either state. A 0/0 differential is
indistinguishable from a dead instrument, and this one is dead in all three legs.

Worse on the third: `SCPIStorageSD.c:1872` was reported *absent* while it **contains** a
`log_budget` annotation, unchanged across both heads — and **`SCPIStorageSD.c` is named by no retired
baseline entry at all.** The baseline's SD-side entries name `sd_card_manager.c`. Wrong file, not
just a wrong line.

So the entire FIXED conclusion rests on **one** surviving check — `log_budget.py` exit 0 — which is
precisely the green the coordinator flagged as expiring. The reproduction has one leg, not four.

### Where the addresses came from, and why no address could have been sound

`:4832` occurs **exactly once in the whole tree**, and it is inside a code comment:

> `SCPIInterface.c:5584:  * already use (:4832, :5151) -- measured worst case 118`

That is an **N-class source** — our own prior note — and it is **stale**: the sites it cross-references
now live at `:5044`/`:5364`/`:5588`. The baseline names a **file** and a **message string** and never a
line number, so an address-keyed check could not have been derived from the baseline in the first
place. This is `cite the PREDICATE, not the ADDRESS` firing on a live reproduction: firmware
`file:line` drifts 10–55 lines between trees, and `7b1443084`'s branch has taken two base refreshes.

---

## ⭐ NEW — nobody has this: the baseline carries STALE SUPPRESSIONS, and a duplicate

`7b1443084` retires **5** baseline lines and adds none. Two of them name `SCPIInterface.c` — **a file
this commit never modifies**, whose annotation count is **4 before and 4 after**.

> Those two entries were suppressing findings that were **already fixed** at `fada72a5f` or earlier.
> The baseline was over-suppressing, and the gate was green partly on stale entries.

This matters for the merge discipline the coordinator named: *"regenerate the baseline immediately
before merge, never early."* The failure mode here is the **other** direction from absorption —
retiring is safe, but a baseline that carries entries for already-fixed findings means its line count
is not a defect count, and a shrinking baseline is not by itself evidence of a fix.

**And the raw/distinct shape recurs inside the baseline itself:** two of the five retired lines are
**byte-identical** (same file, same rule, same message). **5 raw / 4 distinct** — the same shape as the
`6 raw / 5 distinct` on the audit side, one layer down.

---

## What I am NOT saying

- I have **not** audited this row and will not. The decline stands on grounds untouched by any of
  this: I spent one of its ten capped `/agentic_review` rounds, and I authored its unpark condition.
- I verified **one** finding — the retired `SCPIInterface.c` message. I did **not** enumerate whether
  nq-a's 6-raw/5-distinct set is otherwise discharged, and this says nothing about the other four.
- `6 raw / 5 distinct` remains a **floor**. Nothing here raises it; the stale-suppression result
  argues the baseline is a weaker instrument than its green suggests, which pushes the same way.
