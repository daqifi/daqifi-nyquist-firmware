# PRE-REGISTERED FILTER — "invisible parks": MERGEABLE/CLEAN, unlabelled, audited-BLOCK or parked in prose

On disk BEFORE the query runs. nq-b, 2026-10-01. Read-only. Counts and row numbers only, **no
verdict**, and **no labels applied** (test-suite and claude-skills writes are both ask-first, and the
coordinator explicitly declined to have a lane apply park labels when the problem under study is
parks whose authority cannot be traced).

**Why the filter is pre-registered:** my `*349*` glob matched three agent hex ids and would have
inflated a round count by 167%. The filter is this finding's weakest joint, so it is fixed before
the data is seen.

## POPULATION

Open PRs in **both** repos, `--limit 200`.

⚠ **`gh pr list` defaults to 30 and under-reports silently.** Measured population:
`daqifi/daqifi-python-test-suite` **74** open, `cptkoolbeenz/claude-skills` **24** open = **98**.
A default-limit query would have returned 60 and looked complete.

## STAGE 2 — MERGEABLE/CLEAN

`mergeable == "MERGEABLE"` **and** `mergeStateStatus == "CLEAN"`.

⚠ Recorded from tonight's sk#231 work: **`CLEAN` means "no merge conflict" and nothing else.**
`statusCheckRollup` is `[]` in claude-skills (no PR CI), so CLEAN is not a review, audit or test
signal. It is used here only because it is the field a reader actually filters on.

## STAGE 3 — LABEL EXCLUSION, and the labels are NOT symmetric between the repos

Exclude a row carrying any of: `parked`, `blocked:operator-decision`, `blocked:audit-findings`.

**Enumerated the real label sets first, because excluding a label that does not exist is a silent
no-op:**

| label | test-suite | claude-skills |
|---|---|---|
| `parked` | ✅ | ✅ |
| `blocked:operator-decision` | ✅ | ✅ |
| `blocked:audit-findings` | ✅ | ⛔ **DOES NOT EXIST** |
| `needs-audit` | ✅ | — |
| `blocks-autonomy` | — | ✅ |

> ⛔ **`blocked:audit-findings` is absent from claude-skills entirely.** So the fleet's marker for
> an audit-blocked row **cannot be applied there** — sk#199 could not carry it even if someone
> wanted to. Excluding it in that repo filters nothing. **Reported as a fact about the label set,
> not as a recommendation to create it.**

## STAGE 4 — THE "BLOCKED OR PARKED IN PROSE" TEST, patterns named

Comments read with **`gh api repos/<R>/issues/<N>/comments --paginate`**, never
`gh pr view --json comments`, which **silently truncates**.

Two pattern families, **counted and reported separately, never merged into one verdict**:

```
AUDIT_BLOCK   gate.{0,6}BLOCK        (covers  gate: BLOCK  /  "gate":"BLOCK"  /  gate = BLOCK)
PARK_PROSE    PARKED | parked on | blocked on
```

⚠ **I deliberately do NOT match a bare `BLOCK`.** It over-fires on quoted gate output
(`BLOCKED: Run the adversarial pre-merge audit…`), on prose, and on my own ledger excerpts. A bare
match would turn this count into a projection of how often the word appears.

⚠ **`blocked on` will over-fire** on ordinary discussion ("this was blocked on CI"). So every hit is
reported with its **verbatim snippet** and the classification is left to the reader —
the same discipline as the corpus rationales.

## WHAT I AM NOT DOING

- **Not applying any label.** Both repos are ask-first.
- **Not ruling** on whether a row is "really" parked. A park statement in prose is a fact about the
  comment; whether it binds is a disposition.
- **Not treating an unmatched row as clean.** A row with no match may be parked in language neither
  pattern covers — the count is a **LOWER BOUND**, which is the only direction a lexical filter
  can honestly report after tonight (16 phrasings, 3 reference syntaxes, bare integers).
