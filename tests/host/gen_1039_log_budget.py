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

WHAT IS SHARED WITH tools/lint/log_budget.py, AND WHAT MUST NEVER BE

⛔ AN EARLIER REVISION OF THIS SECTION SAID THIS MODULE SHARES NOTHING WITH THE
GATE, on the grounds that "reusing the gate's parser here would put the gate's
own extractor on both sides of the check". That was right about one half and
wrong about the other, and the wrong half cost a finding (codex audit on #1110
r4). The distinction the old text missed:

  * THE MEASUREMENT IS NOT SHARED, and must never be. The gate MODELS
    truncation -- it sums a format string's fixed bytes plus a conservative
    per-specifier width. This test's whole value is that it does NOT share that
    model: it runs the real strings through real `vsnprintf` and the real
    clamp. Two independent models of the same thing is the point.

  * THE CEILING IS SHARED, and must be. `LOG_MESSAGE_SIZE - 2` and
    `min((LOG_MESSAGE_SIZE - 3), ...)` are not a model of anything -- they are
    two integers transcribed out of Logger.c. There is one right answer, so two
    readers can only agree or be wrong, and "independently transcribed" buys
    nothing.

What it cost: the ceiling reader existed twice, the gate's was fixed to walk by
paren DEPTH, and this copy kept the `[^,)]*` regex the gate had just discarded.
Measured on `min((LOG_MESSAGE_SIZE - 3) - 10, size)` the gate read 13 and this
module read 3 -- a silent 10-byte disagreement, with nothing anywhere comparing
the two. So the independence delivered no detection, only divergence; the
reader is now imported (see `_reserved_after`) and there is one of it.

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
import importlib.util
import re
import sys
from pathlib import Path

FW = Path("../../firmware/src")

LOGGER_H = FW / "Util/Logger.h"
LOGGER_C = FW / "Util/Logger.c"

#: The gate, located from THIS FILE rather than the cwd. The Makefile runs the
#: generator with tests/host as the working directory, but a developer running
#: it from the repo root should not get a different answer.
GATE_PY = Path(__file__).resolve().parents[2] / "tools/lint/log_budget.py"


class GenError(Exception):
    """Something could not be extracted. Always fails the build, by name."""


_gate_mod = None


def _gate():
    """tools/lint/log_budget.py, loaded by path and cached.

    Loaded lazily so that a missing or broken gate fails only the extraction
    that needs it, with this module's own error type, rather than making
    `--help` unavailable.
    """
    global _gate_mod
    if _gate_mod is None:
        if not GATE_PY.is_file():
            raise GenError(
                f"cannot find the ceiling reader at {GATE_PY} -- it is shared "
                f"with the gate on purpose (see this module's docstring). "
                f"Restore it rather than re-implementing the walk here.")
        try:
            spec = importlib.util.spec_from_file_location("_log_budget_gate",
                                                          GATE_PY)
            mod = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(mod)
        except Exception as e:
            raise GenError(f"cannot load {GATE_PY}: {e}") from e
        _gate_mod = mod
    return _gate_mod


def _gate_read_ceiling(h_text, c_text):
    """The gate's read_ceiling(), with its ToolError renamed to GenError.

    THE ONE SEAM, and it is deliberately the WHOLE derivation rather than just
    the arithmetic walk: masking the comments, locating LogMessageFormatImpl's
    body, and applying the walk to both reservations. r4 shared only the walk
    and left this module preparing its own unmasked input, which reproduced the
    very divergence the sharing was supposed to end (r5 finding 2).

    ⭐ The rule that cost two rounds to learn: A SEAM MUST START WHERE THE
    INPUT IS PREPARED, not where the parsing begins. Sharing a parser fed by
    two different pipelines shares nothing that matters.
    """
    gate = _gate()
    try:
        return gate.read_ceiling(h_text, c_text)
    except gate.ToolError as e:
        raise GenError(str(e)) from e


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


