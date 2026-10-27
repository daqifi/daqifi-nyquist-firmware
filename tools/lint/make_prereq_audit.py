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

THE TWO WAYS ASKING make CAN STILL GO WRONG, AND WHY BOTH ARE REFUSALS
    Delegating to make removes the parser's blind spots but introduces two of
    its own, both of which used to produce a GREEN audit:

      * make STOPPING EARLY. A warning list is only a verdict on the whole
        file if make finished reading it. Measured on a fixture whose line 2
        was a `missing separator`: make died there, never read the genuinely
        undefined prerequisite on line 4, and this tool printed
        `warnings : 0 / (none)` and exited 0 -- a clean bill on a file make
        could not read. Only ONE non-zero exit was handled before (the one
        whose stderr happened to say "No rule to make target"), so every other
        way of failing was reported as cleanliness. ANY non-zero exit is now a
        refusal. The benign case is unaffected: the audited Makefile exits 0
        with 0 stderr lines, so this carves out nothing.
      * $(value X). `value` yields a variable's unexpanded text WITHOUT
        referencing it, so --warn-undefined-variables says nothing at all.
        Measured on one fixture: `ok: base.c $(value LATE)` warns 0 times,
        while the same rule written `${LATE}` warns once. A prerequisite list
        built that way is just as empty and completely invisible here. So a
        file using it gets NO VERDICT rather than a green one -- including via
        an `include`, which is why the scan follows those and refuses any it
        cannot follow by text alone. The audited Makefile contains 0 of both.

    Both are the same shape as the parser's: an answer that looks like a
    measurement but is the absence of one. The rule here is that this tool has
    three outcomes, not two -- clean, findings, and NO VERDICT.

WHAT THIS DOES NOT COVER. It answers "can this guard fire at all?" -- does the
rule's prerequisite list actually name the source it guards. It does NOT answer
"is this guard ever RE-EVALUATED?": a rule omitting $(RECIPES) (the Makefile
itself) does not re-run when the guard's own recipe is edited. run_947_tests is
currently the only one of the run_* rules omitting it, which is issue #1170
item (a) -- live on main, correctly not reported here, and a deliberate
follow-up rather than part of this tool.
"""
import contextlib
import io
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

# $(value X) / ${value X}: reads X's text without referencing X, so
# --warn-undefined-variables is silent whether or not X is defined yet.
VALUE_RE = re.compile(r"[$][({]\s*value\b")

# include / -include / sinclude, with whatever it names.
INCLUDE_RE = re.compile(r"^\s*[-s]?include\s+(?P<what>.+?)\s*$")


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


def gather_sources(makefile):
    """(files whose TEXT must be scanned, includes that cannot be followed).

    An `include` can carry the $(value ...) evasion just as well as the file
    itself, so the scan follows them. An include naming a variable, or a file
    that is unreadable, CANNOT be followed by text alone -- and skipping one
    silently would reopen exactly the hole this scan closes, so it is returned
    as unresolved for the caller to refuse on.
    """
    root = os.path.dirname(os.path.abspath(makefile)) or "."
    pending, seen, files, unresolved = [os.path.abspath(makefile)], set(), [], []
    while pending:
        path = pending.pop()
        if path in seen:
            continue
        seen.add(path)
        try:
            with open(path, "r", errors="replace") as fh:
                text = fh.read()
        except OSError as exc:
            unresolved.append((path, f"unreadable: {exc.strerror}"))
            continue
        files.append((path, text))
        for lineno, ln in enumerate(text.splitlines(), 1):
            m = INCLUDE_RE.match(ln)
            if not m:
                continue
            what = m.group("what")
            if "$" in what:
                unresolved.append(
                    (path, f"line {lineno}: include names a variable "
                           f"({what!r}) -- not resolvable by text"))
                continue
            for token in what.split():
                pending.append(os.path.join(root, token))
    return files, unresolved


def find_evasions(makefile):
    """[(file, line, text)] for every $(value ...), plus unfollowable includes.

    Returns (hits, unresolved). Either one means NO VERDICT: make's warnings
    cannot speak for text make never reports on, nor for text never read.
    """
    files, unresolved = gather_sources(makefile)
    hits = []
    for path, text in files:
        for lineno, ln in enumerate(text.splitlines(), 1):
            if VALUE_RE.search(ln):
                hits.append((path, lineno, ln.strip()[:120]))
    return hits, unresolved


def report(makefile, goal="run"):
    print(f"makefile : {makefile}")
    print(f"goal     : {goal}  (warnings are goal-independent -- prerequisite "
          f"lists expand when each rule is READ)")

    evasions, unresolved = find_evasions(makefile)
    for path, lineno, text in evasions:
        print(f"::error file={os.path.basename(path)},line={lineno}::"
              f"$(value ...) reads a variable without referencing it, so "
              f"--warn-undefined-variables is silent here whether or not it "
              f"is defined yet -- this audit cannot see this line: {text}")
    for path, why in unresolved:
        print(f"::error file={os.path.basename(path)}::an include could not be "
              f"followed, so part of this Makefile was never scanned ({why})")

    warnings, stderr, rc = run_make(makefile, goal)
    print(f"warnings : {len(warnings)}")
    for w in warnings:
        print(f"::error file={w['file']},line={w['line']}::"
              f"prerequisite variable '{w['var']}' is not defined at this "
              f"point, so this rule's prerequisite list was EMPTY when make "
              f"read it -- the guard will not re-run when its source changes")
    if not warnings:
        print("  (none)")

    # ANY non-zero exit means make stopped before the end of the file, so the
    # list above is a PREFIX of it, not a verdict on it. Reporting that as
    # clean is how a Makefile make cannot even read scored 0 findings.
    if rc != 0:
        print(f"ERROR: make exited {rc} and did not finish reading this "
              f"Makefile, so the {len(warnings)} warning(s) above cover only "
              f"the part it reached. NO VERDICT -- absence of more is not "
              f"evidence of none.")
        print(stderr.strip()[:500])
        return 2
    if evasions or unresolved:
        print(f"ERROR: {len(evasions)} $(value ...) reference(s) and "
              f"{len(unresolved)} unfollowable include(s). NO VERDICT -- this "
              f"audit is only sound over text make reports on.")
        return 2
    if warnings:
        return 1
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

# ---------------------------------------------------------------------------
# Fixtures for the arms that exercise report()'s EXIT CODE rather than
# run_make's warning list. The first three each scored a clean `exit 0` before
# the refusals existed, which is why they are here.

# make dies on line 2's `missing separator` and never reaches line 3, where a
# genuinely undefined prerequisite sits. 0 warnings is therefore "make did not
# look", not "there is nothing to find".
SELFTEST_TRUNCATED = """\
ok: base.c
   @true
