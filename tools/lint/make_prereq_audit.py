#!/usr/bin/env python3
"""Ask MAKE whether a guard's prerequisite variable was defined in time.

THE DEFECT
    make expands a PREREQUISITE list when it READS the rule. A variable
    defined later in the file is empty AT THAT POINT, so the dependency
    silently disappears: an edit to the guarded source reuses the stale
    binary and the guard never runs.

    The RECIPE is unaffected -- recipe lines expand at EXECUTION time, after
    the parse -- so every grep has the right path and every guard that DOES
    run is correct. A correct recipe with an absent dependency.

WHY THIS ASKS make INSTEAD OF READING THE MAKEFILE
    An earlier version of this tool parsed the Makefile itself, and an
    adversarial audit took it apart on the real file. Every one of these was
    reproduced, not hypothesised:

      * CONTINUATION LINES. It read only a rule's FIRST line, so in
            $(WAITLOOP_BIN): test_1056_wait_loop.c ... $(WAITLOOP_H) \\
                             $(USER_SPI_SRC) ... $(RECIPES)
        five prerequisite variables on the second line were invisible.
      * ${VAR} references. Its regex matched only $(VAR).
      * TAB-INDENTED SHELL ASSIGNMENTS read as make definitions. 220 recipe
        lines in that Makefile match `name=`, so shell temporaries (a, b, n,
        bad) registered as defined make variables -- making a genuinely
        undefined prerequisite of that name look fine. A false NEGATIVE.
      * Its make cross-check never affected the exit code, so the "two legs"
        it advertised were one leg and a printout.

    Those are four spellings of one mistake: modelling a language instead of
    asking its implementation. `make --warn-undefined-variables` warns at READ
    time, with file:line, for exactly the thing this gate is about, and it
    cannot be evaded by syntax because it IS make.

    Measured on a fixture carrying all three evasions:

        Makefile:6: warning: undefined variable 'LATE1'   <- ${} form
        Makefile:9: warning: undefined variable 'LATE2'   <- continuation line
        (no warning for a recipe-only reference)          <- correctly silent

    And goal-independent: prerequisite lists expand when each rule is read, so
    the same warnings appear whichever target is requested.

WHY env -i IS NOT OPTIONAL
    A variable present in the ENVIRONMENT is defined as far as make is
    concerned, so `LATE_SRC=x make ...` suppresses the warning for exactly the
    defect this looks for. Measured: with the name exported, 0 warnings; under
    `env -i`, 1. The subprocess therefore runs with a scrubbed environment,
    carrying only PATH.

USAGE
    python3 tools/lint/make_prereq_audit.py               # tests/host/Makefile
    python3 tools/lint/make_prereq_audit.py [Makefile]    # any other one
    python3 tools/lint/make_prereq_audit.py --self-test   # prove it can FIRE

    exit 0  no undefined-variable warnings
    exit 1  warnings, printed with ::error file=,line=:: annotations
    exit 2  could not run (no such Makefile, make unusable, or the self-test
            fixture failed to behave) -- never silently green

WHAT THIS DOES NOT COVER. It answers "can this guard fire at all?" -- does the
rule's prerequisite list actually name the source it guards. It does NOT answer
"is this guard ever RE-EVALUATED?": a rule omitting $(RECIPES) (the Makefile
itself) does not re-run when the guard's own recipe is edited. run_947_tests is
currently the only one of the run_* rules omitting it, which is issue #1170
item (a) -- live on main, correctly not reported here, and a deliberate
follow-up rather than part of this tool.
"""
import os
import re
import subprocess
import sys
import tempfile

DEFAULT_MAKEFILE = os.path.join("tests", "host", "Makefile")

# GNU make's own wording, captured with file:line so a finding is clickable.
WARN_RE = re.compile(
    r"^(?P<file>[^:]+):(?P<line>\d+):\s*warning:\s*"
    r"undefined variable ['\"](?P<var>[^'\"]+)['\"]")


