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
    r"abspath|notdir|call|eval|origin|flavor|findstring|"
    r"firstword|lastword|wordlist|if|or|and)[ \t]")
# (`basename` and `realpath` are deliberately NOT listed: they are real shell
# commands, so a recipe's `$$(basename x)` prints as `$(basename x)` after -n
# and is shell text. make has already expanded its own `$(basename ...)`.)
# A target/prerequisite line of the -p database (not a variable, not a recipe).
DB_RULE = re.compile(r"^[^\s#:=][^:=]*:(?!=)")
# A target-specific variable assignment, which -p prints as `target: VAR = v`
# (recursive values are kept unexpanded). It is NOT a prerequisite line; the dry
# run expands it in the target's real scope. Accepts `=`, `:=`, `::=`, `?=`,
# `+=`, `!=` and the override/export/private prefixes.
TARGET_ASSIGN = re.compile(
    r"^[^\s#:=][^:=]*:\s*(?:(?:override|export|private)\s+)*[^\s:=]+\s*[:?+!]*=")
# #1303: make's `shell` and `file` functions hide a path from every oracle here
# (make replaces the call by its RESULT before the database or dry run exists).
SHELL_FN = re.compile(r"\$[({]\s*(?:shell|file)(?![A-Za-z0-9_])")
EVAL_FN = re.compile(r"\$[({]\s*eval(?![A-Za-z0-9_])")
# `VAR != cmd` (also `override`/`export` prefixes, and target-specific): GNU make
# runs the right-hand side in a shell at parse time, exactly like $(shell).
# Recipe lines (leading tab) are shell text, not assignments.
BANG_ASSIGN = re.compile(r"^(?!\t)[^#=]*?!=(.*)$")
PATHISH = re.compile(r"[^\s'\"();|&<>=,]*(?:firmware|\.\./)[^\s'\"();|&<>=,]*")
CLEAN_FW = re.compile(r"^(?:-[A-Za-z])?\.\./\.\./(firmware/src(?:/(?!\.{1,2}(?:/|$))[A-Za-z0-9_.-]+)*)/?$")
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
# make's `shell` and `file` functions are NOT modelled (#1303): make replaces the
# call by its result before the database or dry run exists. Instead the raw
# Makefile is scanned and any $(shell/$(file call is REFUSED (exit 1, Makefile:N)
# unless every firmware path in it is a literal ../../firmware/src/... (no
# variable, $$, backtick, glob, or `..` after firmware/src). A literal with no
# extension is a directory a `find` walks: the filters must cover beneath it.
#
# -I include dirs are classified and printed as a NOTE, not enforced (#1300).

SENTINEL = "CHECK_CI_PATH_FILTERS_INNER"

