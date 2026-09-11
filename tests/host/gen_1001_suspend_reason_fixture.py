#!/usr/bin/env python3
"""Generate the #1001 length-guard fixture for test_953_bench_suspend_diagnosis.c.

WHY THIS EXISTS AT ALL

`SD_SuspendReasonText()` (firmware/src/services/SCPI/SCPIStorageSD.c) returns
strings that are interpolated into `LOG_E` format strings, and Logger cuts a
formatted line at `LOG_MESSAGE_SIZE - 3` characters. #986 was a reason that did
not fit: 76 characters is what it is today, and at its previous length the line
was cut mid-command, losing `SYST:STOR:SD:ENAble 1` from the message whose only
job was to name it.

Three regression guards were attempted in PR #983 and each was defeated in
review. All three failed the same way -- they pinned something *about* the
strings instead of measuring the strings:

  * `grep -qF 'then SYST:STOR:SD:ENAble 1'` on the source. The matched words
    are a substring of the 94-character string they replaced AND of the comment
    explaining the replacement, so the grep passed for exactly the state it
    existed to catch.
  * sha256 of the function's text, through a pipeline that ran
    `tr -s '[:space:]' ' '` -- which collapses whitespace INSIDE the literals,
    so a reason respaced in its own text left the hash unchanged.
  * the test measuring its own copies of the strings against a `-D` limit. The
    copies drift from the firmware, which is what the first two existed to
    prevent.

So this script measures the real thing. Nothing downstream of it is a copy:
the reason strings, the format strings they are interpolated into, the command
mnemonics substituted alongside them, and the character ceiling itself are all
read out of the firmware sources named in SOURCES below and emitted as a C
header the test includes.

WHAT IT REFUSES TO DO

It never guesses. Every parse below either matches what it expects or exits
non-zero with a message naming the file, the line and what a human has to
re-derive. That is the whole design: a guard that silently measures nothing is
how this class of check goes quiet, which is what the three attempts above did
in three different ways. In particular it fails -- rather than emitting an
empty or partial fixture -- when:

  * `SD_SuspendReasonText()` cannot be found, or its body will not brace-match.
  * a `return` inside it is any shape other than `NULL` or a run of adjacent
    string literals (e.g. `return g_reason;`), because that return's real
    length is then not knowable from the text.
  * the number of returned literals is not EXPECTED_REASON_COUNT. Both
    directions fail: a new reason needs measuring, and a removed one means the
    call-site enumeration below was written against a different function.
  * the number of `LOG_E` call sites interpolating the reason is not
    EXPECTED_SITE_COUNT.
  * a format string at such a site holds a conversion other than exactly one
    or two `%s` (a `%d` has no length bound without knowing the value).
  * a `cmd` mnemonic reaching such a site cannot be enumerated -- including the
    case where it arrives through a forwarding wrapper whose own callers cannot
    be resolved (see cmd_sets()).
  * `LOG_MESSAGE_SIZE`, or either of the two reservations in Logger.c that
    together set the surviving-character count, will not parse.

WHY IT LIVES IN tests/host/ AND NOT tools/lint/

It imports tools/lint/check_log_ascii.py, so tools/lint/ would be the obvious
home. It is here instead because .github/workflows/host-tests.yml triggers on
`tests/host/**` and NOT on `tools/lint/**`: a generator under tools/lint/ could
be edited by a PR that never queues the job it feeds. That is not hypothetical
-- the same workflow already carries a comment block about a host test whose
source was not in its own `paths:` list, so a PR touching only that file never
ran it.

Usage:
    python3 gen_1001_suspend_reason_fixture.py [--out PATH] [--report]
Exit codes:
    0 = header written, 1 = a parse assumption failed, 2 = usage/IO error
"""
import argparse
import re
import sys
from pathlib import Path

# tools/lint/check_log_ascii.py owns the string-literal scanner this file
# depends on. It is imported rather than reimplemented: it is a scanner and not
# a regex precisely because a regex matched the span between two unrelated
# quote characters in prose, producing 14 bogus "literals" out of 21 candidates
# when it was written. mask_code() below mirrors its structure to blank
# comments as well, and cross-checks itself against it -- see mask_code().
HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent
sys.path.insert(0, str(REPO / "tools" / "lint"))
try:
    from check_log_ascii import string_literals, has_line_splice
except ImportError as exc:  # pragma: no cover - environment problem, not drift
    print(f"ERROR: cannot import tools/lint/check_log_ascii.py: {exc}",
          file=sys.stderr)
    sys.exit(2)

SD_SRC = REPO / "firmware" / "src" / "services" / "SCPI" / "SCPIStorageSD.c"
LOGGER_H = REPO / "firmware" / "src" / "Util" / "Logger.h"
LOGGER_C = REPO / "firmware" / "src" / "Util" / "Logger.c"
SOURCES = (SD_SRC, LOGGER_H, LOGGER_C)

DEFAULT_OUT = HERE / "gen_1001_suspend_reasons.h"