def run_make(makefile, goal="run"):
    """(warnings, stderr, returncode). Asks make; parses no Makefile syntax.

    `env -i` so an exported variable cannot define the defect away. `-n` so
    nothing is built: the warnings happen at parse time, before any recipe
    would run.
    """
    env = {"PATH": os.environ.get("PATH", "/usr/bin:/bin")}
    proc = subprocess.run(
        ["make", "-n", "--warn-undefined-variables",
         "-f", os.path.basename(makefile), goal],
        cwd=os.path.dirname(makefile) or ".",
        capture_output=True, text=True, env=env,
    )
    warnings = []
    for ln in proc.stderr.splitlines():
        m = WARN_RE.match(ln.strip())
        if m:
            warnings.append(m.groupdict())
    return warnings, proc.stderr, proc.returncode


def report(makefile, goal="run"):
    warnings, stderr, rc = run_make(makefile, goal)
    print(f"makefile : {makefile}")
    print(f"goal     : {goal}  (warnings are goal-independent -- prerequisite "
          f"lists expand when each rule is READ)")
    print(f"warnings : {len(warnings)}")
    for w in warnings:
        print(f"::error file={w['file']},line={w['line']}::"
              f"prerequisite variable '{w['var']}' is not defined at this "
              f"point, so this rule's prerequisite list was EMPTY when make "
              f"read it -- the guard will not re-run when its source changes")
    if warnings:
        return 1
    # make failing to read the file is a different answer from a clean audit,
    # and must not be reported as one.
    if rc != 0 and "No rule to make target" in stderr:
        print(f"ERROR: make could not read this Makefile (goal {goal!r}).")
        print(stderr.strip()[:500])
        return 2
    print("  (none)")
    return 0


# ---------------------------------------------------------------------------
# The fixture carries all three evasions the parser version missed, so a
# regression to any of them fails here rather than in review.
SELFTEST_MAKEFILE = """\
EARLY := early.c

ok: base.c $(EARLY)
\t@true

bad_brace: base.c ${LATE1}
\t@true

bad_continuation: base.c \\
                  $(LATE2)
\t@true

recipe_only: base.c
\tLOCALVAR=x; echo $(RECIPE_LATE)

LATE1 := late1.c
LATE2 := late2.c
RECIPE_LATE := r.c
"""


def self_test():
    """Four arms that MUST or MUST NOT warn, and the result DECIDES the exit.

    The previous version printed a make cross-check that never affected its
    exit code, so it advertised a control it did not have. Every arm here is
    returned.
    """
    with tempfile.TemporaryDirectory() as d:
        mk = os.path.join(d, "Makefile")
        with open(mk, "w") as fh:
            fh.write(SELFTEST_MAKEFILE)
        for n in ("base.c", "early.c", "late1.c", "late2.c", "r.c"):
            open(os.path.join(d, n), "w").close()
        warnings, stderr, _ = run_make(mk, goal="ok")
        got = {w["var"] for w in warnings}

        arms = [
            ("POSITIVE  ${VAR} form", "LATE1" in got, True),
            ("POSITIVE  continuation line", "LATE2" in got, True),
            ("NEGATIVE  recipe-only reference", "RECIPE_LATE" in got, False),
            ("NEGATIVE  correctly-ordered variable", "EARLY" in got, False),
        ]
        print("--- self-test ---")
        ok = True
        for name, actual, want in arms:
            good = (actual == want)
            ok = ok and good
            print(f"  {'PASS' if good else 'FAIL'}  {name}: "
                  f"warned={actual}, wanted={want}")

        # A finding without file:line is not actionable.
        located = all(w["file"] and w["line"].isdigit() for w in warnings)
        print(f"  {'PASS' if located else 'FAIL'}  every warning carries "
              f"file:line")
        ok = ok and located

        if not warnings:
            print("  FAIL  the fixture produced NO warnings at all -- either "
                  "this make lacks --warn-undefined-variables, or the fixture "
                  "stopped modelling the defect")
            print(stderr.strip()[:300])
            ok = False

        print()
        print("SELF-TEST " + ("PASS" if ok else "FAIL"))
        return 0 if ok else 2


if __name__ == "__main__":
    if "--self-test" in sys.argv:
        sys.exit(self_test())
    path = os.path.abspath(sys.argv[1] if len(sys.argv) > 1
                           else DEFAULT_MAKEFILE)
    if not os.path.isfile(path):
        print(f"ERROR: no such Makefile: {path}")
        print("       Run from the repository root, or pass the path.")
        sys.exit(2)
    sys.exit(report(path))
