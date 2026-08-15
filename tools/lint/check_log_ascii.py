#!/usr/bin/env python3
"""Fail if any string literal in our firmware source contains non-ASCII (#787).

Device text reaches operators over `SYST:LOG?` and `SYST:ERR?`. A non-ASCII
byte there is unreadable on a cp1252 console, throws `UnicodeEncodeError`
outright in a Python client, AND costs extra bytes against the firmware's
128-byte `LOG_MESSAGE_SIZE` -- so it truncates the rest of the message too.
That combination is how #787 surfaced: a client reading the log died, and the
line it died on had already lost its explanation to truncation.

WHY ALL LITERALS AND NOT JUST LOG_* CALLS
    The first version of this checker inspected only text inside `LOG_*(...)`.
    It reported the tree clean while SEVEN em dashes still reached the log,
    because they lived in strings assigned to a variable:

        *err = "IC: busy - another measurement in progress";   (UserIC.c)
        ...
        SCPI_ExecutionError(context, err);                     (SCPIDIO.c)
        -> static inline: LOG_E("SCPI exec error: %s", reason) (SCPIInterface.h)

    A checker scoped to the call site cannot see that. "No non-ASCII in any
    string literal we compile" is both simpler to state and impossible to
    sidestep by indirection -- and on a bare-metal target whose only text
    output is a serial console, there is no legitimate exception.

WHY COMMENTS ARE SKIPPED
    Comments never reach the device. Failing CI for an em dash in prose would
    be a false failure, and this file is littered with them on purpose.
    Literals are therefore extracted with a small scanner that skips `//`,
    `/* */` and character literals rather than by regex.

SCOPE
    firmware/src, excluding third_party/, libraries/ and config/ -- vendored
    code we do not own. Same exclusions as tools/lint/cppcheck.sh.

    config/ is excluded for a concrete reason, not by analogy: vendored FatFs
    (config/default/system/fs/fat_fs/file_system/ff.c) embeds a boot-sector
    jump instruction as "\xEB\x76\x90". That is binary data in a string
    literal, entirely legitimate, and scanning it turns this gate red on
    correct code. An earlier revision did include config/ and did exactly
    that.

    CAVEAT worth knowing: one real violation was fixed under config/
    (config/default/driver/sdspi/src/drv_sdspi.c) and is therefore NOT
    guarded against regression here. MCC regeneration is retired, so that
    file changes only by hand -- but a hand edit could reintroduce it
    silently.

Usage:
    python3 tools/lint/check_log_ascii.py [ROOT]     # default firmware/src
Exit codes:
    0 = clean, 1 = offending literals found, 2 = usage/IO error
"""
import re
import sys
from pathlib import Path

DEFAULT_ROOT = Path("firmware/src")
EXCLUDED_PREFIXES = ("third_party/", "libraries/", "config/")

# Advisory replacements for characters seen in this codebase. Any non-ASCII
# is a failure whether or not it appears here.
HINTS = {
    "—": "-",     # em dash
    "–": "-",     # en dash
    "→": "->",    # rightwards arrow
    "⇒": "=>",    # rightwards double arrow
    "×": "x",     # multiplication sign
    "±": "+/-",   # plus-minus
    "≥": ">=",
    "≤": "<=",
    "≈": "~",
    "…": "...",
}


def escaped_nonascii(lit):
    """Return escape sequences in `lit` that encode a value > 127.

    Walks the literal instead of regex-scanning it, because a regex gets three
    things wrong here -- all three were caught by an adversarial audit, and the
    third would have failed CI on perfectly good code:

    * ESCAPED BACKSLASH. In "literal \\\\xE2 token" the compiler emits the ASCII
      bytes \\, x, E, 2. A regex retries after a failed match and finds "\\xE2"
      starting at the SECOND backslash, so it reports non-ASCII in a string
      that has none. Consuming \\\\ as a unit is the only way to be right.
    * MAXIMAL MUNCH. C99 6.4.4.4: a hex escape consumes ALL following hex
      digits. "\\x0E2" is one byte 0xE2, not \\x0E followed by '2'. A {1,2}
      slice sees 0x0E (14) and passes an em dash straight through.
    * LINE SPLICES. Translation phase 2 removes backslash-newline before
      tokenizing, so "\\x" + splice + "E2" is really \\xE2. Handled by the
      caller, which strips splices before this runs.
    """
    out = []
    i, n = 0, len(lit)
    while i < n:
        if lit[i] != "\\":
            i += 1
            continue
        i += 1                              # consume the backslash
        if i >= n:
            break
        c = lit[i]
        if c == "\\":                        # escaped backslash: consume BOTH
            i += 1
            continue
        if c in "xX":
            j = i + 1
            while j < n and lit[j] in "0123456789abcdefABCDEF":
                j += 1                      # maximal munch, per C99
            if j > i + 1 and int(lit[i + 1:j], 16) > 127:
                out.append(lit[i - 1:j])
            i = j
        elif c in "01234567":
            j = i
            while j < n and j < i + 3 and lit[j] in "01234567":
                j += 1                      # octal is capped at 3 digits
            if int(lit[i:j], 8) > 127:
                out.append(lit[i - 1:j])
            i = j
        elif c in "uU":
            width = 4 if c == "u" else 8
            j, k = i + 1, i + 1 + width
            digits = lit[j:k]
            if len(digits) == width and all(
                    d in "0123456789abcdefABCDEF" for d in digits):
                if int(digits, 16) > 127:
                    out.append(lit[i - 1:k])
                i = k
            else:
                i += 1
        else:
            i += 1                          # \\n, \\t, \\", ... : ordinary
    return out


