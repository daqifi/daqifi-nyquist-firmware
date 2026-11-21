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

HEADERS REACHED THROUGH -I (#1300). A host-test source can #include a header
from any directory the Makefile puts on the compiler's -I path without the
Makefile naming that header, so the text harvest above never sees it. For those,
`gcc -MM -MG` is run over every tests/host/*.c with the Makefile's own -I
directories (resolved from its `-I...` tokens and simple `VAR := path`
assignments, so no second hand-maintained list of directories) and every
firmware/src file the preprocessor reports is required in both filters too.

WHAT THE -MM PASS DOES NOT SEE: (a) a header that only a -D-conditional #include
reaches under a flag this pass does not pass; (b) a -I directory the checker
cannot resolve from the Makefile text (printed as a NOTE, never ignored
silently); (c) the includes of firmware .c files the Makefile COMPILES -- only
the tests/host/*.c sources are preprocessed; (d) a directory order different
from the real per-target one, which can only change WHICH same-named header
is reported, not whether one is.

WHAT IT DOES NOT CHECK: that the filters contain nothing superfluous (a stale
entry is harmless), that a referenced file exists, or anything about the push
and pull_request filters agreeing with each other beyond covering this set.
"""
import fnmatch
import os
import re
import subprocess
import sys

MAKEFILE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "Makefile")
WORKFLOW = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                        "..", "..", ".github", "workflows", "host-tests.yml")

# `../../firmware/src/...` is how every host-test target names a firmware file,
# whether through a variable definition or inline in a recipe. Both forms are
# the same evidence: the Makefile text mentions the path.
REF = re.compile(r"\.\./\.\./(firmware/src/[A-Za-z0-9_./-]+\.(?:c|h))")
ENTRY = re.compile(r"^\s*-\s*'([^']+)'", re.M)


def referenced(makefile_text):
    return sorted(set(REF.findall(makefile_text)))


_VAR = re.compile(r"^([A-Za-z0-9_]+)\s*[:?]?=\s*([^#\n]*)", re.M)
_ITOK = re.compile(r"-I(\S+)")
_FIND = re.compile(r"find\s+(\S+)\s+-type\s+d")


def _expand(text, variables, depth=0):
    """Substitute $(VAR) from simple assignments; unresolved ones stay."""
    if depth > 8:
        return text
    return re.sub(r"\$\(([A-Za-z0-9_]+)\)",
                  lambda m: _expand(variables[m.group(1)], variables, depth + 1)
                  if m.group(1) in variables else m.group(0), text)


def include_dirs(makefile_text, base):
    """(existing -I dirs relative to `base`, unresolved -I tokens). Resolved
    from the Makefile's own text; `find DIR -type d` adds DIR's subtree, as
    HIF881_INC does for winc/include."""
    variables = {m.group(1): m.group(2).strip()
                 for m in _VAR.finditer(makefile_text)}
    dirs, unresolved = [], []

    def add(d):
        if d not in dirs and os.path.isdir(os.path.join(base, d)):
            dirs.append(d)

    for tok in _ITOK.findall(makefile_text):
        d = _expand(tok, variables)
        if d.startswith(","):
            continue  # `$(addprefix -I,$(shell find ...))`: _FIND handles it
        if "$" in d:
            unresolved.append(tok)
        else:
            add(d)
    for m in _FIND.finditer(makefile_text):
        root = _expand(m.group(1), variables)
        if "$" in root:
            unresolved.append(m.group(0))
            continue
        for dp, _, _ in os.walk(os.path.join(base, root)):
            add(os.path.relpath(dp, base))
    dirs.sort(key=lambda d: (not d.startswith("stubs"), d))
    return dirs, sorted(set(unresolved))


def header_deps(base, dirs):
    """firmware/src files the host-test sources reach, via gcc -MM -MG.
    -MM still prints its dependency list when a #error fires (two sources need
    Makefile -D values), so the exit status is NOT the success test: an EMPTY
    dependency list for a source is, and is fatal."""
    from concurrent.futures import ThreadPoolExecutor
    found = set()
    srcs = sorted(f for f in os.listdir(base) if f.endswith(".c"))

    def run(src):
        r = subprocess.run(["gcc", "-MM", "-MG"] + ["-I" + d for d in dirs]
                           + [src], cwd=base, stdout=subprocess.PIPE,
                           stderr=subprocess.DEVNULL, universal_newlines=True)
        return src, r.stdout.replace("\\\n", " ").split()

    with ThreadPoolExecutor(max_workers=8) as pool:
        results = list(pool.map(run, srcs))
    for src, toks in results:
        if not toks or not toks[0].endswith(":"):
            print("ERROR: gcc -MM produced no dependency list for %s" % src)
            sys.exit(2)
        for t in toks[1:]:
            full = os.path.normpath(os.path.join(base, t))
            rel = os.path.relpath(full, os.path.join(base, "..", ".."))
            if rel.startswith("firmware/src/"):
                found.add(rel.replace(os.sep, "/"))
    return sorted(found)


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
    """Both directions, because a checker that cannot FAIL is not a checker."""
    checks = 0

    def ck(label, got, want):
        nonlocal checks
        checks += 1
        if got != want:
            print("  self-test FAIL: %s: got %r want %r" % (label, got, want))
            sys.exit(1)

    mk = "  $(CC) -o x ../../firmware/src/services/csv_encoder.c\n" \
         "SRC := ../../firmware/src/Util/CRC32.c\n"
    ck("both reference forms are found -- a variable and an inline recipe",
       referenced(mk),
       ["firmware/src/Util/CRC32.c", "firmware/src/services/csv_encoder.c"])

    wf = ("on:\n  pull_request:\n    paths:\n"
          "      - 'firmware/src/Util/CRC32.c'\n"
          "  push:\n    paths:\n"
          "      - 'firmware/src/Util/CRC32.c'\n"
          "      - 'firmware/src/services/csv_encoder.c'\n")
    pr, push = filters(wf)
    ck("the two filter blocks are read separately", (len(pr), len(push)), (1, 2))
    # POSITIVE CONTROL: the real defect shape -- present in push, absent in PR.
    ck("a file missing from ONE filter is reported",
       uncovered(referenced(mk), pr), ["firmware/src/services/csv_encoder.c"])
    ck("...and is NOT reported against the filter that has it",
       uncovered(referenced(mk), push), [])
    # a glob must cover its members, or every header would be a false alarm
    ck("a glob entry covers matching paths",
       uncovered(["firmware/src/services/a.h"], ["firmware/src/services/*.h"]),
       [])
    import tempfile
    with tempfile.TemporaryDirectory() as td:
        for d in ("stubs", "fw/a", "fw/tree/x", "fw/tree/y"):
            os.makedirs(os.path.join(td, d))
        mk2 = ("FWA := fw/a\nINC := -Istubs -I$(FWA)\n"
               "X := $(addprefix -I,$(shell find fw/tree -type d))\n"
               "Y := -I$(NOPE)/z\n")
        dirs, unres = include_dirs(mk2, td)
        ck("-I tokens resolve through a variable, stubs first",
           dirs[:2], ["stubs", "fw/a"])
        ck("find DIR -type d adds the whole subtree",
           [d for d in dirs if d.startswith("fw/tree")],
           ["fw/tree", "fw/tree/x", "fw/tree/y"])
        ck("an unresolvable -I is REPORTED, not dropped", len(unres) >= 1, True)
        # POSITIVE CONTROL for the whole pass: a header reachable ONLY through
        # -I, named nowhere in the Makefile, must be found -- and must then be
        # reported uncovered by a filter that lacks it.
        host = os.path.join(td, "tests", "host")
        util = os.path.join(td, "firmware", "src", "Util")
        os.makedirs(host)
        os.makedirs(util)
        open(os.path.join(util, "Hidden1300.h"), "w").write("\n")
        open(os.path.join(host, "test_x.c"), "w").write(
            '#include "Hidden1300.h"\nint main(void){return 0;}\n')
        got = header_deps(host, ["../../firmware/src/Util"])
        ck("a header reached only through -I is found", got,
           ["firmware/src/Util/Hidden1300.h"])
        ck("...and reported when no filter lists it",
           uncovered(got, ["firmware/src/Util/Other.h"]), got)
    print("  self-test: %d/%d checks passed" % (checks, checks))


def main():
    if "--self-test" in sys.argv:
        self_test()
        return 0
    mk = open(MAKEFILE, encoding="utf-8").read()
    wf = open(WORKFLOW, encoding="utf-8").read()
    paths = referenced(mk)
    base = os.path.dirname(MAKEFILE)
    dirs, unresolved = include_dirs(mk, base)
    via_inc = header_deps(base, dirs)
    for tok in unresolved:
        print("NOTE: -I token the checker could not resolve from the Makefile "
              "text (headers under it are not checked): %s" % tok)
    paths = sorted(set(paths) | set(via_inc))
    pr, push = filters(wf)
    bad_pr, bad_push = uncovered(paths, pr), uncovered(paths, push)
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