REASON_FUNC = "SD_SuspendReasonText"
REASON_PTR = "why"          # the local the call sites interpolate

# Tripwires, not auto-adapting counts. A mismatch in EITHER direction means the
# downstream budget table was derived against a different firmware than the one
# being built, so the human is told to re-derive rather than handed a fixture
# that measures a subset.
EXPECTED_REASON_COUNT = 3
EXPECTED_NULL_RETURNS = 1
EXPECTED_SITE_COUNT = 4


class Drift(Exception):
    """A parse assumption failed. Carries the operator-facing message."""


# ---------------------------------------------------------------------------
# Source masking
# ---------------------------------------------------------------------------
def mask_code(src):
    """Return (masked, literals): `src` with comments and string/char literal
    bodies blanked to spaces, length and line structure preserved.

    Every structural scan below (brace matching, `return` and `;` location,
    identifier and call finding) runs on the mask, so a brace, semicolon or
    keyword inside a comment or a string cannot be mistaken for code -- and
    because the mask is the same length as the source, an offset found in it
    indexes the real text directly.

    This mirrors check_log_ascii.string_literals()'s walk rather than calling
    it, because that generator reports literals and not the comment spans a
    mask also needs. The two are then cross-checked against each other, so a
    future fix to the shared scanner that this mirror does not track fails the
    build instead of silently changing what gets measured.

    The cross-check is worth exactly what it says and no more: the two agree
    ON THIS FILE'S TEXT. It is a same-input comparison, so a scanner change
    that happens not to matter for anything SCPIStorageSD.c contains passes it
    -- measured, not assumed: disabling the shared scanner's char-literal arm
    left both lists identical (nothing in this file is a char literal holding
    a quote), while disabling its block-comment arm diverged them 95 vs 127
    and failed the build. That is the right sensitivity for a fixture
    generator, whose only job is to read THIS file correctly; it is not a
    conformance test of the shared scanner.
    """
    out = []
    lits = []
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
            lits.append((i, src[i:j]))
            out.append(blank(src[i:j]))
            i = j
        elif c == "'":
            j = i + 1
            while j < n:
                if src[j] == "\\":
                    j += 2
                    continue
                if src[j] == "'":
                    j += 1
                    break
                j += 1
            out.append(blank(src[i:j]))
            i = j
        elif src.startswith("//", i):
            j = src.find("\n", i)
            j = n if j < 0 else j
            out.append(blank(src[i:j]))
            i = j
        elif src.startswith("/*", i):
            j = src.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append(blank(src[i:j]))
            i = j
        else:
            out.append(c)
            i += 1

    masked = "".join(out)
    if len(masked) != len(src):
        raise Drift("internal: mask changed the source length "
                    f"({len(masked)} vs {len(src)})")

    shared = list(string_literals(src))
    if shared != lits:
        raise Drift(
            "the string-literal scan in mask_code() no longer agrees with\n"
            "       tools/lint/check_log_ascii.py's string_literals().\n"
            f"       shared scanner found {len(shared)} literal(s), the mirror "
            f"found {len(lits)}.\n"
            "       mask_code() deliberately mirrors that function so the mask "
            "and the\n"
            "       measured literals cannot disagree. Re-read both before "
            "changing either.")
    return masked, lits


def blank(text):
    """Same length as `text`, all spaces except newlines (kept for line math)."""
    return "".join("\n" if ch == "\n" else " " for ch in text)


def directive_spans(masked):
    """(start, end) of every preprocessor directive, continuations included."""
    spans = []
    pos = 0
    n = len(masked)
    while pos < n:
        eol = masked.find("\n", pos)
        eol = n if eol < 0 else eol
        line = masked[pos:eol]
        if line.lstrip().startswith("#"):
            start = pos
            end = eol
            # A directive continues while its line ends with a backslash.
            while masked[start:end].rstrip().endswith("\\") and end < n:
                nxt = masked.find("\n", end + 1)
                end = n if nxt < 0 else nxt
            spans.append((start, end))
            pos = end + 1
            continue
        pos = eol + 1
    return spans


def structural_mask(src, masked):
    """`masked` with preprocessor directives blanked as well.

    Every structural scan (function spans, `return`s, calls) runs on this
    rather than on `masked`, because a directive is not a statement: the
    declarator scan in find_functions() walks back from a body's `{` to the
    previous top-level `;` or `}`, and a directive carries parentheses without
    a semicolon -- `#define LOG_SD_BUSY(cmd) LOG_E(...)` a few lines above
    SD_SuspendReasonText() is exactly that, and left unblanked it makes the
    declarator scan read the macro's parameter list as the function's.

    The literal list from mask_code() is deliberately NOT filtered the same
    way, so it still agrees with the shared scanner; nothing below looks for
    literals inside a directive.

    The cost of blanking is that a LOG_E written INSIDE a macro definition
    becomes invisible to log_sites(). That is closed rather than accepted: the
    reason pointer must not appear in a directive at all.
    """
    out = list(masked)
    for start, end in directive_spans(masked):
        region = masked[start:end]
        if re.search(r"\b%s\b" % REASON_PTR, region):
            raise Drift(
                f"{SD_SRC.name}:{line_of(src, start)}: a preprocessor directive "
                f"mentions `{REASON_PTR}`.\n"
                "       Directives are blanked before the call sites are "
                "scanned (see\n"
                "       structural_mask), so a LOG_E inside a macro would not be "
                "measured.\n"
                "       Move the logging out of the macro, or extend this "
                "generator to\n"
                "       expand it -- do not leave the site unmeasured.")
        out[start:end] = blank(region)
    return "".join(out)


