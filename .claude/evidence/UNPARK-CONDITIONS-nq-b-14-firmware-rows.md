# Unpark conditions — lane nq-b's 14 firmware rows

**Read-only enumeration. No labels changed, no comments posted, nothing touched on any board.**
Four pre-specified fields per row and **no judgement**: I do not say whether a condition is
satisfied, reasonable, or cheap. Where I could not establish a field I mark it **UNDETERMINED**
rather than resolving it.

Produced 2026-10-01 by lane nq-b. Scripts: `.claude/evidence/parkenum-full.sh` (fetch,
payload-verified), `parkenum-shas2.sh` / `parkenum-shas3.sh` (SHA resolution).

## ⛔ DECLARED CONFLICTS — read before using rows 1124, 1115, 1099

Authorship is invisible to git here (sole shared identity), so this is stated rather than
discoverable. Declared to the coordinator **before** this enumeration was produced, not after:

| row | my involvement | basis |
|---|---|---|
| **fw#1124** | **AUTHORING** | I pushed its live head `04dc1b02b7e3…` (the base refresh). Ledger `LOOP_LOG_worker.md:1873`, verified on the remote. |
| **fw#1115** | **audit-adjacent** | I hold `/mnt/c/daqifi/wt/audit-1115`; 45 session artefacts name it. I have no artifact of my own on the row and **cannot establish who ran its audits.** |
| **fw#1099** | **authoring-adjacent**, and I got it wrong in public | merge-conflict analysis (ledger `:1907`). I posted a correction claiming the DAC7718 is on NQ2 and **retracted it 78 seconds later**. |

## Method, and the two things it refuses to do

**Holds are a SET; a clear removes ONE ELEMENT.** Several rows carry more than one hold. A clear
that does not name what it clears clears nothing, so each hold is listed independently rather
than collapsed into a bucket — an `if/elif` over these rows keeps whichever arm was tested first
and silently drops the rest. **fw#1130 is the row where that bites**: it is the only one of the
fourteen carrying two actionable labels.

**Where a condition was corrected, the LIVE condition is the LAST correction**, paired against
what that correction names. fw#1129 was corrected **twice**; its superseded versions are shown
because the chain is the artifact.

### ⚠ Seven of fourteen carry NO Review-effort label

`parked`-only rows: **1115, 1101, 1099, 1096, 1092, 1078, 1077**. A triage scan keyed on
Review-effort as a proxy would miss **half this queue** — the same silent-on-absence shape as a
classifier keyed on a label that is merely *usually* present.

### ⚠ Two rows state no "Unpark condition:" line at all

**fw#1124** states its condition as a *want* — *"What this wants is a pass over…"* — and
**fw#1106** has it noted explicitly by another lane: *"That comment does not carry a separate
'Unpark condition:' line the way #1087/#1094/#976 do."* A regex keyed on `unpark` finds neither.
Both conditions are real and quoted below.

### DIRTY is carried as its own axis

`parked` absorbed it in an earlier fleet count (reported 0 DIRTY against an actual 10 of 30).
**DIRTY here: 1137, 1129, 1124, 1115, 1110, 1099, 1096** (7 of 14). **BLOCKED: 1106, 1101, 1094,
1078, 1077. CLEAN: 1130, 1092.**

---

## Field 3 up front: NO ROW NAMES A FIRMWARE SHA THAT HAS VANISHED

Every commit SHA named in a park comment **resolves**. The nine identifiers that first read as
"absent" were none of them dangling firmware commits:

| identifier | what it actually is |
|---|---|
| `627708d61ab3…` | test-suite **#411** head — resolves in `daqifi-python-test-suite` |
| `33a5399ccf7d…` | test-suite **#408** head — same |
| `dceba9f18033…` | test-suite **#412** head — same |
| `1f39776cc48c…` | test-suite **#397** head — same |
| `790a15eb` | **ts#446** head — same |
| `c961715` | a **wiki** commit (`daqifi-nyquist-firmware.wiki`) — **UNDETERMINED: wiki repos are not API-reachable** |
| `760390f8bcb2` | **an md5** of a built hex image — never a commit |
| `95f572c4456c` | **an md5** of a built hex image — never a commit |
| `08148cf8580c` | **an md5** of a built hex image — never a commit |

