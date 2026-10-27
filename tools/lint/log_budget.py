#!/usr/bin/env python3
"""Fail if a LOG_E message can be truncated by Logger's format buffer (#1039).

`Util/Logger.c`'s `LogMessageFormatImpl` formats into a `LOG_MESSAGE_SIZE`
buffer and then clamps the result, so a message longer than the resulting
ceiling is cut WITHOUT any error, return code, or marker. The operator sees a
plausible, complete-looking line. Because a remedy is written last, the remedy
is the part that disappears -- which makes a truncated diagnostic worse than no
diagnostic, since it reads as finished.

#1029 fixed three such messages in `sd_card_manager.c`; a sweep for #1039 then
found more in `app_freertos.c`, `SCPIADC.c` and `wifi_manager.c`, none of which
the first fix touched, and two of which had NO substitutions -- meaning no input
made them fit and they truncated identically on every single firing. The ceiling
is invisible at the call site, so the sweep recurs until a gate holds it. This
is that gate.

WHAT IS MEASURED
    The WORST-CASE formatted length of each `LOG_E(...)` format string: its
    fixed bytes (after decoding C escapes -- "\\r\\n" is two bytes of output,
    not four -- and counted as UTF-8 BYTES, because Logger's ceiling bounds a
    `char[]` and a two-byte character spends two of it) plus, for each
    conversion specifier, the widest that specifier could print. If that total
    exceeds the ceiling the site is a finding.

    A message is also a finding when its worst case cannot be BOUNDED at all --
    an unannotated `%s`, a `*` width, a specifier with no entry in the width
    table, a `%` sequence that is not a conversion this tool can parse, or a
    format argument that is not a string literal. Those are not skipped and not
    assumed to be zero: "we could not measure it" is reported with the same
    severity as "we measured it and it is too long", because the failure mode
    this gate exists to stop is exactly the silent one.

WHAT IS NOT MEASURED
    * LOG_I / LOG_D. #1039's acceptance criterion is about LOG_E, and the
      information levels are not what an operator is reading when something has
      gone wrong. `--macros` widens the scope without touching the parser, so a
      later PR can take them on as its own baseline exercise.
    * Whether the specifier count matches the argument count. A mismatch is a
      real bug (printf walks into stack garbage) but it is not a budget bug,
      and folding it in here would fill this tool's baseline with findings that
      have nothing to do with truncation.
    * Runtime `LogMessage()` calls that do not go through a LOG_* macro.

KNOWN LIMIT, REPORTED RATHER THAN HIDDEN
    A LOG_E written inside another macro's body (SCPIStorageSD.c's
    `LOG_SD_BUSY(cmd)` is the one in this tree) is measured where it is
    DEFINED, not at each expansion. The definition splices a macro parameter
    into the format string, so this tool reports it as "format argument is not
    a string literal" and it lands in the baseline. That is the honest outcome
    for a textual checker -- the site is named and visible rather than quietly
    skipped -- but be aware the individual expansion sites are not separately
    measured, so a long `cmd` there is not caught.

SCOPE
    Every *.c under firmware/src, excluding third_party/, libraries/ and
    config/ -- the same exclusion list tools/lint/cppcheck.sh uses, for the
    same reason: vendored code we do not own and would not edit.

Usage:
    python3 tools/lint/log_budget.py --self-test      # prove the tool works
    python3 tools/lint/log_budget.py                  # gate: new vs baseline
    python3 tools/lint/log_budget.py --list           # print all findings
    python3 tools/lint/log_budget.py --write-baseline # regenerate the baseline
Exit codes:
    0 = this tree's findings are EXACTLY the committed baseline
    1 = the baseline no longer describes this tree -- new findings, or baseline
        entries that are no longer reported, or a failed self-test. All three
        have the same remedy (read the report, then one deliberate commit), and
        drift is failed in BOTH directions for the same reason cppcheck.sh
        fails it in both: a record left behind after its finding was fixed
        still matches by text, so it silently re-blesses that exact message if
        it is ever reintroduced.
    2 = the tool could not run (missing file, unreadable Logger.c, bad
        suppression file) -- never confused with "clean"

THE SUPPORTED SUBSET, AND WHAT IS REFUSED (#1110 r9)

Nine parser edge cases over rounds 7 and 8 shared ONE cause: this tool
INTERPRETED a construct it cannot fully model and guessed PERMISSIVELY, which
in a truncation gate always means silencing a message. Patching the sites was
losing to the generator. So the subset is now declared, and OUTSIDE it the tool
raises ToolError (exit 2, "could not produce a verdict") rather than guessing:

  * ONE LINE MODEL. Lines are split on newline ONLY, everywhere
    (split_lines_1model), because every line NUMBER here comes from
    count(chr(10)). splitlines() is not used by any code in this module and the
    self-test pins that with ast.
  * THE CEILING MUST BE DEFINED EXACTLY ONCE. A conditional or duplicated
    `#define LOG_MESSAGE_SIZE` is REFUSED: this tool does not evaluate the
    preprocessor, so there is no single right answer to read.
  * C INTEGER NOTATION is honoured where the firmware writes it (hex, octal,
    binary) and malformed constants are refused. Integer suffixes (128u) are
    refused, not parsed.
  * ANNOTATION VALUES MUST BE DECIMAL. `max=0x80` is refused, never read as the
    0 its leading digit used to match.
  * AN ANNOTATION MAY NOT SHARE ITS LINE WITH ANY PART OF ANOTHER LOG CALL --
    including a multi-line call that merely ENDS there. Refused, because an
    annotation whose subject is ambiguous cannot be read at all, and a guess
    silences.
  * AN UNBALANCED ARGUMENT LIST (a preprocessor conditional opening a paren on
    one arm) is a reported finding, never a skipped call.
  * THE BASELINE IS A SERIALISATION BOUNDARY: every non-printable byte is
    escaped on write and the loader splits on newline only, so no message can
    split its own record.

Only the gate run compares against the baseline, so only it can drift: --list
just prints what it sees, and --write-baseline IS the remedy for drift. Neither
reads the baseline to reach a verdict, so neither has anything analogous to
check; both exit 0 on success.
"""
import argparse
import collections
import re
import sys
from pathlib import Path

DEFAULT_ROOT = Path("firmware/src")
DEFAULT_LOGGER_H = Path("firmware/src/Util/Logger.h")
DEFAULT_LOGGER_C = Path("firmware/src/Util/Logger.c")
DEFAULT_BASELINE = Path("tools/lint/log_budget-baseline.txt")
DEFAULT_SUPPRESS = Path("tools/lint/log_budget-suppress.txt")

# Same three trees tools/lint/cppcheck.sh passes to `-i`.
EXCLUDED_PREFIXES = ("third_party/", "libraries/", "config/")

# macro name -> index of its format-string argument.
#
# LOG_E_ONCE / LOG_E_SESSION take a latch bit FIRST and the format string
# second (Logger.h). They are included because leaving them out would leave the
# gate trivially sidesteppable: the same over-budget text moved from LOG_E to
# LOG_E_ONCE would stop being reported while still truncating identically.
DEFAULT_MACROS = {
    "LOG_E": 0,
    "LOG_E_ONCE": 1,
    "LOG_E_SESSION": 1,
}
OPTIONAL_MACROS = {
    "LOG_I": 0, "LOG_I_ONCE": 1, "LOG_I_SESSION": 1,
    "LOG_D": 0, "LOG_D_ONCE": 1, "LOG_D_SESSION": 1,
}

# ---------------------------------------------------------------------------
# Worst-case width per conversion specifier.
#
# Each number is the most characters that conversion can emit, so the sum is an
# upper bound and a site this tool calls safe really is safe. Reasoning, so a
# future reader can audit whether a number is still conservative on this
# target (XC32 / PIC32MZ, 32-bit int and long, 64-bit long long):
#
#   d/i    11  32-bit signed decimal, sign included: "-2147483648" is 11 chars.
#   u      10  32-bit unsigned decimal: "4294967295" is 10 chars.
#   o      11  32-bit unsigned octal: "37777777777" is 11 chars.
#   x/X     8  32-bit hex, 8 nibbles. NO "0x" prefix: that only appears with
#              the '#' flag, which is handled separately below (+2) rather
#              than baked in, so a plain %x is not over-charged.
#   ll*    20  64-bit: "18446744073709551615" and "-9223372036854775808" are
#              both 20 chars.
#   llx    16  64-bit hex, 16 nibbles.
#   c       1  one character, NARROW only. `l` (a wide `wchar_t`) is
#              intercepted in measure() before this table is ever consulted
#              -- see "lc"/"ls" below.
#   lc/ls       DELIBERATELY ABSENT, and deliberately not merely "missing":
#               measure() refuses %lc/%ls explicitly, by name, rather than
#               falling through to the generic "no conservative width"
#               message, because the danger here is not the same shape as an
#               unlisted specifier. A previous version of this table charged
#               "lc": 1 -- the SAME width as a narrow %c -- and measure()
#               dispatched %ls through the identical code path as a narrow
#               %s, ignoring the `l` length modifier entirely. Both silently
#               assumed one wide character costs the same as one narrow byte.
#               How many bytes THIS target's vsnprintf actually emits per
#               wide character is unverified, and firmware/src has zero
#               %lc/%ls call sites (2026-09-16) to derive a real width from
#               empirically -- so refuse rather than guess. Chosen over
#               charging a conservative multi-byte constant (e.g. 4, for
#               UTF-8's worst case) because a constant here would still be
#               unverified against what this vsnprintf actually does with a
#               wide conversion, and an explicit refusal is simpler and no
#               less safe than a guessed bound (deferred /improve item,
#               importance 7, PR #1110).
#   p           DELIBERATELY ABSENT. The format of a pointer is
#               implementation-defined; a number here would be a guess dressed
#               as a bound. An unlisted specifier is a named finding, which is
#               the honest outcome -- annotate the site if you know the width.
#   f/e/g       DELIBERATELY ABSENT. %f of a large double runs to ~310 digits
#               before the decimal point. No small constant is conservative,
#               so these are findings until annotated.
#   s           Unbounded by definition -- see resolve_string_width().
#
# `l` on an int-class conversion is the same width as no length modifier here
# because long is 32-bit on this target; both spellings are listed so the
# lookup never falls through to "unrecognized" on a correct format string.
# ---------------------------------------------------------------------------
WIDTHS = {
    "d": 11, "i": 11, "ld": 11, "li": 11, "hd": 11, "hi": 11, "hhd": 11, "hhi": 11,
    "u": 10, "lu": 10, "hu": 10, "hhu": 10,
    "o": 11, "lo": 11,
    "x": 8, "X": 8, "lx": 8, "lX": 8, "hx": 8, "hX": 8,
    "lld": 20, "lli": 20, "llu": 20, "jd": 20, "ji": 20, "ju": 20,
    "llx": 16, "llX": 16,
    "zd": 11, "zi": 11, "zu": 10, "zx": 8, "zX": 8,
    "td": 11, "ti": 11,
    "c": 1,
}

# Sign bytes folded into each integer conversion's WIDTHS entry, so an explicit
# PRECISION can replace the digit half without disturbing the sign half.
#
# C99 7.19.6.1p6: for d, i, o, u, x and X the precision is "the minimum number
# of DIGITS to appear" -- a value with fewer digits is padded with leading
# zeros. The sign is not a digit, so `%.200d` of a negative number is 1 + 200 =
# 201 bytes, not 200 and not 211. Splitting WIDTHS this way keeps one source of
# truth: digits = WIDTHS[key] - INT_SIGN_BYTES[conv], which reproduces the
# reasoning in the table above exactly ("-2147483648" = 1 sign + 10 digits;
# "4294967295" = 0 + 10; "18446744073709551615" = 0 + 20).
#
# A '+' or ' ' flag makes a POSITIVE signed value carry a sign too, but never
# more than the one byte already charged, so the flags need no separate term.
# The '#' prefix for x/X/o is charged on top, as it always was: it is a prefix,
# not a digit, and C99 raises the precision for '#o' only by the one digit the
# +2 already over-covers.
INT_SIGN_BYTES = {"d": 1, "i": 1, "u": 0, "o": 0, "x": 0, "X": 0}

# %[flags][width][.precision][length]conversion -- the full C99 shape. `*` is
# matched (and then rejected in measure()) rather than left out, so a
# `%*d` reads as "width comes from an argument, unbounded" instead of silently
# failing to match and being counted as plain text.
SPEC_RE = re.compile(
    r"%"
    r"(?P<flags>[-+ #0']*)"
    r"(?P<width>\*|[0-9]*)"
    r"(?:\.(?P<prec>\*|[0-9]*))?"
    r"(?P<length>hh|h|ll|l|j|z|t|L)?"
    r"(?P<conv>[diouxXeEfgGaAcspn%])"
)

# The value list is captured LOOSELY (any run of value-ish characters) so a
# non-decimal spelling is REFUSED by the parse below rather than silently
# half-matched. #1110 r9: the previous `([0-9]+...)` matched the leading 0 of
# `max=0x80` and budgeted that %s at ZERO -- a silencing annotation nobody
# wrote. See annotation_values().
ANNOTATION_RE = re.compile(r"log_budget:\s*max\s*=\s*([0-9A-Za-z_]+(?:\s*,\s*[0-9A-Za-z_]+)*)")
ANNOTATION_DECIMAL_RE = re.compile(r"^[0-9]+$")

SEP = " :: "


# ---------------------------------------------------------------------------
# Reading the ceiling out of the firmware, never hardcoding it
# ---------------------------------------------------------------------------
class ToolError(Exception):
    """The tool cannot produce a trustworthy verdict. Always exit 2, never 0."""


def c_int_literal(text, where):
    r"""Value of a C integer constant. Python's int() is NOT this function.

    #1124 r8/2 (codex audit on #1110): `int("0200")` is 200 in Python and
    0200 is 128 in C -- a decimal-only read makes LOG_MESSAGE_SIZE, and so the
    ceiling every message is measured against, too GENEROUS. Too generous
    UNDER-reports, which is the direction this gate must never fail in.
    `int("0x80")` raises where C reads 128, which fails closed but noisily.

    Integer SUFFIXES (u/U/l/L) are deliberately not accepted: the callers'
    regexes do not capture them, so `128u` reaches neither site and raises a
    loud ToolError instead. Fail-closed, and out of scope for this finding --
    recorded so the omission reads as a decision, not an oversight.
    """
    s = text.strip()
    try:
        low = s[:2].lower()
        if low == "0x":
            if len(s) == 2:
                raise ValueError(s)
            return int(s[2:], 16)
        if low == "0b":
            if len(s) == 2:
                raise ValueError(s)
            return int(s[2:], 2)
        if len(s) > 1 and s[0] == "0":
            return int(s[1:], 8)          # C octal: 0200 == 128
        return int(s, 10)
    except ValueError:
        raise ToolError(
            f"{where}: {s!r} is not an integer constant this tool can read. "
            f"The ceiling is derived from it, so guessing would mis-set every "
            f"other check -- read it by hand and update read_ceiling().")


def reserve_after(impl, prefix_re, where, gone_msg):
    r"""Total subtracted from LOG_MESSAGE_SIZE in the argument after `prefix_re`.

    THE THIRD VERSION OF THIS FUNCTION, and the first two failed the same way
    from opposite ends (codex audit on #1110, twice):

        `LOG_MESSAGE_SIZE\s*-\s*(\d+)`   read `- 2 - 10` as 2
        `LOG_MESSAGE_SIZE([^,)]*)`       read `(LOG_MESSAGE_SIZE - 3) - 10` as 3,
                                         because it stops at the closing paren

    Both traded one prefix for another. So this does not pattern-match the
    expression at all: it walks the argument with paren DEPTH, then accounts
    for every character of it.

    `(` after the first `-` is REFUSED rather than stripped, and that is not
    fussiness: paren-stripping is sound for `(A - b) - c` because subtraction
    is left-associative, and WRONG for `A - (b - c)`, which it would read as
    `A - b - c` -- the opposite sign on c. Refusing is the only safe reading.
    """
    m = re.search(prefix_re + r"(?=\(*\s*LOG_MESSAGE_SIZE)", impl)
    if not m:
        raise ToolError(f"{gone_msg} -- re-derive the ceiling by hand and "
                        f"update read_ceiling()")
    # Walk the argument: from here to the depth-0 ',' or the ')' that closes
    # the enclosing call.
    i, depth, start = m.end(), 0, m.end()
    while i < len(impl):
        c = impl[i]
        if c == "(":
            depth += 1
        elif c == ")":
            if depth == 0:
                break
            depth -= 1
        elif c == "," and depth == 0:
            break
        i += 1
    arg = impl[start:i]
    j = arg.find("LOG_MESSAGE_SIZE")
    if j < 0:
        raise ToolError(f"{where}'s argument {arg.strip()!r} does not mention "
                        f"LOG_MESSAGE_SIZE")
    rest = arg[j + len("LOG_MESSAGE_SIZE"):]
    terms, k, seen_minus = [], 0, False
    while k < len(rest):
        ch = rest[k]
        if ch.isspace():
            k += 1
            continue
        if ch == ")":
            # closes a paren opened BEFORE LOG_MESSAGE_SIZE, e.g. the `(` in
            # `min((LOG_MESSAGE_SIZE - 3) - 10, size)`. It carries no value;
            # a `(` here is refused below, so this cannot flatten `A - (b - c)`.
            k += 1
            continue
        if ch == "(":
            raise ToolError(
                f"{where}'s argument {arg.strip()!r} parenthesises a term "
                f"AFTER a subtraction. `A - (b - c)` is not `A - b - c`, so "
                f"this cannot be flattened safely -- re-derive the ceiling by "
                f"hand and update read_ceiling().")
        mm = re.compile(r"-\s*([0-9][0-9a-fA-FxXbB]*)").match(rest, k)
        if not mm:
            raise ToolError(
                f"cannot account for {rest[k:].strip()!r} in {where} "
                f"(argument {arg.strip()!r}). Only a subtraction of integer "
                f"constants is understood; a macro, a '+', a multiplication or "
                f"anything else must be read by a human, because guessing here "
                f"mis-sets the ceiling every other check is measured against.")
        seen_minus = True
        terms.append(c_int_literal(mm.group(1), where))
        k = mm.end()
    if not terms:
        raise ToolError(
            f"{where} subtracts nothing from LOG_MESSAGE_SIZE (argument "
            f"{arg.strip()!r}) -- the reservation is read from source and must "
            f"not be assumed")
    return sum(terms)


