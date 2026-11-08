#!/usr/bin/env python3
"""Pin that CONF:ADC:SAVEcal stays behind the streaming claim guard (#885).

#885 converted five ADC setters that mutated live conversion inputs with no
guard. Its own pre-merge audit then found SAVEcal / SAVEFcal: the read side
of the same cal store, and the last commands that MUTATE PERSISTENT STATE
from CalM/CalB with no claim at all. Both route through CalSaveCommon, which
takes the claim, uses its verdict, and releases it once on the single path
out.

Replaces the device leg deleted from ts#504 by ruling FT A.

⛔ WHOLE BODIES, NOT FRAGMENTS, and this is round 3 of learning that lesson.
An earlier version of this pin compared exact FRAGMENTS -- the claim
statement, the verdict text, the rejection call, the release count -- and
r1's audit found four defects living in the control flow BETWEEN them, all
passing with every fragment intact:

  * `if (type == DaqifiSettings_UserAInCalParams)` inserted before the reject
    return, so SAVEFcal writes NVM unclaimed while streaming;
  * `if (claim != STREAM_CFG_CLAIM_OK); {` -- a stray semicolon making the
    rejection unconditional and the claim never released;
  * `return result;` moved ABOVE the release, leaving it unreachable and the
    claim held forever (counting the release text does not establish the
    path);
  * `return SCPI_RES_OK;` at the top of SCPI_ADCCalSave, reporting success
    without saving, with the helper's name still present below it.

fw#1221 learned the same thing one level down: recognising a shape loses to
an open-ended mutant space, so it pinned expressions exactly. Fragments have
the same problem one level up -- the connective tissue is unpinned. So the
unit of pinning here is the unit of behaviour: THE ENTIRE BODY of each
function on the path, compared exactly.

Any edit to these functions fails the build, INCLUDING A CORRECT ONE. That is
the point: a deliberate change updates the canonical text in the same PR, and
that diff is the review moment. Do not relax this into matching.

⛔ WHY FUNCTION-SCOPED, measured rather than assumed. At 61a631745^ -- the
revision before the #885 fix -- `Streaming_BeginConfigChange` appears TEN
times in SCPIADC.c, because other setters already used it, while
CalSaveCommon did not exist at all. So a file-scoped grep for the guard
PASSES ON THE BROKEN REVISION and says nothing about this command.

⚠ PREPROCESSOR LINE DIRECTIVES ARE STRIPPED. `cc -fpreprocessed -E` emits
`# <line> "<file>"` markers where comments were removed -- 47 of them in this
file. Leaving them in would embed LINE NUMBERS in the canonical text, so
adding a comment hundreds of lines earlier would fail the build. That is a
correct change breaking the pin, which is the one failure mode worth
engineering out.

⛔ AND IT NEITHER EVALUATES `#if` NOR EXPANDS MACROS, which rounds 3 and 4
are both about. Exact text kept not being enough, because the checker and the
compiler were reading different copies of it, or the same text with different
meanings:

  * r3: `-fpreprocessed` keeps BOTH branches of a conditional, so wrapping the
    real body in `#if 0` and compiling a no-op `#else` left every pinned
    string present and passing. Hence `depth_map` and defined-exactly-once.
  * r4: it also DELETES `#define`/`#undef` without expanding the invocations,
    so `#define Streaming_EndConfigChange() ((void)0)` inside CalSaveCommon,
    or `#define CalSaveCommon(...) SCPI_RES_OK` around the wrappers, left the
    bodies byte-identical while the build did something else. Hence
    `blank_comments`, the directive scan, and `macro_sensitive_identifiers`.
  * r4 also: a row can be exact, singular and at depth 0 and still not
    dispatch, because it sits in an array nothing hands to SCPI_Init. Hence
    `active_command_table`.

⛔⛔ SO HERE IS THE BOUNDED CLAIM, replacing "this checker reads what the
compiler compiles" -- a sentence every round so far has falsified, and which
should not be written here again.

WHAT THIS GUARANTEES, over the text of these TWO FILES ONLY:

  * the three bodies on the path are byte-exact against the canonical strings;
  * each of the three is defined exactly once, at conditional depth 0, and
    contains no preprocessor directive;
  * neither file `#define`s any identifier the canonical text names outside a
    string literal;
  * both table rows are byte-exact, singular, at conditional depth 0, and
    inside the initialiser of the array that THIS file passes to SCPI_Init.

WHAT IT DOES NOT COVER, and nobody should read it as covering:

  * macros from INCLUDED HEADERS. This reads two .c files and no headers, so a
    `#define Streaming_EndConfigChange() ((void)0)` in a header still passes
    here. Closing that needs a real preprocess, which needs XC32 and the
    project's include path and -D flags, and is a build-side check rather than
    a host check.
  * anything only a real preprocess would show: include order, conditional
    compilation selected by build configuration, NQ1/NQ2/NQ3 variant defines.
  * ANOTHER table row shadowing a short form of these commands --
    `{.pattern = "CONF:ADC:SAVE", ...}` placed earlier dispatches instead,
    because findCommandHeader takes the first match. That is NOT checked here
    because it is already covered: the #907 gate in
    `tools/lint/scpi_wiki_sync.py` rejects two registered patterns that accept
    the same spelling, runs in CI on any `firmware/src/services/SCPI/**`
    change, and was verified to fire on exactly this row -- rc=0 clean, rc=1
    with it, naming `CONF:ADC:SAVE <-> CONFigure:ADC:SAVEcal`.
  * anything outside these files: a different translation unit, link-time
    substitution, or a callback pointer reassigned at runtime.
  * whether the guarded code is CORRECT. This says only that it is UNCHANGED.

Run: python3 tests/host/check_885_savecal_claim_guard.py
"""
import difflib
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
ADC = ROOT / "firmware/src/services/SCPI/SCPIADC.c"
IFACE = ROOT / "firmware/src/services/SCPI/SCPIInterface.c"

