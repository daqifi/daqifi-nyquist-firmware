# OPERATOR DECISION PACKET — ts#306's arm choice (terminus of the fw#991 chain)

**This is a READING, not a ranking. The arms are transcribed verbatim, the facts are verified at
source, and I have deliberately NOT ranked or recommended.**

⭐ **How the "verbatim" claim is backed, because the claim is the packet's whole value.** Every
sentence of the park comment is checked against the GitHub API body by script (normalised for
markdown and whitespace only): **7/7 match, 0 mismatches.** The checker is proven live by a negative
control — a deliberately corrupted copy (`is chosen` → `was chosen`) is caught. **My first draft
failed this check twice**: I had dropped the word *"is"* from one sentence and omitted another
sentence entirely. Both are fixed above. A transcription trusted by eye would have shipped both.

⚠ **Conflict disclosure, up front.** I (lane nq-b) **wrote ts#306's park** and **wrote fw#991's
park**. I am conflicted on both rows and cannot assess either. A verbatim transcription adds no new
position, which is why this packet is safe for me to produce — but **every evaluative sentence here
would be out of bounds, and there are none by design.** The choice between arms is the coordinator's
to frame and the operator's to make.

Produced 2026-10-07. Repo: `daqifi/daqifi-python-test-suite`.

---

## 1. The decision, as the park states it — VERBATIM

From `issuecomment-5673079021`, 2026-09-15T01:03:30Z, 1,074 bytes, authored by lane nq-b. **The
comment in full — every sentence, mechanically verified against the API body:**

> ## PARKED — what would unpark this PR (lane nq-b, 2026-09-14)
>
> This PR stays **parked** and does not count toward the lane's open-PR cap.
>
> **State:** the last full audit, at `bb1e518c57b1d0494538b98b54fa0d62d11666e5`, returned **BLOCK**
> with 3 confirmed findings in this PR's own new code (see the comment above). The Qodo cycle is past
> its 5-round cap, and the last two rounds were operator-directed exceptions, so the lane will not
> patch it again on its own judgement.
>
> **It unparks when the operator picks one of these:**
> 1. **Authorise one more round:** one commit fixing the confirmed findings, a bench run on board
>    `7E2873046200E891` (tightening the budget to the absolute term has not been shown to hold against
>    real window-edge truncation on hardware), then a fresh full audit at that head, with codex
>    capacity back so the blind leg runs. Merge on PASS.
> 2. **Close and re-file:** close this PR and re-file the #164 companion test as a new ticket, with
>    the three confirmed findings as acceptance criteria.
>
> Until one of those is chosen, no loop fire touches this PR.

**Who decides:** the operator. **Two arms only** — there is no third.

## 2. Facts on the ground, verified at source today

| fact | value |
|---|---|
| state | `OPEN`, `MERGEABLE` / `CLEAN`, label **`parked`** |
| branch | `test/164-json-encoder-correctness` |
| live head | `bb1e518c57b1d0494538b98b54fa0d62d11666e5` |
| park-cited audited SHA | `bb1e518c57b1d0494538b98b54fa0d62d11666e5` |

> ⭐ **The head has NOT moved since the audit. The BLOCK is LIVE, not stale** — unusual for this
> fleet and it means the verdict governs as written. No head-drift caveat applies.

### What the BLOCK actually consists of — it stacks THREE independent reasons

From the audit comment the park points at (`issuecomment-5658349885`, 2026-09-14T02:55:55Z):

```
gate / finalGate            BLOCK / true
gateReason                  the blind (un-steered) leg was requested and did not run
blindLegRequested/Ran       true / FALSE      blindLegMissingFor [306]
codexFellBack/codexErrors   true / "Selected model is at capacity" (unavailable: false)
noProvenance                TRUE
auditorLegsOk               2 of 3  (codex -> sonnet -> fable; codex refused)
raw / refuted / confirmed   3 / 0 / 3
steeringSuppressed          []  -- "proves nothing", there was no un-steered pass to compare
```

Quoting the audit's own conclusion: *"this BLOCK stacks three reasons: **no blind leg**, **no
provenance** (either alone makes a PASS impossible), **and three skeptic-confirmed findings**. The
findings are usable; the verdict could not have been PASS regardless."*

