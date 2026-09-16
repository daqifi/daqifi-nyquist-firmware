#!/usr/bin/env python3
"""Extract the #1039 log-budget facts out of the REAL firmware sources.

Issue #1039 -- more `LOG_E` messages whose tails, and so whose operator-facing
remedies, were cut by `Util/Logger.c`'s message frame. The fix shortens four of
them; `test_1039_log_message_budget.c` proves each one now survives that frame
intact, by running the REAL texts through a mirror of `LogMessageFormatImpl`'s
truncation -- the same `vsnprintf`, the same bound, the same clamp, the same
CRLF fixup.

WHY A GENERATOR RATHER THAN CONSTANTS COPIED INTO THE TEST

Everything the test measures lives in firmware source that can be edited
without anyone thinking about this test:

    LOG_MESSAGE_SIZE                         Util/Logger.h
    vsnprintf's and the clamp's reservations Util/Logger.c
    the four LOG_E format strings            app_freertos.c, SCPIADC.c,
                                             wifi_services/wifi_manager.c

A copy in the test would go stale silently, measuring a message the device no
longer emits and reporting a pass for it. So the strings and the numbers are
read out of the sources at build time and written into a generated header.
Re-word a message and this test re-measures the new words; grow one past the
ceiling and it FAILS, rather than asking to be updated.

WHY THIS DOES NOT SIMPLY CALL tools/lint/log_budget.py

That tool is the static gate the same PR adds, and it MODELS the truncation:
it sums a format string's fixed bytes and a conservative per-specifier width.
This test's whole value is that it does not share that model -- it runs the
real strings through real `vsnprintf` and the real clamp. Reusing the gate's
parser here would put the gate's own extractor on both sides of the check, so
the scanning below is deliberately separate, small, and written to fail loudly
rather than to be clever.

HOW A SITE IS IDENTIFIED, AND WHY NOT BY ITS MESSAGE TEXT

Each site is anchored on a chain of CODE -- the branch condition that gates the
message, and where that is not unique, the enclosing function's definition
line first. Anchoring on the message would be the wrong guard twice over: it
would fail the build on an editorial re-word (which should re-MEASURE, not
break), and it would pass happily on a message that kept its opening words and
lost its ending, which is the exact defect here.

Every step fails the build, by name, on anything it cannot find, finds more
than once, or cannot reproduce:

  * the first anchor of a chain must appear EXACTLY once in the file;
  * each later anchor must appear after the previous one;
  * the LOG_E must be found within a bounded window after the last anchor;
  * its format argument must be a run of adjacent string literals, and the
    next thing after them must be a ',' or the closing ')' -- a partial
    extraction that stopped at a literal boundary hits a '"' instead and is
    rejected, which is the one failure mode (finding LESS than the whole
    message) that would otherwise pass every length assertion in the test
    while establishing nothing;
  * the conversion specifiers found must match the signature declared below,
    because the test substitutes worst-case arguments by hand -- if a site
    gains or loses a substitution, its worst case must be re-derived, not
    silently measured with the old arguments.

Comments are masked before any of this, because the house convention for this
class of fix leaves the OLD, over-budget text in a comment above the call.

Usage:
    python3 gen_1039_log_budget.py --out gen_1039_log_budget.h
"""
import argparse
import re
import sys
from pathlib import Path

FW = Path("../../firmware/src")

LOGGER_H = FW / "Util/Logger.h"
LOGGER_C = FW / "Util/Logger.c"


class GenError(Exception):
    """Something could not be extracted. Always fails the build, by name."""


