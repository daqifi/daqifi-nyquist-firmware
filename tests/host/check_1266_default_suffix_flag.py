#!/usr/bin/env python3
"""#1266: is the "MAC suffix applied" flag still cleared everywhere the suffix
stops being in force?

WHY A SOURCE CHECK. wifi_manager.c is not host-includable (FreeRTOS, Harmony,
the WINC driver), and the property is about WHERE the flag is written, which no
test that merely calls the predicate can reach. gDefaultSuffixApplied answers
"has the WINC's MAC suffix been applied to the runtime SSID?" and
wifi_manager_IsDefaultSsidSuffixPending() hides the bare default "DAQiFi" only
while it is false. The audit found two writers missing, each leaving the bare
default visible:

  r2 MED 1  wifi_manager_BootInit scrubs every retained-RAM static (#409) but
            not this flag, so after an MCLR reset it can still read true.
  r2 MED 2  nothing cleared it when the WINC was closed, so after standby +
            LAN:FACRESET + LAN:APPLY the runtime name is bare again while the
            flag says "applied".

THE INVARIANT pinned here is the complete set of writers, not the two sites:
exactly three assignments exist --
  set   `= true`  in ApplyDefaultSuffixAndDeviceName
  clear `= false` in wifi_manager_BootInit
  clear `= false` in the MainState DEINIT case (where the WINC is closed)
A fourth writer (or a missing one) fails the COUNT, so a site cannot be added
or dropped quietly.

WHAT THIS IS NOT. It does not drive the real state machine (nothing on the host
can) and does not prove the DEINIT event is processed in every power-down
sequence; that is argued in the PR and exercised on hardware.

Comments are STRIPPED before any check (string- and char-literal aware): the
prose around this fix names every identifier looked for, so a raw search would
be satisfied by the paragraphs explaining the fix instead of by the fix.

Usage:
    check_1266_default_suffix_flag.py --src <wifi_manager.c>
    check_1266_default_suffix_flag.py --src <wifi_manager.c> --mutants
    check_1266_default_suffix_flag.py --self-test
"""
import argparse
import re
import sys

FLAG = "gDefaultSuffixApplied"


def strip_comments(src):
    """Remove // and /* */ comments, leaving string and char literals intact.
    Newlines are preserved so line geometry survives."""
    out = []
    i, n = 0, len(src)
    while i < n:
        c = src[i]
        two = src[i:i + 2]
        if two == "//":
            while i < n and src[i] != "\n":
                i += 1
        elif two == "/*":
            i += 2
            while i < n and src[i:i + 2] != "*/":
                out.append("\n" if src[i] == "\n" else " ")
                i += 1
            i += 2
            out.append(" ")
        elif c in "\"'":
            q = c
            out.append(c)
            i += 1
            while i < n and src[i] != q:
                # blank the CONTENT: an assignment quoted in a string is not a
                # writer, and a literal must not satisfy a structural check.
                if src[i] == "\\":
                    out.append(" ")
                    i += 1
                out.append("\n" if src[i] == "\n" else " ")
                i += 1
            out.append(q)
            i += 1
        else:
            out.append(c)
            i += 1
    return "".join(out)


def brace_body(code, start):
    """Text of the {...} block whose '{' is the first one at or after start."""
    i = code.index("{", start)
    depth = 0
    for j in range(i, len(code)):
        if code[j] == "{":
            depth += 1
        elif code[j] == "}":
            depth -= 1
            if depth == 0:
                return code[i:j + 1]
    raise ValueError("unbalanced braces")


def function_body(code, signature_re):
    ms = list(re.finditer(signature_re, code, re.M))
    if len(ms) != 1:
        raise ValueError("expected exactly one definition of %r, found %d"
                         % (signature_re, len(ms)))
    return brace_body(code, ms[0].end())