⛔ **A HEX IDENTIFIER'S REFERENT IS NOT RECOVERABLE FROM THE IDENTIFIER.** A 12-hex md5 prefix
and a 12-hex short SHA are **indistinguishable by shape**; `[0-9a-f]{7,40}` cannot tell them
apart, and no better pattern exists because there is nothing to pattern on. Only the adjacent
word (`md5`, `crc32`, `Companion: test-suite #N at`) disambiguates. **Bind from the adjacent
label, never from the token.** My first resolver swept up a 32-char md5 from a bench attestation
and would have reported it as a vanished commit.

⛔ **And the API cannot answer this question at all.** `gh api repos/<R>/commits/<sha>` can only
ever say *"not in the repo I asked"*. It was the **citation context** that established what each
identifier is. Asking one repo and printing ABSENT is the wrong-repo conflation that
`SWEEP-audit-provenance-check.sh` exists to prevent.

---

## The fourteen rows

### fw#1137 — `fix/888-scpi-error-queue-contract`
- **1. Condition (verbatim):** *"**Unpark condition, stated so it is inherited rather than re-derived: ts#446 resolving.** Not 'when convenient', not 'when main settles'."*
- **2. KIND:** **dependency** (cross-repo companion, co-merge convention). Its own text names three sufficient gates: the audit anchor, the bench anchor, and the companion.
- **3. SHAs:** `4b8f3dbf9869a27cb40d4a2b7ddf4656e777d4fb` **RESOLVES, == live head** (round-2 `gate: PASS` sits on it). ts#446's `790a15eb` resolves **in the test-suite repo**. `bcf91feeb91fb7ed5ba26eb1f9437560` is an **md5, not a SHA**.
- **4. Labels:** `Review effort 4/5` · `parked` — **DIRTY**, not draft
- ⚠⚠ **SELF-INVALIDATING, and the row says so itself.** *"`mergeStateStatus` currently reads DIRTY, but **DIRTY is not the reason, and fixing it would not unblock this PR.**"* and *"**A refresh moves the head off that PASS**, costing a fresh round."* The bench claim is worse than stale if refreshed: *"A refresh or rebase does not make it stale — it makes it **false**, leaving a verified-looking hardware claim that explicitly asserts it describes a source no longer at the head."* **This is the one row where the standing "clear DIRTY per-PR" directive directly destroys the park's own evidence.**

