#!/usr/bin/env python3
"""#1201: is the stop-request predicate still the ONE implementation, and is it
still wired into every site that needs it?

WHY A SOURCE CHECK AT ALL. test_1201_stop_requested_since.c compiles the real
predicate and tests its LOGIC, which is the part a host test can reach.
Two properties it cannot reach matter just as much, and both are about
PLACEMENT rather than logic:

  1. ⛔ THE PREDICATE MUST NOT TAKE A CRITICAL SECTION OF ITS OWN. Its callers
     take one, and at the arm sites the reads have to be inside the SAME
     section that publishes IsEnabled -- that is what makes the answer usable
     rather than merely atomic, because a stop then cannot slip between the
     test and the publish. A section acquired inside the predicate would make
     each call atomic and reopen exactly that window, while looking MORE
     careful than the correct code. It is the single most plausible wrong
     "cleanup" anyone will try on this function, and it is invisible to a test
     that only calls it.

  2. EVERY CALL SITE MUST USE THE SHARED PREDICATE, not a re-typed copy of the
     three terms. #1201 exists because the test was copy-pasted twice and two
     sites that needed it therefore never got one. A re-inlined copy puts that
     back, and it passes every behavioural test there is.

A third property is checked as the COMPLEMENT of 2, because "fixing" 2 the
wrong way is easy: the two PIN sites must still read the globals directly.
Pinning is not the predicate -- it records the window's start, under its own
critical section, and routing it through StopRequestedSince() would be a
category error that a naive "no direct reads anywhere" rule would demand.

⚠ WHAT THIS IS NOT. It does not check that each call sits inside a critical
section, or at the right point in its function. Those are argued in the source
comments and exercised on hardware by the companion test in
daqifi-python-test-suite. A pass here is "the wiring is intact", never "the
placement is correct".

Comments are STRIPPED before any check, with string- and char-literal
awareness, because the source prose around this fix names every identifier the
checks look for -- a raw grep would be satisfied by the paragraphs explaining
the fix rather than by the fix. That failure mode is the one this file's
directory has hit most often, so --self-test proves the stripper on inputs
built to fool a naive one.

Usage:
    check_1201_predicate_wiring.py --src <SCPIInterface.c>
    check_1201_predicate_wiring.py --self-test
"""
import argparse
import sys

PREDICATE = "StopRequestedSince"
GLOBALS = ("gStreamStopGen", "gStreamStopsActive")
CRITICAL = ("taskENTER_CRITICAL", "taskEXIT_CRITICAL")

PREDICATE_SIG = "static inline bool StopRequestedSince("

# function-signature prefix (must start at column 0) -> expected number of
# StopRequestedSince() calls in that function.
CALL_SITES = {
    "static scpi_result_t SCPI_RunThroughputBenchClaimed(": 2,
    "static bool FindMeasureStep(": 2,
    "static scpi_result_t SCPI_StartStreamingClaimed(": 1,
}

# Functions that legitimately read the globals directly: they PIN, they do not
# ask. Each must contain both reads.
PIN_SITES = (
    "static scpi_result_t SCPI_RunSessionStartClaimed(",
    "static scpi_result_t SCPI_StartStreaming(scpi_t * context)",
)


def strip_comments(text):
    """Remove C comments, preserving string and char literals.

    Replaces each comment with a single space so token adjacency cannot be
    invented by the removal (`a/*x*/b` must not become `ab`).
    """
    out = []
    i = 0
    n = len(text)
    while i < n:
        c = text[i]
        if c == '"' or c == "'":
            quote = c
            out.append(c)
            i += 1
            while i < n:
                if text[i] == "\\" and i + 1 < n:
                    out.append(text[i:i + 2])
                    i += 2
                    continue
                out.append(text[i])
                if text[i] == quote:
                    i += 1
                    break
                if text[i] == "\n":      # unterminated literal; do not run on
                    i += 1
                    break
                i += 1
            continue
        if c == "/" and i + 1 < n and text[i + 1] == "*":
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            # Keep newlines so line numbers in messages stay usable.
            out.append(" " + "\n" * text.count("\n", i, j))
            i = j
            continue
        if c == "/" and i + 1 < n and text[i + 1] == "/":
            j = text.find("\n", i)
            j = n if j < 0 else j
            out.append(" ")
            i = j
            continue
        out.append(c)
        i += 1
    return "".join(out)