def dependency_registration_problems():
    r"""Why the imported gate is not a declared build input. [] when it is.

    ⛔ A SEAM IS A NEW EDGE, AND EVERY DEPENDENCY LIST THAT TRACKED THE OLD
    COPIES MUST NOW TRACK IT. When read_ceiling stopped being local and became
    an import, this module gained a build input that nothing knew about: it was
    in neither the generated-header prerequisites nor host-tests.yml's path
    filters. Renaming `reserve_after` in the checker left the checker's OWN
    self-tests green while breaking this module's external call, a PR touching
    only that file queued no host tests, and an incremental make kept the stale
    fixture and binary -- so the breakage surfaced on someone else's later
    clean build (codex audit on #1110 r5).

    ⭐ Derived from `GATE_PY` -- the path actually imported -- rather than from
    a list written by hand, because a hand-maintained list is exactly what
    dropped this edge. If the import moves, this check follows it.

    ⚠ It asserts the Makefile PREREQUISITE, not the mere presence of the
    string: that string already appeared twice in prose comments while the
    edge was missing, so a `grep -q` would have reported this registered the
    whole time. Presence is not semantics.

    Scoped deliberately: this answers "is MY import registered?", not "is
    every host-test input registered?". The general form -- deriving the whole
    set of firmware files the host targets read and failing on any absent from
    a CI filter -- is a repo-wide instrument and belongs in its own change,
    not copied in here.
    """
    repo = Path(__file__).resolve().parents[2]
    rel = GATE_PY.relative_to(repo).as_posix()
    here = Path(__file__).resolve().parent
    problems = []

    mk_path = here / "Makefile"
    try:
        mk = mk_path.read_text(encoding="utf-8")
    except OSError as e:
        return [f"cannot read {mk_path}: {e}"]

    # Resolve the variable, then confirm the generated-header rule actually
    # DEPENDS on it. Two separate questions, both required.
    var = re.search(r"^(\w+)\s*:=\s*\.\./\.\./" + re.escape(rel) + r"\s*$",
                    mk, re.M)
    if not var:
        problems.append(
            f"tests/host/Makefile has no variable assigned to ../../{rel}; "
            f"add one (see LOG_BUDGET_GATE) and list it as a prerequisite of "
            f"$(LOG1039_GEN)")
    else:
        rule = re.search(r"^\$\(LOG1039_GEN\):((?:[^\n\\]*\\\n)*[^\n]*)",
                         mk, re.M)
        if not rule:
            problems.append(
                "tests/host/Makefile has no $(LOG1039_GEN): rule to check")
        elif f"$({var.group(1)})" not in rule.group(1):
            problems.append(
                f"$({var.group(1)}) is defined but is NOT a prerequisite of "
                f"$(LOG1039_GEN), so an incremental make keeps the old "
                f"fixture when only {rel} changes")

    wf_path = repo / ".github/workflows/host-tests.yml"
    try:
        wf = wf_path.read_text(encoding="utf-8")
    except OSError as e:
        problems.append(f"cannot read {wf_path}: {e}")
        return problems

    # Count the `paths:` blocks and require an entry in each, so adding a
    # trigger block later cannot leave this edge half-registered.
    blocks = len(re.findall(r"^\s*paths:\s*$", wf, re.M))
    entries = len(re.findall(r"^\s*-\s*'" + re.escape(rel) + r"'\s*$",
                             wf, re.M))
    if blocks == 0:
        problems.append(f"{wf_path} declares no paths: filter to check")
    elif entries < blocks:
        problems.append(
            f".github/workflows/host-tests.yml lists '{rel}' in {entries} of "
            f"{blocks} paths: blocks; a PR touching only that file queues no "
            f"host tests from the block(s) missing it")
    return problems


def conversion_signature(text, where):
    r"""Every conversion in `text`, REFUSING any '%' it cannot parse.

    ⛔ IT ACCOUNTS FOR EVERY PERCENT SIGN, because omitting one is not a
    cosmetic gap here -- it is a wild store. SPEC_RE's conversion class is
    `[diouxXeEfgGaAcsp%]`, which excludes `n`. A plain `finditer` therefore
    SKIPS `%n` silently, so a format that gained one still compared equal to
    the recorded `['%u', '%u']` signature, and the renderer went on supplying
    exactly two UINT32_MAX values. `snprintf` then treats the first as `%n`'s
    `int *` destination and writes through it -- the audit reproduced a
    SIGSEGV from the accepted format and the renderer's own arguments (codex
    audit on #1110 r5, memory_safety).

    So this anchors SPEC_RE AT each '%' rather than searching for the next one
    it happens to like. Anything unmatched stops the build. Same discipline as
    the gate's `reserve_after` ("account for every character, refuse the rest")
    and its `literal_bytes` ("a surviving '%' is not literal text") -- the gate
    already refused this class and only the generator did not.

    `%%` is a conversion for parsing purposes and is excluded from the
    signature, since it consumes no argument.
    """
    specs, i = [], 0
    while True:
        j = text.find("%", i)
        if j < 0:
            return specs
        m = SPEC_RE.match(text, j)
        if not m:
            raise GenError(
                f"{where}: cannot parse the conversion at offset {j} "
                f"({text[j:j + 8]!r}) in {text!r}. Only "
                f"{'[diouxXeEfgGaAcsp%]'} conversions are understood, and an "
                f"unparsed one must NOT be dropped: "
                f"test_1039_log_message_budget.c substitutes a fixed argument "
                f"list for the recognised signature, so a conversion omitted "
                f"here consumes an argument that was never supplied. `%n` in "
                f"particular writes through whatever that argument is. "
                f"Re-derive the worst case by hand.")
        if m.group(1) != "%":
            specs.append(m.group(0))
        i = m.end()


