#!/usr/bin/env python3
"""Every firmware file a host-test target READS must be in BOTH host-tests.yml
path filters -- otherwise the test that guards it never runs for it.

WHY THIS EXISTS. The same blind spot appeared in THREE artifacts written at
different times, each listing JSON_Encoder.c and omitting csv_encoder.c:

  the CODE       csv_encoder.c's oversize eviction did not book its own loss
  the PIN        $(BIN970)'s self-report check enumerated JSON_ENC_SRC and
                 NANOPB_ENC_SRC and not CSV
  the CI FILTER  both path filters listed JSON_Encoder.c and not csv_encoder.c

They were not independent: all three were written from one mental model of
which encoders matter, so the omission and the defect had a common cause. Three
hand-written enumerations agreed with each other and with nothing else.

So this derives the list MECHANICALLY from the Makefile's own text -- the one
list that is not written from the blind spot -- and fails the build when a file
the host tests read is missing from a filter. The failure is LOUD and
co-located with the thing that caused it: adding a firmware source to a target
is what trips it.

REACHABILITY, which is the whole point and was the #991 round-2 finding: this
check is useless if it cannot run when it matters. It can. Both filters include
'tests/host/**' and '.github/workflows/host-tests.yml', and adding a firmware
reference to a host-test target means editing the Makefile under tests/host/ --
so the edit that creates the gap is itself in the filters that trigger this.

WHAT IT DOES NOT CHECK: that the filters contain nothing superfluous (a stale
entry is harmless), that a referenced file exists, or anything about the push
and pull_request filters agreeing with each other beyond covering this set.
"""
import fnmatch
import os
import posixpath
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
MAKEFILE = os.path.join(HERE, "Makefile")
WORKFLOW = os.path.join(HERE, "..", "..", ".github", "workflows", "host-tests.yml")

# `../../firmware/src/...` is how every host-test target names a firmware file.
REF = re.compile(r"\.\./\.\./(firmware/src/[A-Za-z0-9_./-]+\.(?:c|h))")
INC = re.compile(r"-I(\.\./\.\./firmware/src/[^\s)$]*)")
ENTRY = re.compile(r"^\s*-\s*'([^']+)'", re.M)
# What can SURVIVE into `make -n` output. -n has expanded every make reference,
# so a bare `$(NAME)` / `${NAME}` there is SHELL text (`$(grep -c ...)`,
# `${piece1}`, an echoed `\$(CC)`) and is NOT flagged: measured on the real
# tree, flagging those fails on ~60 legitimate lines. Only text shaped like a
# make FUNCTION call (names a shell would not run) is flagged -- it means a
# function make did not evaluate.
MAKE_SHAPED = re.compile(
    r"\$[({](?:addprefix|addsuffix|patsubst|subst|wildcard|foreach|filter-out|"
    r"abspath|realpath|notdir|basename|call|eval|origin|flavor|findstring|"
    r"firstword|lastword|wordlist|if|or|and)[ \t]")
# A target/prerequisite line of the -p database (not a variable, not a recipe).
DB_RULE = re.compile(r"^[^\s#:=][^:=]*:(?!=)")
MAKE_ERRORS = re.compile(r"\*\*\*|No rule to make target|missing separator|Stop\.|Circular")