# #1303 self-test fixtures, module-level so the same text can be replayed
# against an older checker. label -> (Makefile text, expected 'Makefile:N:' line)
_UTIL = "../../firmware/src/Util"
REFUSED = {
    "shell with a $(VAR) path": ("FW := %s\nX := $(shell grep -c x $(FW)/A.h)\nall:\n\t@:\n" % _UTIL, 2),
    "shell with a ${VAR} path": ("FW := %s\nX := $(shell grep -c x ${FW}/A.h)\nall:\n\t@:\n" % _UTIL, 2),
    "shell with a $$ shell var": ("X := $(shell d=%s; grep -c x $$d/A.h)\nall:\n\t@:\n" % _UTIL, 1),
    "shell with backticks": ("X := $(shell cat `printf %%s %s/A`.h)\nall:\n\t@:\n" % _UTIL, 1),
    "shell glob *": ("X := $(shell cat %s/A*.h)\nall:\n\t@:\n" % _UTIL, 1),
    "shell glob ?": ("X := $(shell cat %s/A?.h)\nall:\n\t@:\n" % _UTIL, 1),
    "shell glob [": ("X := $(shell cat %s/A[a].h)\nall:\n\t@:\n" % _UTIL, 1),
    "shell .. after firmware/src": ("X := $(shell cat %s/../Other/A.h)\nall:\n\t@:\n" % _UTIL, 1),
    "file read with a $(VAR) path": ("FW := %s\nX := $(file <$(FW)/A.h)\nall:\n\t@:\n" % _UTIL, 2),
    "!= with a $(VAR) path": ("FW := %s\nX != grep -c x $(FW)/A.h\nall:\n\t@:\n" % _UTIL, 2),
    "!= with backticks": ("X != cat `printf %%s %s/A`.h\nall:\n\t@:\n" % _UTIL, 1),
    "override != with a $(VAR) path": ("FW := %s\noverride X != grep -c x $(FW)/A.h\nall:\n\t@:\n" % _UTIL, 2),
    "$(eval) is refused outright": ("$(eval X := 1)\nall:\n\t@:\n", 1),
    "continuation: reported at its FIRST line": (
        "A := 1\nX := $(shell cat \\\n  $(FW)/A.h)\nall:\n\t@:\n", 2),
}
PASSING_SHELL = {
    "shell with literal paths": "X := $(shell grep -c x %s/A.h)\nall:\n\t@:\n" % _UTIL,
    "file read with a literal path": "X := $(file <%s/A.h)\nall:\n\t@:\n" % _UTIL,
    "a commented-out shell is not read": "# X := $(shell cat $(FW)/A.h)\nall:\n\t@:\n",
    "!= with a literal file": "X != grep -c x %s/A.h\nall:\n\t@:\n" % _UTIL,
    "a != inside a recipe is shell text": "all:\n\t[ a != b ] && cat $(FW)/A.h\n",
    "shell with no firmware path": "X := $(shell echo hi)\nall:\n\t@:\n",
}
# label -> (Makefile text, number of `problems` the fixed checker reports)
LOUD = {
    "target-specific `=`": ("FW := %s\nall: V = $(FW)/A.h\nall:\n\t@:\n" % _UTIL, 0),
    "target-specific `?=`": ("FW := %s\nall: V ?= $(FW)/A.h\nall:\n\t@:\n" % _UTIL, 0),
    "target-specific `+=`": ("FW := %s\nall: V += $(FW)/A.h\nall:\n\t@:\n" % _UTIL, 0),
    "target-specific override `=`": ("FW := %s\nall: override V = $(FW)/A.h\nall:\n\t@:\n" % _UTIL, 0),
    "target-specific export `?=`": ("FW := %s\nall: export V ?= $(FW)/A.h\nall:\n\t@:\n" % _UTIL, 0),
    "$$(basename x) in a recipe": ("all:\n\tn=$$(basename x)\n", 0),
    "$$(realpath x) in a recipe": ("all:\n\tn=$$(realpath x)\n", 0),
}


class MakeError(Exception):
    pass


class Refusal(MakeError):
    pass


def _logical_lines(raw):
    """(first physical line number, text) per make logical line, backslash-newline
    continuations spliced, comment lines dropped."""
    out, buf, start = [], "", 0
    for i, ln in enumerate(raw.split("\n"), 1):
        if not buf:
            start = i
        if ln.endswith("\\"):
            buf += ln[:-1] + " "
            continue
        buf += ln
        if not buf.lstrip().startswith("#"):
            out.append((start, buf))
        buf = ""
    if buf and not buf.lstrip().startswith("#"):
        out.append((start, buf))
    return out


def _call_args(text, at):
    """Text inside the make call whose `$(`/`${` starts at `at`, or None."""
    depth, i = 0, at + 1
    while i < len(text):
        c = text[i]
        if c in "({":
            depth += 1
        elif c in ")}":
            depth -= 1
            if depth == 0:
                return text[at + 2:i]
        i += 1
    return None


