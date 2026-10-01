# PRE-REGISTERED — review-to-next-push interval distribution

On disk BEFORE measuring. nq-b, 2026-10-01. Read-only, no disposition on any row.

Currently carried by **three points**: 130s (sk#231), 258s (sk#230), 311s (fw#996). The claim under
test is that those are a **property of the pipeline**, not three incidents.

## ⛔⛔ MY ERROR DIRECTION, AND IT IS A SELECTION EFFECT I COULD EASILY REPORT AS THE HEADLINE

**I proposed the pipeline-property framing from those three points, so I want short intervals.** The
crude thing in the method that delivers them: **the interval only EXISTS for rows where a push
FOLLOWED an attestation.** Rows reviewed and then never touched again have no interval and drop out
of the sample — and those are exactly the rows where freshness is fine.

> **So the interval distribution is CONDITIONAL on "a push followed", and that conditional is the
> event which creates short intervals.** Measuring it answers *"when a push does follow a review, how
> soon?"* — it does **NOT** answer *"are attestations usually stale?"*

**PRE-COMMITTED, and this is the whole discipline of this measurement:**
1. **Two numbers, never one.** (A) the conditional interval distribution, stated with its conditional
   **inside the sentence**, and (B) staleness prevalence over the FULL row denominator including rows
   with no later push at all.
2. **(A) must never be reported as evidence for (B).** A short median interval is compatible with
   almost every attestation being current, if few rows are pushed after review.
3. **A result where most intervals are SHORT is the one I must distrust**, and I owe it an explicit
   statement of how many rows were EXCLUDED for having no post-attestation push — because that
   excluded count is the denominator (A) silently dropped.

**And per the flag lesson from an hour ago: the number is handed over with its negative clause inside
the claim, not beside it.** Not *"median 4 minutes"* but *"median 4 minutes among rows that were
pushed after review; says nothing about how many rows are pushed after review."*

## THE THREE BOUNDS, STATED RATHER THAN DISCOVERED (coordinator's, adopted)

1. ⛔ **`created_at` on a Qodo body is FROZEN at the first review ever** — there is exactly one
   standing body, edited in place. **So the attestation timestamp comes from the POINTER COMMENT that
   NAMES THE HEAD, never from the body's timestamp.** Using the body would inflate every interval to
   the age of the PR and would confirm my bias spectacularly.
2. **`?since=` keys on `updated_at`, not `created_at`.** Not used for selection here.
3. **An empty `gh --json` under a bws 503 reads identically to "no attestation."** Structural retry;
   book **UNDETERMINED**, never zero. (Measured tonight: a 503 stripped `GH_TOKEN` and returned ZERO
   comments on a row that has 41.)

## METHOD

**Attestation pointer** = a comment whose body cites a 40-hex SHA **that is one of this PR's own
commits**. SHA candidates are validated against git (`cat-file -t` == commit) — a bare hex class
picks up all-digit GitHub comment IDs, measured on fw#1110.

**interval** = (timestamp of the first commit on the branch strictly AFTER the pointer comment's
`created_at`) − (that `created_at`). Rows with no such commit are **EXCLUDED and COUNTED**.

**Staleness prevalence** = of ALL rows in the named population, how many have their newest
attestation pointer naming a SHA that is not the live `headRefOid`. Denominator is every row,
including those with no attestation at all (reported as its own bucket, not folded).

## POPULATION — named, and not selected by me where avoidable

Primary: the **30 open firmware rows**, because the census already fixed that population and nq-c
holds it cached. Asking nq-c and conv-fw for their cached corpora in parallel — **a cached corpus I
did not select is also a population someone else can check**, which is worth more than a bigger one
I chose. Any extension to the test-suite or claude-skills is reported as a separate population with
its own counts, never pooled silently.

⚠ **`gh pr list --limit 200`**; the default is 30 and firmware has ~30 open, the exact value where a
default query looks complete.
