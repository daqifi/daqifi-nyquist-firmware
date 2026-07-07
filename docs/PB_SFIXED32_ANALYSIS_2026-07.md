# Protobuf `analog_in_data`: `sint32` vs `sfixed32` — evaluation (#392)

**Date:** 2026-07 · **Status:** analysis / recommendation only — no code change proposed.
**Scope:** `DaqifiOutMessage.proto` field 2 (`repeated sint32 analog_in_data`) and the
streaming encode path in `NanoPB_Encoder.c`.

## TL;DR

**Keep `sint32`. Do not switch to `sfixed32`.**

For every ADC resolution DAQiFi ships (12/18/24-bit), the raw code magnitude is
bounded such that the zigzag-varint encoding is **never larger than 4 bytes** and is
**smaller than 4 bytes on average** — so `sfixed32` (always 4 bytes) offers **zero
worst-case benefit** and a **strict average-case wire-size loss**, worst for the NQ1
12-bit volume product (payload doubles). The only upside — a modestly cheaper inner
loop — does not move the throughput wall, which the existing characterization shows is
transport-bound (not encoder-CPU-bound) below the enforced caps. The change is also
**wire-incompatible** (breaks every deployed client parser). Net: cost with no
corresponding benefit.

## 1. How the value reaches the wire (verified)

- `AInSample.Values[]` are **`uint32_t`** raw ADC codes
  (`state/data/AInSample.h:94`), holding at most 24 significant bits
  (proto comment field 2: "maximum 24bit/ch").
- The streaming fast path (`encode_streaming_fields`,
  `NanoPB_Encoder.c:1196`) casts each to `int32_t` and calls
  `pb_encode_svarint()` — **zigzag** then varint. Because a raw code
  `c ∈ [0, 2^24-1]` casts to a **non-negative** `int32_t`, zigzag is simply
  `zz = 2·c`. (Even a sign-extended bipolar code lands at `|n| ≤ 2^23`, giving
  `zz ≤ 2^24` — the same 4-byte ceiling.)
- The field is **packed** (proto3 default, honored by nanopb): one tag + one
  length prefix + back-to-back element bytes. The current code computes the
  length prefix with a **second, sizing-only `pb_encode_svarint` pass** over the
  channels (`NanoPB_Encoder.c:1221-1238`), i.e. two svarint passes per message.

## 2. Wire size per sample

Varint length of an unsigned value `v`: 1 B (`v≤127`), 2 B (`≤16 383`),
3 B (`≤2 097 151`), 4 B (`≤268 435 455`), 5 B (larger). With `zz = 2·c`:

| ADC res (board) | code range | zigzag range | `sint32` bytes: min / typical (mid-scale) / max | `sfixed32` bytes |
|---|---|---|---|---|
| 12-bit (NQ1) | 0 – 4 095 | 0 – 8 190 | 1 / **2** / 2 | 4 |
| 18-bit (NQ3) | 0 – 262 143 | 0 – 524 286 | 1 / **3** / 3 | 4 |
| 24-bit (NQ2) | 0 – 16 777 215 | 0 – 33 554 430 | 1 / **4** / 4 | 4 |

Reading the table:

- **12-bit / NQ1:** codes ≥ 64 take 2 bytes; sfixed32 is **4** → payload **doubles**.
  This is the highest-volume product and the worst case for the switch.
- **18-bit / NQ3:** codes ≥ 8 192 take 3 bytes → sfixed32 is **+33 %**.
- **24-bit / NQ2:** codes ≥ 1 048 576 (≈ the top 93.75 % of the range) take 4 bytes,
  same as sfixed32 → **tie** on the bulk of the range, `sint32` marginally better on
  the low tail, **never worse**.

Note the issue's premise that full-scale 24-bit zigzag reaches "4–5 bytes" is not
reachable with real data: a genuine ≤24-bit code zigzags to ≤ `2^25` (33.5 M) which is
< `2^28` (268 M), so it is a **4-byte** varint. A 5-byte varint needs magnitude
> `2^27` (134 M) — impossible for a 24-bit ADC. Therefore **`sfixed32`'s bounded 4-byte
worst case provides no improvement over `sint32`'s worst case for any DAQiFi variant.**

### Message-level example (16ch, NQ1 12-bit, mid-scale)

- AIN payload: `sint32` ≈ 16 × 2 = **32 B** vs `sfixed32` = 16 × 4 = **64 B**.
- Plus fixed overhead (delimiter + ts tag/val + packed tag/len + DIO) ≈ 8–10 B.
- Whole message ≈ **40 B → 74 B (≈ +85 %)**. On a bandwidth-bound path this cuts the
  achievable sample rate proportionally.

## 3. Encode CPU

`sfixed32` genuinely simplifies the inner loop:

- **`sint32` today:** two `pb_encode_svarint` passes (sizing + writing), each a
  zigzag + a branch-per-byte varint loop (1–4 iterations/sample).
