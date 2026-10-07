# ⛔ CORRECTION: my fw#976 free read was WRONG. The six threads are not out-of-scope — they have NO home.

Supersedes the claim in `fw976-SEVEN-THREADS-free-read.md` (`c82b49cc7`). Produced 2026-10-07 after
the authorised fix round. Verified at head `2ef26a3010fb3c5ff797b400d3e245787d6d6753`.

## What I published, and why it was wrong

I wrote: *"six of these seven are instances 16–22 of a class the file enumerated to fifteen and then
removed from scope on purpose… Acting on them restarts a treadmill the row abandoned after three
rounds."*

The **first half is true**: the class is real, the docstring does enumerate fifteen defeating
refactors, and the assertion genuinely was moved out. **The second half does not follow**, and I
never checked it. "Removed from scope" requires that the thing it moved *to* covers these threads.

**It does not.** The fire I briefed found all six NOT COVERED; I then verified it myself rather than
accept it:

| thread | independent check at the live head | covered? |
|---|---|---|
| **:920** claim failure must terminate handler | **`SD_ClaimOrRefuse` → 0 occurrences** in the 707-line host test | ❌ |
| **:1122** caller checks wrapper bool, aborts | host test line 111: *"it is **deliberately absent**. See PROPERTY 3"* | ❌ |
| **:842** wrapper delegates ≥1 time | both `SD_ArmOrRefuse` mentions are **docstring prose** (lines 22, 172) — never test code | ❌ |
| **:1033** streaming verdict controls exit | different file/function; no host-test model exists (#998, unlanded) | ❌ |
| **:1043** `SD_RefuseIfSuspended` checked | **`SD_RefuseIfSuspended` → 0 occurrences** in the host test | ❌ |
| **:694** null callback leaves format stuck | NULL-case test uses the non-`FORmat` shape | ❌ |

### ⭐ The best evidence is the test's own docstring, and it is an outright admission

Line 172, the test author explaining why the caller side is absent:

> *"Re-implementing six `if (!SD_ArmOrRefuse(...)) { return ...; }` shapes against mocks would assert
> only that the six copies in THIS file behave as written. **It binds to nothing.**"*

and line 155:

> *"the obvious move here was a grep asserting 'the clear appears, then the retraction, then the
> release'. **That was deliberately not done.**"*

**So the move was REAL but NARROWER than the docstring's framing implies.** What moved to the host
test is the **helper's own internal ordering**. The **caller-side consequence** properties — which is
what all six threads are about — were deliberately left unmodelled *and are not asserted anywhere
else either.* Not out of scope. **Homeless.**

## ⛔ The defect in my own method, and it is the one this row is about

I read the **docstring's account** of where the assertion went and treated it as the assertion being
covered. I verified the host test **exists**, is **sized** (30,057 bytes), and is **built**
(`ARM_BIN := run_971_tests`) — and never read it.

> **That is a presence check standing in for a consequence check — the exact defect all seven threads
> report, committed by me, in the act of dispositioning them.**

It is also summary-instead-of-source: the docstring is a *summary* of the move, and I took it for the
*record* of the move. Summary-instead-of-source is undirected — care does not protect against it,
only reading the primary record does. **The one thing that caught it was flagging my own unverified
premise as the fire's item 1 instead of carrying it forward as settled.**

## ⚠ And the fire's own citations were imprecise — right conclusion, two wrong supports

Logged because this is the second instance today of a correct conclusion riding on inaccurate
evidence, and the first cost a whole analysis:

1. *":842 — host test never models `SD_ArmOrRefuse()` **at all**"* — **false as written.** The symbol
   appears twice. Both are prose, so the conclusion stands; "at all" does not. A bare `-F` count of
   5 was the **substring trap** (`SD_ArmOrRefuseWithCleanup` contains `SD_ArmOrRefuse`) — 3 + 2, not 5
   of one thing.
2. *docstring says "never as 'a retraction runs'"* — **not verbatim**; 0 hits. The real admissions are
   at lines 155 and 68, and line 172 is stronger than what was cited.

**The fire reached the right answer with weaker evidence than the file actually offered.** No finding
changes; every support had to be re-derived.

## Where this leaves the seven threads

- **:801** — FIXED this round (`efba54c1f56e24147bb3ad53497850e18bfbae4b`). Was **real and live**: 9
  call sites to `sd_card_manager_UpdateSettings`, only 1 inside the recognised helper; a realistic
  mutation passed identically to baseline before the fix, and the fix fails closed. Both directions
  proven by neutralising it (112/113 without, 113/113 with).
- **stale path citation** — FIXED (`d55d5dbd8b8b842a9ca72d0e7aae6d7e68865a6d`); 10 other citations
  checked, all resolve.
- **:1033, :694** — open, with a named destination (#998 / the docstring's own disclaimer).
- **:920, :1122, :842, :1043** — ⛔ **open with NO home.** Not covered by the host test, not in scope
  for the checker by the fifteen-variants argument, and not asserted anywhere else. **This needs a
  decision, not a disposition**, and it is not mine to make: I am the fixer on this row now, so the
  audit and this call both go elsewhere.

**I did not resolve any thread, post any disposition, or touch any label.** Drafting dispositions
citing coverage that does not exist would have repeated the error above at one more remove.
