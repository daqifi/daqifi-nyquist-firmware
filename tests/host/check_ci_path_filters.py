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
import glob
import os
import posixpath
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
#
# SOUNDNESS RULE (the #1178 review round): harvesting the shapes we know is not
# enough -- a variable used in any OTHER shape (addprefix, wildcard, patsubst,
# -I flags, a continuation line) was neither harvested nor reported, which is
# "silently tolerates forms the expander does not handle". So every top-level
# `$(VAR)` / `${VAR}` whose expansion contains firmware/src must be ACCOUNTED
# FOR, by exactly one of:
#   * composed with a .c/.h suffix           -> harvested as a file
#   * inside $(addprefix P,LIST)/$(wildcard) -> expanded, files harvested
#   * an -I include directory                -> must lie under a filter entry
#                                               that covers files below it
#   * bare use of a file-list variable       -> its own definition was scanned
# and anything else FAILS, listed. There is no "skip" category.
ASSIGN = re.compile(r"^([A-Za-z_][A-Za-z0-9_]*)\s*(:|\?|\+)?=\s*(.*?)(?:\s+#.*)?\s*$")
VARNAME = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")
VARUSE = re.compile(r"\$[({]([A-Za-z_][A-Za-z0-9_]*)[)}]")
SUFFIX_CH = re.compile(r"\.(?:c|h)$")
BASE = os.path.dirname(os.path.abspath(__file__))


class Unsupported(Exception):
    pass


def splice(text):
    """Join backslash-newline continuations: an assignment value can span lines."""
    return re.sub(r"\\\r?\n", " ", text)


def nodes(s):
    """Top-level `$(...)` / `${...}` as (start, end, body). `$$` is a literal
    dollar (shell), never a make reference."""
    i = 0
    while i < len(s):
        if s[i] == "$" and i + 1 < len(s):
            if s[i + 1] == "$":
                i += 2
                continue
            if s[i + 1] in "({":
                op = s[i + 1]
                cl = ")" if op == "(" else "}"
                depth, j = 0, i + 1
                while j < len(s):
                    if s[j] == op:
                        depth += 1
                    elif s[j] == cl:
                        depth -= 1
                        if depth == 0:
                            break
                    j += 1
                yield (i, min(j + 1, len(s)), s[i + 2:j])
                i = j + 1
                continue
        i += 1


def split_args(body):
    out, depth, cur = [], 0, ""
    for ch in body:
        if ch in "({":
            depth += 1
        elif ch in ")}":
            depth -= 1
        if ch == "," and depth == 0:
            out.append(cur)
            cur = ""
        else:
            cur += ch
    return out + [cur]


def ev_str(s, defs, seen=(), base=None):
    """Expand make text. Supports $(VAR), $(addprefix P,L), $(wildcard ...);
    raises Unsupported for anything else (undefined, recursive, other
    functions) -- never guesses."""
    base = BASE if base is None else base
    out, last = [], 0
    for a, b, body in nodes(s):
        out.append(s[last:a])
        out.append(_ev_node(body, defs, seen, base))
        last = b
    out.append(s[last:])
    return "".join(out)


def _ev_node(body, defs, seen, base):
    if VARNAME.fullmatch(body):
        if body in seen or body not in defs:
            raise Unsupported(body)
        return ev_str(defs[body], defs, seen + (body,), base)
    m = re.match(r"(addprefix|wildcard)\s+(.*)$", body, re.S)
    if not m:
        raise Unsupported(body)
    fn, rest = m.groups()
    if fn == "addprefix":
        args = split_args(rest)
        if len(args) != 2:
            raise Unsupported(body)
        pre = ev_str(args[0], defs, seen, base)
        return " ".join(pre + w for w in ev_str(args[1], defs, seen, base).split())
    words = []
    for pat in ev_str(rest, defs, seen, base).split():
        got = sorted(glob.glob(os.path.join(base, pat)))
        if not got and "firmware/src" in pat:
            raise Unsupported("wildcard matches no file: " + pat)
        words += [os.path.relpath(g, base).replace(os.sep, "/") for g in got]
    return " ".join(words)


def soft(s, defs, seen=()):
    """Best-effort text of s with every known variable substituted and the
    rest dropped -- ONLY used to ask 'does this mention firmware/src'."""
    def sub(m):
        n = m.group(1)
        return "" if n in seen or n not in defs else soft(defs[n], defs, seen + (n,))
    return VARUSE.sub(sub, s)


def fw_via_vars(body, defs):
    return any("firmware/src" in soft("$(%s)" % n, defs)
               for n in VARUSE.findall("$(" + body + ")"))


