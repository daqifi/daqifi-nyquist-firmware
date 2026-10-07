# 23 of 59 emitted `SYST:STR:STATS?` response fields are undocumented — and no CI gate can catch it

**A READING — a measurement, not a judgement, so it carries no eligibility cost and belongs to
whoever wants it.** Produced 2026-10-07. Found as a by-product of pinning the bench board's firmware
capability for ts#306.

## The measurement

Device: `DAQiFi,Nq1,7E2873046200E891,01-02` (lane nq-b's board), live `SYST:STR:STATS?` response.
Wiki: `daqifi-nyquist-firmware.wiki.git` at `7bbf2299`, **all 5 pages**.

```
device emits                          59 distinct STR:STATS? fields
undocumented anywhere in the wiki     23   (39%)
```

### Controls — both directions, because a one-sided differential proves nothing

| control | expected | got |
|---|---|---|
| `ScanStaleDropped` (known documented) | DOCUMENTED | ✅ DOCUMENTED |
| `DioDroppedSamples` (suspected absent) | UNDOCUMENTED | ✅ UNDOCUMENTED |
| `zzqqNoSuchField` (cannot exist) | ABSENT | ✅ ABSENT |

**Two bounds I checked rather than assumed:**

1. **One page vs all five** — the page-only count was 23; the whole-wiki count is **also 23**.
   Widening rescued nothing, so the finding is not an artefact of looking in one file.
2. **Case variants** — my matcher is `grep -wF`, case-*sensitive*, and I have had three
   case-mismatch failures today. Re-ran all 23 case-**insensitively**: **0 rescues.** They are
   genuinely absent, not differently spelled.

## The 23, and they fall into two clusters — not random oversights

**Cluster A — every `*Steady` (grace-filtered) variant, while its base field IS documented:**

```
QueueDroppedSamplesSteady     (QueueDroppedSamples   IS documented)
EncoderDroppedSamplesSteady   (EncoderDroppedSamples IS documented, line 837)
DioDroppedSamplesSteady       (DioDroppedSamples     also undocumented)
EncoderFailuresSteady
```

> Whoever documented the base counters did not document their variants. **A systematic pattern, so a
> one-by-one fix will not prevent the next one.**

**Cluster B — the WiFi socket-lifecycle diagnostics, 13 fields, none documented:**

```
WifiAcceptFails · WifiAcceptRefused · WifiCirbufBufSize · WifiCirbufConsumed
WifiCirbufProduced · WifiClientForceClosed · WifiIdleClosed · WifiListenFails
WifiListenHardResets · WifiListenReopens · WifiListenState · WifiSocketOpenFails
WifiWriteBufferRejectedBytes · WifiWriteBufferRejectedCalls
```

**Remainder:** `CircularBufferEndBytes`, `DioDroppedSamples`, `PoolExhaustedSamples`,
`QueueOverflowSamples`, `UsbBytesSent`.

## ⛔ Why no gate catches this, and why that is the real finding

`CLAUDE.md` records that an August 2026 audit found the command surface had drifted **both ways** —
37 shipped commands with no wiki entry, and 11 documented commands that were not registered — and
that this was fixed by adding a CI gate: `.github/workflows/scpi-wiki-sync.yml` →
`tools/lint/scpi_wiki_sync.py`.

> **That gate compares REGISTERED COMMANDS against the wiki. It does not compare RESPONSE FIELDS.**
> So the identical drift, in the response-field surface, is structurally invisible to it — and
> `CLAUDE.md` itself says new `SYST:STR:STATS?` fields *"flow through automatically — no script
> changes needed"*, which is true of the **test harness** and is exactly why nothing notices they
> were never documented.

The command drift got a gate because someone measured it. **The field drift has never been measured
until now**, and it is 39%.

## ⭐ Independent confirmation of an unrelated finding, from the device

On 2026-09-11 I confirmed fw#991's finding #5 by source read: the loss summary prints
`(all post-grace)` while totalling **three** grace-filtered terms plus one **raw** one
(`scanStaleDropped`). The emitted field list confirms it a month later from the opposite direction:

```
QueueDroppedSamples   + QueueDroppedSamplesSteady     ✅ pair
EncoderDroppedSamples + EncoderDroppedSamplesSteady   ✅ pair
DioDroppedSamples     + DioDroppedSamplesSteady       ✅ pair
ScanStaleDropped      + (no Steady sibling)           ⛔ raw only
```

**Three paired, one unpaired — exactly as the source read said.** V plus E, derived independently.

## Bounds — what this does NOT establish

- **Measured against ONE board's firmware**, and I could **not** establish its version: there is no
  firmware-version SCPI command (`SYSTem:VERSion?` returns **`1999.0`**, the *SCPI standard* year —
  I read the pattern without reading its callback and briefly mistook it), and the lane registry
  records `image=UNKNOWN/UNKNOWN`. So this is *what this firmware emits*, not *what `main` emits*.
  **Capability-based dating is the available substitute:** `ReadLoopMaxNs`/`ReadLoopMeanNs` are
  present and were wiki-documented 2026-09-15 (#251), so the image is at least that recent.
- **The inverse direction is NOT a finding.** 63 wiki table-row field names are not emitted by this
  firmware — but the wiki documents many queries (`SYST:MEM:FREE?`, `SYST:DIAG:SPIBus:STATs?`, …),
  not only `STR:STATS?`, so that number is **not** a phantom-documentation count and I am not
  reporting it as one. Measuring it properly needs a per-query field inventory.
- **I have not checked whether any of the 23 is deliberately undocumented** (internal diagnostics
  may be intentionally omitted). That is a judgement for an owner, not a reading.

## What an owner could do with it

A response-field gate is the same shape as the existing command gate: enumerate the fields each
`*:STATS?`-style callback emits, compare against the wiki's response-field tables, allow an explicit
`NOT DOCUMENTED (internal)` marker. **Cluster A suggests the cheapest first step is narrower:** assert
that every documented counter's `*Steady` sibling is also documented, which is a two-line rule that
would have caught 4 of the 23 and prevents the recurring half.
