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
import re
import sys

MAKEFILE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "Makefile")
WORKFLOW = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                        "..", "..", ".github", "workflows", "host-tests.yml")

# `../../firmware/src/...` is how every host-test target names a firmware file,
# whether through a variable definition or inline in a recipe. Both forms are
# the same evidence: the Makefile text mentions the path.
REF = re.compile(r"\.\./\.\./(firmware/src/[A-Za-z0-9_./-]+\.(?:c|h))")
ENTRY = re.compile(r"^\s*-\s*'([^']+)'", re.M)


# A source file named through a variable: `$(FW_UTIL)/Logger.c`. The literal
# `../../firmware/src` text is on the VARIABLE's line, so REF alone never sees
# the file (#1178: "25 firmware files covered" while the host tests read 28).
ASSIGN = re.compile(r"^([A-Za-z_][A-Za-z0-9_]*)\s*(?::|\?)?=\s*([^#\n]*?)\s*$", re.M)
COMPOSED = re.compile(r"\$\(([A-Za-z_][A-Za-z0-9_]*)\)(/[A-Za-z0-9_./-]*\.(?:c|h))\b")
VARREF = re.compile(r"\$\(([A-Za-z_][A-Za-z0-9_]*)\)")


def _expand(value, defs, seen=()):
    """Expand $(VAR) using simple assignments; None if any reference cannot be
    resolved (undefined, recursive, or a make function)."""
    def sub(m):
        name = m.group(1)
        if name in seen or name not in defs:
            raise KeyError(name)
        got = _expand(defs[name], defs, seen + (name,))
        if got is None:
            raise KeyError(name)
        return got
    try:
        return VARREF.sub(sub, value)
    except KeyError:
        return None


def unresolved(makefile_text):
    """`$(VAR)/x.c|h` tokens whose VAR cannot be expanded. Reported, never
    skipped: each is a possible firmware read this checker cannot see."""
    defs = dict(ASSIGN.findall(makefile_text))
    return sorted({m.group(0) for m in COMPOSED.finditer(makefile_text)
                   if _expand("$(%s)" % m.group(1), defs) is None})


def referenced(makefile_text):
    found = set(REF.findall(makefile_text))
    defs = dict(ASSIGN.findall(makefile_text))
    for m in COMPOSED.finditer(makefile_text):
        base = _expand("$(%s)" % m.group(1), defs)
        if base is not None:
            found.update(REF.findall(base + m.group(2)))
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
    # #1178: a variable-composed path is seen; an unresolvable one fails.
    mk2 = "FW_HAL := ../../firmware/src/HAL\nFW_X := $(FW_HAL)/Sub\n" \
          "A := $(FW_HAL)/WaitLoop.h\nB := $(FW_X)/y.c\n"
    ck("a variable-composed path is harvested, chained too",
       referenced(mk2), ["firmware/src/HAL/Sub/y.c", "firmware/src/HAL/WaitLoop.h"])
    ck("a resolvable composition is not reported unresolved", unresolved(mk2), [])
    ck("an unresolvable composition is reported, not skipped",
       unresolved("A := $(NOPE)/z.c\n"), ["$(NOPE)/z.c"])
    ck("a recursive definition is unresolvable, not a hang",
       unresolved("R := $(R)/z\nA := $(R)/z.c\n"), ["$(R)/z.c"])
    print("  self-test: %d/%d checks passed" % (checks, checks))


def main():
    if "--self-test" in sys.argv:
        self_test()
        return 0
    mk = open(MAKEFILE, encoding="utf-8").read()
    wf = open(WORKFLOW, encoding="utf-8").read()
    paths = referenced(mk)
    blind = unresolved(mk)
    if blind:
        print("ERROR: the Makefile composes source paths from variables this")
        print("       checker cannot expand, so it cannot say whether the path")
        print("       filters cover them (#1178). Spell the path out in full")
        print("       (../../firmware/src/...) or teach the expander:")
        for b in blind:
            print("       unresolvable  %s" % b)
        return 1
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
