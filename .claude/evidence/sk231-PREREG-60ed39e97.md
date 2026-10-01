# PRE-REGISTERED acceptance checks — sk#231 audit, written BEFORE launching

Any artifact failing ANY provenance check is VOID and its verdict discarded, per the ts#349-r4
precedent where every health field was true and the run was worthless.

## Provenance (read FIRST, before any health field and before any finding)
```
repo          MUST == cptkoolbeenz/claude-skills      (NOT the 'ORG/REPO' placeholder)
base_sha      MUST == 72a199a4a4e9530f8d178c6c44bfa49ab8f35387   (merge-base of the PR base)
head_sha      MUST == 60ed39e970b3e7d7facdd08d36e50a61cc2d2d87
covered_bytes MUST be within ~1% of 4056   (band 4015-4097)
              measured in the SHELL with `wc -c`; never Python text mode on this CRLF /mnt/c repo
⛔ covered_bytes == 0 is NOT A CLAIM -- REJECT, never compute a delta against it
              (`adversarial-audit.js:798` is `r.covered_bytes || 0`)
engine        MUST be codex (not 'both')
```
**Discriminating controls, measured in advance — any of these means the wrong object was swept:**
```
4912792   the main-based gate diff (269 files)
369515    my own nq-b ledger commit      -> the ts#349 r4 failure, exactly
19621     the sk#230 gate-repair diff
```

## Health (only meaningful AFTER provenance)
```
arbiterModel   MUST be non-null              <- GATE ON THIS
arbiter block  MUST have 6 keys: decline_comment_markdown, dispositions, summary, treadmill,
               unsound_refutations, verdict.  5 = key withheld; 0 = no arbiter
arbiterMissing / arbiterDegraded   DO NOT GATE ON THESE -- defaults
blindLegRan    report it; it is NOT a claim about WHAT was swept
treadmill      ABSENT has TWO causes: no arbiter (vacuous) vs an arbiter that withheld the key
               (UNASSESSED). Check the arbiter exists first.
gateReason     READ AS PROSE -- a PASS can be "clean in-scope"
```

## Findings
Severity ONLY from `.arbiter.dispositions[].agreed_severity`, joined on RATIONALE CONTENT
(never `index`: `rawFindings` is an integer, so an index join is impossible).
Key-count each finding: <=6 = summary, 15+ = real record. Report `__source` per finding —
steered vs blind — since 62% of fleet findings are seen by exactly one leg and no report shows it.
Citation check: `line: null` with a narrated range only. **Address-agreement stays RETRACTED** —
an object address and a prose address may point at opposite ends of one defect.
`user.login` on every comment read; the pointer comment is the Qodo anchor; report each leg
separately — the suggestions leg has multiple members, one edited 13 days after creation, so
"newest created" is not a shortcut.

## ⛔ MY CONFLICT, AND THE HAND-OFF RULE

I have a DIRECT derivative stake in the added text at **:40, :42, :49, :50** — `:40`/`:42` restate
my own finding that `device-guard.sh` matches command TEXT, and `:49`/`:50` recommend MY workaround
(compose with `Write`, `cat` it in). **My read of any finding landing in that range is conflicted.**

> **EVERY finding in that range goes to nq-c VERBATIM, with its FULL key set — not a selection.**
> The residual risk is not misquoting, it is CHOOSING WHICH TO QUOTE: projection-is-a-predicate
> applied to a hand-off. **If zero land there, I say zero. If three do, all three go over whole.**

My read is clean, and is mine to give, for: step-zero ordering · registry-as-authority ·
`bench whoami` · substitution-vs-evasion · **and the ts#432 reachability question** — whether a
reader following these recipes IN ORDER can still reach an unauthorised reflash leaving a board on a
firmware PR's build. That is a test of the document's EFFECT, not of my sentence.

## Hazard (claude-skills row)
Installed tree baseline BEFORE: HEAD `6c6d16f4af4b1fb839e05b43ac0d74f767be2787`, dirty **0**.
Detached worktree only (`/mnt/c/daqifi/wt/audit-sk231`, porcelain 0). Assert installed HEAD and
dirty count unchanged AFTER. Leave nothing dirty there — it auto-commits under the operator's name
within minutes.
