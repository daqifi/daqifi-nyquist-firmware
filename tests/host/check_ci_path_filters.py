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

HEADERS REACHED THROUGH -I / -D (#1300). A host-test source can #include a
header from any directory the compiler's -I path holds without the Makefile
naming that header, so the text harvest above never sees it. For those, the
compile commands themselves are taken from make: `LC_ALL=C make -n -B -k run`
prints every command the `run` target would execute with every variable, -I
and -D already expanded BY MAKE, so nothing here parses Makefile text for flags
(#1298 spent six rounds re-implementing make's expansion and lost to one more
spelling each time). Each command that compiles a .c file is re-run as
`gcc -MM -MG` with only its own -I/-D/-U/-iquote/-isystem/-include/-std flags,
and every firmware/src file the preprocessor reports is required in both
filters too. That includes the headers of firmware .c files the Makefile
compiles, and -D-conditional #includes under the flags the target really passes.
A `cp A B` the recipe performs before compiling B is followed (B is
preprocessed from A's content, from tests/host).

FAILS CLOSED, with exit 2 and a message, on: make exiting non-zero or timing
out; zero compile commands found; a recipe line shlex cannot tokenise; a
`cc`/`gcc` line that is neither a compile nor a preprocess-only (-E) run; a
source that does not exist and is not the target of a `cp`; any source whose
-MM output is empty. There is no note-and-pass path.

THE TRUE BOUNDARY. `make -n` prints commands; it does not run them. So a path
the SHELL composes when the recipe runs is invisible here: a source named with a
shell variable (`/tmp/.x.$$.c`, assembled by `cat`) is counted and reported as
skipped, not preprocessed, and a compiler invoked indirectly (`python3 x.py cc
f.c`) is not seen. Headers behind a -D value the `run` target never passes are
not reached either, nor are targets not under `run`. The text harvest above
still covers any firmware path the Makefile names.

WHAT IT DOES NOT CHECK: that the filters contain nothing superfluous (a stale
entry is harmless), that a referenced file exists, or anything about the push
and pull_request filters agreeing with each other beyond covering this set.
"""
import fnmatch
import os
import re
import shlex
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


class CheckError(Exception):
    """A condition under which the -MM pass cannot vouch for completeness."""


_CMD_END = {";", ";;", "&&", "||", "|", "|&", "(", ")", "`", "&"}
_LEAD = {"{", "!", "then", "do", "else", "elif", "if", "while", "until", "time"}
_WITH_ARG = ("-I", "-D", "-U", "-iquote", "-isystem", "-include", "-idirafter")
_KEEP_PREFIX = _WITH_ARG + ("-std=",)
_SKIP_WITH_ARG = ("-o", "-MF", "-MT", "-MQ", "-x")


def run_make(base, timeout=300):
    try:
        r = subprocess.run(["make", "-n", "-B", "-k", "run"], cwd=base,
                           env=dict(os.environ, LC_ALL="C"),
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                           universal_newlines=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        raise CheckError("`make -n -B -k run` timed out after %ds" % timeout)
    except OSError as e:
        raise CheckError("cannot run make: %s" % e)
    if r.returncode != 0:
        raise CheckError("`make -n -B -k run` exited %d:\n%s"
                         % (r.returncode, r.stdout[-2000:]))
    return r.stdout


def split_commands(make_output):
    """Every simple command in make's -n output as an argv list. Backslash-
    continued lines are one logical line; shlex (quotes respected) splits it,
    then shell operators split it into commands and redirections are dropped."""
    cmds = []
    for line in make_output.replace("\\\n", " ").split("\n"):
        if not line.strip():
            continue
        try:
            lex = shlex.shlex(line, posix=True, punctuation_chars=";()<>|&`")
            lex.whitespace_split = True
            toks = list(lex)
        except ValueError as e:
            raise CheckError("cannot tokenise make output line (%s): %.200s"
                             % (e, line))
        cur, skip = [], False
        for tok in toks + [";"]:
            if skip:
                skip = False
                continue
            if tok in _CMD_END:
                if cur:
                    cmds.append(cur)
                cur = []
            elif tok and set(tok) <= set("<>&") and ("<" in tok or ">" in tok):
                skip = True  # redirection: the next token is its target
            else:
                cur.append(tok)
        # (the trailing ";" flushed the last command)
    out = []
    for argv in cmds:
        while argv and (argv[0] in _LEAD or re.match(r"^[A-Za-z_]\w*=", argv[0])):
            argv = argv[1:]
        if argv:
            out.append(argv)
    return out


def parse_compile(argv):
    """(kept flags, .c sources) for a compile command; None if `argv` is not a
    cc/gcc run at all; "E" for a preprocess-only run (nothing compiled)."""
    names = {"cc", "gcc"}
    if os.environ.get("CC"):
        names.add(os.environ["CC"])
    if argv[0] not in names:
        return None
    if "-E" in argv:
        return "E"
    kept, srcs, i = [], [], 1
    while i < len(argv):
        a = argv[i]
        if a in _SKIP_WITH_ARG:
            i += 2
            continue
        if a in _WITH_ARG:
            kept += argv[i:i + 2]
            i += 2
            continue
        if a.startswith(_KEEP_PREFIX):
            kept.append(a)
        elif a.endswith(".c"):
            srcs.append(a)
        i += 1
    return kept, srcs


def header_deps(base, make_output):
    """(firmware/src files reached, number of compile commands, shell-composed
    sources skipped). -MM still prints its dependency list when a #error fires,
    so the exit status is NOT the success test; an EMPTY list for a source is,
    and is fatal."""
    from concurrent.futures import ThreadPoolExecutor
    cmds = split_commands(make_output)
    copies = {}
    for argv in cmds:
        ops = [a for a in argv[1:] if not a.startswith("-")]
        if argv[0] == "cp" and len(ops) == 2:
            copies[ops[1]] = ops[0]
    jobs, skipped, ncompile = {}, 0, 0
    for argv in cmds:
        pc = parse_compile(argv)
        if pc is None or pc == "E":
            continue
        kept, srcs = pc
        if not srcs:
            if "-c" in argv or "-o" in argv:
                continue  # link step (objects only): nothing to preprocess
            raise CheckError("cc/gcc line that is neither compile nor -E: %s"
                             % " ".join(argv)[:200])
        ncompile += 1
        for s in srcs:
            if "$" in s:
                skipped += 1
                continue
            jobs[(tuple(kept), s)] = True
    if ncompile == 0:
        raise CheckError("make -n printed no compile command; the extraction "
                         "(or the Makefile's `run` target) is broken")

    def run(job):
        kept, src = job
        cmd = ["gcc", "-MM", "-MG"] + list(kept)
        stdin = None
        if os.path.isfile(os.path.join(base, src)):
            cmd.append(src)
        elif src in copies and os.path.isfile(os.path.join(base, copies[src])):
            cmd += ["-I.", "-x", "c", "-"]
            stdin = open(os.path.join(base, copies[src]), "rb").read()
        else:
            raise CheckError("source %s does not exist and no `cp` produces it"
                             % src)
        r = subprocess.run(cmd, cwd=base, input=stdin, stdout=subprocess.PIPE,
                           stderr=subprocess.DEVNULL)
        toks = r.stdout.decode("utf-8", "replace").replace("\\\n", " ").split()
        if not toks or not toks[0].endswith(":"):
            raise CheckError("gcc -MM produced no dependency list for %s (%s)"
                             % (src, " ".join(kept)))
        return toks[1:]

    found = set()
    with ThreadPoolExecutor(max_workers=8) as pool:
        for deps in pool.map(run, list(jobs)):
            for t in deps:
                full = os.path.normpath(os.path.join(base, t))
                rel = os.path.relpath(full, os.path.join(base, "..", ".."))
                if rel.startswith("firmware/src/"):
                    found.add(rel.replace(os.sep, "/"))
    return sorted(found), ncompile, skipped


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

    def fails(fn):
        try:
            fn()
        except CheckError:
            return True
        return False

    # tokeniser: compounds split, prose is not a flag, redirects dropped
    cmds = split_commands("echo 'I/O -Ifake' && cc -c a.c -Ix; \\\n"
                          "  x=`cc -E -P b.c` 2>/dev/null\n")
    ck("compounds split and echoed prose stays an echo operand",
       [c[0] for c in cmds][:3], ["echo", "cc", "cc"])
    ck("-E runs are preprocess-only, not compiles",
       [parse_compile(c) for c in cmds[1:3]], [(["-Ix"], ["a.c"]), "E"])
    ck("an untokenisable line fails closed",
       fails(lambda: split_commands("echo 'unterminated\n")), True)
    # fail-closed: no compile lines at all
    ck("zero compile commands -> CheckError",
       fails(lambda: header_deps(".", "echo hi\nmake: Nothing to be done\n")),
       True)

    def tree(td, makefile, host_c):
        host = os.path.join(td, "tests", "host")
        util = os.path.join(td, "firmware", "src", "Util")
        os.makedirs(host)
        os.makedirs(util)
        for h in ("Hidden1300.h", "Cond1300.h"):
            open(os.path.join(util, h), "w").write("\n")
        open(os.path.join(host, "Makefile"), "w").write(makefile)
        for name, body in host_c.items():
            open(os.path.join(host, name), "w").write(body)
        return host

    mk = ("INC := -I../../firmware/src/Util\n"
          "run:\n\t$(CC) -DFOO_1300 $(INC) -o x test_x.c\n")
    srcs = {"test_x.c": '#include "Hidden1300.h"\n#ifdef FOO_1300\n'
                        '#include "Cond1300.h"\n#endif\n'
                        'int main(void){return 0;}\n'}
    with tempfile.TemporaryDirectory() as td:
        host = tree(td, mk, srcs)
        got, n, sk = header_deps(host, run_make(host))
        # POSITIVE CONTROL: Hidden1300.h is named nowhere in the Makefile and
        # is reached only through -I (a variable make expanded, not us).
        ck("a header reached only through -I is found (and the -D one too)",
           got, ["firmware/src/Util/Cond1300.h",
                 "firmware/src/Util/Hidden1300.h"])
        ck("...and reported when no filter lists it",
           uncovered(got, ["firmware/src/Util/Other.h"]), got)
        # the -D-conditional include is invisible without the target's -D:
        r = subprocess.run(["gcc", "-MM", "-MG", "-I../../firmware/src/Util",
                            "test_x.c"], cwd=host, stdout=subprocess.PIPE,
                           universal_newlines=True)
        ck("WITHOUT -DFOO_1300 (the c4ed1fb9 pass) Cond1300.h is missed",
           "Cond1300.h" in r.stdout, False)
        ck("make reported 1 compile command, 0 skipped", (n, sk), (1, 0))
    with tempfile.TemporaryDirectory() as td:
        host = tree(td, "run:\n\t@echo nothing\n", srcs)
        ck("a run target with no compile -> CheckError",
           fails(lambda: header_deps(host, run_make(host))), True)
    with tempfile.TemporaryDirectory() as td:
        host = tree(td, "run:\n\t$(CC) -o x nosuch.c\n", srcs)
        ck("a source that does not exist -> CheckError",
           fails(lambda: header_deps(host, run_make(host))), True)
    with tempfile.TemporaryDirectory() as td:
        host = tree(td, "include nosuch1300.mk\nrun:\n\t@echo x\n", srcs)
        ck("make exiting non-zero -> CheckError",
           fails(lambda: run_make(host)), True)
    with tempfile.TemporaryDirectory() as td:
        host = tree(td, "run:\n\tcp ../../firmware/src/Util/Copy1300.c"
                    " Copy1300_uut.c\n\t$(CC) -I../../firmware/src/Util -o x Copy1300_uut.c\n", {})
        open(os.path.join(td, "firmware", "src", "Util", "Copy1300.c"),
             "w").write('#include "Hidden1300.h"\nint main(void){return 0;}\n')
        got, _, _ = header_deps(host, run_make(host))
        ck("a cp-produced source is preprocessed from its origin",
           got, ["firmware/src/Util/Hidden1300.h"])
    print("  self-test: %d/%d checks passed" % (checks, checks))


def main():
    if "--self-test" in sys.argv:
        self_test()
        return 0
    mk = open(MAKEFILE, encoding="utf-8").read()
    wf = open(WORKFLOW, encoding="utf-8").read()
    paths = referenced(mk)
    base = os.path.dirname(MAKEFILE)
    try:
        via_inc, ncompile, skipped = header_deps(base, run_make(base))
    except CheckError as e:
        print("ERROR: check_ci_path_filters cannot vouch for completeness "
              "(failing closed):\n%s" % e)
        return 2
    print("  -MM pass: %d compile command(s), %d firmware/src header/source(s)"
          " reached, %d shell-composed source(s) not preprocessed"
          % (ncompile, len(via_inc), skipped))
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