def read_ceiling(logger_h_text, logger_c_text):
    """Derive the effective ceiling from Logger.h and Logger.c.

    Both reservations are read from `LogMessageFormatImpl` rather than assumed,
    and the ceiling is the tighter of what each one allows:

      * vsnprintf(buffer, LOG_MESSAGE_SIZE - R1, ...) writes at most
        LOG_MESSAGE_SIZE - R1 - 1 characters, because its size argument counts
        the terminating NUL it always writes.
      * the clamp min((LOG_MESSAGE_SIZE - R2), size) caps the length that is
        carried forward regardless of what vsnprintf returned.

    Raises ToolError if either cannot be found. A checker that silently assumed
    128/2/3 after a refactor moved them would keep reporting green against a
    ceiling the firmware no longer has -- the one failure this gate must not
    have.
    """
    # COMMENTS ARE MASKED FIRST. A commented-out or historical line mentioning
    # `vsnprintf(buffer, LOG_MESSAGE_SIZE - 99, ...)` otherwise WINS the search
    # and sets the ceiling every message in the tree is measured against --
    # measured: such a comment produced a ceiling of 28. Found by the blind leg
    # of the codex audit on #1110. `mask_comments` preserves offsets and
    # newlines, so every regex below is unaffected except for no longer being
    # able to match comment text.
    # ⛔ #1110 r8/1: SPLICE FIRST, THEN MASK. C deletes a line-ending
    # backslash and the newline after it in translation phase 2, STRICTLY
    # before comments are recognised in phase 3 -- so
    #
    #     // historical, superseded: \
    #     size = min((LOG_MESSAGE_SIZE - 13), size);
    #
    # is ONE comment and that clamp is NOT live code. Masking raw text saw the
    # comment end at its own newline and read the continuation line as live,
    # setting the ceiling from a commented-out clamp: measured 115 where the
    # firmware's is 125.
    #
    # splice_continuations()' own docstring already states this ordering and
    # says every later stage must read its output. read_ceiling() was the one
    # caller that did not -- the rule was written down and not run here.
    # Splicing shifts offsets, which is harmless in this function because it
    # extracts NUMBERS and never reports a position.
    logger_h_text = mask_comments(splice_continuations(logger_h_text)[0])
    logger_c_text = mask_comments(splice_continuations(logger_c_text)[0])

    # ⛔ #1110 r9: EXACTLY ONE definition, or refuse. re.search took the FIRST
    # match, so an #ifdef DEBUG_BUILD / #define 256 / #else / #define 128 /
    # #endif silently gated the whole tree against whichever arm appeared
    # first, regardless of which one the firmware compiles with. This tool does
    # not evaluate the preprocessor and must not pretend to: with more than one
    # definition there is no single right answer to read.
    defs = re.findall(
        r"^\s*#define\s+LOG_MESSAGE_SIZE\s+([0-9][0-9a-fA-FxXbB]*)\s*$",
        logger_h_text, re.M)
    if not defs:
        raise ToolError(
            "could not find '#define LOG_MESSAGE_SIZE <n>' in Logger.h -- the "
            "ceiling is read from source and must not be assumed")
    if len(defs) > 1:
        raise ToolError(
            f"Logger.h defines LOG_MESSAGE_SIZE {len(defs)} times "
            f"({', '.join(defs)}) -- a conditional or duplicated ceiling is "
            f"refused, not resolved. Picking one would gate every message in "
            f"the tree against a ceiling the firmware may not have.")
    size = c_int_literal(defs[0], "LOG_MESSAGE_SIZE in Logger.h")

    body = re.search(
        r"LogMessageFormatImpl\s*\([^)]*\)\s*\{(.*?)\n\}", logger_c_text, re.S)
    if not body:
        raise ToolError(
            "could not find the body of LogMessageFormatImpl in Logger.c -- "
            "the buffer reservations are read from it and must not be assumed")
    impl = body.group(1)

    vsn_reserve = reserve_after(impl, r"vsnprintf\s*\(\s*\w+\s*,\s*",
                                "vsnprintf's size argument",
                                "LogMessageFormatImpl no longer calls "
                                "vsnprintf(buf, LOG_MESSAGE_SIZE - N, ...)")
    clamp_reserve = reserve_after(impl, r"min\s*\(\s*",
                                  "the min() clamp",
                                  "LogMessageFormatImpl no longer clamps with "
                                  "min((LOG_MESSAGE_SIZE - N), ...)")
    ceiling = min(size - vsn_reserve - 1, size - clamp_reserve)
    if ceiling <= 0:
        raise ToolError(
            f"derived a nonsensical ceiling of {ceiling} from "
            f"LOG_MESSAGE_SIZE={size}, reservations {vsn_reserve}/"
            f"{clamp_reserve}")
    return ceiling, size, vsn_reserve, clamp_reserve


# ---------------------------------------------------------------------------
# Source scanning
# ---------------------------------------------------------------------------
def mask_comments(src):
    """Blank every comment, preserving length and newlines.

    Comments are replaced with spaces (newlines kept) rather than deleted so
    every offset and line number in the masked text still refers to the same
    place in the real file.

    This matters more here than in most linters: the house convention for THIS
    class of fix (#1025, #1029, #1039) is to leave a comment above the
    shortened call quoting the OLD, over-budget text verbatim -- often as a
    complete `LOG_E("...")` fragment. A scanner that did not mask comments
    would read every one of those as a live call site and report the very
    message the commit just fixed, forever.

    String and character literals are tracked while scanning so that a "/*"
    inside a literal does not open a comment, and a quote inside a comment does
    not open a literal.
    """
    out = list(src)
    i, n = 0, len(src)
    while i < n:
        c = src[i]
        if c == '"' or c == "'":
            quote = c
            i += 1
            while i < n:
                if src[i] == "\\":
                    i += 2
                    continue
                if src[i] == quote:
                    i += 1
                    break
                i += 1
        elif src.startswith("//", i):
            j = src.find("\n", i)
            j = n if j < 0 else j
            for k in range(i, j):
                out[k] = " "
            i = j
        elif src.startswith("/*", i):
            j = src.find("*/", i + 2)
            j = n if j < 0 else j + 2
            for k in range(i, j):
                if out[k] != "\n":
                    out[k] = " "
            i = j
        else:
            i += 1
    return "".join(out)


def split_args(text):
    """Split a call's argument text on top-level commas.

    Depth is tracked across (), [] and {} and literals are skipped, so a
    `LOG_E("x %u", foo(a, b))` is two arguments, not three.
    """
    args, depth, start = [], 0, 0
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c == '"' or c == "'":
            quote = c
            i += 1
            while i < n:
                if text[i] == "\\":
                    i += 2
                    continue
                if text[i] == quote:
                    break
                i += 1
        elif c in "([{":
            depth += 1
        elif c in ")]}":
            depth -= 1
        elif c == "," and depth == 0:
            args.append(text[start:i])
            start = i + 1
        i += 1
    args.append(text[start:])
    return args


def splice_continuations(src):
    """Delete every backslash-newline continuation from `src`. Returns
    (spliced, orig_pos), where orig_pos[i] is the index in `src` that
    spliced[i] came from.

    C deletes a line-ending backslash and the newline right after it in
    translation phase 2 (C99 5.1.1.2) -- strictly BEFORE comments are
    recognized (phase 3) or anything is tokenized. So this runs FIRST, on
    raw source text, and mask_comments() / find_calls() / literal decoding
    all work on its output rather than on `src` directly: whatever those
    later stages see is what the compiler would actually tokenize.

    A FIRST VERSION OF THIS FUNCTION REPLACED THE BACKSLASH WITH A SINGLE
    SPACE AND LEFT THE NEWLINE, reasoning that the call-site regex's `\\s*`
    already crosses a real newline, so that was enough to "cross" the gap.
    That is true only when the continuation falls BETWEEN two tokens. When
    it falls INSIDE one -- splitting a macro name itself, e.g. `LOG\\`
    <newline> `_E(...)` -- C really does delete both characters and glue the
    two halves into ONE token, `LOG_E`. A whitespace separator keeps them as
    TWO tokens forever, so the macro name is no longer contiguous text and
    the call-site regex cannot match it: the call goes invisible again, in
    exactly the direction this tool exists to prevent (Qodo /agentic_review,
    PR #1110, 2026-09-16: "Split macro names evade checks" -- verified
    empirically against 827284cc1, 0 findings for a 126-byte literal reached
    only through a continuation inside the macro name).

    LINE NUMBERS STILL HAVE TO BE REAL. Splicing can delete an actual
    newline (when it was part of a continuation), so a position in the
    spliced text cannot be turned into a line number by counting newlines
    IN the spliced text -- that undercounts every line after a splice.
    orig_pos is the fix: translate a spliced-text position `p` back to a
    real source line with `pos_to_line(src, orig_pos, p)`, never by counting
    "\\n" in the spliced text itself.
    """
    out, orig_pos = [], []
    i, n = 0, len(src)
    while i < n:
        if src[i] == "\\" and i + 1 < n:
            if src[i + 1] == "\n":
                i += 2
                continue
            if src[i + 1] == "\r" and i + 2 < n and src[i + 2] == "\n":
                i += 3
                continue
        out.append(src[i])
        orig_pos.append(i)
        i += 1
    return "".join(out), orig_pos


def pos_to_line(src, orig_pos, pos):
    """Translate a position in a spliced/masked text back to its 1-based
    line number in the real, original `src` -- see splice_continuations().
    """
    if not orig_pos:
        return 1
    idx = orig_pos[pos] if pos < len(orig_pos) else orig_pos[-1]
    return src.count("\n", 0, idx) + 1


def mask_literals(text):
    """Blank the CONTENTS of every string/char literal -- not the quote
    marks -- preserving length and newlines. Same technique as
    mask_comments(), for a different purpose: this view decides where a
    call-site match is LEGITIMATE and is never used to read a call's actual
    content.

    find_calls() used to run its call-site regex directly against `masked`
    -- comment-blanked, but literal CONTENT preserved verbatim, because a
    genuine call's real arguments have to survive. That same preservation
    let a literal whose TEXT happens to contain something shaped like
    "LOG_E(" match as if it were a real call. This got a new, narrower way
    to fire once splice_continuations() started running on raw source
    (deleting a continuation wherever it falls, phase 2, literals included
    -- matching real C, where splicing precedes even literal recognition):
    a literal such as `"LOG` + backslash + newline + `_E(value);"` becomes,
    post-splice, one containing the contiguous text `LOG_E(value);`, which
    the call-site regex then matches -- a valid, harmless log message incorrectly
    reported as an unresolvable call (Qodo /agentic_review, PR #1110,
    2026-09-16: "Valid source fails logging gate", verified empirically
    against 076bdce84).

    Used ONLY to locate a call's start; find_calls() still reads the actual
    call -- parens, quotes, the real format string -- from the unmodified
    `masked` text at the same offsets, so genuine literal content is never
    lost.
    """
    out = list(text)
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c != '"' and c != "'":
            i += 1
            continue
        quote = c
        content_start = i + 1
        i += 1
        while i < n:
            if text[i] == "\\":
                i += 2                # skip the escape pair -- see
                continue               # mask_comments()'s identical walk
            if text[i] == quote:
                break
            i += 1
        content_end = i               # the closing quote's position, or n
        for k in range(content_start, content_end):
            if out[k] != "\n":
                out[k] = " "
        i = content_end + 1 if content_end < n else n
    return "".join(out)


def find_calls(masked, macros):
    """Yield (macro, pos, arg_list, call_text) for each log-macro call.

    `pos` is the macro name's start offset in `masked` (the already-spliced,
    comment-masked text) -- this function has no access to the original
    source, so translate `pos` to a real line with pos_to_line().
    """
    names = "|".join(sorted(macros, key=len, reverse=True))
    call_re = re.compile(r"(?<![A-Za-z0-9_])(" + names + r")\s*\(")
    match_view = mask_literals(masked)
    for m in call_re.finditer(match_view):
        open_paren = m.end() - 1
        i, depth, n = open_paren, 0, len(masked)
        while i < n:
            c = masked[i]
            if c == '"' or c == "'":
                quote = c
                i += 1
                while i < n:
                    if masked[i] == "\\":
                        i += 2
                        continue
                    if masked[i] == quote:
                        break
                    i += 1
            elif c == "(":
                depth += 1
            elif c == ")":
                depth -= 1
                if depth == 0:
                    break
            i += 1
        if i >= n:
            # ⛔ #1110 r8/3: NOT a skip. The walk ran off the end without
            # balancing -- e.g. a #if/#else inside the argument list opening
            # parens on one arm -- and `continue` here made the call INVISIBLE,
            # so a message that truncates was reported clean. "Could not
            # evaluate" must never read as "clean"; args=None tells the caller
            # to report it.
            yield m.group(1), m.start(), None, None
            continue
        inner = masked[open_paren + 1:i]
        yield m.group(1), m.start(), split_args(inner), masked[m.start():i + 1]


LITERAL_RE = re.compile(r'"((?:[^"\\]|\\.)*)"', re.S)


def literal_text(arg):
    """Decode an argument made only of adjacent string literals, else None.

    C concatenates adjacent literals, so a format string split over five
    physical lines is one message -- the common shape in this codebase. Anything
    else in the argument (an identifier, a ternary, a macro) means the format is
    not statically knowable, and None tells the caller to report that rather
    than guess.

    EACH LITERAL IS DECODED BEFORE THE PIECES ARE JOINED, never after. C99
    6.4.4.4 decodes the escapes within a single string literal in translation
    phase 5, strictly before phase 6 concatenates adjacent literals -- a
    literal boundary is not a character a maximal-munch escape can see across
    in real C. An earlier version of this function joined the RAW (undecoded)
    text of every piece first and called unescape() once on the result, which
    let a hex/octal escape at the tail of one literal spill into the next
    literal's leading characters when they happened to be valid hex/octal
    digits: `"....\\x41" "B"` decodes, correctly, to "....AB" (\\x41 is 'A',
    then a separate 'B'), but joining first produced the single string
    "....\\x41B", whose \\x41B maximal-munches all of "41B" as one hex
    escape, silently dropping the 'B' and undercounting the message by a byte
    (verified empirically against ca3b4254f, 2026-09-16: 0 findings on the
    unfixed tool for a message that is 126 bytes over the ceiling).
    """
    pieces, pos = [], 0
    for m in LITERAL_RE.finditer(arg):
        if arg[pos:m.start()].strip():
            return None                   # non-literal token between the parts
        pieces.append(unescape(m.group(1)))
        pos = m.end()
    if arg[pos:].strip() or not pieces:
        return None
    return "".join(pieces)


_SIMPLE_ESCAPES = {"n": "\n", "r": "\r", "t": "\t", "a": "\a", "b": "\b",
                   "f": "\f", "v": "\v", "\\": "\\", "'": "'", '"': '"',
                   "?": "?", "0": "\0"}


def _byte_char(value):
    """Map a raw 0-255 byte to a str that round-trips to exactly that one
    byte through `.encode('utf-8', errors='surrogateescape')` -- the same
    convention scan_file() already relies on for a non-UTF-8 source byte, so
    a decoded escape and a raw source byte are charged identically by
    literal_bytes().
    """
    value &= 0xFF
    return chr(value) if value < 0x80 else chr(0xDC00 + value)