def line_of(src, off):
    return src.count("\n", 0, off) + 1


# ---------------------------------------------------------------------------
# C string literal decoding
# ---------------------------------------------------------------------------
_SIMPLE_ESCAPES = {
    "n": "\n", "r": "\r", "t": "\t", "a": "\a", "b": "\b", "f": "\f",
    "v": "\v", "\\": "\\", "'": "'", '"': '"', "?": "?",
}


def decode_c_string(lit, where):
    """Decode one C string literal (quotes included) to its runtime characters.

    Hex escapes use C99 6.4.4.4 maximal munch -- "\\x0E2" is ONE byte 0xE2, not
    \\x0E then '2' -- which is the same trap check_log_ascii.escaped_nonascii()
    documents. `\\u`/`\\U` and any unrecognised escape are refused rather than
    guessed: the strings this measures are plain ASCII device text today, and a
    silently mis-decoded escape is a mis-MEASURED string, which is the one
    thing this script exists to prevent.
    """
    if has_line_splice(lit):
        raise Drift(
            f"{where}: a string literal contains a line splice "
            "(backslash-newline).\n"
            "       The compiler joins the lines before tokenizing, so the "
            "literal's real\n"
            "       text cannot be read from unspliced source. Remove the "
            "splice.")
    if not (lit.startswith('"') and lit.endswith('"') and len(lit) >= 2):
        raise Drift(f"{where}: not a complete string literal: {lit!r}")
    body = lit[1:-1]
    out = []
    i, n = 0, len(body)
    while i < n:
        ch = body[i]
        if ch != "\\":
            out.append(ch)
            i += 1
            continue
        i += 1
        if i >= n:
            raise Drift(f"{where}: literal ends with a lone backslash: {lit!r}")
        esc = body[i]
        if esc in _SIMPLE_ESCAPES:
            out.append(_SIMPLE_ESCAPES[esc])
            i += 1
        elif esc in "xX":
            j = i + 1
            while j < n and body[j] in "0123456789abcdefABCDEF":
                j += 1
            if j == i + 1:
                raise Drift(f"{where}: \\x with no hex digits: {lit!r}")
            out.append(chr(int(body[i + 1:j], 16)))
            i = j
        elif esc in "01234567":
            j = i
            while j < n and j < i + 3 and body[j] in "01234567":
                j += 1
            out.append(chr(int(body[i:j], 8)))
            i = j
        else:
            raise Drift(
                f"{where}: unsupported escape '\\{esc}' in {lit!r}.\n"
                "       This decoder refuses escapes it cannot measure exactly "
                "rather than\n"
                "       guess a length. Extend decode_c_string() deliberately "
                "if one is\n"
                "       genuinely needed in device text.")
    return "".join(out)


def c_escape(text):
    """Re-escape decoded text as a C string literal body."""
    out = []
    for ch in text:
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
        elif 0x20 <= ord(ch) <= 0x7E:
            out.append(ch)
        else:
            # Three octal digits, never fewer: "\0" followed by a digit would
            # be re-read as a different (maximal-munch) escape.
            out.append("\\%03o" % ord(ch))
    return "".join(out)


# ---------------------------------------------------------------------------
# Structure: functions, calls, argument lists
# ---------------------------------------------------------------------------
_IDENT_TAIL = re.compile(r"[A-Za-z0-9_]")


class Func:
    __slots__ = ("name", "params", "decl_start", "body_start", "body_end")

    def __init__(self, name, params, decl_start, body_start, body_end):
        self.name = name
        self.params = params
        self.decl_start = decl_start
        self.body_start = body_start      # offset of '{'
        self.body_end = body_end          # offset just past the matching '}'

    def contains(self, off):
        return self.body_start <= off < self.body_end


def find_functions(src, masked):
    """Every top-level `name(params) { ... }` definition, with its span.

    Brace depth is tracked on the mask, so a brace in a comment or a string
    cannot open or close a body. A top-level `{` whose declarator holds no `(`
    (an aggregate initialiser, an enum, a struct) is recorded with no name and
    simply never matches a lookup.
    """
    funcs = []
    depth = 0
    boundary = 0            # start of the current top-level declarator
    open_at = None
    for i, ch in enumerate(masked):
        if ch == "{":
            if depth == 0:
                open_at = i
            depth += 1
        elif ch == "}":
            if depth == 0:
                raise Drift(f"unbalanced '}}' at line {line_of(src, i)}")
            depth -= 1
            if depth == 0:
                decl = masked[boundary:open_at]
                name, params = split_declarator(decl)
                if name:
                    funcs.append(Func(name, params, boundary, open_at, i + 1))
                boundary = i + 1
                open_at = None
        elif depth == 0 and ch == ";":
            boundary = i + 1
    if depth != 0:
        raise Drift("unbalanced braces: the file does not close every body")
    return funcs


