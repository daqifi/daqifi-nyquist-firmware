# PRE-REGISTERED CRITERION — is "delete the five `_drain_errors` copies" safe?

On disk BEFORE opening any file. nq-b, 2026-09-30, read-only. Test-suite at a commit named in the report.

## POPULATION — "five" is a BRACKET, not the population

Five populations were grep-sized in this session and **all five were wrong in at least two
directions**, two of them mine. So I enumerate by several independent shapes and treat every hit as a
candidate to OPEN:

1. `def _drain_errors` — the named copy.
2. `def .*drain` — any locally-defined drainer under another name (the invisible-sixth risk).
3. bodies that loop `SYST:ERR?` / `SYSTem:ERRor?` without defining a function — an INLINE drainer,
   which no `def` pattern can see.
4. importers of the harness `drain_errors` / `drain_confirmed` — the comparison group.

**A file reached by no pattern is outside my measurement and I will say so rather than imply coverage.**

## Q1 — DOES A CALLER TEST THE RESULT? (the actual defect)

Per call site of a LOCAL drainer:
- **TESTS-RESULT** — the return value is bound and then branched on, compared, truthiness-tested,
  indexed, iterated, or passed to something that decides. ⛔ Since every local copy has **zero
  `return` statements**, it returns `None` unconditionally, so any such branch **cannot fire** — an
  assertion that cannot fail. Quote the predicate.
- **IGNORES-RESULT** — called for effect; return unbound or discarded. **Scored FINE.** Deleting the
  copy and calling the harness changes nothing observable here.
- **UNDECIDABLE** — the value flows somewhere I cannot settle by reading. Own bucket.

## Q2 — WOULD THE HARNESS VERSION SATISFY THE SITE? (the relocation question)

The harness `drain_errors` **RETURNS codes** (and `[ERR_UNREADABLE]`, not `[]`, on an unreadable
read). So a swap makes a site that currently receives `None` start receiving a **list**.
- **SAFE-SWAP** — nothing downstream inspects the value, so `None` → `[...]` is unobservable.
- ⛔ **MEANING-CHANGES** — a site whose logic would newly take a branch it never took, because a
  falsy `None` becomes a possibly-truthy list. *This is the `idn_serial` relocation shape: the remedy
  moves the failure into the consumer instead of closing it.*
- **BREAKS** — a site that would raise or mis-execute.

## Q3 — IS THE FIX ALREADY IN THE TREE?

`drain_confirmed` is used in 3 of 152 files. **Open one and decide between:**
- **CANONICAL** — it is the pattern the five should adopt; the remedy already exists and the job was
  to find it, as with `complete_lines()`.
- **DIFFERENT PROBLEM** — it solves something adjacent, so citing it as the remedy would be the
  "guarded against the wrong tear" error.

## Q4 — DOES ANY COPY DEPEND ON RETURNING `None`?

Explicitly asked, because dependence-by-contrast was invisible until asked on `idn_serial`:
a site asserting the drainer returns nothing, or a test whose subject IS the local copy's behaviour.

## BOUNDS AND CITATION

Report what I did not cover. Quote greppable predicates; any line number is qualified by its commit.
**`UNDECIDABLE` stays its own bucket and is never folded into a direction.**