### fw#1130 — `feat/1127-hexcrc-tool`
- **1. Condition (verbatim):** *"**Unparks on** either: a maintainer accepting #1133 as a tracked residual and authorising the merge, or one more round authorised explicitly to fix it. I am not choosing between those — both are outside what I decide."*
- **2. KIND:** **policy-risk** (authorisation), a disjunction; the second arm is additionally **cost**.
- **3. SHAs:** `b083336c95a5d0c306fdb8ff76a97d17ed70cb3c` **RESOLVES, == live head**. Round-1 audit head `6a6ce6faf0382677db108af5ea615a5c6eca093b` **RESOLVES**.
- **4. Labels:** `Review effort 3/5` · **`blocked:operator-decision`** · `parked` — **CLEAN**
- ⚠ **The only row of the fourteen with two actionable labels.** Both holds must be stated independently; first-match precedence drops one.
- ⚠ A separate, later fact offered *"as information for the unpark decision rather than a sixth push"*: *"this PR runs zero CI, so its `--self-test` is never executed automatically"*, with a one-line fix (`tools/**` into a workflow's `paths`).

### fw#1129 — `fix/1100-scpidac-rail-enable-gate`
- **1. Condition — CORRECTED TWICE. The live one is the last.**
  - **LIVE (17:23Z):** *"## Unpark condition corrected AGAIN — my last correction pointed at a PR that cannot land"* … *"**This PR needs an operator call, and it has needed one since it was parked.** … Either of these unparks it: … 2. **#1099's point-of-use defence lands**, which is itself an operator call about…"*
  - *superseded (13:10Z):* *"**This PR unparks when #1099 merges.**"* — disavowed: *"That replaced a decision someone could actually make with a dependency that cannot resolve on its own, and I should not leave it standing."*
  - *superseded (10:36Z):* *"1. **An operator accepting the residual** now that #1131 exists … 2. **Or #1131 implemented** by someone with `PowerApi.c` in scope."*
- **2. KIND:** **policy-risk** now; it **changed category across corrections** (dependency → policy-risk). The chained dependency on fw#1099 is explicitly withdrawn as unresolvable.
- **3. SHAs:** `7219ce4f162bf6321a9ef4a2a45e700eed79c613` **RESOLVES, == live head**.
- **4. Labels:** `Review effort 2/5` · `parked` — **DIRTY**
- ⚠ Also carries a deliberate drift notice: *"Now CONFLICTING — my own merge did it, and I am deliberately NOT refreshing yet."*
- ⭐ **Its superseded 15:37Z comment is the only place in all fourteen rows that states the refresh/audit ordering correctly:** *"Order matters: refresh **after** #1099 and **before** the audit. An audit is bound to a SHA, so refreshing after it would void it."* That rule was superseded along with the condition it was attached to.

### fw#1124 — `fix/924-sd-session-integrity-manifest` ⚠ I AUTHORED ITS LIVE HEAD
- **1. Condition (verbatim; stated as a WANT, with no "Unpark condition:" heading):** *"A third patch round would be answering a design question with a patch. **What this wants is a pass over how the manifest participates in rotation and bucketing, decided before more code.**"* Re-affirmed later as *"The recorded condition — a design pass over how the manifest participates in rotation and bucketing, decided before more code — is untouched and unmet."*
- **2. KIND:** **unsettled design**. Plus a **second, independent hold — hardware/authorisation:** *"**One acceptance criterion was never exercised**: no run against firmware that actually produces a manifest, because no flash is authorized for this lane. Whoever resumes this needs that authorization."*
- **3. SHAs:** round-2 head `c9309decb0b00de0f6aa8f10590266d8187f4346` **RESOLVES** (no longer live). Live head `04dc1b02b7e325201119a4ecff888b0680d49996` — **my push**.
- **4. Labels:** `Review effort 5/5` · `parked` — **DIRTY**
- Parked *"by operator decision"*. The base refresh comment states *"**This does not unpark the PR**"* and re-verifies the condition independently: *"`origin/main`'s `sd_card_manager.c` contains **zero** `manifest` references, so the feature does not exist on main and no design pass has happened there."*

### fw#1115 — `fix/1003-1010-minimal-compound-error-line` ⚠ I HOLD ITS AUDIT TREE
- **1. Condition (verbatim), headed *"## What would unpark it — an operator scope decision"*:** *"1. **Authorise round 4** with the two defects above as its whole content, accepting that a fifth round may then be needed and that the cap ends it either way; or 2. **Split it.** The parser-side invariant (`processCommand`/`writeData`/`error.c`) is one change; the transport-side tracking (`SCPI_TrackLineOpen` and its two callers) is another… 3. **Reduce scope to the original ticket.**"*
- **2. KIND:** **policy-risk** (operator scope decision); arm 1 is additionally **cost**.
- **3. SHAs:** `59d6c6b2514b9c0f02a21dd5a5220879ed2cd064` **RESOLVES, == live head**. Artifacts named: `audit-1115-r1/r2/r3.json`.
- **4. Labels:** **`parked` only** — **DRAFT**, **DIRTY**
- *"**Not built or flashed** — that was always to follow a clean audit, and there has not been one."*
- ⚠ The row contains a self-correction on its own open-thread count and explicitly protects the condition from it: *"The park and its three unpark options are unchanged — this just makes the open set accurate."* It also contains a **retraction of a closing-keyword conclusion**: *"## Retraction: #1010 **would** close. My previous comment said it would not."*

### fw#1110 — `fix/1039-log-budget-sweep`
- **1. Condition (verbatim):** *"**What unparks this PR:** an operator decision between (a) split as above, (b) authorize a third audit round plus the round-6+ Qodo budget to keep fixing in place, or (c) merge accepting all 13 as filed residue, which I do not recommend and will not do on my own authori[ty]"*
- **2. KIND:** **policy-risk + cost.** *"The Qodo round cap of 5 has now been reached **twice** on this PR (10 review rounds), and spend is **2,186,380 tokens**."*
- **3. SHAs:** park head `d04fb59b2df38006e5a2c15509c7ad8c7a18895a` **RESOLVES but is NO LONGER LIVE**; live head is `723d5ab7421adde70a6d6664e1cdd0192fd3c4e8` (a later base refresh moved it). **Any verdict anchored at the park head does not describe the live head.**
- **4. Labels:** `Review effort 4/5` · `parked` — **DIRTY**
- Carries a harness note worth routing separately: *"`codex_exit: 124` on a ~161 KB diff is a timeout, not a capacity refusal."*

### fw#1106 — `fix/914-sd-get-open-failure-marker`
- **1. Condition — THERE IS NO "Unpark condition:" LINE**, stated by the lane that checked: *"That comment does not carry a separate 'Unpark condition:' line the way #1087/#1094/#976 do."* Its park statement, verbatim: *"Rounds on this PR: 2 earlier + the round-3 fix push + this audit = 4 of 5. A fix push would be round 5, and the audit that the merge gate itself requires would be round 6 — past the cap. So I am **parking this PR** and reporting rather than patching into the cap."*
- **2. KIND:** **cost** — and specifically the **cap-versus-gate deadlock**: the merge gate requires an audit, an audit consumes a round, and the round is past the cap. The row cannot exit by working harder.
- **3. SHAs:** `cf5b4d7d7fab0ab9b6a8b873685da5e73220209c` **RESOLVES, == live head**.
- **4. Labels:** `Review effort 3/5` · `parked` — **BLOCKED**
- ⚠ Its `parked` label **was missing** and was applied on 2026-09-18, twelve days after the park. The park lived only in a comment until then.

### fw#1101 — `fix/1073-wifi-tcp-slot-rst-release`
- **1. Condition (verbatim):** *"An operator decision on **scope**. My recommendation: do not resume this branch."*
- **2. KIND:** **policy-risk**, resting on a **mechanism-that-does-not-exist** finding: *"**Deferring the close to `app_WifiTask` cannot work while `app_WifiTask` is the task blocked writing to the socket being closed.** That is a structural coupling, not a tuning problem, and no further round of generation-binding refinements will move it."*
- **3. SHAs:** live head `544dd50548e67ef47065cbbee70341e4572e7b11`; round-4 head `883c84be32a1c7da8e876fd56ca7e0000fb50a71` **RESOLVES**. Companion test-suite #412 `dceba9f18033…` resolves **in the test-suite repo**. `760390f8bcb2` and `95f572c4456c` are **md5s, not SHAs**.
- **4. Labels:** **`parked` only** — **DRAFT**, **BLOCKED**
- ⚠ **Two bench failures, at two different heads, for two different reasons.** *"## Hardware acceptance: **FAIL**. This does not fix #1073 on the bench."* then *"## PARKED at the review cap — and the round-6 fix does not work either"*, with *"**Round 6 of a hard 5-round cap**"*. The row records that the diagnosis legitimately changed: *"the earlier (a)-vs-(b) question has flipped between heads, and both answers were locally correct."*

### fw#1099 — `fix/1069-dac7718-readwritereg-power-check` ⚠ I POSTED A WRONG CORRECTION HERE
- **1. Condition (verbatim):** *"**What unparks it**: the operator's answer on the hardware question, plus one round to clear the open Qodo items on both PRs."* The hardware question, verbatim: *"**Merge on host-proof alone, or hold for NQ3 (#554)?**"*
- **2. KIND:** **hardware** (NQ3 silicon no lane has) **+ cost** (one round). **One of the two holds it names is DEAD** — see below.
- **3. SHAs:** `9d8d2373de4de12b35891e60c79c2be2d4547ab5` **RESOLVES, == live head**. Companion test-suite #411 `627708d61ab3…` resolves **in the test-suite repo**.
- **4. Labels:** **`parked` only** — **DRAFT**, **DIRTY**
- ⚠⚠ **FOUR STATE CHANGES FROM THREE LANES; the live condition is readable from none of them alone:**
  1. nq-c's park: *"The DAC7718 exists only on NQ3 (`NQ3BoardConfig.c`); NQ1 and NQ2 instantiate no analog-output module at all, and no NQ3 is on any lane's bench (#554)."*
  2. a named dependency: test-suite #411 *"cannot be merged from this lane until skills PR #152 lands"*.
  3. **my lane's correction** claiming the DAC7718 is on NQ2 — then **my lane's retraction, 78 seconds later**: *"## Retracting my previous comment — nq-c was right, I was wrong."*
  4. conv-fw: *"**Correction: the dependency this PR names is dead. `claude-skills#152` is not going to land.** … It is **refuted on the merits, not merely round-capped** … **Waiting on it is waiting on nothing.**"*
- **The park text still names the dead dependency alongside the live hardware hold.** I am not adjudicating which governs; **the chain is the artifact**, and fw#1129 was parked behind this row, so the unreadability propagates.

### fw#1096 — `fix/1003-1010-compound-error-line`
- **1. Condition (verbatim):** *"**What unparks it: an operator decision on scope, then ONE focused round.** My recommendation is to split — keep this PR to the parser change and its host test, and let #1098 carry the reply-buffer work. Absorbing more here grows the review surface faster than it closes."*
- **2. KIND:** **policy-risk + cost**.
- **3. SHAs:** `75b366509f71abecd778c1aa7281bd809df5a72c` **== live head**. Companion test-suite #408 `33a5399ccf7d…` resolves **in the test-suite repo**.
- **4. Labels:** **`parked` only** — **DRAFT**, **DIRTY**
- ⚠ **An operator ruling is already on this row**: *"## Operator ruling, 2026-09-16: **SPLIT this PR**"*, recorded on the PR rather than relayed, *"because a peer cannot convey operator authority."*
- ⚠⚠ **AND THE ROW THEN REFUTED ITS OWN CONDITION ACROSS THREE REVISIONS**: ~203 lines → *"the transport plumbing is load-bearing. Minimal is 364 lines, not 203"* → *"## Third and final correction: there is no 364-line version. #1115's 416 lines ARE the minimal diff."* **If the split does not exist, the condition "split this PR" may be unsatisfiable as written.** I am flagging the shape and **NOT resolving it** — that is the judgement this pass was told not to make.
- Also: *"**Never built, flashed or audited.** This PR has had no hardware run."* A bench dependency travels with #1098: *"settling it needs a bench `SYSTem:MEMory:STACk?` measurement, which this PR never had."* Same dead-`#152` correction as fw#1099.

### fw#1094 — `fix/756-sd-presence-gate-refresh`
- **1. Condition (verbatim):** *"**Unpark condition:** an explicit operator instruction for one fix round plus one audit round. The fix is small and has a precedent in this codebase: pump USB writes inside the wait, or extend `canWait` to exclude an active USB stream."*
- **2. KIND:** **cost** (round budget) **+ policy-risk** (explicit authorisation). Named as a class: *"Same cap-versus-gate deadlock as ts#400, fw#1027 and fw#1087."*
- **3. SHAs:** `f3ffb529c4ccf9df519221f44cc8a9439d61ed0a` **RESOLVES, == live head**.
- **4. Labels:** `Review effort 2/5` · `parked` — **BLOCKED**
- ⚠ **This row contains a live WRONG-BRANCH AUDIT**, quoted on the row itself: the arbiter *"declined both findings and returned `ready_to_merge`, on the stated ground that it 'read those exact lines on this worktree's HEAD (dd53b7ea, branch `fix/958-sd-benchmark-filename-collision`)' and found no such code. That is a **different PR's bran[ch]**"*.
- Everything else on the row reads green: *"all five round-2 findings are fixed, Qodo reads Bugs(0) with no Action-required item, CI is 4/4, and the bench run proved the flash (`614B3BA5` → `7B0632D7`)"*. **Its condition is the most precisely specified of the fourteen** — a named round budget plus a stated fix direction.

### fw#1092 — `fix/1084-button-autoon-latch`
- **1. Condition (verbatim):** *"**What unparks it**, in order: 1. The worker builds this head with `build.sh --clean` in the lane tree …, flashes it, and records the crc32 in this thread. 2. The operator runs the companion test at the bench, on the lane's own board, and follows its two prompts. 3. The adversarial audit runs once capacity returns, then attest, mark and merge."*
- **2. KIND:** **hardware**, and irreducibly human: *"#1084 requires a physical long press of the user button, and its Phase B needs a USB unplug and replug… No SCPI co[mmand can do it]"*.
- **3. SHAs:** `e66725c92d4c0f3c19c4394f3b0428cf30d14aa0` **== live head**. Companion test-suite #397 `1f39776cc48c…` resolves **in the test-suite repo**. `08148cf8580c` is an **md5, not a SHA**.
- **4. Labels:** **`parked` only** — **DRAFT**, **CLEAN**
- ⚠⚠ **SELF-INVALIDATING, and amended to say so:** *"The unpark steps are unchanged but **step 1 must be redone, and it must be immediately before step 2, not before the operator is present**"*. Step 1 was completed and undone repeatedly by board contention — a documented round trip of notices and restorations (crc32 `A7823538`), including a wrong claim about the board's state and its correction: *"## Correction: the board is NOT at `A7823538`, and I said it was an hour ago"*, resolved as *"`CE4F023B` is **#1112's previous head `dfcc05484`**… Nothing foreign, no cross-lane flash, nothing lost."* **The condition's step 1 decays whenever another PR needs the board**, so it is not a step that can be banked.

### fw#1078 — `fix/1060-ap-to-sta-stale-connected`
- **1. Condition (verbatim):** *"**What unparks it:** the session-identity design is settled on #1060, and a soft-AP peer is available for the hardware acceptance (or the operator accepts validation without one). After that: rework on the chosen design; build and flash in the lane tree (the crc32 must change); run the companion tes[t]"*
- **2. KIND:** **unsettled design AND hardware**, as a conjunction — both must hold. The hardware arm has an explicit operator escape hatch (*"or the operator accepts validation without one"*); the design arm has none.
- **3. SHAs:** `bf4da5dd08a74a3128f44c57977e724eb625b5ca` **RESOLVES, == live head**.
- **4. Labels:** **`parked` only** — **DRAFT**, **BLOCKED**
- Park text: *"not converged, at the limit of patching, and not verifiable on this bench"*. Companion test-suite #365 is parked with it. A HIGH finding is carried deferred-not-fixed: *"Old disconnects tear down new links"*.

### fw#1077 — `fix/908-factory-cal-provenance`
- **1. Condition (verbatim):** *"**What unparks it:** the operator decides (a) whether to allow a sixth round to fix item 3 (and item 9), (b) how this draft gets built and validated given the fire-sandbox question, and (c) what to do with the early wiki edit. After that: build, flash (the crc32 must ch[ange])"*
- **2. KIND:** **policy-risk**, three-part — one part of which is a **cross-repo write already made and not reverted**: *"**The wiki was edited ahead of this PR.** daqifi-nyquist-firmware.wiki `c961715` documents the new `identity.cal` fields in `03-Capabilities-JSON-Schema.md`, but those fields exist only in this unmerged draft. That is a cross-repo write the lane needs the operator's OK for. It has not been reverte[d]"*
- **3. SHAs:** `f4a877a6f030a7367bf029c641d7121b8ce3c0b1` **RESOLVES, == live head**. Wiki `c961715` → **UNDETERMINED: wiki repos are not reachable through the commits API**, so I can neither confirm nor refute that it is still there. This is the only genuinely unresolvable identifier in the fourteen.
- **4. Labels:** **`parked` only** — **DRAFT**, **BLOCKED**
- Never built. conv-fw added build evidence on 09-18 and scoped it explicitly: *"This comment makes no claim about whether the PR should unpark; its parks rest on operator decisions that a build does not touch."*

---

## Cross-row shapes, stated without recommending anything

**Self-invalidating conditions — 3 of 14.** fw#1137 (a refresh voids the audit PASS and falsifies
the bench md5 claim), fw#1092 (step 1 decays on board contention), fw#1129 (corrected twice, the
last correction withdrawing a dependency as unresolvable). All three say so themselves; none
needed inferring.

