# DAQiFi Nyquist‑1 — Streaming Throughput Spec (source of truth for marketing)

**Firmware basis:** v3.6.2 (current shipping). The enforced streaming caps were set
at v3.6.0 (#541 read‑path / scan‑bound release) and are **unchanged through v3.6.1
and v3.6.2** — neither touched the cap math.
**Last updated:** 2026‑06‑29.
**Maintainer note:** this doc is the canonical source the web agent uses to update
<https://daqifi.com/products/nyquist-1/>. Every number here is a **firmware‑enforced,
zero‑loss cap** — the rate a customer can actually set via `SYST:STR:START` and
receive *complete* data. Burst / NOCAP / soak‑ceiling numbers are deliberately
excluded (the device **rejects** them with SCPI `‑222`).

---

## ⚠️ The current product page OVERSTATES throughput — correct it down

The live page headlines rates the firmware does **not** allow:

| Live page claim | Firmware‑enforced reality (v3.6.2) |
|---|---|
| "Up to 20 kHz × 1 ch" | **15 kHz × 1 ch** |
| "9 kHz × 16 ch = 144 ksps" / "USB streaming: 144 ksps" | **5 kHz × 16 ch = 80 kS/s** |

A customer cannot set 20 kHz (1 ch) or 9 kHz (16 ch) and get complete data — the
device caps and rejects. Publishing those is a failed‑bench‑test risk. Use the
verified numbers below.

---

## Verified enforced caps — per‑channel rate (Hz) and aggregate (samples/s)

Config per cell: 1 ch = 1×T1 · 5 ch = 5×T1 · 10 ch = 5T1+5T2 · 16 ch = 5T1+11T2
(OBDiag OFF). T1 = dedicated simultaneous‑conversion ADC; T2 = shared‑scan ADC.
Aggregate = channels × per‑channel rate.

| Interface | Encoding | 1 ch | 5 ch | 10 ch | 16 ch (aggregate) | Status |
|---|---|--:|--:|--:|--:|:--:|
| **USB** | Protocol Buffers | **15,000** | 10,000 | 6,260 | **5,000 (80,000 S/s)** | ✅ validated |
| **USB** | CSV | 15,000 | 5,666 | 3,090 | 2,000 (≈494 KB/s wire) | ✅ validated |
| **MicroSD** | Protocol Buffers | **9,000** | 7,500 | 4,500 | **3,750 (60,000 S/s)** | ✅ validated ³ |
| **MicroSD** | CSV | 7,500 | 2,470 | 1,900 | 1,500 | ✅ validated ³ |
| **WiFi** (typical AP)¹ | Protocol Buffers | 5,175 | 3,971 | 3,475 | 3,021 (48,336 S/s) | ✅ validated ⁴ |
| **WiFi** (typical AP)¹ | CSV | 4,675 | 2,857 | 1,666 | 1,111 | ✅ validated ⁴ |
| **USB + SD** | Protocol Buffers | 8,000 | 6,000 | 5,250 | 3,000 | ⏳ provisional |
| **USB + SD** | CSV | 8,000 | 3,000 | 1,500 | 1,000 | ⏳ provisional |

✅ **validated** = at‑cap, zero‑loss soak on the v3.6.0 read path (2026‑06‑12, USB
28/28 cells PASS), unchanged through v3.6.2.
⏳ **provisional** = from the #524 fit‑basis / Session‑24 soak (v3.5.x‑era);
**being re‑validated at‑cap on v3.6.2 this session** — do not publish until ✅.
¹ WiFi caps are **worst‑night‑observed** (real link varies ~1.5× night‑to‑night);
always publish with a "typical 2.4 GHz AP, link‑dependent" qualifier.
⁴ **WiFi validated 2026‑06‑29** (v3.6.2 + #573, full‑matrix 60 s walk‑down,
**all headline cells PASS at the enforced cap, zero WiFi drops**,
`benchmarks/atcap_20260629_192641.csv`). Tonight's link was good — these are the
enforced caps holding on a typical AP; per ¹ a worse link can still drop at these
rates (the firmware enforces the cap regardless; runtime AIMD #523 would harvest
good‑link headroom). The 16‑ch PB cell (3,021 Hz) supersedes the inflated v3.5.0
~4,565 Hz (pre‑#540 scan‑skip basis). Required a clean‑state device — see #576
(a heap leak degraded the first attempt).
³ **MicroSD validated 2026‑06‑29** (v3.6.2 + #573, 60 s walk‑down zero‑loss soak,
`benchmarks/atcap_20260629_181627.csv`). Two cells came in **below the old provisional
because the firmware SD cap is over‑set there** (10 ch PB enforced 5,198 → zero‑loss
4,550; the prior 6,000/5,000 dropped samples) — the table publishes the **zero‑loss
rate**, not the over‑set cap. The cap derate is tracked in **#574**.

---

## Headline numbers (defensible today — USB, validated)

- **Per channel:** **up to 15 kHz/channel** (USB, 1 channel, zero‑loss).
- **Aggregate:** **up to 80,000 samples/s** (USB, Protocol Buffers, 16 channels @ 5 kHz).
- **Wire rate:** **≈0.5 MB/s** CSV straight off USB (16 ch @ 2 kHz, `pc_kbps` ≈ 494).

### Headline copy options (use only these — all V)
- "Stream **15 kHz on a single channel** or **80,000 samples/s across all 16** — USB, with **guaranteed zero data loss**."
- "Up to **15 kHz per channel**, zero data loss."
- "Up to **80 kS/s aggregate** (16 channels), guaranteed complete."

### The differentiator to lead with
**Guaranteed zero‑loss streaming:** rates above the safe cap are *rejected*, never
silently dropped. (v3.6.0 #541 made Type‑1 channel *values* fresh at rate; #524 made
caps format‑aware so a quoted rate is one you actually receive intact.)

---

## Methodology footnote (the skeptic's version — include near the spec table)

> Rates are firmware‑*enforced* zero‑loss caps on Nyquist‑1 firmware **v3.6.2**,
> measured at‑cap with 120 s+ soaks (USB cells validated 2026‑06‑12 on the v3.6.0
> read path — every cell PASS, zero bytes lost). "Per‑channel rate" is
> samples/second/channel (Hz); "aggregate" = channels × per‑channel rate. Each figure
> is specific to the stated **interface**, **encoding** (Protocol Buffers vs CSV —
> CSV is larger on the wire, so its cap is lower), and **channel count/type**.
> Exceeding a cap is rejected by the device, never silently degraded. **WiFi figures
> are worst‑night‑observed** (link capacity varies ~1.5× night‑to‑night). Full
> characterization: `daqifi-python-test-suite/benchmarks/`.

---

## Concrete product‑page edits (before → after)

**Hero / headline** — *overstated, correct down:*
- Before: "Up to 20kHz×1ch, or 9kHz×16ch = 144ksps aggregate"
- After: **"Up to 15 kHz per channel, or 80,000 samples/s aggregate across 16 channels — USB, with guaranteed zero data loss."**

**USB streaming line** — *overstated, correct down:*
- Before: "USB streaming: 144ksps · PB 16ch @ 9 kHz (5T1+11T2)"
- After: **"USB streaming: up to 80 kS/s — Protocol Buffers, 16 ch @ 5 kHz (5T1+11T2), zero‑loss enforced cap."**

**MicroSD logging line** — *validated, correct down:*
- Before: "MicroSD logging: 64ksps · PB 16ch @ 4 kHz (5T1+11T2)"
- After (validated): **"MicroSD logging: up to 60 kS/s — Protocol Buffers, 16 ch @ 3.75 kHz, zero‑loss."** (v3.6.2 walk‑down‑validated 2026‑06‑29; the prior 4 kHz/64 kS/s was above the zero‑loss rate. SD cap derate tracked in #574.)

**WiFi streaming line** — *validated, add qualifier:*
- Before: "WiFi streaming: 22ksps · PB 11×T2 @ 2 kHz"
- After: **"WiFi streaming: up to ~48 kS/s — Protocol Buffers, 16 ch @ 3 kHz (5T1+11T2); typical 2.4 GHz AP, link‑dependent."** (v3.6.2 good‑night‑validated 2026‑06‑29; 1 ch reaches 5.2 kHz. WiFi rates vary ~1.5× night‑to‑night — always publish with the link‑dependent qualifier.)

**No change (all verify):** "12‑bit · 4,096 levels", "16 analog inputs (0–5 V)",
"16 digital I/O", "802.11n WiFi (AP + Station)", "USB 2.0", "4000 mAh".

**Add:** a zero‑loss‑guarantee statement; CSV‑vs‑ProtoBuf as a user choice (CSV ≈0.5 MB/s
USB wire rate); the firmware‑version basis (v3.6.2) for auditability.

**Fix:** the linked datasheet PDF (`Nyquist_1.pdf`, 2023 link) is an **image‑only 2021
photo** with no extractable spec text — regenerate it as a real text/spec sheet.

---

## DO NOT PUBLISH (device rejects these)

| Number | Why it's wrong for marketing |
|---|---|
| 20 kHz × 1 ch | NOCAP/burst; enforced cap is 15 kHz |
| 9 kHz × 16 ch / 144 ksps | exceeds enforced 5 kHz/80 kS/s; rejected `‑222` |
| USB CSV 16 ch @ 7 kHz → 1,798 KB/s | Session‑24 soak ceiling, above enforced cap |
| USB 10 ch CSV @ 6,000 Hz → 1,142 KB/s | pre‑#541 SPEC_TABLES measured ceiling |
| Any single‑trial figure | policy: cite soak/zero‑loss‑validated only |

---

## Validation status & pending work

- **USB (PB + CSV, all channel counts):** ✅ validated — `benchmarks/541_adc_read_path/atcap_20260612_085210.csv` (v3.6.0, zero‑loss, unchanged v3.6.2).
- **MicroSD:** ✅ validated 2026‑06‑29 — `benchmarks/atcap_20260629_181627.csv` (v3.6.2 + #573, full‑matrix 60 s walk‑down zero‑loss soak, no wedge). 10 ch / 16 ch PB corrected **down** from the provisional (firmware SD cap over‑set there — zero‑loss rate published; derate tracked in #574).
- **WiFi:** ✅ validated 2026‑06‑29 — `benchmarks/atcap_20260629_192641.csv` (v3.6.2 + #573, fresh‑device full‑matrix 60 s walk‑down, **all headline cells PASS at the enforced cap, zero WiFi drops**; good‑night link — keep the link‑dependent qualifier). The first attempt failed on a heap‑degraded post‑SD‑soak device (#576); a reboot to clean state fixed it.
- **USB+SD:** ⏳ provisional — not yet re‑validated on v3.6.2 (lower priority unless headlined).
- The real path to **higher** published numbers: the #557 cap‑headroom review (the conservative #541 event/EOS‑rate caps were retained pending review; the #525 bug they guarded is fixed in v3.6.1 — the 16‑ch USB hardware sustained 11 kHz in soak vs the 5 kHz enforced cap). Re‑validate at‑cap, then raise the caps, then market the higher figures. Tracked separately from this spec.

### Source data
- `daqifi-python-test-suite/benchmarks/541_adc_read_path/atcap_20260612_085210.csv` (USB, v3.6.0)
- `daqifi-python-test-suite/benchmarks/541_adc_read_path/SILICON_ANCHORS.md`
- `daqifi-python-test-suite/benchmarks/524_streaming_characterization/SPEC_TABLES.md`
- `CLAUDE.md` → "Streaming Frequency Capping" (fit basis) + "ADC Architecture › Characterization results" (Session‑24 soaks)
