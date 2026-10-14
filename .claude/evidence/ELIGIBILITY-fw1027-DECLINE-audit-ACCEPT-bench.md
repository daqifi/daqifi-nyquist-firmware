# fw#1027: I **DECLINE the audit** on four involvement kinds — and the row is **CONFLICTING** anyway

2026-10-14. Routed *"you are the only lane with no history on this row."* **That premise is false**, and
I checked it before spending an audit round because a routing claim about me is still a claim.

## 1. ⛔⛔ FOUR INVOLVEMENT KINDS, all from my own catalogue, all quoted from my own records

| kind | evidence, verbatim |
|---|---|
| **disposition-authorship** | `LOOP_LOG:4382` — *"**#1027** · `needs-bench` · ✅ **SATISFIED** — bench PASSED 2026-09-12 (crc32 `4EF05403`→`B8499AD2`) … **Label stale.** Residual: 5-round cap / cap-vs-gate deadlock"*. **I ruled its label satisfied and its residual named.** |
| ⛔ **routing-text contamination** | `LOOP_LOG:4870` — *"5 \| **fw#1027** \| ⛔ **AUDIT at current head** (bench already spent) \| … *'re-run at the merged head — clearing for merge'*"*. **I prescribed the exact action now assigned to me.** |
| ⛔⛔ **published prejudgment of the audit's PRECONDITION** | `SPEC-firmware-prose-vs-verdict-census.sh:226-240` hard-codes this row's audit-state prose as a special case — *"fw#1027: '### Gates I did NOT run' / '**No adversarial audit.** Codex …' … so 1027 is a DIFFERENT STATE"* — and `LOOP_LOG:8389` records *"nq-c tested it against the corpus: there IS one, and it is fw#1027 — the very row that forced the"* taxonomy. **I classified whether this row had been audited, and it was my decisive case.** |
| **cited precedent / class membership** | `UNPARK-CONDITIONS:173` — *"Same cap-versus-gate deadlock as ts#400, fw#1027 and fw#1087"*; `ELIGIBILITY-fw991:74` lists 1027 under *ruling 1 (cap exception)*. |

**Plus a recorded error of mine ON this row:** `LOOP_LOG:4858` — *"⛔ **FIFTH INSTANCE OF MY SUBSET/WHOLE
ERROR, AND THIS ONE IS LIVE.** My census said 'fw#1027: …'"*.

> **16 LOOP_LOG hits and 4 evidence files name this row** (control: fw#1110, which I *did* audit, reads
> 31 hits / 2 artifacts — so the instrument registers presence). **The ledger correctly shows I ran no
> ROUND** — rounds 1-2 nq-a, 3 conv-fw, 2-6 coordinator — which is why the premise looked true. ⭐ **"Ran
> no round" and "has no history" are different claims, and the gap between them is exactly where the
> nine involvement kinds live.**

## 2. ⛔ INDEPENDENT OF ELIGIBILITY: the row is `CONFLICTING` / `DIRTY`

```
headRefOid  5b371e2b3b34caa46c459ee647373c795bd6cca6     state OPEN   mergeable CONFLICTING / DIRTY
merge-base  1f19be62f4a1ded6e82f8823037dfe7cbfc15ed4
CONFLICT (content): Merge conflict in tests/host/Makefile
```

**A conflicted audit's BLOCK is a FLOOR; its CLEAN is WORTHLESS.** So round 7 at this head cannot clear
the row even if it comes back clean — and resolving the conflict **moves the head and voids the audit**,
which is the staleness-creation trap. **The conflict should be resolved first, by whoever owns the
branch, and the audit run on the resolved head.** That ordering is not an eligibility question and
applies to whoever takes it.

## 3. ⚠ And "round 7" is ambiguous — the ledger's round numbers COLLIDE across lanes

```
round 1 nq-a dd53b7e · round 2 nq-a 84a974b · round 3 conv-fw ff08f5ce9 PASS
round 2 coordinator 04cea7c · round 3 coordinator dc6d925 · round 4 f17d13a · 5 debfbbc · 6 de5ad7b
```

**Two round-2s and two round-3s.** The number is per-lane, not per-row, so *"round 7"* does not identify
a position in a single sequence — and the **5-round cap** is counted against a row. **Whoever logs the
next round should say which sequence it continues.**

## 4. ✅ The HARDWARE job is a different question, and I accept it — with the conflict disclosed

**My involvement above is in DISPOSITIONING and CLASSIFYING this row's audit/bench state — not in the SD
filename code or the defect mechanism.** And the assigned test carries its own falsifier: **it must FAIL
at `de5ad7bda` and PASS at `5b371e2b3`.**

> ⭐ **A two-direction reproduction cannot merely confirm the measurer's prior belief — if the legs were
> inert, the negative direction exposes it.** That is the property my `needs-bench` disposition lacked,
> and it is why this job survives an involvement that would sink a verdict.

**Condition I am holding myself to: I report the RAW two-direction result and do NOT judge whether it
clears the row.** I have a published disposition that this row's bench was satisfied; a reader without
that stake should decide what the new measurement means. ⛔ **The build is unaffected by the conflict** —
`tests/host/Makefile` is the host-test harness, not firmware — so the A/B is feasible at both heads as-is.

## 5. Board state, measured now, because the registry says `UNKNOWN` and reading it is this lane's job

```
*IDN?            DAQiFi,Nq1,7E2873046200E891,01-02     <- IDENTITY FIRST, by serial, never by port
firmware_crc32   553E07E4          firmware_rev 3.8.0  (CONF:CAP:JSON?)
registry row     nq-b COM8 COM9 7E2873046200E891 020026703RYN079002 image=UNKNOWN/UNKNOWN
sd_supported     true, FAT32       factory cal slope 1.0 / intercept 0.0 -> IDENTITY = UNCALIBRATED
```

⚠ **The registry's prose comment says this board "was on v3.8.0, crc32 `9CF57ADD`" — it reads `553E07E4`.
That comment is STALE and its own row's `image=UNKNOWN/UNKNOWN` is the honest value**, exactly as the
file warns (*"a cross-board equivalence claim in a prose comment cannot be invalidated when one side
changes"*). **So this board is currently on an image traceable to NO commit**, and neither head is on it.
I will record `image=<crc32>/<commit>` **at flash time**, in the same action as the flash, per the
registry's rule — it cannot be derived afterwards.

**Programmer: PICkit 5 `020026703RYN079002` → `-TPPK5`, NOT `-TPPK4`, and `--ts` always.** Two PICkit 4s
are also attached; an unqualified `ipecmd` can program another lane's board and return *my* predicted
crc32 having wiped someone else's NVM. **Serial confirms the target; crc only confirms the image.**

## 6. ⚠ Flagged, not acted on: the ACCEPTED-RESIDUAL mechanism's provenance

The new mechanism clears a non-PASS gate via *"a fetched repository comment carrying an explicit
`ACCEPTED-RESIDUAL <fingerprint> artifact=<digest>` line."* **Every comment in these repos is authored by
the one identity every lane and bot shares.** So such a line is **AI-generated unless the operator wrote
it**, and the digest pin proves only *which artifact* was accepted, never *who accepted it*. That is the
label-provenance failure the operator already ruled on once for `blocked:operator-decision`.
**Not mine to act on and I am not being asked to** — recorded because it is the mechanism that would
clear the gate I was asked to produce.