def shell_file_paths(raw, name="Makefile"):
    """Firmware paths named by $(shell ...)/$(file ...) calls and by the right-hand
    side of a `VAR != cmd` assignment (also a shell run at parse time) in the raw
    Makefile, as (display, path-to-test) pairs; raises Refusal (with file:line)
    for any such text not spelled with LITERAL `../../firmware/src/...` paths
    only, and for any `$(eval` (it can build any refused form at expansion time).
    A path with no extension is a DIRECTORY (a `find` root): it is tested via a
    synthetic child, so the filter must cover everything beneath it."""
    found = []

    def check(arg, what, refuse):
        if re.search(r"\$[({$]|`", arg):
            refuse("%s builds a path through a variable, $$ or backticks; "
                   "spell every firmware path as a literal "
                   "../../firmware/src/..." % what)
        for tok in PATHISH.findall(arg):
            cm = CLEAN_FW.match(tok)
            if not cm:
                if "firmware" in tok or re.search(r"[*?\[]", tok):
                    refuse("%s names a firmware path that is not a plain "
                           "literal (%r)" % (what, tok))
                continue
            path = cm.group(1)
            if "." in path.rsplit("/", 1)[-1]:
                found.append((path, path))
            else:
                found.append((path + "/", path + "/__child__.h"))

    for line, text in _logical_lines(raw):
        def refuse(why, line=line, text=text):
            raise Refusal("%s:%d: %s: %s" % (name, line, why, text.strip()[:140]))
        if EVAL_FN.search(text):
            refuse("$(eval is refused: it can construct any refused form at "
                   "expansion time")
        bang = BANG_ASSIGN.match(text)
        if bang:
            check(bang.group(1), "`!=` shell assignment", refuse)
        for m in SHELL_FN.finditer(text):
            arg = _call_args(text, m.start())
            if arg is None:
                refuse("unbalanced $(shell/$(file expression")
            check(arg, "$(shell/$(file argument", refuse)
    return found


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
    shell_paths = shell_file_paths(raw, os.path.basename(mk))
    db = _repo_relative(_make(["-pRrq", "-f", mk], cwd).stdout, root)
    dry = _make(["-n", "-k", "-B", "-f", mk] + ([goal] if goal else []), cwd, (0,))
    if MAKE_ERRORS.search(dry.stderr):
        raise MakeError("make -n reported: " + dry.stderr.strip()[:500])
    dry_text = _repo_relative(dry.stdout, root)
    problems = []
    for ln in db.split("\n"):
        if DB_RULE.match(ln) and not TARGET_ASSIGN.match(ln) and ("$(" in ln or "${" in ln):
            problems.append("unexpanded reference in a prerequisite line: " + ln.strip()[:160])
    for ln in dry_text.split("\n"):
        if MAKE_SHAPED.search(ln):
            problems.append("unexpanded make reference in a dry-run recipe: " + ln.strip()[:160])
    files = set(REF.findall(raw)) | set(REF.findall(db)) | set(REF.findall(dry_text))
    dirs = set(INC.findall(raw)) | set(INC.findall(db)) | set(INC.findall(dry_text))
    return (sorted(files), sorted({posixpath.normpath(d) for d in dirs}), problems,
            sorted(set(shell_paths)))


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
    for label, (text, line) in REFUSED.items():
        try:
            run(text, ["firmware/src/Util/A.h"])
            ck("refused: " + label, "returned", "raised Refusal")
        except Refusal as e:
            ck("refused: " + label + " (names Makefile:%d)" % line,
               str(e).startswith("Makefile:%d:" % line), True)
    for label, text in PASSING_SHELL.items():
        ck("not refused: " + label, run(text, ["firmware/src/Util/A.h"])[2], [])
    ck("literal shell path is harvested", run(PASSING_SHELL["shell with literal paths"])[0],
       ["firmware/src/Util/A.h"])
    ck("literal != path is harvested", run(PASSING_SHELL["!= with a literal file"])[0],
       ["firmware/src/Util/A.h"])
    ck("a directory literal under $(shell is returned as a directory to cover",
       run("X := $(shell find ../../firmware/src/W/include -type d)\nall:\n\t@:\n",
           ["firmware/src/W/include/a.h"])[3],
       [("firmware/src/W/include/", "firmware/src/W/include/__child__.h")])
    ck("...a filter lacking it leaves the synthetic child uncovered",
       uncovered(["firmware/src/W/include/__child__.h"], ["firmware/src/W/include/a.h"]),
       ["firmware/src/W/include/__child__.h"])
    ck("...a `dir/**` filter covers it",
       uncovered(["firmware/src/W/include/__child__.h"], ["firmware/src/W/**"]), [])
    for label, (text, n) in LOUD.items():
        ck("loud fix: " + label, len(run(text)[2]), n)
    for form in ("V = x", "V := x", "V ::= x", "V ?= x", "V += x", "V != x",
                 "override V = x", "export V := x", "private V = x"):
        ck("TARGET_ASSIGN matches `%s`" % form,
           bool(TARGET_ASSIGN.match("all: " + form)), True)
    ck("TARGET_ASSIGN does not match a prerequisite list",
       bool(TARGET_ASSIGN.match("all: a.o b.o")), False)
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
        paths, dirs, blind, shell_paths = analyse(MAKEFILE, "run")
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
    for disp, test in shell_paths:
        note = (disp + "  (a directory read by $(shell/$(file: a file beneath "
                "it is not covered)") if disp.endswith("/") else disp
        if uncovered([test], pr):
            bad_pr.append(note)
        if uncovered([test], push):
            bad_push.append(note)
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
