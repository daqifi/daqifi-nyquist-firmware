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

## The crash record block (#1190)

`exceptions.c` places the persisted #552 crash record (`sCrashRecord`) at
`CRASH_RECORD_ADDR` with the same `persistent, coherent, address()` construct,
and the same hazard applies: a cached global linked into one of its lines can
write back over a record the exception handler stored through the uncached
alias. `check_crash_block()` applies the same declaration checks (persistent,
coherent, a whole number of 16-byte lines) to that declaration, and
`check_crash_header()` checks the address arithmetic in `Util/CrashRecord.h`:
`CRASH_RECORD_ADDR` is `FORCE_BOOTLOADER_FLAG_ADDR - CRASH_RECORD_GAP`, the gap
must be 16-byte aligned and large enough that the block ends below the
reboot-handoff line (flag line + handoff line = 32 bytes), and the block size
the header declares must match a whole number of lines.

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
    r"(?:\[\s*(?P<size>\w+)\s*\])?\s*"
    r"__attribute__\s*\(\(\s*(?P<attrs>.*?)\)\)\s*;"
    % "|".join(sorted(_TYPE_SIZES, key=len, reverse=True)),
    re.DOTALL,
)

# Masks comments AND string/char literal CONTENTS in a SINGLE linear scan,
# tracking which of (code / line-comment / block-comment / string / char)
# the scanner currently sits in. This is deliberate, not a style choice:
# two independent regex passes (strip comments, THEN mask literals) is
# unsound in BOTH directions, and this PR's own review found the first
# direction live -- a comment-delimiter substring inside a string, e.g.
# `const char *a = "/*"; <real declaration>; const char *b = "*/";`, makes
# a comment-first pass treat the real declaration between the two strings
# as commented out, and reject a genuinely correct file (a Qodo
# /agentic_review finding on this PR, round 2). A literal-first pass has
# the same problem in reverse: a comment containing an unbalanced quote,
# e.g. `// don't`, can make it misread where the next real string starts.
# One pass that decides "am I in a string/comment RIGHT NOW" as it goes,
# rather than finding all of one construct and then all of the other,
# has neither failure mode: entering a string is checked before entering
# a comment at each position, so a `"` seen while scanning ordinary code
# always wins over a `/` two characters later, and nothing inside an
# already-open string or comment is re-interpreted as opening another.
def _mask_comments_and_literals(text):
    """Blank comments and the CONTENTS of string/char literals (quotes
    kept), preserving length and newlines so this stays a pure textual
    pass with no effect on downstream `;`/brace counting.
    """
    out = []
    i = 0
    n = len(text)
    while i < n:
        two = text[i:i + 2]
        if two == "/*":
            out.append("  ")
            i += 2
            while i < n and text[i:i + 2] != "*/":
                out.append(text[i] if text[i] == "\n" else " ")
                i += 1
            if i < n:
                out.append("  ")
                i += 2
            continue
        if two == "//":
            while i < n and text[i] != "\n":
                out.append(" ")
                i += 1
            continue
        c = text[i]
        if c == '"' or c == "'":
            quote = c
            out.append(quote)
            i += 1
            while i < n and text[i] != quote:
                if text[i] == "\\" and i + 1 < n:
                    out.append("  ")
                    i += 2
                    continue
                out.append(text[i] if text[i] == "\n" else " ")
                i += 1
            if i < n:
                out.append(quote)
                i += 1
            continue
        out.append(c)
        i += 1
    return "".join(out)


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


