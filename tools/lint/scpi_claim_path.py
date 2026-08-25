#!/usr/bin/env python3
"""Every SYSTem:MEMory:* setter must take the streaming config-change claim.

## What this protects

The seven `SYSTem:MEMory:*` setters (`SD:BUFfer`, `WIFI:BUFfer`, `USB:BUFfer`,
`SAMPle:POOL`, `ENCoder:BUFfer`, `AUTO`, `RESet`) must not mutate the memory
config while a streaming session is starting or running. #857 gave them that
protection through a SINGLE shared helper, `SCPI_MemRunClaimed`, which takes
`Streaming_BeginConfigChange()` before dispatching to each command's own
`...Claimed` body.

Two edits would silently remove the protection, and neither is loud:

1. **Route one command off the helper** -- give it back a plain body that does
   its own stream-state test, or none. The other six keep working, so nothing
   on the bench necessarily notices.
2. **Gut the helper** -- keep all seven routed through it while removing the
   `Streaming_BeginConfigChange()` call inside it. Every command still "goes
   through the claim path" and none of them claims anything.

This checker fails on both.

## Why it is a source check and not a bench test

`test_857_mem_guard_claim.py`'s race arm is the only arm that separates a real
claim from a plain stream-state guard, and it can only hammer `SYST:MEM:AUTO`:
the race needs the claim held long enough to land inside, and AUTO is the only
one of the seven that holds it across work -- `SCPI_MemAutoBalanceClaimed` runs
`PrepareStreamingBuffers` (~1 s quiesce), while the other six are a parse, a
range check and one scalar store. Rotating the hammer to those six would miss a
window that does not exist and report a false negative, so the coverage gap
cannot be closed from the bench (test-suite #246).

It is closed here instead, where the property actually lives, at no bench cost.

Run:
    python3 tools/lint/scpi_claim_path.py
    python3 tools/lint/scpi_claim_path.py --self-test
"""

import argparse
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
# Reused rather than re-implemented: SCPIInterface.c has commented-out
# `.pattern` entries, so a checker that does not strip comments counts
# commands that are not shipped. `_REGISTRATION` / `_joined` handle adjacent
# string-literal concatenation, which is legal C and appears in that table.
from scpi_wiki_sync import (               # noqa: E402
    strip_c_comments, _joined,
)

# A table row, e.g. `{.pattern = "A", .callback = X,}`. Rows contain no nested
# braces, so a non-greedy brace pair is an exact row match.
#
# NOT scpi_wiki_sync's `_REGISTRATION`: that one requires `.pattern` to be
# IMMEDIATELY followed by `.callback`. Designated initializers are legal in any
# order, so `{.callback = X, .pattern = "..."}` is a valid registration it does
# not match -- and a setter this checker never examines is exactly the silent
# pass it exists to prevent (pre-merge audit on PR #863). Each field is found
# independently within the row instead.
_ROW = re.compile(r"\{([^{}]*)\}")
_ROW_PATTERN = re.compile(r'\.pattern\s*=\s*((?:"[^"]*"\s*)+)')
_ROW_CALLBACK = re.compile(r"\.callback\s*=\s*([A-Za-z_]\w*)")
_ROW_PATTERN_FIELD = re.compile(r"\.pattern\s*=")
_ROW_NULL = re.compile(r"\.pattern\s*=\s*NULL\b")

CLAIM_HELPER = "SCPI_MemRunClaimed"
CLAIM_BEGIN = "Streaming_BeginConfigChange"
CLAIM_END = "Streaming_EndConfigChange"
MEM_PREFIX = "SYSTem:MEMory:"

# The seven that existed when this checker was written. Used ONLY as a floor:
# a new setter must also claim (it is discovered from the table, not from this
# list), but dropping below seven means the table moved or the regex broke, and
# a checker that silently examines nothing is the failure this guards against.
KNOWN_MEM_SETTERS = 7

_STR_OR_CHAR = re.compile(r'"(?:\\.|[^"\\])*"' r"|'(?:\\.|[^'\\])*'", re.S)