def unescape(body):
    """Decode C escapes to the bytes the compiler actually emits.

    Source length is not output length: "\\r\\n" is four characters of source
    and two bytes of message. Measuring the source form would over-count and
    make this tool fail sites that fit.

    A NUMERIC ESCAPE IS DECODED TO ITS REAL BYTE, NEVER BLANKED TO A '?'
    PLACEHOLDER. An earlier version of this function did exactly that,
    reasoning that only the BYTE COUNT of a \\xHH / \\OOO escape mattered.
    That reasoning breaks for the one byte whose VALUE is 0x25: \\x25 (or the
    octal spelling \\045) compiles to a literal '%' character, identical to
    typing % directly in the source -- vsnprintf cannot tell the two apart.
    Blanking it to '?' destroyed that '%' before measure()'s conversion scan
    ever ran, so `LOG_E("Code \\x25.200u", n)` was measured as 11 inert bytes
    instead of the real 205-byte %.200u directive it actually compiles to --
    the exact silent-undercount failure this gate exists to catch (verified
    empirically against ca3b4254f, 2026-09-16: 0 findings on the unfixed
    tool). Decoding to the real byte needs no special case in measure(): a
    plain decoded byte flows through literal_bytes() exactly as before, and a
    decoded '%' is scanned by SPEC_RE exactly like one that was typed.

    Values above 0x7F are re-encoded through _byte_char()'s surrogateescape
    convention, so a decoded escape and a raw non-UTF-8 source byte are
    charged identically downstream.

    An out-of-range hex escape (\\xHH... wider than one byte) is
    implementation-defined per C99 6.4.4.4; XC32/GCC truncate to the target
    char width (8 bits here), so this does too (mod 256) rather than raising
    or guessing at undefined behaviour.
    """
    out, i, n = [], 0, len(body)
    while i < n:
        if body[i] != "\\":
            out.append(body[i])
            i += 1
            continue
        i += 1
        if i >= n:
            break
        c = body[i]
        if c in "xX":
            j = i + 1
            while j < n and body[j] in "0123456789abcdefABCDEF":
                j += 1               # maximal munch, C99 6.4.4.4
            digits = body[i + 1:j]
            out.append(_byte_char(int(digits, 16)) if digits else "x")
            i = j
        elif c in "01234567":
            j = i
            while j < n and j < i + 3 and body[j] in "01234567":
                j += 1
            out.append(_byte_char(int(body[i:j], 8)))
            i = j
        else:
            out.append(_SIMPLE_ESCAPES.get(c, c))
            i += 1
    return "".join(out)


# ---------------------------------------------------------------------------
# ⛔ NOT IMPLEMENTED, AND THE MEASUREMENT SAYS WHY -- read this before adding a
# "the stated byte count must equal the measured one" check.
#
# The idea is sound and it would have caught real defects: four comments in
# this tree have overclaimed their own byte budget, one of them inside a
# sentence asserting "measured, not estimated" (sd_card_manager.c, corrected
# by this PR: 56/107/18 where the truth is 57/108/17). The blocker is not the
# comparison. It is that THE ASSOCIATION BETWEEN A STATED COUNT AND ITS CALL
# IS NOT RELIABLY DERIVABLE FROM POSITION, proven in both directions on
# 2026-10-16:
#
#   STRICT (the contiguous comment run above the call, i.e. what
#   find_annotation already walks): 8 LOG_E sites tree-wide state a count, and
#   all 8 AGREE. The check would be INERT -- and it misses the one real
#   discrepancy, because at sd_card_manager.c the "margin 18" comment is
#   separated from its LOG_E by three lines of code (an assignment, an enum,
#   a flag), so the walk correctly stops and never reaches it.
#
#   LOOSE (nearest call within a character window): finds that one, and
#   MISPAIRS -- it produced a second "mismatch" that was the instrument
#   pairing a comment with the wrong call, not a defect.
#
# A feature whose strict form is inert and whose loose form fabricates is not
# a tuning problem. So the fix is to make the claim checkable WHERE IT IS
# MADE, by carrying the number in the annotation itself:
#
#     /* log_budget: max=51 worst=108 */
#
# Then the association is exact, the gate verifies `worst=N` when present and
# is silent otherwise -- zero findings today, zero baseline churn, no false
# pairings -- and the class is closed for anyone who opts in. Deliberately not
# built on this PR: it had taken a finding on its own previous fix in three
# consecutive audit rounds, and growing scope was the best-measured way to
# keep it from landing.
# ---------------------------------------------------------------------------


def split_code_comments(src_lines):
    """Per line: (surviving code, comment text), from ONE forward walk.

    Two views are derived from this and nothing re-walks: comment_only_map()
    asks whether any code survives, and comment_text() asks what the comments
    on a line actually say. They were separate before, and the gap between
    them was a finding -- see comment_text().

    COMMENT-ONLY-NESS IS NOT DECIDABLE LINE BY LINE, and two rounds of trying
    produced two rounds of near-misses (codex audit on #1110, reopened). The
    same leading `*` means opposite things depending on state:

        in a block comment:      * explaining the bound        -> comment
        NOT in a block comment:  *ptr = value;                 -> CODE
        closing a block:        */ LOG_E("short %s", "x");     -> CODE after it

    The first fix special-cased the block-interior forms by shape, so it
    accepted BOTH of the latter two as comment-only -- which let a bound leak
    past a call again, and additionally read a pointer dereference as a
    comment. A bound that leaks forward is worse than no bound: the next call
    reads clean and the unchanged baseline then records that silence as
    accepted.

    So this scans FORWARD over the whole file, tracking block state, and keeps
    the one property that was right all along: *does any code survive removing
    this line's comments*. String and character literals are skipped so a `/*`
    inside a message cannot open a phantom block -- no shipping LOG_E contains
    one today, but the previous two attempts were also correct on today's
    corpus.

    ⛔ A PREVIOUS REVISION OF THIS DOCSTRING ENDED: "Annotations on the macro's
    OWN line never depend on this: find_annotation reads that line directly
    rather than through the walk." That sentence WAS the defect, stated as a
    design note (codex audit on #1110 r4, both legs). Reading the line directly
    meant `LOG_E("log_budget: max=1 %s", big)` had its FORMAT STRING read as an
    annotation -- no comment on the line at all -- so the gate accepted a
    message needing 144 bytes against a ceiling of 125. The walk already knew
    that text was a string literal; find_annotation just was not asking it.
    """
    out = []
    in_block = False
    for raw in src_lines:
        code = []
        cmt = []
        i = 0
        n = len(raw)
        while i < n:
            if in_block:
                j = raw.find("*/", i)
                if j < 0:
                    cmt.append(raw[i:])
                    break                      # rest of the line is comment
                cmt.append(raw[i:j])
                in_block = False
                i = j + 2
                continue
            if raw.startswith("//", i):
                cmt.append(raw[i + 2:])
                break                          # rest of the line is comment
            if raw.startswith("/*", i):
                in_block = True
                i += 2
                continue
            ch = raw[i]
            if ch in "\"'":
                code.append(ch)                # a literal IS code
                i += 1
                while i < n:
                    if raw[i] == "\\":
                        i += 2
                        continue
                    if raw[i] == ch:
                        i += 1
                        break
                    i += 1
                continue
            code.append(ch)
            i += 1
        out.append(("".join(code), " ".join(cmt)))
    return out


def comment_only_map(src_lines, split=None):
    """Per line: True iff it carries comment text and NO code."""
    if split is None:
        split = split_code_comments(src_lines)
    return [not code.strip() for code, _cmt in split]


def comment_text(src_lines, idx, split=None):
    """The COMMENT text on line `idx` (0-based), with code and literals gone.

    This is the half find_annotation was missing. An annotation is a comment,
    so it is looked for in comment text and nowhere else; a `log_budget: max=N`
    appearing inside a format string is message text that happens to read like
    a marker, and treating it as a bound is how the gate blessed a 144-byte
    message (codex audit on #1110 r4). Derived from the same walk as
    comment_only_map so the two cannot disagree about what a comment is.
    """
    if split is None:
        split = split_code_comments(src_lines)
    if not (0 <= idx < len(split)):
        return ""
    return split[idx][1]


def find_annotation(src_lines, line, split=None):
    """Return the widths declared by a /* log_budget: max=N[,N...] */ marker.

    Searched on the macro's own line and across the contiguous run of comment
    lines immediately above it, because the house style for this class of fix
    puts a multi-line block comment there and an annotation naturally belongs
    inside it.

    ⛔ SEARCHED IN COMMENT TEXT ONLY, never in the raw line. The raw line also
    holds the call's own format string, and `LOG_E("log_budget: max=1 %s", big)`
    then reads as an annotation although the file contains no such comment --
    the gate's own ceiling defeated by message text shaped like a marker
    (codex audit on #1110 r4, both legs). comment_text() comes off the same
    walk as comment_only_map(), so "what is a comment" has one answer here.
    """
    if split is None:
        split = split_code_comments(src_lines)
    cmap = comment_only_map(src_lines, split)
    idx = line - 1
    # A line number past EOF has no annotation and, importantly, must not fall
    # through to the backward scan -- that would read the comment block at the
    # END of the file as this line's. ambiguous_annotation() used to carry this
    # bound check itself; it delegates here now, so the check lives here.
    if not (0 <= idx < len(split)):
        return None
    texts = [comment_text(src_lines, idx, split)]
    k = idx - 1
    while k >= 0 and cmap[k]:
        texts.append(comment_text(src_lines, k, split))
        k -= 1
    for text in texts:
        m = ANNOTATION_RE.search(text)
        if m:
            vals = [x.strip() for x in m.group(1).split(",")]
            for v in vals:
                if not ANNOTATION_DECIMAL_RE.match(v):
                    # ⛔ #1110 r9: REFUSED, not interpreted. `max=0x80` used to
                    # match the leading `0` alone and budget that %s at ZERO --
                    # a silencing annotation the author never wrote. There is
                    # no safe guess: 0x80 could mean 128 or be a typo, and
                    # either reading sets a real message's budget.
                    raise ToolError(
                        f"line {line}: /* log_budget: max={m.group(1)} */ has a "
                        f"value that is not a decimal integer ({v!r}). Hex, "
                        f"octal, suffixes and identifiers are refused rather "
                        f"than interpreted -- write the width in decimal.")
            return [int(v) for v in vals]
    return None


# ---------------------------------------------------------------------------
# Measurement
# ---------------------------------------------------------------------------
Finding = collections.namedtuple("Finding", "path line reason fmt")


def literal_bytes(chunk):
    """Return (emitted byte count, None) for a run of literal format text.

    Returns (None, reason) if the chunk is not literal text after all. Both of
    measure()'s literal chunks -- the text before each conversion and the tail
    after the last one -- go through this one function, so neither can be fixed
    while the other is left behind.

    BYTES, NOT CHARACTERS. The ceiling read out of Logger.c is a byte budget:
    `vsnprintf` formats into a `char[LOG_MESSAGE_SIZE]`, so a two-byte UTF-8
    character spends two of the 125. Counting Python characters under-counts
    any non-ASCII message and would call a site that truncates "safe". The
    encoding here is the one scan_file() reads sources with (utf-8 with
    `surrogateescape`), so a source byte that is not valid UTF-8 round-trips to
    exactly the one byte the compiler emits instead of raising.

    A SURVIVING '%' IS NOT LITERAL TEXT. SPEC_RE has already consumed every
    conversion this tool understands, `%%` included -- its `conv` class
    contains a literal '%', so an escaped percent is matched and charged one
    byte in measure()'s loop, never left in a chunk seen here. A '%' that
    reaches this function is therefore a directive `vsnprintf` WILL act on and
    this tool could not parse: a typo, or a conversion outside the C99 set.
    What it prints is unspecified -- it can consume the wrong argument or emit
    something wider than anything in WIDTHS -- so charging its source bytes as
    plain text would be a bound the tool cannot justify. It is reported
    unbounded, on the same footing as a '*' width or an unannotated %s, which
    is what this gate exists to do with anything it cannot measure.
    """
    i = chunk.find("%")
    if i >= 0:
        return None, (f"unbounded: unrecognized printf sequence "
                      f"{chunk[i:i + 16]!r} -- not a conversion this tool can "
                      f"parse, and vsnprintf's behaviour for one is "
                      f"unspecified")
    return len(chunk.encode("utf-8", errors="surrogateescape")), None


def measure(fmt, annotation):
    """Return (worst_case, None) or (None, reason) for a decoded format string.

    A reason means the worst case could not be bounded. It is returned instead
    of a number, never instead of a finding: every caller treats an unbounded
    site exactly as it treats an over-budget one.
    """
    fixed, total, pos = 0, 0, 0
    str_widths = list(annotation) if annotation else None
    str_seen = 0
    for m in SPEC_RE.finditer(fmt):
        n, reason = literal_bytes(fmt[pos:m.start()])
        if reason is not None:
            return None, reason
        fixed += n
        pos = m.end()
        conv = m.group("conv")
        if conv == "%":
            fixed += 1                     # a literal percent sign, not a sub
            continue
        flags, width, prec = m.group("flags"), m.group("width"), m.group("prec")
        if width == "*" or prec == "*":
            return None, (f"unbounded: '{m.group(0)}' takes its width from an "
                          f"argument")
        field = int(width) if width else 0
        length = m.group("length") or ""
        if conv in "sc" and length == "l":
            # %ls/%lc take a wchar_t*/wchar_t, not a narrow char*/char --
            # refused explicitly, by name, rather than falling through to the
            # narrow %s/%c handling below (which does not look at `length` at
            # all) or to WIDTHS' generic "no conservative width" message. See
            # the "lc/ls DELIBERATELY ABSENT" note above WIDTHS for why a
            # guessed multi-byte constant is not used instead.
            return None, (
                f"unresolvable: '%{length}{conv}' is a WIDE conversion "
                f"(wchar_t) -- its printed width on this vsnprintf is "
                f"unverified and firmware/src has zero %l{conv} call sites "
                f"to derive one from; annotate or extend WIDTHS only after "
                f"checking what this target's vsnprintf actually emits for "
                f"one")
        if conv == "s":
            str_seen += 1
            if prec not in (None, ""):
                # %.Ns prints at most N characters -- bounded by the format
                # itself, no annotation needed.
                total += max(field, int(prec))
                continue
            if str_widths is None:
                return None, ("unresolvable: %s with no /* log_budget: max=N */ "
                              "annotation")
            if len(str_widths) == 1:
                total += max(field, str_widths[0])
            elif str_seen <= len(str_widths):
                total += max(field, str_widths[str_seen - 1])
            else:
                return None, (f"annotation declares {len(str_widths)} width(s) "
                              f"but the call has more %s than that")
            continue
        key = length + conv
        if key not in WIDTHS:
            # #1110 r8 review LOW: the remedy is CONVERSION-SPECIFIC, because
            # "add it to WIDTHS" is actively bad advice for %f. A conservative
            # constant for a double is about 310 characters -- DBL_MAX is
            # ~1.8e308, so %f prints 309 integer digits plus '.' plus the
            # precision -- which is larger than the whole LOG_MESSAGE_SIZE
            # buffer. Anyone following the old text would add a small number
            # that LOOKS conservative and is not, in the under-reporting
            # direction.
            #
            # ⛔ #1110 r9 review LOW: and the r8 version of this advice said
            # "use %e/%g, which are bounded". MEASURED: e, E, g, G, a, A and p
            # are ALL absent from WIDTHS, so switching to %e reports the
            # identical "no conservative width" finding. Advice that cannot
            # work is worse than none -- it sends the reader round a loop. For
            # a float the only sound remedy is annotating the site. %p is
            # genuinely derivable (8 hex digits on this target) and keeps the
            # add-to-WIDTHS advice, which is why the two messages differ.
            #
            # ⚠ The word "maximum", not "worst case", is deliberate. The
            # over-limit finding's reason reads "worst case N bytes > ceiling
            # N", and this tool's output IS consumed by greps -- its own
            # baseline is keyed on this text. Using the same phrase here made a
            # plain `grep 'worst case'` count this finding as an over-limit
            # one, which is exactly how a measurement acquires a false
            # positive. Keep the two classes textually distinguishable.
            if conv in "fF":
                return None, (
                    f"no conservative width for '{m.group(0)}' (conversion "
                    f"'%{conv}') -- and do NOT add one to WIDTHS: a double's "
                    f"maximum is ~310 characters (DBL_MAX is ~1.8e308, so "
                    f"%{conv} prints ~309 integer digits plus the precision), "
                    f"which is larger than the whole buffer. Annotate the site "
                    f"with its real bound -- annotation is the only sound "
                    f"remedy here, because NO floating conversion has a WIDTHS "
                    f"entry: %e/%g/%a are absent too, so switching between them "
                    f"reports the identical finding")
            return None, (f"no conservative width for '{m.group(0)}' "
                          f"(conversion '%{conv}') -- add it to WIDTHS in "
                          f"tools/lint/log_budget.py or annotate the site")
        w = WIDTHS[key]
        if conv in INT_SIGN_BYTES and prec:
            # An explicit precision on an integer conversion is a MINIMUM digit
            # count, not a maximum: `%.200d` zero-pads to at least 200 digits,
            # so charging the plain 11 would let a site 190 bytes over budget
            # walk through the gate (Qodo round-1 finding 1 on PR #1110). The
            # sign is charged separately because precision counts digits only.
            #
            # `prec` is falsy here for BOTH "no precision" and the bare `%.d`
            # form, which is precision ZERO -- fewer digits than the natural
            # worst case, never more -- so both correctly leave w alone.
            sign = INT_SIGN_BYTES[conv]
            w = sign + max(w - sign, int(prec))
        if "#" in flags and conv in "xXo":
            w += 2                         # the 0x / 0 prefix the '#' flag adds
        total += max(field, w)
    n, reason = literal_bytes(fmt[pos:])
    if reason is not None:
        return None, reason
    fixed += n
    if str_widths is not None and str_seen and len(str_widths) > max(1, str_seen):
        return None, (f"annotation declares {len(str_widths)} width(s) but the "
                      f"call has {str_seen} %s")
    return fixed + total, None


