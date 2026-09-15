#!/usr/bin/env python3
"""The bootloader-entry flag must reserve its whole 16-byte D-cache line.

## What this protects

`SCPIInterface.c` places `force_bootloader_flag` at a fixed address
(`FORCE_BOOTLOADER_FLAG_ADDR`) with `persistent, coherent` so `SYSTem
:FORceBoot`'s write reaches SRAM through the uncached KSEG1 alias and
survives a reset. PIC32MZ's D-cache is write-back and not hardware-coherent,
so if an ordinary CACHED global is linked into the same 16-byte line, a
write to that global dirties the line, and if the line is evicted before
the reset, its write-back replays the flag's old value over the magic --
silently turning a bootloader-entry request into a normal boot (#1083).

That happened by accident once already: before the fix, `force_bootloader_
flag` was a bare `uint32_t` reserving only its own 4 bytes, and the linker
placed the plain (cached) global `gLogLevels` in the remaining 12 bytes of
the same line. The fix is to declare the flag as a 4-word (16-byte) array
at that address, so the toolchain reserves the WHOLE line and nothing else
can ever be linked into it -- the same construct PowerApi.c's reboot-
handoff block already uses one cache line below, at
`POWER_REBOOT_HANDOFF_ADDR`.

This checker guards against that fix being quietly undone: a future edit
that shrinks the reservation back down (e.g. "cleanup" that turns the
array back into a scalar, or that drops `coherent` so writes stop bypassing
the cache) reintroduces the hazard without changing anything a build or a
bench run would notice -- the flag still gets written, `SYSTem:FORceBoot`
still usually works, and the failure is a narrow eviction-timing window
that a PICkit flash and a manual reboot will not hit.

## What this does NOT protect

This is a source-text check, not a linker-map check: it does not prove the
toolchain actually honors the reservation (see PR #1083's linker-map
evidence for that, and #1083's acceptance criteria for the bench-side
`SYSTem:FORceBoot` proof). It also does not notice a SECOND `persistent,
coherent` object placed inside the same 16-byte address range under a
different declaration shape than the one matched here -- see `check()`'s
docstring.

It masks comments, string/char literals, and literal `#if 0` blocks before
searching, so a declaration-shaped string constant or a disabled copy of
the real declaration cannot satisfy the check ahead of a genuinely broken
active one, and requires exactly one remaining candidate rather than
picking the first. It does not evaluate the C preprocessor in general
(`#ifdef NEVER_DEFINED`, a macro expanding to 0, `#if defined(X) && 0`
are all invisible to it), and does not special-case an `#else`/`#elif`
inside a masked `#if 0` block -- both are documented, narrow limitations
rather than silent gaps: every failure mode they can cause is "reports a
problem" (caught by CI), never a silent pass on a declaration this script
did not actually see.

## Usage

    python3 tools/lint/force_bootloader_cacheline.py
    python3 tools/lint/force_bootloader_cacheline.py --self-test
"""

import argparse
import os
import re
import sys

DEFAULT_SCPI = "firmware/src/services/SCPI/SCPIInterface.c"
ADDR_MACRO = "FORCE_BOOTLOADER_FLAG_ADDR"
CACHE_LINE_BYTES = 16

# C integer type widths this checker knows how to size. Sorted longest-first
# below so the alternation tries "uint32_t" before "int" etc.
_TYPE_SIZES = {
    "uint8_t": 1, "uint16_t": 2, "uint32_t": 4, "uint64_t": 8,
    "int8_t": 1, "int16_t": 2, "int32_t": 4, "int64_t": 8,
    "char": 1, "short": 2, "int": 4, "long": 4,
}

# Matches ONE declaration statement of the shape:
#   <qualifiers> <type> <name>[<size>]  __attribute__((<attrs>));
# with the array brackets optional (a bare scalar has no [N] at all).
# DOTALL because the type/name and the __attribute__(()) commonly sit on
# separate source lines (as they do in SCPIInterface.c today).
_DECL_RE = re.compile(
    r"\b(?P<type>%s)\b\s+(?P<name>[A-Za-z_][A-Za-z_0-9]*)\s*"
    r"(?:\[\s*(?P<size>\d+)\s*\])?\s*"
    r"__attribute__\s*\(\(\s*(?P<attrs>.*?)\)\)\s*;"
    % "|".join(sorted(_TYPE_SIZES, key=len, reverse=True)),
    re.DOTALL,
)