bad: base.c ${LATE2}
\t@true
LATE2 := late2.c
"""

# Exactly the defect this gate is for, written so make says nothing: measured
# 0 warnings here against 1 for the same rule spelled ${LATE}.
SELFTEST_VALUE = """\
ok: base.c $(value LATE)
\t@true
LATE := late.c
"""

# The same evasion one `include` away. make exits 0 and warns 0 times, so only
# a scan that FOLLOWS the include can see it.
SELFTEST_INCLUDER = """\
include inc.mk
ok: base.c $(FROM_INC)
\t@true
"""
SELFTEST_INCLUDED = """\
FROM_INC := inc.c
other: base.c $(value LATE3)
\t@true
LATE3 := l3.c
"""

# The control. Three "must refuse" arms are all satisfied by a tool that
# refuses everything; this is the arm that makes them mean something.
SELFTEST_CLEAN = """\
EARLY := early.c

ok: base.c $(EARLY)
\t@true
"""


def _fixture_exit(files, goal="ok"):
    """(exit code, captured output) from the REAL report() over a fixture tree.

    report()'s printing is captured and shown only when its arm fails: the arm
    is decided by the exit code, because that is what CI consumes.
    """
    with tempfile.TemporaryDirectory() as d:
        for name, text in files.items():
            with open(os.path.join(d, name), "w") as fh:
                fh.write(text)
        for n in ("base.c", "early.c", "late.c", "late2.c", "inc.c", "l3.c"):
            p = os.path.join(d, n)
            if not os.path.exists(p):
                open(p, "w").close()
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            rc = report(os.path.join(d, "Makefile"), goal=goal)
        return rc, buf.getvalue()


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

        # The arms above score make's WARNINGS. These score report()'s EXIT,
        # which is a different question and the one CI acts on.
        refusal_arms = [
            ("REFUSE    make stopped early -- no verdict, not 'none'",
             {"Makefile": SELFTEST_TRUNCATED}, 2),
            ("REFUSE    $(value X) in the Makefile itself",
             {"Makefile": SELFTEST_VALUE}, 2),
            ("REFUSE    $(value X) in an INCLUDED file",
             {"Makefile": SELFTEST_INCLUDER, "inc.mk": SELFTEST_INCLUDED}, 2),
            ("CONTROL   a clean Makefile still exits 0",
             {"Makefile": SELFTEST_CLEAN}, 0),
        ]
        for name, files, want_rc in refusal_arms:
            got, out = _fixture_exit(files)
            good = (got == want_rc)
            ok = ok and good
            print(f"  {'PASS' if good else 'FAIL'}  {name}: "
                  f"exit={got}, wanted={want_rc}")
            if not good:
                for ln in out.strip().splitlines():
                    print(f"        | {ln}")

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