def function_body(text, name):
    """The brace-delimited body of C function `name`, or None if not found.

    `text` must already have comments stripped. String and character literals
    are blanked before brace counting so a brace inside a literal -- e.g. a
    format string -- cannot unbalance the scan.
    """
    # Anchored to a line that STARTS with a return type, because the bare
    # form `\bNAME\s*\([^;{]*\)\s*\{` also matches a CALL inside a
    # condition: in `if (foo(a)) {` the group can absorb `a)` and take the
    # outer `)` before the brace, so a call would be read as a definition and
    # the wrong body returned. A prototype is still excluded -- it ends in `;`,
    # which `[^;{]*` cannot cross.
    sig = re.search(r"(?m)^[A-Za-z_][\w \t\*]*\b%s\s*\([^;{]*\)\s*\{"
                    % re.escape(name), text)
    if not sig:
        return None
    start = text.index("{", sig.start())
    masked = _STR_OR_CHAR.sub(lambda m: " " * len(m.group(0)), text)
    depth = 0
    for i in range(start, len(masked)):
        if masked[i] == "{":
            depth += 1
        elif masked[i] == "}":
            depth -= 1
            if depth == 0:
                return text[start:i + 1]
    return None


def _calls(body, name):
    """True iff `body` CALLS `name`, rather than merely mentioning it.

    A raw substring test passes a body that only names the helper -- e.g.
    `const char *marker = "SCPI_MemRunClaimed";` beside a direct call to the
    unclaimed inner body. That reports OK on a setter with no claim at all
    (pre-merge audit on PR #863). String literals are blanked first so a
    mention inside one cannot satisfy the call form either.
    """
    return re.search(r"\b%s\s*\(" % re.escape(name),
                     _STR_OR_CHAR.sub(lambda m: " " * len(m.group(0)), body)) is not None


def mem_setters(registrations):
    """Registered SYSTem:MEMory:* patterns that MUTATE config -> callbacks.

    Queries are excluded by the trailing `?`: they read and cannot corrupt a
    session's partition, so they are deliberately outside the claim.
    """
    return [(p, cb) for p, cb in registrations
            if p.startswith(MEM_PREFIX) and not p.endswith("?")]


def parse_registrations(text):
    """-> (registrations, unparsed) from an already-comment-stripped table.

    Fields are read independently within each row, so either designated-
    initializer order works. `unparsed` counts rows that HAVE a `.pattern`
    field but from which a (pattern, callback) pair could not be recovered --
    reported rather than skipped, because a row this cannot read is a command
    it cannot check.
    """
    regs, unparsed = [], 0
    for row in _ROW.findall(text):
        if not _ROW_PATTERN_FIELD.search(row):
            continue
        if _ROW_NULL.search(row):
            continue                      # libscpi's end-of-table sentinel
        pat, cb = _ROW_PATTERN.search(row), _ROW_CALLBACK.search(row)
        if pat and cb:
            regs.append((_joined(pat.group(1)), cb.group(1)))
        else:
            unparsed += 1
    return regs, unparsed


def check(source_text):
    """-> (problems, examined_count). Pure, so --self-test can drive it."""
    text = strip_c_comments(source_text)
    registrations, unparsed = parse_registrations(text)
    setters = mem_setters(registrations)
    problems = []

    if unparsed:
        problems.append(
            "%d command-table row(s) have a .pattern field this checker could "
            "not read. A row it cannot parse is a command it cannot check, so "
            "this fails rather than reporting on the rest." % unparsed)

    if not setters:
        problems.append(
            "no SYSTem:MEMory:* setters found in the command table -- the "
            "table moved or the registration regex broke. Refusing to report "
            "a pass on a file this checker could not read.")
        return problems, 0

    if len(setters) < KNOWN_MEM_SETTERS:
        problems.append(
            "found only %d SYSTem:MEMory:* setters, expected at least %d. "
            "If one was genuinely removed, lower KNOWN_MEM_SETTERS in this "
            "file and say why in the commit."
            % (len(setters), KNOWN_MEM_SETTERS))

    # (1) every setter routes through the shared helper
    for pattern, callback in sorted(setters):
        body = function_body(text, callback)
        if body is None:
            problems.append(
                "%s -> %s(): could not find that function to check it"
                % (pattern, callback))
        elif not _calls(body, CLAIM_HELPER):
            problems.append(
                "%s -> %s() does not go through %s(), so it can mutate the "
                "memory config while a session is arming or running (#857)."
                % (pattern, callback, CLAIM_HELPER))

    # (2) the shared helper still actually claims. Without this, (1) passes
    # while every command claims nothing -- all seven routed through a helper
    # that no longer holds the exclusion.
    helper = function_body(text, CLAIM_HELPER)
    if helper is None:
        problems.append(
            "%s() not found -- every setter above was checked for a call to a "
            "function that does not exist." % CLAIM_HELPER)
    else:
        for needed, why in ((CLAIM_BEGIN, "take"), (CLAIM_END, "release")):
            if not _calls(helper, needed):
                problems.append(
                    "%s() does not call %s(), so it does not %s the claim -- "
                    "routing through it protects nothing."
                    % (CLAIM_HELPER, needed, why))

    return problems, len(setters)


