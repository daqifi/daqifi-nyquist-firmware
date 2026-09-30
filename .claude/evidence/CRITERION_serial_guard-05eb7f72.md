# PRE-REGISTERED CRITERION — serial-comparison completeness guard

Written to disk BEFORE opening any file. nq-b, 2026-09-30. Read-only survey; nothing fixed.

## The defect shape being screened for

`ReliableSCPI.query` returns the FIRST FRAGMENT once `in_waiting == 0` after its 50 ms gap. So a
split reply yields a **truncated-but-parseable** serial. Comparing that against a **complete** reply
from another transport produces a **false wrong-peer FAIL against a healthy board.**
Canonical guard: `TcpControl.verify_serial`.

## UNIT OF JUDGEMENT

**The COMPARISON SITE, not the file.** A file is scored once per comparison site that *reaches a
verdict*; a file with several sites is scored by its **worst** site. This is the whole point of the
re-survey: the prior bound scored "the file mentions a guard anywhere."

## What is a COMPARISON THAT REACHES A VERDICT

A serial value is compared (`==`, `!=`, `in`, `startswith`, a regex fullmatch, a set/dict lookup)
and the outcome can cause a **FAIL, abort, skip, `sys.exit`, `_fail(...)`, assertion, or a
PASS/FAIL row**. A comparison used only for a log line, a progress message or an informational
print is NOT a verdict site and is recorded separately as INFORMATIONAL.

## GUARDED — any ONE of these, textually present and dominating the comparison

1. `TcpControl.verify_serial` (or a wrapper that calls it) supplies the value.
2. A **length** assertion on the serial before comparing — e.g. `len(s) == 16`, a `{16}` regex
   quantifier, or a full-SHA-style exact-width check.
3. A **framing/terminator** check establishing the reply ended — prompt (`DAQIFI>`), `\r\n`
   terminator, or an explicit "reply complete" predicate.
4. A **strict single-candidate reader** that rejects partial or multiple candidates
   (`read_int_strict`-family; "exactly one bare token" readers).
5. A **re-query-until-stable / retry-on-mismatch** loop, so one truncated read cannot decide.

## NOT A GUARD — explicitly, because these are the near-misses

- `'DAQiFi' in idn` or any **substring/presence** test — passes on a truncated reply.
- `idn.split(',')[2]` or any **positional parse** — parses a truncated reply without complaint.
- A regex that can match a **PREFIX** (`search`, unanchored, or `*`/`+` without an exact width).
- `.strip()` / whitespace normalisation — cleans a fragment without detecting it.
- A guard present **elsewhere in the file** but not dominating this comparison. ⛔ This is the exact
  over-scoring the prior bound suffered from.
- A guard on a **different transport's** read when the compared value comes from the unguarded one.

## VERDICT BUCKETS (one line per file, worst site)

- **UNGUARDED** — a verdict-reaching comparison with no dominating guard. Quote the condition.
- **GUARDED** — a dominating guard present. Name which of rules 1–5 and quote it.
- **INFORMATIONAL-ONLY** — serial compared but no verdict depends on it.
- **NO-COMPARISON** — the serial is read/printed but never compared.
- **UNDECIDABLE** — the comparison path cannot be settled without executing it. Its own bucket; not
  folded into either direction.

## POPULATION RULE

**A grep is a candidate list, not a population.** I enumerate candidates by several independent
patterns, then OPEN every candidate. A file is only scored after being read. Files reached by no
pattern are, by construction, outside my measurement — I will say so rather than implying coverage.

## CITATION RULE

**Cite the greppable PREDICATE, not the address.** Any line number is qualified by the commit it was
read at. Checkouts differ in age across lanes.