def split_lines_1model(text):
    r"""Split into lines on '\n' ONLY, dropping one trailing '\r' per line.

    ⛔ #1110 r9, and the reason it is a module-level rule rather than a local
    choice: str.splitlines() also splits on \v, \f, \x1c, \x1d, \x1e, \x85,
    U+2028 and U+2029, while EVERY line number in this tool comes from
    count("\n"). The two models therefore disagree the moment a message
    contains one of those bytes -- the line LIST gains an element the line
    NUMBERING does not -- and an annotation then binds to a call it was never
    written for, in the silencing direction. Nothing in this module may split
    lines any other way; the self-test pins that structurally.

    The trailing '\r' strip keeps CRLF sources (which this firmware uses)
    yielding clean line text without reintroducing a second line model: the
    NUMBERING is still '\n'-only, which is what has to agree.
    """
    return [ln[:-1] if ln.endswith("\r") else ln for ln in text.split("\n")]


def escape_for_record(text):
    r"""One-line, round-trippable rendering of a format string for the baseline.

    ⛔ #1110 r9: EVERY non-printable byte is escaped, not just \r \n \t. A
    message containing \v or \f went into the baseline raw, and the loader then
    SPLIT one record into two -- the file silently gained a malformed entry and
    lost a real one. A record format is a serialisation boundary: anything that
    can appear in the data and also means something to the reader must be
    escaped at the boundary, not hoped about.
    """
    out = []
    for ch in text:
        if ch == "\\":
            out.append("\\\\")
        elif ch == "\r":
            out.append("\\r")
        elif ch == "\n":
            out.append("\\n")
        elif ch == "\t":
            out.append("\\t")
        elif ord(ch) < 0x20 or ord(ch) == 0x7F:
            out.append("\\x%02x" % ord(ch))
        else:
            out.append(ch)
    return "".join(out)


def ambiguous_annotation(src_lines, line, n_calls, split=None):
    """True when one annotation on `line` would bound SEVERAL calls there.

    An annotation names a bound for A call; a line holding two cannot say
    which, and the old behaviour applied it to BOTH -- so a single `max=1`
    silenced a neighbour that could truncate (codex audit on #1110, both
    legs). Refusing costs a split line; guessing costs a silent overflow that
    the unchanged baseline then blesses as accepted.

    Extracted from scan_file purely so it can be pinned: inline, the condition
    was only reachable by building a source tree, and an untestable branch is
    how the previous two rounds' near-misses survived.

    ⛔ IT ASKS find_annotation RATHER THAN RE-DERIVING THE SCOPE. It used to
    search only the calls' OWN line, while find_annotation also honours the
    contiguous comment block ABOVE -- so a `/* log_budget: max=1 */` on the
    preceding line was applied to BOTH calls and refused by neither, which is
    precisely the silence this guard exists to prevent (codex audit on #1110
    r4, both legs). A guard whose scope is NARROWER than the thing it guards
    passes exactly the cases that matter, so the two now share one definition
    of "an annotation is in scope for this line" by construction, not by two
    implementations that happen to agree.
    """
    if n_calls <= 1:
        return False
    return find_annotation(src_lines, line, split) is not None


def scan_file(path, rel, macros, ceiling):
    src = path.read_text(encoding="utf-8", errors="surrogateescape")
    src_lines = split_lines_1model(src)
    spliced, orig_pos = splice_continuations(src)
    masked = mask_comments(spliced)
    found = []
    # ⛔ THE ANNOTATION VIEW READS THE SAME SPLICED TEXT AS THE CALL VIEW.
    # It used to read the RAW physical lines while find_calls() read the
    # spliced text, so the two halves of one question saw different source
    # (codex audit on #1110 r5, both legs). C translation phase 2 deletes
    # backslash-newline BEFORE tokens exist, so
    #
    #     puts("a\
    #     /* log_budget: max=1 */"); LOG_E("%s", big);
    #
    # is ONE logical line whose marker is STRING CONTENT. Reading the physical
    # lines made line 2 look like a comment line and bound a max=1 to the
    # LOG_E, silencing a message that truncates. ⭐ Sharing a parser is not
    # enough when each side PREPARES ITS OWN INPUT -- that is where this
    # round's divergence moved after the readers were unified.
    spliced_lines = split_lines_1model(spliced)
    # Computed ONCE per file: split_code_comments() is a forward scan from the top
    # (block state cannot be recovered from a line in isolation), so letting
    # find_annotation() derive it per call site would be quadratic in a file
    # with many LOG_E sites.
    split = split_code_comments(spliced_lines)
    calls = list(find_calls(masked, macros))
    # ⛔ #1110 r8/4: the ambiguity count must see EVERY log family, not only
    # the scanned one. With `LOG_I("x"); /* log_budget: max=1 */ LOG_E("%s", s);`
    # the count saw ONE call (the LOG_E), judged the bound unambiguous, and let
    # an annotation written for the LOG_I silence the LOG_E's unresolvable %s.
    # Measured. The MEASURED set stays `macros`; only the count widens.
    all_families = dict(DEFAULT_MACROS)
    all_families.update(OPTIONAL_MACROS)
    all_families.update(macros)
    # How many log-macro calls share each line. An annotation on a line holding
    # MORE THAN ONE call cannot say which call it bounds, and the old behaviour
    # was to apply it to every one of them -- so a single `max=1` silenced a
    # neighbour that could truncate (codex audit on #1110, both legs). A bound
    # whose subject is ambiguous is refused rather than guessed: refusing costs
    # a split line, guessing costs a silent overflow the baseline then blesses.
    # Keyed by SPLICED line, because that is the line an annotation can bind
    # to after phase 2 -- two calls joined by a continuation share one logical
    # line even though they occupy two physical ones. Reported line numbers
    # stay ORIGINAL (see `line` below); only the lookup key is spliced.
    def spliced_line(p):
        return spliced.count("\n", 0, p) + 1

    # ⛔ #1110 r9: count every line a call TOUCHES, not the line it starts on.
    # r8/4 widened this to every family but still keyed on the START line, so a
    # MULTI-LINE call whose last line is another call's line was not counted
    # there -- and its annotation leaked onto that other call. Overlap, not
    # start, is the question an annotation's subject depends on.
    calls_touching = collections.Counter()
    for _m, _p, _a, _raw in find_calls(masked, all_families):
        first = spliced_line(_p)
        last = spliced_line(_p + max(len(_raw) - 1, 0)) if _raw else first
        for _ln in range(first, last + 1):
            calls_touching[_ln] += 1
    for macro, pos, args, _call in calls:
        line = pos_to_line(src, orig_pos, pos)   # for REPORTING: physical line
        sline = spliced_line(pos)                # for LOOKUP: logical line
        if ambiguous_annotation(spliced_lines, sline, calls_touching[sline],
                                split):
            # ⛔ #1110 r9: REFUSED, not reported as a finding. An annotation
            # whose subject is ambiguous cannot be read at all -- and the
            # failure mode of guessing is SILENCING a message that truncates,
            # which a baselined finding would then bless. Outside the declared
            # subset the tool stops rather than picks.
            raise ToolError(
                f"{rel}:{line}: a /* log_budget: max=N */ annotation is in "
                f"scope here and {calls_touching[sline]} log calls share this "
                f"line (any part of a call counts, including a multi-line call "
                f"ENDING here), so it cannot say which call it bounds. Put each "
                f"call on its own line.")
        if args is None:
            # #1110 r8/3: find_calls could not balance this call's parentheses.
            found.append(Finding(
                rel, line,
                f"unresolvable: {macro}'s argument list is not balanced -- the "
                f"parenthesis walk reached end-of-file. A preprocessor "
                f"conditional inside the arguments does this. Reported rather "
                f"than skipped: an unmeasured call used to read as a clean one",
                ""))
            continue
        fmt_index = macros[macro]
        if len(args) <= fmt_index:
            found.append(Finding(rel, line, f"{macro} has no format argument",
                                 ""))
            continue
        fmt = literal_text(args[fmt_index])
        if fmt is None:
            found.append(Finding(
                rel, line,
                f"unresolvable: {macro}'s format argument is not a string "
                f"literal", ""))
            continue
        worst, reason = measure(fmt, find_annotation(spliced_lines, sline,
                                                     split))
        if reason is not None:
            found.append(Finding(rel, line, reason, escape_for_record(fmt)))
        elif worst > ceiling:
            found.append(Finding(
                rel, line, f"worst case {worst} bytes > ceiling {ceiling}",
                escape_for_record(fmt)))
    return found


#: What scan() actually visits. DECLARED rather than implied, because the gate
#: reports "clean" and a reader reasonably hears "no LOG_E in this tree can
#: truncate" -- which is not what is measured.
#:
#: ⛔ KNOWN COVERAGE GAP, measured 2026-10-15 (codex audit on #1110, found by
#: the BLIND leg): LOG_E calls in HEADERS are never visited. There is one such
#: file today, `services/SCPI/SCPIInterface.h`, with five calls in inline
#: functions that compile into every translation unit including it -- and one
#: of them, `scpi_printf`'s truncation diagnostic at :238, measures 165 bytes
#: against this ceiling of 125. So the gap is hiding a real finding.
#:
#: It is NOT closed by adding "*.h" to the glob, which is why this is a
#: declaration and not a fix: measured, that surfaces seven findings and THREE
#: are `Util/Logger.h`'s own macro DEFINITIONS (`LOG_E`, `LOG_E_ONCE`,
#: `LOG_E_SESSION`), where the format argument is a macro parameter rather than
#: a literal. Scanning a definition is meaningless, so header coverage needs a
#: way to skip definitions while scanning uses -- a design decision, plus
#: baseline entries for three unannotated `%s`, plus a real fix for the
#: 165-byte message. Filed separately rather than grown onto this row.
SCANNED_SUFFIXES = ("*.c",)


def scan(root, macros, ceiling):
    findings, scanned = [], 0
    for path in sorted(p for s in SCANNED_SUFFIXES for p in root.rglob(s)):
        rel_in_root = path.relative_to(root).as_posix()
        if any(rel_in_root.startswith(p) for p in EXCLUDED_PREFIXES):
            continue
        scanned += 1
        findings.extend(scan_file(path, path.as_posix(), macros, ceiling))
    return findings, scanned


# ---------------------------------------------------------------------------
# Baseline / suppression
#
# Same idea as tools/lint/cppcheck-baseline.txt: a machine-generated snapshot of
# what the tree reports today, committed, and diffed by CI so only NEW findings
# fail. Regenerating is one command, exactly as it is for cppcheck.
#
# One deliberate difference from the cppcheck baseline's shape: a record is
# keyed on file + reason + the format string, NOT on the line number. cppcheck's
# baseline can afford line numbers because it is empty; this one is not, and a
# LOG_E's line moves whenever anything above it in the file is edited. A
# line-keyed baseline of this size would go red on changes that touched no log
# message at all, which is how a gate gets routinely regenerated without being
# read -- the failure mode that makes a baseline worthless. The line number is
# still printed in the report, where a human needs it.
# ---------------------------------------------------------------------------
def record(f):
    return f"{f.path}{SEP}{f.reason}{SEP}{f.fmt}"


def suppress_key(f):
    return f"{f.path}{SEP}{f.fmt}"


def compare_to_baseline(current, baseline):
    """Return (new, fixed, exit_code) for this tree against the baseline.

    A gate that failed only on NEW findings would be half a gate. The baseline
    is keyed on `file :: reason :: message text`, so a record that stops being
    reported and is left in the file keeps matching if that exact text is ever
    reintroduced in that file -- the regression then lands already blessed, and
    the commit that fixed it need not even be in living memory (Qodo round-1
    finding 2 on PR #1110).

    So drift in EITHER direction is a failure, which is exactly the policy
    tools/lint/cppcheck.sh already runs under (CLAUDE.md, "Static Analysis":
    CI "fails the check if the new output differs from the committed baseline
    -- added findings = potential bugs; removed findings = baseline drift").
    Both directions exit 1 rather than splitting into two codes, because both
    have the identical remedy and the identical meaning: the committed baseline
    no longer describes this tree, and saying so takes one deliberate
    `--write-baseline` commit. Exit 2 stays reserved for "the tool could not
    produce a verdict at all", which is a different kind of statement and must
    never be confused with either.

    Returned as a pure function of two Counters so the self-test can pin the
    verdict directly; main() reads its exit code from here rather than
    recomputing one, so there is a single decision to test.
    """
    new = current - baseline
    fixed = baseline - current
    return new, fixed, (1 if (new or fixed) else 0)


def load_baseline(path):
    if not path.exists():
        return collections.Counter()
    lines = [ln.rstrip("\r\n") for ln in
             split_lines_1model(path.read_text(encoding="utf-8"))]
    return collections.Counter(
        ln for ln in lines if ln.strip() and not ln.lstrip().startswith("#"))


def load_suppressions(path):
    """Parse the suppression file, requiring a reason comment above each entry.

    The requirement is enforced rather than merely documented: an entry with no
    comment above it is a tool error, not a silent pass. A permanent waiver with
    no stated reason is indistinguishable from an accident, and this file is the
    one place in the gate where a human can turn a finding off for good.

    Returns a Counter, not a set, because ONE entry waives ONE finding. The key
    is `<file> :: <format string>` and carries no line number (deliberately --
    see the baseline comment above), so two call sites in the same file logging
    the same text share a key. With a set, the single reason comment the loop
    below insists on would cover both of them, and every further copy added
    later, without anyone writing a word about the second one -- the same
    "one reason, unlimited waivers" inversion the had_comment reset fixes
    within the file, reappearing at the point of USE. Counting here lets
    apply_suppressions() spend each waiver exactly once.
    """
    if not path.exists():
        return collections.Counter()
    entries, had_comment = collections.Counter(), False
    for lineno, raw in enumerate(
            split_lines_1model(path.read_text(encoding="utf-8")), 1):
        line = raw.rstrip()
        if not line.strip():
            had_comment = False
            continue
        if line.lstrip().startswith("#"):
            had_comment = True
            continue
        if not had_comment:
            raise ToolError(
                f"{path}:{lineno}: suppression entry has no reason comment "
                f"above it -- every waiver must say why it is safe:\n"
                f"    {line}")
        if SEP not in line:
            raise ToolError(
                f"{path}:{lineno}: malformed entry, expected "
                f"'<file>{SEP}<format string>':\n    {line}")
        entries[line.strip()] += 1
        # A reason authorizes exactly ONE waiver. Clearing the flag here --
        # not only on a blank line -- is what makes the requirement PER-ENTRY:
        # left set, a single comment would silently cover every consecutive
        # entry below it, which is the whole safety property inverted (Qodo
        # round-1 finding 3 on PR #1110). The next line must now be a comment
        # of its own or it is rejected above.
        had_comment = False
    return entries


def apply_suppressions(findings, suppressions):
    """Return (kept, suppressed_count), spending each waiver at most once.

    A waiver is a claim about ONE message that a human reasoned about and wrote
    a reason for, so it retires ONE finding. A membership test would instead
    retire every finding sharing the key, which for a key with no line number
    in it means every OTHER copy of the same text in that file -- including
    copies added months later, which nobody has reasoned about and which are
    exactly what a gate is for. Findings are spent in the order given, so which
    copy is waived is stable for a given tree; it does not matter which, since
    the key cannot tell them apart in the first place.

    A pure function of a list and a Counter, for the same reason
    compare_to_baseline() is one: it is a decision the gate makes, so the
    self-test pins it directly and main() has no second copy of the logic.
    """
    remaining = collections.Counter(suppressions)
    kept, suppressed = [], 0
    for f in findings:
        key = suppress_key(f)
        if remaining[key]:
            remaining[key] -= 1
            suppressed += 1
        else:
            kept.append(f)
    return kept, suppressed


