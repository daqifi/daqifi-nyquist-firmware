# fw#965: the fix is **pre-refuted in-tree**, and the row **fails the ruling it cites**

2026-10-14. Offered as *"fw#965's FIX … yours to design."* **There is no fix to design here, and the
row cannot land under the existing ruling either.** Both halves measured below. **I implemented
nothing.**

## 1 · ELIGIBILITY — declared, and I took it with a safeguard

| channel | result |
|---|---|
| ledger rows on #965 | **3 of 3 are nq-c** (rounds 1-3, all BLOCK). No nq-b row. |
| the `audit-965` worktree | **nq-c's**, by head-sequence correlation I reproduced; my session id absent with a live positive control |
| my 20 LOOP_LOG hits | **verifications of OTHER lanes' claims about** the row (`:4560-4561`, `:4679`), artifact-health tables (`:8150`, `:8496`), and last night's **artifact-custody** work (`:10342+`) |
| authored diff content | none — the branch's commits are lane-blind and the tree is nq-c's |

**So: artifact custody and claim-verification, not verdict authorship.** A fix is independently
audited, so the false-clearance asymmetry that makes eligibility load-bearing for an *audit* does not
apply.

> ⚠ **One real bias, declared:** I am on record in the shared memory store that this row's `BLOCK`
> sits atop `arbiter.verdict: ready_to_merge` and that `2` is a raw count. **That biases me toward
> UNDER-fixing.** Safeguard imposed: treat the two reports as independent and do not lean on *"it's
> one defect"* to narrow anything.