- **`sfixed32`:** packed length is analytic (`4 × ainCount`, no sizing pass) and each
  element is a fixed 4-byte little-endian store (PIC32MZ is little-endian; no byteswap).

That removes roughly one full pass plus the per-byte branching — on the order of the
issue's estimated 15–30 → ~3 cycles/sample. **But**:

- The measured whole-message encode is ~10–20 µs (`NanoPB_Encoder.c:1172`); the AIN
  varint work is a fraction of that.
- Per the CLAUDE.md throughput characterization, **below the transport ceiling "the ADC
  [and encoder path] is ~free"** — the enforced caps are **transport/wire-rate bound**,
  not encoder-CPU bound, on USB (below the F3 basis), WiFi, and SD. Saving encoder
  cycles there does not raise the cap; it just idles more.
- Where the cap *is* partly ISR/transport bound (USB 1×T1 @ 15 000), the saving is not
  the binding term either.

So the CPU win is real but **does not move the wall**, while the wire cost (§2)
**directly lowers** every bandwidth-bound cap.

## 4. RAM impact (contra the issue's #391 concern)

The issue worried `DaqifiOutMessage_size` would grow and collide with the ~500 B static
headroom (#391). **It does not — and the concern is inverted:**

- nanopb maps **both** `sint32` and `sfixed32` to a C `int32_t` field
  (`sfixed32 → PB_LTYPE_FIXED32`, storage `int32_t`; `pb.h:243,864`). The generated
  struct stays `int32_t analog_in_data[16]` — so **`sizeof(DaqifiOutMessage)` is
  unchanged**. No BSS / no static-buffer growth. #391's headroom is not touched.
- The only constant that changes is the compile-time **max-encoded-size** macro
  `DaqifiOutMessage_size` (currently 2008). nanopb's per-element worst case is **5 B for
  a packed `sint32` varint** but **exactly 4 B for `sfixed32`**, so the macro would
  **shrink** by `16 × (5−4) = 16 B`. Likewise `STREAMING_MSG_MAX_SIZE`
  (`NanoPB_Encoder.c:44`) uses `PB_AIN_MAX_COUNT × PB_VARINT32_MAX(5)` and would drop to
  `×4`.

RAM verdict: **neutral-to-favorable** — not a blocker, and not a reason to switch either.

## 5. Client compatibility (must flag)

This is a **wire-incompatible, breaking** proto change. Field 2's wire type changes from
`0` (varint) to `5` (fixed32); existing parsers (`daqifi-core` .NET,
`daqifi-python-core`, `daqifi-java-api`, node/labview/arduino) would mis-decode field 2.
Rolling it out would require a coordinated firmware + all-clients release (or a new field
number / version gate). That coordination cost is only justified by a clear firmware win
— which §2–§4 show does not exist.

## 6. Recommendation

| Dimension | Verdict |
|---|---|
| Wire size (avg) | **`sint32` wins** (12-bit ×2, 18-bit +33 %, 24-bit ~tie) |
| Wire size (worst case) | tie at 24-bit; `sint32` wins at 12/18-bit — `sfixed32` **never** wins |
| Encode CPU | `sfixed32` modestly cheaper, but not the binding term (transport-bound) |
| RAM | neutral (struct `sizeof` unchanged; max-size macro shrinks 16 B) |
| Client impact | breaking, coordinated rollout required |

**Keep `sint32`.** Do not change `DaqifiOutMessage.proto` field 2.

### If encoder CPU ever becomes the wall

Pursue the win **without** the wire cost by removing the double `pb_encode_svarint`
sizing pass (§1) — e.g. size while buffering, or write-then-patch the packed length —
which is the #389 (memcpy/pass-elimination) direction and requires **no** wire-format
change. Re-open this evaluation only if #388 profiling shows the AIN varint loop is a
material, cap-binding fraction of encode time on a transport that still has headroom.

## Sources

- `firmware/src/services/DaqifiPB/DaqifiOutMessage.proto:10` (field def)
- `firmware/src/services/DaqifiPB/DaqifiOutMessage.pb.h:37,209,283` (C storage, LTYPE, size macro)
- `firmware/src/services/DaqifiPB/NanoPB_Encoder.c:44,1196-1238,1172` (streaming encode + double-pass + measured cost)
- `firmware/src/state/data/AInSample.h:94` (`Values[]` is `uint32_t`)
- `firmware/src/libraries/nanopb/pb.h:243,864` (`sfixed32 → FIXED32 → int32_t`)
- CLAUDE.md "Streaming Frequency Capping" / "ADC cost" (transport-bound, encoder ~free below ceiling)
- Prerequisite tickets #388 (profiling), #389 (memcpy elimination), #390 (DMA pipelining) — **not yet landed** as of this writing; profiling-dependent conclusions are marked accordingly.
