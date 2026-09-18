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

ANNOTATION_RE = re.compile(r"log_budget:\s*max\s*=\s*([0-9]+(?:\s*,\s*[0-9]+)*)")

SEP = " :: "


# ---------------------------------------------------------------------------
# Reading the ceiling out of the firmware, never hardcoding it
# ---------------------------------------------------------------------------
class ToolError(Exception):
    """The tool cannot produce a trustworthy verdict. Always exit 2, never 0."""


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
    m = re.search(r"^\s*#define\s+LOG_MESSAGE_SIZE\s+(\d+)\s*$",
                  logger_h_text, re.M)
    if not m:
        raise ToolError(
            "could not find '#define LOG_MESSAGE_SIZE <n>' in Logger.h -- the "
            "ceiling is read from source and must not be assumed")
    size = int(m.group(1))

    body = re.search(
        r"LogMessageFormatImpl\s*\([^)]*\)\s*\{(.*?)\n\}", logger_c_text, re.S)
    if not body:
        raise ToolError(
            "could not find the body of LogMessageFormatImpl in Logger.c -- "
            "the buffer reservations are read from it and must not be assumed")
    impl = body.group(1)

    m1 = re.search(r"vsnprintf\s*\(\s*\w+\s*,\s*LOG_MESSAGE_SIZE\s*-\s*(\d+)",
                   impl)
    if not m1:
        raise ToolError(
            "LogMessageFormatImpl no longer calls "
            "vsnprintf(buf, LOG_MESSAGE_SIZE - N, ...) -- re-derive the "
            "ceiling by hand and update read_ceiling()")
    m2 = re.search(r"min\s*\(\s*\(?\s*LOG_MESSAGE_SIZE\s*-\s*(\d+)", impl)
    if not m2:
        raise ToolError(
            "LogMessageFormatImpl no longer clamps with "
            "min((LOG_MESSAGE_SIZE - N), ...) -- re-derive the ceiling by hand "
            "and update read_ceiling()")

    vsn_reserve, clamp_reserve = int(m1.group(1)), int(m2.group(1))
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
            continue                      # unbalanced: nothing trustworthy here
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


def find_annotation(src_lines, line):
    """Return the widths declared by a /* log_budget: max=N[,N...] */ marker.

    Searched on the macro's own line and across the contiguous run of comment
    lines immediately above it, because the house style for this class of fix
    puts a multi-line block comment there and an annotation naturally belongs
    inside it.
    """
    idx = line - 1
    texts = [src_lines[idx]] if 0 <= idx < len(src_lines) else []
    k = idx - 1
    while k >= 0:
        stripped = src_lines[k].strip()
        if (stripped.startswith("//") or stripped.startswith("/*")
                or stripped.startswith("*") or stripped.endswith("*/")):
            texts.append(src_lines[k])
            k -= 1
            continue
        break
    for text in texts:
        m = ANNOTATION_RE.search(text)
        if m:
            return [int(x) for x in m.group(1).split(",")]
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


def escape_for_record(text):
    """One-line, round-trippable rendering of a format string for the baseline."""
    return (text.replace("\\", "\\\\").replace("\r", "\\r")
                .replace("\n", "\\n").replace("\t", "\\t"))


def scan_file(path, rel, macros, ceiling):
    src = path.read_text(encoding="utf-8", errors="surrogateescape")
    src_lines = src.splitlines()
    spliced, orig_pos = splice_continuations(src)
    masked = mask_comments(spliced)
    found = []
    for macro, pos, args, _call in find_calls(masked, macros):
        line = pos_to_line(src, orig_pos, pos)
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
        worst, reason = measure(fmt, find_annotation(src_lines, line))
        if reason is not None:
            found.append(Finding(rel, line, reason, escape_for_record(fmt)))
        elif worst > ceiling:
            found.append(Finding(
                rel, line, f"worst case {worst} bytes > ceiling {ceiling}",
                escape_for_record(fmt)))
    return found


def scan(root, macros, ceiling):
    findings, scanned = [], 0
    for path in sorted(root.rglob("*.c")):
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
             path.read_text(encoding="utf-8").splitlines()]
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
            path.read_text(encoding="utf-8").splitlines(), 1):
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

SELF_TEST_CASES = [
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

        # The ceiling must come from source. Pin both that it is derived
        # correctly from the real reservations and that a shape it cannot read
        # fails loudly instead of falling back to a remembered 125.
        h = "#define LOG_MESSAGE_SIZE 128\n"
        c = ("static int LogMessageFormatImpl(const char* format, va_list args)"
             "\n{\n    size = vsnprintf(buffer, LOG_MESSAGE_SIZE - 2, format, "
             "args);\n    size = min((LOG_MESSAGE_SIZE - 3), size);\n}\n")
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
                read_ceiling(bad_h, bad_c)
            except ToolError:
                continue
            failures += 1
            print(f"  FAIL [ceiling]: '{label}' did not raise -- the tool would "
                  f"gate against an assumed ceiling")

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
          f"{len(SELF_TEST_CASES)} extraction cases + 4/4 ceiling cases + "
          f"{len(SELF_TEST_SUPPRESSION_CASES)}/"
          f"{len(SELF_TEST_SUPPRESSION_CASES)} suppression cases + "
          f"{len(SELF_TEST_WAIVER_CASES)}/{len(SELF_TEST_WAIVER_CASES)} "
          f"waiver cases + "
          f"{len(SELF_TEST_GATE_CASES)}/{len(SELF_TEST_GATE_CASES)} gate "
          f"cases + 1/1 IO-error case pass")
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
            "# and commit the result. CI fails only on findings NOT listed\n"
            "# here, so this file stops the count growing; it does not bless\n"
            "# what is in it. Shrinking it is the point.\n"
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
        print(f"log_budget: clean - {len(kept)} finding(s) in {scanned} "
              f"file(s), all in the baseline ({suppressed} suppressed)")
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