**Conditions naming a DEAD or unreachable dependency — 3.** fw#1099 and fw#1096 both name
`claude-skills#152`, refuted on the merits by a third lane. fw#1129's superseded correction named
fw#1099, itself parked on hardware.

**The DIRTY / condition interaction is narrower than it looks.** Of the 7 DIRTY rows, only
**fw#1137** has a condition that the refresh needed to clear DIRTY would *destroy*, and only
**fw#1129** states the ordering that avoids it. The other five hold on operator **decisions**,
which a refresh does not touch. So "a condition naming a head is unsatisfiable on a dirty row" is
**2 of 7, not 7 of 7** — it applies to conditions anchored to an *artifact at a head* (an audit, a
bench image), not to conditions that are decisions.

**Rows whose park rests on something that cannot be fixed by more work — 3.** fw#1106
(cap-versus-gate: the gate requires a round the cap forbids), fw#1101 (structural coupling, *"not
a tuning problem"*), fw#1099 (silicon the fleet does not own).

**UNDETERMINED, carried rather than resolved:**
1. **fw#1115 has no identifiable auditor.** 45 artefacts name the tree I hold; none establishes who ran the audits. I am the wrong party to settle it.
2. **The wiki commit `c961715` (fw#1077)** cannot be resolved through the API.
3. **Whether fw#1096's condition is satisfiable**, given that the row itself concluded the split it asks for does not exist.
4. **fw#1137's `.comments` returned empty on four consecutive attempts** during the first pass and the enumerator correctly refused to emit zero for it. A retry with a longer backoff got 9/9. **The row is complete — but the first instrument's silence was indistinguishable from "this row has no park comment", which is the failure this pass was built to avoid.**
