# fw#1027 hardware **SMOKE TEST** — explicitly *not* verification of the fix

2026-10-14, board `7E2873046200E891` (COM8), programmer PICkit 5 `020026703RYN079002` (`-TPPK5`,
`--ts` passed). **Labelled smoke test because the branch this PR repairs cannot be reached from the
device's command surface** — see `MEASURED-fw1027-the-exhaustion-path-is-NOT-REACHABLE-on-hardware.md`.
**Every assertion below passes identically at `de5ad7bda` and at the fix. None of them tests the fix.**

## Image, recorded AT FLASH TIME because it cannot be derived afterwards

```
flashed      714036910d9ea9ba72c283ebca00163335bba2b7   (built in /mnt/c/daqifi/wt/bench-1027)
*IDN?        DAQiFi,Nq1,7E2873046200E891,01-02   <- SERIAL confirms the TARGET
crc32        553E07E4  ->  247EB7BF              <- crc confirms the IMAGE changed
registry     nq-b … 247EB7BF/714036910           (was the UNKNOWN/UNKNOWN placeholder)
```

⚠ **The head moved to `15ba2cfbf` after this flash.** `714036910..15ba2cfbf` is a log-string rewrite in
one file, and **the log line is unobservable on the bench** (§5), so every conclusion here transfers.
**I did not reflash**: it would cost an NVM wipe for zero additional information.

## 1 · `SD:FILE` accepts a scratch-candidate name — the blind-leg premise, ON SILICON

| probe | `SYST:ERR?` | role |
|---|---|---|
| `"benchmark_nqbprobe_1.dat"` | `0,"No error"` | **ACCEPTED** |
| `"benchmark_12345_1.dat"` — the exact scratch shape | `0,"No error"` | **ACCEPTED** |
| `"../escape.dat"` | **`-224,"Illegal parameter value"`** | ⭐ **negative control — the instrument CAN refuse** |
| `"nqbplain.csv"` | `0,"No error"` | positive control — it is not refusing everything |

> **Round 6's blind-leg premise is confirmed on silicon: a user can point the logging target at a name
> in the exact shape of a benchmark scratch candidate.** The traversal refusal is what makes this a
> measurement rather than a validator that passes anything.

⛔ **Consequence NOT assessed.** I declined this row's audit on four involvement kinds; whether this
makes the fallback's loss worse than *"a stale benchmark file"* is attack point 1 and belongs to
whoever audits.

## 2 · Name format on silicon — and a differential against the board's own history

```
produced by 3 runs in one boot:
  DAQiFi/benchmark_207504_1.dat   1024
  DAQiFi/benchmark_262823_2.dat   1024
  DAQiFi/benchmark_285211_3.dat   1024
already on the card, from the masked-tick era:
  benchmark_32738.dat · benchmark_57322.dat · benchmark_63233.dat · benchmark_34101.dat
```

⭐⭐ **Two fields present, sequence advancing 1→2→3, every tick ABOVE 65535** — against four
pre-existing files whose ticks are **all below 65536**, the signature of the 16-bit mask that was
#958's original defect. **So the whole-32-bit-tick + sequence fix is confirmed on hardware**, which is
the PR's *earlier* half, not the walk fix. Inter-run tick deltas **55,319** and **22,388** against
sleeps of ~25 s and ~18 s, so **the tick is a 1 kHz millisecond counter — measured, not assumed.**

## 3 · No collateral loss

`17 → 18 → 20` entries across three runs; `diff` of the full listing shows **additions only, zero
modified or removed.** The 13 pre-existing user files (`t846r*.csv`, `s848.csv`, `route6_stream.bin`, …)
hold identical sizes throughout.

## 4 · ⭐ The collision could not be forced — demonstrated, not asserted

```
#958 lines in SYST:LOG? after three runs:  0
POSITIVE CONTROL: the log is live -- 4 non-blank lines carrying exactly the errors my own
  probes generated:  -113 ×2 (my guessed SD:FILE?)  ·  -224 ×2 (the traversal control)
```

> **Three runs, three unique names, candidate 0 free every time, and the `!scratchNameFree` branch did
> not execute once.** The zero is sound because the same log carries four entries I caused deliberately
> — **an uninformative instrument and a working one are different, and this one is working.**

**The measured tick behaviour explains why 16 collisions cannot be arranged:** names collide only at
the same post-boot millisecond, observed spacing is tens of thousands of ms with multi-second jitter,
and exhaustion needs **sixteen** such coincidences across sixteen reboots.

## 5 · ⛔ What this does NOT show

**The log line lives inside `!scratchNameFree`.** It never printed, so the bench observed **nothing**
about the new wording, the field ordering, the `%.Ns` width, or which candidate is named. Those are
**host-test territory** — `tests/host/test_958_sd_benchmark_filename_collision.c`.

> **A hardware suite that passes at both heads is an inert leg.** I caught one in the recipe I was
> handed; shipping one myself and calling it verification would be the same defect with my name on it.

## 6 · Requested text for the host test's docstring (for the row's owner to land)

```
# POPULATION: the base name PLUS -1 … -16 — SEVENTEEN files — with the victim
# content in -16. DO NOT TRIM THIS.
# Trimming to -1 … -15 makes the broken head's `base-16` not exist, so its open
# CREATES instead of TRUNCATING: the test still runs, still passes, and no longer
# tests anything. The 17th file is the only reason the defect reproduces.
```

## 7 · Defects found in my own probing

1. ⛔ **`SYSTem:STORage:SD:FILE?` DOES NOT EXIST** (`-113`). The setter is registered; there is no
   query. **I had enumerated the command table myself minutes earlier and the query was not in it** —
   so I violated this repo's *"never guess SCPI syntax, always verify"* rule **with the evidence already
   on screen.** Fourth instance today of *writing a rule down feels like running it*.
2. ⛔ **The SCPI error queue is FIFO, so "the error after command X" can be a STALE error from an
   earlier command.** My first pass read `-113` as the escape-probe's result when it was the leftover
   from the bad `SD:FILE?`. **Drain to `0,"No error"` between probes**, or the attribution is wrong.