# The command table rows that must route to the guarded path.
EXPECTED_REGISTRATIONS = {
    "CONFigure:ADC:SAVEcal":
        '{.pattern = "CONFigure:ADC:SAVEcal", .callback = SCPI_ADCCalSave,},',
    "CONFigure:ADC:SAVEFcal":
        '{.pattern = "CONFigure:ADC:SAVEFcal", .callback = SCPI_ADCCalFSave,},',
}

# The entire body of every function on the path, verbatim with whitespace
# runs collapsed. Updating one of these is a deliberate act that belongs in
# the same PR as the firmware change it describes.
EXPECTED_BODIES = {
    "SCPI_ADCCalSave": """
        return CalSaveCommon(context, DaqifiSettings_UserAInCalParams,
        "CONF:ADC:SAVEcal");
    """,
    "SCPI_ADCCalFSave": """
        return CalSaveCommon(context, DaqifiSettings_FactAInCalParams,
        "CONF:ADC:SAVEFcal");
    """,
    "CalSaveCommon": """
        StreamingCfgClaim claim = Streaming_BeginConfigChange();
        if (claim != STREAM_CFG_CLAIM_OK) {
            return SCPI_RejectCfgClaim(context,
                claim == STREAM_CFG_CLAIM_BUSY, what);
        }
        AInRuntimeArray * pRuntimeAInChannels = BoardRunTimeConfig_Get(
            BOARDRUNTIMECONFIG_AIN_CHANNELS);
        bool saved = daqifi_settings_SaveADCCalSettings(type,
            pRuntimeAInChannels);
        if (type == DaqifiSettings_FactAInCalParams) {
            RefreshFactoryCalPresence();
        }
        scpi_result_t result = saved ? SCPI_RES_OK : SCPI_RES_ERR;
        Streaming_EndConfigChange();
        return result;
    """,
}

fails = []


def check(ok, msg):
    print("  [%s] %s" % ("ok" if ok else "FAIL", msg))
    if not ok:
        fails.append(msg)