def split_declarator(decl_masked):
    """(name, param_identifiers) for a declarator, or (None, ()) if not a
    function definition."""
    open_paren = decl_masked.find("(")
    if open_paren < 0:
        return None, ()
    end = open_paren
    while end > 0 and decl_masked[end - 1].isspace():
        end -= 1
    start = end
    while start > 0 and _IDENT_TAIL.match(decl_masked[start - 1]):
        start -= 1
    name = decl_masked[start:end]
    if not name or name[0].isdigit():
        return None, ()
    close = match_paren(decl_masked, open_paren)
    if close is None:
        return None, ()
    params = tuple(
        m.group(0)
        for m in re.finditer(r"[A-Za-z_][A-Za-z0-9_]*",
                             decl_masked[open_paren + 1:close]))
    return name, params


def match_paren(masked, open_off):
    """Offset of the ')' matching the '(' at `open_off`, or None."""
    depth = 0
    for i in range(open_off, len(masked)):
        if masked[i] == "(":
            depth += 1
        elif masked[i] == ")":
            depth -= 1
            if depth == 0:
                return i
    return None


def iter_calls(masked, pattern):
    """Yield (name, args_start, args_end) for each `IDENT(` matching `pattern`.

    `args_start` is just past the '(' and `args_end` is the offset of the
    matching ')'. Matching runs on the mask, so a call spelled inside a comment
    or a string is not a call.
    """
    for m in re.finditer(pattern, masked):
        name = m.group(1)
        open_paren = masked.index("(", m.end(1))
        close = match_paren(masked, open_paren)
        if close is None:
            raise Drift(f"call to {name}( never closes its parenthesis")
        yield name, open_paren + 1, close


def split_args(masked, start, end):
    """Split an argument list into (start, end) spans at depth-0 commas."""
    spans = []
    depth = 0
    arg_start = start
    for i in range(start, end):
        ch = masked[i]
        if ch in "([{":
            depth += 1
        elif ch in ")]}":
            depth -= 1
        elif ch == "," and depth == 0:
            spans.append((arg_start, i))
            arg_start = i + 1
    spans.append((arg_start, end))
    return spans


def literal_run(src, masked, lits, start, end, where):
    """Decode the adjacent-string-literal run occupying [start, end), or None.

    Returns None when the span holds anything other than string literals,
    comments and whitespace -- so `return g_reason;` is rejected rather than
    silently contributing nothing, and a run split across source lines
    (`"a, " "b"`, which the compiler joins with no extra characters) is
    concatenated exactly as the compiler concatenates it.
    """
    if masked[start:end].strip():
        return None
    inner = [(off, lit) for off, lit in lits if start <= off < end]
    if not inner:
        return None
    return "".join(decode_c_string(lit, where) for _, lit in inner)


def sole_identifier(masked, start, end):
    """The one identifier occupying the span, or None if it is anything else."""
    text = masked[start:end].strip()
    return text if re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", text) else None