# ---------------------------------------------------------------------------
# Self-test
# ---------------------------------------------------------------------------
SELF_TEST_CEILING = 125

# U+00B5 MICRO SIGN: one character, and the two bytes C2 B5 once encoded. The
# one fixture ingredient this file cannot write as plain ASCII, so it is built
# from its codepoint (see the byte-counting cases below).
MICRO_SIGN = chr(0xB5)

# ⛔ #1110 r9: THE DECLARED SUBSET'S REJECTIONS, one self-test each.
#
# Nine parser edge cases over r7 and r8 shared one shape: the tool INTERPRETED
# something it cannot fully model and guessed PERMISSIVELY. r9 converges that as
# a class -- outside the subset stated in this module's header the tool REFUSES
# rather than guesses. Each refusal is pinned here, because a refusal that stops
# firing is indistinguishable from a tool that started guessing again.
#
# (name, source body, substring the ToolError must contain, why)
SELF_TEST_REJECTION_CASES = [
    (
        "r9: max= must be decimal",
        '/* log_budget: max=0x80 */ LOG_E("%s", s);',
        "not a decimal",
        "ANNOTATION_RE captured [0-9]+, so 0x80 matched the leading 0 and the "
        "%s was budgeted at ZERO -- a silencing annotation the author never "
        "wrote. Hex is refused, not interpreted",
    ),
    (
        "r8/4 (now a refusal): a LOG_I annotation on a LOG_E's line",
        'LOG_I("x"); /* log_budget: max=1 */ LOG_E("%s", s);',
        "share this line",
        "r8 made this a FINDING once the count spanned every family; r9 "
        "converts the whole class to a refusal, because a baselined finding "
        "would BLESS an annotation that silences a truncating message. The "
        "behaviour change is deliberate and this case records it",
    ),
    (
        "r9: an annotation may not share a line with another call's span",
        'LOG_I("a"\n      "b"); /* log_budget: max=1 */ LOG_E("%s", s);',
        "share this line",
        "r8/4 counted calls by their START line, so a MULTI-LINE call ending on "
        "the LOG_E's line was not counted and its annotation leaked onto the "
        "LOG_E. Overlap, not start, is the question",
    ),
]

SELF_TEST_CASES = [
    (
        "r8/3: a preprocessor conditional unbalances the argument list",
        'LOG_E("a message long enough to truncate once it is measured at all, '
        'padded out past the ceiling so a SKIP is visibly wrong", x\n#if A\n'
        '      , f(1\n#else\n      , g(2\n#endif\n',
        1,
        "the paren walk reaches EOF and used to `continue`, so the call was "
        "SILENTLY SKIPPED -- 'could not evaluate' must never read as 'clean'",
    ),
    (
        "under budget",
        'LOG_E("PrepareStreamingBuffers: partition failed USB=%u WiFi=%u '
        'enc=%u sd=%u", a, b, c, d);',
        0,
        "61 fixed + 4x10 = 101, inside the ceiling",
    ),
    (
        "over budget with no substitutions",
        'LOG_E("WiFi unreachable and an SD card IS present on the shared bus '
        '- the card is likely bus-incompatible; remove it (wiki: '
        'SD-Card-Compatibility)");',
        1,
        "139 fixed bytes, nothing interpolated: truncates on every firing",
    ),
    (
        "numeric substitutions push it over",
        'LOG_E("Clock mismatch (#716): PBCLK3 is %u Hz, image built for %u Hz. '
        'Device configuration words hold an older PLL and cannot be updated by '
        'a firmware update - reprogram with a PICkit/IPE to reach the intended '
        'clock.", x, y);',
        1,
        "the fixed text alone is over; the two %u make it worse",
    ),
    (
        "annotated %s within its declared max",
        '/* log_budget: max=40 */\nLOG_E("SD start refused (#689): %s", why);',
        0,
        "30 fixed + 40 declared = 70",
    ),
    # ⛔ r5 FINDING 3, and reachable ONLY end-to-end: the defect was in how
    # scan_file PREPARES the two views, so nothing at find_annotation's own
    # level could see it. Both halves now read the spliced text.
    (
        "a marker inside a SPLICED string literal is not an annotation",
        'puts("a\\\n/* log_budget: max=1 */"); LOG_E("%s", big);',
        1,
        "r5 both legs: valid C -- phase 2 deletes the backslash-newline "
        "BEFORE tokens exist, so this is ONE logical line whose marker is "
        "STRING CONTENT. Reading the raw physical lines made line 2 look "
        "like a comment and bound max=1 to the LOG_E, reporting 0 findings "
        "for a message that truncates. Sharing a parser does not help when "
        "each side prepares its own input",
    ),
    (
        "the same two lines WITHOUT the continuation really do bind",
        'puts("a");\n/* log_budget: max=1 */\nLOG_E("%s", big);',
        0,
        "the control that makes the case above a discriminator rather than a "
        "blanket refusal: with no splice the marker IS a comment line, the "
        "bound is honoured, and the call measures inside the ceiling",
    ),
    (
        "unannotated %s",
        'LOG_E("SD start refused (#689): %s", why);',
        1,
        "short, but unbounded -- reported, never assumed to be zero",
    ),
    (
        "annotated %s that still does not fit",
        '/* log_budget: max=98 */\nLOG_E("[SD] STR:START refused (#689): this '
        'is a long prefix that leaves no room at all for the reason: %s", r);',
        1,
        "the annotation bounds it; the bound is still over the ceiling",
    ),
    (
        "multi-line concatenated literal",
        'LOG_E("Clock mismatch (#716): PBCLK3 is %u Hz, image built for "\n'
        '      "%u Hz. Device configuration words hold an older PLL and "\n'
        '      "cannot be updated by a firmware update - reprogram with a "\n'
        '      "PICkit/IPE to reach the intended clock.", x, y);',
        1,
        "adjacent literals are ONE message; measuring only the first would "
        "call this site clean",
    ),
    (
        "a comment that looks like a call",
        '/* This used to read:\n'
        ' * LOG_E("WiFi unreachable and an SD card IS present on the shared '
        'bus - the card is likely bus-incompatible; remove it (wiki: '
        'SD-Card-Compatibility)");\n'
        ' */\nLOG_E("WiFi down; remove the SD card");',
        0,
        "the house style quotes the old over-budget text in the fix comment; "
        "an unmasked scanner would report the message the commit just fixed",
    ),
    (
        "%% is a literal percent",
        'LOG_E("ADC gain error 12%% over the trim window; recalibrate");',
        0,
        "one fixed byte, not a substitution and not deleted",
    ),
    (
        "%% does not hide an over-budget message",
        'LOG_E("ADC gain error 12%% over the trim window and well past every '
        'documented limit for this part; recalibrate the analog front end "\n'
        '      "before trusting any reading it produces");',
        1,
        "counting %% as zero bytes instead of one must not shorten a message "
        "into passing",
    ),
    (
        "escape sequences count as emitted bytes",
        r'LOG_E("SD log arm refused (#942): a reason\r\n");',
        0,
        r'\r\n is 4 source chars and 2 output bytes',
    ),
    (
        "unrecognized specifier",
        'LOG_E("Sample mean %f V", mean);',
        1,
        "%f has no conservative constant width -- named, not skipped",
    ),
    (
        "star width is unbounded",
        'LOG_E("Value %*d", w, v);',
        1,
        "the width comes from an argument, so no static bound exists",
    ),
    (
        "precision bounds a %s without an annotation",
        'LOG_E("SD start refused (#689): %.40s", why);',
        0,
        "%.40s prints at most 40 characters -- bounded by the format itself",
    ),
    (
        "non-literal format argument",
        'LOG_E(gFormatFromTable, x);',
        1,
        "not statically knowable -- reported rather than skipped",
    ),
    # ---- integer precision (Qodo round-1 finding 1 on PR #1110) ------------
    # C99 7.19.6.1p6: on d/i/o/u/x/X the precision is a MINIMUM DIGIT COUNT,
    # so it can only make a conversion wider. Charging the plain WIDTHS entry
    # let a site hundreds of bytes over budget through; every case below is
    # one the pre-fix tool called clean, or a boundary that pins the sign and
    # prefix arithmetic in BOTH directions so a fix that over-charges is
    # caught as readily as one that under-charges.
    (
        "integer precision alone pushes a site over",
        'LOG_E("Code %.200d", v);',
        1,
        "%.200d zero-pads to at least 200 digits; the pre-fix tool charged "
        "the plain 11 and called this 16 bytes",
    ),
    (
        "octal and hex precision are charged too",
        'LOG_E("mode=%.200o mask=%.200x", m, k);',
        1,
        "11 fixed + 200 + 200 = 411; the pre-fix tool charged 11+8 and called "
        "it 30",
    ),
    (
        "unsigned precision is not charged a sign byte",
        'LOG_E("Trim word (zero-padded): %.100u", w);',
        0,
        "25 fixed + 100 digits = 125, exactly at the ceiling -- a phantom "
        "sign byte would fail a site that fits",
    ),
    (
        "signed precision IS charged its sign byte",
        'LOG_E("Trim word (zero-padded): %.100d", w);',
        1,
        "the same 25 fixed + 1 sign + 100 digits = 126: precision counts "
        "digits, the sign is separate, and dropping it would call this clean",
    ),
    (
        "'#' prefix is charged on top of the precision",
        'LOG_E("Register mask (padded): %#.100x", m);',
        1,
        "24 fixed + 2 ('0x') + 100 digits = 126; the prefix is not a digit, "
        "so precision does not absorb it",
    ),
    (
        "...and only when the '#' flag is there",
        'LOG_E("Register mask (padded): %.100x", m);',
        0,
        "the same site without '#' is 24 + 100 = 124 -- the twin above must "
        "fail for the prefix, not because precision is over-charged",
    ),
    (
        "a precision SMALLER than the natural width changes nothing",
        'LOG_E("Streaming stalled while the encoder drained and the transport '
        'did not come back; sample counter stands at %.1llu", n);',
        1,
        "106 fixed + 20 = 126: %.1llu still prints up to 20 digits, so the "
        "charge is max(natural, precision), never the precision alone",
    ),
    # ---- unparsed '%' sequences ------------------------------------------
    # (Qodo on PR #1110: "Reject unparsed format sequences".)
    # Everything between two SPEC_RE matches used to be counted as literal
    # text, so a '%' the regex could not parse was charged as its own source
    # bytes and the site was pronounced measured. It is not measured: what
    # vsnprintf does with an unrecognized directive is unspecified. Both
    # literal chunks -- between matches, and the tail after the last one --
    # have a case, because a fix applied to only one of them would leave the
    # other silently charging source bytes.
    (
        "an unparsed '%' BETWEEN two specifiers is unbounded",
        'LOG_E("Trim %q applied at step %u", a, b);',
        1,
        "%q is not a C99 conversion; the pre-fix tool charged its 2 source "
        "bytes as plain text and called the site measured",
    ),
    (
        "an unparsed '%' AFTER the last specifier is unbounded",
        'LOG_E("Step %u reached, gain %q", a, b);',
        1,
        "the tail chunk after the final match is a second accounting site, "
        "and a fix that touched only the in-loop one would miss it",
    ),
    (
        "a trailing bare '%' is unbounded",
        r'LOG_E("Duty cycle reached 100%");',
        1,
        "a '%' with no conversion character after it is a directive C does "
        "not define; it is not a literal percent sign",
    ),
    (
        "%% is still a literal percent after that rejection",
        'LOG_E("100%% done");',
        0,
        "SPEC_RE matches %% (its conv class contains '%'), so an escaped "
        "percent never reaches the unparsed-'%' check -- the rejection must "
        "not turn every correctly-escaped message into a finding",
    ),
    # ---- literal text is measured in BYTES, not characters ----------------
    # (Qodo on PR #1110: "Measure literals by emitted bytes".)
    # Logger's ceiling bounds the char[] vsnprintf writes into, so a two-byte
    # UTF-8 character spends two of the 125. These twins are the SAME
    # 125-character message differing in one character: 'u' becomes U+00B5
    # MICRO SIGN, which encodes as the two bytes C2 B5. The character count
    # cannot tell them apart; the byte count must. Counting characters -- what
    # the tool did before -- passes both, so the second twin is the case that
    # goes red on the pre-fix code, and the first is here to prove the fix
    # does not simply over-charge everything.
    #
    # The character is built with chr() rather than written inline, so this
    # file stays ASCII and the codepoint under test is named instead of
    # eyeballed. Not a "\\u00b5" escape either: one stray backslash there and
    # the fixture silently becomes a 129-byte ASCII message that fails for
    # length rather than for encoding -- green, and testing nothing.
    (
        "a 125-byte ASCII literal sits exactly on the ceiling",
        'LOG_E("Timebase resynchronised: the ADC sample window slipped past '
        'the us guard band, so every reading below is suspect; resync now.");',
        0,
        "125 characters, 125 bytes -- the twin below must fail for its extra "
        "BYTE, not because a 125-byte message is over-charged",
    ),
    (
        "the same 125 characters are over budget with one multi-byte char",
        'LOG_E("Timebase resynchronised: the ADC sample window slipped past '
        'the ' + MICRO_SIGN + 's guard band, so every reading below is '
        'suspect; resync now.");',
        1,
        "U+00B5 is one character and TWO UTF-8 bytes, so this message is 126 "
        "bytes and truncates; len() of the Python string says 125 and calls "
        "it clean",
    ),
    (
        "LOG_E_ONCE takes its format second",
        'LOG_E_ONCE(LOG_BIT_X, "WiFi unreachable and an SD card IS present on '
        'the shared bus - the card is likely bus-incompatible; remove it '
        '(wiki: SD-Card-Compatibility)");',
        1,
        "reading argument 0 would measure the latch bit and call it clean",
    ),
    (
        "a longer macro name is not a LOG_E call",
        'LOG_ERROR_COUNTER(x);',
        0,
        "the call matcher must not fire on an identifier that merely starts "
        "with LOG_E",
    ),
    # ---- five confirmed BLOCK findings from the adversarial pre-merge audit
    # on PR #1110 (2026-09-16, https://github.com/daqifi/daqifi-nyquist-
    # firmware/pull/1110#issuecomment-5698980187), all reproduced empirically
    # against the tool at ca3b4254f before any of these cases existed: every
    # one below reported ZERO findings on the unfixed tool. Two silent-undercount
    # root causes:
    #
    #   A) unescape() replaced a numeric (\\xHH / \\OOO) escape with a literal
    #      '?' placeholder instead of the real decoded byte, and literal_text()
    #      joined adjacent literals' RAW text before decoding instead of after.
    #      Both let a decoded '%' (or a decoded byte that spuriously extends a
    #      neighbouring literal's leading hex/octal digits) vanish before
    #      measure() ever saw it -- an escaped-percent directive was silently
    #      undercounted as inert text.
    #   B) find_calls()'s call-site regex (`\\s*\\(`) never accounted for a C
    #      backslash-newline line continuation between the macro name and its
    #      opening paren -- `\\s` matches a real newline but not the backslash
    #      in front of it, so a call split that way was invisible to the
    #      scanner although GCC splices it into a normal, potentially
    #      truncating call.
    (
        "an escaped '%' (\\x25) must not be destroyed before the conversion "
        "scan sees it",
        r'LOG_E("Code \x25.200u", 1u);',
        1,
        "\\x25 compiles to a literal '%', identical to typing % directly -- "
        "the pre-fix unescape() replaced it with '?' and this 205-byte "
        "%.200u directive measured as 11 inert bytes and passed clean",
    ),
    (
        "the octal spelling of the same escaped '%' (\\045) is caught too",
        r'LOG_E("Code \045.200u", 1u);',
        1,
        "0o45 is also 0x25 -- the octal branch of unescape() had the "
        "identical '?' bug as the hex branch, and a fix that touched only "
        "one branch would leave this one still silently passing",
    ),
    (
        "a hex escape must not consume the next adjacent literal's text",
        'LOG_E("....\\x41" "B" '
        '"WiFi down and an SD card IS present on the shared bus - likely '
        'bus-incompatible; remove it (wiki: SD-Card-Compatibility)");',
        1,
        "C decodes each literal before concatenating (C99 6.4.4.4); joining "
        "the raw prefix first let \\x41's maximal munch eat the following "
        "literal's 'B', undercounting a 126-byte message (real SDPRESENT "
        "site, #589/#1039) by exactly the one byte that put it over the "
        "125-byte ceiling and calling it clean at exactly 125",
    ),
    (
        "a backslash-newline between the macro name and its paren is not "
        "invisible",
        'LOG_E \\\n("' + ("X" * 126) + '");',
        1,
        "GCC splices this into one ordinary call before parsing anything; "
        "the pre-fix call-site regex required only whitespace (never a "
        "backslash) between the macro name and '(' and so never matched "
        "this call at all -- a 126-byte literal, 1 byte over the ceiling, "
        "was entirely absent from the scan",
    ),
    # ---- follow-up Qodo /agentic_review Bug on PR #1110 (2026-09-16, "Split
    # macro names evade checks"), found against the fix for root cause B
    # immediately above: that fix neutralized a continuation by turning its
    # backslash into a SPACE and leaving the newline in place, which works
    # when the continuation falls BETWEEN the macro name and '(' (real
    # whitespace there is already legal) but not when it falls INSIDE the
    # macro name itself -- a space there permanently splits one token into
    # two, and a split token can never match the call-site regex. Fixed by
    # replacing the whitespace-substitution with a real C phase-2 splice
    # (the backslash-newline is deleted, not blanked), applied once up front
    # in scan_file() before comment masking or call matching, with a
    # position map (orig_pos / pos_to_line()) so every reported line number
    # is still the real line in the source file.
    (
        "a continuation splitting the MACRO NAME ITSELF is not invisible "
        "either",
        "LOG\\\n_E(\"" + ("X" * 126) + "\");",
        1,
        "C deletes the backslash and the newline and glues 'LOG' and '_E' "
        "into one token, LOG_E, before anything is tokenized; the "
        "whitespace-substitution fix for the case above left them as two "
        "tokens forever, so a 126-byte literal reached only through a "
        "continuation inside the macro name was still entirely absent from "
        "the scan (verified empirically against 827284cc1, 2026-09-16: "
        "0 findings)",
    ),
    # ---- follow-up Qodo /agentic_review Bug on PR #1110 (2026-09-16, "Valid
    # source fails logging gate"), found against the fix immediately above:
    # once splice_continuations() ran on raw source before literal
    # recognition (matching real C, where phase-2 splicing precedes phase-3
    # tokenization), a continuation that happens to fall INSIDE a string
    # literal is spliced too -- correctly, since that is what C does -- but
    # find_calls() still ran its call-site regex against literal CONTENT
    # left otherwise untouched, so a harmless message like
    # "LOG" + backslash-newline + "_E(value);" became, post-splice, a
    # literal containing the contiguous text LOG_E(value);, which the call
    # regex then matched as if it were a real call -- a valid message
    # incorrectly reported as unresolvable (a false positive, never a
    # silent pass, but still a Bug in this PR's own product code). Fixed
    # with mask_literals(), used only to LOCATE a call; the real content is
    # still read from the untouched masked text at the same offsets.
    (
        "call detection must not fire on text that only LOOKS like a call "
        "inside a literal",
        'LOG_D("LOG\\\n_E(value);");',
        0,
        "a continuation splicing 'LOG' and '_E' together INSIDE a string "
        "literal (not gated -- LOG_D isn't in DEFAULT_MACROS) must not be "
        "mistaken for a real call to the LOG_E macro (verified empirically "
        "against 076bdce84, 2026-09-16: 1 spurious finding)",
    ),
    # ---- deferred /improve item on PR #1110, importance 7, same
    # silent-undercount class as the five above: measure() dispatched purely
    # on the conversion letter and ignored the `l` length modifier, so %lc
    # was charged the same 1 byte as a narrow %c and %ls ran through the
    # identical code path as a narrow %s (including an annotation meant to
    # bound a narrow %s's character count). Disposition: REFUSE rather than
    # charge a guessed multi-byte constant -- see the "lc/ls DELIBERATELY
    # ABSENT" note above WIDTHS for why. firmware/src has zero %ls/%lc call
    # sites as of 2026-09-16 (verified with a known-positive control: these
    # same cases DO fire against the synthetic fixtures below).
    (
        "%lc is a WIDE conversion, not a narrow %c",
        'LOG_E("Char: %lc", wc);',
        1,
        "the pre-fix WIDTHS table charged \"lc\": 1, the same as a narrow "
        "%c, with no verification of what this target's vsnprintf actually "
        "emits for a wchar_t",
    ),
    (
        "%ls is a WIDE conversion, not a narrow %s",
        'LOG_E("Str: %ls", ws);',
        1,
        "measure() dispatched on conv == 's' alone; ignoring the 'l' length "
        "modifier ran %ls through the exact narrow-%s annotation path",
    ),
    (
        "an annotated %ls is still refused, not silently narrowed",
        '/* log_budget: max=40 */\nLOG_E("Str: %ls", ws);',
        1,
        "the annotation exists to bound a narrow %s's CHARACTER count; "
        "applying it to a wide conversion would just be a different "
        "unverified guess -- %ls stays refused regardless of an annotation "
        "aimed at %s",
    ),
]