def norm(text):
    """Whitespace runs collapsed, ends stripped.

    So reflowing or re-indenting is not a failure, and a CRLF checkout
    normalises to the same string as an LF one -- CI is LF.
    """
    return re.sub(r"\s+", " ", text).strip()


def strip_comments(src, cc="cc"):
    """The file's own text with comments and line directives gone.

    Comment stripping is mandatory, not cosmetic: SCPIADC.c's prose discusses
    the claim, the rejected alternatives and SAVEcal by name at length, so a
    raw scan finds the shapes it is looking for inside the explanations. Done
    with the compiler rather than a regex -- a regex stripper eats pointer
    dereferences, and this file is full of them.
    """
    out = subprocess.run([cc, "-fpreprocessed", "-E", "-"],
                         input=src, capture_output=True, text=True)
    if out.returncode != 0:
        raise RuntimeError("%s -fpreprocessed -E failed: %s"
                           % (cc, out.stderr.strip()[:200]))
    # See the module docstring: these carry line numbers.
    return re.sub(r'(?m)^\s*#\s+\d+\s+".*"\s*$', "", out.stdout)


def blank_comments(src):
    """Comments replaced by spaces; every other byte and every offset kept.

    A SECOND stripper, with a different job from `strip_comments`, and the
    two are not interchangeable:

      * `strip_comments` (the compiler) produces the text that gets COMPARED.
        It also DELETES every preprocessor directive, which is exactly how the
        r3 findings hid -- a `#define` that neuters a call vanishes from the
        comparison while the call text stays put.
      * this one produces the text that gets SCANNED for those directives. It
        must therefore keep them, and it must keep byte offsets so a span
        found here maps back to the real file.

    ⛔ AND LINE GEOMETRY IS NOT PRESERVED BY THE FIRST ONE, measured: SCPIADC.c
    is 2553 lines raw and 1271 after `strip_comments`. The `# <line> "<file>"`
    markers ARE the resync mechanism, so removing them (which the canonical
    text requires -- see above) collapses the mapping with it. Reading a
    stripped-text line number as a raw line number is therefore wrong, and
    was wrong in the first draft of this check. Hence a stripper that cannot
    drift: same length, same newlines, verified on both files.

    Written out rather than regexed because a naive comment regex eats pointer
    dereferences and string contents; this tracks string and char literals and
    their escapes, including a `//` comment continued by a trailing backslash.
    """
    out = list(src)
    i, n = 0, len(src)
    NORMAL, STR, CHR, LINE, BLOCK = range(5)
    st = NORMAL
    while i < n:
        c = src[i]
        nxt = src[i + 1] if i + 1 < n else ""
        if st == NORMAL:
            if c == "/" and nxt == "/":
                st = LINE
                out[i] = out[i + 1] = " "
                i += 2
                continue
            if c == "/" and nxt == "*":
                st = BLOCK
                out[i] = out[i + 1] = " "
                i += 2
                continue
            if c == '"':
                st = STR
            elif c == "'":
                st = CHR
        elif st in (STR, CHR):
            if c == "\\":
                i += 2
                continue
            if (st == STR and c == '"') or (st == CHR and c == "'"):
                st = NORMAL
        elif st == LINE:
            if c == "\\" and nxt == "\n":
                out[i] = " "
                i += 2
                continue
            if c == "\n":
                st = NORMAL
            else:
                out[i] = " "
        elif st == BLOCK:
            if c == "*" and nxt == "/":
                out[i] = out[i + 1] = " "
                st = NORMAL
                i += 2
                continue
            if c != "\n":
                out[i] = " "
        i += 1
    return "".join(out)


def macro_sensitive_identifiers():
    """Every identifier in the canonical text that a `#define` could change.

    Derived from the canonical strings themselves rather than a hand-written
    list, so adding a call to a pinned body extends this set automatically
    instead of silently leaving the new callee unguarded.

    String-literal contents are excluded: macros do not expand inside a string,
    so `#define ADC ...` cannot alter `.pattern = "CONFigure:ADC:SAVEcal"`.
    That is precision, not a relaxation.
    """
    names = set(EXPECTED_BODIES)
    for text in list(EXPECTED_BODIES.values()) + list(
            EXPECTED_REGISTRATIONS.values()):
        outside = re.sub(r'"(?:[^"\\]|\\.)*"', " ", text)
        names |= set(re.findall(r"[A-Za-z_][A-Za-z0-9_]*", outside))
    return names