# ---------------------------------------------------------------------------
# The three extractions
# ---------------------------------------------------------------------------
def reason_strings(src, masked, lits, funcs):
    """Decode every literal `SD_SuspendReasonText()` can return."""
    target = [f for f in funcs if f.name == REASON_FUNC]
    if len(target) != 1:
        raise Drift(
            f"expected exactly one definition of {REASON_FUNC}() in "
            f"{SD_SRC.name}, found {len(target)}.\n"
            "       If it moved to another file, this generator and the "
            "host-tests.yml\n"
            "       `paths:` list both need re-pointing -- the guard measures "
            "the strings\n"
            "       in THIS file and would otherwise measure nothing.")
    fn = target[0]

    reasons, nulls = [], 0
    for m in re.finditer(r"\breturn\b", masked[fn.body_start:fn.body_end]):
        off = fn.body_start + m.start()
        semi = masked.find(";", fn.body_start + m.end())
        if semi < 0 or semi >= fn.body_end:
            raise Drift(f"{SD_SRC.name}:{line_of(src, off)}: a `return` in "
                        f"{REASON_FUNC}() has no terminating ';'")
        start = fn.body_start + m.end()
        where = f"{SD_SRC.name}:{line_of(src, off)}"
        if sole_identifier(masked, start, semi) == "NULL":
            nulls += 1
            continue
        text = literal_run(src, masked, lits, start, semi, where)
        if text is None:
            raise Drift(
                f"{where}: a `return` in {REASON_FUNC}() is neither NULL nor a "
                "run of\n"
                "       string literals, so its runtime length cannot be read "
                "from the text:\n"
                f"           return {masked[start:semi].strip()!r}\n"
                "       The #1001 guard measures the REAL strings; a returned "
                "variable or\n"
                "       call has to be measured some other way. Re-derive "
                "before proceeding.")
        bad = [ch for ch in text if not 0x20 <= ord(ch) <= 0x7E]
        if bad:
            raise Drift(
                f"{where}: a returned reason holds non-printable or non-ASCII "
                f"characters {bad!r}.\n"
                "       Logger's newline handling inspects the last two "
                "characters of the\n"
                "       formatted line, so a control character in a reason "
                "changes what the\n"
                "       budget below even means. Keep reasons to printable "
                "ASCII.")
        reasons.append((line_of(src, off), text))

    if len(reasons) != EXPECTED_REASON_COUNT or nulls != EXPECTED_NULL_RETURNS:
        raise Drift(
            f"{REASON_FUNC}() now returns {len(reasons)} string literal(s) and "
            f"{nulls} NULL(s);\n"
            f"       this generator was written against "
            f"{EXPECTED_REASON_COUNT} and {EXPECTED_NULL_RETURNS}.\n"
            "       This is a deliberate tripwire and fails in BOTH "
            "directions:\n"
            "         - a NEW reason has to be measured, and the call-site "
            "budget below\n"
            "           has to be re-read to see whether it still fits;\n"
            "         - a REMOVED one means the four call sites this guard "
            "enumerates were\n"
            "           derived against a different function.\n"
            "       Update EXPECTED_REASON_COUNT / EXPECTED_NULL_RETURNS in "
            f"{Path(__file__).name}\n"
            "       once you have re-derived both, and say so in the PR.")
    return reasons


def cmd_sets(src, masked, lits, funcs):
    """Which command mnemonics can reach each refusal helper's `cmd`.

    Every `SD_*(context, ...)` call in the file is examined. A literal second
    argument is a mnemonic that helper is called with directly. An identifier
    second argument that is one of the CALLER's own parameters is a
    forwarding wrapper, so the caller's mnemonics flow through to the callee --
    `SD_ArmOrRefuse(context, cmd, cfg)` reaches
    `SD_ArmOrRefuseWithCleanup(context, cmd, cfg, NULL)` exactly this way, and
    without following it the arm-refusal site would be measured against
    `"FORmat"` (its one direct caller) while five more mnemonics reach it.

    An identifier that is NOT a caller parameter -- a local, a global -- is
    refused: its value is not bounded by anything this script can read, and
    silently ignoring it would loosen the budget rather than tighten it. The
    edges are closed transitively so a chain of wrappers still resolves.
    """
    lits_by_helper = {}
    edges = []                 # (caller_name, callee_name)
    pattern = r"\b(SD_[A-Za-z0-9_]*)\s*\("
    for name, a0, a1 in iter_calls(masked, pattern):
        args = split_args(masked, a0, a1)
        if len(args) < 2:
            continue
        if sole_identifier(masked, *args[0]) != "context":
            continue
        where = f"{SD_SRC.name}:{line_of(src, a0)}"
        s, e = args[1]
        lit_text = literal_run(src, masked, lits, s, e, where)
        if lit_text is not None:
            lits_by_helper.setdefault(name, set()).add(lit_text)
            continue
        ident = sole_identifier(masked, s, e)
        enclosing = next((f for f in funcs if f.contains(a0)), None)
        if ident and enclosing and ident in enclosing.params:
            edges.append((enclosing.name, name))
            continue
        raise Drift(
            f"{where}: {name}(context, ...) is passed a second argument this "
            "script cannot\n"
            f"       bound: {masked[s:e].strip()!r}"
            + (f" (inside {enclosing.name}())" if enclosing else "") + ".\n"
            "       The command mnemonic interpolated next to the suspend "
            "reason sets how\n"
            "       much of the 128-byte log line is left for the reason, so an "
            "unbounded\n"
            "       one makes the budget unknowable. Either pass a literal, or "
            "forward a\n"
            "       parameter of the enclosing function (which is followed), or "
            "extend\n"
            f"       cmd_sets() in {Path(__file__).name} deliberately.")

    # Transitive closure over the forwarding edges.
    resolved = {k: set(v) for k, v in lits_by_helper.items()}
    for caller, callee in edges:
        resolved.setdefault(callee, set())
        resolved.setdefault(caller, set())
    changed = True
    guard = 0
    while changed:
        changed = False
        guard += 1
        if guard > 64:
            raise Drift("forwarding edges between SD_* helpers do not settle "
                        "(a cycle?)")
        for caller, callee in edges:
            before = len(resolved[callee])
            resolved[callee] |= resolved[caller]
            if len(resolved[callee]) != before:
                changed = True
    return resolved