# Blanks /* block */ and // line comments so a commented-OUT declaration
# (e.g. a correct one left disabled while a broken replacement was added
# elsewhere) cannot satisfy the search below. Not string-literal-aware on
# its own -- see _mask_literals and _mask_disabled_blocks below, which
# close that specific gap (a Qodo /agentic_review finding on this PR: a
# declaration-shaped string constant, or one inside `#if 0`, could
# otherwise satisfy the search ahead of an undersized active one).
# Still proportionate to this file's narrow job (one declaration, in one
# source file we control), not the general-purpose splice-safe masking
# `cdef.py`/`hash_function.py` need for arbitrary C (#1066 tracks that
# harder class separately).
_COMMENT_RE = re.compile(r"/\*.*?\*/|//[^\r\n]*", re.DOTALL)

_STRING_RE = re.compile(r'"(?:\\.|[^"\\\n])*"')
_CHAR_RE = re.compile(r"'(?:\\.|[^'\\\n])*'")


def _mask_literals(text):
    """Blank the CONTENTS of string/char literals, keeping the quotes and
    the overall length (so this stays a pure textual pass with no effect
    on `;`/brace counting elsewhere). A string like
    `"__attribute__((persistent, coherent, address(FORCE_BOOTLOADER_FLAG_"
    `ADDR)))"` must not satisfy the search the way a real declaration does.
    """
    text = _STRING_RE.sub(lambda m: '"' + " " * (len(m.group(0)) - 2) + '"',
                           text)
    text = _CHAR_RE.sub(lambda m: "'" + " " * (len(m.group(0)) - 2) + "'",
                         text)
    return text


_PP_IF_RE = re.compile(r"^[ \t]*#[ \t]*(?:if|ifdef|ifndef)\b", re.MULTILINE)
_PP_ENDIF_RE = re.compile(r"^[ \t]*#[ \t]*endif\b", re.MULTILINE)
_PP_IF0_RE = re.compile(r"^[ \t]*#[ \t]*if[ \t]+0[ \t]*(?:$|//)", re.MULTILINE)


def _mask_disabled_blocks(text):
    """Blank every `#if 0` ... `#endif` block, line-preserving, nesting-aware.

    Deliberately narrow: recognizes only the literal `#if 0` spelling, not
    `#ifdef NEVER_DEFINED`, a macro that expands to 0, or `#if defined(X)
    && 0`. A real preprocessor is out of scope for a checker whose whole
    job is one declaration in one file we control.

    Does NOT special-case a `#else`/`#elif` inside a masked block, so a
    declaration in that live alternative branch is blanked too. That is
    conservative in the SAFE direction for this checker: the failure mode
    is "no declaration found" (a refusal, caught by CI) or a real reservation
    problem, never a silent pass on a declaration this function could not
    actually see. See the self-test's `if0_else_case`.
    """
    lines = text.split("\n")
    out = []
    masking = False
    nested_ifs = 0
    for line in lines:
        if masking:
            if _PP_IF_RE.match(line):
                nested_ifs += 1
            elif _PP_ENDIF_RE.match(line):
                if nested_ifs:
                    nested_ifs -= 1
                else:
                    masking = False
            out.append("")
            continue
        if _PP_IF0_RE.match(line):
            masking = True
            out.append("")
            continue
        out.append(line)
    return "\n".join(out)


def check(source_text):
    """-> list of problem strings; [] means the reservation is intact.

    Finds the one statement that places an object at
    `address(FORCE_BOOTLOADER_FLAG_ADDR)` and requires it to be:
      - qualified `persistent` AND `coherent` (both attributes present), and
      - sized (type width * array count, or just the type width for a bare
        scalar) at least CACHE_LINE_BYTES.

    Does not attempt to find every object that might overlap the address
    range some other way (e.g. two smaller `address()`-placed objects that
    together happen to span 16 bytes) -- the fix this guards is "one object,
    one full line", and that is what SCPIInterface.c does today.
    """
    code = _COMMENT_RE.sub(" ", source_text)
    code = _mask_literals(code)
    code = _mask_disabled_blocks(code)

    needle = "address(%s)" % ADDR_MACRO
    positions = [m.start() for m in re.finditer(re.escape(needle), code)]
    if not positions:
        return ["no declaration places anything at %s -- "
                 "SCPI_ForceBootloader has nothing to write" % ADDR_MACRO]
    if len(positions) > 1:
        return ["%d candidate declarations place something at %s in active "
                 "code (comments, string/char literals and `#if 0` blocks "
                 "excluded) -- refusing to guess which is authoritative; "
                 "there must be exactly one"
                 % (len(positions), ADDR_MACRO)]
    idx = positions[0]

    start = code.rfind(";", 0, idx) + 1
    end = code.find(";", idx)
    if end == -1:
        return ["unterminated declaration containing address(%s)" % ADDR_MACRO]
    stmt = code[start:end + 1]

    m = _DECL_RE.search(stmt)
    if not m or ADDR_MACRO not in m.group("attrs"):
        return ["found address(%s) but could not parse the declaring "
                 "statement as '<type> <name>[N] __attribute__((...));': %r"
                 % (ADDR_MACRO, stmt.strip())]

    problems = []
    attrs = m.group("attrs")
    # Token match, not substring: a hypothetical attribute spelled
    # "non_coherent" or "coherent_ish" must not satisfy `"coherent" in attrs`.
    attr_tokens = [tok.strip() for tok in attrs.split(",")]
    for required in ("persistent", "coherent"):
        if required not in attr_tokens:
            problems.append(
                "%s is missing '%s' in its attribute list (%r) -- without "
                "it the object is not the uncached, crt0-untouched "
                "placement force_bootloader_flag needs"
                % (m.group("name"), required, attrs))

    type_size = _TYPE_SIZES[m.group("type")]
    array_size = int(m.group("size")) if m.group("size") else 1
    total_bytes = type_size * array_size
    # Exactly one line, not merely "at least": FORCE_BOOTLOADER_FLAG_ADDR is
    # the last 16 bytes of physical RAM, so growing the reservation past 16
    # bytes runs off the end of RAM rather than protecting anything further
    # -- catch that misconfiguration here too, not only the under-reservation
    # this checker was written for.
    if total_bytes != CACHE_LINE_BYTES:
        problems.append(
            "%s reserves %d byte(s) (%s%s) at %s, not exactly one %d-byte "
            "D-cache line -- fewer bytes lets a cached (KSEG0) global link "
            "into the remainder and a later write-back can undo a "
            "bootloader-entry request (#1083); more bytes runs past the top "
            "of physical RAM at this fixed address"
            % (m.group("name"), total_bytes, m.group("type"),
               ("[%d]" % array_size) if m.group("size") else "",
               ADDR_MACRO, CACHE_LINE_BYTES))

    return problems