def active_command_table(code):
    """(name, start, end) of the array actually handed to SCPI_Init.

    Extracted from the call rather than hardcoded, so renaming the table moves
    this check with it instead of quietly pointing at nothing. Both "exactly
    one" requirements are part of the check: several call sites or several
    definitions of that name mean this function would have to guess which one
    dispatches, and a guess here is the whole failure class.
    """
    calls = list(re.finditer(r"\bSCPI_Init\s*\(([^;]*?)\)\s*;", code, re.S))
    if len(calls) != 1:
        return None, "SCPI_Init is called %d times, expected exactly 1" % len(
            calls)
    args = [a.strip() for a in calls[0].group(1).split(",")]
    if len(args) < 2 or not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", args[1]):
        return None, "SCPI_Init's 2nd argument is not a plain array name: %r" % (
            args[1] if len(args) > 1 else None)
    name = args[1]
    defs = list(re.finditer(
        r"\bscpi_command_t\s+%s\s*\[\s*\]\s*=\s*\{" % re.escape(name),
        code))
    if len(defs) != 1:
        return None, "%r is defined %d times, expected exactly 1" % (
            name, len(defs))
    i = code.index("{", defs[0].start())
    depth, j = 0, i
    while j < len(code):
        if code[j] == "{":
            depth += 1
        elif code[j] == "}":
            depth -= 1
            if depth == 0:
                return (name, i, j), None
        j += 1
    return None, "%r's initialiser is unterminated" % name


def depth_map(text):
    """Conditional-directive depth for every line.

    ⛔ WHY THIS EXISTS. `cc -fpreprocessed -E` strips comments but does NOT
    evaluate `#if`, so BOTH branches of a conditional survive as text. r2's
    audit wrapped the real SCPI_ADCCalSave in `#if 0` and put a no-op in
    `#else`: this checker read the first (inactive) definition and passed,
    while the compiler built the no-op. Demonstrated in two lines --
    `cc -E` keeps the `#if 0` branch 0 times, `cc -fpreprocessed -E` keeps
    it once.

    So the rule is that what this checker reads must be what the compiler
    compiles: every pinned definition and table row has to sit at depth 0,
    inside no conditional region at all.
    """
    depths, depth = [], 0
    for line in text.split("\n"):
        t = line.lstrip()
        if re.match(r"#\s*(if|ifdef|ifndef)\b", t):
            depths.append(depth)
            depth += 1
        elif re.match(r"#\s*endif\b", t):
            depth = max(0, depth - 1)
            depths.append(depth)
        else:
            # `#else` / `#elif` keep the depth they are already inside, which
            # is >= 1 by construction, so an #else branch is never depth 0.
            depths.append(depth)
    return depths


def depth_at(text, depths, offset):
    return depths[text.count("\n", 0, offset)]


def body_span(code, name):
    """(open-brace index, close-brace index) of `name`'s body, or None."""
    m = re.search(r"\b%s\s*\([^;{]*\)\s*\{" % re.escape(name), code)
    if not m:
        return None
    i = code.index("{", m.start())
    depth, j = 0, i
    while j < len(code):
        if code[j] == "{":
            depth += 1
        elif code[j] == "}":
            depth -= 1
            if depth == 0:
                return (i, j)
        j += 1
    return None


def function_body(code, name):
    """The body of `name`, by brace matching. '' if not found."""
    sp = body_span(code, name)
    return "" if sp is None else code[sp[0] + 1:sp[1]]


def show_diff(name, expected, found):
    """Statement-level diff, so a reviewer sees WHAT moved."""
    exp = [s.strip() + ";" for s in expected.split(";") if s.strip()]
    got = [s.strip() + ";" for s in found.split(";") if s.strip()]
    print("         --- expected %s" % name)
    print("         +++ found    %s" % name)
    for line in difflib.unified_diff(exp, got, lineterm="", n=1):
        if line.startswith(("---", "+++", "@@")):
            continue
        print("         %s" % line)


