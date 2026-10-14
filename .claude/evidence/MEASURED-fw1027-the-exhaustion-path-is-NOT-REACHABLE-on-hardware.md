# fw#1027: the branch the fix repairs is **not reachable from the device's own command surface**

2026-10-14, before flashing. **The assigned hardware A/B cannot test this fix.** Not a refusal — a
reachability measurement, with the trace. The PR already ships the test that *can* reach it.

## 1. The scratch name makes candidate 0 free by construction, within a boot

```
name      benchmark_<tick>_<seq>.dat          (SCPIStorageSD.c:1391 worked example)
tick      used WHOLE, 32-bit                  :1321 "the tick is used WHOLE. It used to be masked to
                                                      16 bits, so the name repeated every 65536 ticks"
seq       gBenchNameSeq, pre-incremented once per run, never 0
spacing   :1344 "two runs cannot share a tick, because this callback cannot run to completion in
                 under a tick" · :1355 ">= 10 ticks apart, whichever transport calls it"
```

> **So inside one boot every run gets a unique base name, candidate 0 always stats absent, the walk
> breaks at `probes == 0` with `scratchNameFree = true`, and the `!scratchNameFree` branch — the one
> this fix repairs — never executes.**

## 2. Across a reboot it is reachable in PRINCIPLE, and the source says exactly how

`:1380-1392`, which is the other half of #958 and is explicitly **not** fixed:

> *"a reboot restarts the sequence at 1 … and the tick at 0, so NEITHER field distinguishes one boot
> from another, and a run after a reboot can land on the name of a file left on the card before it and
> truncate it silently. … two first-runs collide only when they arrive at the same post-boot offset,
> and `benchmark_5000_1.dat` and `benchmark_10000_1.dat` are different names."*

**To exhaust the walk I need the base name AND `-1` … `-15` all present — 16 names sharing ONE base,
i.e. 16 runs that each land on the SAME post-boot tick.** The tick is 1 kHz, so that is **the same
millisecond, sixteen times**, across sixteen reboots, with USB-CDC enumeration jitter in the path.

⛔ **I cannot construct that procedure, and I am not going to present an approximation of it as a
verification.**

## 3. ⭐ The population half IS achievable — and that is a separate, reportable fact

There is **no reserved-prefix restriction** on the logging target. `SCPI_StorageSDLoggingSet` runs
`SD_StripConfiguredDir` → length check → `SD_ValidatePathParam` (#612), and nothing else:

```
:747  "Before the length check, so a max-length name is not rejected for the prefix."
      ^^ this "prefix" is the <directory>/ prefix that SD:LISt? prints -- NOT a benchmark_ guard
```

**So a user can set `SD:FILE` to `benchmark_<anything>.dat` and stream to SD, creating a file at a
scratch-candidate name.** ⚠ **This is round 6's blind-leg premise, and the source supports it.**
**I am reporting the fact and NOT the consequence** — whether that makes the fallback's loss worse than
"a stale benchmark file" is attack point 1, a judgement on a row I declined.

⛔ **But it still does not reach the branch**, because the benchmark chooses its own base from a tick I
cannot predict. **I can create any name; I cannot make the benchmark select a name I created.** Both
halves are needed and only one is available.

## 4. ✅ The venue that CAN reach it is already in this PR

```
tests/host/test_958_sd_benchmark_filename_collision.c     546 lines, added by this PR
tests/host/Makefile                                       +171 lines of recipe, 8 guards
.github/workflows/host-tests.yml                          +17
```

A host test **injects tick values** and sets up occupied names directly, so it reaches the exhaustion
branch in a way no device command sequence can. The recipe's own guards already pin the post-fix format,
the 40-byte field, the absence of the masked 16-bit form, and **the sequence-advance ordering — which
the pre-merge audit reproduced as a passing swap.** ⭐ **The verification this fix needs exists and is
host-side; the bench cannot substitute for it.**

## 5. What I WILL deliver on hardware, labelled honestly

The operator's rule is *"test on hardware always"*, and there is real hardware value here — just not
verification of this branch. At `714036910`, with `de5ad7bda` as the control:

| check | what it establishes |
|---|---|
| build boots, `*IDN?` + `CONF:CAP:JSON?` answer, crc32 recorded with its commit | the image is real and traceable |
| `SD:FILE` accepts a `benchmark_`-prefixed name **on silicon** | the blind-leg premise, measured rather than read |
| a benchmark run completes and produces a uniquely-named scratch file | the happy path, at the new head |
| a pre-existing differently-named user file is untouched by a run | no collateral loss on the normal path |
| a **documented attempt** to force the collision, and its failure | the reachability claim above, demonstrated not asserted |

⛔⛔ **And the thing I will NOT do: present that set as verification of the fix.** The `#958` log line
lives inside `!scratchNameFree`, so **it does not fire at all without a collision** — meaning every
assertion I can run on hardware **passes identically at `de5ad7bda` and `714036910`.**

> **A hardware suite that passes at both heads is an INERT LEG.** I caught exactly that in the recipe I
> was handed four hours ago; delivering one myself, having caught it, would be the same defect with my
> name on it — and *"writing a rule down feels like running it"* is already today's filed lesson with
> three of my instances in it.

**So: SMOKE TEST, labelled SMOKE TEST, with the reason stated. The fix's verification is the host test.**

## 6. Scope

I declined this row's audit on four involvement kinds and I am holding that line. §1-§4 are traces and
file facts, reported so the auditor has the measurement; **the venue question, the consequence of the
missing prefix restriction, and whether the host test's coverage is sufficient are judgements that are
not mine.** Surface the fact, withhold the judgement.
