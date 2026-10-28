#!/usr/bin/env python3
r"""Ask MAKE whether a guard's prerequisite variable was defined in time.

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
        file using it gets NO VERDICT rather than a green one.

    Both are the same shape as the parser's: an answer that looks like a
    measurement but is the absence of one. The rule here is that this tool has
    three outcomes, not two -- clean, findings, and NO VERDICT.

AND THEN I REINTRODUCED A PARSER FOR THE REFUSALS THEMSELVES
    An adversarial audit took the refusals apart three ways on the real file,
    and all three were ONE mistake: having removed the Makefile parser for
    prerequisites, I wrote a new text scan to decide WHICH FILES to scan and
    WHICH SPELLING counts as `value`. Measured, every one of these printed
    `warnings : 0 / (none)` and exited 0 on a Makefile whose guard rule had a
    silently empty prerequisite list:

      * `include\` + newline + ` inc.mk`. The include regex was matched per
        PHYSICAL line, so neither line of a continued directive matched: the
        included file was never scanned AND never recorded as unfollowable.
      * `$(eval include inc.mk)`. The line starts with `$`, so an
        `^\s*include`-anchored regex never sees it, while make performs the
        include perfectly well (proved by the included file's rule appearing
        in `make -pn`).
      * `$(call value,LATE)`. `call` invokes the builtin by name, so this IS
        `$(value LATE)` -- the control prints `changed.c` for both -- but a
        regex demanding `value` immediately after `$(` cannot match it.

    So the include scan is GONE too. The file list now comes from make's own
    `MAKEFILE_LIST`, read out of `make -pn`: whatever route make took to read
    a file -- a continued directive, an eval, a computed name -- the file is
    in that list, because make put it there. There is no "unfollowable
    include" category left to get wrong, and a file make could NOT read makes
    make exit non-zero, which is already a refusal.

    ⚠ `make -np`'s variable dump is normally a CLEAN-BILL GENERATOR here and
    must not be used as evidence: it prints every variable's FINAL value after
    the whole file is parsed, so a late-defined prerequisite variable shows a
    perfectly good path for a rule that never had the dependency. That warning
    is about VALUES over TIME. `MAKEFILE_LIST` is a different kind of fact --
    "which files did you read?" has no timing component, and the post-parse
    answer is the complete one. Nothing else is taken from the dump.

    The `value` check stays a text scan, because no make flag warns on it, but
    it now matches the two positions in which make can name a function:
    directly (`$(value X)`, `${value X}`) and through `call`
    (`$(call value,X)`). Measured: 0 hits on the audited Makefile, and prose
    like "the return value of foo" does not match.

    ⚠ IRREDUCIBLE RESIDUE, stated rather than discovered later: a function
    name ASSEMBLED at runtime (`V := val$(nothing)ue`, then
    `$(call $(V),LATE)`) contains the word nowhere and is undetectable by any
    text scan. That is accepted, and the reason is the tool's purpose: this
    gate detects an ACCIDENTAL late-defined prerequisite, and nobody writes
    that by accident. It is not a defence against someone deliberately hiding
    a stale dependency from the gate.

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

# Reads a variable's text without REFERENCING it, so
# --warn-undefined-variables is silent whether or not it is defined yet. Both
# positions in which make can name a function: directly, and through `call` --
# `$(call value,X)` IS the builtin (measured: both spellings expand alike).
VALUE_RE = re.compile(r"[$][({]\s*(?:call\s+)?value\b")

# make's own record of every file it read, out of its `-p` dump. See the module
# docstring for why this ONE line of that dump is evidence when the rest is a
# clean-bill generator: "which files did you read?" has no timing component.
MAKEFILE_LIST_RE = re.compile(r"^MAKEFILE_LIST\s*:?=\s*(?P<files>.*)$")


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


def gather_sources(makefile, goal="run"):
    """(files whose TEXT must be scanned, reasons the list is untrustworthy).

    ⛔ THE FILE LIST COMES FROM MAKE, NOT FROM READING `include` LINES. Three
    separate evasions of the old text scan were reproduced on the real file --
    a continued `include\\` directive, `$(eval include ...)`, and an include
    naming a variable -- and every one of them produced a GREEN audit over a
    file whose text was never examined. Asking make removes the whole class:
    MAKEFILE_LIST holds every file make read, by whatever route, because make
    is what put them there.

    A missing or unparseable MAKEFILE_LIST is a REASON, never an empty list:
    an audit over zero files would otherwise be the cleanest result there is.
    """
    root = os.path.dirname(os.path.abspath(makefile)) or "."
    env = {"PATH": os.environ.get("PATH", "/usr/bin:/bin")}
    proc = subprocess.run(
        ["make", "-pn", "--warn-undefined-variables",
         "-f", os.path.basename(makefile), goal],
        cwd=root, capture_output=True, text=True, env=env,
    )
    listed = None
    for ln in proc.stdout.splitlines():
        m = MAKEFILE_LIST_RE.match(ln)
        if m:
            listed = m.group("files").split()
            break
    if listed is None:
        return [], [(makefile, "make printed no MAKEFILE_LIST, so WHICH files "
                               "it read is unknown -- scanning the named file "
                               "alone would be a guess")]

    files, problems, seen = [], [], set()
    for token in listed:
        path = os.path.abspath(os.path.join(root, token))
        if path in seen:
            continue
        seen.add(path)
        try:
            with open(path, "r", errors="replace") as fh:
                files.append((path, fh.read()))
        except OSError as exc:
            # make read it and we cannot: that is a refusal, not a skip.
            problems.append((path, f"make read this file but it is not "
                                   f"readable here: {exc.strerror}"))
    if not files and not problems:
        problems.append((makefile, "MAKEFILE_LIST named no readable file"))
    return files, problems


def logical_lines(text):
    """[(first physical line number, the LOGICAL line)] -- continuations joined.

    ⛔ SCAN LOGICAL LINES, NEVER PHYSICAL ONES. make joins a trailing backslash
    with the next line BEFORE it interprets anything, and a physical-line scan
    has now been defeated by that three separate times in this one tool:

      r1  the Makefile parser read only a rule's FIRST line, so five
          prerequisite variables on a continuation line were invisible.
      r3  `include\\` + newline + ` inc.mk` matched neither physical line, so
          the included file was never scanned and never reported.
      r4  `$(call \\` + newline + ` value,LATE)` splits the function name from
          `call`, so VALUE_RE matched neither line -- and the joined form is
          genuinely the builtin (measured: `joined=[x.c] direct=[x.c]`).

    Three spellings of one mistake, so this joins ONCE, here, and every scan
    runs over the result. The line number reported is the FIRST physical line
    of the logical line, which is where a reader has to go to edit it.

    Joined with a single SPACE, which is what make does (the backslash-newline
    and the next line's leading whitespace collapse to one space). That is also
    the safe direction: joining with nothing could GLUE two fragments into a
    keyword make never saw -- `$(va\\` + `lue X)` is `$(va lue X)` to make, not
    a `value` call at all.
    """
    def joined(parts):
        """First part as it stands; each continuation adds exactly one space."""
        out = parts[0]
        for part in parts[1:]:
            out += " " + part.lstrip()
        return out

    lines, buf, start = [], [], None
    for lineno, ln in enumerate(text.splitlines(), 1):
        if start is None:
            start = lineno
        if ln.endswith("\\"):
            buf.append(ln[:-1])
            continue
        buf.append(ln)
        lines.append((start, joined(buf)))
        buf, start = [], None
    if buf:                              # a trailing backslash at end of file
        lines.append((start, joined(buf)))
    return lines


def find_evasions(makefile, goal="run"):
    """[(file, line, text)] for every `value` call, plus list-integrity problems.

    Returns (hits, problems). Either one means NO VERDICT: make's warnings
    cannot speak for a construct it does not report on, nor for text that was
    never read.
    """
    files, problems = gather_sources(makefile, goal)
    hits = []
    for path, text in files:
        # LOGICAL lines -- see logical_lines() for the three times a
        # physical-line scan was defeated by a continuation.
        for lineno, ln in logical_lines(text):
            if VALUE_RE.search(ln):
                hits.append((path, lineno, ln.strip()[:120]))
    return hits, problems


def report(makefile, goal="run"):
    print(f"makefile : {makefile}")
    print(f"goal     : {goal}  (warnings are goal-independent -- prerequisite "
          f"lists expand when each rule is READ)")

    evasions, unresolved = find_evasions(makefile, goal)
    for path, lineno, text in evasions:
        print(f"::error file={os.path.basename(path)},line={lineno}::"
              f"a `value` call reads a variable without referencing it, so "
              f"--warn-undefined-variables is silent here whether or not it "
              f"is defined yet -- this audit cannot see this line: {text}")
    for path, why in unresolved:
        print(f"::error file={os.path.basename(path)}::the set of files make "
              f"actually read could not be established, so part of this "
              f"Makefile may never have been scanned ({why})")

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
        print(f"ERROR: {len(evasions)} `value` call(s) and {len(unresolved)} "
              f"file-list problem(s). NO VERDICT -- this audit is only sound "
              f"over text make reports on, in files make says it read.")
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

# ---------------------------------------------------------------------------
# The three evasions of the REFUSALS, each reproduced on the real file by an
# adversarial audit and each scoring a clean exit 0 before this round. All
# three were one mistake: a text scan deciding which files to read and which
# spelling counts.

# `call` invokes the builtin BY NAME, so this is $(value LATE) -- measured,
# both spellings expand to the same text -- but it does not follow `$(`.
SELFTEST_VALUE_VIA_CALL = """\
ok: base.c $(call value,LATE)
\t@true
LATE := late.c
"""

# A CONTINUED include directive: neither physical line matches an
# `^\\s*include\\s+` regex, while make follows it perfectly well.
SELFTEST_INCLUDE_CONTINUED = "include\\\n inc.mk\nok: base.c\n\t@true\n"

# An include make performs through eval. The line starts with `$`, so an
# anchored include regex never sees it.
SELFTEST_INCLUDE_EVAL = """\
$(eval include inc.mk)
ok: base.c
\t@true
"""

# The included file carries the evasion, so it is only found if the file list
# is right. Its own rule also proves the include really happened.
SELFTEST_INCLUDED_VALUE = """\
guard: $(value LATE)
\t@true
LATE := guarded.c
"""

# A continuation SPLITTING `call` from `value`. The joined form really is the
# builtin -- measured, `joined=[x.c] direct=[x.c]` -- and neither physical line
# matches on its own. The third continuation defeat of a physical-line scan in
# this tool, which is why logical_lines() exists.
SELFTEST_VALUE_CONTINUED = "ok: base.c $(call \\\n value,LATE)\n\t@true\nLATE := late.c\n"

# ⛔ THE CONTROL FOR THE JOIN ITSELF. make replaces a backslash-newline with a
# SPACE, so a word cannot be split across one: `$(va\\` + `lue X)` is
# `$(va lue X)` to make and is no `value` call at all. A join that glued the
# fragments would invent a refusal for a Makefile make reads as clean.
SELFTEST_SPLIT_WORD = "ok: base.c $(va\\\nlue LATE)\n\t@true\nLATE := late.c\n"


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
            # The three that defeated the refusals themselves. Each exited 0.
            ("REFUSE    $(call value,X) is the builtin too",
             {"Makefile": SELFTEST_VALUE_VIA_CALL}, 2),
            ("REFUSE    a CONTINUED include is still an include",
             {"Makefile": SELFTEST_INCLUDE_CONTINUED,
              "inc.mk": SELFTEST_INCLUDED_VALUE}, 2),
            ("REFUSE    $(eval include ...) is still an include",
             {"Makefile": SELFTEST_INCLUDE_EVAL,
              "inc.mk": SELFTEST_INCLUDED_VALUE}, 2),
            # ⛔ AND THE CONTROL FOR THOSE THREE: an include make reads, whose
            # text is clean, must still PASS. Without this, "refuse whenever
            # anything is included" would satisfy all three above -- and that
            # is a tool nobody could add an include to.
            ("CONTROL   a clean INCLUDED file still exits 0",
             {"Makefile": "include inc.mk\nok: base.c $(FROM_INC)\n\t@true\n",
              "inc.mk": "FROM_INC := inc.c\n"}, 0),
            # r4: a continuation SPLITTING `call` from `value`.
            ("REFUSE    a CONTINUED $(call value,X) is still that call",
             {"Makefile": SELFTEST_VALUE_CONTINUED}, 2),
            # ⛔ And the control for the JOIN: make turns a backslash-newline
            # into a SPACE, so `$(va\ / lue X)` is the variable `va lue X` --
            # which --warn-undefined-variables CATCHES (exit 1, a real
            # finding), and which is NOT a `value` call. A join that glued the
            # fragments would answer 2 here, inventing a refusal for a file
            # make reads perfectly well. Measured:
            #   Makefile:1: warning: undefined variable 'va lue LATE'
            ("CONTROL   a word split by a continuation is NOT a value call",
             {"Makefile": SELFTEST_SPLIT_WORD}, 1),
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