def _clamp_of(clamp_src, extra=""):
    """The clamp reservation the PRODUCTION path derives. Self-test only.

    Runs `clamp_src` through the same `_gate_read_ceiling` that read_ceiling()
    uses, inside a synthetic LogMessageFormatImpl body -- so a case here
    exercises comment masking and body location too, not merely the arithmetic.

    That matters: r4's cases called the walk directly on expression fragments,
    passed green, and could not see that the text reaching the walk in
    production was unmasked. ⚠ A self-test that enters the pipeline downstream
    of the defect is green by construction, so these enter at the top.

    `extra` goes ABOVE the clamp line, for cases about surrounding source.
    """
    h = "#define LOG_MESSAGE_SIZE 128\n"
    c = ("void LogMessageFormatImpl(const char *f, va_list a)\n{\n"
         "    size = vsnprintf(buffer, LOG_MESSAGE_SIZE - 2, f, a);\n"
         + (extra + "\n" if extra else "")
         + clamp_src + "\n}\n")
    return _gate_read_ceiling(h, c)[3]          # (ceiling, size, vsn, clamp)


def splice_continuations(src):
    r"""Delete every backslash-newline, as C translation phase 2 does.

    No position mapping is kept, unlike the gate's namesake in log_budget.py:
    this module reports sites by anchor text rather than by line number, so
    nothing downstream needs to map an offset back to the original source.
    """
    return re.sub(r"\\\r?\n", "", src)


def extract_site(site):
    path = site["path"]
    where = f"{path}: {site['what']}"
    try:
        src = path.read_text(encoding="utf-8", errors="surrogateescape")
    except OSError as e:
        raise GenError(f"{where}: cannot read the source: {e}") from e
    # PHASE ORDER, and it is C's order, not a convenience. Translation phase 2
    # deletes every backslash-newline BEFORE phase 3 forms tokens, so the
    # compiler sees a spliced string where this reader saw two lines. Masking
    # or decoding first makes the two disagree: the audit's case is
    # "\x2\<newline>5.200u", which C splices to "\x25.200u" -- the conversion
    # %.200u, 240 formatted bytes -- while reading unspliced yields 0x02
    # followed by a literal "5.200u" and accepts the OLD two-conversion
    # signature. The sibling gate already gets this right
    # (log_budget.py's splice_continuations, called before its own masking),
    # and hash_function.py got the same ordering right for comments; this is
    # the one place that had it backwards.
    src = splice_continuations(src)
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
    # The COMPLETE match (m.group(0)), not "%" + the final conversion
    # letter alone (m.group(1)): the letter-only form makes "%u" and "%lu"
    # -- or "%d" and "%lld" -- look like the identical signature, silently
    # losing a length-modifier change. On THIS target a bare 'l' happens not
    # to widen an int-class conversion (log_budget.py's WIDTHS table:
    # "long is 32-bit"), but 'll'/'j'/'z'/'t' do, and this check exists
    # precisely so a site gaining or losing a length modifier is caught
    # here rather than silently measured with the OLD worst-case arguments
    # test_1039_log_message_budget.c hand-substitutes for the signature
    # (Qodo /improve, PR #1110, importance 9, "Preserve complete format
    # conversion signatures").
    specs = conversion_signature(text, where)
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

    # ⛔ THE WHOLE DERIVATION IS THE GATE'S, INPUT PREPARATION INCLUDED.
    #
    # r4 shared only the paren-depth walk (`reserve_after`) and kept this
    # module's own `#define` regex, its own LogMessageFormatImpl body regex,
    # and -- the defect -- its own UNMASKED source. r5 showed that sharing a
    # parser prevents nothing while each side feeds it different text: with
    #
    #     /* Previously: size = min((LOG_MESSAGE_SIZE - 3), size); */
    #     size = min((LOG_MESSAGE_SIZE - 13), size);
    #
    # the gate (which masks comments first) read 13 and this module read 3 --
    # the SAME divergence as r4, relocated from the parser to its input, and
    # my r4 note claiming the shared walk had closed the class was wrong
    # (codex audit on #1110 r5, both legs).
    #
    # So the seam is now the gate's `read_ceiling`, which masks both texts,
    # locates the body, and applies the walk twice. Three duplicated readers
    # deleted, not one.
    #
    # Why it matters beyond tidiness: FW_LOG_CLAMP_RESERVE feeds test_1039's
    # own `#define LOG_BUDGET (FW_LOG_MESSAGE_SIZE - FW_LOG_CLAMP_RESERVE)`,
    # so a reserve read short makes the C mirror clamp at 125 where the device
    # clamps at 115 -- the fixture PASSES while asserting a ceiling the
    # firmware does not have. `ASSERT_TRUE(CLAMP > VSNPRINTF)` cannot catch
    # it: 3 > 2 and 13 > 2 both hold.
    #
    # ⭐ What is still NOT shared is the MEASUREMENT: the gate models
    # truncation, this test runs the real strings through real vsnprintf. That
    # independence is the point and must stay. The ceiling is a transcription
    # of two integers out of Logger.c, which is why it may not.
    gate = _gate()
    try:
        _ceiling, size, vsn, clamp = gate.read_ceiling(h, c)
    except gate.ToolError as e:
        raise GenError(str(e)) from e
    return size, vsn, clamp


