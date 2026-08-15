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
    firmware/src, excluding third_party/ and libraries/ -- vendored code we do
    not own, matching tools/lint/cppcheck.sh's exclusions.

Usage:
    python3 tools/lint/check_log_ascii.py [ROOT]     # default firmware/src
Exit codes:
    0 = clean, 1 = offending literals found, 2 = usage/IO error
"""
import sys
from pathlib import Path

DEFAULT_ROOT = Path("firmware/src")
EXCLUDED_PREFIXES = ("third_party/", "libraries/")

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
            hint = HINTS.get(ch)
            suffix = f"  (use {hint!r})" if hint else ""
            print(f"::error file={path},line={line}::non-ASCII "
                  f"U+{ord(ch):04X} {ch!r} in a string literal{suffix}"
                  f"  --  {preview!r}")
    print("\nDevice text is read over SYST:LOG? / SYST:ERR?, where non-ASCII "
          "is mangled on a cp1252 console, can throw in a client, and eats "
          "the 128-byte LOG_MESSAGE_SIZE budget. See #787.")
    return 1


if __name__ == "__main__":
    sys.exit(main())
