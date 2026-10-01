# ⛔ READ THIS BEFORE TRUSTING `audit-sk231-60ed39e97-PASS.json`

**That artifact is genuine, its gate really is PASS, and it DOES NOT COVER the head sk#231 has
now.** Nothing about the artifact is wrong; it attests a commit the PR no longer points at.

| | |
|---|---|
| artifact attests `provenance.head_sha` | `60ed39e970b3e7d7facdd08d36e50a61cc2d2d87` |
| sk#231's live `headRefOid` (checked 2026-10-01 06:45Z) | `dae17d85107ab64f04bf95843b5f2fd111baca63` |
| difference | **1 commit ahead, 0 behind** — `+36/-6` on `bench/SKILL.md` |

## The timeline, because "stale audit" is the wrong diagnosis

```
01:22:10Z  60ed39e97 committed
01:46:39Z  Qodo's last comment            <- ran against 60ed39e97
02:09:44Z  MY AUDIT ARTIFACT WRITTEN      <- PASS on 60ed39e97: CORRECT AND CURRENT at that moment
02:11:54Z  dae17d851 committed            <- 2 minutes 10 seconds later
06:45:23Z  now                            <- both legs 4h33m stale
```

**I did not audit a superseded head.** The audit was valid when it ran and the head moved 130
seconds afterwards. **The attestation's useful life was two minutes**, and it has been false for
four and a half hours — during which the PR reads `mergeable: MERGEABLE`, `mergeStateStatus: CLEAN`,
and a file named `...-PASS.json` sits in this directory.

## ⛔ WHY THIS IS WORSE THAN AN UNAUDITED PR

The known row is *CLEAN + UNLABELLED = UN-AUDITED* — a PR that looks nearly-landed because no
verdict exists to be clean. **This is the next variant and it is sharper: CLEAN + A GENUINE PASS
ARTIFACT THAT NAMES A DIFFERENT HEAD.** An absent verdict invites the audit. A real PASS filed
under the PR's name forecloses it — which is the same shape as a validity warranty that shares its
subject's assumption (see `feedback_independence_of_checks_requires_independence_of_their_inputs`,
final section).

**And `mergeStateStatus: CLEAN` carries none of the information a reader takes from it.** It means
*no merge conflict*. `statusCheckRollup` is **`[]`** — this repo runs no CI on the PR, so CLEAN is
not a review signal, not an audit signal, and not a test signal. Three absent legs presenting as
one green word.

## STATE: sk#231 IS NOT CONVERGED. DO NOT MERGE.

Both legs attest `60ed39e97`:
- **audit** — mine, PASS, superseded 2m10s after it was written
- **Qodo** — last comment 01:46:39Z, 25 minutes BEFORE the live commit

Merging needs, per the merge order: `headRefOid` == the audited FULL SHA. It is not.
`60ed39e97…` ≠ `dae17d851…`, and short-SHA comparison is not permitted here.

**To unpark:** Qodo re-triggered on `dae17d851` (`/agentic_review` alone — `/improve` already ran
once on this PR), plus a fresh audit on `dae17d851`. **Both are cross-repo writes to
`cptkoolbeenz/claude-skills`, which is ask-first, so neither is mine to start unasked.**

## What the new commit contains, and the checks I ran on it

`dae17d851` — *"docs(bench): step zero must establish the BUILD, not only the board"*, `+36/-6`.
It is a substantive answer to my own ts#432 point (board ≠ build) and it reproduces the
crc32-provenance lesson nearly verbatim. **The `-6` rewrote content that existed at my audited
head** — the second failure-table row and the closing paragraph — so this is not an append my PASS
could be stretched over.

**It prescribes a SCPI command in prose, so I verified it rather than assuming** (the accepted
reversal on this very PR: a command typed verbatim makes the text the executable artifact):

| claim in the new text | check | result |
|---|---|---|
| `CONF:CAP:JSON?` | registered as `CONFigure:CAPabilities:JSON?`, `SCPIInterface.c:8497-8498` | ✅ legal — `CONFigure`→`CONF`, `CAPabilities`→`CAP`, `JSON` all-caps so one spelling only |
| response carries `firmware_crc32` | emitted at `SCPIInterface.c:7748` as `"firmware_crc32":"%08lX"` | ✅ real field, uppercase 8-digit hex (matches `9CF57ADD`) |

**Both hold.** Recording the pass explicitly: a verification that confirms is evidence, and leaving
it unrecorded is how the next reader pays for it again.