def has_line_splice(lit):
    """True if the literal contains a backslash-newline line splice.

    The compiler removes these before tokenizing, so they can hide an escape
    from any scanner that reads unspliced source. Rather than decode around
    them, the caller fails closed and asks for the splice to be removed --
    they do not occur anywhere in this codebase and have no legitimate use
    inside a log string.
    """
    return "\\\n" in lit or "\\\r\n" in lit


def string_literals(src):
    """Yield (offset, text) for each string literal, skipping comments.

    Deliberately a scanner, not a regex: a regex that ignores comments
    matches the span between two unrelated quote characters in prose, which
    during development produced 14 bogus "literals" out of 21 candidates.
    """
    i, n = 0, len(src)
    while i < n:
        c = src[i]
        if c == '"':
            j = i + 1
            while j < n:
                if src[j] == "\\":
                    j += 2
                    continue
                if src[j] == '"':
                    j += 1
                    break
                j += 1
            yield i, src[i:j]
            i = j
        elif c == "'":                      # char literal: skip, don't report
            j = i + 1
            while j < n:
                if src[j] == "\\":
                    j += 2
                    continue
                if src[j] == "'":
                    j += 1
                    break
                j += 1
            i = j
        elif src.startswith("//", i):
            j = src.find("\n", i)
            i = n if j < 0 else j
        elif src.startswith("/*", i):
            j = src.find("*/", i + 2)
            i = n if j < 0 else j + 2
        else:
            i += 1


def main():
    root = Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_ROOT
    if not root.exists():
        print(f"check_log_ascii: root not found: {root}", file=sys.stderr)
        return 2

    offenders = []
    scanned = 0
    for path in sorted(root.rglob("*.c")) + sorted(root.rglob("*.h")):
        rel = path.relative_to(root).as_posix()
        if any(rel.startswith(p) for p in EXCLUDED_PREFIXES):
            continue
        scanned += 1
        try:
            src = path.read_text(encoding="utf-8", errors="surrogateescape")
        except OSError as e:
            print(f"check_log_ascii: cannot read {path}: {e}", file=sys.stderr)
            return 2
        for off, lit in string_literals(src):
            bad = sorted({ch for ch in lit if ord(ch) > 127})
            bad += escaped_nonascii(lit)
            if has_line_splice(lit):
                bad.append("<line-splice>")
            if bad:
                offenders.append((path, src.count("\n", 0, off) + 1, bad,
                                  lit.strip('"')[:60]))

    if not offenders:
        print(f"check_log_ascii: clean - no non-ASCII in any string literal "
              f"({scanned} files scanned)")
        return 0

    print(f"check_log_ascii: {len(offenders)} string literal(s) contain "
          f"non-ASCII\n")
    for path, line, bad, preview in offenders:
        for ch in bad:
            if len(ch) == 1:
                hint = HINTS.get(ch)
                suffix = f"  (use {hint!r})" if hint else ""
                what = f"non-ASCII U+{ord(ch):04X} {ch!r}"
            else:
                suffix = "  (escape encodes a byte > 127)"
                what = f"non-ASCII escape {ch!r}"
            print(f"::error file={path},line={line}::{what} in a string "
                  f"literal{suffix}  --  {preview!r}")
    print("\nDevice text is read over SYST:LOG? / SYST:ERR?, where non-ASCII "
          "is mangled on a cp1252 console, can throw in a client, and eats "
          "the 128-byte LOG_MESSAGE_SIZE budget. See #787.")
    return 1


if __name__ == "__main__":
    sys.exit(main())
