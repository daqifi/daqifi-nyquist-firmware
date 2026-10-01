# PRE-REGISTERED CRITERION — where does a test double's notion of "valid" come from?

On disk BEFORE enumerating or opening any double. nq-b, 2026-09-30. Read-only survey.
Population commits named in the report.

## THE QUESTION — and it is NOT "does a control assert a literal"

The original framing was *"does any control assert a literal command string?"* **That question is
answered YES by a confirmed defective case and is therefore useless.** ts#326's negative control
required the exact malformed string its code sent: **the literal in the test and the literal in the
code were ONE AUTHORED BELIEF WRITTEN TWICE**, and a verbatim-recording double made positive,
negative and mutation controls all confirm that two copies of one belief agree.

> **THE QUESTION: does the double's notion of "valid" come from somewhere the TEST AUTHOR DID NOT
> WRITE?**

## AXIS 1 — VALIDITY SOURCE (one value per double)

- **AUTHOR** — the double accepts whatever the author wrote; validity is a restatement of the
  expected string. ⛔ Every syntax defect is invisible to every control built on it.
- **GRAMMAR (weak external)** — the double encodes the *shape* rather than the string, so a
  malformed command fails structurally. Example in-tree: `ch_s, val_s = args.split(',')` raises on a
  missing comma.
- **AUTHORITY (strong external)** — validity is derived from the registered pattern table
  (`SCPIInterface.c`) or an equivalent artefact the author did not write. **Expected population: 0.
  82 files CITE `SCPIInterface` in prose; a prose citation is the author's reading, not a check.**
- **UNDECIDABLE** — cannot be settled by reading. Its own bucket, never folded into a direction.

## AXIS 2 — WHAT HAPPENS TO AN UNRECOGNISED COMMAND

- **RAISES** — refuses, loudly.
- **BUCKETED-AND-ASSERTED** — collected and a check asserts the bucket empty. The bucket is only
  load-bearing if something READS it.
- ⛔ **BUCKETED-UNREAD** — collected and never asserted. **This is the whole defect**: the bucket
  looks like rigour and does nothing.
- **RECORDED SILENTLY** — appended to `sent` and indistinguishable from a valid command.

## AXIS 3 — IS A MISS LOUD OR SILENT? (the triage rule, and it decides severity)

> **A vacuous check is dangerous only if it can GO vacuous without a companion check failing.**

Measured precedent: the SAME case-sensitivity was a **HIGH** in one double (miss -> unread bucket ->
silent) and a **nuisance** in another (miss -> every channel reads GARBLED -> three checks fail
loudly). **Only the direction distinguished them.** So I report loudness per double and never infer
severity from the validity source alone.

## THE TWO-SIDED CRITERION — applied to every proposed fix

> **A looser double HIDES defects; a stricter one MANUFACTURES them. The double must MATCH the
> device's acceptance set, not bound it from one side.**

A divergence is reported with its DIRECTION. A "fix" that tightens a double past firmware is a
false-failure generator and is **not** an improvement — one such finding was already refuted on
ts#326 (`int(float(...))` mirrors firmware's double->int narrowing, so refusing `2.7` would reject a
command the device accepts and acts on).

**And the method matters:** the `#877` divergences were found by **enumerating the device's behaviour
per command**, not by reasoning about the double's code. Reasoning about the double finds the
author's intent; enumerating the device finds the mismatch.

## POPULATION RULE — and why main-only is a FALSE CLEAN

⛔ **`_RecordingSCPI` has ZERO hits on `origin/main`.** Doubles are authored on PR branches and the
hazard is in new work. **A main-only sweep reports clean across the entire corpus.** So the
population is `origin/main` **plus every open PR head**, and the report states which refs were
covered and which were not.

A grep is a candidate list, not a population: enumerate by several independent shapes
(`class .*Fake`/`Recording`, `def command(self, cmd`, `self.sent.append`, `def query(self, cmd`) and
open every candidate. A double reached by no pattern is outside the measurement and I will say so.

## INSTRUMENT CALIBRATION — before any zero is believed

Two **confirmed positives** must be found by the instrument or the instrument is dead:
1. `test_980_dac7718_all_channel_latch.py` **pre-fix** — `_RecordingSCPI` recording verbatim.
2. `test_1018_982_loss_summary.py` (ts#349) — `_RecordingSCPI`, byte-identical records-only shape.

**If the sweep does not independently find both, every zero it reports is meaningless.**

## COUNTS

**Control counts are reported ONLY if the counter is calibrated against a known value.** A high
control count on a permissive double is **actively misleading** — ts#326 had 13 checks and 5 mutants
all green on a command the device rejects. So a count is a finding only when paired with the validity
source, never alone.

## PROOF SHAPE FOR ANY FIX I PROPOSE

**Graded mutants**, not one all-or-nothing deletion: three variants admitting 5 / 3 / 2 identify
*which property each lost*, where a single mutant shows only that the line matters.
**And print the applied-flag** — a mutation that did not apply prints green and is the
strongest-looking evidence available while being worth nothing.
