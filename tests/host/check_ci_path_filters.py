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
BARE = re.compile(r"\$[({]([A-Za-z_][A-Za-z0-9_]*)[)}]")
FUNC = re.compile(r"\$[({](?:addprefix|addsuffix|patsubst|subst|wildcard|foreach|filter|"
                  r"filter-out|basename|dir|notdir|abspath|realpath)\s[^)}]*firmware/src")

# #1178 / PR #1298 audit: three rounds of a hand-written Makefile expander each
# missed a GNU make semantic (addprefix/patsubst renaming, a variable
# redefined after use, ...), because re-implementing make in Python is the same
# blind spot this checker exists to remove. So make itself answers:
# `make -pRrq` prints its database, in which prerequisites and `:=` values are
# already fully expanded (addprefix, patsubst, wildcard, redefinition, all of
# it). Recipes and recursive (`=`) variables are printed UNEXPANDED, so the
# `$(NAME)` references in recipes are resolved by a second make run that
# `$(info)`s each NAME against the same Makefile (the smaller sound option,
# vs. flagging them). A `$(func ...)` in a recipe that still mentions
# firmware/src after that is FAILED, never skipped. The raw Makefile text is
# also scanned, which keeps literal paths in recipe greps.
#
# -I include dirs are classified and printed as a NOTE, not enforced (#1300).


class MakeError(Exception):
    pass


def _make(args, cwd):
    try:
        r = subprocess.run(["make"] + args, cwd=cwd, capture_output=True, text=True,
                           timeout=120, env={k: v for k, v in dict(os.environ, LC_ALL="C").items()
                                if k not in ("MAKEFLAGS", "MAKELEVEL", "MFLAGS", "MAKEOVERRIDES")})
    except FileNotFoundError:
        raise MakeError("GNU make is not installed")
    except subprocess.TimeoutExpired:
        raise MakeError("make timed out: " + " ".join(args))
    if r.returncode not in (0, 1):  # 1 = default goal out of date: normal for -q
        raise MakeError("make exited %d: %s" % (r.returncode, r.stderr.strip()[:500]))
    return r.stdout


def analyse(makefile):
    """(files, dirs, problems) for one Makefile, as GNU make sees it."""
    cwd = os.path.dirname(os.path.abspath(makefile))
    mk = os.path.abspath(makefile)
    raw = open(mk, encoding="utf-8").read()
    db = _make(["-pRrq", "-f", mk], cwd)
    files = set(REF.findall(raw)) | set(REF.findall(db))
    dirs = set(INC.findall(raw)) | set(INC.findall(db))
    problems = []
    recipes = [ln for ln in db.split("\n") if ln.startswith("\t")]
    names = sorted({n for ln in recipes for n in BARE.findall(ln.replace("$$", ""))})
    vals = {}
    if names:
        with tempfile.TemporaryDirectory() as td:
            tmp = os.path.join(td, "print.mk")
            with open(tmp, "w") as f:
                for n in names:
                    f.write("$(info @@%s@@$(%s))\n" % (n, n))
            out = _make(["-q", "-f", mk, "-f", tmp], cwd)
        cur = None
        for ln in out.rstrip("\n").split("\n"):
            m = re.match(r"@@([A-Za-z_][A-Za-z0-9_]*)@@(.*)$", ln)
            if m and m.group(1) in names:
                cur = m.group(1)
                vals[cur] = m.group(2)
            elif cur:
                vals[cur] += " " + ln
    for ln in recipes:
        t = ln.replace("$$", "\0")
        for _ in range(8):
            t2 = BARE.sub(lambda m: vals.get(m.group(1), m.group(0)), t)
            if t2 == t:
                break
            t = t2
        files.update(REF.findall(t))
        dirs.update(INC.findall(t))
        if FUNC.search(t):
            problems.append("recipe composes a firmware path with a make function: "
                            + t.replace("\0", "$$").strip()[:160])
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
    ck("E: a make function over a firmware path in a recipe is reported", len(run(
        "D := ../../firmware/src/R\nall:\n\techo $(addprefix $(D)/,a.c)\n")[2]), 1)
    ck("-I dirs are collected, not enforced", run(
        "D := ../../firmware/src/x\nF = -I$(D)/inc\nall:\n\tcc $(F)\n")[1],
       ["../../firmware/src/x/inc"])
    try:
        run("all: ( broken\n\t@:\n$(error boom)\n")
        ck("make exit 2 must raise", "returned", "raised")
    except MakeError:
        ck("make exit 2 raises MakeError", 1, 1)

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
    if "--self-test" in sys.argv:
        self_test()
        return 0
    wf = open(WORKFLOW, encoding="utf-8").read()
    try:
        paths, dirs, blind = analyse(MAKEFILE)
    except MakeError as e:
        print("ERROR: cannot read the Makefile through GNU make (#1178): %s" % e)
        return 1
    if blind:
        print("ERROR: the Makefile uses a variable that expands to a firmware path")
        print("       in a shape this checker cannot account for, so it cannot say")
        print("       whether the path filters cover it (#1178). Spell the path out")
        print("       in full (../../firmware/src/...) or teach the checker:")
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