def c_literal_byte_len(literal):
    """Bytes a C string literal (quotes included) encodes at compile time.

    Deliberately counts BYTES, not characters -- it exists to check c_literal()
    against Logger's byte ceiling, and the defect it guards was precisely a
    character count standing in for a byte count. An octal escape is at most
    three digits (C99 6.4.4.4) and always one byte; the simple escapes are one
    byte each; anything else contributes its own UTF-8 length.
    """
    body = literal[1:-1] if literal.startswith('"') else literal
    n = 0
    i = 0
    while i < len(body):
        if body[i] == "\\" and i + 1 < len(body):
            m = re.match(r"\\([0-7]{1,3})", body[i:])
            if m:
                n += 1
                i += len(m.group(0))
                continue
            n += 1          # \\ \" \r \n \t -- one byte each
            i += 2
            continue
        n += len(body[i].encode("utf-8"))
        i += 1
    return n


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
        elif cp < 0x80:
            # a narrow control byte (from an octal/hex escape, or a literal
            # control character) that isn't printable ASCII.
            out.append(f"\\{cp:03o}")
        elif cp < 0x100:
            # U+0080..U+00FF decoded as a CHARACTER -- not a raw byte
            # round-tripped through surrogateescape, which the branch above
            # already took. The codex audit on #1110 found this emitting ONE
            # octal escape here, which conflates "the codepoint fits in a byte"
            # with "the character IS one byte". It is not: the firmware source
            # is UTF-8, so XC32 compiles 'e'-acute from the TWO bytes
            # \303\251, while \351 is one. The fixture was therefore SHORTER
            # than the shipping message -- 3 bytes short for three such
            # characters -- so run_1039_tests measured a different, smaller
            # string and passed its survival checks on it.
            #
            # Emit the character's real UTF-8 bytes, so the generated literal
            # encodes byte-for-byte what the compiler sees.
            out.extend(f"\\{b:03o}" for b in ch.encode("utf-8"))
        else:
            raise GenError(
                f"message contains the non-byte codepoint {ch!r} (U+"
                f"{cp:04X}), which this generator will not re-emit blind -- "
                f"decide what it should become and extend c_literal()")
    literal = '"' + "".join(out) + '"'

    # THE INVARIANT, ASSERTED RATHER THAN ASSUMED. The whole point of this
    # fixture is to measure the SHIPPING message against Logger's byte ceiling,
    # so a literal that encodes a different number of bytes than the firmware
    # string measures the wrong thing -- and does it silently, by passing.
    # That is what the UTF-8 defect above did. `surrogateescape` is what turns
    # round-tripped raw bytes back into the real byte sequence, so this is the
    # firmware's own byte count, not a codepoint count.
    want = len(text.encode("utf-8", "surrogateescape"))
    got = c_literal_byte_len(literal)
    if got != want:
        raise GenError(
            f"c_literal() changed the message's BYTE LENGTH: the literal "
            f"encodes {got} byte(s) but the message is {want}. A fixture of a "
            f"different length measures a different string against the "
            f"ceiling. Message: {text!r}")
    return literal


