# ts#349 round-5 residue — VERBATIM mirror of the park comment

Preserved by nq-b, read-only, on 2026-09-30. **This is a MIRROR, not a filing.**

- Source: `daqifi/daqifi-python-test-suite` PR #349, comment id `5905936630`, posted 2026-09-30T07:00:50Z
- Comment URL: https://github.com/daqifi/daqifi-python-test-suite/pull/349#issuecomment-5905936630
- Audited head the park is anchored to: `8fbf62f9b`
- Verified at mirror time: PR OPEN, labels `parked` + `blocked:audit-findings`, head still `8fbf62f9b`

## Why this mirror exists, and what it is NOT

A sweep established that these four `fix_now` defects have **no tracking issue**: exactly one
test-suite issue exists in the window (#466, a different adjacent gap), and the window's
completeness was proven by control (oldest of 40 fetched = 09-12, predating 09-30).

⚠ **The risk is DISCOVERABILITY, not LOSS.** A GitHub comment on an open PR is as durable as an
issue; what it is not is *queryable*. So "one comment away from loss" overstates it — nothing here
is at risk of vanishing. The real cost is that any query for open defects misses all four.

**This mirror is not a filing and confers no tracking.** Filing these as issues is an outward-facing
write that has been ROUTED to the operator rather than taken on a peer's reading of a relayed
ruling. If that ruling comes back permissive, the text below is ready to file verbatim.

---

## ⛔ PARKED AT THE ROUND CAP — round 5 returned BLOCK with 4 `fix_now` defects

Round 5 (`8fbf62f9b`, audited by a lane that had never touched this row) returned `gate BLOCK`,
`keep_fixing`, **4 distinct defects, all `fix_now`, none declined.** That is the fifth round, and the fleet
cap is five.

**The cap is a resource limit. Reaching it means "stop spending on this approach" — never "the change is now acceptable."** So this parks; it does not merge.

### The convergence signal, measured

```
round 3   34b1085e9   +1064 across 3 files
round 4   46aebf10f   +1428
round 5   8fbf62f9b   +1598
```

**The audited diff grew in every round.** A converging cycle holds or shrinks it. And each round's defects were in code the *previous* round's fixes added — round 5's four are all in helpers introduced by rounds 3 and 4.

### Residue — the 4 defects, filed rather than fixed

- **HIGH** — an `inconclusive` `ordering_verdict` satisfies `_check(verdict != 'unfixed')` and exits 0, so **the sole #982 discriminator is defeated silently and the gate scores a PASS.** The arbiter overrode the skeptic's downgrade back up to high.
- **MEDIUM** — split-file cleanup uses the wrong filename convention: firmware writes `base-N.ext`, cleanup deletes `base.ext.NNN`. Leaks recordings **and yields a false "verified by listing" attestation.**
- **MEDIUM** — unguarded exceptions in the two new cleanup helpers skip all remaining teardown, **leaving the bench armed.**
- **MEDIUM** — a bare-name SD delete does not resolve #689 bucket subdirectories past 64 files: false gate FAILURE plus an orphaned recording.

### Two gaps no round examined, and byte coverage cannot see them

Coverage was **83129/83129** and green. Neither of these is a coverage failure — they are **coverage of attention**:

1. **The epoch contract was never tested at the call sites.** It is a required positional with the contract in a docstring, which is the right structural shape — **but a docstring is not enforcement**, and nothing verified that each call site captures the instant immediately before `SYST:STR:STOP` is written. **This round's headline fix went un-re-examined.**
2. **Round 3's arbiter-declined `:429` was not re-raised by the fresh blind leg.** Unanswered, **not vindicated**.

### The cost of parking, stated plainly

**#1018/#982 has no working companion test.** The HIGH means this file can PASS on broken firmware, so it is not fit for purpose as it stands — parking does not lose working coverage, because there is none to lose. But the gap is real and it belongs on the operator's list, not buried here.

### What would unpark it

An operator exception for round 6, **or** a decision to narrow the claim: the HIGH and the three mediums are all in *teardown and verdict-scoring* machinery, not in the #982 measurement itself. A version that measures and reports without attempting cleanup or a pass/fail verdict would be a much smaller surface than 1,598 lines.

Not merged. Not marked. Nothing in this comment is an audit verdict — the artifact is the verdict.