**Both test files are new in this PR** (verified in the audit by `git cat-file -e <base>:<file>`
failing for both), so none of the three findings is pre-existing residue.

### ⛔ A precondition inside arm 1 that cannot be cheaply verified

Arm 1 requires *"codex capacity back so the blind leg runs"*. The BLOCK's own `codexErrors` reads
**`"Selected model is at capacity"`** — *refused at start, not exhausted* (`unavailable: false`). So
**arm 1's precondition is the same condition that caused one of the three BLOCK reasons.** Whether
capacity has returned is only testable by running an audit, which spends the round arm 1 would be
authorising. **Stated as a fact about the arm, not as an argument about it.**

## 3. What it unblocks — the chain, each link verified

> **fw#991 → ts#306 → this arm choice**

fw#991's park (4 items, verbatim-sourced): item **1** is *"The companion-suite leak predicate updated
to include `EncoderDroppedSamples` (finding 1). Without it the nightly gate cannot see the loss this
PR exists to report."* Its same-day amendment narrows it to *"whoever lands [test-suite] #306 first
owns the fix."*

Current status of fw#991's four items, each re-derived today:

| # | item | status |
|---|---|---|
| 1 | leak predicate includes `EncoderDroppedSamples` | **the work EXISTS, unmerged** — see below |
| 2 | wiki field table updated and pushed | **UNVERIFIED by anyone** (conv-fw explicitly skipped; so did I) |
| 3 | conflict against `main` resolved | ✅ done — nq-c's 2026-10-01 refresh, `590ecfd81` → `a66aadb2b` |
| 4 | fresh adversarial audit at the resulting head | ❌ not done; head is `a66aadb2b` |

### ⭐⭐ Item 1's work is already written and sitting behind this park

`077c694bd` — *"fix(harness): leaked must count EncoderDroppedSamples (ts#306)"*, 2026-09-12 — is
**on ts#306's branch**:

```
test_harness.py                   +37/-1
test_164_json_encoder_no_loss.py  +17/ -9
verify_306_leak_predicate.py     +133/ -0   (new)
```

> **So fw#991 item 1 is not waiting for someone to DO the work. The work is done and unmerged,
> blocked by this park.** That reframes the chain from "work needed" to "a merge held by a decision".

### ⛔ But item 1's stated PURPOSE may not be achieved by item 1's stated ACTION

Reading fw#991's item 1 and ts#306's own patch text together — neither shows this alone:

- fw#991 item 1's rationale: *"Without it the nightly gate cannot see the loss this PR exists to
  report."*
- ts#306's patch, in its own emitted message and docstring: the firmware *"increments
  `EncoderDroppedSamples` in the same `if (encoded == 0)` branch as … partial encode, so **no counter
  old or new fires on it**. `m.leaked` reads False"*, and *"(now includes `EncoderDroppedSamples`,
  ts#306) — that counter only fires on mid-call truncation, so **`leaked` still cannot see the
  gap**"*.

**The predicate now includes the counter; the patch says the gate still cannot see the gap.** Both
statements are from primary sources and I am not reconciling them — **flagging that landing ts#306
may satisfy fw#991 item 1 as WORDED without achieving the effect item 1 gives as its reason.** That
bears on both arms and on whether item 1 should be reworded, which is a decision and not mine.

## 4. What is NOT in this packet, deliberately

- **No ranking and no recommendation.** Not an omission — ranking rows is the judgement half that
  disqualifies the lane doing it, and I am already conflicted on both rows in this chain.
- **The three findings' content is summarised only as "3 confirmed, 0 refuted"**, with the audit
  comment cited. I quote the gate fields rather than re-characterising the findings, because I wrote
  the park that cites them.
- **Nothing about whether the park was correctly judged.** Not re-litigated; this records what it
  says and what is true today.
- **Resource availability as a fact, not an argument:** board `7E2873046200E891` is lane nq-b's, and
  this lane is currently parked with no assignable firmware row, so the bench in arm 1 is available.
  **Availability is not a reason to choose arm 1.**