GEN_SELF_TESTS = [
    # (name, callable, expected, why) -- expected may be the exception class.
    ("splice: phase 2 runs before escapes are read",
     lambda: splice_continuations('x = "\\x2\\\n5.200u";'),
     'x = "\\x25.200u";',
     "C deletes backslash-newline in translation phase 2, BEFORE tokens are "
     "formed, so the compiler sees %.200u (240 formatted bytes) where an "
     "unspliced read sees 0x02 then a literal '5.200u' and accepts the old "
     "two-conversion signature"),
    ("splice: CRLF continuations too",
     lambda: splice_continuations('a\\\r\nb'), 'ab',
     "a CRLF source must splice the same way, or the check is "
     "platform-dependent"),
    ("splice: a lone backslash is untouched",
     lambda: splice_continuations('"a\\\\b"'), '"a\\\\b"',
     "an escaped backslash is not a continuation"),
    # The clamp reader is the gate's paren-depth walk, reached through the one
    # seam in _reserved_after(). These cases run the SHARED code, so they also
    # pin that the import still works and still raises GenError.
    ("clamp: every term is summed",
     lambda: _clamp_of("size = min(LOG_MESSAGE_SIZE - 2 - 10, size);"), 12,
     "the prefix defect: '- 2 - 10' read as 2 measured the fixture against a "
     "ceiling 10 bytes too generous"),
    ("clamp: the shipping shape still reads 3",
     lambda: _clamp_of("size = min((LOG_MESSAGE_SIZE - 3), size);"), 3,
     "what Logger.c:405 actually says today -- this must not move while the "
     "reader underneath it changes"),
    # ⛔ r4 FINDING 3. The regex this replaced stopped at the first `)` and
    # returned 3 here. 13 is the real reservation, and the 10-byte gap went
    # straight into test_1039's LOG_BUDGET as a ceiling the device lacks.
    ("clamp: arithmetic AFTER the closing paren is counted",
     lambda: _clamp_of("size = min((LOG_MESSAGE_SIZE - 3) - 10, size);"), 13,
     "r4 both legs: `LOG_MESSAGE_SIZE([^,)]*)` read this as 3. The gate had "
     "already fixed exactly this pattern in the same PR; the generator kept "
     "the broken twin, so the fix and the defect shipped together"),
    ("clamp: a parenthesised term after a minus REFUSES",
     lambda: _clamp_of("size = min(LOG_MESSAGE_SIZE - (3 - 1), size);"),
     GenError,
     "`A - (b - c)` is not `A - b - c`; flattening it would flip c's sign, so "
     "the only safe reading is to refuse. Inherited from the gate's walk "
     "rather than re-argued here"),
    ("clamp: a macro term refuses",
     lambda: _clamp_of("size = min(LOG_MESSAGE_SIZE - PAD, size);"), GenError,
     "an expression this reader cannot account for must stop the build, not "
     "contribute the part it recognises"),
    ("clamp: an addition refuses",
     lambda: _clamp_of("size = min(LOG_MESSAGE_SIZE - 2 + 1, size);"),
     GenError, "same, other direction"),
    ("clamp: nothing subtracted refuses",
     lambda: _clamp_of("size = min(LOG_MESSAGE_SIZE, size);"), GenError,
     "a reservation of zero is a reading, not a default"),
    ("clamp: no clamp at all refuses by NAME",
     lambda: _clamp_of("size = other(LOG_MESSAGE_SIZE - 3, size);"), GenError,
     "the mirror models a min() clamp; if that shape is gone the build must "
     "stop rather than extract a number from whatever replaced it"),
    # ⛔ r5 FINDING 1: an unparsed '%' is a wild store, not a cosmetic gap.
    ("signature: %n is REFUSED, not silently dropped",
     lambda: conversion_signature("clock %n is %u Hz, pll %u", "t"), GenError,
     "r5, memory_safety: SPEC_RE excludes 'n', so finditer skipped it and the "
     "format still matched the recorded ['%u','%u']. The renderer supplied "
     "two UINT32_MAX values and snprintf wrote through the first as %n's "
     "int* -- the audit reproduced a SIGSEGV"),
    ("signature: a clean format is unchanged",
     lambda: conversion_signature("clock %u Hz, pll %u", "t"), ["%u", "%u"],
     "the shape that ships -- the refusal must not cost the ordinary case"),
    ("signature: %% consumes no argument and is not in the signature",
     lambda: conversion_signature("100%% of %u", "t"), ["%u"],
     "parsed, so it does not trip the refusal, but excluded from the "
     "signature because it takes no argument"),
    ("signature: a length modifier is preserved",
     lambda: conversion_signature("%llu and %u", "t"), ["%llu", "%u"],
     "'ll' widens on this target, so losing it would measure the OLD worst "
     "case -- the reason the signature check exists at all"),
    ("signature: a trailing bare %% is refused",
     lambda: conversion_signature("value %u pct %", "t"), GenError,
     "a '%' at end-of-string parses as nothing; dropping it silently is the "
     "same hole as %n with a different ending"),
    # ⛔ r5 FINDING 2. The historical expression in a comment WINS a plain
    # search, and this module used to run one over unmasked source.
    ("clamp: a commented-out PREVIOUS clamp does not win",
     lambda: _clamp_of(
         "    size = min((LOG_MESSAGE_SIZE - 13), size);",
         extra="    /* Previously: size = min((LOG_MESSAGE_SIZE - 3), size); */"),
     13,
     "r5 both legs: unmasked, this read 3 while the gate read 13 -- so the "
     "fixture accepted 119/120-byte messages against 125 while the firmware "
     "truncated them at 115. It disproved r4's claim that sharing the walk "
     "had ended ceiling-reader divergence: the shared walk was being fed "
     "different text on each side"),
    ("clamp: a commented-out clamp ALONE refuses rather than being read",
     lambda: _clamp_of(
         "    size = size;",
         extra="    /* Previously: size = min((LOG_MESSAGE_SIZE - 3), size); */"),
     GenError,
     "the other direction, and the one that matters if the clamp is ever "
     "removed: with the live clamp gone the masked text has none, so this "
     "must STOP rather than quietly adopt the historical 3"),
    ("c_literal: UTF-8 keeps its byte length",
     lambda: c_literal("\u00e9"), '"\\303\\251"',
     "e-acute is TWO bytes in a UTF-8 source; one octal escape made the "
     "fixture shorter than the shipping message"),
    ("c_literal: a round-tripped raw byte stays ONE byte",
     lambda: c_literal(chr(0xDCC3)), '"\\303"',
     "the surrogateescape path must not be re-encoded as two bytes"),
]


