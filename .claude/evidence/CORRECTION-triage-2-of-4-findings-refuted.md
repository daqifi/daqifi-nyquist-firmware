# CORRECTION: 2 of my 4 "not superseded" findings are REFUTED — and a RENAME is indistinguishable from a REMOVAL

Supersedes the per-branch verdicts in `TRIAGE-5-closed-issue-branches.md` (`26b4ff5bf`) for
`fix/1053` and `preserve/824`. Produced 2026-10-09.

## Final state of the five, with the basis for each

| branch | verdict | basis |
|---|---|---|
| `fix/1053` | ✅ **FULLY superseded** — *my finding REFUTED* | subject-scoped: every `app_SystemInit` claim on main is **correct** |
| `fix/560` | ⭐ **partial — CONFIRMED** | main's **own comment**: `clientForceClosed … (0 until Opt 1)` |
| `perf/757` | ✅ fully superseded | both removed memsets **0 on main** |
| `preserve/824` | ✅ **FULLY superseded** — *my finding REFUTED* | behaviour present on main, **renamed** |
| `wip/913` | ⛔ **stands** — fix live, test orphaned | `test_913_spi_wait_stat.c` **absent** from main |

## ⛔ Refutation 1 — `fix/1053`: I counted VOCABULARY, not ASSERTIONS

#1053 asks whether lines claiming **`app_SystemInit` runs pre-scheduler** are wrong. My phrase grep
counted *"pre-scheduler"* anywhere, and it appears **legitimately** on main about **other** functions
— `SCPI_InitIdentification()`, `UserEdge_Initialize`, static semaphore creation, a hypothetical future
caller. **None is a claim about `app_SystemInit`.** Subject-scoped, every such claim on main is
already correct (`UserEdge.c:259`, `SCPIInterface.c:365`/`:668`, `SCPIInterface.h:43`).

```
UserEdge.c       phrase-grep 3   subject-scoped 0
SCPIInterface.c  phrase-grep 2   subject-scoped 0
```

> **A prose correction's unit is a CLAIM — subject + predicate.** A phrase grep answers *"does this
> vocabulary appear"*, which is a question about the file's **diction**, not about what it **asserts**.

## ⛔⛔ Refutation 2 — `preserve/824`: A RENAME IS INDISTINGUISHABLE FROM A REMOVAL

The branch adds `Streaming_GenerateSdFileHeader` / `Streaming_ResetSdFileHeader`; both are absent from
main. **The behaviour is not.** Main has it under different names:

```
streaming.c:2730   static void Streaming_BuildSdFileHeader(StreamingEncoding)
streaming.c:2806   size_t Streaming_GetSdFileHeader(const uint8_t** ppHeader)
streaming.c:3302   Streaming_BuildSdFileHeader(...) called
sd_card_manager.c:2470  ? Streaming_GetSdFileHeader(&hdr) : 0u
```

**`Generate`/`Reset` → `Build`/`Get`. Same work, renamed.** So #824's header-at-file-open landed.

> ⭐⭐ **THE PRECISE LIMIT OF THE INSTRUMENT, and it is sharper than "match the kind of change":**
> **an identifier set difference soundly detects that a NAME is absent, and CANNOT support an
> inference that BEHAVIOUR is absent — because a rename presents identically to a removal.**
> **Every "absent identifier" needs a behaviour follow-up before it is a finding.**

⛔ **I wrote exactly that caveat for `fix/689` and then did not apply it to `preserve/824` two
paragraphs later.** The rule was stated and not run. That is worse than not having it: the caveat's
presence in the same document made the un-caveated claims look deliberate.

## ✅ What stands, and why the basis matters

- **`fix/560`** — not "an identifier is missing" but **main's own declaration**:
  `wifi_tcp_server.h:237  uint32_t clientForceClosed; /* self-heal: dead client force-closed (0 until
  Opt 1) */`, and `wifi_manager.c:701` labels `acceptRefused` *"#560 Opt 0"*. **Opt 0 landed; main says
  Opt 1 has not.** A positive statement beats an absence.
  ⭐ **Side note:** `WifiClientForceClosed` is one of the **23 undocumented `SYST:STR:STATS?` fields**
  I measured earlier — so it is **emitted, undocumented, AND structurally always-zero.** A telemetry
  field that can only ever read 0 is the assertions-that-cannot-fail shape in the monitoring
  dimension.
- **`wip/913`** — the test FILE is absent, not a symbol inside one. A missing file cannot be a rename
  of a present file, so the inference is sound. Independently verified by the coordinator.

## ⛔ The instrument scoreboard, honestly

| branch | identifier check said | truth | classification |
|---|---|---|---|
| `fix/560` | 1 absent | partial | ✅ TRUE |
| `wip/913` | 17 absent | test orphaned | ✅ TRUE |
| `preserve/824` | 2 absent | superseded, renamed | ⛔ **FALSE** |
| `fix/1053` | 0 absent | superseded | **SILENT** (uninformative, not wrong) |
| `perf/757` | 0 absent | superseded | **SILENT** (a deletion — blind by construction) |

> ⭐ **AND I MISCLASSIFIED THE SILENCE.** I wrote that the identifier check *"returned a FALSE
> 'superseded'"* for `fix/1053`. It did not — it returned **nothing informative**, and I read its
> silence as a false positive. **An uninformative instrument and a wrong one are different failures
> and I had them conflated.** Of three informative readings, it was wrong once, and the failure mode
> is **renaming**.
