# PRE-REGISTERED CRITERION — are the 33 NEWLY BLOCKED lines harvest artifacts or genuine pushes?

On disk BEFORE reading `newblocked.txt`. nq-b, 2026-10-01. Read-only. **Counts and verbatim lines
only; the disposition is conv-fw's and the coordinator's.**

## WHY THIS ONE CLAIM GETS CHECKED, STATED STRUCTURALLY AND NOT AS AN ACCUSATION

Everything else in conv-fw's A/B is model-free — old-rc vs new-rc on the same fixture. **"32 of 33
are harvest artifacts" is a judgement about the corpus, not an output of the A/B**, and it is the
single claim that converts a **3.0% false-block rate into one line.**

conv-fw themselves noticed that **all five corrections they made to their instrument moved the
answer the same way**, toward *"fail-closed is cheap."* **This classification moves in that same
direction.** That is the reason for an independent read — the same reason their self-suspicion makes
the rest of the result trustworthy.

## POPULATION — verified, and `wc -l` was wrong about it

```
newblocked.txt   wc -l = 32   grep -c '' = 33   <- last byte is 0x32 ('2'), NOT a newline
push_decoded.txt wc -l = 1097 grep -c '' = 1097 <- the stated population, confirmed both ways
push_uniq.txt    1148 lines   <- MORE than the population; uniq'd != the 1097. Not used.
```
**33 rows confirmed.** ⚠ Reporting `wc -l`'s 32 against a claim of 33 would have manufactured a
discrepancy out of a missing trailing newline.

## THE DISCRIMINATOR IS MECHANICAL, AND IT IS A PARSE — NOT A JUDGEMENT ABOUT PLAUSIBILITY

The harvest pulled command text out of transcript JSON, so the expected artifact is a **fragment**:
cut mid-token, unbalanced quotes, embedded literal `\n`, or prose that merely mentions a push.

> **TEST 1 — `bash -n` on the line.** `-n` **parses and does not execute.** A line that is not
> syntactically valid shell cannot have been an executed command, so a parse failure is
> **HARVEST_ARTIFACT** on mechanical grounds, with no appeal to how plausible it looks.

> **TEST 2 — does a `git push` survive quote-stripping?** Strip single- and double-quoted segments,
> then look for `git push` in the remainder. A push that exists only *inside* a quoted string is
> text being passed to something else (a `-m` message, a heredoc body, a doc string), not a push
> being run. **Survives ⇒ GENUINE candidate. Does not survive ⇒ HARVEST_ARTIFACT (quoted).**

> **TEST 3 — for GENUINE rows only: is it a FIRST PUSH, i.e. the thing the gate exists to catch?**
> Mechanical markers: `-u`, `--set-upstream`, or an explicit `HEAD:refs/heads/<name>` /
> `HEAD:<branch>` destination. Absent those, it is a push to a presumably-tracked branch, which the
> gate is **not** meant to gate.

⛔ **SAFETY: nothing here executes a corpus line.** `bash -n` parses only. No line is ever run, and
no `git push` is ever invoked. The corpus is read as text.

## PRE-COMMITTED REPORTING RULES

1. **Every one of the 33 is reported with its classification AND a verbatim excerpt**, so the reader
   can disagree with me per row. A count alone would be a projection.
2. **Three buckets, not two:** `HARVEST_ARTIFACT`, `GENUINE`, and **`UNDECIDABLE`** — a row that
   parses and whose push survives stripping but which I cannot tell apart from a fixture. Folding
   UNDECIDABLE into either bucket is the error this third bucket exists to prevent.
3. **I predict the direction of my own error.** If my classification disagrees with conv-fw's, the
   likely cause is **my quote-stripping being cruder than a real shell**, which would push rows
   toward GENUINE and *inflate* the false-block rate. So a result of "more genuine than they said"
   is the one I must distrust and re-read by hand, not the one I should report as a catch.
4. **The headline number is the GENUINE count**, and it is reported as a **floor on artifacts /
   ceiling on genuine** only after rule 3's hand re-read.

## WHAT WOULD CONFIRM conv-fw

32 of 33 failing TEST 1 or TEST 2 — i.e. the artifacts are artifacts for a *mechanical* reason that
has nothing to do with whether a gate should have blocked them.