# ---------------------------------------------------------------------------
# The four sites #1039 fixed.
#
# `anchors` is a chain of code fragments; the first must be unique in the file,
# each later one is searched for after the previous match. `nth` picks which
# LOG_E after the last anchor is the subject (the wifi pair share one anchor --
# the if/else arms of a single condition -- so they are 1 and 2). `specs` is
# the conversion signature the test substitutes worst-case arguments into.
# ---------------------------------------------------------------------------
SITES = [
    {
        "key": "CLOCK",
        "path": FW / "app_freertos.c",
        "anchors": ["if (!TimerApi_ClockMatchesBuild()) {"],
        "nth": 1,
        "specs": ["%u", "%u"],
        "what": "#716 clock-mismatch warning in app_SystemInit",
    },
    {
        "key": "ADCCHAN",
        "path": FW / "services/SCPI/SCPIADC.c",
        # The channelIndex guard appears in three ADC callbacks, so the
        # function's own definition line (with its opening brace, which the
        # forward declaration does not have) narrows it first.
        "anchors": [
            "static scpi_result_t ADCChanEnableSetClaimed(scpi_t * context) {",
            "if (channelIndex >= (size_t) pBoardConfigAInChannels->Size) {",
        ],
        "nth": 1,
        "specs": ["%d"],
        "what": "CONF:ADC:CHAN not-addressable refusal",
    },
    {
        "key": "SDPRESENT",
        "path": FW / "services/wifi_services/wifi_manager.c",
        "anchors": ["if (DRV_SDSPI_IsCardAttached(sysObj.drvSDSPI0)) {"],
        "nth": 1,
        "specs": [],
        "what": "#589 WiFi-down-with-SD-card-present hint",
    },
    {
        "key": "SDCLEAR",
        "path": FW / "services/wifi_services/wifi_manager.c",
        "anchors": ["if (DRV_SDSPI_IsCardAttached(sysObj.drvSDSPI0)) {"],
        "nth": 2,
        "specs": [],
        "what": "#589 WiFi-down-with-SPI4-clear hint (the else arm)",
    },
]

# How far after the last anchor the LOG_E may be. Generous enough for the
# multi-line fix comments the convention puts above each call, tight enough
# that a deleted call cannot be silently replaced by the next one in the file.
SEARCH_WINDOW = 4000

SPEC_RE = re.compile(r"%[-+ #0']*[0-9]*(?:\.[0-9]*)?(?:hh|h|ll|l|j|z|t|L)?"
                     r"([diouxXeEfgGaAcsp%])")


# ---------------------------------------------------------------------------
def mask_comments(src):
    """Blank comments, preserving every offset and newline.

    The fix convention quotes the OLD over-budget message in a comment above
    the shortened call, so an unmasked scan would extract the very text the
    commit removed -- and then cheerfully report it as over budget.
    """
    out = list(src)
    i, n = 0, len(src)
    while i < n:
        c = src[i]
        if c in '"\'':
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


_ESCAPES = {"n": "\n", "r": "\r", "t": "\t", "a": "\a", "b": "\b", "f": "\f",
            "v": "\v", "\\": "\\", "'": "'", '"': '"', "?": "?", "0": "\0"}


def _byte_char(value):
    """Map a raw 0-255 byte to a str that round-trips to exactly that one
    byte through `.encode('utf-8', errors='surrogateescape')` -- the same
    convention `tools/lint/log_budget.py` uses for the identical purpose, so
    a decoded escape here is charged/re-emitted the same way it is there.
    """
    value &= 0xFF
    return chr(value) if value < 0x80 else chr(0xDC00 + value)