def extract_function(stripped, sig_prefix):
    """Return the text of the function whose signature starts at column 0 with
    sig_prefix, from that line through the first line that is exactly '}'.

    Returns None when the signature is not found, so a caller can FAIL rather
    than silently check an empty string -- a check that extracted nothing must
    not report a pass.
    """
    lines = stripped.split("\n")
    start = None
    for idx, line in enumerate(lines):
        if line.startswith(sig_prefix):
            if start is not None:
                return None          # ambiguous: more than one definition
            start = idx
    if start is None:
        return None
    for idx in range(start, len(lines)):
        if lines[idx].rstrip() == "}":
            return "\n".join(lines[start:idx + 1])
    return None                      # never closed at column 0


def check(src_text):
    """Return a list of failure strings; empty means every property holds."""
    bad = []
    s = strip_comments(src_text.replace("\r\n", "\n"))

    # ---- 1. the predicate exists exactly once, and is critical-section-free
    pred = extract_function(s, PREDICATE_SIG)
    if pred is None:
        bad.append(
            "the predicate '%s...' was not found exactly once at column 0, or "
            "is not closed by a '}' at column 0. A check that extracted "
            "nothing must fail rather than pass vacuously." % PREDICATE_SIG)
    else:
        for tok in CRITICAL:
            if tok in pred:
                bad.append(
                    "%s appears INSIDE %s(). It must not: its callers hold the "
                    "section, and at the arm sites the reads must be in the "
                    "SAME section that publishes IsEnabled. A section taken "
                    "here makes each call atomic and reopens the window "
                    "between the test and the publish." % (tok, PREDICATE))
        for g in GLOBALS:
            if g not in pred:
                bad.append(
                    "%s() does not read %s. It is supposed to be the ONE place "
                    "the three-term test lives; a predicate that has stopped "
                    "reading one of its inputs answers a different question."
                    % (PREDICATE, g))

    # ---- 2. every call site asks the predicate and keeps no private copy
    for sig, want in CALL_SITES.items():
        fn = extract_function(s, sig)
        if fn is None:
            bad.append(
                "could not extract the function '%s...' -- it was renamed, "
                "moved, or its closing brace left column 0. #1201's wiring "
                "cannot be checked at a site that cannot be found." % sig)
            continue
        got = fn.count(PREDICATE + "(")
        if got != want:
            bad.append(
                "'%s...' calls %s() %d time(s), expected %d. The four added "
                "by #1201 are THRoughput's arm, THRoughput's post-dwell "
                "discard, the finder's arm and the finder's post-observation "
                "check; a dropped call silently restores the defect."
                % (sig, PREDICATE, got, want))
        for g in GLOBALS:
            if g in fn:
                bad.append(
                    "'%s...' reads %s DIRECTLY. That is a re-inlined copy of "
                    "the three-term test, which is the duplication #1201 "
                    "removed -- and the reason two sites that needed the test "
                    "never got one. Call %s() instead."
                    % (sig, g, PREDICATE))

    # ---- 3. the complement: the pin sites still read the globals themselves
    for sig in PIN_SITES:
        fn = extract_function(s, sig)
        if fn is None:
            bad.append(
                "could not extract the pin site '%s...'. The pins are what "
                "define each command's window; if one is gone the predicate "
                "has nothing to compare against." % sig)
            continue
        for g in GLOBALS:
            if g not in fn:
                bad.append(
                    "the pin site '%s...' no longer reads %s. Pinning is NOT "
                    "the predicate: it records the START of the window under "
                    "its own critical section. Do not route it through %s()."
                    % (sig, g, PREDICATE))
    return bad


# --------------------------------------------------------------------- self-test

