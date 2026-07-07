# Batched PB Streaming with Per-Sample-Set Timestamps (`analog_in_data_ts`)

**Issue:** [#238](https://github.com/daqifi/daqifi-nyquist-firmware/issues/238) — perf: batch PB
streaming with per-sample-set timestamps via `analog_in_data_ts`
**Related:** #237 (fast PB encoder, merged `c4a19596`), #391 (bump `analog_in_data`
max_count — RAM-blocked), #235 (PB encode optimization), #62 (self-describing PB), #116
(streaming throughput)
**Status:** Analysis only. No firmware change in this PR. Doc-only.
**Date:** 2026-07

Evidence tags used below follow the repo's debugging-discipline convention:
**V**erified-from-source, **E**mpirical, **I**nference, **X**external, **N**our-prior-notes.

---

## 1. Executive summary

Today the fast PB streaming encoder (`Nanopb_EncodeStreamingFast`, added in #237) emits
**one length-delimited `DaqifiOutMessage` per sample-set** — each carries its own
`msg_time_stamp` (field 1) and its channel values in `analog_in_data` (field 2). The
`analog_in_data_ts` field (field 4, `repeated uint32`, `max_count:16`) is defined in the
proto but **never populated during streaming** — the slow path explicitly zeroes it
(`NanoPB_Encoder.c:457`, `message.analog_in_data_ts_count = 0`) and the fast path never
touches it. **V**

**#238 proposes** packing *B* consecutive sample-sets into **one** message: one base
`msg_time_stamp`, one `analog_in_data_ts` entry per set, and all *B×N* channel values
concatenated in `analog_in_data`. This amortizes per-message framing (the tag/length/prefix
bytes) and — more importantly — the per-message *encoder-entry* and *transport-write* rate.

**Bottom line recommendation:** the win is real but **channel-count-regressive** — large in
byte terms only at low channel counts (where absolute throughput is already highest), and the
more valuable effect (ceiling lift, e.g. #237's reported 16ch 5→7 kHz) comes from reduced
encoder-entry / USB-transfer rate, not raw bytes. Crucially, unlike #391 this can be done
**entirely inside the fast encoder with near-zero new static RAM** (it writes wire bytes
directly and never touches the 65-field nanopb struct that #391 would have grown by ~1.8 KB).
The gating risks are **not** RAM — they are (a) a **wire-format backward-incompatibility that
corrupts silently** on un-updated clients, and (b) a **variable-`validMask` chunking hazard**
that the ticket's "client chunks by channel count" sketch does not address. Recommend:
implement in the fast encoder, **opt-in via an extended SCPI parameter (default off)**, bound
the batch into the existing encoder buffer, and ship client updates in lockstep. Given the
modest high-channel benefit, this is a **medium-priority throughput refinement**, not a
must-ship.

---

## 2. Current state (verified from source)

### 2.1 Proto / generated struct

`DaqifiOutMessage.proto`: **V**
```proto
uint32          msg_time_stamp    = 1;   // base timestamp (streaming trigger counter)
repeated sint32 analog_in_data    = 2;   // channel values (zigzag+varint packed)
repeated float  analog_in_data_float = 3;
repeated uint32 analog_in_data_ts = 4;   // "timestamp offset ... added to msg_time_stamp"
```

`DaqifiOutMessage.options`: `analog_in_data`, `analog_in_data_float`, `analog_in_data_ts`,
`digital_data_ts` all `max_count:16`. **V** So the generated C struct
(`DaqifiOutMessage.pb.h:37–41`) has `int32_t analog_in_data[16]`, `uint32_t
analog_in_data_ts[16]`, etc. The `analog_in_data_ts` array **already exists** in the struct at
count 16 — the field is defined and decodable, just unused on the wire during streaming. **V**

### 2.2 The fast encoder (the relevant code path)

`Nanopb_EncodeStreamingFast()` (`NanoPB_Encoder.c:1340`) does **not** use the
`DaqifiOutMessage` struct at all — it writes protobuf wire bytes directly with nanopb's
low-level primitives (`pb_encode_tag`, `pb_encode_varint`, `pb_encode_svarint`,
`pb_encode_string`), two passes (size then write), touching only 2–4 fields instead of 65.
This is the ~15× speedup over the struct-based path (248 µs → ~10–20 µs per encode). **V/N**

Per-sample-set emission today (`NanoPB_Encoder.c:1391–1461`): **V**
```
while (queue not empty && buffer has >= STREAMING_MSG_MAX_SIZE room):
    pop one AInPublicSampleList_t
    collect valid channels into int32_t values[MAX_AIN_PUBLIC_CHANNELS]   // 16*4 = 64 B stack
    encode_streaming_msg_delimited(... timestamp, values, count ...)      // one delimited msg
```
`STREAMING_MSG_MAX_SIZE` (`:44`) is sized for **one** sample-set: len-prefix + ts field + AIN
tag/packed-len + `16 × PB_VARINT32_MAX` values + DIO. **V** The batch flow-control already
exists in spirit — the loop drains the whole sample queue per encoder wake, just as *separate*
messages.

### 2.3 Framing overhead being paid per sample-set

Each delimited message currently costs, beyond the channel payload: **V/I**
- length-delimited prefix varint: 1–2 B
- field 1 tag (`0x08`) + `msg_time_stamp` varint: 1 + up to 5 B (the timestamp is a
  free-running counter → typically 3–5 B mid-session)
- field 2 tag (`0x12`) + packed-length varint: 1 + 1–2 B
- (+ DIO fields on the first message only)

So **~7–10 fixed framing bytes per sample-set**, independent of channel count.

---

## 3. Proposed batched wire format (#238)

For a batch of *B* sample-sets, each with the session's *N* enabled channels:
```
msg_time_stamp   : T0                            (base = first set's timestamp)
analog_in_data_ts: [d0, d1, ... d(B-1)]          (one entry per set; offset from T0, or absolute)
analog_in_data   : [set0 ch0..chN-1, set1 ch0..chN-1, ... ]   (B*N concatenated values)
```
Client reconstructs: set *k* spans `analog_in_data[k*N : (k+1)*N]`, timestamp
`T0 + analog_in_data_ts[k]` (or `analog_in_data_ts[k]` directly if absolute). **X** — this is
a valid, wire-compatible use of an existing proto field; the `.proto` and on-wire tag numbers
are unchanged.

**Design choice — offset vs absolute in `analog_in_data_ts`:** the proto comment says
"offset ... added to `msg_time_stamp`". Offsets are smaller varints (inter-tick deltas are
small: e.g. at 10 kHz the counter advances a fixed small step per tick), so offsets both honor
the documented semantics *and* minimize bytes. Recommend **offsets**. Note `analog_in_data_ts[0]`
is then always 0 (redundant with `msg_time_stamp`); it can be omitted (start the array at set 1)
or kept for uniformity — a client-contract detail to pin down.

---

## 4. Wire-size benefit

### 4.1 First-principles estimate (NQ1, 12-bit, fullscale worst case) — **I**

Fullscale value `4095` → `zigzag=8190` → 2-byte varint. Inter-tick timestamp offset → 1–2 B.

**1 channel:**
| | single-set (×8 messages) | batched B=8 (1 message) |
|---|---|---|
| len prefixes | 8 × 1 = 8 | 1 |
| base ts | 8 × (1+~4) = 40 | 1 + ~4 = 5 |
| AIN tag+len | 8 × (1+1) = 16 | 1 + 1 = 2 |
| `analog_in_data_ts` | — | 1 + 1 + 7×~2 = ~16 |
| values | 8 × 2 = 16 | 8 × 2 = 16 |
| **total** | **~80 B / 8 sets = 10 B/set** | **~40 B / 8 sets = 5 B/set** |

→ **~45–50% fewer bytes/set at 1 ch.** Consistent with #237's reported **39%**. **E** (#237) / **I**

**16 channels:**
| | single-set (×4) | batched B=4 |
|---|---|---|
| framing (len+ts+tag) | 4 × ~8 = 32 | ~8 + ts-array ~6 = 14 |
| values | 4 × 32 = 128 | 128 |
| **total** | **~160 / 4 = 40 B/set** | **~142 / 4 = 35.5 B/set** |

→ **~11% fewer bytes/set at 16 ch.** Consistent with #237's reported **15%**. **E**/**I**

### 4.2 The shape of the benefit

Byte savings **fall as channel count rises**, because the fixed framing cost is a shrinking
fraction of a growing payload. The savings are largest exactly where absolute throughput is
already highest (1 ch runs at the top of every ceiling table). **This is the central caveat:
the wire-byte win is concentrated where it is least needed.**

### 4.3 Why #237 still saw a *ceiling* lift at 16ch (5→7 kHz)

The 16ch ceiling improved ~40% in #237 testing **E** (#237, per the issue) despite only ~15%
byte savings. The dominant effect there is **not** bytes on the wire — it is **fewer trips
through the encode+write pipeline per second**: one `encode_streaming_msg_delimited` +
`Streaming_WriteWithRetry` + USB transfer per *B* sets instead of per set. Each USB CDC
transfer carries its own token/ACK/handshake overhead **X**, and each encoder entry re-pays the
two-pass sizing preamble. Amortizing those is the real lever at high channel counts. **I**

> Caveat on the #237 numbers: they are from the reverted #237 batching prototype (which
> discarded per-set timestamps), so they are directional **E**, not a validated cap. Any real
> implementation must be re-characterized with `StreamingMeasurement` under the current
> (post-#541) read path and the freeze-aware caps before any cap table is touched. **N**

---

## 5. RAM cost — and why #238 sidesteps the #391 blocker

### 5.1 What blocked #391

#391 proposed bumping `analog_in_data` `max_count` 16 → 64/128 **in the nanopb options**. That
grows the generated `DaqifiOutMessage` struct arrays (`analog_in_data`,
`analog_in_data_float`, `analog_in_data_ts`, `digital_data_ts` — each `4 × max_count` bytes):
at 128, `4 × 128 × 4 = 2048 B` vs `256 B` at 16, **~+1.8 KB**, plus the encoder buffer must
grow to hold `16ch × 128 samples × ~5 B ≈ 10 KB` (exceeds the 8 KB default). **X** (#391 body)
On a firmware with ~500 B static headroom above the 8192-word min-stack (a +876 B BSS change
already broke the link once), that struct + buffer growth is the blocker. **N**

### 5.2 Why the fast encoder avoids it

The fast encoder **never instantiates `DaqifiOutMessage`** — it streams wire bytes directly.
Batching therefore does **not** require bumping the nanopb `max_count` at all: the number of
values in `analog_in_data` on the wire is limited only by the **output buffer**, not by any
compile-time struct array. **V/I** The `.options` change #391 needed is simply not on the
critical path for #238's firmware side.

### 5.3 The RAM cost that *does* remain: the length-prefix problem

Protobuf length-delimited framing writes the **inner message length before the payload**. Today
that is cheap: a per-set sizing sub-stream runs over ≤16 values, then the real write. For a
**batched** message the total length depends on all *B* sets — but the sample queue is a
**consume-on-pop FIFO**, so once a set is popped and freed
(`AInSampleList_FreeToPool`, `:1437`) it cannot be re-read for a second (write) pass. **V**
Three ways to resolve this, with their RAM costs:

| Strategy | How | RAM cost | Risk |
|---|---|---|---|
| **A. Buffer the batch** | pop *B* sets into a local `int32_t[B*N]` + `uint32_t[B]` ts, size, then encode | `B*N*4 + B*4`. At B=8,N=16 = **544 B** (stack or a small static) | low; bounded by choice of B |
| **B. Scratch-encode payload** | encode `analog_in_data` + `analog_in_data_ts` into a scratch buffer, then emit len + copy | one batched-payload worth (~`B*N*3`); a `memcpy` | low; extra copy |
| **C. Padded varint length** | reserve a fixed 5-byte non-canonical varint for the length prefix, patch in place | **~0** extra | **client risk** — non-minimal length varints are not guaranteed decodable by every PB implementation **X**; reject |

**Recommendation: Strategy A with a bounded B**, batching **into the existing encoder buffer**
(default 8 KB, already sized and auto-balanced from the streaming pool). Bound B so that
`B*N*PB_VARINT32_MAX + framing ≤ encoderBufSize`; the existing
`buffSize - bufferOffset < STREAMING_MSG_MAX_SIZE` guard generalizes to a batched
`MSG_MAX_SIZE(B)`. No new large static buffer; the 544 B scratch is trivially within budget (it
can even reuse the `values[]` stack local pattern, promoted to `values[B*N]` with B small).
**This keeps #238 comfortably clear of the RAM ceiling that blocked #391.** **I**

### 5.4 Client-side RAM (not firmware)

A **C client using nanopb** to *decode* the batched message needs its `analog_in_data`
`max_count ≥ B*N` — i.e. #391's struct growth reappears **on the client**
(`daqifi_nyquist_arduino`). The .NET (`daqifi-core`) and Python (`daqifi-python-core`) decoders
use dynamic/repeated collections with no fixed max_count, so they are unaffected by array size
(only by the parsing-logic change). This is a client-portability constraint to document, not a
firmware RAM cost.

---

## 6. Encoder complexity

Beyond the length-prefix handling (§5.3), two correctness issues that the ticket's
"client chunks `analog_in_data` by channel count" sketch under-specifies:

### 6.1 Variable `validMask` across sets — the chunking hazard

Each sample-set carries a `validMask`; the fast encoder emits **only valid channels**
(`NanoPB_Encoder.c:1419–1435`). **V** Under the post-#541 read path a Type-1 channel can miss
its `ARDY` on a given tick (`T1ArdyMisses`), leaving that set with **fewer than N** values for
one tick. **V/N** In the single-message-per-set format this is self-describing: each message's
`analog_in_data_count` stands alone. In a **batched** message with a single concatenated
`analog_in_data`, a client that chunks by a fixed *N* will **mis-align every subsequent set** in
the batch the moment one set is short — silent, cascading corruption within the message.

Mitigations (pick one, must be in the design):
1. **Pad short sets to N** (emit a sentinel / repeat last / emit 0 and rely on a companion
   valid-mask) — but there is no per-set mask field in the batched layout, so a consumer still
   can't tell a padded 0 from a real 0.
2. **Only batch sets whose `validMask == the session's enabled-channel mask`** (all-present);
   flush the current batch and emit any short set as its own single-set message. Simple, keeps
   the fixed-N chunking valid, costs a little amortization on the rare miss tick.
   **Recommended.**
3. **Add a per-set count array** (another `repeated uint32`) — more bytes, defeats some of the
   savings, and needs a new proto field. Reject.

### 6.2 DIO interleaving

Today DIO is attached to the *first* AIN message of the wake (`dioIncluded` flag, `:1441`).
**V** In a batched message there is one message per wake, so DIO naturally rides it — but the
DIO sample(s) correspond to specific ticks; batching *N* AIN sets under one DIO snapshot loses
per-tick DIO timing the same way the pre-#238 problem lost per-set AIN timing. If DIO
per-tick timing matters to any client, `digital_data_ts` (field 6, also `max_count:16`, also
unused today) is the symmetric fix — out of scope for #238 but should be noted so a future
batched-DIO change doesn't have to re-derive it.

### 6.3 Net complexity verdict

Moderate. The direct-write encoder makes the byte-layout easy; the hard parts are the
length-prefix strategy (solved, §5.3-A) and the variable-mask flush rule (solved, §6.1-2). Both
are contained, testable, and do not touch the ADC/ISR path. No FPU, no ISR context, no new
shared state (still `streaming_Task`-only, per the existing thread-safety note at
`NanoPB_Encoder.c:1163`). **V**

---

## 7. Client compatibility — the real gate

**The batched format is NOT transparently backward compatible.** An un-updated client decoding
a batched message sees one `msg_time_stamp` and a single `analog_in_data` of length *B×N*. With
no knowledge of `analog_in_data_ts`-as-per-set-timestamps it will interpret those *B×N* values
as **one sample-set of B×N channels** — for a 4-channel config, a B=3 batch reads as a bogus
12-channel set at one timestamp. This is **silent misinterpretation**, not a clean parse
failure. **I** (Every DAQiFi client keys channel count from device config, not from the
message.)

Therefore the change must be **negotiated / opt-in**, default **off**:

- Firmware ships the *mechanism* with an opinion-free default (per the repo's
  firmware=mechanism / client=policy rule): **default single-set-per-message (current
  behavior)**; batching enabled only on explicit request.
- **SCPI surface (lean-surface rule):** do **not** add a new command tree. Extend an existing
  streaming-config command with a batch parameter, e.g. a `SYST:STReam:PB:BATch <n>` setter
  (0/1 = off, ≥2 = target max sets/message) mirroring the existing `SYST:STR:*` family, **or**
  fold it into the existing streaming-format/config path. Verify the exact existing pattern in
  `SCPIInterface.c` before wiring (SCPI verification protocol) and update the wiki
  `01-SCPI-Interface.md`. Runtime-only, rejected while streaming (matches the other
  `SYST:MEM:*` setters).
- **Lockstep client updates** before batching is advertised as usable: `daqifi-core` (.NET,
  `daqifi-desktop` in-app path), `daqifi-python-core`, `daqifi-java-api`, `daqifi-node`,
  `daqifi-labview`, `daqifi_nyquist_arduino` (the last also needs the max_count bump, §5.4).
  Each must: (a) read `analog_in_data_ts`; (b) when present with entries, chunk
  `analog_in_data` by config channel count and assign `T0 + ts[k]`; (c) keep the count=0 path
  unchanged for backward compat.

A capability/handshake could let a client *discover* batch support (relates to #62
self-describing PB), but the minimum viable gate is the default-off SCPI opt-in above.

---

## 8. Recommendation

1. **Worth doing, medium priority.** The framing-amortization ceiling lift (esp. the ~40%
   16ch improvement #237 hinted at) is the valuable part; the raw byte savings are real but
   concentrated at low channel counts where throughput is already ample. Re-validate with
   `StreamingMeasurement` on current firmware before quoting any number — #237's figures
   pre-date #541 and the freeze-aware caps.
2. **Implement in the fast encoder, not via #391's max_count bump.** Direct wire-write batching
   avoids the ~1.8 KB struct growth + oversized encoder buffer that RAM-blocked #391. Firmware
   RAM cost is a bounded ~0.5 KB scratch (Strategy A) into the existing 8 KB encoder buffer —
   comfortably within the tight static headroom.
3. **Bound the batch by the encoder buffer**, generalizing the existing
   `STREAMING_MSG_MAX_SIZE` room check to `MSG_MAX_SIZE(B)`; no new large static buffer.
4. **Solve the variable-`validMask` hazard** by only batching full-mask sets and flushing a
   short set as its own single-set message (§6.1-2). This is a correctness requirement, not an
   optimization.
5. **Ship default-off, opt-in via an extended `SYST:STR:*` parameter** (lean SCPI surface),
   rejected mid-stream, wiki-documented. **Do not enable by default until clients are updated in
   lockstep** — the format silently corrupts on un-updated decoders (§7).
6. **Emit `analog_in_data_ts` as offsets from `msg_time_stamp`** (honors the proto comment,
   minimizes varint width); pin down whether `ts[0]` is omitted or a redundant 0.
7. **Behavior-changing → regression test required** (per repo policy): a
   `daqifi-python-test-suite` script that (a) enables batching, (b) captures a stream, (c)
   verifies each set's reconstructed timestamp is monotonic and matches the un-batched timeline,
   (d) verifies zero value corruption vs a Counter test pattern, and (e) confirms the default-off
   path is byte-identical to today. Use `ReliableSCPI` + `FastReader` + a PB-decoding assertion.
8. **Follow-ups, out of scope:** symmetric `digital_data_ts` batching for per-tick DIO (§6.2);
   client capability handshake (#62); arduino `max_count` bump (§5.4).

---

## 9. Relationship to #391 in one line

#391 tried to amortize framing by making one struct-based message hold more sample-sets, and hit
a firmware **RAM ceiling** (bigger nanopb struct + bigger encoder buffer). #238 achieves the same
amortization in the **struct-free fast encoder**, moving the only hard constraints off RAM and
onto **client compatibility** (silent-corruption-if-not-updated) and **encode correctness**
(variable-mask chunking) — both solvable in-scope. #238 is the "done right" version of #391's
intent.