def unescape(body):
    """Decode C escapes to the bytes the compiler emits.

    Source length is not output length: `\\r\\n` is four characters of source
    and two bytes of message, and measuring the source form would over-count.

    A NUMERIC ESCAPE IS DECODED TO ITS REAL BYTE, NEVER BLANKED TO A '?'
    PLACEHOLDER. An earlier version of this function did exactly that --
    the same bug `tools/lint/log_budget.py`'s `unescape()` had (see its
    docstring): `\\x25` (or octal `\\045`) compiles to a literal '%',
    identical to typing % directly, and blanking it to '?' would make this
    GENERATOR measure a different, shorter signature than the real compiled
    text and than what the lint tool itself would measure for the same
    source -- silently validating the wrong thing (Qodo /agentic_review
    finding on PR #1110, 2026-09-16, "Generated tests misread escaped
    formats": fixing the join-order bug in read_literal_group() without also
    fixing this decoder left the identical twin defect in place). No real
    extracted site uses a numeric escape today, so this changes no generated
    output, but a future site that does is now measured correctly instead of
    silently wrong.
    """
    out, i, n = [], 0, len(body)
    while i < n:
        if body[i] != "\\":
            out.append(body[i])
            i += 1
            continue
        i += 1
        if i >= n:
            raise GenError("string literal ends in a lone backslash")
        c = body[i]
        if c in "xX":
            j = i + 1
            while j < n and body[j] in "0123456789abcdefABCDEF":
                j += 1
            digits = body[i + 1:j]
            out.append(_byte_char(int(digits, 16)) if digits else "x")
            i = j
        elif c in "01234567":
            j = i
            while j < n and j < i + 3 and body[j] in "01234567":
                j += 1
            out.append(_byte_char(int(body[i:j], 8)))
            i = j
        elif c == "\n":
            # A backslash immediately followed by a real newline is a C
            # line continuation (C99 5.1.1.2), not an "unknown escape" --
            # it deletes BOTH characters, emitting zero bytes, before the
            # string literal's contents are even formed. The pre-fix
            # fallback (`_ESCAPES.get(c, c)`) returned `c` itself, since the
            # newline BYTE (0x0A) is not a key in `_ESCAPES` (whose keys are
            # the escape LETTERS, e.g. the character 'n', never the
            # character it decodes to) -- so a continued literal was
            # measured and re-emitted with one extra byte the firmware
            # never emits, either rejecting a message that actually fits or
            # generating a test string that diverges from what ships (Qodo
            # /agentic_review, PR #1110, 2026-09-16: "Continued test
            # messages gain a newline"). tools/lint/log_budget.py does not
            # need this case: its splice_continuations() deletes a
            # continuation from raw source BEFORE literal boundaries are
            # even recognized, so its own unescape() never sees one.
            i += 1
        elif c == "\r" and i + 1 < n and body[i + 1] == "\n":
            i += 2                    # \<CR><LF> continuation: same, 0 bytes
        else:
            out.append(_ESCAPES.get(c, c))
            i += 1
    return "".join(out)


def read_literal_group(masked, start, where):
    """Decode the run of adjacent string literals beginning at `start`.

    C concatenates adjacent literals, so a message split over three physical
    lines is ONE message. Returns (decoded text, index just past the run).
    Raises unless the run is followed by a ',' or the call's ')': stopping in
    the middle of a concatenation is the failure that would understate a
    message's length and pass every assertion downstream.

    EACH LITERAL IS DECODED (via unescape()) BEFORE IT IS JOINED TO THE NEXT
    ONE, never after. This mirrors tools/lint/log_budget.py's literal_text(),
    which had the identical bug: C99 6.4.4.4 decodes the escapes inside a
    single string literal in translation phase 5, strictly before phase 6
    concatenates adjacent literals, so a literal boundary is not a character
    a maximal-munch escape (\\xHH / \\OOO) can see across in real C. Joining
    every literal's RAW text first and decoding once at the end -- what this
    function did before -- would let a hex/octal escape at the tail of one
    literal spill into the next literal's leading characters when they
    happen to be valid hex/octal digits, silently understating the message
    (verified against log_budget.py's twin of this bug, PR #1110 audit,
    2026-09-16). No real site this generator extracts uses a numeric escape
    today, so this is a latent-bug fix, not an output change -- but a future
    site that does would otherwise be measured with the wrong text and no
    warning.
    """
    i, n = start, len(masked)
    pieces = []
    while True:
        while i < n and masked[i] in " \t\r\n":
            i += 1
        if i >= n or masked[i] != '"':
            break
        i += 1
        body = []
        while i < n:
            if masked[i] == "\\":
                body.append(masked[i:i + 2])
                i += 2
                continue
            if masked[i] == '"':
                i += 1
                break
            body.append(masked[i])
            i += 1
        else:
            raise GenError(f"{where}: unterminated string literal")
        pieces.append(unescape("".join(body)))
    if not pieces:
        raise GenError(f"{where}: the LOG_E format argument is not a string "
                       f"literal -- this test measures literal message text")
    j = i
    while j < n and masked[j] in " \t\r\n":
        j += 1
    if j >= n or masked[j] not in ",)":
        raise GenError(
            f"{where}: the format literal is followed by {masked[j:j + 20]!r}, "
            f"not ',' or ')'. The extraction stopped in the middle of the "
            f"message; measuring that fragment would understate its length.")
    return "".join(pieces), i


