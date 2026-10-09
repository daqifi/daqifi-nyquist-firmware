# The 5 remaining PR-less branches — and **the instrument was wrong for 2 of them while returning confident numbers**

A READING. No recommendations. Produced 2026-10-09, completing the 13-branch triage.

## Result per branch — identifier set-difference vs **the check that actually fits**

| branch | #issue | identifier check | THE RIGHT CHECK | state |
|---|---|---|---|---|
| `fix/1053-app-systeminit-scheduler-claim` | CLOSED | 15/15, **0 absent** → "superseded" | ⛔ **prose only HALF landed** | **NOT superseded** |
| `fix/560-wifi-listener-selfheal` | CLOSED | 25/26, **1 absent** | `wifi_tcp_server_ReapDeadClient` absent | partially superseded |
| `perf/757-rotation-window-memsets` | CLOSED | 4/4, 0 absent *(vacuous)* | ✅ both memsets **0 on main** | **fully superseded** |
| `preserve/824-uncommitted-followup` | CLOSED | 19/21, **2 absent** | `Streaming_{Generate,Reset}SdFileHeader` absent | partially superseded |
| `wip/913-spi-yield-rescued` | CLOSED | 13/30, **17 absent** | ⭐ **fix on main, TEST absent** | **fix landed, test orphaned** |

Controls ran against `main` once and behaved both directions: positive
(`sd_card_manager_UpdateSettings`) found, negative (`zzqqNoSuchSymbolZZ`) absent. The deletion check
carried its own pair.

## ⭐⭐ The three findings worth more than the list

### 1. `fix/1053` — #1053 is CLOSED while main still carries the WRONG claim in five files

The issue: *"Two files claim `app_SystemInit` runs pre-scheduler; it runs INSIDE the pri-1 task
(UserEdge.c and SCPIInterface.c)."*

```
main, WRONG claim ("before the scheduler" / "pre-scheduler"):
    UserEdge.c ×3 · UserEdge.h · UserI2c.c · UserI2c.h · DAC7718.c
main, CORRECTED claim ("inside the pri-1 task" / "within the priority-1"):
    SCPIInterface.c ×3 · SCPIInterface.h · UserIC.c
```

**SCPIInterface was corrected; UserEdge was not** — and the issue names both. The branch holds the
full 11-file correction. ⛔ **The identifier check called this one "fully superseded, 0 absent."**

### 2. `wip/913` — the fix is live on main and its 339-line host test is orphaned

```
branch:  UserSpi.c +95 · tests/host/Makefile +31 · tests/host/test_913_spi_wait_stat.c +339 (new)
main UserSpi.c  yield sites: 3      branch UserSpi.c: 3     -> the PRODUCTION FIX LANDED
main tests/host/test_913_spi_wait_stat.c            -> ABSENT
```

The 17 absent identifiers are almost all test names (`condition_met_mid_spin_returns_without_yield`,
`deadline_survives_tick_counter_wrap`, `mock_delay_1_tick`, …). **So #913 closed with the fix shipped
and its regression test left on an unreferenced branch** — against this project's standing rule that
every firmware change ships with a regression test. *The orphaned-application shape, in the test
dimension.*

### 3. `fix/560` and `preserve/824` each landed half

`fix/560`'s two commits are *"Opt 0 — listener-health observability counters"* and *"Opt 1 — reap
PATH-2 zombie TCP client"*. 25 of 26 identifiers are on main; the one absent is
**`wifi_tcp_server_ReapDeadClient`** — **the counters landed, the reaper did not.**
`preserve/824` is missing `Streaming_GenerateSdFileHeader` and `Streaming_ResetSdFileHeader`, i.e.
the write-the-header-at-file-open work #824 was closed for.

## ⛔⛔ METHOD — the instrument must match the KIND OF CHANGE, not the kind of artifact

The identifier set-difference check was proven on `fix/689`, endorsed as "the right instrument", and
**was wrong for 2 of these 5 while returning confident numbers for both:**

- **`fix/1053` (prose correction):** its identifiers are the symbols being *discussed*, so they are on
  main whether or not the correction landed. **It reported 0 absent — a FALSE "superseded".**
- **`perf/757` (a deletion):** the change *removes* two memsets, so an **added**-identifier diff is
  structurally blind. Its "0 absent" was a **right answer from a wrong instrument**, which is not a
  result.

> **Three kinds of change, three instruments:**
> | the branch… | check |
> |---|---|
> | **ADDS** code | added-identifier set difference vs main |
> | **REMOVES** code | are the removed lines still present on main? |
> | **CORRECTS** prose | does main carry the wrong form, the right form, or both? |
>
> ⭐ **My guard caught the EMPTY case and missed the WRONG-KIND case.** I had built a fallback for "the
> diff adds no identifiers" — but `fix/1053` added fifteen, so the guard passed and the measurement
> was still irrelevant. **A non-empty result is not evidence the instrument fits.**

## Two errors of mine in this run, both caught

1. ⛔ **I grepped `HAL/spi.c` for `wip/913`'s yield. The branch touches `HAL/UserSpi/UserSpi.c`.** The
   empty result let me infer *"the fix did not land"* — **wrong, and in the direction that OVERSTATES
   the residue.** Reading the branch's own `--stat` corrected it. *Same wrong-path class as reading a
   shared file at the wrong revision.*
2. `git grep -c <rev> -- <path>` prints `rev:path:count`; my `cut -d: -f2` returned the **path**. Fixed
   with `awk -F: '{print $NF}'`.

## Scope held

**Nothing is recommended for deletion and no disposition is offered.** `perf/757` is the only one
measured fully superseded. The other four each hold content absent from main, and in two cases
(`fix/1053`, `wip/913`) **the issue is closed while the gap is live** — which is a finding for an
owner, not a conclusion I am entitled to draw about SD or SPI correctness.