def case_body(code, label):
    """Body of `case <label>:` up to the next top-level `case`/`default` of the
    same switch (brace depth 0 relative to the case)."""
    ms = list(re.finditer(r"\bcase\s+%s\s*:" % re.escape(label), code))
    if len(ms) != 1:
        raise ValueError("expected exactly one `case %s:`, found %d"
                         % (label, len(ms)))
    i = ms[0].end()
    depth = 0
    j = i
    while j < len(code):
        ch = code[j]
        if ch == "{":
            depth += 1
        elif ch == "}":
            if depth == 0:
                break
            depth -= 1
        elif depth == 0 and re.match(r"(case\s+\w+\s*:|default\s*:)", code[j:]):
            break
        j += 1
    return code[i:j]


ASSIGN = re.compile(r"\b%s\s*=\s*(true|false)\s*;" % FLAG)


def check(src):
    """Return a list of problems (empty = intact)."""
    problems = []
    code = strip_comments(src)

    allw = ASSIGN.findall(code)
    # the declaration `static volatile bool gDefaultSuffixApplied = false;`
    decl = len(re.findall(r"\bbool\s+%s\s*=\s*false\s*;" % FLAG, code))
    if decl != 1:
        problems.append("expected exactly one declaration of %s, found %d"
                        % (FLAG, decl))
    writers = len(allw) - decl
    if writers != 3:
        problems.append("expected exactly 3 writers of %s (1 set + 2 clears), "
                        "found %d" % (FLAG, writers))

    def one(label, body_fn, want):
        try:
            body = body_fn()
        except ValueError as e:
            problems.append("%s: %s" % (label, e))
            return
        got = ASSIGN.findall(body)
        if got != [want]:
            problems.append("%s must contain exactly one `%s = %s;`, found %r"
                            % (label, FLAG, want, got))

    one("ApplyDefaultSuffixAndDeviceName",
        lambda: function_body(
            code, r"^static\s+void\s+ApplyDefaultSuffixAndDeviceName\s*\([^)]*\)\s*"),
        "true")
    one("wifi_manager_BootInit",
        lambda: function_body(
            code, r"^void\s+wifi_manager_BootInit\s*\(\s*void\s*\)\s*"),
        "false")
    one("MainState DEINIT case",
        lambda: case_body(code, "WIFI_MANAGER_EVENT_DEINIT"),
        "false")
    return problems


MUTANTS = {
    "drop the BootInit clear (r2 MED 1)": (
        "    gDefaultSuffixApplied = false;  // #1266 — retained-RAM scrub", ""),
    "drop the DEINIT clear (r2 MED 2)": (
        "            gDefaultSuffixApplied = false;\n            mdns_responder_Stop();",
        "            mdns_responder_Stop();"),
    "drop the set in ApplyDefaultSuffixAndDeviceName": (
        "    gDefaultSuffixApplied = true;\n", ""),
}


def mutate(src, old, new):
    nl = "\r\n" if "\r\n" in src else "\n"
    old = old.replace("\n", nl)
    new = new.replace("\n", nl)
    if src.count(old) != 1:
        raise ValueError("mutant anchor found %d times: %r" % (src.count(old), old[:60]))
    return src.replace(old, new)


def run_mutants(src):
    bad = 0
    base = check(src)
    if base:
        print("FAIL: the unmutated source is already red: %s" % base)
        return 1
    for name, (old, new) in MUTANTS.items():
        try:
            m = mutate(src, old, new)
        except ValueError as e:
            print("FAIL: %s: %s" % (name, e))
            bad += 1
            continue
        if check(m):
            print("caught: %s" % name)
        else:
            print("SURVIVED: %s" % name)
            bad += 1
    # an added fourth writer must be caught too
    extra = src.replace("    gApplyInProgress = false;\r\n    gApplyInProgressDeadlineTick = 0;",
                        "    gApplyInProgress = false;\r\n    gDefaultSuffixApplied = false;\r\n    gApplyInProgressDeadlineTick = 0;", 1)
    if extra == src:
        extra = src.replace("    gApplyInProgress = false;\n    gApplyInProgressDeadlineTick = 0;",
                            "    gApplyInProgress = false;\n    gDefaultSuffixApplied = false;\n    gApplyInProgressDeadlineTick = 0;", 1)
    if extra == src:
        print("FAIL: could not place the extra-writer mutant")
        bad += 1
    elif check(extra):
        print("caught: a fourth writer")
    else:
        print("SURVIVED: a fourth writer")
        bad += 1
    print("RESULT: %d mutant problem(s)" % bad)
    return 1 if bad else 0