def check(source_text, addr_macro=ADDR_MACRO, whole_lines=False,
          what="force_bootloader_flag", defines=None):
    """-> list of problem strings; [] means the reservation is intact.

    `addr_macro`/`whole_lines`/`what` retarget the same declaration check at
    another persistent, coherent block (the #1190 crash record): with
    `whole_lines` the size must be a non-zero multiple of CACHE_LINE_BYTES
    instead of exactly one line.

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
    code = _mask_comments_and_literals(source_text)
    code = _mask_disabled_blocks(code)
    # A preprocessor line has no terminating ';', so it would otherwise be
    # swallowed into the front of the next declaration statement.
    code = re.sub(r"^[ \t]*#[^\n]*$", "", code, flags=re.MULTILINE)

    needle = "address(%s)" % addr_macro
    positions = [m.start() for m in re.finditer(re.escape(needle), code)]
    if not positions:
        return ["no declaration places anything at %s -- "
                 "%s has nothing to write" % (addr_macro, what)]
    if len(positions) > 1:
        return ["%d candidate declarations place something at %s in active "
                 "code (comments, string/char literals and `#if 0` blocks "
                 "excluded) -- refusing to guess which is authoritative; "
                 "there must be exactly one"
                 % (len(positions), addr_macro)]
    idx = positions[0]

    start = code.rfind(";", 0, idx) + 1
    end = code.find(";", idx)
    if end == -1:
        return ["unterminated declaration containing address(%s)" % addr_macro]
    stmt = code[start:end + 1]

    m = _DECL_RE.search(stmt)
    if not m or addr_macro not in m.group("attrs"):
        return ["found address(%s) but could not parse the declaring "
                 "statement as '<type> <name>[N] __attribute__((...));': %r"
                 % (addr_macro, stmt.strip())]

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
                "placement %s needs"
                % (m.group("name"), required, attrs, what))

    type_size = _TYPE_SIZES[m.group("type")]
    size_tok = m.group("size")
    if size_tok is None:
        array_size = 1
    elif size_tok.isdigit():
        array_size = int(size_tok)
    elif defines and size_tok in defines:
        array_size = defines[size_tok]
    else:
        return ["array size %r of %s is neither a literal nor a macro the "
                "checker was given the value of" % (size_tok, m.group("name"))]
    total_bytes = type_size * array_size
    # Exactly one line, not merely "at least": FORCE_BOOTLOADER_FLAG_ADDR is
    # the last 16 bytes of physical RAM, so growing the reservation past 16
    # bytes runs off the end of RAM rather than protecting anything further
    # -- catch that misconfiguration here too, not only the under-reservation
    # this checker was written for.
    if whole_lines:
        if total_bytes == 0 or total_bytes % CACHE_LINE_BYTES != 0:
            problems.append(
                "%s reserves %d byte(s) (%s%s) at %s, not a whole number of "
                "%d-byte D-cache lines -- a cached (KSEG0) global can link "
                "into the remainder of the last line and a later write-back "
                "can undo a record stored through the uncached alias (#1190, "
                "#1083)"
                % (m.group("name"), total_bytes, m.group("type"),
                   ("[%d]" % array_size) if m.group("size") else "",
                   addr_macro, CACHE_LINE_BYTES))
    elif total_bytes != CACHE_LINE_BYTES:
        problems.append(
            "%s reserves %d byte(s) (%s%s) at %s, not exactly one %d-byte "
            "D-cache line -- fewer bytes lets a cached (KSEG0) global link "
            "into the remainder and a later write-back can undo a "
            "bootloader-entry request (#1083); more bytes runs past the top "
            "of physical RAM at this fixed address"
            % (m.group("name"), total_bytes, m.group("type"),
               ("[%d]" % array_size) if m.group("size") else "",
               addr_macro, CACHE_LINE_BYTES))

    return problems


CRASH_ADDR_MACRO = "CRASH_RECORD_ADDR"
DEFAULT_CRASH_SRC = "firmware/src/config/default/exceptions.c"
DEFAULT_CRASH_HDR = "firmware/src/Util/CrashRecord.h"
# Flag line (top 16 bytes of RAM) + reboot-handoff line directly below it.
RESERVED_TOP_BYTES = 2 * CACHE_LINE_BYTES


def check_crash_block(source_text, header_text=None):
    """The #1190 crash record's declaration: persistent, coherent, whole lines.

    The array length is a macro (CRASH_RECORD_WORDS), whose value is read from
    the header when one is given.
    """
    defines = {}
    if header_text is not None:
        code = _mask_disabled_blocks(_mask_comments_and_literals(header_text))
        words = _define_int(code, "CRASH_RECORD_WORDS")
        if words is not None:
            defines["CRASH_RECORD_WORDS"] = words
    return check(source_text, addr_macro=CRASH_ADDR_MACRO, whole_lines=True,
                 what="the crash record", defines=defines)


def _define_int(code, name):
    m = re.search(r"^[ \t]*#[ \t]*define[ \t]+%s[ \t]+\(?[ \t]*"
                  r"(0[xX][0-9a-fA-F]+|\d+)[uU]?[ \t]*\)?[ \t]*$" % name,
                  code, re.MULTILINE)
    return int(m.group(1), 0) if m else None


def check_crash_header(header_text):
    """-> problems with the address arithmetic in Util/CrashRecord.h."""
    code = _mask_disabled_blocks(_mask_comments_and_literals(header_text))
    problems = []
    gap = _define_int(code, "CRASH_RECORD_GAP")
    words = _define_int(code, "CRASH_RECORD_WORDS")
    m = re.search(r"^[ \t]*#[ \t]*define[ \t]+%s[ \t]+(.*)$" % CRASH_ADDR_MACRO,
                  code, re.MULTILINE)
    if gap is None or words is None or not m:
        return ["could not find CRASH_RECORD_GAP, CRASH_RECORD_WORDS and "
                "CRASH_RECORD_ADDR in the header -- the placement cannot be "
                "checked"]
    if not re.fullmatch(r"\(\s*%s\s*-\s*CRASH_RECORD_GAP\s*\)" % ADDR_MACRO,
                        m.group(1).strip()):
        problems.append("CRASH_RECORD_ADDR must be (%s - CRASH_RECORD_GAP), "
                        "got %r" % (ADDR_MACRO, m.group(1).strip()))
    if gap % CACHE_LINE_BYTES != 0:
        problems.append("CRASH_RECORD_GAP 0x%X is not a multiple of the "
                        "%d-byte D-cache line, so the block is misaligned"
                        % (gap, CACHE_LINE_BYTES))
    nbytes = words * 4
    if nbytes % CACHE_LINE_BYTES != 0:
        problems.append("CRASH_RECORD_WORDS %d is %d bytes, not a whole "
                        "number of %d-byte lines" % (words, nbytes,
                                                     CACHE_LINE_BYTES))
    if gap < RESERVED_TOP_BYTES + nbytes:
        problems.append("CRASH_RECORD_GAP 0x%X leaves the %d-byte block "
                        "overlapping the flag/reboot-handoff lines (needs at "
                        "least 0x%X)" % (gap, nbytes,
                                         RESERVED_TOP_BYTES + nbytes))
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

    # Round-2 Qodo finding: strings containing raw comment delimiters must
    # not make a comment-first pass treat the genuinely correct code
    # between them as commented out.
    comment_delims_in_strings = (
        'const char *a = "/*";\n' + fixed +
        'const char *b = "*/";\n'
    )
    cds_problems = check(comment_delims_in_strings)
    _ck("a '/*' ... '*/' pair INSIDE string literals does not eat the "
        "real, correct declaration between them",
        cds_problems, [])

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

    # ---- #1190: the crash-record block ----------------------------------
    crash_ok = (
        "static volatile uint32_t sCrashRecord[12]\n"
        "    __attribute__((persistent, coherent, address(CRASH_RECORD_ADDR)));\n"
    )
    _ck("crash block: 48 bytes (3 whole lines) passes",
        check_crash_block(crash_ok), [])
    _ck("crash block: 16 bytes (1 line) passes",
        check_crash_block(crash_ok.replace("[12]", "[4]")), [])
    cb_partial = check_crash_block(crash_ok.replace("[12]", "[10]"))
    _ck("crash block: 40 bytes (a PARTIAL line) is flagged -- must fail",
        len(cb_partial), 1)
    _ck("...and the problem says whole number of lines",
        "whole number" in cb_partial[0] if cb_partial else False, True)
    _ck("crash block: missing 'coherent' is flagged -- must fail",
        len(check_crash_block(crash_ok.replace("persistent, coherent",
                                               "persistent"))), 1)
    _ck("crash block: missing 'persistent' is flagged -- must fail",
        len(check_crash_block(crash_ok.replace("persistent, coherent",
                                               "coherent"))), 1)
    _ck("crash block: no declaration at all is flagged -- must fail",
        len(check_crash_block("static volatile uint32_t x[12];\n")), 1)
    _ck("crash block: the flag declaration does not satisfy the crash check",
        len(check_crash_block(fixed)), 1)
    _ck("flag check is unchanged: a crash declaration does not satisfy it",
        len(check(crash_ok)), 1)

    macro_decl = crash_ok.replace("[12]", "[CRASH_RECORD_WORDS]")
    _ck("crash block: macro-sized array resolves through the header",
        check_crash_block(macro_decl, "#define CRASH_RECORD_WORDS 12u\n"), [])
    _ck("crash block: macro-sized array that resolves to a partial line fails",
        len(check_crash_block(macro_decl, "#define CRASH_RECORD_WORDS 10u\n")),
        1)
    _ck("crash block: an unresolvable size macro is refused, not passed",
        len(check_crash_block(macro_decl)), 1)
    _ck("crash block: a #include line before the declaration does not break it",
        check_crash_block('#include "Util/CrashRecord.h"\n' + crash_ok), [])

    hdr_ok = (
        "#define CRASH_RECORD_WORDS 12u\n"
        "#define CRASH_RECORD_GAP   0x400\n"
        "#define CRASH_RECORD_ADDR  (FORCE_BOOTLOADER_FLAG_ADDR - CRASH_RECORD_GAP)\n"
    )
    _ck("crash header: 0x400 gap, 12 words passes",
        check_crash_header(hdr_ok), [])
    _ck("crash header: gap 0x30 (smallest block that clears flag+handoff) passes",
        check_crash_header(hdr_ok.replace("0x400", "0x50")), [])
    _ck("crash header: misaligned gap is flagged -- must fail",
        len(check_crash_header(hdr_ok.replace("0x400", "0x404"))) >= 1, True)
    _ck("crash header: gap that overlaps the handoff line is flagged -- must fail",
        len(check_crash_header(hdr_ok.replace("0x400", "0x30"))), 1)
    _ck("crash header: a partial-line word count is flagged -- must fail",
        len(check_crash_header(hdr_ok.replace("12u", "10u"))) >= 1, True)
    _ck("crash header: an address not anchored to the flag macro is flagged",
        len(check_crash_header(hdr_ok.replace(
            "(FORCE_BOOTLOADER_FLAG_ADDR - CRASH_RECORD_GAP)",
            "0x8007FBF0"))), 1)
    _ck("crash header: missing defines are flagged, not passed",
        len(check_crash_header("/* nothing */\n")), 1)
    _ck("crash header: defines inside a comment do not count",
        len(check_crash_header("/*\n" + hdr_ok + "*/\n")), 1)

    bad = _CHECKS.count(False)
    print("self-test: %d/%d checks passed" % (_CHECKS.count(True), len(_CHECKS)))
    return 1 if bad else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--scpi", default=DEFAULT_SCPI,
                     help="path to SCPIInterface.c")
    ap.add_argument("--crash-src", default=DEFAULT_CRASH_SRC,
                     help="path to exceptions.c (the #1190 crash record)")
    ap.add_argument("--crash-hdr", default=DEFAULT_CRASH_HDR,
                     help="path to Util/CrashRecord.h")
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

    failed = False
    problems = check(source)
    if problems:
        failed = True
        print("FAIL: force_bootloader_flag's D-cache-line reservation (#1083)")
        for p in problems:
            print("  - %s" % p)
    else:
        print("OK: the object at %s reserves the full %d-byte D-cache line "
              "(persistent, coherent)" % (ADDR_MACRO, CACHE_LINE_BYTES))

    for path in (args.crash_src, args.crash_hdr):
        if not os.path.isfile(path):
            sys.exit("error: %r not found (run from the repo root, or pass "
                      "--crash-src/--crash-hdr)" % path)
    with open(args.crash_src, "r", encoding="utf-8", errors="replace") as fh:
        crash_src = fh.read()
    with open(args.crash_hdr, "r", encoding="utf-8", errors="replace") as fh:
        crash_hdr = fh.read()
    cproblems = (check_crash_block(crash_src, crash_hdr)
                 + check_crash_header(crash_hdr))
    if cproblems:
        failed = True
        print("FAIL: the crash record's D-cache-line reservation (#1190)")
        for p in cproblems:
            print("  - %s" % p)
    else:
        print("OK: the object at %s reserves whole D-cache lines "
              "(persistent, coherent) clear of the flag/handoff lines"
              % CRASH_ADDR_MACRO)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
