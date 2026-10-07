# fw#991 park item 2 — the wiki work was pushed the DAY BEFORE the item was written

**A READING, not a disposition.** I wrote fw#991's park, so whether this satisfies item 2 is not my
call — I report the facts and the dates. Produced 2026-10-07. Nobody had checked this item: conv-fw
explicitly skipped it (*"Item 2 … not independently re-verified by this comment"*) and so did I.

## The item, verbatim

From fw#991's park (`issuecomment-5644810065`, 2026-09-12T08:43:04Z):

> ### What this PR needs, in order
> …
> 2. The wiki field table updated and pushed (finding 8).

## The facts

Wiki is a separate repo (`daqifi-nyquist-firmware.wiki.git`), cloned read-only, 241 commits,
head `7bbf2299` (2026-09-21).

| question | answer |
|---|---|
| Is `EncoderDroppedSamples` in `01-SCPI-Interface.md`? | **YES — 1 hit, line 837**, inside the field table |
| Anywhere else in the wiki? | no — 1 hit total across all pages |
| Which commit added it? | **`2538017`** — *"docs(scpi): document EncoderDroppedSamples' #970 semantics"* |
| When? | **2026-09-11 00:33:51 -0600** |
| When was item 2 written? | **2026-09-12T08:43:04Z** |

> ⛔ **The wiki entry predates the item asking for it by about a day.**

Controls: positive `ScanStaleDropped` → 3 hits (extracted from the file, non-empty asserted);
negative `zzqq_no_such_field_zzqq` → 0 hits. Both behaved, so the 1-hit result is interpretable.

### The entry's content ties it to this exact PR

Line 837 reads, in part:

> *"Samples an encoder consumed from a queue and then failed to emit (a real loss). **Since #970** it
> is booked by the encoder that destroyed the sample (`Streaming_ReportEncoderSampleLoss()`), **not
> inferred from a zero-byte return** — so it no longer counts still-queued samples that a later call
> re-encodes successfully."*

`#970` is fw#991's own ticket (its title: *fix(streaming): encoder consume-accounting must not book a
sample count on a zero-byte return (#970)*), and *"not inferred from a zero-byte return"* is that
PR's exact change. `git log -S 'Streaming_ReportEncoderSampleLoss'` returns the same single commit,
so the #970 clause and the row arrived together.

## What I am NOT concluding

- **Whether item 2 is satisfied.** The item cites *"finding 8"*, and I have not read finding 8 to
  confirm the wiki row is the specific artefact it demanded. The row exists, documents #970's
  semantics, and is pushed; whether that discharges finding 8 is a judgement on a park item I wrote.
- **Whether the park should be amended.** Also not mine.

## ⭐ The shape, because it is the third instance this fleet has hit

A park item asking for work that was **already complete when the park was written**, left unverified
for ~26 days because the two lanes that looked both (correctly) declined to assess it. Same family as
*ts#405's decision made 7 days before the label was removed* and *a park should RETIRE its spent
conditions*. **The cost here was not the work — it was that nobody could cheaply check, so an item
with no remaining work kept counting as a blocker on a row with a throughput mandate.**

**Cheap preventative:** a park item that names an artefact in another repo should carry the artefact's
expected location, so verifying it is a grep rather than an investigation. Item 2 said *"the wiki
field table"*; `01-SCPI-Interface.md` line 837 took a clone plus two greps to find, which is more
than anyone triaging 30 PRs will spend.

## By-product, reported as a fact and not chased

Same-family loss counters in `01-SCPI-Interface.md`:

```
QueueDroppedSamples      5      ScanStaleDropped   3
EncoderDroppedSamples    1      SdDroppedBytes     4
DioDroppedSamples        0  <-- zero             WifiDroppedBytes   4
```

`DioDroppedSamples` has **no wiki mention**, while the firmware's loss snapshot carries
`dioDroppedSamplesSteady` (seen in fw#976's source read). **I have not established whether
`DioDroppedSamples` is an emitted `SYST:STR:STATS?` field** — if it is, it is undocumented, and the
`scpi_wiki_sync` CI gate would not catch it because that gate compares *registered commands*, not
*response fields*. Flagged for someone with no stake in it; this lane has not verified the premise.