GOOD = """
static volatile bool gDefaultSuffixApplied = false;
static void ApplyDefaultSuffixAndDeviceName(stateMachineInst_t *p) {
    gDefaultSuffixApplied = true;
}
static int MainState(int ev) {
    switch (ev) {
        case WIFI_MANAGER_EVENT_INIT:
            break;
        case WIFI_MANAGER_EVENT_DEINIT:
            if (x) { SendEvent(1); break; }
            gDefaultSuffixApplied = false;
            break;
        case WIFI_MANAGER_EVENT_ERROR:
            break;
    }
}
void wifi_manager_BootInit(void) {
    gDefaultSuffixApplied = false;
}
"""


def self_test():
    fails = 0

    def expect(name, src, red):
        nonlocal fails
        got = bool(check(src))
        ok = got == red
        print("%s: %s" % ("ok" if ok else "FAIL", name))
        if not ok:
            fails += 1

    expect("intact source is green", GOOD, False)
    expect("BootInit clear dropped", GOOD.replace(
        "void wifi_manager_BootInit(void) {\n    gDefaultSuffixApplied = false;",
        "void wifi_manager_BootInit(void) {\n"), True)
    expect("DEINIT clear dropped", GOOD.replace(
        "            gDefaultSuffixApplied = false;\n            break;\n        case WIFI_MANAGER_EVENT_ERROR",
        "            break;\n        case WIFI_MANAGER_EVENT_ERROR"), True)
    expect("DEINIT clear moved into the neighbouring case", GOOD.replace(
        "            gDefaultSuffixApplied = false;\n            break;\n        case WIFI_MANAGER_EVENT_ERROR:\n",
        "            break;\n        case WIFI_MANAGER_EVENT_ERROR:\n            gDefaultSuffixApplied = false;\n"), True)
    expect("set dropped", GOOD.replace(
        "    gDefaultSuffixApplied = true;\n", ""), True)
    expect("a fourth writer", GOOD.replace(
        "case WIFI_MANAGER_EVENT_INIT:\n", "case WIFI_MANAGER_EVENT_INIT:\n            gDefaultSuffixApplied = false;\n"), True)
    # comment-only mention must not satisfy the check (the stripper's job)
    expect("clear present only in a comment", GOOD.replace(
        "            gDefaultSuffixApplied = false;\n            break;\n        case WIFI_MANAGER_EVENT_ERROR",
        "            // gDefaultSuffixApplied = false;\n            break;\n        case WIFI_MANAGER_EVENT_ERROR"), True)
    expect("clear present only in a block comment", GOOD.replace(
        "void wifi_manager_BootInit(void) {\n    gDefaultSuffixApplied = false;",
        "void wifi_manager_BootInit(void) {\n    /* gDefaultSuffixApplied = false; */"), True)
    expect("assignment inside a string literal is not a writer", GOOD.replace(
        "void wifi_manager_BootInit(void) {\n    gDefaultSuffixApplied = false;",
        "void wifi_manager_BootInit(void) {\n    puts(\"gDefaultSuffixApplied = false;\");"), True)
    print("RESULT: %d self-test failure(s)" % fails)
    return 1 if fails else 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--src")
    ap.add_argument("--mutants", action="store_true")
    ap.add_argument("--self-test", action="store_true")
    a = ap.parse_args()
    if a.self_test:
        return self_test()
    if not a.src:
        ap.error("--src is required")
    with open(a.src, encoding="utf-8", newline="") as f:
        src = f.read()
    if a.mutants:
        return run_mutants(src)
    problems = check(src)
    for p in problems:
        print("FAIL: %s" % p)
    print("RESULT: %d problem(s)" % len(problems))
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
