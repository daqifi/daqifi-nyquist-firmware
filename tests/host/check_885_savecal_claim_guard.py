#!/usr/bin/env python3
"""Pin that CONF:ADC:SAVEcal stays behind the streaming claim guard (#885).

#885 converted five ADC setters that mutated live conversion inputs with no
guard. Its pre-merge audit then found SAVEcal / SAVEFcal: the read side of
the same cal store, and the last commands that MUTATE PERSISTENT STATE from
CalM/CalB with no claim at all. Both now route through CalSaveCommon, which
takes the claim, uses its verdict, and releases it on the single path out.

This replaces the device leg deleted from ts#504 by ruling FT A.

⛔ WHY THIS MUST BE FUNCTION-SCOPED, measured rather than assumed. At
61a631745^ -- the revision before the #885 fix -- `Streaming_BeginConfigChange`
appears TEN times in SCPIADC.c, because other setters already used it, while
CalSaveCommon's own body contained NONE. So a file-scoped grep for the guard
PASSES ON THE BROKEN REVISION and proves nothing about this command. The
guard has to be found inside the body that SAVEcal actually reaches.

⛔ AND WHY THE EXPRESSIONS ARE PINNED EXACTLY, not recognised. fw#1221 spent
three audit rounds on the other shape: a pin that recognises structure is
taught the mutants it is shown and the next round finds the next one --
operand text missed an operator, structural matching missed a comparison
inside an operand and an added conjunct. "Recognise the shape" has an
open-ended mutant space. So the claim, its verdict test and its rejection are
compared EXACTLY against canonical text, whitespace normalised.

Any other edit fails the build, INCLUDING A CORRECT ONE. That is the point: a
deliberate change to this guard updates the canonical strings in the same PR,
and that diff is the review moment. Do not relax this into matching.

Run: python3 tests/host/check_885_savecal_claim_guard.py
"""
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
ADC = ROOT / "firmware/src/services/SCPI/SCPIADC.c"
IFACE = ROOT / "firmware/src/services/SCPI/SCPIInterface.c"

# Verbatim from the source, whitespace runs collapsed.
EXPECTED_CLAIM = "StreamingCfgClaim claim = Streaming_BeginConfigChange();"
EXPECTED_VERDICT = "claim != STREAM_CFG_CLAIM_OK"
EXPECTED_REJECT = ("return SCPI_RejectCfgClaim(context, "
                   "claim == STREAM_CFG_CLAIM_BUSY, what);")
RELEASE = "Streaming_EndConfigChange();"

# The commands that must reach the guarded body, and the callback each is
# registered with. SAVEFcal shares CalSaveCommon, so one body check covers
# the pair -- and the #885 follow-up names both as having been unguarded.
COMMANDS = {
    "CONFigure:ADC:SAVEcal": "SCPI_ADCCalSave",
    "CONFigure:ADC:SAVEFcal": "SCPI_ADCCalFSave",
}
GUARDED_BODY = "CalSaveCommon"

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
    """The file's own text with comments gone, includes unchased.

    Mandatory, not cosmetic: SCPIADC.c's prose discusses the claim, the
    rejected alternatives and SAVEcal by name at length, so a raw scan finds
    the shapes it is looking for inside the explanations. Done with the
    compiler rather than a regex -- a regex stripper eats pointer
    dereferences, and this file is full of them.
    """
    out = subprocess.run([cc, "-fpreprocessed", "-E", "-"],
                         input=src, capture_output=True, text=True)
    if out.returncode != 0:
        raise RuntimeError("%s -fpreprocessed -E failed: %s"
                           % (cc, out.stderr.strip()[:200]))
    return out.stdout


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


def main():
    for p in (ADC, IFACE):
        if not p.exists():
            print("  [FAIL] missing: %s" % p)
            return 1

    adc_raw = ADC.read_text()
    adc = strip_comments(adc_raw)
    iface = strip_comments(IFACE.read_text())

    # ---- routing: each command reaches the guarded body ------------------
    for pattern, callback in sorted(COMMANDS.items()):
        reg = re.search(
            r'\{\s*\.pattern\s*=\s*"%s"\s*,\s*\.callback\s*=\s*(\w+)\s*,?\s*\}'
            % re.escape(pattern), iface)
        found_cb = reg.group(1) if reg else None
        check(found_cb == callback,
              "%s is registered to %s (found %s)"
              % (pattern, callback, found_cb or "no registration"))
        body = function_body(adc, callback)
        check(GUARDED_BODY in body,
              "and %s delegates to %s, the body that holds the claim"
              % (callback, GUARDED_BODY))

    # ---- the guard itself, inside the body SAVEcal reaches ---------------
    guarded = function_body(adc, GUARDED_BODY)
    check(bool(guarded), "%s's body was located" % GUARDED_BODY)
    if not guarded:
        print("\nFAILED: %d check(s)" % len(fails))
        return 1

    statements = [norm(s) + ";" for s in guarded.split(";") if norm(s)]
    first = statements[0] if statements else ""
    # ⛔ FIRST, not merely present. The #885 class is "mutates live
    # conversion inputs with no guard": a claim taken after the first
    # mutation is not a guard, it is a decoration.
    check(first == EXPECTED_CLAIM,
          "the claim is the FIRST statement in %s" % GUARDED_BODY
          if first == EXPECTED_CLAIM else
          "the FIRST statement in %s is not the pinned claim\n"
          "         expected: %s\n"
          "         found:    %s" % (GUARDED_BODY, EXPECTED_CLAIM, first))

    verdicts = [norm(m) for m in re.findall(r"if\s*\(([^)]*)\)", guarded)]
    check(EXPECTED_VERDICT in verdicts,
          "the claim's verdict is tested exactly as `%s`" % EXPECTED_VERDICT
          if EXPECTED_VERDICT in verdicts else
          "the pinned verdict test is absent\n"
          "         expected: %s\n"
          "         found:    %r" % (EXPECTED_VERDICT, verdicts))

    # ⚠ Anchored on the `return`, not on a ';' split. Splitting the body on
    # ';' hands back the enclosing `if (...) {` along with the statement,
    # which is what the first version of this check compared and failed on.
    rejects = [norm(m) for m in re.findall(
        r"return\s+SCPI_RejectCfgClaim\s*\([^;]*\);", guarded)]
    check(rejects == [EXPECTED_REJECT],
          "a refused claim returns exactly the pinned rejection"
          if rejects == [EXPECTED_REJECT] else
          "the rejection changed\n"
          "         expected: [%s]\n"
          "         found:    %r" % (EXPECTED_REJECT, rejects))

    releases = guarded.count(RELEASE)
    check(releases == 1,
          "the claim is released exactly once, on the single path out "
          "(found %d). Two releases mean a path that double-releases; none "
          "means a claim held forever, which wedges every later config "
          "change" % releases)

    print()
    if fails:
        print("FAILED: %d check(s)" % len(fails))
        print("If this guard was changed deliberately, update the canonical "
              "strings at the top of this file in the SAME PR -- that diff is "
              "the review point. Do not relax them into pattern matching: "
              "fw#1221 spent three audit rounds learning that recognising a "
              "shape cannot converge.")
        return 1
    print("OK: CONF:ADC:SAVEcal and SAVEFcal reach the claim-guarded body, "
          "and the guard is byte-for-byte what it was pinned as")
    return 0


if __name__ == "__main__":
    sys.exit(main())