# --------------------------------------------------------------------------
# self-test: pure, no source tree needed
# --------------------------------------------------------------------------
_GOOD = '''
static scpi_result_t SCPI_MemRunClaimed(scpi_t *c, scpi_result_t (*b)(scpi_t *),
                                        const char *what) {
    StreamingCfgClaim claim = Streaming_BeginConfigChange();
    if (claim != STREAM_CFG_CLAIM_OK) { return SCPI_RejectCfgClaim(c, 1, what); }
    scpi_result_t r = b(c);
    Streaming_EndConfigChange();
    return r;
}
static scpi_result_t SCPI_SetMemSdBuf(scpi_t * context) {
    return SCPI_MemRunClaimed(context, SCPI_SetMemSdBufClaimed, "a{b}c");
}
static scpi_result_t SCPI_SetMemWifiBuf(scpi_t * context) {
    return SCPI_MemRunClaimed(context, SCPI_SetMemWifiBufClaimed, "x");
}
static scpi_result_t SCPI_GetMemFree(scpi_t * context) { return SCPI_RES_OK; }
static const scpi_command_t scpi_commands[] = {
    {.pattern = "SYSTem:MEMory:SD:BUFfer", .callback = SCPI_SetMemSdBuf,},
    {.pattern = "SYSTem:MEMory:WIFI:BUFfer", .callback = SCPI_SetMemWifiBuf,},
    {.pattern = "SYSTem:MEMory:FREE?", .callback = SCPI_GetMemFree,},
};
'''

_CHECKS = []


def _ck(name, got, want):
    ok = got == want
    _CHECKS.append(ok)
    if not ok:
        print("  self-test FAIL: %s: got %r want %r" % (name, got, want))


