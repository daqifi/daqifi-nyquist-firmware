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
    not four) plus, for each conversion specifier, the widest that specifier
    could print. If that total exceeds the ceiling the site is a finding.

    A message is also a finding when its worst case cannot be BOUNDED at all --
    an unannotated `%s`, a `*` width, a specifier with no entry in the width
    table, or a format argument that is not a string literal. Those are not
    skipped and not assumed to be zero: "we could not measure it" is reported
    with the same severity as "we measured it and it is too long", because the
    failure mode this gate exists to stop is exactly the silent one.

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
    0 = no findings outside the baseline
    1 = new findings (or a failed self-test)
    2 = the tool could not run (missing file, unreadable Logger.c, bad
        suppression file) -- never confused with "clean"
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
#   c       1  one character.
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
    "c": 1, "lc": 1,
}

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


def find_calls(masked, macros):
    """Yield (macro, line, arg_list, call_text) for each log-macro call."""
    names = "|".join(sorted(macros, key=len, reverse=True))
    call_re = re.compile(r"(?<![A-Za-z0-9_])(" + names + r")\s*\(")
    for m in call_re.finditer(masked):
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
        line = masked.count("\n", 0, m.start()) + 1
        yield m.group(1), line, split_args(inner), masked[m.start():i + 1]


LITERAL_RE = re.compile(r'"((?:[^"\\]|\\.)*)"', re.S)


def literal_text(arg):
    """Decode an argument made only of adjacent string literals, else None.

    C concatenates adjacent literals, so a format string split over five
    physical lines is one message -- the common shape in this codebase. Anything
    else in the argument (an identifier, a ternary, a macro) means the format is
    not statically knowable, and None tells the caller to report that rather
    than guess.
    """
    pieces, pos = [], 0
    for m in LITERAL_RE.finditer(arg):
        if arg[pos:m.start()].strip():
            return None                   # non-literal token between the parts
        pieces.append(m.group(1))
        pos = m.end()
    if arg[pos:].strip() or not pieces:
        return None
    return unescape("".join(pieces))


_SIMPLE_ESCAPES = {"n": "\n", "r": "\r", "t": "\t", "a": "\a", "b": "\b",
                   "f": "\f", "v": "\v", "\\": "\\", "'": "'", '"': '"',
                   "?": "?", "0": "\0"}


def unescape(body):
    """Decode C escapes to the bytes the compiler actually emits.

    Source length is not output length: "\\r\\n" is four characters of source
    and two bytes of message. Measuring the source form would over-count and
    make this tool fail sites that fit.
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
            out.append("?")          # one byte, whatever its value
            i = j
        elif c in "01234567":
            j = i
            while j < n and j < i + 3 and body[j] in "01234567":
                j += 1
            out.append("?")
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
        fixed += m.start() - pos
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
        key = (m.group("length") or "") + conv
        if key not in WIDTHS:
            return None, (f"no conservative width for '{m.group(0)}' "
                          f"(conversion '%{conv}') -- add it to WIDTHS in "
                          f"tools/lint/log_budget.py or annotate the site")
        w = WIDTHS[key]
        if "#" in flags and conv in "xXo":
            w += 2                         # the 0x / 0 prefix the '#' flag adds
        total += max(field, w)
    fixed += len(fmt) - pos
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
    masked = mask_comments(src)
    found = []
    for macro, line, args, _call in find_calls(masked, macros):
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
    """
    if not path.exists():
        return set()
    entries, had_comment = set(), False
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
        entries.add(line.strip())
    return entries


# ---------------------------------------------------------------------------
# Self-test
# ---------------------------------------------------------------------------
SELF_TEST_CEILING = 125

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

    if failures:
        print(f"\n::error::log_budget: {failures} self-test(s) failed")
        return 1
    print(f"log_budget self-test: {len(SELF_TEST_CASES)}/"
          f"{len(SELF_TEST_CASES)} extraction cases + 4/4 ceiling cases pass")
    return 0


# ---------------------------------------------------------------------------
def main():
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
    args = ap.parse_args()

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
        if not args.root.exists():
            raise ToolError(f"root not found: {args.root}")
        ceiling, size, r1, r2 = read_ceiling(
            args.logger_h.read_text(encoding="utf-8"),
            args.logger_c.read_text(encoding="utf-8"))
        suppressions = load_suppressions(args.suppress)
    except (ToolError, OSError) as e:
        print(f"::error::log_budget: {e}", file=sys.stderr)
        return 2

    print(f"log_budget: ceiling {ceiling} bytes "
          f"(LOG_MESSAGE_SIZE {size}, vsnprintf reserves {r1} incl. its NUL, "
          f"clamp reserves {r2}) - read from "
          f"{args.logger_h} / {args.logger_c}")

    findings, scanned = scan(args.root, macros, ceiling)
    kept = [f for f in findings if suppress_key(f) not in suppressions]
    suppressed = len(findings) - len(kept)
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

    baseline = load_baseline(args.baseline)
    current = collections.Counter(record(f) for f in kept)
    new = current - baseline
    fixed = baseline - current

    if not new:
        print(f"log_budget: clean - {len(kept)} finding(s) in {scanned} "
              f"file(s), all in the baseline ({suppressed} suppressed)")
        if fixed:
            print(f"\n{sum(fixed.values())} baseline entr(ies) no longer "
                  f"reported. Regenerate so the gate keeps the ground it "
                  f"gained:\n"
                  f"    python3 tools/lint/log_budget.py --write-baseline")
            for ln in sorted(fixed):
                print(f"  - {ln}")
        return 0

    by_record = {}
    for f in kept:
        by_record.setdefault(record(f), f)
    print(f"\n::error::log_budget: {sum(new.values())} new finding(s) not in "
          f"{args.baseline}\n")
    for ln in sorted(new):
        f = by_record[ln]
        print(f"::error file={f.path},line={f.line}::{f.reason}  --  "
              f"\"{f.fmt}\"")
    print(f"\nLogger.c truncates a formatted message at {ceiling} bytes with "
          f"no error and no marker, and the remedy is written last, so the "
          f"remedy is what is lost. Either:")
    print("  - shorten the message (keep the remedy; see #1025 / #1039 for the "
          "comment convention), or")
    print("  - bound a %s with /* log_budget: max=N */ above the call, or")
    print("  - add a permanent waiver WITH a reason to "
          "tools/lint/log_budget-suppress.txt")
    return 1


if __name__ == "__main__":
    sys.exit(main())
