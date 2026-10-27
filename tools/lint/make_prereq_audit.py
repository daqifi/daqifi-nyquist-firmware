#!/usr/bin/env python3
"""Audit Makefile guard prerequisites that expand to NOTHING.

THE DEFECT
    make expands a PREREQUISITE list when it READS the rule. A variable
    defined later in the file is empty AT THAT POINT, so the dependency
    silently disappears: an edit to the guarded source reuses the stale
    binary and the guard never runs.

WHY THE OBVIOUS CHECK IS A CLEAN-BILL GENERATOR
    `make -np`'s VARIABLE DUMP happens AFTER the whole file is parsed, so it
    reports the variable as a perfectly good path while the rule it feeds has
    no such dependency. Measured on fw#904 before the fix:

        SETTINGS_SRC_904 := ../../firmware/src/services/daqifi_settings.c   <- "fine"
        run_904_tests: test_904_cal_pair_tear.c test_framework.h \
                       ../../firmware/src/services/SCPI/SCPIADC.c Makefile   <- ABSENT

    The RECIPE's uses are unaffected too (recipe lines expand at EXECUTION
    time, after the parse), so every grep has the right path and every guard
    that DOES run is correct. A correct recipe with an absent dependency.
    Only the expanded RULE line is evidence.

WHAT PRODUCES A FINDING -- ONE TEST, NOT TWO
    Definition order, read off this file: a prerequisite variable that is
    defined AFTER the rule using it, or never assigned in the file at all.
    Both mean the rule's prerequisite list was empty when make read it.

    ⚠ AN EARLIER VERSION OF THIS DOCSTRING CLAIMED TWO LEGS -- ordering, plus
    "does make's own database show the rule's prerequisites naming the file?"
    -- and said "neither alone is enough". That was not what the code did. The
    expanded database is read, and it is used ONLY as the self-test's
    independent cross-check (proving the modelled defect is real in make's own
    terms, not just in this script's model). No finding has ever come from it.
    The claim was corrected rather than the code extended, because this tool
    was being given a permanent home and a CI gate, and an overclaiming
    description is worse institutionalised than fixed.

    Making the expanded database a second finding source -- comparing each
    rule's REAL prerequisites against the files its recipe greps -- is a
    worthwhile follow-up and deliberately not done here.

USAGE
    python3 tools/lint/make_prereq_audit.py               # tests/host/Makefile
    python3 tools/lint/make_prereq_audit.py [Makefile]    # any other one
    python3 tools/lint/make_prereq_audit.py --self-test   # prove it can FIRE

⚠ RUN --self-test BEFORE TRUSTING A ZERO. The original positive control was
fw#904's real gap, and that gap is now FIXED -- so a clean run today proves
nothing on its own. The self-test carries its own two-arm control: a rule it
MUST flag and a correct twin it MUST NOT. `.github/workflows/make-prereq.yml`
runs the self-test as its OWN step, BEFORE the audit, for exactly this reason:
a green from an unvalidated instrument is not a result.

⛔ WHAT THIS DOES **NOT** COVER, stated because a green here is otherwise easy
to over-read. This audits whether a rule's prerequisite list NAMES the guarded
source -- "can this guard fire at all?". It does not audit whether a rule
declares the MAKEFILE itself, which is the separate "is this guard ever
RE-EVALUATED?" question: a rule missing $(RECIPES) does not re-run when the
guard's own recipe is edited.

That gap is live on `main` as this lands -- `run_947_tests` is the only one of
the 33 `run_*` rules omitting the Makefile from its prerequisites (issue #1170
item (a)) -- and this checker reports 0 findings against it, correctly, because
it is a different property. Extending the audit to that class is a deliberate
follow-up and NOT folded in here: it would make this fail on `main`, which
would drag #1170's fix into what is meant to be a permanent home for an
existing tool. See #1170.
"""
import os
import re
import subprocess
import sys
import tempfile

ASSIGN_RE = re.compile(r"^\s*([A-Za-z_][A-Za-z0-9_]*)\s*(?::=|\?=|\+=|=)")
RULE_RE = re.compile(r"^([^\t#=][^:=]*?):(?!=)([^=].*)?$")
VAR_REF_RE = re.compile(r"\$\(([A-Za-z_][A-Za-z0-9_]*)\)")


