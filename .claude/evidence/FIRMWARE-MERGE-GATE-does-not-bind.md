# ⛔ firmware's merge gate does not bind the lanes — and `--admin` is the bypass, by configuration

Produced 2026-10-07 while verifying a relayed claim about branch protection. The relay reported two
fields; **reading the whole object changes the conclusion.** Read-only.

## V — the full protection object on `main` (relay covered only the first two lines)

```
required_status_checks               none          <- relayed
required_pull_request_reviews        none          <- relayed
required_conversation_resolution     enabled TRUE  <- the ONLY enabled requirement
enforce_admins                       enabled FALSE <- ⛔ admins are NOT subject to it
allow_force_pushes                   enabled TRUE  <- ⛔ main's history is rewritable
allow_deletions                      enabled FALSE
block_creations                      enabled TRUE
required_linear_history              FALSE    lock_branch FALSE    required_signatures FALSE
restrictions.users                   [cptkoolbeenz]   <- the identity EVERY lane authenticates as
```

`gh api user` → **`cptkoolbeenz`, type `User`**. Confirmed independently: the lanes authenticate as the
operator's own account, not a bot.

## ⛔ E — the bypass is EMPIRICAL. Two PRs merged past unresolved threads.

GraphQL over the last 20 merged firmware PRs; of the 13 that had any review threads, **2 merged while
carrying threads whose first comment PREDATES the merge**:

| PR | merged | threads | unresolved | unresolved & predating merge |
|---|---|---|---|---|
| **fw#1111** | 2026-09-17T03:43:38Z | 4 | 3 | **3** |
| **fw#1147** | 2026-09-21T08:24:08Z | 7 | 2 | **2** |

So `required_conversation_resolution: true` **did not prevent those merges**. This is E, not I — I did
not have to reason about `enforce_admins` semantics to get here, the merges happened.

⚠ **fw#1147 is this lane's row.** My publication footprint lists `#1147` with a *"Qodo r3
disposition"* marker. I am not claiming the unresolved threads there were substantive — **I have not
read them** and a thread can be left unresolved deliberately (a declined suggestion). The structural
point does not depend on it: **nobody was stopped.**

## ⭐ The composite, which is the actual finding

1. Exactly **one** requirement is enabled on `main`.
2. `enforce_admins: false` makes it **not apply to admins**.
3. My own standing MERGE ORDER step 4 is **the squash-admin merge path** — precisely the one the
   protection exempts. *(Described, not spelled: the bench and merge guards match raw command text,
   so a document quoting a gated command is indistinguishable from an attempt to run it. A peer's
   draft of this same finding was refused by the pre-merge gate for exactly that.)*
4. The push restriction restricts `main` to **one user: the identity every lane already is.**

> **So the repo enforces nothing against us.** The gate is not weak, it is *inapplicable* to the only
> actor using it.

## ⛔⛔ THE INVERSION TO RESIST — and it points the opposite way from throughput

`mergeStateStatus: BLOCKED` on firmware does track unresolved threads (my prior 17/17 measurement
stands, and `required_conversation_resolution` is genuinely enabled, unlike on test-suite /
claude-skills where the field is unsettable at 403). The tempting next step, under a 30-PR throughput
mandate, is: *"BLOCKED is only advisory, so it is not a real barrier — stop parking on it."*

**That reading is available and it is wrong.**

> **GitHub declining to stop me is not permission.** The binding rule is the fleet's, not the repo's:
> *admin-merge only the converged shape — Qodo converged AND a clean audit on the head being merged.*
> That rule is unaffected by `enforce_admins`.

⭐ **This discovery makes the fleet rule MORE load-bearing, not less.** It is now established to be
the *only* thing enforcing anything on this repo — the platform contributes zero. A finding that the
guardrail is absent is a reason to hold the hand-rule harder, not to relax it. The two merged rows
above are what the absence looks like.

## What this does NOT establish

- **Whether fw#1111's and fw#1147's unresolved threads mattered.** Not read. Counting threads is not
  reading them, and that distinction is this week's recurring defect.
- **Which mechanism** each merge used (`--admin`, or a direct push to `main` under the single-user
  restriction). The merges are E; the *route* is not established. `allow_force_pushes: true` means a
  direct-push route also exists.
- **Whether `enforce_admins: false` is deliberate.** It may be the only workable setting when one
  identity owns every role — which is the same single-identity architecture that makes an on-row
  "operator instruction" unverifiable. **Both findings have one root cause.**

## Operator items this raises

1. `allow_force_pushes: true` on `main` — main's history is rewritable by the lane identity.
2. `enforce_admins: false` — the sole enabled requirement does not apply to the sole actor.
3. A separate identity for lanes would fix the merge gate **and** the unverifiable-instruction
   problem at once, since both follow from one account holding every role.