def extract_site(site):
    path = site["path"]
    where = f"{path}: {site['what']}"
    try:
        src = path.read_text(encoding="utf-8", errors="surrogateescape")
    except OSError as e:
        raise GenError(f"{where}: cannot read the source: {e}") from e
    masked = mask_comments(src)

    first = site["anchors"][0]
    hits = masked.count(first)
    if hits != 1:
        raise GenError(
            f"{where}: the anchor {first!r} appears {hits} time(s) in {path}, "
            f"expected exactly 1. This test identifies the site by the code "
            f"around it; re-derive the anchor before trusting any verdict.")
    pos = masked.index(first) + len(first)
    for anchor in site["anchors"][1:]:
        nxt = masked.find(anchor, pos)
        if nxt < 0:
            raise GenError(
                f"{where}: the anchor {anchor!r} was not found after the "
                f"previous one. The code around this message has moved.")
        pos = nxt + len(anchor)

    window = masked[pos:pos + SEARCH_WINDOW]
    calls = [m.end() for m in re.finditer(r"(?<![A-Za-z0-9_])LOG_E\s*\(",
                                          window)]
    if len(calls) < site["nth"]:
        raise GenError(
            f"{where}: expected at least {site['nth']} LOG_E call(s) within "
            f"{SEARCH_WINDOW} characters of the anchor, found {len(calls)}. "
            f"The message this test measures is gone or has moved.")
    text, _ = read_literal_group(masked, pos + calls[site["nth"] - 1], where)

    if not text.strip():
        raise GenError(f"{where}: extracted an empty message -- an empty "
                       f"string satisfies every budget assertion while "
                       f"establishing nothing")
    specs = ["%" + m.group(1) for m in SPEC_RE.finditer(text)
             if m.group(1) != "%"]
    if specs != site["specs"]:
        raise GenError(
            f"{where}: conversion signature is {specs}, expected "
            f"{site['specs']}. test_1039_log_message_budget.c substitutes "
            f"worst-case arguments for exactly that signature, so a change "
            f"here means the worst case must be re-derived, not re-run.")
    return text


def read_ceiling():
    """LOG_MESSAGE_SIZE plus BOTH of LogMessageFormatImpl's reservations.

    The arithmetic is split across the two files -- the constant is in the
    header, but what actually survives is set by the vsnprintf bound and the
    clamp in the .c -- so both are read, and neither is assumed.
    """
    try:
        h = LOGGER_H.read_text(encoding="utf-8")
        c = LOGGER_C.read_text(encoding="utf-8")
    except OSError as e:
        raise GenError(f"cannot read Logger.h / Logger.c: {e}") from e

    m = re.search(r"^\s*#define\s+LOG_MESSAGE_SIZE\s+(\d+)\s*$", h, re.M)
    if not m:
        raise GenError("no '#define LOG_MESSAGE_SIZE <n>' in Logger.h")
    size = int(m.group(1))

    body = re.search(r"LogMessageFormatImpl\s*\([^)]*\)\s*\{(.*?)\n\}", c, re.S)
    if not body:
        raise GenError("could not find the body of LogMessageFormatImpl in "
                       "Logger.c -- the mirror in "
                       "test_1039_log_message_budget.c models that function, "
                       "so a reshape must be re-read by hand, not assumed")
    impl = body.group(1)
    m1 = re.search(r"vsnprintf\s*\(\s*\w+\s*,\s*LOG_MESSAGE_SIZE\s*-\s*(\d+)",
                   impl)
    m2 = re.search(r"min\s*\(\s*\(?\s*LOG_MESSAGE_SIZE\s*-\s*(\d+)", impl)
    if not m1 or not m2:
        raise GenError(
            "LogMessageFormatImpl no longer bounds vsnprintf at "
            "LOG_MESSAGE_SIZE - N and clamps with min((LOG_MESSAGE_SIZE - N), "
            "...). The mirror models both; re-derive it before re-enabling "
            "this test.")
    return size, int(m1.group(1)), int(m2.group(1))