def audit(makefile, workdir):
    lines = open(makefile, encoding="utf-8", errors="replace").read().splitlines()

    # the finding source: first definition line of each variable
    def_line = {}
    for i, ln in enumerate(lines, 1):
        m = ASSIGN_RE.match(ln)
        if m and m.group(1) not in def_line:
            def_line[m.group(1)] = i

    # make's own expanded rules. NOT a finding source -- see the docstring.
    # This is the self-test's independent cross-check: it shows the modelled
    # defect in make's OWN terms, so a pass is not just this script agreeing
    # with itself. (The variable dump remains no evidence at all.)
    db = subprocess.run(
        ["make", "-np", "-f", os.path.basename(makefile)],
        cwd=workdir, capture_output=True, text=True,
    ).stdout
    expanded = {}
    for ln in db.splitlines():
        m = re.match(r"^([A-Za-z0-9_./$()-]+):(?!=)\s*(.*)$", ln)
        if m and not ln.startswith("\t"):
            expanded.setdefault(m.group(1), m.group(2))

    findings, rules = [], 0
    for i, ln in enumerate(lines, 1):
        if ln.startswith("\t") or not ln.strip() or ln.lstrip().startswith("#"):
            continue
        if ASSIGN_RE.match(ln):
            continue
        m = RULE_RE.match(ln)
        if not m:
            continue
        target_txt, prereqs = m.group(1), (m.group(2) or "")
        if not prereqs.strip():
            continue
        rules += 1
        tgt = target_txt.strip()
        for var in VAR_REF_RE.findall(prereqs):
            dl = def_line.get(var)
            late = dl is not None and dl > i
            undef = dl is None
            if late or undef:
                findings.append(
                    dict(line=i, target=tgt, var=var, def_line=dl,
                         reason="defined AFTER this rule" if late
                                else "never assigned in this file")
                )
    return rules, findings, expanded


def report(makefile, workdir):
    rules, findings, _ = audit(makefile, workdir)
    print(f"makefile           : {makefile}")
    print(f"rules w/ prereqs   : {rules}")
    print(f"findings           : {len(findings)}")
    for f in findings:
        print(f"  line {f['line']:>5}  {f['target']:<26} ${{{f['var']}}}"
              f"  def@{f['def_line']}  -- {f['reason']}")
    if not findings:
        print("  (none)")
        print("  ⚠ a zero is only evidence if --self-test passes; the original")
        print("    real positive control (fw#904) has since been fixed.")
    print()
    print("CONFIRM EACH GUARDED FILE BY THE RULE LINE, NOT THE VARIABLE:")
    print("    make -np | grep '^<target>:'    # must NAME the guarded source")
    return len(findings)


SELFTEST_MAKEFILE = """\
EARLY_SRC := early.c

ok_target: base.c $(EARLY_SRC)
\t@true

bad_target: base.c $(LATE_SRC)
\t@true

LATE_SRC := late.c
"""


def self_test():
    """Two arms. One MUST fire, one MUST NOT. A checker that has never been
    shown to fire is not an instrument."""
    with tempfile.TemporaryDirectory() as d:
        mk = os.path.join(d, "Makefile")
        open(mk, "w").write(SELFTEST_MAKEFILE)
        for n in ("base.c", "early.c", "late.c"):
            open(os.path.join(d, n), "w").write("")
        _, findings, expanded = audit(mk, d)

        flagged = {(f["target"], f["var"]) for f in findings}
        must_fire = ("bad_target", "LATE_SRC") in flagged
        must_not = ("ok_target", "EARLY_SRC") not in flagged

        print("--- self-test (two arms) ---")
        print(f"  POSITIVE arm: bad_target/$(LATE_SRC) flagged   : "
              f"{must_fire}  {'PASS' if must_fire else 'FAIL'}")
        print(f"  NEGATIVE arm: ok_target/$(EARLY_SRC) NOT flagged: "
              f"{must_not}  {'PASS' if must_not else 'FAIL'}")
        print()
        print("  and make agrees the defect is real, not just our model:")
        print(f"    ok_target  -> {expanded.get('ok_target', '<absent>')}")
        print(f"    bad_target -> {expanded.get('bad_target', '<absent>')}")
        print("    (late.c is MISSING from bad_target while early.c is present)")
        ok = must_fire and must_not
        print()
        print("SELF-TEST " + ("PASS" if ok else "FAIL"))
        return 0 if ok else 1


# The one Makefile in this repo that carries guard rules. Defaulted so CI (and
# anyone at the repo root) invokes the checker with no arguments and cannot
# point it at the wrong file by typo -- a checker aimed at a Makefile with no
# guard rules reports 0 findings and looks like a pass.
DEFAULT_MAKEFILE = os.path.join("tests", "host", "Makefile")


if __name__ == "__main__":
    if "--self-test" in sys.argv:
        sys.exit(self_test())
    path = os.path.abspath(sys.argv[1] if len(sys.argv) > 1
                           else DEFAULT_MAKEFILE)
    if not os.path.isfile(path):
        print(f"ERROR: no such Makefile: {path}")
        print("       Run from the repository root, or pass the path.")
        sys.exit(2)
    sys.exit(1 if report(path, os.path.dirname(path)) else 0)
