# fw#965 reachability read: **practically unreachable** from a host, and the blast radius is **one command**

2026-10-14, source-only, no board. The two questions that move the disposition either way.

## Q2 FIRST, because it is the cleaner answer: **the hazard has a population of ONE today**

#1120 says the class *"is still open for every other command routed through this same runner."* True as a
class statement. **Measured, the population is one.**

The shared runner has exactly two callers:

```c
:2775  SCPI_RunThroughputBench(context) {
           return SCPI_RunSessionStartClaimed(context, SCPI_RunThroughputBenchClaimed, "…THRoughput");
       }                                   /* BARE PASSTHROUGH -- writes NOTHING before the claim */

:3807  SCPI_WifiFindRate(context) {
           taskENTER_CRITICAL();
           gWifiFindPinStopGen/StopActive/Owner = …   /* the ONLY pre-claim mailbox write */
           taskEXIT_CRITICAL();
           return SCPI_RunSessionStartClaimed(context, SCPI_WifiFindRateClaimed, "…WIFI:FINd?");
       }
```

`SCPI_RunThroughputBenchClaimed` reads only its own `SCPI_ParamInt32` arguments, `pStreamCfg->Running`
and the power state — **no mailbox, so nothing to clobber.** A third arming command exists and `:6168`
records that it *"does not use `SCPI_RunSessionStartClaimed`"* at all, so the runner's signature cannot
reach it either.

> ⭐ **The clobber hazard is not a property of the runner — it is a property of writing a mailbox before
> taking the claim, and exactly one command does that.** So the vulnerability is **prospective**: it
> becomes live when a *second* command adopts the pre-claim-mailbox pattern, which is precisely what
> #1120's parameter-threading would prevent by removing the need for the pattern.

**Severity consequence, stated as a fact and not a verdict:** there is **no second live command**, so
the *"the class is still open for every other command"* argument does not currently widen the blast
radius. **Whether that changes the disposition is the audit's call, not mine.**

⚠ **Checked and clean, so it is not raised as a finding:** `gStreamStopGen`/`gStreamStopsActive` appear
twice each (`:2893-2894` without initialisers, `:4682`/`:4705` with `= 0`). That is a tentative
definition followed by a definition of the same static object — **legal C, one object.** Not a defect;
recorded because an identifier appearing twice is the shape that usually *is* one.

## Q1 · The window is **structurally reachable** and **practically unreachable**, and the arithmetic says why

```
THE WINDOW   taskEXIT_CRITICAL()  :3811   ->   the mailbox read  :3355
             spans one call plus Streaming_BeginSessionStart()   ~= MICROSECONDS of MCU time
             USB is priority 7, WifiTask priority 2, so USB can preempt it at ANY instruction --
             the two commands need not be simultaneous, USB need only ARRIVE inside that span

THE ATTEMPT COST, from the finder's own constants:
  FIND_SOAK_MS           60000u   60 s per soak dwell
  FIND_SOAK_CLEAN_STREAK     2u   TWO consecutive clean 60 s soaks required before locking
  FIND_DWELL_MS           3000u   per arc/refine step   + FIND_SETTLE_MS 500 + FIND_INTERSTEP_MS 300
  FIND_SOAK_STEP_NUM        49u   -2% walk-down per LOSSY soak, which RESETS the streak
  -> ONE SWEEP IS >= 120 SECONDS, and minutes in the ordinary case
```

> ⛔⛔ **A microsecond window at a two-minute attempt cost.** Host arrival is controllable to
> milliseconds, so the per-attempt hit probability is on the order of **10⁻³ or worse**, and the
> expected attempts are ~10³ — **tens of hours of continuous bench time at the optimistic bound**, on a
> row whose own PR body records a bench run that **wedged the board** (`#939`, recovered by a PICkit
> reflash).

⭐⭐ **And this is WORSE than fw#1027's unreachability, not better.** #1027's branch was structurally
impossible to reach, which is honestly reportable in one line. **This one is reachable, so a test can
be written, run for days, miss, and report nothing — while looking like diligence.** A miss is
uninformative; only a hit is evidence.

**So a host companion test is not worth running without a HIT-DETECTOR** — something that proves the
interleaving occurred rather than inferring it from a `-200`, which both the winner and the loser return
and which an unpowered WiFi module also returns. Without that, the test cannot distinguish *"the race
did not happen"* from *"the race happened and I could not tell."*

⭐ **The venue is the same as #1027's: this belongs in a host test**, where the preemption can be
injected between the two statements instead of waited for. And #1120's remedy **deletes the mailbox, the
owner stamp and the refusal path together** — so it deletes the subject of any test written now.

## What this does and does not support

| | |
|---|---|
| ✅ the defect is **real** and the mechanism is confirmed against source | |
| ✅ the blast radius **today** is one command; the widening argument is prospective | |
| ✅ the window is a **genuine preemption window**, not a simultaneity coincidence | |
| ⛔ a host reproduction is **not practically runnable**, and a miss would prove nothing | |
| ⛔ **I am not pricing the severity or picking a disposition** — I measured the window, the attempt cost and the population | |

⚠ **Carried over from `MEASURED-fw965-needs-NO-FIX-and-FAILS-the-standing-ruling.md` and unaffected by
this read:** the row cites the *"a plausible finding the arbiter DEFERS no longer blocks"* ruling, and
that ruling says **"a CONFIRMED finding still blocks absolutely"** while this artifact is
`confirmed: 2 / plausible: 0` at **medium**. **Two of its five conditions fail, one being the scope.**
So *"plus an operator merge ruling already on record"* does not hold for this row — **the decision still
has to be made, not applied.**