# Suppression-file fixtures (Qodo round-1 finding 3 on PR #1110).
#
# The suppression file is the one place a finding can be turned off FOREVER,
# and the rule that makes that safe is "every waiver states why". Before the
# fix the reason flag survived an accepted entry, so one comment silently
# covered every consecutive entry under it -- the property inverted, with no
# diagnostic. `expect` is the number of entries accepted, or None for
# "must raise ToolError".
SELF_TEST_SUPPRESSION_CASES = [
    (
        "one comment, one entry",
        "# the reason this waiver is safe\na.c :: msg one\n",
        1,
        "the shape the file documents -- must keep working",
    ),
    (
        "one comment, TWO entries",
        "# the reason this waiver is safe\na.c :: msg one\nb.c :: msg two\n",
        None,
        "the second entry has no reason of its own; before the fix BOTH were "
        "accepted on the strength of the first one's comment",
    ),
    (
        "one comment, THREE entries",
        "# the reason\na.c :: one\nb.c :: two\nc.c :: three\n",
        None,
        "it is not a two-line special case -- the flag stayed set for the "
        "whole run",
    ),
    (
        "two entries, each with its own comment",
        "# why a.c is safe\na.c :: msg one\n# why b.c is safe\nb.c :: msg two\n",
        2,
        "the legitimate multi-waiver shape must still be accepted, or the fix "
        "would have made the file unusable instead of safe",
    ),
    (
        "a blank line does not carry a reason across",
        "# why a.c is safe\na.c :: msg one\n\nb.c :: msg two\n",
        None,
        "pre-existing behaviour, pinned here because the fix moved the code "
        "that clears the flag",
    ),
    (
        "an entry with no comment at all",
        "a.c :: msg one\n",
        None,
        "pre-existing behaviour and the rule's base case",
    ),
    (
        "a malformed entry is rejected, not accepted",
        "# why this is safe\na.c -- no separator here\n",
        None,
        "a waiver that cannot match anything is a typo, not a waiver",
    ),
    (
        "the SAME waiver twice, each with its own reason, counts twice",
        "# why the first site is safe\na.c :: msg one\n"
        "# why the second site is safe\na.c :: msg one\n",
        2,
        "two reasoned waivers for two same-text sites are two waivers; while "
        "this returned a set they collapsed into one, and the count is what "
        "apply_suppressions() spends",
    ),
]

# Waiver-spending fixtures (Qodo on PR #1110: "Limit waivers to one finding").
#
# The suppress key is `<file> :: <format string>` with no line number in it, so
# two call sites in one file logging the same text are indistinguishable to it.
# A membership test therefore let ONE reasoned waiver retire ALL of them,
# including copies added long after the reason was written -- exactly the
# "one comment, unlimited entries" inversion the had_comment reset fixes inside
# the file, reappearing at the point of use. Each case is
# (name, waiver lines, [(path, fmt)...], expected kept, expected suppressed,
# why).
SELF_TEST_WAIVER_CASES = [
    ("one waiver, one matching finding",
     ["a.c :: msg one"], [("a.c", "msg one")], 0, 1,
     "the documented common case -- the fix must not change it"),
    ("one waiver, TWO findings sharing the key",
     ["a.c :: msg one"], [("a.c", "msg one"), ("a.c", "msg one")], 1, 1,
     "the finding: one reason waived both, so the second site was never "
     "reported and never reasoned about"),
    ("two waivers cover both of them",
     ["a.c :: msg one", "a.c :: msg one"],
     [("a.c", "msg one"), ("a.c", "msg one")], 0, 2,
     "waiving both is still possible -- it just costs a second entry with a "
     "second reason, which is the whole point"),
    ("a waiver is keyed on the file too",
     ["b.c :: msg one"], [("a.c", "msg one")], 1, 0,
     "same text, different file: not this waiver's site"),
    ("an unused waiver suppresses nothing",
     ["a.c :: msg one"], [("a.c", "msg two")], 1, 0,
     "a waiver retires the message it names, not the next one along"),
    ("no waivers at all",
     [], [("a.c", "msg one"), ("b.c", "msg two")], 2, 0,
     "the empty suppression file every clean tree has"),
]

# Gate-verdict fixtures (Qodo round-1 finding 2 on PR #1110).
#
# Baseline records are matched by TEXT, so an entry left behind after its
# finding was fixed keeps blessing that exact message if it is reintroduced.
# Drift therefore fails in both directions, as it already does for cppcheck.
# Each case is (name, current, baseline, expected exit code, why).
SELF_TEST_GATE_CASES = [
    ("empty tree, empty baseline", [], [], 0,
     "nothing to report and nothing stale"),
    ("current matches the baseline exactly", ["a", "b"], ["a", "b"], 0,
     "the only clean state"),
    ("a new finding", ["a", "b"], ["a"], 1,
     "the case the gate always failed"),
    ("a fixed finding and nothing new", ["a"], ["a", "b"], 1,
     "the finding: before the fix this printed a suggestion and exited 0, "
     "leaving 'b' in the file to bless its own reintroduction"),
    ("both directions at once", ["a", "c"], ["a", "b"], 1,
     "reported together, one exit code"),
    ("a duplicate record disappeared", ["a"], ["a", "a"], 1,
     "counts matter: two identical messages, one fixed, is still drift"),
    ("a duplicate record appeared", ["a", "a"], ["a"], 1,
     "the same in the other direction"),
]

# ambiguous_annotation(): one bound, several calls on the line -> refuse.
# Each case is (name, src_lines, line, n_calls, expected, why).
SELF_TEST_AMBIGUITY_CASES = [
    ("two calls and one annotation on the line",
     ['LOG_E("a %s", s); LOG_E("%s", big); /* log_budget: max=1 */'], 1, 2,
     True,
     "the audit's case: the bound was applied to BOTH, so the second was "
     "reported clean while it could truncate"),
    ("two calls and NO annotation",
     ['LOG_E("a %s", s); LOG_E("%s", big);'], 1, 2, False,
     "nothing to misattribute -- both are measured unbounded, which is the "
     "stricter reading and already correct"),
    ("one call with its own annotation",
     ['LOG_E("%s", s); /* log_budget: max=1 */'], 1, 1, False,
     "the unambiguous form must keep working -- this is the shape the "
     "annotation feature exists for"),
    ("one call, annotation on the line above",
     ['/* log_budget: max=1 */', 'LOG_E("%s", s);'], 2, 1, False,
     "six of the eight annotations in firmware/src/ have this shape"),
    ("three calls and one annotation",
     ['LOG_E("%s",a); LOG_E("%s",b); LOG_E("%s",c); /* log_budget: max=1 */'],
     1, 3, True, "the count is reported, so the message names the real shape"),
    # ⛔ r4 FINDING 1. This guard checked only the calls' OWN line while
    # find_annotation also honours the comment block ABOVE, so this exact
    # shape was refused by neither and one max=1 bounded both calls.
    ("two calls and the annotation on the line ABOVE",
     ['/* log_budget: max=1 */', 'LOG_E("%s", "x"); LOG_E("%s", big);'], 2, 2,
     True,
     "r4 both legs: a guard whose scope is NARROWER than the finder's passes "
     "exactly the cases that matter. It delegates to find_annotation now, so "
     "the two cannot drift apart again"),
    ("two calls under a MULTI-LINE comment block holding the annotation",
     ['/* why this bound is safe:', ' * log_budget: max=1', ' */',
      'LOG_E("%s", "x"); LOG_E("%s", big);'], 4, 2, True,
     "the house-style shape -- six of the eight real annotations live in a "
     "block like this, so the leak was reachable from the dominant form"),
    ("two calls, annotation-looking text only in a STRING above",
     ['puts("log_budget: max=1");', 'LOG_E("%s", "x"); LOG_E("%s", big);'],
     2, 2, False,
     "a walk-termination guard, NOT a discriminator -- measured against the "
     "pre-fix predicate it passes there too, because the line above is code "
     "so the backward scan stopped before it either way. Kept to pin that "
     "termination; it is not coverage of the finding"),
    ("a line number out of range is not a crash",
     ['LOG_E("%s", s);'], 99, 2, False,
     "pos_to_line and the source list must not be able to raise here -- a "
     "scanner that crashes on a malformed file reports nothing at all"),
]

# find_annotation() must bind an annotation to ITS OWN call and to no other.
# The codex audit on #1110 found it leaking forward: a code line carrying a
# trailing block comment also ends with "*/", so the walk upward from the NEXT
# call accepted it as comment-only and applied the previous call's bound.
#
# BOTH DIRECTIONS ARE TESTED ON PURPOSE. The leak cases are the defect; the
# "must still bind" cases are the regression that a careless fix causes, and
# they are the ones that matter here -- every one of the eight annotations
# shipping in firmware/src/ today is a `/* ... */` or `* ...` comment line, so
# a fix that stopped those binding would silently un-bound every real %s in
# the tree while looking like it had tightened the gate.
#
# Each case is (name, src_lines, line, expected, why).
SELF_TEST_ANNOTATION_CASES = [
    # --- the defect: a bound must NOT reach the following call
    ("trailing comment does not leak forward",
     ['LOG_E("short %s", "x"); /* log_budget: max=1 */',
      'LOG_E("%s", big);'], 2, None,
     "the audit's case: line 1 ends with */ but carries code, so its bound is "
     "line 1's alone. Leaking it reported line 2 as clean while it truncates"),
    ("leading comment on a code line does not leak forward",
     ['/* log_budget: max=1 */ LOG_E("a %s", s);',
      'LOG_E("%s", big);'], 2, None,
     "the blind leg's case, same defect through the startswith('/*') arm"),
    ("a bound two calls back does not reach either",
     ['LOG_E("a %s", s); /* log_budget: max=1 */',
      'LOG_E("b %s", s);',
      'LOG_E("%s", big);'], 3, None,
     "a walk-termination guard, NOT a leak test -- measured: it passes on the "
     "pre-fix predicate too, because the walk already stopped at line 2's "
     "code and never reached the annotation. Kept because it pins that "
     "termination, but it discriminates nothing and must not be counted as "
     "coverage of the leak"),
    # --- ⛔ r4 FINDING 2: the marker must be COMMENT text, never message text
    ("a marker inside the FORMAT STRING is not an annotation",
     ['LOG_E("log_budget: max=1 %s", big);'], 1, None,
     "r4 both legs: there is no comment on this line at all. Searching the "
     "raw line let a message's own text bound it, and the gate blessed a "
     "144-byte message against a ceiling of 125 -- the gate's whole purpose "
     "defeated by text shaped like a marker"),
    ("a marker in a non-format string argument is not an annotation",
     ['LOG_E("%s", "log_budget: max=1");'], 1, None,
     "same defect through an argument rather than the format -- the raw-line "
     "search could not tell the two apart because it never parsed either"),
    ("a REAL comment annotation wins over marker-shaped message text",
     ['LOG_E("log_budget: max=99 %s", big); /* log_budget: max=7 */'], 1, [7],
     "both appear on one line; only the comment is an annotation. Pins WHICH "
     "one is read, not merely that one is"),
    ("a marker in a string on the line above does not bind",
     ['puts("log_budget: max=1");', 'LOG_E("%s", big);'], 2, None,
     "a walk-termination guard, NOT a discriminator -- measured, it passes on "
     "the pre-fix raw-line search too, since that line is code and the walk "
     "stopped before it. Kept because it pins the termination for the SECOND "
     "route into this defect; it is not coverage of the finding"),
    # --- the regression a careless fix causes: these MUST keep binding
    ("comment-only line above still binds",
     ['/* log_budget: max=1 */',
      'LOG_E("%s", s);'], 2, [1],
     "six of the eight annotations in firmware/src/ have this exact shape"),
    ("block-comment continuation still binds",
     ['/* a block comment',
      ' * explaining the bound',
      ' * log_budget: max=40 */',
      'LOG_E("%s", s);'], 4, [40],
     "the other two (sd_card_manager.c) have this shape"),
    ("a block closer carrying no opener still binds",
     ['/* a block comment',
      '   log_budget: max=7 */',
      'LOG_E("%s", s);'], 3, [7],
     "a continuation line without a leading '*' ends with */ and has no "
     "opener, so it cannot carry code -- this is what the endswith('*/') arm "
     "was for, and the fix must keep it"),
    ("a // line above still binds",
     ['// log_budget: max=3',
      'LOG_E("%s", s);'], 2, [3],
     "the line-comment form"),
    ("an annotation on the macro's OWN line still binds",
     ['LOG_E("%s", s); /* log_budget: max=5 */'], 1, [5],
     "read directly from the macro's line, never through the upward walk, so "
     "the fix must not disturb it -- and this is the same text shape as the "
     "first leak case, which is why the two must be tested together"),
    ("multi-width annotations survive",
     ['/* log_budget: max=4,9 */',
      'LOG_E("%s %s", a, b);'], 2, [4, 9],
     "the comma form is parsed by ANNOTATION_RE, not by this walk"),
    ("no annotation anywhere returns None",
     ['LOG_E("%s", s);'], 1, None,
     "the base case: absence must read as absence, not as an empty bound"),

    # --- round 2 of the same finding. The first fix special-cased the
    # block-interior forms BY SHAPE, which re-opened the leak through the
    # `startswith('*')` arm and additionally read a pointer dereference as a
    # comment. Both are here because a shape test cannot tell them apart --
    # only block STATE can, which is why comment_only_map() scans forward.
    ("code AFTER a block close does not leak forward",
     ['/* log_budget: max=1',
      ' */ LOG_E("short %s", "x");',
      'LOG_E("%s", big);'], 3, None,
     "REOPENED FINDING: ' */ LOG_E(...)' closes the block and then carries "
     "code, so it must stop the walk. The shape-based fix accepted it solely "
     "because it starts with '*', and both calls got max=1 while the second "
     "truncated"),
    ("the call on the block-closing line keeps its own bound",
     ['/* log_budget: max=1',
      ' */ LOG_E("short %s", "x");'], 2, [1],
     "the other half of the same shape: that call's annotation IS the block "
     "above it, so binding must still happen -- the fix must not simply "
     "refuse every line containing '*/'"),
    ("a pointer dereference does not pass a bound through",
     ['/* log_budget: max=1 */',
      '*ptr = value;',
      'LOG_E("%s", big);'], 3, None,
     "a second hole in the same arm, which the audit did not name: outside a "
     "block comment a leading '*' is a dereference or multiplication. The "
     "shape test read it as a continuation and walked straight THROUGH it to "
     "the annotation above. The intervening code line must stop the walk -- "
     "measured: this case passes on the shape predicate without the "
     "annotation above it, so the annotation is what makes it discriminate"),
    # (the phantom-block case lives in SELF_TEST_COMMENT_MAP_CASES below:
    #  measured, it cannot leak a bound through find_annotation, because the
    #  line carrying the string is itself code and therefore always stops an
    #  upward walk. Asserting it here would have passed for a reason other
    #  than the one claimed.)
    ("a block spanning several lines above still binds",
     ['/* opening',
      ' * middle',
      ' * log_budget: max=12',
      ' */',
      'LOG_E("%s", s);'], 5, [12],
     "the common real shape, with the closer on its own line"),
]