def log_sites(src, masked, lits, funcs, cmds):
    """Every LOG_E() that interpolates the suspend reason, with its prefix cost."""
    sites = []
    for _, a0, a1 in iter_calls(masked, r"\b(LOG_E)\s*\("):
        if not re.search(r"\b%s\b" % REASON_PTR, masked[a0:a1]):
            continue
        args = split_args(masked, a0, a1)
        where = f"{SD_SRC.name}:{line_of(src, a0)}"
        fmt = literal_run(src, masked, lits, args[0][0], args[0][1], where)
        if fmt is None:
            raise Drift(
                f"{where}: a LOG_E() interpolating `{REASON_PTR}` does not have "
                "a string-literal\n"
                "       format argument, so the fixed text around the reason "
                "cannot be\n"
                "       measured. Re-derive before proceeding.")
        convs = re.findall(r"%(.)", fmt)
        if not convs or len(convs) > 2 or any(c != "s" for c in convs):
            raise Drift(
                f"{where}: the format string holds conversions {convs!r}; this "
                "guard measures\n"
                "       formats with exactly one or two `%s` and nothing else. "
                "A numeric or\n"
                "       width-bearing conversion has no length bound without "
                "the value, so\n"
                "       the budget would be a guess:\n"
                f"           {fmt!r}")

        bad = [ch for ch in fmt
               if not (0x20 <= ord(ch) <= 0x7E or ch in "\r\n\t")]
        if bad:
            raise Drift(
                f"{where}: a format string holds non-printable or non-ASCII "
                f"characters {bad!r}.\n"
                "       tools/lint/check_log_ascii.py gates non-ASCII in device "
                "text repo-wide,\n"
                "       so this should be unreachable -- it is refused here "
                "rather than\n"
                "       emitted, because the header would otherwise carry an "
                "escape this\n"
                "       generator cannot spell correctly in C.")

        enclosing = next((f for f in funcs if f.contains(a0)), None)
        if enclosing is None:
            raise Drift(f"{where}: cannot find the function enclosing this "
                        "LOG_E() call")

        candidates = set()
        if len(convs) == 2:
            # First %s is the mnemonic, second is the reason -- asserted by the
            # order of the call's own arguments, not assumed: args[1] is the
            # mnemonic and the reason pointer appears after it.
            mnemonic_arg = sole_identifier(masked, *args[1])
            if mnemonic_arg is None or mnemonic_arg == REASON_PTR:
                raise Drift(
                    f"{where}: a two-`%s` format's first substituted argument is "
                    f"{masked[args[1][0]:args[1][1]].strip()!r},\n"
                    f"       not a command mnemonic, so which `%s` takes the "
                    "reason is no longer\n"
                    "       clear. Re-derive before proceeding.")
            candidates = cmds.get(enclosing.name) or set()
            if not candidates:
                raise Drift(
                    f"{where}: {enclosing.name}() interpolates a command "
                    "mnemonic next to the\n"
                    "       reason, but no mnemonic could be traced to it. "
                    "Without the longest\n"
                    "       one the reason's budget is unknown. See cmd_sets().")
            worst = max(sorted(candidates), key=len)
        else:
            worst = ""

        # Formatted length with the reason removed: the literal's own characters
        # (which already include the trailing \r\n) less the two characters of
        # each `%s`, plus the mnemonic actually substituted.
        prefix_len = len(fmt) - 2 * len(convs) + len(worst)
        sites.append({
            "func": enclosing.name,
            "line": line_of(src, a0),
            "format": fmt,
            "worst_cmd": worst,
            "candidates": sorted(candidates) if len(convs) == 2 else [],
            "prefix_len": prefix_len,
        })

    if len(sites) != EXPECTED_SITE_COUNT:
        raise Drift(
            f"found {len(sites)} LOG_E() call site(s) interpolating "
            f"`{REASON_PTR}` in {SD_SRC.name};\n"
            f"       this generator was written against {EXPECTED_SITE_COUNT}.\n"
            "       A NEW site may have a tighter prefix than every existing "
            "one, which\n"
            "       would silently lower the ceiling the reasons are held to; a "
            "REMOVED one\n"
            "       means the binding site may have gone away. Re-derive the "
            "budget, then\n"
            f"       update EXPECTED_SITE_COUNT in {Path(__file__).name}.\n"
            "       NOTE: sites in OTHER files are deliberately out of scope -- "
            "see #1000\n"
            "       for SCPIInterface.c, whose prefixes no reason can satisfy.")
    return sites