def main():
    for p in (ADC, IFACE):
        if not p.exists():
            print("  [FAIL] missing: %s" % p)
            return 1

    adc_raw, iface_raw = ADC.read_text(), IFACE.read_text()
    adc = strip_comments(adc_raw)
    iface = strip_comments(iface_raw)

    # The directive scan below maps spans back to the real file by OFFSET, so
    # the blanker's invariant is load-bearing. Assert it rather than trust it:
    # if it ever drifts, every directive check silently looks in the wrong
    # place and passes.
    adc_blank, iface_blank = blank_comments(adc_raw), blank_comments(iface_raw)
    for label, raw, blank in (("SCPIADC.c", adc_raw, adc_blank),
                              ("SCPIInterface.c", iface_raw, iface_blank)):
        ok = len(raw) == len(blank) and raw.count("\n") == blank.count("\n")
        check(ok, "%s: blank_comments preserved length and line count" % label
              if ok else
              "%s: blank_comments changed the file's geometry (%d->%d bytes, "
              "%d->%d lines) -- every offset-based check below is unsound"
              % (label, len(raw), len(blank), raw.count("\n"),
                 blank.count("\n")))

    # ---- the command table rows, exactly -------------------------------
    for pattern, expected_row in sorted(EXPECTED_REGISTRATIONS.items()):
        rows = [norm(m) for m in re.findall(
            r'\{\s*\.pattern\s*=\s*"%s"\s*,[^}]*\}\s*,' % re.escape(pattern),
            iface)]
        want = norm(expected_row)
        ok = rows == [want]
        check(ok, "%s is registered exactly as pinned" % pattern if ok else
              "%s's registration changed\n"
              "         expected: [%s]\n"
              "         found:    %r" % (pattern, want, rows))

    # ---- the text must not be one the build discards ------------------
    adc_depths, iface_depths = depth_map(adc), depth_map(iface)
    # Non-vacuity: if neither file contained a conditional, the depth checks
    # below could not fail and would be decoration. Reported, not asserted --
    # a file legitimately having none is not an error.
    print("  [--] conditional depth: max %d in SCPIADC.c, max %d in "
          "SCPIInterface.c -- the tracker has real structure to measure"
          % (max(adc_depths), max(iface_depths)))

    for name in EXPECTED_BODIES:
        defs = list(re.finditer(r"\b%s\s*\([^;{]*\)\s*\{" % re.escape(name),
                                adc))
        check(len(defs) == 1,
              "%s is defined exactly once (found %d) -- two definitions mean "
              "the compiler picks one and this checker picks the other"
              % (name, len(defs)))
        if not defs:
            continue
        d = depth_at(adc, adc_depths, defs[0].start())
        check(d == 0,
              "%s's definition is at conditional depth 0" % name if d == 0 else
              "%s's definition sits inside a #if/#else region (depth %d). "
              "The compiler may discard it and build something else, while "
              "this checker reads the text that is there" % (name, d))

    for pattern in EXPECTED_REGISTRATIONS:
        rows = list(re.finditer(
            r'\{\s*\.pattern\s*=\s*"%s"\s*,[^}]*\}\s*,' % re.escape(pattern),
            iface))
        if not rows:
            continue
        d = depth_at(iface, iface_depths, rows[0].start())
        check(d == 0,
              "%s's table row is at conditional depth 0" % pattern if d == 0
              else "%s's table row sits inside a #if/#else region (depth %d), "
                   "so the command may not be registered in the real build"
                   % (pattern, d))

    # ---- directives the comparison cannot see --------------------------
    # r3's HIGH: `#define Streaming_EndConfigChange() ((void)0)` placed inside
    # CalSaveCommon passes the exact body compare, because `strip_comments`
    # removes the directive without expanding the invocation. The release
    # becomes a no-op and the claim is held forever. Measured on the real
    # source first: all three bodies contain zero directives, so this forbids
    # nothing the file does today.
    for name in EXPECTED_BODIES:
        sp = body_span(adc_blank, name)
        if sp is None:
            continue
        found = re.findall(r"(?m)^[ \t]*#[ \t]*(\w+)", adc_blank[sp[0]:sp[1]])
        check(not found,
              "%s's body contains no preprocessor directive" % name
              if not found else
              "%s's body contains preprocessor directive(s) %s. The canonical "
              "comparison cannot see these -- the compiler acts on them and "
              "this checker does not" % (name, found))

    # r3's second MEDIUM: the same trick one level out -- `#define
    # CalSaveCommon(...) SCPI_RES_OK` before the wrappers, `#undef` after.
    # The identifier set is DERIVED from the canonical text, so adding a call
    # to a pinned body extends this automatically instead of leaving the new
    # callee unguarded.
    sensitive = macro_sensitive_identifiers()
    for label, blank in (("SCPIADC.c", adc_blank),
                         ("SCPIInterface.c", iface_blank)):
        defined = set(re.findall(
            r"(?m)^[ \t]*#[ \t]*define[ \t]+([A-Za-z_][A-Za-z0-9_]*)", blank))
        clash = sorted(defined & sensitive)
        check(not clash,
              "%s defines no macro over a pinned identifier (%d #defines vs "
              "%d identifiers)" % (label, len(defined), len(sensitive))
              if not clash else
              "%s #defines %s, which the canonical text names. A macro can "
              "change what that text MEANS while leaving it byte-identical"
              % (label, clash))

    # ---- the rows must be in the table SCPI_Init is actually handed -----
    # r3's third MEDIUM: both rows moved into a separate, unused
    # `static const scpi_command_t` array pass every check above -- they are
    # present, singular and at depth 0 -- while nothing registers them.
    table, err = active_command_table(iface_blank)
    check(table is not None,
          "the active command table was located: %s"
          % (table[0] if table else err) if table is not None else
          "could not identify the command table SCPI_Init is handed: %s. "
          "Without it, a row in ANY array satisfies the rows above" % err)
    if table is not None:
        tname, tstart, tend = table
        for pattern in sorted(EXPECTED_REGISTRATIONS):
            rows = list(re.finditer(
                r'\{\s*\.pattern\s*=\s*"%s"\s*,[^}]*\}\s*,'
                % re.escape(pattern), iface_blank))
            if not rows:
                continue
            outside = [r for r in rows if not tstart < r.start() < tend]
            check(not outside,
                  "%s's row is inside %s, the array passed to SCPI_Init"
                  % (pattern, tname) if not outside else
                  "%s's row is NOT inside %s, the array SCPI_Init is handed, "
                  "so the command is not registered in the real build"
                  % (pattern, tname))

    # ---- every body on the path, exactly -------------------------------
    for name, expected in EXPECTED_BODIES.items():
        found = norm(function_body(adc, name))
        want = norm(expected)
        if not found:
            check(False, "%s's body was not found at all" % name)
            continue
        ok = found == want
        check(ok, "%s's body is EXACTLY as pinned (%d chars)"
              % (name, len(want)) if ok else
              "%s's body CHANGED -- any edit here is a deliberate act and "
              "updates this pin in the same PR" % name)
        if not ok:
            show_diff(name, want, found)

    print()
    if fails:
        print("FAILED: %d check(s)" % len(fails))
        print("If this path was changed deliberately, paste the new body into "
              "EXPECTED_BODIES in this file, in the SAME PR -- that diff is "
              "the review point. Do not relax these into fragment or pattern "
              "matching: fw#1221 and this PR's r1 both established that "
              "anything less than the whole unit leaves the control flow "
              "between the fragments unpinned.")
        return 1
    print("OK: SAVEcal and SAVEFcal route to the claim-guarded body, and "
          "every body on that path is byte-for-byte what it was pinned as")
    return 0


if __name__ == "__main__":
    sys.exit(main())