# comment_only_map() is a named predicate in its own right, so its contract is
# pinned directly rather than only through find_annotation(). These cases exist
# because two of find_annotation()'s cases turned out to pass for reasons other
# than the ones claimed -- a map that is wrong is wrong whether or not today's
# single caller happens to expose it.
# Each case is (name, src_lines, expected map, why).
SELF_TEST_COMMENT_MAP_CASES = [
    ("a '/*' inside a string does not open a block",
     ['LOG_E("a /* b");', 'int x = 1;'], [False, False],
     "without skipping string literals the '/*' in the message opens a block "
     "that never closes, and the NEXT line -- ordinary code -- is then "
     "classified as comment text. find_annotation cannot expose this (the "
     "line holding the string is itself code, so it always stops an upward "
     "walk) which is exactly why it is pinned here instead"),
    ("a character literal holding a quote does not open a string",
     ["char q = '\"'; int y = 2;"], [False],
     "the quote inside a char literal must not start a string that swallows "
     "the rest of the line"),
    ("state carries across lines",
     ['/* a', 'b', 'c */', 'code();'], [True, True, True, False],
     "the interior lines carry no markers at all and are comment only by "
     "STATE -- the property a per-line shape test cannot have"),
    ("code before and after an inline block",
     ['f(); /* x */ g();'], [False],
     "both halves are code; removing the complete span must not empty it"),
    ("a line comment after code",
     ['f(); // note'], [False],
     "the trailing // does not make the line a comment"),
    ("a bare closer is comment",
     ['/* a', ' */'], [True, True],
     "the closer carries nothing else"),
]


def self_test():
    """Check extraction and measurement against synthetic fixtures.

    Synthetic, not real firmware files: a fixture that reads the tree would
    change its own expected answer every time someone edits a log message, and
    a self-test that has to be updated to stay green stops being evidence.
    """
    import tempfile
    failures = 0
    with tempfile.TemporaryDirectory() as d:
        for i, (name, body, expected, why) in enumerate(SELF_TEST_CASES):
            p = Path(d) / f"case{i}.c"
            p.write_text("void f(void) {\n" + body + "\n}\n", encoding="utf-8")
            try:
                got = scan_file(p, f"case{i}.c", DEFAULT_MACROS,
                                SELF_TEST_CEILING)
            except Exception as e:                       # noqa: BLE001
                print(f"  FAIL [{name}]: raised {e!r} -- {why}")
                failures += 1
                continue
            if len(got) != expected:
                failures += 1
                print(f"  FAIL [{name}]: {len(got)} finding(s), expected "
                      f"{expected} -- {why}")
                for f in got:
                    print(f"        got: {f.line}: {f.reason}")

        # #1110 r8 review LOW: this count is DERIVED, not carried. It was a
        # hardcoded '11/11' that the two cases added this round silently
        # invalidated -- the same stale-ordinal defect the audit flagged in the
        # Makefile. Incremented BEFORE each attempt, so the cases that expect a
        # ToolError are counted too.
        ceiling_checks = 0
        # ⛔ #1110 r9: THE ONE LINE MODEL, PINNED STRUCTURALLY.
        #
        # A behavioural case for this proved inert -- I could not construct a
        # source where the splitlines()/count(chr(10)) disagreement changed a
        # reported finding, so I am not claiming one. The CLASS rule is what
        # matters and it is checkable directly: no code in this module may call
        # splitlines(), because its line LIST would then disagree with the line
        # NUMBERING every other part derives from count(chr(10)).
        #
        # Via ast, not grep: this file's own prose says "str.splitlines()" in
        # several places, and a text search cannot tell a comment from a call.
        try:
            import ast as _ast
            _tree = _ast.parse(Path(__file__).read_text(encoding="utf-8"))
            _bad = [n.lineno for n in _ast.walk(_tree)
                    if isinstance(n, _ast.Attribute) and n.attr == "splitlines"]
            if _bad:
                failures += 1
                print(f"  FAIL [r9: one line model]: splitlines() is CALLED at "
                      f"line(s) {_bad} -- it splits on \\v, \\f, \\x1c-\\x1e and "
                      f"U+2028 as well as newline, so the line list stops "
                      f"agreeing with the count(chr(10)) numbering every line "
                      f"number here comes from. Use split_lines_1model().")
        except OSError:
            pass          # running from a zip/frozen build: skip, do not fail

        # ⛔ #1110 r9: every REJECTION in the declared subset, one case each.
        for name, body, want, why in SELF_TEST_REJECTION_CASES:
            rp = Path(d) / f"rej{abs(hash(name)) % 100000}.c"
            rp.write_text("void f(void) {\n" + body + "\n}\n", encoding="utf-8")
            try:
                scan_file(rp, rp.name, DEFAULT_MACROS, SELF_TEST_CEILING)
            except ToolError as e:
                if want not in str(e):
                    failures += 1
                    print(f"  FAIL [{name}]: refused, but not for the stated "
                          f"reason -- wanted {want!r}, got {str(e)!r}")
            except Exception as e:                       # noqa: BLE001
                failures += 1
                print(f"  FAIL [{name}]: raised {e!r}, expected a ToolError "
                      f"containing {want!r} -- {why}")
            else:
                failures += 1
                print(f"  FAIL [{name}]: did NOT refuse -- {why}")

        # ⛔ #1110 r9: ONE LINE MODEL. splitlines() splits on \v, \f, \x1c-\x1e
        # and U+2028 as well as \n, while every line NUMBER in this tool comes
        # from count("\n"). The two therefore disagree the moment a message
        # contains one of those bytes, and an annotation binds to the wrong
        # call. Nothing may split lines any other way.
        ff_src = ('void f(void) {\n'
                  'LOG_E("a\x0bb");  /* log_budget: max=1 */\n'
                  'LOG_E("%s", s);\n'
                  '}\n')
        fp = Path(d) / "formfeed.c"
        fp.write_text(ff_src, encoding="utf-8")
        ff = scan_file(fp, "formfeed.c", DEFAULT_MACROS, SELF_TEST_CEILING)
        ff_lines = sorted(x.line for x in ff)
        if ff_lines != [3]:
            failures += 1
            print(f"  FAIL [r9: one line model]: findings on lines {ff_lines}, "
                  f"expected exactly [3] -- a vertical tab in a message made "
                  f"splitlines() and the count(chr(10)) numbering disagree, so the "
                  f"annotation bound to the wrong call")

        # ⛔ #1110 r9: a non-printable byte must not be able to SPLIT a baseline
        # record. The serializer escapes every one of them and the loader splits
        # on newline only.
        for bad_byte, lbl in ((chr(11), "VT"), (chr(12), "FF"), (chr(0), "NUL")):
            rendered = escape_for_record("a" + bad_byte + "b")
            if "\n" in rendered or bad_byte in rendered:
                failures += 1
                print(f"  FAIL [r9: record escaping {lbl}]: rendered {rendered!r} "
                      f"still carries a raw control byte -- it would split the "
                      f"baseline record it is written into")

        # The ceiling must come from source. Pin both that it is derived
        # correctly from the real reservations and that a shape it cannot read
        # fails loudly instead of falling back to a remembered 125.
        h = "#define LOG_MESSAGE_SIZE 128\n"
        c = ("static int LogMessageFormatImpl(const char* format, va_list args)"
             "\n{\n    size = vsnprintf(buffer, LOG_MESSAGE_SIZE - 2, format, "
             "args);\n    size = min((LOG_MESSAGE_SIZE - 3), size);\n}\n")
        # ⛔ r8/1: A COMMENT CONTINUED BY A BACKSLASH STILL COMMENTS THE NEXT
        # LINE. C splices backslash-newline in phase 2, STRICTLY BEFORE
        # comments are recognised in phase 3 -- splice_continuations()' own
        # docstring says so and says every later stage must read its output.
        # read_ceiling() was the one caller that masked RAW text, so the
        # continuation line read as live code and its clamp set the ceiling.
        cont_c = ("static int LogMessageFormatImpl(const char* f, va_list a)\n"
                  "{\n    size = vsnprintf(buffer, LOG_MESSAGE_SIZE - 2, f, a);"
                  "\n    // historical, superseded: \\\n"
                  "    size = min((LOG_MESSAGE_SIZE - 13), size);\n"
                  "    size = min((LOG_MESSAGE_SIZE - 3), size);\n}\n")
        ceiling_checks += 1
        got, _, _, r_cl = read_ceiling(h, cont_c)
        if (got, r_cl) != (125, 3):
            failures += 1
            print(f"  FAIL [ceiling: continued comment]: derived {got} with "
                  f"clamp reserve {r_cl}, expected 125 with 3 -- a COMMENTED "
                  f"OUT clamp set the ceiling every message is measured "
                  f"against")

        # ⛔ r8/2: C INTEGER NOTATION. int('0200') is 200 in Python and 128 in
        # C, and 0x80 is 128 -- a decimal-only read makes the ceiling too
        # GENEROUS, which under-reports.
        for lit, want in (("0200", 125), ("0x80", 125), ("128", 125)):
            ceiling_checks += 1
            got, sz, _, _ = read_ceiling(f"#define LOG_MESSAGE_SIZE {lit}\n", c)
            if (got, sz) != (want, 128):
                failures += 1
                print(f"  FAIL [ceiling: C integer {lit}]: derived ceiling "
                      f"{got} from size {sz}, expected {want} from 128 -- C "
                      f"integer notation was not honoured")

        ceiling_checks += 1
        ceiling, _, _, _ = read_ceiling(h, c)
        if ceiling != 125:
            failures += 1
            print(f"  FAIL [ceiling]: derived {ceiling}, expected 125 from "
                  f"LOG_MESSAGE_SIZE 128 with reservations 2 and 3")
        for label, bad_h, bad_c in (
                ("no LOG_MESSAGE_SIZE", "#define SOMETHING_ELSE 128\n", c),
                ("no vsnprintf reservation", h,
                 "static int LogMessageFormatImpl(const char* format, va_list a"
                 ")\n{\n    size = snprintf_other(buffer, 99);\n    size = min("
                 "(LOG_MESSAGE_SIZE - 3), size);\n}\n"),
                ("no clamp", h,
                 "static int LogMessageFormatImpl(const char* format, va_list a"
                 ")\n{\n    size = vsnprintf(buffer, LOG_MESSAGE_SIZE - 2, f, a"
                 ");\n}\n")):
            try:
                ceiling_checks += 1
                read_ceiling(bad_h, bad_c)
            except ToolError:
                continue
            failures += 1
            print(f"  FAIL [ceiling]: '{label}' did not raise -- the tool would "
                  f"gate against an assumed ceiling")

        # MULTI-TERM reservations. The codex audit on #1110 found the old
        # regexes captured only the FIRST integer, so `LOG_MESSAGE_SIZE - 2 -
        # 10` read as a reservation of 2 and derived 125 where the truth is
        # 115 -- the gate then exited 0 while a 120-byte message truncated.
        # The ceiling is the quantity every other check is expressed in, so
        # this is not one wrong answer, it is every answer being about the
        # wrong thing.
        multi_c = ("static int LogMessageFormatImpl(const char* format, va_lis"
                   "t args)\n{\n    size = vsnprintf(buffer, LOG_MESSAGE_SIZE "
                   "- 2 - 10, format, args);\n    size = min((LOG_MESSAGE_SIZE"
                   " - 3 - 10), size);\n}\n")
        ceiling_checks += 1
        got, _, r_vsn, r_clamp = read_ceiling(h, multi_c)
        if (got, r_vsn, r_clamp) != (115, 12, 13):
            failures += 1
            print(f"  FAIL [ceiling: multi-term]: derived ceiling {got} with "
                  f"reservations {r_vsn}/{r_clamp}, expected 115 with 12/13. "
                  f"A prefix match reads '- 2 - 10' as 2 and mis-sets the "
                  f"number every other measurement is made against")

        # ARITHMETIC AFTER A CLOSING PAREN. Round 2's fix captured
        # `LOG_MESSAGE_SIZE([^,)]*)`, which stops AT the paren -- so
        # `min((LOG_MESSAGE_SIZE - 3) - 10, size)` read as a reservation of 3
        # and derived 125 where the truth is 115. The first fix took a prefix
        # of the integer, the second took a prefix of the expression; both
        # were prefixes. reserve_after() walks the argument by paren depth
        # instead (codex audit on #1110, round 3, both legs).
        paren_c = ("static int LogMessageFormatImpl(const char* f, va_list a)"
                   "\n{\n    size = vsnprintf(buffer, LOG_MESSAGE_SIZE - 2, f,"
                   " a);\n    size = min((LOG_MESSAGE_SIZE - 3) - 10, size);"
                   "\n}\n")
        ceiling_checks += 1
        got, _, r_v, r_c = read_ceiling(h, paren_c)
        if (got, r_v, r_c) != (115, 2, 13):
            failures += 1
            print(f"  FAIL [ceiling: after a paren]: derived {got} with "
                  f"{r_v}/{r_c}, expected 115 with 2/13 -- `(LMS - 3) - 10` "
                  f"reserves 13, and stopping at the ')' reads 3")

        # A COMMENT MUST NOT SET THE CEILING. Found by the blind leg: the
        # regexes searched raw Logger.c, so a commented-out or historical line
        # won the search. Measured: a comment saying `- 99` produced 28.
        cmt_c = ("static int LogMessageFormatImpl(const char* f, va_list a)\n"
                 "{\n    /* was vsnprintf(buffer, LOG_MESSAGE_SIZE - 99, f, a)"
                 "; */\n    size = vsnprintf(buffer, LOG_MESSAGE_SIZE - 2, f, "
                 "a);\n    size = min((LOG_MESSAGE_SIZE - 3), size);\n}\n")
        ceiling_checks += 1
        got, _, r_v, _ = read_ceiling(h, cmt_c)
        if (got, r_v) != (125, 2):
            failures += 1
            print(f"  FAIL [ceiling: comment wins]: derived {got} with "
                  f"vsnprintf reserve {r_v}, expected 125 with 2 -- a comment "
                  f"set the number every message is measured against")

        # A PAREN AFTER A SUBTRACTION MUST REFUSE, NOT FLATTEN. `A - (b - c)`
        # is NOT `A - b - c` -- the sign on c inverts -- so a reader that
        # strips parens to cope with `(A - b) - c` would silently mis-derive
        # this one. Refusing is the only safe reading.
        neg_c = ("static int LogMessageFormatImpl(const char* f, va_list a)\n"
                 "{\n    size = vsnprintf(buffer, LOG_MESSAGE_SIZE - (3 - 10),"
                 " f, a);\n    size = min((LOG_MESSAGE_SIZE - 3), size);\n}\n")
        try:
            ceiling_checks += 1
            read_ceiling(h, neg_c)
            failures += 1
            print("  FAIL [ceiling: paren after a minus]: did not raise -- "
                  "`A - (3 - 10)` reserves -7, not 13, and flattening the "
                  "parens inverts the sign")
        except ToolError:
            pass

        # ... and an expression this tool cannot account for must RAISE, not
        # contribute whatever part of it happens to be recognisable.
        for label, expr_c in (
                ("a macro term",
                 "    size = vsnprintf(buffer, LOG_MESSAGE_SIZE - PAD, f, a);"
                 "\n    size = min((LOG_MESSAGE_SIZE - 3), size);\n"),
                ("an addition",
                 "    size = vsnprintf(buffer, LOG_MESSAGE_SIZE - 2 + 1, f, a)"
                 ";\n    size = min((LOG_MESSAGE_SIZE - 3), size);\n"),
                ("a multiplication",
                 "    size = vsnprintf(buffer, LOG_MESSAGE_SIZE - 2*2, f, a);"
                 "\n    size = min((LOG_MESSAGE_SIZE - 3), size);\n")):
            bad = ("static int LogMessageFormatImpl(const char* f, va_list a)"
                   "\n{\n" + expr_c + "}\n")
            try:
                ceiling_checks += 1
                read_ceiling(h, bad)
            except ToolError:
                continue
            failures += 1
            print(f"  FAIL [ceiling: {label}]: did not raise -- an expression "
                  f"the tool cannot read must stop it, not be silently "
                  f"truncated to the part it recognises")

        # The suppression file is checked through load_suppressions() itself,
        # on a real file on disk, so these cases run the same code path the
        # gate does rather than a re-implementation of it.
        #
        # Counted with sum(values()), not len(): the number of WAIVERS is the
        # quantity apply_suppressions() spends, and it is the one that differs
        # from the pre-fix set behaviour when two entries share a key.
        for i, (name, text, expected, why) in enumerate(
                SELF_TEST_SUPPRESSION_CASES):
            p = Path(d) / f"suppress{i}.txt"
            p.write_text(text, encoding="utf-8")
            try:
                got = sum(load_suppressions(p).values())
            except ToolError:
                if expected is not None:
                    failures += 1
                    print(f"  FAIL [suppress: {name}]: raised ToolError, "
                          f"expected {expected} entr(ies) -- {why}")
                continue
            if expected is None:
                failures += 1
                print(f"  FAIL [suppress: {name}]: accepted {got} entr(ies), "
                      f"expected a ToolError -- {why}")
            elif got != expected:
                failures += 1
                print(f"  FAIL [suppress: {name}]: accepted {got} entr(ies), "
                      f"expected {expected} -- {why}")

    # Waiver spending, pinned on the same function main() filters with.
    for (name, waivers, sites, want_kept, want_suppressed,
            why) in SELF_TEST_WAIVER_CASES:
        findings = [Finding(path, n + 1, "worst case 999 bytes > ceiling 125",
                            fmt) for n, (path, fmt) in enumerate(sites)]
        kept, suppressed = apply_suppressions(findings,
                                              collections.Counter(waivers))
        if len(kept) != want_kept or suppressed != want_suppressed:
            failures += 1
            print(f"  FAIL [waiver: {name}]: kept {len(kept)}/suppressed "
                  f"{suppressed}, expected {want_kept}/{want_suppressed} "
                  f"-- {why}")

    # ambiguous_annotation(): a bound with several candidate calls is refused.
    for name, src, line, n, expected, why in SELF_TEST_AMBIGUITY_CASES:
        got = ambiguous_annotation(src, line, n)
        if got != expected:
            failures += 1
            print(f"  FAIL [ambiguity: {name}]: {got}, expected {expected} "
                  f"-- {why}")

    # comment_only_map(): the predicate find_annotation's walk rests on.
    for name, src, expected, why in SELF_TEST_COMMENT_MAP_CASES:
        got = comment_only_map(src)
        if got != expected:
            failures += 1
            print(f"  FAIL [comment-map: {name}]: {got}, expected {expected} "
                  f"-- {why}")

    # find_annotation(): a bound binds to its own call and to no other.
    for name, src, line, expected, why in SELF_TEST_ANNOTATION_CASES:
        got = find_annotation(src, line)
        if got != expected:
            failures += 1
            print(f"  FAIL [annotation: {name}]: find_annotation -> {got}, "
                  f"expected {expected} -- {why}")

    # The gate's verdict, pinned on the same function main() takes its exit
    # code from.
    for name, cur, base, expected, why in SELF_TEST_GATE_CASES:
        _, _, status = compare_to_baseline(collections.Counter(cur),
                                           collections.Counter(base))
        if status != expected:
            failures += 1
            print(f"  FAIL [gate: {name}]: exit {status}, expected {expected} "
                  f"-- {why}")

    # gather_inputs(): an unreadable input during the scan must raise, so
    # main()'s try/except turns it into the documented exit 2 rather than an
    # uncaught traceback (Qodo round-2 finding on PR #1110: "Report
    # unreadable inputs consistently"). A directory named `*.c` makes
    # `Path.read_text()` raise `IsADirectoryError` (an OSError subclass)
    # unconditionally -- unlike `chmod 0`, this also fails as root, which is
    # how most CI runners execute, so the fixture is reliable there too.
    # Driven through `gather_inputs()` itself (the function main()'s try
    # block now calls), not a hand re-implementation of its control flow.
    with tempfile.TemporaryDirectory() as d:
        root = Path(d) / "src"
        root.mkdir()
        (root / "unreadable.c").mkdir()          # a directory, not a file
        logger_h = Path(d) / "Logger.h"
        logger_c = Path(d) / "Logger.c"
        logger_h.write_text("#define LOG_MESSAGE_SIZE 128\n", encoding="utf-8")
        logger_c.write_text(
            "static int LogMessageFormatImpl(const char* format, va_list a)"
            "\n{\n    size = vsnprintf(buffer, LOG_MESSAGE_SIZE - 2, f, a);\n"
            "    size = min((LOG_MESSAGE_SIZE - 3), size);\n}\n",
            encoding="utf-8")
        try:
            gather_inputs(root, logger_h, logger_c,
                          Path(d) / "no-such-suppress.txt",
                          Path(d) / "no-such-baseline.txt", DEFAULT_MACROS)
            failures += 1
            print("  FAIL [gather_inputs: unreadable scan input]: did not "
                  "raise -- an IsADirectoryError from scan() must not be "
                  "swallowed")
        except ToolError:
            failures += 1
            print("  FAIL [gather_inputs: unreadable scan input]: raised "
                  "ToolError instead of letting the real OSError through -- "
                  "main()'s except clause already catches both, so this "
                  "would still reach exit 2, but it means the wrong "
                  "exception type was actually raised")
        except OSError:
            pass                                  # the expected outcome

    if failures:
        print(f"\n::error::log_budget: {failures} self-test(s) failed")
        return 1
    print(f"log_budget self-test: {len(SELF_TEST_CASES)}/"
          f"{len(SELF_TEST_CASES)} extraction cases + "
          f"{ceiling_checks}/{ceiling_checks} ceiling cases + "
          f"{len(SELF_TEST_SUPPRESSION_CASES)}/"
          f"{len(SELF_TEST_SUPPRESSION_CASES)} suppression cases + "
          f"{len(SELF_TEST_WAIVER_CASES)}/{len(SELF_TEST_WAIVER_CASES)} "
          f"waiver cases + "
          f"{len(SELF_TEST_GATE_CASES)}/{len(SELF_TEST_GATE_CASES)} gate "
          f"cases + {len(SELF_TEST_COMMENT_MAP_CASES)}/"
          f"{len(SELF_TEST_COMMENT_MAP_CASES)} comment-map cases + "
          f"{len(SELF_TEST_AMBIGUITY_CASES)}/"
          f"{len(SELF_TEST_AMBIGUITY_CASES)} ambiguity cases + "
          f"{len(SELF_TEST_ANNOTATION_CASES)}/"
          f"{len(SELF_TEST_ANNOTATION_CASES)} annotation-binding cases + "
          f"1/1 IO-error case pass")
    return 0