GOOD = """\
static volatile uint32_t gStreamStopGen;
static volatile uint32_t gStreamStopsActive;

static inline bool StopRequestedSince(uint32_t pinnedGen, bool pinnedActive,
                                      bool *outStopInFlight) {
    const bool inFlight = pinnedActive || (gStreamStopsActive != 0u);
    *outStopInFlight = inFlight;
    return inFlight || (gStreamStopGen != pinnedGen);
}

static scpi_result_t SCPI_RunSessionStartClaimed(scpi_t * context) {
    taskENTER_CRITICAL();
    stopGenPinned = gStreamStopGen;
    stopActivePinned = (gStreamStopsActive != 0u);
    taskEXIT_CRITICAL();
    return body(context);
}

static scpi_result_t SCPI_RunThroughputBenchClaimed(scpi_t * context,
                                                    uint32_t g, bool a) {
    taskENTER_CRITICAL();
    stopRequested = StopRequestedSince(g, a, &stopInFlight);
    taskEXIT_CRITICAL();
    taskENTER_CRITICAL();
    dwellStopped = StopRequestedSince(g, a, &dwellInFlight);
    taskEXIT_CRITICAL();
    return SCPI_RES_OK;
}

static bool FindMeasureStep(StreamingRuntimeConfig* cfg) {
    taskENTER_CRITICAL();
    stopRequested = StopRequestedSince(b->g, b->a, &stopInFlight);
    taskEXIT_CRITICAL();
    taskENTER_CRITICAL();
    stopAfterObs = StopRequestedSince(b->g, b->a, &obsInFlight);
    taskEXIT_CRITICAL();
    return false;
}

static scpi_result_t SCPI_StartStreamingClaimed(scpi_t * context,
                                                uint32_t g, bool a) {
    taskENTER_CRITICAL();
    stopRequested = StopRequestedSince(g, a, &stopInFlight);
    taskEXIT_CRITICAL();
    return SCPI_RES_OK;
}

static scpi_result_t SCPI_StartStreaming(scpi_t * context) {
    taskENTER_CRITICAL();
    stopGenPinned = gStreamStopGen;
    stopActivePinned = (gStreamStopsActive != 0u);
    taskEXIT_CRITICAL();
    return SCPI_RunSessionStartClaimed(context);
}
"""


