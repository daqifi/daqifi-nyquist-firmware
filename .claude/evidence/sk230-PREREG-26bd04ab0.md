# PRE-REGISTERED acceptance checks — sk#230 audit, written BEFORE launching

Any artifact failing ANY of these is VOID and its verdict is discarded, per the ts#349 r4 precedent
where every health field was true and the run was worthless.

## Provenance (read FIRST, before any health field or finding)
```
repo          MUST == cptkoolbeenz/claude-skills      (NOT the 'ORG/REPO' placeholder)
base_sha      MUST == b98e8acd6dac5176629dca2869a37c23c4a64517
head_sha      MUST == 26bd04ab05256664a442e087c3b704eeea407245
covered_bytes MUST be within ~1% of 19621  (band 19425-19817; the 4-file, +220/-12 diff)
              measured in the SHELL with `wc -c`, never Python text mode -- this is a CRLF
              /mnt/c repo and `subprocess(text=True)` silently shrank the same diff by 34 bytes
⛔ covered_bytes == 0 is NOT A CLAIM -- REJECT, never compute a delta against it.
              `adversarial-audit.js:798` is `covered_bytes: r.covered_bytes || 0`, so an omitting
              producer yields a clean-looking zero that agrees with any empty diff. Same
              fail-open-to-clean default as `arbiterMissing`, sitting inside its own remedy.
```
**Discriminating controls, measured in advance — any of these means the wrong object was swept:**
```
4912792   the main-based diff (269 files) -> reviewed the whole autopush lineage
369515    my own nq-b ledger commit       -> the ts#349 r4 failure, exactly
73435     the ts#349 PR diff              -> a different repo entirely
```

## Health (only meaningful AFTER provenance)
```
arbiterModel        MUST be non-null        <- gate on THIS
arbiter block       MUST have 6 keys: decline_comment_markdown, dispositions, summary,
                    treadmill, unsound_refutations, verdict.  5 = key withheld; 0 = no arbiter
blindLegRan         report it; it is NOT a claim about WHAT was swept
arbiterMissing/Degraded  DO NOT GATE ON THESE -- defaults
engine              MUST be codex (not 'both')
```

## Findings
Severity ONLY from `.arbiter.dispositions[].agreed_severity`, joined on RATIONALE CONTENT
(never `index` -- `rawFindings` is an integer, so the index join is impossible).
Key-count each finding: <=6 = summary, 15+ = real record.

## ⛔ WHAT I CANNOT BRIEF, AND MUST APPLY MYSELF
The per-row baseline split (3 live rows vs 2 proposal rows) **cannot be conveyed to the auditor** --
there is NO brief parameter, and the only two free-text args are SUPPRESSION channels. So the audit
sees ONE base. **The live-vs-proposal standard is reader-side discipline applied by me at
artifact-reading time**, and I will not report it as briefed.

Passing NO `fixed` and NO `dispositions`, deliberately: `fixed` is "hunt BEYOND these", and the
PR's own claim is that the three live rows are closed -- putting that in `fixed` would suppress the
hunt on exactly what this audit exists to check.
