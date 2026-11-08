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


def function_body(code, name):
    """The body of `name`, by brace matching. '' if not found."""
    m = re.search(r"\b%s\s*\([^;{]*\)\s*\{" % re.escape(name), code)
    if not m:
        return ""
    i = code.index("{", m.start())
    depth, j = 0, i
    while j < len(code):
        if code[j] == "{":
            depth += 1
        elif code[j] == "}":
            depth -= 1
            if depth == 0:
                return code[i + 1:j]
        j += 1
    return ""


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

    adc = strip_comments(ADC.read_text())
    iface = strip_comments(IFACE.read_text())

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