def analyse(mk_text, base=None):
    """(files, dirs, problems): firmware files read, -I directories used, and
    every `$(VAR)` occurrence that touches firmware/src and could not be
    accounted for."""
    base = BASE if base is None else base
    lines = splice(mk_text).split("\n")
    defs = {}
    for ln in lines:
        m = ASSIGN.match(ln)
        if m:
            name, op, val = m.groups()
            defs[name] = (defs.get(name, "") + " " + val) if op == "+" else val
    # a variable that is a single directory-ish word: its USES are classified
    alias = set()
    for name, val in defs.items():
        if "firmware/src" in soft(val, defs) or "firmware/src" in val:
            try:
                v = ev_str(val, defs, (name,), base)
            except Unsupported:
                continue
            if v.strip() and len(v.split()) == 1 and not SUFFIX_CH.search(v):
                alias.add(name)
    files, dirs, problems = set(), set(), []
    files.update(REF.findall(splice(mk_text)))

    def prob(msg):
        if msg not in problems:
            problems.append(msg)

    for ln in lines:
        if ln.lstrip().startswith("#"):
            continue
        m = ASSIGN.match(ln)
        if m and m.group(1) in alias:
            continue  # a directory definition; each USE is classified below
        for lit in re.findall(r"-I(\.\./\.\./firmware/src/[^\s)$]*)", ln):
            dirs.add(lit)
        for a, b, body in nodes(ln):
            tok = re.match(r"/[^\s)$]*", ln[b:])
            tok = tok.group(0) if tok else ""
            if VARNAME.fullmatch(body):
                composed = re.match(r"/[A-Za-z0-9_./-]*\.(?:c|h)$", tok)
                try:
                    exp = ev_str("$(%s)" % body, defs, (), base)
                except Unsupported:
                    exp = None
                if composed:
                    if exp is None:
                        prob("$(%s)%s  (variable cannot be expanded)" % (body, tok))
                    else:
                        files.update(REF.findall(exp + tok))
                    continue
                if not fw_via_vars(body, defs):
                    continue
                if ln[max(0, a - 2):a] == "-I":
                    if exp is None:
                        prob("-I$(%s)%s  (cannot be expanded)" % (body, tok))
                    else:
                        dirs.add(exp + tok)
                elif tok == "":
                    if body in alias:
                        prob("$(%s) used bare but it is a directory" % body)
                else:
                    prob("$(%s)%s  (a firmware directory used outside -I)" % (body, tok))
                continue
            is_wild = body.startswith("wildcard") and "firmware/src" in body
            if not (is_wild or fw_via_vars(body, defs)):
                continue
            if not re.match(r"(addprefix|wildcard)\s", body):
                # call/patsubst/shell/...: not expandable, so the only thing
                # that is safe to let through is a FILE-LIST variable passed
                # whole (its definition line was scanned). A directory
                # variable inside an unknown function is the unaccounted shape.
                for n in VARUSE.findall("$(" + body + ")"):
                    if n in alias:
                        prob("$(%s)  (directory variable $(%s) inside a function "
                             "shape the expander does not handle)" % (body.strip(), n))
                continue
            mi = re.match(r"addprefix\s+-I\s*,\s*\$\(shell\s+find\s+(.+?)\s+-type\s+d\)\s*$",
                          body, re.S)
            try:
                if mi:
                    dirs.add(ev_str(mi.group(1), defs, (), base))
                    continue
                words = ev_str("$(%s)" % body, defs, (), base).split()
            except Unsupported:
                prob("$(%s)  (function shape the expander does not handle)" % body.strip())
                continue
            for w in words:
                if w.startswith("-I"):
                    dirs.add(w[2:])
                elif SUFFIX_CH.search(w):
                    files.update(REF.findall(w))
                else:
                    prob("$(%s)  (yields non-source word %s)" % (body.strip(), w))
    return sorted(files), sorted({posixpath.normpath(d) for d in dirs}), problems


def referenced(makefile_text, base=None):
    return analyse(makefile_text, base)[0]


def unresolved(makefile_text, base=None):
    return analyse(makefile_text, base)[2]


