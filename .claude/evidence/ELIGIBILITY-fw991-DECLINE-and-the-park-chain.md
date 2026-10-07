# fw#991 — eligibility: **DECLINE**. I parked it, dispositioned its findings, and parked its blocker.

Produced 2026-10-07. ⛔ **This REVERSES what I reported mid-read.** I told the coordinator my fw#991
eligibility was "CLEAN so far" on the strength of my own records. **It is not.** Reading the row's
park convicted me.

## ⭐⭐ THE METHOD FINDING, which matters more than the decline

This morning on fw#1110 the rule was: *own records are PRIMARY, the row is the dating channel; a bare
"0 hits on the row" reads as unclearable rather than clear.* That was correct **there** — my records
convicted me and the row's authorship field carried no lane signal.

**On fw#991 it ran exactly backwards.** My records returned **zero authorship hits** on 991 (67 raw
hits, all artifact-health tables plus one line recording *conv-ts's* finding about a bws 503 — not my
work). **The ROW convicted me, in its own words:**

> `## Lane nq-b, worker level: PARKED at the round cap — and one finding above is a FALSE POSITIVE`
> *"Reviewing my own fire's disposition comment before acting on it… **Declining #3**… Confirmed —
> finding #5 is real"* — 2026-09-11T06:43:57Z, id 5630562778

> **So neither channel can CLEAR. Both can CONVICT. The direction of the asymmetry is not fixed —
> it reverses per row.** Run both every time and act on whichever FIRES; never let one channel's zero
> stand as a clearance because the *other* channel happened to be decisive last time.

The reason my records missed it: **my ledger writes bare `#NNN` with no repo prefix**, the exact
defect my own publication footprint recorded (124 ambiguous numbers). A `991` search drowns in
artifact tables. I nearly proceeded on that zero.

## My involvement — three kinds, plus a fourth on the blocker

1. **I PARKED fw#991** at the round cap (disposition-authorship, kind seven).
2. **I authored findings dispositions**: declined #3 as a false positive after re-deriving it,
   confirmed #5 as real against `4fb929076`, and left #4 as my fire had placed it.
3. **I briefed the fire** whose disposition comment I was reviewing.
4. ⛔⛔ **I ALSO PARKED ITS BLOCKER.** ts#306's park reads
   *"## PARKED — what would unpark this PR (**lane nq-b**, 2026-09-14)"*.

## ⛔ THE PARK CHAIN — and its terminus needs MY bench

fw#991's park, quoted at source (4 items, in order), with each re-derived today:

| # | item | status today |
|---|---|---|
| 1 | companion leak predicate to include `EncoderDroppedSamples` | ⛔ **BLOCKED on ts#306**, which is `OPEN` **and itself `parked`** |
| 2 | wiki field table updated and pushed | ⚠ **UNVERIFIED** — conv-fw explicitly did not check it; neither have I |
| 3 | conflict against `main` resolved | ✅ **DONE** — nq-c's 2026-10-01 refresh, `590ecfd81` → `a66aadb2b`, DIRTY → MERGEABLE |
| 4 | fresh adversarial audit at the resulting head | ❌ **NOT DONE** — head is now `a66aadb2b` |

**ts#306's park** then requires the operator to pick an arm, and arm 1 is:

> *"one commit fixing the confirmed findings, **a bench run on board `7E2873046200E891`**, then a
> fresh full audit at that head, with codex capacity back so the blind leg runs."*

`7E2873046200E891` is **lane nq-b's board**. So the chain is:

> **fw#991 → ts#306 → an operator arm choice → arm 1 needs nq-b's bench + codex capacity.**

Every link except the last is a park I wrote. **I cannot assess any of it, and the one thing I could
contribute — the bench run — is gated behind an operator decision, not behind my availability.**

### ⭐ Second instance of the documented-from-neither-end shape, in a new direction

ts#306 names `991` **exactly once** across 33 comments, in a 2026-09-12 comment about the leak
predicate — **not inside its park.** So fw#991's park cites ts#306, and ts#306's park says nothing
about blocking fw#991. This is a **half-documented** dependency: discoverable from the firmware end,
invisible from the suite end. The prior instance (ts#330 ↔ fw#996) was documented from *neither* end.
**Reading ts#306's park alone tells you nothing about what unparks if you land it.**

## ⛔ AND fw#991 IS IN NONE OF THE THREE OPERATOR RULINGS' SCOPES

Checked against the enumerated lists verbatim:

```
ruling 1 (cap exception)     976 1024 1027 1094 1106 1110 1115 1130
ruling 2 (merge + hw ticket) 1099 1129  (+ by dependency 704 1013 1092)
ruling 3 (get tests working) 1008 1020 1055 1137
991 -> appears in NONE
```

This matters under the scope discipline that validated fw#976's grant: a grant reaches a row because
the **question enumerated it**. fw#991 was queued to me as the next row, but **no ruling names it**,
it carries a live `parked` label, and **3 of its 4 park items are outstanding.** The throughput
mandate (*"if there is a big reason to park, we can park"*) does not by itself unpark a row whose
condition is unmet — and here the condition's blocker is another parked row.

## The 4 unresolved threads, for whoever takes it

All authored `qodo-code-review`; the gate reads BLOCKED because
`required_conversation_resolution` is enabled (and note separately that it does **not** prevent a
merge on this repo):

| path:line | severity | opening phrase | outdated |
|---|---|---|---|
| `docs/STREAMING_AND_ADC.md:177` | **High** | *"Protocol-buffer loss passes as clean"* | no |
| `firmware/src/services/JSON_Encoder.c:1125` | Medium | *"Operators see conflicting los[s]"* | no |
| `firmware/src/services/streaming.c:2438` | Medium | *"Scpi loss documentation falls…"* | no |
| `tests/host/Makefile` | Medium | *"Firmware ci owns a durable re…"* | **yes** |

**I read these as metadata only and did not disposition them** — I am the lane that dispositioned
this row's earlier findings, so that is not mine either.

## Process notes

- **The 503 guard fired correctly and was needed.** My own records held conv-ts's finding that a bws
  503 strips `GH_TOKEN` and returns **ZERO** comments on *this exact row* before a retry returned 41.
  I guarded with a retry loop plus a second independent channel (GraphQL `reviewThreads`). First
  attempt returned **41** — matching the known-good figure. A zero here would have read as "no
  threads" against a gate whose `isResolved` **fails OPEN**.
- **`590ecfd81` is not a dangling SHA.** It is fw#991's **previous head**, superseded by the refresh,
  and it is the live checked-out HEAD of two worktrees. A park pinned to a superseded head is
  **stale, not dangling** — and stale is the more dangerous reading because it looks like compliance.
- **Worktree attribution by SHA pairing.** My footprint listed `audit-991n` as UNDETERMINED (0
  ledger, 0 evidence, 83 subagent files — a column that cannot distinguish *holding* a tree from
  *seeing* it in a `worktree list` dump). It sits at the **identical detached SHA** as `nq-c-991`,
  with newest-file times ~2 h apart, and a third nq-c tree is on the same row. **`audit-991n` is
  nq-c's.** Pairing an undetermined tree's HEAD against lane-named trees resolves ownership with no
  ledger and no transcripts. All three were idle (clean, 0 holders, nothing in 120 min) and **I
  touched none of them.**