def logger_limit():
    """The number of formatted characters that survive LogMessageFormatImpl().

    Both reservations are read, and the tighter wins, because they are two
    independent truncations of the same line (Logger.c):

        size = vsnprintf(buffer, LOG_MESSAGE_SIZE - R, format, args);
        ...
        size = min((LOG_MESSAGE_SIZE - C), size);

    vsnprintf writes at most (LOG_MESSAGE_SIZE - R) - 1 characters before its
    NUL, and the clamp then caps what is kept at LOG_MESSAGE_SIZE - C. With
    R=2 and C=3 on a 128-byte buffer the two agree at 125; taking the min means
    a change to either one is followed rather than assumed away.
    """
    h = LOGGER_H.read_text(encoding="utf-8", errors="surrogateescape")
    m = re.search(r"^[ \t]*#[ \t]*define[ \t]+LOG_MESSAGE_SIZE[ \t]+(\d+)",
                  h, re.M)
    if not m:
        raise Drift(
            f"cannot find `#define LOG_MESSAGE_SIZE <integer>` in "
            f"{LOGGER_H.name}.\n"
            "       That constant is the whole budget; this guard reads it "
            "rather than\n"
            "       carrying a copy, and refuses to invent one.")
    size = int(m.group(1))

    c = LOGGER_C.read_text(encoding="utf-8", errors="surrogateescape")
    m = re.search(r"vsnprintf\s*\(\s*buffer\s*,\s*LOG_MESSAGE_SIZE\s*-\s*(\d+)"
                  r"\s*,\s*format\s*,\s*args\s*\)", c)
    if not m:
        raise Drift(
            f"cannot find `vsnprintf(buffer, LOG_MESSAGE_SIZE - N, format, "
            f"args)` in {LOGGER_C.name}.\n"
            "       That call's reservation is half of the surviving-character "
            "arithmetic\n"
            "       the reason lengths are measured against. If the formatting "
            "moved or\n"
            "       changed shape, re-derive logger_limit() -- do not relax the "
            "pattern to\n"
            "       make the build pass.")
    reserve = int(m.group(1))

    m = re.search(r"size\s*=\s*min\s*\(\s*\(?\s*LOG_MESSAGE_SIZE\s*-\s*(\d+)"
                  r"\s*\)?\s*,\s*size\s*\)", c)
    if not m:
        raise Drift(
            f"cannot find the `size = min((LOG_MESSAGE_SIZE - N), size)` clamp "
            f"in {LOGGER_C.name}.\n"
            "       It is the other half of the surviving-character "
            "arithmetic. Re-derive\n"
            "       logger_limit() rather than relaxing the pattern.")
    clamp = int(m.group(1))

    surviving = min(size - reserve - 1, size - clamp)
    if surviving <= 0:
        raise Drift(f"derived a non-positive surviving-character count "
                    f"({surviving}) from {LOGGER_H.name}/{LOGGER_C.name}")
    return size, reserve, clamp, surviving


# ---------------------------------------------------------------------------
# Emit
# ---------------------------------------------------------------------------
def render(reasons, sites, limit):
    size, reserve, clamp, surviving = limit
    worst = max(sites, key=lambda s: s["prefix_len"])
    me = Path(__file__).name
    out = []
    w = out.append
    w("/* GENERATED -- do not edit by hand. Regenerated by %s" % me)
    w(" * from firmware/src/services/SCPI/SCPIStorageSD.c,")
    w(" *      firmware/src/Util/Logger.h and firmware/src/Util/Logger.c.")
    w(" *")
    w(" * Issue #1001. Everything here is READ from those sources, never copied:")
    w(" * the reason strings SD_SuspendReasonText() returns, the LOG_E format")
    w(" * strings that interpolate them, the longest command mnemonic that can")
    w(" * reach each one, and the character ceiling Logger truncates at. Three")
    w(" * earlier guards pinned properties OF these strings (a grep for their own")
    w(" * words, a whitespace-collapsing hash, a copy in the test) and each was")
    w(" * defeated; this file exists so the test measures the strings themselves.")
    w(" *")
    w(" * Derivation of the ceiling (Logger.c, LogMessageFormatImpl):")
    w(" *   LOG_MESSAGE_SIZE                      = %d" % size)
    w(" *   vsnprintf(buffer, LOG_MESSAGE_SIZE-%d) -> at most %d chars written"
      % (reserve, size - reserve - 1))
    w(" *   min((LOG_MESSAGE_SIZE-%d), size)       -> at most %d chars kept"
      % (clamp, size - clamp))
    w(" *   surviving characters                  = min of the two = %d"
      % surviving)
    w(" */")
    w("#ifndef GEN_1001_SUSPEND_REASONS_H")
    w("#define GEN_1001_SUSPEND_REASONS_H")
    w("")
    w("#define GEN_1001_LOG_MESSAGE_SIZE        %d" % size)
    w("#define GEN_1001_LOG_VSNPRINTF_RESERVE   %d" % reserve)
    w("#define GEN_1001_LOG_CLAMP_RESERVE       %d" % clamp)
    w("/* Formatted characters that survive a LOG_E line, CRLF included. */")
    w("#define GEN_1001_LOG_SURVIVING_CHARS     %d" % surviving)
    w("")
    w("/* SD_SuspendReasonText()'s literal returns, in source order. */")
    w("#define GEN_1001_REASON_COUNT            %d" % len(reasons))
    w("static const char *const kGen1001Reasons[GEN_1001_REASON_COUNT] = {")
    for line, text in reasons:
        w("    /* SCPIStorageSD.c:%d -- %d characters */" % (line, len(text)))
        w('    "%s",' % c_escape(text))
    w("};")
    w("")
    w("/* Each LOG_E() in SCPIStorageSD.c that interpolates the reason.")
    w(" *")
    w(" * `prefixLen` is the formatted line's length with the reason removed and")
    w(" * `worstCmd` substituted -- the format's own characters (trailing CRLF")
    w(" * included) less two per %s, plus the mnemonic. The test recomputes it")
    w(" * from `format` and `worstCmd` rather than trusting the number.")
    w(" *")
    w(" * Sites in other files are out of scope: SCPIInterface.c's")
    w(" * SCPI_StartStreamingClaimed has an 88-character prefix that no reason can")
    w(" * satisfy, which is #1000 and needs that prefix shortened, not these")
    w(" * strings shortened further.")
    w(" */")
    w("#define GEN_1001_SITE_COUNT              %d" % len(sites))
    w("typedef struct {")
    w("    const char *func;      /* enclosing function in SCPIStorageSD.c */")
    w("    int         line;      /* line of the LOG_E call */")
    w("    const char *format;    /* the real format literal, decoded */")
    w("    const char *worstCmd;  /* longest mnemonic reaching it, \"\" if none */")
    w("    int         prefixLen; /* formatted chars other than the reason */")
    w("} Gen1001Site;")
    w("")
    w("static const Gen1001Site kGen1001Sites[GEN_1001_SITE_COUNT] = {")
    for s in sites:
        if s["candidates"]:
            w("    /* mnemonics reaching it: %s */"
              % ", ".join('"%s"' % c for c in s["candidates"]))
        w('    { "%s", %d, "%s", "%s", %d },'
          % (s["func"], s["line"], c_escape(s["format"]),
             c_escape(s["worst_cmd"]), s["prefix_len"]))
    w("};")
    w("")
    w("/* The binding site: the longest prefix, hence the least room for a")
    w(" * reason. %s (SCPIStorageSD.c:%d) leaves %d characters."
      % (worst["func"], worst["line"], surviving - worst["prefix_len"]))
    w(" */")
    w("#define GEN_1001_WORST_PREFIX_LEN        %d" % worst["prefix_len"])
    w("#define GEN_1001_REASON_BUDGET           %d"
      % (surviving - worst["prefix_len"]))
    w("")
    w("#endif /* GEN_1001_SUSPEND_REASONS_H */")
    return "\n".join(out) + "\n"


