# fw#991 finding 8 was MINE and wrong in both halves — my decline is DEEPENED, not reopened

Produced 2026-10-13. ⚠ My last work on this row was 2026-10-07, so I re-read rather than relied on
memory.

## The decline stands. A row changing does not reopen eligibility.

I declined fw#991 on 2026-10-07 (`36d747b86`) on four grounds: I parked the row (`5630562778`), I
parked its blocker ts#306, I dispositioned findings #3/#4/#5, and I briefed the fire. **Item 2 being
satisfied removes a blocker from the row; it removes nothing from my involvement.**

**And it adds a fifth, heavier ground.** The four-item unpark condition — comment **`5644810065`,
2026-09-12T08:43:04Z** — is **mine**, by its own first-person text:

> *"**I am parking it here** rather than continuing to patch…"* · *"**I had already applied a fix** for
> this before testing it, and **reverted that fix** when the meas[urement]…"* · *"…**I would fix
> first**"*

So I wrote item 2, and item 2 cites **finding 8**, and finding 8 is in that same comment.

## ⛔⛔ Finding 8 is false in BOTH halves — verified independently, with controls

Finding 8 claims the wiki *"still documents the old `EncoderDroppedSamples` semantics."*

```
wiki 2538017   authored 2026-09-11T00:33:51-06:00  ==  2026-09-11T06:33:51Z
finding 8 /  item 2     2026-09-12T08:43:04Z        ->  ~26.2 h LATER

EncoderDroppedSamples rows at 2538017^ (parent) : 0     <- the field was UNDOCUMENTED
EncoderDroppedSamples rows at 2538017           : 1
controls at that SAME parent:  ScanStaleDropped 2  ✅   zzqqNoSuchField 0  ✅
```

1. **It asked for an artefact that already existed** — 26 hours earlier, and its row already read
   *"Since #970 it is booked by the encoder that destroyed the sample
   (`Streaming_ReportEncoderSampleLoss()`), not inferred from a zero-byte return."*
2. **It described a prior state that never existed** — there was **no row at all** at the parent. The
   field was **undocumented**, not **mis-documented**.

⭐ **The positive control is what makes the zero sound:** `ScanStaleDropped` reads **2** at the same
parent revision, so the table existed and the grep worked there. A zero without that control would be
the wrong-path error I made on 2026-10-09.

> **So I authored a park item citing a finding of mine that was wrong in both halves, and the row
> carried a false independent merge-blocker on it for a month.**

## ⭐⭐ The handoff worked — and it was right for a stronger reason than I knew

On 2026-10-07 I found the 26-hour gap and **refused to conclude from it**:

> *"I am NOT declaring item 2 satisfied — it cites 'finding 8', and I have not read finding 8 to
> confirm the wiki row is the artefact that finding demanded. **That is a judgement on a park item I
> wrote, so it is yours.**"*

I disclosed that the **item** was mine. **I did not know the FINDING was mine** — I hadn't read it,
which was the stated reason for withholding. So:

> ⭐ **I withheld a judgement on grounds that turned out to be stronger than I had.** The person who
> could not judge the finding was the person who wrote it, and neither of us knew that at the time.
> **A conflict disclosed at the granularity you can see can still understate itself** — and the remedy
> is not more caution, it is the same split: surface the fact, withhold the judgement, let a reader
> without the stake close it.

The coordinator read finding 8 and closed item 2, disclosing that their own later wiki commit
`e868a1e` expanded the row but that the thing which **satisfies** the item is `2538017`, a month
earlier, not their edit. That is the correct attribution and the right disclosure.

## What still blocks fw#991 — recorded, not assessed

- **Item 1** — the companion leak predicate. **Open, and its same-day amendment records it as
  CONTENDED, not merely undone.** ⚠ Anyone touching the predicate should read that amendment first.
  Note also that the predicate's *stated purpose* may not be achievable: the test itself prints
  *"leaked still cannot see the gap this test exists to cover"*, which I filed on 2026-10-07.
- **Item 2** — ✅ satisfied (above).
- **Item 3** — base refresh landed 2026-10-01.
- **Item 4** — fresh audit wanted; the gate is BLOCK at a head that has since moved, so **void
  regardless**.
- **`needs-bench`** — earlier bench evidence stands: falsifiers 1-4 measured, none fire, **5 and 6
  explicitly not reached**. That is the gap if anyone takes the bench half. **Not me** — I am
  conflicted on this row on five grounds.

## Board and tooling notes, carried forward unverified by me

- My board `7E2873046200E891` (COM8, PICkit 5 `020026703RYN079002`) has **factory cal = identity, i.e.
  UNCALIBRATED** — not for accuracy work. A `*IDN?` serial of `0` means *this firmware does not read
  DEVSN*, never that the board lacks identity. **COM9 is that PICkit's own virtual UART** — registered
  to this lane, not a Nyquist, never answers `*IDN?`.
- `batch.sh` fixed at claude-skills `2c11ba7`: it previously **died before sending a byte** on a
  COM-port box because `WSLENV` was never populated, and `CMD? arg` was misparsed so a parameterised
  query lost its argument. Verify is now keyed on a **leading `?`**. **I have not re-tested it.**