# #1178 / PR #1298. Three rounds of a hand-written Makefile expander, then a
# round-4 `$(info)` second run, each missed a GNU make semantic (target-specific
# variables, `$(VAR:.c=.h)`, `$(if)`, `$(abspath)` ...): re-implementing make
# in Python is the same blind spot this checker exists to remove. So Python
# expands NOTHING; GNU make does, twice:
#   1. `make -pRrq`   its database: prerequisites and `:=` values, expanded.
#   2. `make -n -k -B <goal>`  its dry run: every recipe line expanded in its
#      real target scope (target-specific variables, substitution refs,
#      functions, abspath, all of it). No recipe under `run` uses $(MAKE) or a
#      `+` prefix, which -n would otherwise EXECUTE (checked, #1298 r5).
# File set = REF over (the database) U (the dry-run text) U (the raw Makefile,
# for safety). Absolute paths inside the repo are rewritten to the `../../`
# form first (realpath and abspath forms of the root, derived at runtime, since
# the CI runner's path differs). FAIL CLOSED on: make not installed, a timeout,
# a database exit other than 0/1, a dry run that exits non-zero OR whose stderr
# carries make's own errors (`***`, `No rule to make target`, `missing
# separator`, `Stop.`) -- measured on the real tree: `-n -k -B run` exits 0 with
# empty stderr, so any of these is a genuine signal -- and on a `$(`/`${`
# surviving in a prerequisite line, or a make FUNCTION call surviving in the
# dry-run text (a bare `$(x)`/`${x}` there is shell text, see MAKE_SHAPED).
# Recursion sentinel: CHECK_CI_PATH_FILTERS_INNER makes a nested run exit 0.
#
# DECLARED BOUNDARY: a path composed by the SHELL inside a recipe
# (`d=../../firmware/src/Util; cat $$d/x.h`) is invisible to every make-level
# oracle. This checker covers what MAKE expands, and claims nothing beyond it.
#
# -I include dirs are classified and printed as a NOTE, not enforced (#1300).

SENTINEL = "CHECK_CI_PATH_FILTERS_INNER"


class MakeError(Exception):
    pass


def _make(args, cwd, ok_rc=(0, 1)):
    env = {k: v for k, v in os.environ.items()
           if k not in ("MAKEFLAGS", "MAKELEVEL", "MFLAGS", "MAKEOVERRIDES")}
    env["LC_ALL"] = "C"
    env[SENTINEL] = "1"
    try:
        r = subprocess.run(["make", "--no-print-directory"] + args, cwd=cwd,
                           capture_output=True, text=True, timeout=300, env=env)
    except FileNotFoundError:
        raise MakeError("GNU make is not installed")
    except subprocess.TimeoutExpired:
        raise MakeError("make timed out: " + " ".join(args))
    if r.returncode not in ok_rc:
        raise MakeError("make exited %d: %s" % (r.returncode, r.stderr.strip()[:500]))
    return r


def _repo_relative(text, root):
    """Rewrite any absolute path under the repo root to the ../../ form REF knows."""
    for form in {os.path.realpath(root), os.path.abspath(root)}:
        text = text.replace(form + "/", "../../")
    return text


def analyse(makefile, goal=None):
    """(files, dirs, problems) for one Makefile, as GNU make sees it."""
    cwd = os.path.dirname(os.path.abspath(makefile))
    mk = os.path.abspath(makefile)
    root = os.path.join(cwd, "..", "..")
    raw = open(mk, encoding="utf-8").read()
    db = _repo_relative(_make(["-pRrq", "-f", mk], cwd).stdout, root)
    dry = _make(["-n", "-k", "-B", "-f", mk] + ([goal] if goal else []), cwd, (0,))
    if MAKE_ERRORS.search(dry.stderr):
        raise MakeError("make -n reported: " + dry.stderr.strip()[:500])
    dry_text = _repo_relative(dry.stdout, root)
    problems = []
    for ln in db.split("\n"):
        if DB_RULE.match(ln) and ("$(" in ln or "${" in ln):
            problems.append("unexpanded reference in a prerequisite line: " + ln.strip()[:160])
    for ln in dry_text.split("\n"):
        if MAKE_SHAPED.search(ln):
            problems.append("unexpanded make reference in a dry-run recipe: " + ln.strip()[:160])
    files = set(REF.findall(raw)) | set(REF.findall(db)) | set(REF.findall(dry_text))
    dirs = set(INC.findall(raw)) | set(INC.findall(db)) | set(INC.findall(dry_text))
    return sorted(files), sorted({posixpath.normpath(d) for d in dirs}), problems


def referenced(makefile):
    return analyse(makefile)[0]


def filters(workflow_text):
    """(pull_request entries, push entries). Split on the two trigger keys
    rather than parsing YAML, so this needs no dependency in CI."""
    pr_at = workflow_text.index("pull_request:")
    push_at = workflow_text.index("push:", pr_at)
    return (ENTRY.findall(workflow_text[pr_at:push_at]),
            ENTRY.findall(workflow_text[push_at:]))


def uncovered(paths, patterns):
    return [p for p in paths
            if not any(fnmatch.fnmatch(p, pat) for pat in patterns)]