def report(reasons, sites, limit):
    size, reserve, clamp, surviving = limit
    print("LOG_MESSAGE_SIZE=%d  vsnprintf reserve=%d  clamp reserve=%d  "
          "surviving chars=%d" % (size, reserve, clamp, surviving))
    print()
    print("%-32s %6s %-11s %7s %7s" %
          ("call site (SCPIStorageSD.c)", "line", "worst cmd", "prefix",
           "budget"))
    for s in sorted(sites, key=lambda x: -x["prefix_len"]):
        print("%-32s %6d %-11s %7d %7d" %
              (s["func"][:32], s["line"], s["worst_cmd"] or "-",
               s["prefix_len"], surviving - s["prefix_len"]))
    budget = surviving - max(s["prefix_len"] for s in sites)
    print()
    print("binding budget for every reason: %d characters" % budget)
    print()
    for line, text in reasons:
        print("  line %-5d len=%-4d headroom=%-4d %r"
              % (line, len(text), budget - len(text), text))


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--out", type=Path, default=DEFAULT_OUT,
                    help="header to write (default: %s)" % DEFAULT_OUT.name)
    ap.add_argument("--report", action="store_true",
                    help="also print the derived budget table")
    args = ap.parse_args()

    for p in SOURCES:
        if not p.is_file():
            print(f"ERROR: source not found: {p}", file=sys.stderr)
            return 2

    try:
        src = SD_SRC.read_text(encoding="utf-8", errors="surrogateescape")
    except OSError as exc:
        print(f"ERROR: cannot read {SD_SRC}: {exc}", file=sys.stderr)
        return 2

    try:
        masked, lits = mask_code(src)
        code = structural_mask(src, masked)
        funcs = find_functions(src, code)
        reasons = reason_strings(src, code, lits, funcs)
        cmds = cmd_sets(src, code, lits, funcs)
        sites = log_sites(src, code, lits, funcs, cmds)
        limit = logger_limit()
        text = render(reasons, sites, limit)
    except Drift as exc:
        print("ERROR: %s: %s" % (Path(__file__).name, exc), file=sys.stderr)
        print("       The #1001 length guard measures the real strings, so it "
              "refuses to\n"
              "       emit a fixture it cannot derive. Nothing was written.",
              file=sys.stderr)
        return 1

    try:
        args.out.write_text(text, encoding="ascii")
    except OSError as exc:
        print(f"ERROR: cannot write {args.out}: {exc}", file=sys.stderr)
        return 2

    if args.report:
        report(reasons, sites, limit)
    return 0


if __name__ == "__main__":
    sys.exit(main())