def self_test():
    # Floor lowered so the two-setter fixture is a valid sample.
    global KNOWN_MEM_SETTERS
    saved, KNOWN_MEM_SETTERS = KNOWN_MEM_SETTERS, 2
    try:
        probs, n = check(_GOOD)
        _ck("a compliant table is clean", probs, [])
        _ck("queries are not counted as setters", n, 2)

        # A brace inside a string literal must not truncate the body scan.
        _ck("literal braces do not unbalance the scan",
            CLAIM_HELPER in function_body(_GOOD, "SCPI_SetMemSdBuf"), True)

        # A CALL in a condition must not be mistaken for a definition. Without
        # the line anchor the signature regex backtracks across the inner `)`
        # and matches `if (helper_probe(x)) {`, returning that block as the
        # function body -- which would then be searched for the claim call.
        called = _GOOD + """
static scpi_result_t decoy(scpi_t * c) {
    if (helper_probe(c)) { return SCPI_RES_ERR; }
    return SCPI_RES_OK;
}
"""
        _ck("a call inside a condition is not read as a definition",
            function_body(called, "helper_probe"), None)

        # (1) one command routed off the helper
        off = _GOOD.replace(
            'return SCPI_MemRunClaimed(context, SCPI_SetMemWifiBufClaimed, "x");',
            'return SCPI_SetMemWifiBufClaimed(context);')
        probs, _ = check(off)
        _ck("a setter routed off the helper is caught",
            any("does not go through" in p for p in probs), True)

        # (2) the helper gutted -- every setter still routes through it
        gutted = _GOOD.replace(
            "StreamingCfgClaim claim = Streaming_BeginConfigChange();",
            "StreamingCfgClaim claim = STREAM_CFG_CLAIM_OK;")
        probs, _ = check(gutted)
        _ck("a gutted helper is caught (routing alone proves nothing)",
            any(CLAIM_BEGIN in p for p in probs), True)
        _ck("...and the gutted case is NOT reported as a routing problem",
            any("does not go through" in p for p in probs), False)

        # --- pre-merge audit on PR #863: two ways this reported a false OK ---

        # (1) reversed designated-initializer order. Legal C, and the old
        # parser required .pattern to be immediately followed by .callback, so
        # a setter written this way was never examined -- while the summary
        # line still claimed to have checked them all.
        rev = _GOOD.replace(
            '{.pattern = "SYSTem:MEMory:WIFI:BUFfer", .callback = SCPI_SetMemWifiBuf,},',
            '{.callback = SCPI_SetMemFoo, .pattern = "SYSTem:MEMory:FOO",},'
        ).replace(
            'static scpi_result_t SCPI_SetMemWifiBuf(scpi_t * context) {\n'
            '    return SCPI_MemRunClaimed(context, SCPI_SetMemWifiBufClaimed, "x");\n}',
            'static scpi_result_t SCPI_SetMemFoo(scpi_t * context) {\n'
            '    return SCPI_SetMemFooClaimed(context);\n}')
        probs, n = check(rev)
        _ck("a reversed-order registration is still examined", n, 2)
        _ck("...and an unclaimed one is caught",
            any("SYSTem:MEMory:FOO" in p for p in probs), True)

        # (2) a body that only MENTIONS the helper must not count as calling it.
        mention = _GOOD.replace(
            'return SCPI_MemRunClaimed(context, SCPI_SetMemSdBufClaimed, "a{b}c");',
            'const char *marker = "SCPI_MemRunClaimed"; (void)marker;\n'
            '    return SCPI_SetMemSdBufClaimed(context);')
        probs, _ = check(mention)
        _ck("a string mention is not accepted as a call",
            any("does not go through" in p for p in probs), True)

        # a row with a .pattern it cannot read is REPORTED, not skipped
        bad_row = _GOOD.replace(
            '{.pattern = "SYSTem:MEMory:SD:BUFfer", .callback = SCPI_SetMemSdBuf,},',
            '{.pattern = "SYSTem:MEMory:SD:BUFfer", .callback = ,},')
        probs, _ = check(bad_row)
        _ck("an unreadable table row fails rather than being skipped",
            any("could not read" in p for p in probs), True)

        # a commented-out registration must not be counted
        commented = _GOOD.replace(
            '    {.pattern = "SYSTem:MEMory:SD:BUFfer", .callback = SCPI_SetMemSdBuf,},',
            '//  {.pattern = "SYSTem:MEMory:SD:BUFfer", .callback = SCPI_SetMemSdBuf,},')
        _, n2 = check(commented)
        _ck("a commented-out registration is not counted", n2, 1)

        # vacuity: a file with no table must FAIL, not pass quietly
        probs, n3 = check("int main(void) { return 0; }")
        _ck("an unreadable table fails rather than passing", len(probs) >= 1, True)
        _ck("...and reports nothing examined", n3, 0)
        _ck("...with a message naming the refusal",
            any("could not read" in p for p in probs), True)
    finally:
        KNOWN_MEM_SETTERS = saved

    bad = _CHECKS.count(False)
    print("self-test: %d/%d checks passed" % (_CHECKS.count(True), len(_CHECKS)))
    return 1 if bad else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--scpi", default="firmware/src/services/SCPI/SCPIInterface.c",
                    help="path to SCPIInterface.c")
    ap.add_argument("--self-test", action="store_true",
                    help="run the built-in checks and exit (no source needed)")
    args = ap.parse_args()

    if args.self_test:
        return self_test()

    if not os.path.isfile(args.scpi):
        sys.exit("error: %r not found (run from the repo root, or pass --scpi)"
                 % args.scpi)
    with open(args.scpi, "r", encoding="utf-8", errors="replace") as fh:
        problems, examined = check(fh.read())

    if problems:
        print("FAIL: SYSTem:MEMory:* claim-path check (%d examined)" % examined)
        for p in problems:
            print("  - %s" % p)
        return 1
    print("OK: all %d SYSTem:MEMory:* setters take the claim via %s()"
          % (examined, CLAIM_HELPER))
    return 0


if __name__ == "__main__":
    sys.exit(main())