def gather_inputs(root, logger_h, logger_c, suppress, baseline, macros):
    """Read every filesystem input the gate needs, or raise.

    Everything that can fail on I/O lives in this ONE function, called from
    inside main()'s single try/except, so a `ToolError` or an `OSError` from
    ANY of these five reads -- the ceiling, the suppression file, the source
    scan, or the baseline -- reaches the same "the tool could not run, exit 2"
    path instead of one of them escaping as an uncaught traceback. Before
    this existed, `scan()` and `load_baseline()` were called AFTER the try
    block, so a permissions change, a symlink loop, or a file deleted
    mid-scan on either one propagated as a raw Python traceback rather than
    the documented exit 2 -- indistinguishable in a CI log from a crash in
    the tool's own logic, and not the "never confused with clean" the module
    docstring promises (Qodo round-2 finding on PR #1110: "Report unreadable
    inputs consistently"). `--list`/`--write-baseline` load the baseline too,
    even though neither gates on it, so both get the same guard for free.

    Extracted to its own function (rather than inlined in main()'s try block,
    which is where this logic lived before) so the self-test can drive this
    exact path directly with a hand-built broken input, without going through
    argparse or main()'s own self-test-of-itself guard -- calling `main()`
    from inside `self_test()` recurses into that guard.

    Returns (ceiling, size, vsn_reserve, clamp_reserve, suppressions,
    findings, scanned, baseline_counter).
    """
    if not root.exists():
        raise ToolError(f"root not found: {root}")
    ceiling, size, r1, r2 = read_ceiling(
        logger_h.read_text(encoding="utf-8"),
        logger_c.read_text(encoding="utf-8"))
    suppressions = load_suppressions(suppress)
    findings, scanned = scan(root, macros, ceiling)
    baseline_counter = load_baseline(baseline)
    return (ceiling, size, r1, r2, suppressions, findings, scanned,
            baseline_counter)


# ---------------------------------------------------------------------------
def main(argv=None):
    """argv defaults to sys.argv[1:] via argparse; the self-test passes an
    explicit list so it can drive this function directly (a real CLI
    invocation, not a re-implementation of its control flow) without
    touching the process's actual command line."""
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--root", type=Path, default=DEFAULT_ROOT)
    ap.add_argument("--logger-h", type=Path, default=DEFAULT_LOGGER_H)
    ap.add_argument("--logger-c", type=Path, default=DEFAULT_LOGGER_C)
    ap.add_argument("--baseline", type=Path, default=DEFAULT_BASELINE)
    ap.add_argument("--suppress", type=Path, default=DEFAULT_SUPPRESS)
    ap.add_argument("--self-test", action="store_true",
                    help="check extraction and measurement, then exit")
    ap.add_argument("--list", action="store_true",
                    help="print every current finding and exit 0")
    ap.add_argument("--write-baseline", action="store_true",
                    help="regenerate the committed baseline from this tree")
    ap.add_argument("--macros", default="error", choices=["error", "all"],
                    help="which log macros to measure (default: the LOG_E "
                         "family, which is what #1039 gates)")
    args = ap.parse_args(argv)

    if args.self_test:
        return self_test()

    # A broken extractor makes every verdict below junk -- same guard
    # scpi_wiki_sync.py uses before it trusts its own matcher.
    if self_test() != 0:
        return 1

    macros = dict(DEFAULT_MACROS)
    if args.macros == "all":
        macros.update(OPTIONAL_MACROS)

    try:
        (ceiling, size, r1, r2, suppressions, findings, scanned,
         baseline) = gather_inputs(args.root, args.logger_h, args.logger_c,
                                   args.suppress, args.baseline, macros)
    except (ToolError, OSError) as e:
        print(f"::error::log_budget: {e}", file=sys.stderr)
        return 2

    print(f"log_budget: ceiling {ceiling} bytes "
          f"(LOG_MESSAGE_SIZE {size}, vsnprintf reserves {r1} incl. its NUL, "
          f"clamp reserves {r2}) - read from "
          f"{args.logger_h} / {args.logger_c}")

    kept, suppressed = apply_suppressions(findings, suppressions)
    kept.sort(key=lambda f: (f.path, f.line, f.reason))

    if args.list:
        for f in kept:
            print(f"{f.path}:{f.line}: {f.reason}")
        print(f"\n{len(kept)} finding(s) in {scanned} file(s), "
              f"{suppressed} suppressed")
        return 0

    if args.write_baseline:
        lines = sorted(record(f) for f in kept)
        header = (
            "# log_budget baseline - GENERATED, do not hand-edit.\n"
            "#\n"
            "# Regenerate after fixing or annotating a site:\n"
            "#     python3 tools/lint/log_budget.py --write-baseline\n"
            "# and commit the result.\n"
            "#\n"
            "# CI FAILS IN BOTH DIRECTIONS (#1110 r9 review LOW -- this header\n"
            "# used to say 'only on findings NOT listed here', which is half the\n"
            "# rule and the more dangerous half to omit):\n"
            "#   * a finding NOT listed here  -> new problem, fix or annotate it\n"
            "#   * an entry here NOT reported -> STALE, and it still matches by\n"
            "#     TEXT, so leaving it re-blesses that exact message in that file\n"
            "#     if anyone ever reintroduces it. Regenerate.\n"
            "#\n"
            "# So this file stops the count growing AND must shrink as findings\n"
            "# are fixed; it does not bless what is in it. Shrinking is the point.\n"
            "#\n"
            "# Format: <file> :: <reason> :: <format string, one line>\n"
            "# No line number: a LOG_E's line moves whenever anything above it\n"
            "# is edited, and a line-keyed baseline this size would go red on\n"
            "# changes that touched no log message at all.\n"
            "#\n"
            "# Permanent waivers with a stated reason go in\n"
            "# tools/lint/log_budget-suppress.txt instead.\n")
        args.baseline.write_text(header + "".join(ln + "\n" for ln in lines),
                                 encoding="utf-8")
        print(f"Wrote {len(lines)} finding(s) to {args.baseline}")
        return 0

    current = collections.Counter(record(f) for f in kept)
    new, fixed, status = compare_to_baseline(current, baseline)

    if status == 0:
        # The SCOPE rides with the verdict. "clean" otherwise reads as "no
        # LOG_E in this tree can truncate", which is not what was measured --
        # headers are not visited, and one of them holds a 165-byte message.
        print(f"log_budget: clean - {len(kept)} finding(s) in {scanned} "
              f"file(s), all in the baseline ({suppressed} suppressed); "
              f"scope: {' '.join(SCANNED_SUFFIXES)} only -- LOG_E in headers "
              f"is NOT measured (see SCANNED_SUFFIXES)")
        return 0

    by_record = {}
    for f in kept:
        by_record.setdefault(record(f), f)

    if new:
        print(f"\n::error::log_budget: {sum(new.values())} new finding(s) not "
              f"in {args.baseline}\n")
        for ln in sorted(new):
            f = by_record[ln]
            print(f"::error file={f.path},line={f.line}::{f.reason}  --  "
                  f"\"{f.fmt}\"")
        print(f"\nLogger.c truncates a formatted message at {ceiling} bytes "
              f"with no error and no marker, and the remedy is written last, "
              f"so the remedy is what is lost. Either:")
        print("  - shorten the message (keep the remedy; see #1025 / #1039 for "
              "the comment convention), or")
        print("  - bound a %s with /* log_budget: max=N */ above the call, or")
        print("  - add a permanent waiver WITH a reason to "
              "tools/lint/log_budget-suppress.txt")

    if fixed:
        # A FAILURE, not a suggestion. See compare_to_baseline(): a stale
        # record silently re-blesses the same text if it comes back.
        print(f"\n::error::log_budget: {sum(fixed.values())} baseline "
              f"entr(ies) in {args.baseline} are no longer reported\n")
        for ln in sorted(fixed):
            print(f"::error file={args.baseline}::stale baseline entry, no "
                  f"longer reported  --  {ln}")
        print("\nA baseline record is matched by text, not by history, so an "
              "entry left in the file keeps blessing that exact message if it "
              "is ever reintroduced. Regenerate so the gate keeps the ground "
              "it gained:")
        print("    python3 tools/lint/log_budget.py --write-baseline")

    return status


if __name__ == "__main__":
    sys.exit(main())
