# SCPI reference details

> Split out of `CLAUDE.md` on 2026-09-10. That file is loaded into **every turn of
> every agent**, and at 38k tokens it was ~13% of all token spend; this material is
> reference — needed when you work in this area, not on every task. **It is
> unchanged, not summarised.** Read it in full before changing anything it
> describes, and update it here rather than re-adding it to `CLAUDE.md`.

The `SYSTem:MEMory:*` claim-path CI gate and the stream-control namespace migration. The command VERIFICATION PROTOCOL and the abbreviation rule stay in CLAUDE.md — they are needed on every task.

---

#### Stream-control namespace migration (round 3, see #311 / #324)

The streaming start/stop/query commands consolidated into the `SYST:STR:*` namespace, alongside the rest of the streaming family (`SYST:STR:FOR`, `SYST:STR:INT`, `SYST:STR:STATS?`, etc.). Legacy forms remain as aliases — both work:

| Canonical (preferred)     | Legacy alias (still works)   |
|---------------------------|------------------------------|
| `SYST:STR:START <freq>`   | `SYST:StartStreamData <freq>` |
| `SYST:STR:STOP`           | `SYST:StopStreamData`         |
| `SYST:STR:DATA?`          | `SYST:StreamData?`            |
| `SYST:USB:TRANSparent:MODE` | `SYST:USB:SetTransparentMode` |

New code, docs, and the wiki should use the canonical forms. Existing client libraries (`daqifi-python-core`, `daqifi-core` (.NET), `daqifi-java-api`, etc.) continue to work without changes; migrate them on their own schedule. The legacy aliases will not be removed without a separate, scheduled deprecation cycle (months out, with explicit comm to client maintainers).

#### `SYSTem:MEMory:*` claim-path gate

`tools/lint/scpi_claim_path.py` (CI: `.github/workflows/scpi-claim-path.yml`)
asserts **four** things, each of which the other three pass without (#863, #864).
Queries (trailing `?`) are exempt — they read and cannot corrupt a partition.

| # | asserted | what passes it if the others are the only checks |
|---|---|---|
| 1 | every registered `SYSTem:MEMory:*` **setter** reaches the claim through the shared `SCPI_MemRunClaimed` helper | a command routed off the helper — nothing else looks at where a setter goes |
| 2 | the helper **calls** `Streaming_BeginConfigChange` / `EndConfigChange`, and **looks at Begin's verdict** — neither discarding it nor storing it unread | a gutted helper — all seven still "go through the claim path", none claims anything; `(void)Begin();`, which refuses nothing; or `claim = Begin();` with the rejection arm deleted, which dispatches after a BUSY verdict |
| 3 | the helper **holds** the claim across its dispatch — the call through its function-pointer parameter falls inside the single Begin…End pair | a helper reordered to Begin → End → body: both calls present, nothing held |
| 4 | in `streaming.c`, `Begin` **sets** the claim flag inside a single critical section that also **reads** it (a test-and-set, not a plain set) and `End` **clears** it | a `Begin` that returns `STREAM_CFG_CLAIM_OK` having set nothing — 1–3 all read `SCPIInterface.c` only |

4 is why the CI trigger includes `firmware/src/services/streaming.*`. That
trigger landed in #863 and, until #864, fired on a file the checker asserted
nothing about — the gate ran and established nothing, which is the same defect
one level up. The flag name is **discovered** from `Streaming_ConfigChangeInProgress`
rather than hard-coded, so a rename is followed instead of silently disarming it.
The critical section is load-bearing because granting the claim is a
read-modify-write (test the flag, then set it), which is not atomic on PIC32MZ.

3 and 4 are positional, and position only means anything against **one opener
and one closer**: "between the first opener and the last closer" is not "inside
a region", because the gap *between two* regions satisfies it. So anything other
than exactly one `Begin`+one `End`, or exactly one `taskENTER_CRITICAL`+one
`taskEXIT_CRITICAL`, is **refused** rather than passed on a looser bound — and
that includes an *unbalanced* count (one opener, a second closer on an early
return), which is correct C refused only because deciding it needs control flow.
Of these *count* refusals, one with at least one opener **and** at least one
closer prints the counts, so the message says which case you have. A *missing*
opener or closer is refused by a different arm that names no counts, and here
the two pairs differ: the critical-section arm emits the same string whichever
side is absent, so it does not distinguish them, while the `Begin`/`End` arm
names the absent call and its verb, so it does. (Refusals for other reasons
carry their own messages and are not part of this: a set placed *outside* a
single, correctly paired section is refused without any count printed.)
Property
4's read requirement establishes that the flag is read in the same section that
sets it — **not** that the read gates the grant, which regex cannot show.

**What it does NOT establish — read this before trusting a green gate.** The
checker is textual: it has no control flow and no reachability, and that limits
**every** property, not just #864(3)'s dead-call case.

- Row 2 shows the verdict is **read**, not **branched on**. Passing it to
  another function counts, so a `LOG_I(..., Begin())` above an unconditional
  dispatch passes.
- Row 4 does **not** assert what the grant is *conditioned on*. Deleting
  `Begin`'s `if (pStreamCfg->IsEnabled || pStreamCfg->Running)` arm leaves a
  `Begin` that hands the claim out mid-session and still passes — the flag is
  set, in the section, and read. Row 4 separates a test-and-set from a plain
  set; it does not verify the test.
- Row 3 does not see branches. A helper releasing the claim only on the body's
  success path passes, and leaks `gCfgChangeBusy` — which then refuses every
  later `SYST:STR:START`.
- A dead branch satisfies rows 3 and 4 as readily as row 1: an `End` whose only
  clear sits in `if (0)`, or a `Begin` that sets then immediately clears, both
  pass.

These are **#896**, filed rather than fixed: closing them means real
reachability analysis, which is a different tool. Recorded here so a green gate
is read for what it is — a guard against honest regression, not against a
determined or half-finished refactor.

**Why a source lint and not a bench test.** `test_857`'s race arm is the only
arm that separates a real claim from a plain stream-state guard, and it can only
hammer `SYST:MEM:AUTO` — the one setter that holds the claim across work
(`PrepareStreamingBuffers`, ~1 s quiesce). The other six are a parse, a range
check and one scalar store, so there is no window to race; hammering them would
miss it every time and report a false negative. That is why test-suite #246's
proposal to rotate the hammered command was **not** adopted — see the ticket.

Run locally: `python3 tools/lint/scpi_claim_path.py` (reads `SCPIInterface.c`
**and** `streaming.c`; `--scpi` / `--streaming` override the paths). Add
`--self-test` for the device-free checks — it prints its own count, which is
why one is not repeated here — including the vacuity cases: an unreadable
command table, and a claim flag that cannot be identified, must each **fail**
rather than quietly examine nothing.
