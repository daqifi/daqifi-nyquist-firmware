# fw#1027 recipe calibration: the log assertion is **INERT**, and "17 tries" is **16**

2026-10-14, before spending any build. The assigned recipe said to assert that `SYST:LOG?` *"must carry
the 'taken after 17 tries - reusing, old file lost' line naming a file the walk actually examined."*
**Traced against both heads' source, read directly rather than inferred from the diff.**

## The constants, measured

```
sd_card_manager.c:112   #define SD_CARD_MANAGER_UNIQUE_TARGET_MAX  16u
:2279  for (; probes < MAX && gSDCardData.filePath[0] != '\0'; probes++)
:2298  FIXED head only:  if (probes + 1u >= MAX) break;      <- breaks AT the last stat'ed candidate
log    FIXED: (unsigned)probes + 1u        BROKEN: (unsigned)probes
name   generateFilename: counter==0 -> "<bucket>/<original>" ; counter!=0 -> "<bucket>/<base>-<counter><ext>"
```

## Trace with every candidate occupied — the two heads side by side

| head | truncates | `probes` at exit | **log prints** |
|---|---|---|---|
| `de5ad7bda` BROKEN | **`base-16`** — regenerated after the last stat, never checked | 16 (fell off the loop) | `probes` = **16** |
| `5b371e2b3` FIXED | **`base-15`** — the last candidate actually stat'ed | 15 (broke at it) | `probes+1` = **16** |

> ⛔⛔ **THE LOGGED COUNT IS 16 ON BOTH HEADS.** The old code's `probes` runs one further and prints the
> same number the new code reaches by adding one. **So an assertion on the log count passes at the
> broken head and the fixed head alike — an INERT LEG**, in the very assignment that warned *"a
> reproduction whose legs are inert looks exactly like a fix."*

**Two further corrections to the recipe:**
1. **"17 tries" is wrong — it is 16**, at both heads. `MAX` is 16 and candidate 0 is the un-suffixed
   original, so the walk examines the base name plus `-1`…`-15`.
2. ⚠ **The message cannot "name a file the walk examined" at either head.** It prints
   **`gpSDCardSettings->file`** — the *configured* name — not `gSDCardData.filePath`, the candidate. So
   the name in the log is the base name regardless of which candidate was taken.

## ✅ What IS a valid discriminator, and the recipe's file half is right

**WHICH FILE GETS TRUNCATED.** Populate the bucket with the original **plus `-1` … `-16`** (17 files),
identifiable content in **`-16`**:

```
BROKEN de5ad7bda : filePath ends on base-16  -> the open TRUNCATES -16   -> content DESTROYED
FIXED  5b371e2b3 : filePath ends on base-15  -> the open TRUNCATES -15   -> content of -16 SURVIVES
```

⭐ **The 17th file is load-bearing and the recipe was right to ask for it.** With only the base plus
`-1`…`-15`, the broken head's `base-16` **does not exist**, so its open *creates* rather than truncates
and **nothing is destroyed — the defect would not reproduce at all.** So: file-population leg **VALID**,
log-count leg **INERT**, and the two arrived in the same instruction.

## How the test will assert, after calibration

| assertion | status |
|---|---|
| `-16`'s content survives at `5b371e2b3` **and is destroyed at `de5ad7bda`** | ✅ **differential** — the primary |
| `-15` is the file truncated at the fixed head | ✅ differential (`-16` at the broken head) |
| `SYST:LOG?` contains the `#958 … taken after N tries` line at all | ⚠ **presence only** — fires on both heads, kept as a liveness check, **not** as a discriminator |
| the count equals 17 | ⛔ **DROPPED** — false at both heads |
| the logged name identifies the taken candidate | ⛔ **DROPPED** — the message logs the configured name |

## ⚠ Scope of this document

**This is TEST CALIBRATION, not an audit finding.** I declined the audit on fw#1027 on four involvement
kinds, and whether *"the log claimed N tries"* constitutes a defect in the log's truthfulness — the
third attack point in the assignment — is a judgement on this row that is **not mine to make.** I am
recording what the two sources print, with the trace, so whoever audits it has the measurement and
reaches their own verdict. **Surface the fact, withhold the judgement.**

⭐ **And the catch was cheap only because the differential was run STATICALLY first.** A source trace
against the broken head costs minutes and does not replace the hardware run — it told me which
assertion to put on the instrument before I spent two builds, two flashes and two NVM restores on a
leg that could not fail.