def self_test():
    """Both directions, because a checker that cannot FAIL is not a checker.
    Mk-file cases run the REAL make on temp Makefiles."""
    checks = 0

    def ck(label, got, want):
        nonlocal checks
        checks += 1
        if got != want:
            print("  self-test FAIL: %s: got %r want %r" % (label, got, want))
            sys.exit(1)

    def run(text, extra=()):
        with tempfile.TemporaryDirectory() as td:
            os.makedirs(os.path.join(td, "tests", "host"))
            for e in list(extra) + REF.findall(text):
                p = os.path.join(td, e)
                os.makedirs(os.path.dirname(p), exist_ok=True)
                open(p, "w").close()
            mk = os.path.join(td, "tests", "host", "Makefile")
            open(mk, "w").write(text)
            return analyse(mk)

    ck("literal variable and inline recipe forms are found",
       run("SRC := ../../firmware/src/Util/CRC32.c\nall:\n\tcc ../../firmware/src/services/csv_encoder.c\n")[0],
       ["firmware/src/Util/CRC32.c", "firmware/src/services/csv_encoder.c"])
    ck("A: addprefix prerequisite", run(
        "all: $(addprefix ../../firmware/src/Util/,DoubleFmt.h)\n\t@:\n",
        ["firmware/src/Util/DoubleFmt.h"])[0],
       ["firmware/src/Util/DoubleFmt.h"])
    ck("B: patsubst renames the prerequisite", run(
        "S := ../../firmware/src/Util/a.c\nall: $(patsubst %.c,%.h,$(S))\n\t@:\n",
        ["firmware/src/Util/a.h"])[0],
       ["firmware/src/Util/a.c", "firmware/src/Util/a.h"])
    ck("C: a redefined variable keeps the value current at use", run(
        "D := ../../firmware/src/A\nX := $(D)/f.c\nD := ../../firmware/src/B\nall: $(X)\n\t@:\n", ["firmware/src/A/f.c"])[0],
       ["firmware/src/A/f.c"])
    ck("wildcard is globbed by make", run(
        "all: $(wildcard ../../firmware/src/W/*.h)\n\t@:\n",
        ["firmware/src/W/a.h", "firmware/src/W/b.h"])[0],
       ["firmware/src/W/a.h", "firmware/src/W/b.h"])
    ck("D: a recursive variable composed in a recipe only", run(
        "D = ../../firmware/src/R\nall:\n\tcat $(D)/only.c\n")[0],
       ["firmware/src/R/only.c"])
    ck("E: a function-composed path in a recipe is now HARVESTED", run(
        "D := ../../firmware/src/R\nall:\n\techo $(addprefix $(D)/,a.c)\n")[0],
       ["firmware/src/R/a.c"])
    ck("-I dirs are collected, not enforced", run(
        "D := ../../firmware/src/x\nF = -I$(D)/inc\nall:\n\tcc $(F)\n")[1],
       ["../../firmware/src/x/inc"])
    # r2-audit findings (run wf_ec15a1d5-de1): each is HARVESTED by make's own
    # expansion in the target's real scope, never silently OK.
    ck("F1: target-specific `=` variable", run(
        "all: V = ../../firmware/src/T/a.c\nall:\n\tcat $(V)\n")[0],
       ["firmware/src/T/a.c"])
    ck("F2: target-specific `:=` variable", run(
        "all: V := ../../firmware/src/T/b.c\nall:\n\tcat $(V)\n")[0],
       ["firmware/src/T/b.c"])
    ck("F3: substitution reference $(VAR:.c=.h)", run(
        "S := ../../firmware/src/U/a.c\nall:\n\tcat $(S:.c=.h)\n")[0],
       ["firmware/src/U/a.c", "firmware/src/U/a.h"])
    ck("F4: $(if ...) composing a path", run(
        "all:\n\tcat $(if $(X),../../firmware/src/I/no.c,../../firmware/src/I/yes.c)\n")[0],
       ["firmware/src/I/no.c", "firmware/src/I/yes.c"])
    ck("F5: $(abspath ...) is normalised to a repo-relative path", run(
        "all:\n\tcat $(abspath ../../firmware/src/Z/abs.h)\n")[0],
       ["firmware/src/Z/abs.h"])
    ck("a shell $(...) substitution in a recipe is not a problem", run(
        "all:\n\tn=$$(grep -c x ../../firmware/src/S/s.c)\n")[2], [])
    ck("a make function call surviving into the dry run is a problem", len(run(
        "all:\n\techo $$(if x,y)\n")[2]), 1)
    try:
        run("all: ( broken\n\t@:\n$(error boom)\n")
        ck("make exit 2 must raise", "returned", "raised")
    except MakeError:
        ck("make exit 2 raises MakeError", 1, 1)
    try:
        run("all: missing_target_xyz\n\t@:\n")
        ck("make -n 'No rule' must raise", "returned", "raised")
    except MakeError:
        ck("make -n 'No rule to make target' raises MakeError", 1, 1)
    r = subprocess.run([sys.executable, os.path.abspath(__file__)],
                       env=dict(os.environ, **{SENTINEL: "1"}),
                       capture_output=True, text=True)
    ck("recursion sentinel: a nested run exits 0 at once", (r.returncode, r.stdout), (0, ""))

    wf = ("on:\n  pull_request:\n    paths:\n"
          "      - 'firmware/src/Util/CRC32.c'\n"
          "  push:\n    paths:\n"
          "      - 'firmware/src/Util/CRC32.c'\n"
          "      - 'firmware/src/services/csv_encoder.c'\n")
    pr, push = filters(wf)
    ck("the two filter blocks are read separately", (len(pr), len(push)), (1, 2))
    miss = ["firmware/src/Util/CRC32.c", "firmware/src/services/csv_encoder.c"]
    ck("a file missing from ONE filter is reported", uncovered(miss, pr),
       ["firmware/src/services/csv_encoder.c"])
    ck("...and not against the filter that has it", uncovered(miss, push), [])
    ck("a glob entry covers matching paths",
       uncovered(["firmware/src/services/a.h"], ["firmware/src/services/*.h"]), [])
    print("  self-test: %d/%d checks passed" % (checks, checks))