def self_test():
    """Each mutant must be REJECTED, and the good source ACCEPTED.

    A guard is only worth its runtime if it fails on the thing it exists to
    catch, so every check above gets an arm that triggers exactly it, plus two
    arms that prove the comment stripper is not the weak link.
    """
    cases = []   # (label, text, expect_ok, must_mention)

    cases.append(("the good source", GOOD, True, None))

    # --- the property that is easiest to break by trying to be tidy
    cases.append((
        "M1 predicate acquires its own critical section",
        GOOD.replace(
            "    const bool inFlight = pinnedActive || (gStreamStopsActive != 0u);",
            "    taskENTER_CRITICAL();\n"
            "    const bool inFlight = pinnedActive || (gStreamStopsActive != 0u);\n"
            "    taskEXIT_CRITICAL();"),
        False, "appears INSIDE"))

    # --- a re-inlined copy at a call site: the exact duplication #1201 removed
    cases.append((
        "M2 a call site re-inlines the three terms",
        GOOD.replace(
            "    stopRequested = StopRequestedSince(g, a, &stopInFlight);\n"
            "    taskEXIT_CRITICAL();\n"
            "    taskENTER_CRITICAL();\n"
            "    dwellStopped = StopRequestedSince(g, a, &dwellInFlight);\n",
            "    stopInFlight = a || (gStreamStopsActive != 0u);\n"
            "    stopRequested = stopInFlight || (gStreamStopGen != g);\n"
            "    taskEXIT_CRITICAL();\n"
            "    taskENTER_CRITICAL();\n"
            "    dwellStopped = StopRequestedSince(g, a, &dwellInFlight);\n"),
        False, "DIRECTLY"))

    # --- the finder's SECOND call dropped: #1201's own finder half reverted
    cases.append((
        "M3 the post-observation call is dropped",
        GOOD.replace(
            "    taskENTER_CRITICAL();\n"
            "    stopAfterObs = StopRequestedSince(b->g, b->a, &obsInFlight);\n"
            "    taskEXIT_CRITICAL();\n",
            ""),
        False, "expected 2"))

    # --- a term removed from the predicate
    cases.append((
        "M4 the predicate stops reading one global",
        GOOD.replace("return inFlight || (gStreamStopGen != pinnedGen);",
                     "return inFlight;"),
        False, "does not read gStreamStopGen"))

    # --- the pin "tidied" into the predicate: the wrong fix for M2
    cases.append((
        "M5 a pin site routed through the predicate",
        GOOD.replace(
            "    stopGenPinned = gStreamStopGen;\n"
            "    stopActivePinned = (gStreamStopsActive != 0u);\n"
            "    taskEXIT_CRITICAL();\n"
            "    return SCPI_RunSessionStartClaimed(context);",
            "    stopRequested = StopRequestedSince(0u, false, &f);\n"
            "    taskEXIT_CRITICAL();\n"
            "    return SCPI_RunSessionStartClaimed(context);"),
        False, "no longer reads"))

    # --- the subject vanishes: a check that measures nothing must not pass
    cases.append((
        "M6 the predicate is gone entirely",
        GOOD.replace("static inline bool StopRequestedSince(", "static inline bool Renamed("),
        False, "was not found"))

    # --- ⭐ the stripper is the weak link if it is naive: PROSE naming every
    #     identifier must NOT satisfy the checks, and must not break them
    cases.append((
        "M7 prose naming the identifiers does not substitute for the code",
        GOOD.replace("    return inFlight;\n", "")
            .replace("return inFlight || (gStreamStopGen != pinnedGen);",
                     "/* reads gStreamStopGen and gStreamStopsActive, honest */\n"
                     "    return inFlight;"),
        False, "does not read gStreamStopGen"))

    cases.append((
        "M8 a comment containing a call does not count as a call",
        GOOD.replace(
            "    taskENTER_CRITICAL();\n"
            "    stopAfterObs = StopRequestedSince(b->g, b->a, &obsInFlight);\n"
            "    taskEXIT_CRITICAL();\n",
            "    /* StopRequestedSince(b->g, b->a, &obsInFlight) belongs here */\n"),
        False, "expected 2"))

    # --- the GD half reverted: THRoughput measures a cancelled run and
    #     reports it as a completed one. Behaviourally invisible to the host
    #     tests (they exercise the predicate, which is still correct), so the
    #     count at this site is the only thing that can see it.
    cases.append((
        "M10 THRoughput's post-dwell discard call is dropped",
        GOOD.replace(
            "    taskENTER_CRITICAL();\n"
            "    dwellStopped = StopRequestedSince(g, a, &dwellInFlight);\n"
            "    taskEXIT_CRITICAL();\n",
            ""),
        False, "expected 2"))

    # --- the stripper must NOT corrupt a source whose strings contain comment
    #     markers; otherwise it invents failures in correct files
    cases.append((
        "M9 comment markers inside string literals are not comments",
        GOOD.replace('    return SCPI_RES_OK;\n}\n\nstatic bool FindMeasureStep',
                     '    LOG_E("/* not a comment */ and // neither is this");\n'
                     '    return SCPI_RES_OK;\n}\n\nstatic bool FindMeasureStep'),
        True, None))

    failed = 0
    for label, text, expect_ok, must_mention in cases:
        # ⛔ A MUTANT THAT DID NOT MUTATE IS NOT A CONTROL. Every arm here is
        # a GOOD.replace(), and a replace whose anchor has rotted returns GOOD
        # UNCHANGED -- silently. The expect_ok=False arms fail loudly when that
        # happens, but the expect_ok=True arms (the stripper controls) would
        # ACCEPT GOOD and report a pass while testing nothing at all. That is
        # the permissive direction, so it is asserted rather than hoped for:
        # found while adding M10, when editing the fixture for #1201's GD half
        # broke M2's anchor and would have left M9 inert.
        if label != "the good source" and text == GOOD:
            failed += 1
            print("  FAIL %s: the mutation did not LAND -- its anchor no "
                  "longer matches the fixture, so this arm tested nothing."
                  % label)
            continue
        bad = check(text)
        ok = not bad
        if ok != expect_ok:
            failed += 1
            print("  FAIL %s: expected %s, got %s"
                  % (label, "ACCEPT" if expect_ok else "REJECT",
                     "ACCEPT" if ok else "REJECT"))
            for b in bad:
                print("         - %s" % b)
        elif must_mention is not None and not any(must_mention in b for b in bad):
            # Rejected, but by the WRONG check -- which would make this arm
            # pass while the property it targets is unguarded.
            failed += 1
            print("  FAIL %s: rejected, but no finding mentions %r"
                  % (label, must_mention))
            for b in bad:
                print("         - %s" % b)
        else:
            print("  ok   %s" % label)

    print("self-test: %d case(s), %d failed" % (len(cases), failed))
    return 1 if failed else 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--src")
    ap.add_argument("--self-test", action="store_true")
    args = ap.parse_args()

    if args.self_test:
        return self_test()

    if not args.src:
        print("ERROR: --src is required (or use --self-test)")
        return 2

    with open(args.src, encoding="utf-8", newline="") as fh:
        text = fh.read()

    bad = check(text)
    if bad:
        print("ERROR: #1201's predicate wiring is broken in %s" % args.src)
        for b in bad:
            print("  - %s" % b)
        return 1
    print("#1201 predicate wiring OK: one critical-section-free predicate, "
          "5 calls across 3 sites, 0 re-inlined copies, both pins intact.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