⭐ **The safeguard paid for itself immediately.** Reading the source, the two findings are **not** the
same defect: `[1]` clobbers the pin **values** (`gWifiFindPinStopGen/StopActive` — the winner would
read the loser's stop observation, sampled at a different instant) and `[2]` clobbers the **owner** (so
the winner's ownership check fails). **A fix addressing only the owner leaves `[1]` live.** The
one-defect framing would have produced exactly that under-fix.

## 2 · The mechanism, verified against the source rather than the prose

⚠ **The artifact's two findings are TITLE-ONLY** — `title/file/line/severity/category`, with **no
description, evidence or rationale field.** So the mechanism came from the routing message, not the
artifact, and had to be re-derived. It checks out:

```c
SCPI_WifiFindRate(context):                      /* :3785 */
    taskENTER_CRITICAL();
    gWifiFindPinStopGen    = gStreamStopGen;     /* :3808 */
    gWifiFindPinStopActive = (gStreamStopsActive != 0u);
    gWifiFindPinOwner      = context;            /* :3810  <- both findings land here */
    taskEXIT_CRITICAL();
    return SCPI_RunSessionStartClaimed(...);     /* :3813  can REFUSE */
```

WifiTask (TCP, prio 2) writes the pin, **wins** the claim; priority-7 USBDeviceTask preempts before
the claimed body reads the mailbox at `:3355`, **overwrites** the pin, **loses** the claim, returns
`-200`; WifiTask resumes, its owner check now fails, and it returns `-200` too. **Both fail although
one legitimately won.**

## 3 · ⛔⛔ BOTH CANDIDATE FIXES ARE ALREADY REJECTED, IN-TREE, WITH REASONING

The declaration's own comment, `:3297-3330`, and the PR body say it:

- **Per-transport / compare-and-set** — *"Under compare-and-set, the dangerous interleaving becomes:
  WiFi claims the EMPTY slot first, is preempted before its own claim attempt, USB's concurrent call
  finds the slot occupied (so USB's OWN write is skipped) and goes on to WIN the #850 claim outright …
  USB then enters its claimed body, finds a foreign (WiFi's) stamp, and **refuses ITSELF** despite
  being the legitimate claim winner. Same shape, same width … just with the two transports' roles
  exchanged. Given USB can preempt WiFi but not the reverse, **this is not a rarer trigger than the one
  it replaces.**"*
- **Moving the pin after the claim** — rejected because *"the claim is itself an observable effect of
  this command"*; a stop arriving between pin-write and claim would be mis-ordered. *"A naive local
  re-pin reopens #973 itself."*
- **The CORRECT fix is named and scoped elsewhere** — *"Route 2 (threading pinned values as real
  per-call arguments through `SCPI_RunSessionStartClaimed`) … would make the finder structurally
  identical to `SCPI_StartStreaming` and **delete this mailbox, the owner stamp, and the refusal path
  together**"*, *"out of scope for this Size-S ticket"*.

**Tracking ticket verified live, not taken on the comment's word:**

```
issue #1120  OPEN  "Thread the stop-race pin through SCPI_RunSessionStartClaimed as a real
                    parameter, not a per-command mailbox (#973 generalization)"   created 2026-09-17
CONTROL: #999998 -> "Could not resolve"   -> the lookup can fail
```

> **So the design space was explored before I arrived, the obvious fixes are refuted in writing, and
> the real fix is another ticket's.** Implementing Route 2 on #965 would grow a Size-S row's diff and
> do #1120's work on #965's branch — **the exact thing three lanes were told not to do tonight.**

## 4 · ⛔⛔ AND THE ROW FAILS THE STANDING RULING IT CITES

The PR body's recommendation is *"accept the #1120-tracked regression and land"*, calling it a
*"Candidate for the operator's standing 'a plausible finding the arbiter DEFERS no longer blocks'
ruling."* **That ruling does not reach this row.** Operator ruling, 2026-09-16, verbatim:

> *"A **CONFIRMED** finding still blocks absolutely. So does a plausible the arbiter says `fix_now`.
> The hard rule is not weakened — it is scoped to what the machinery actually judged, rather than to
> the word BLOCK."*

| ruling condition | fw#965's artifact | |
|---|---|---|
| the finding is **PLAUSIBLE** | **`confirmed: 2`, `plausible: 0`** | ⛔ **FAILS — the ruling's hard scope** |
| arbiter dispositions `defer_ticket` or better | `ready_to_merge` | ✅ |
| agreed severity is **LOW** | **medium** ×2 | ⛔ **FAILS** |
| a ticket is **actually filed** | **#1120 OPEN**, verified with a control | ✅ |
| failure direction cannot silently pass | `-200`, fail-closed, retryable | ✅ |

**Two of five fail, and one is the ruling's scope rather than a parameter.** ⭐ **The row's own
recommendation cites a ruling whose first condition it does not meet** — and `ready_to_merge` satisfies
a *different* condition of the same rule, which is what makes the mis-citation plausible on a quick
read.

## 5 · Companion-test reachability, checked before proposing one

The window is `taskEXIT_CRITICAL()` at `:3811` → the mailbox read at `:3355`, spanning
`Streaming_BeginSessionStart`. **Hittable in principle** — issue the same query on TCP and USB
concurrently in a loop — unlike #1027's branch, which was unreachable outright. But:

1. ⛔ **A miss proves nothing.** A race test that fails to interleave is an inert leg, and the PR body
   already records round 4 item #6 as *"Stop races remain untested"*.
2. ⛔ **There is a recorded board-wedge hazard on this row:** the PR body states a bench run *"wedged
   the board (the `#939` condition — an armed sweep with its consumer gone), which was recovered by a
   PICkit reflash."* My board is the only one with a working programmer tonight.

**So a companion test needs a hit-detector — proof the interleaving occurred — before it is worth
running, and the wedge risk is real and attributable.** Not proposing one until the operator picks
between option 1 and option 2, because **Route 2 deletes the mailbox and the refusal path, which would
delete the test's subject.**

## 6 · What this row actually needs

**Not engineering. A typed operator decision between the two options already posted on it** — and the
decision cannot be *"apply the standing ruling"*, because §4 shows the ruling does not extend to a
confirmed medium. So it is either **(a) an explicit extension of the ruling to confirmed/medium with
this row named**, or **(b) authorization of Route 2 under #1120**.

⚠ **I am not recommending either.** Which option is right is a merge/disposition judgement; I measured
which conditions hold. **Surface the facts, withhold the disposition.**