def c_literal(text):
    """Re-emit a decoded message as a C string literal.

    A byte that is not printable ASCII -- including one that came from a
    decoded \\xHH / \\OOO escape via unescape()'s surrogateescape round-trip
    -- is re-emitted as a 3-DIGIT ZERO-PADDED OCTAL escape (\\OOO), never
    hex. C's hex escape has no digit limit -- it maximal-munches every
    following hex-digit CHARACTER, escape or not -- so re-emitting byte 0x25
    as "\\x25" immediately before a literal '3' in the message would compile
    as the single three-hex-digit escape \\x253 (a different byte entirely),
    not as 0x25 followed by '3'. An octal escape is defined to consume AT
    MOST three digits regardless of what follows (C99 6.4.4.4), so padding
    to exactly three digits makes the boundary unambiguous with no
    string-concatenation trick needed. (Most decoded escapes -- '%' among
    them -- are printable ASCII and never reach this branch at all; it only
    matters for a genuinely non-printable byte.)
    """
    out = []
    for ch in text:
        cp = ord(ch)
        if ch == "\\":
            out.append("\\\\")
        elif ch == '"':
            out.append('\\"')
        elif ch == "\r":
            out.append("\\r")
        elif ch == "\n":
            out.append("\\n")
        elif ch == "\t":
            out.append("\\t")
        elif " " <= ch <= "~":
            out.append(ch)
        elif 0xDC80 <= cp <= 0xDCFF:
            # a raw byte >= 0x80, round-tripped through unescape()'s
            # surrogateescape convention -- re-emit its real byte value.
            out.append(f"\\{cp - 0xDC00:03o}")
        elif cp < 0x100:
            # a narrow control byte (from an octal/hex escape, or a literal
            # control character) that isn't printable ASCII.
            out.append(f"\\{cp:03o}")
        else:
            raise GenError(
                f"message contains the non-byte codepoint {ch!r} (U+"
                f"{cp:04X}), which this generator will not re-emit blind -- "
                f"decide what it should become and extend c_literal()")
    return '"' + "".join(out) + '"'


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--out", type=Path, default=Path("gen_1039_log_budget.h"))
    args = ap.parse_args()

    try:
        size, vsn, clamp = read_ceiling()
        texts = {s["key"]: extract_site(s) for s in SITES}
    except GenError as e:
        print(f"ERROR: gen_1039_log_budget.py: {e}", file=sys.stderr)
        return 1

    lines = [
        "/* GENERATED at build time by tests/host/gen_1039_log_budget.py.",
        " * Do not edit, do not commit -- see tests/host/.gitignore. */",
        "#ifndef GEN_1039_LOG_BUDGET_H",
        "#define GEN_1039_LOG_BUDGET_H",
        f"#define FW_LOG_MESSAGE_SIZE      {size}",
        f"#define FW_LOG_VSNPRINTF_RESERVE {vsn}",
        f"#define FW_LOG_CLAMP_RESERVE     {clamp}",
    ]
    for s in SITES:
        lines.append(f"/* {s['path']}: {s['what']} */")
        lines.append(f"#define FW_1039_{s['key']}_FMT {c_literal(texts[s['key']])}")
    lines.append("#endif")
    args.out.write_text("\n".join(lines) + "\n", encoding="utf-8")

    print(f"gen_1039_log_budget.py: ceiling {size - clamp} bytes "
          f"(LOG_MESSAGE_SIZE {size}, reserves {vsn}/{clamp}); extracted "
          f"{len(texts)} message(s) -> {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