def self_test():
    """Pin the extraction invariants. Run from the Makefile recipe."""
    failures = 0
    for name, fn, expected, why in GEN_SELF_TESTS:
        if isinstance(expected, type) and issubclass(expected, Exception):
            try:
                got = fn()
            except expected:
                continue
            except Exception as e:            # the wrong exception is a failure
                failures += 1
                print(f"  FAIL [{name}]: raised {type(e).__name__}, expected "
                      f"{expected.__name__} -- {why}")
                continue
            failures += 1
            print(f"  FAIL [{name}]: returned {got!r}, expected "
                  f"{expected.__name__} -- {why}")
            continue
        try:
            got = fn()
        except Exception as e:
            failures += 1
            print(f"  FAIL [{name}]: raised {type(e).__name__}: {e} -- {why}")
            continue
        if got != expected:
            failures += 1
            print(f"  FAIL [{name}]: {got!r}, expected {expected!r} -- {why}")
    # The imported gate must be a declared build input in BOTH places. Run
    # here rather than as a table case because it reads the repo rather than a
    # fixture, and because the Makefile recipe calls --self-test before
    # generating, so a dropped edge fails the build at the point it matters.
    dep_problems = dependency_registration_problems()
    for problem in dep_problems:
        failures += 1
        print(f"  FAIL [dependency registration]: {problem}")
    if failures:
        print(f"ERROR: gen_1039_log_budget.py: {failures} self-test(s) failed",
              file=sys.stderr)
        return 1
    # The dependency check is named in the clean verdict too. A pass that
    # mentions only the case count reads as "20 things were checked", and the
    # registration edge -- the one that was silently missing -- would not be
    # among them as far as any reader could tell.
    print(f"gen_1039_log_budget.py self-test: {len(GEN_SELF_TESTS)}/"
          f"{len(GEN_SELF_TESTS)} extraction-invariant cases pass; the "
          f"imported ceiling reader ({GATE_PY.name}) is a declared "
          f"prerequisite and is in every host-tests.yml paths: filter")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--out", type=Path, default=Path("gen_1039_log_budget.h"))
    ap.add_argument("--self-test", action="store_true",
                    help="check the extraction invariants and exit")
    args = ap.parse_args()

    if args.self_test:
        return self_test()

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