# ---------------------------------------------------------------------------
# self-test: pure, no source tree needed
# ---------------------------------------------------------------------------

_CHECKS = []


def _ck(name, got, want):
    ok = got == want
    _CHECKS.append(ok)
    if not ok:
        print("  self-test FAIL: %s: got %r want %r" % (name, got, want))


def self_test():
    fixed = (
        "static volatile uint32_t sForceBootloaderLine[4]\n"
        "    __attribute__((persistent, coherent, "
        "address(FORCE_BOOTLOADER_FLAG_ADDR)));\n"
    )
    _ck("fixed shape passes", check(fixed), [])

    prefix_noise = (
        "/* a block comment that mentions coherent and persistent and\n"
        " * cache lines and even a stray ; inside prose, none of it near\n"
        " * the real declaration below. */\n" + fixed
    )
    _ck("comment noise before the declaration does not break parsing",
        check(prefix_noise), [])

    pre_fix = (
        "volatile uint32_t force_bootloader_flag "
        "__attribute__((persistent, coherent, "
        "address(FORCE_BOOTLOADER_FLAG_ADDR)));\n"
    )
    pre_fix_problems = check(pre_fix)
    _ck("pre-fix bare scalar is flagged", len(pre_fix_problems), 1)
    _ck("pre-fix problem names the 4-byte reservation",
        "4 byte(s)" in pre_fix_problems[0] if pre_fix_problems else False, True)

    too_small_array = (
        "volatile uint8_t sForceBootloaderLine[4] "
        "__attribute__((persistent, coherent, "
        "address(FORCE_BOOTLOADER_FLAG_ADDR)));\n"
    )
    _ck("a 4-byte array (wrong element type) is still flagged",
        len(check(too_small_array)), 1)

    exactly_16 = (
        "volatile uint8_t sLine[16] "
        "__attribute__((persistent, coherent, "
        "address(FORCE_BOOTLOADER_FLAG_ADDR)));\n"
    )
    _ck("exactly 16 bytes passes (boundary)", check(exactly_16), [])

    too_large = (
        "volatile uint32_t sForceBootloaderLine[8] "
        "__attribute__((persistent, coherent, "
        "address(FORCE_BOOTLOADER_FLAG_ADDR)));\n"
    )
    tl_problems = check(too_large)
    _ck("32 bytes (too large) is now flagged too", len(tl_problems), 1)
    _ck("too-large problem says why (runs past the top of RAM)",
        "top of physical RAM" in tl_problems[0] if tl_problems else False, True)

    missing_coherent = (
        "volatile uint32_t sForceBootloaderLine[4] "
        "__attribute__((persistent, address(FORCE_BOOTLOADER_FLAG_ADDR)));\n"
    )
    mc_problems = check(missing_coherent)
    _ck("missing 'coherent' is flagged", len(mc_problems), 1)
    _ck("missing-coherent problem names it",
        "coherent" in mc_problems[0] if mc_problems else False, True)

    substring_coherent = (
        "volatile uint32_t sForceBootloaderLine[4] "
        "__attribute__((persistent, noncoherent, "
        "address(FORCE_BOOTLOADER_FLAG_ADDR)));\n"
    )
    sc_problems = check(substring_coherent)
    _ck("'noncoherent' does not satisfy 'coherent' by substring",
        len(sc_problems), 1)

    missing_persistent = (
        "volatile uint32_t sForceBootloaderLine[4] "
        "__attribute__((coherent, address(FORCE_BOOTLOADER_FLAG_ADDR)));\n"
    )
    mp_problems = check(missing_persistent)
    _ck("missing 'persistent' is flagged", len(mp_problems), 1)

    no_declaration = "static volatile uint32_t gLogLevels[8];\n"
    nd_problems = check(no_declaration)
    _ck("no declaration at all is flagged", len(nd_problems), 1)
    _ck("no-declaration problem says so",
        "nothing to write" in nd_problems[0] if nd_problems else False, True)

    commented_out = (
        "/*\n" + fixed + "*/\n"
        "static volatile uint32_t gLogLevels[8];\n"
    )
    co_problems = check(commented_out)
    _ck("a commented-out (disabled) correct declaration does not pass",
        len(co_problems), 1)
    _ck("commented-out case reports 'nothing to write', not a false pass",
        "nothing to write" in co_problems[0] if co_problems else False, True)

    line_commented_out = (
        "// " + fixed.replace("\n", "\n// ") + "\n"
        "static volatile uint32_t gLogLevels[8];\n"
    )
    lco_problems = check(line_commented_out)
    _ck("a //-commented-out declaration (every line) does not pass",
        len(lco_problems), 1)

    # The Qodo /agentic_review finding this guards: a declaration-shaped
    # string literal, or one disabled via `#if 0`, must not let an
    # UNDERSIZED ACTIVE declaration slip through as if it were the correct
    # one quoted/disabled text happens to describe.
    string_then_undersized = (
        'const char *msg = "uint32_t sForceBootloaderLine[4] '
        '__attribute__((persistent, coherent, '
        'address(FORCE_BOOTLOADER_FLAG_ADDR)));";\n' + pre_fix
    )
    stu_problems = check(string_then_undersized)
    _ck("a declaration-shaped STRING before the real undersized "
        "declaration does not mask the real problem",
        len(stu_problems) >= 1, True)
    _ck("...and the reported problem is the real one (4 bytes), not a "
        "parse failure on the string",
        "4 byte(s)" in stu_problems[0] if stu_problems else False, True)

    if0_then_undersized = (
        "#if 0\n" + fixed + "#endif\n" + pre_fix
    )
    i0u_problems = check(if0_then_undersized)
    _ck("a correct declaration disabled by #if 0 does not mask a "
        "real undersized ACTIVE declaration below it",
        len(i0u_problems) >= 1, True)
    _ck("...and the reported problem is the real one (4 bytes)",
        "4 byte(s)" in i0u_problems[0] if i0u_problems else False, True)

    two_active = fixed + fixed.replace(
        "sForceBootloaderLine", "sForceBootloaderLineTwo")
    ta_problems = check(two_active)
    _ck("two ACTIVE candidate declarations are refused as ambiguous "
        "rather than silently picking the first",
        len(ta_problems), 1)
    _ck("ambiguity problem names the count",
        "2 candidate" in ta_problems[0] if ta_problems else False, True)

    if0_else_case = (
        "#if 0\n" + fixed + "#else\n" + fixed + "#endif\n"
    )
    ie_problems = check(if0_else_case)
    _ck("#if 0 / #else / #endif around the ONLY declaration fails "
        "toward a reported problem, never a silent pass (documented "
        "conservative limitation: #else is not specially unmasked)",
        len(ie_problems) >= 1, True)

    unparsable = (
        "volatile uint32_t sForceBootloaderLine "
        "__attribute__((persistent, coherent, "
        "address(FORCE_BOOTLOADER_FLAG_ADDR))) = weird_initializer();\n"
        # no terminating ';' right after the attribute -- the statement's
        # actual ';' is much later and the shape in between is not the
        # '<type> <name>[N] __attribute__((...));' this checker expects.
    )
    up_problems = check(unparsable)
    _ck("an unparsable shape is flagged rather than silently passed",
        len(up_problems) >= 1, True)

    bad = _CHECKS.count(False)
    print("self-test: %d/%d checks passed" % (_CHECKS.count(True), len(_CHECKS)))
    return 1 if bad else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--scpi", default=DEFAULT_SCPI,
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
        source = fh.read()

    problems = check(source)
    if problems:
        print("FAIL: force_bootloader_flag's D-cache-line reservation (#1083)")
        for p in problems:
            print("  - %s" % p)
        return 1

    print("OK: the object at %s reserves the full %d-byte D-cache line "
          "(persistent, coherent)" % (ADDR_MACRO, CACHE_LINE_BYTES))
    return 0


if __name__ == "__main__":
    sys.exit(main())