def dir_covered(d, patterns):
    """A directory is covered when a filter entry covers a file placed under it."""
    rel = d.replace("../../", "", 1)
    return any(fnmatch.fnmatch(rel + "/_new_.h", p) for p in patterns)


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
       [u.split()[0] for u in unresolved("A := $(NOPE)/z.c\n")], ["$(NOPE)/z.c"])
    ck("a recursive definition is unresolvable, not a hang",
       [u.split()[0] for u in unresolved("R := $(R)/z\nA := $(R)/z.c\n")],
       ["$(R)/z.c"])
    # --- the #1178 review round: shapes the first expander silently ignored ---
    mk3 = ("D := ../../firmware/src/drv\n"
           "A := $(addprefix $(D)/,a.c sub/b.c)\n"
           "B := $(addprefix $(D)/src/, \\\n\tc.c d.c)\n")
    ck("addprefix is expanded, with a continuation line",
       referenced(mk3), ["firmware/src/drv/a.c", "firmware/src/drv/src/c.c",
                         "firmware/src/drv/src/d.c", "firmware/src/drv/sub/b.c"])
    ck("...and nothing in them is left unaccounted", unresolved(mk3), [])
    # wildcard is globbed against the REAL checkout (tests/host is the base)
    wl = ("W := ../../firmware/src/libraries/scpi/libscpi\n"
          "H := $(wildcard $(W)/inc/scpi/*.h)\n")
    got = referenced(wl)
    ck("wildcard is globbed against the checkout and harvested",
       (len(got) > 0, all(g.startswith("firmware/src/libraries/scpi/libscpi/inc/scpi/")
                          and g.endswith(".h") for g in got)), (True, True))
    ck("a wildcard that matches nothing is reported, not dropped",
       len(unresolved("W := ../../firmware/src/NOSUCHDIR\n"
                      "H := $(wildcard $(W)/*.h)\n")), 1)
    ck("a literal firmware wildcard is harvested too",
       len(referenced("H := $(wildcard ../../firmware/src/libraries/scpi/libscpi/inc/scpi/*.h)\n")) > 0,
       True)
    # an unaccounted $(VAR) in a novel function shape
    ck("a novel function shape is reported, never skipped",
       len(unresolved("FW_X := ../../firmware/src/x\n"
                      "S := $(patsubst %,$(FW_X)/%,a.c)\n")), 1)
    ck("a bare use of a firmware DIRECTORY variable is reported",
       len(unresolved("FW_X := ../../firmware/src/x\nrule:\n\tls $(FW_X)\n")), 1)
    ck("a file-list variable used bare is accounted at its definition",
       unresolved("FW_X := ../../firmware/src/x\nL := $(FW_X)/a.c\nrule: $(L)\n"), [])
    ck("a dollar-dollar shell variable is not a make reference",
       unresolved("FW_X := ../../firmware/src/x\nrule:\n\techo $${FW_X}\n"), [])
    # -I directories: collected, and covered only by a glob that reaches below
    mki = "FW_X := ../../firmware/src/x\nF := -I$(FW_X)/inc -I../../firmware/src/y\n"
    ck("-I directories are collected, not skipped",
       analyse(mki)[1], ["../../firmware/src/x/inc", "../../firmware/src/y"])
    ck("an -I dir under a covering glob is covered",
       dir_covered("../../firmware/src/x/inc", ["firmware/src/x/**"]), True)
    ck("an -I dir with only a sibling file entry is NOT covered",
       dir_covered("../../firmware/src/x/inc", ["firmware/src/x/a.c"]), False)
    ck("addprefix -I over a shell find is an -I dir, not skipped",
       analyse("FW_X := ../../firmware/src/x\n"
               "F := $(addprefix -I,$(shell find $(FW_X)/include -type d))\n")[1],
       ["../../firmware/src/x/include"])
    print("  self-test: %d/%d checks passed" % (checks, checks))


def main():
    if "--self-test" in sys.argv:
        self_test()
        return 0
    mk = open(MAKEFILE, encoding="utf-8").read()
    wf = open(WORKFLOW, encoding="utf-8").read()
    paths, dirs, blind = analyse(mk)
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
    bad_dir = [(lab, d) for lab, pats in (("pull_request", pr), ("push", push))
               for d in dirs if not dir_covered(d, pats)]
    if not bad_pr and not bad_push and not bad_dir:
        print("OK: all %d firmware file(s) and %d -I director(ies) the host tests "
              "read are covered by both host-tests.yml path filters (%d / %d entries)."
              % (len(paths), len(dirs), len(pr), len(push)))
        return 0
    print("ERROR: a host-test target reads a firmware file that is NOT in a")
    print("       host-tests.yml path filter, so a change to that file does")
    print("       NOT trigger the job -- the check guarding it never runs.")
    print("       A GREEN GATE CONSTRAINS ONLY WHAT THE GATE RUNS.")
    for label, miss in (("pull_request", bad_pr), ("push", bad_push)):
        for p in miss:
            print("       missing from %-12s %s" % (label, p))
    for label, d in bad_dir:
        print("       -I dir not covered by a glob in %-12s %s" % (label, d))
    print("       Add the path to BOTH filters. Do not hand-enumerate the set:")
    print("       this list is derived from the Makefile's own references,")
    print("       which is the one list not written from the same blind spot")
    print("       that produced the gap (#970 / PR #991 round-2 audit).")
    return 1


if __name__ == "__main__":
    sys.exit(main())