def main():
    if os.environ.get(SENTINEL):  # reached recursively from a make we ran: stop
        return 0
    if "--self-test" in sys.argv:
        self_test()
        return 0
    wf = open(WORKFLOW, encoding="utf-8").read()
    try:
        paths, dirs, blind = analyse(MAKEFILE, "run")
    except MakeError as e:
        print("ERROR: cannot read the Makefile through GNU make (#1178): %s" % e)
        return 1
    if blind:
        print("ERROR: make's own expansion still holds an unexpanded reference, so")
        print("       this checker cannot say whether the path filters cover what it")
        print("       names (#1178). Spell the path out in full (../../firmware/src/...)")
        print("       or fix the reference:")
        for b in blind:
            print("       unaccounted  %s" % b)
        return 1
    pr, push = filters(wf)
    bad_pr, bad_push = uncovered(paths, pr), uncovered(paths, push)
    print("NOTE: %d -I include dirs not checked (headers reached via #include are "
          "outside this checker's prerequisite model; see FINDINGS / follow-up)"
          % len(dirs))
    for d in dirs:
        print("       -I %s" % d)
    if not bad_pr and not bad_push:
        print("OK: all %d firmware file(s) the host tests read are covered by "
              "both host-tests.yml path filters (%d / %d entries)."
              % (len(paths), len(pr), len(push)))
        return 0
    print("ERROR: a host-test target reads a firmware file that is NOT in a")
    print("       host-tests.yml path filter, so a change to that file does")
    print("       NOT trigger the job -- the check guarding it never runs.")
    print("       A GREEN GATE CONSTRAINS ONLY WHAT THE GATE RUNS.")
    for label, miss in (("pull_request", bad_pr), ("push", bad_push)):
        for p in miss:
            print("       missing from %-12s %s" % (label, p))
    print("       Add the path to BOTH filters. Do not hand-enumerate the set:")
    print("       this list is derived from the Makefile's own references,")
    print("       which is the one list not written from the same blind spot")
    print("       that produced the gap (#970 / PR #991 round-2 audit).")
    return 1


if __name__ == "__main__":
    sys.exit(main())
