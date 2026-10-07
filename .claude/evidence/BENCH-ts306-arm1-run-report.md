# ts#306 arm 1 — bench run report. The tightened budget is SAFE, and the park's literal question is UNANSWERED.

Lane nq-b, 2026-10-07. Authorised round: one fix + bench + audit. I am the **fixer**; the audit is not
mine, and I wrote this row's park and fw#991's, so I disposition nothing here.

## Version triplet — recorded, not left blank

| | |
|---|---|
| device | `DAQiFi,Nq1,7E2873046200E891,01-02` — verified by `*IDN?` serial, not by port |
| firmware | **rev `3.8.0`, crc32 `553E07E4`** (from `CONF:CAP:JSON?` → `identity`) |
| test-suite | `80c63d476792a3514f2c98fa0674bf3215ebb794` (PR #306 head, verified via `ls-remote`) |
| python-core | `C:\daqifi\daqifi-python-core` @ `81933cd493cbe0a052d2c2e7d749b7194f670730` |
| python | **3.12.0** — see the 3.14 blocker below |
| pre-state | error queue drained to `0,"No error"` ×2; `SYST:POW:STAT? = 1` |

⚠ **`CLAUDE.md` records this board's crc32 as `9CF57ADD`. The device reports `553E07E4`.** Rev matches
(3.8.0), crc32 does **not**, so the board has been reflashed with a different 3.8.0 build or the note
is stale. **Flagging, not editing** — correcting `CLAUDE.md` is a repo change under
converge-and-merge-only.

⭐ **And this retracts my own earlier claim.** I reported *"there is no firmware-version SCPI
command"* after `SYSTem:VERSion?` returned `1999.0`, and proposed capability dating as a substitute.
**`CONF:CAP:JSON?` carries `firmware_rev` and `firmware_crc32` directly.** I concluded an absence from
one wrong probe and then built a workaround for it — worse than the original error, because the
workaround made the gap look settled. Capability dating remains valid where no version exists; it
was not needed here.

## The runs — 3 trials, all PASS

```
trial   blocks   readings   expected   missing   budget   verdict
  1      3900     62400      62400        0        32      PASS
  2      3976     63616      63616        0        32      PASS
  3      3898     62368      62368        0        32      PASS
```

`--self-test` (pure logic, no device): **13 PASS, 0 FAIL** — the fire's claim that all pre-existing
cases survive, verified independently by me.

⭐ **The audit's recorded figures reproduce exactly.** It cited a *"~3,900-block run"*; trial 1 gave
**3,900 blocks / 62,400 readings**. `Live 16ch JSON cap: 1777 Hz → target 2949 Hz (1.66× cap, NOCAP)`.

## ✅ What the run DOES establish

**The tightening cannot false-fail on observed hardware behaviour.** 3/3 trials pass at budget 32
with 0 missing readings. The park's practical worry — that dropping the fractional term would start
failing good runs — **does not materialise**.

## ⛔ What it does NOT establish — and this is the headline

> **The budget was never exercised. 0 of 32, three times.**

A run with **zero** truncation cannot distinguish *"32 is adequate"* from *"0 would also have been
adequate."* So the park's literal question — *"tightening the budget to the absolute term has not been
shown to hold against real window-edge truncation on hardware"* — **is still not answered.** What is
answered is the stronger and more interesting fact:

> **On this firmware at this configuration, real window-edge truncation DOES NOT OCCUR AT ALL.**
> `channel-readings == blocks × 16` exactly, in all three trials.

**This is the same 0/0 shape as the inert reproduction legs earlier today**, and I am flagging it
rather than letting three PASSes read as a validation of the number 32. The 32-reading residual is
**unexercised headroom**, not a measured tight fit.

### Why truncation may not be reproducing — hypothesis (I), not established

The test's own docstring calls `--rate-multiple 1.66` *"the bench-verified value that exposes the
defect on unmodified firmware"*. It did not expose it. Either **(a)** this firmware is not
"unmodified" in the relevant sense, or **(b)** 1.66× is not a reliable exposure on this board.
**Neither is established and I did not chase it** — fw#991 (the #970 fix) is still OPEN, so I cannot
claim the firmware carries it. **The experiment that would close it:** run the same test against a
firmware build known to predate #970 and confirm `missing_readings > 0` appears. That is a flash, and
flashing is not in this round's grant.

### The loss that DID occur was correctly classified as not-the-defect

```
pc_samples_window=3900  expected~=14745  deficit~=10845  fraction=26.4%
QueueDroppedSamples=9756   EncoderDroppedSamples=0   EncoderFailures=0
```

~9,756 samples dropped at the **queue** (legitimate overload at 1.66× cap), while every sample that
reached the wire was **complete**. The test distinguishes queue-level drop from encoder truncation
correctly. The accounting arm is not satisfied and **correctly not gated** (firmware#982's teardown
drain discards ~pool-depth samples with no counter).

## ⭐⭐ The test PRINTS the purpose-vs-action finding at runtime

Verbatim from the device run — the contradiction is self-documenting:

> *"harness leaked=True (now includes `EncoderDroppedSamples`, ts#306) — that counter only fires on a
> whole-call encoder failure, never on this defect's mid-call truncation, **so leaked still cannot see
> the gap this test exists to cover**"*

So fw#991 park item 1 — *"without it the nightly gate cannot see the loss this PR exists to report"* —
**is satisfied as worded and does not achieve its stated reason**, and the test says so out loud on
every run. The fire confirmed the same against firmware source (`streaming.c:3514` feeds the
`if (encoded == 0)` block at `:3537-3550`; `EncoderDroppedSamples` increments only on whole-call
failure, never on a non-zero partial encode, which is what #164 is).

## ⛔ BLOCKER FOUND: this test cannot run on the box's DEFAULT python

```
py -0p:  -V:3.14 *   C:\Python314\python.exe      <- DEFAULT
         -V:3.12     C:\Program Files\Python312\python.exe
```

Under **3.14.7**: `ValueError: badly formed help string` from `argparse._check_help`, raised at
`add_argument` time — so the module cannot even parse its own arguments, and `--self-test` is
unreachable too. Cause: the `--duration` help text contains **`13% overshoot`**, a bare `%`, which
argparse formats. **Pre-existing** — present at `bb1e518c5:633`, before this round's commits; the fire
did not introduce it. Python 3.14 added eager validation that 3.12 did not have.

⚠ **I do not know how widespread this is.** My first sweep reported "0 files affected", which is
**uninformative, not reassuring**: the `%` sits on a *continuation* line, so a single-line
`help=.*%` pattern cannot see it — including in the file I already knew had it. A sound survey needs
a multi-line or AST-based check. **Not fixed here**: a one-character `%`→`%%` change is outside *"one
commit fixing the confirmed findings"*, and the 3.12 interpreter is available, so the round was not
blocked. **Flagged as its own item** — the suite is silently unrunnable on this box's default
interpreter.

## Honest limits

- **n=3. This is E, not V** (two independent reproductions are stronger E; still not V).
- **One board, one firmware build, one configuration.** The uncalibrated factory cal is irrelevant
  here (this is loss accounting, not accuracy).
- **python-core was imported from the SHARED checkout** `C:\daqifi\daqifi-python-core` (3.12's
  resolution; 3.14 resolves to `C:\daqifi\wt\daqifi-python-core` — two different checkouts). I only
  **read** it, but its HEAD is recorded above because another lane could move it between runs.
- ⛔ **The fire flagged a behaviour change I am passing straight through, unassessed:** its defect-2
  fix makes `release_gate.classify()` go from `('PASS', '1 PASS')` to `('FAIL', …'every phase
  self-skipped but its requirements were not declared --missing')`. So a runtime SKIP now **hard-FAILs**
  the nightly gate rather than merely being distinguishable. That matches the stated intent, and it
  is a real behaviour change that wants the operator's eyes before the next nightly.
