#!/usr/bin/env python3
"""Pin the SHAPE of the three #913 spin-then-yield wait loops.

WHY THIS EXISTS
---------------
`tests/host/test_913_spi_wait_stat.c` cannot include `UserSpi.c` (Harmony,
FreeRTOS and the SFR headers), so it re-implements `spi_WaitStat`'s loop
against a mock. That is the established convention for this suite -- eight
host suites do it -- and its known weakness is that the model is a COPY.

Until this checker the copy was pinned by two GREPS for CONSTANTS: the 20 ms
timeout and the 8000-iteration spin bound. Qodo's review of PR #1009 pointed
out what those constants do not cover, and it is right: delete the fresh
`SPI1STAT` read at expiry, move the deadline test after the yield, or drop the
`vTaskDelay(1)`, and BOTH greps still pass while the firmware regresses and
every modeled test stays green. That is a guard that cannot fail for the
defect it exists to catch.

So this pins the loop's SHAPE -- the three load-bearing statements and their
ORDER -- for all three files that carry it.

WHAT THIS IS NOT
----------------
It does not parse C and it does not extract meaning from it. It matches three
EXACT source lines per file, requires each to appear exactly once, and
compares their line numbers. If anything about those lines changes, it refuses
and says to re-derive the model. A reformat trips it, and that is the intended
behaviour, not a bug: the model is a copy, and a copy whose original moved has
to be re-checked by a human.

That distinction is deliberate. Issue #1032 records a fixture generator in this
repo that tried to DERIVE values by regex-interpreting C, failed two audit
rounds in both directions, and was parked. Copy-and-pin is the pattern that
replaces it, and this is that pattern.

WHAT IT DOES NOT ESTABLISH
--------------------------
Textual presence, order, and PROXIMITY to the named wait function -- nothing
more. It cannot see control flow, so it does not prove the expiry branch is
REACHED, nor that the fresh read's value is what the caller receives, nor that
the statements are in the loop rather than merely inside the function's first
40 lines. Those are the host test's job, and the host test is only meaningful
while this checker says its model still matches the source. The two halves are
useless apart.

The proximity window is why the checks are not file-scoped, which is how the
first version of this checker could be defeated: the statements could be moved
into unrelated code and left out of the real wait loop entirely. A window is a
weaker claim than "inside this loop" and it is stated as the weaker claim, but
it is the strongest one available without a parser.
"""

import argparse
import re
import sys

REPO_DEFAULTS = {
    "spi": "firmware/src/HAL/UserSpi/UserSpi.c",
    "uart": "firmware/src/HAL/UserUart/UserUart.c",
    "i2c": "firmware/src/HAL/UserI2c/UserI2c.c",
}

# Per file: the three statements, in the order they MUST appear.
#
# Each pattern is anchored to a whole line with only leading whitespace, so a
# mention inside a `/* ... */` comment (which always starts with ` *` here)
# cannot satisfy it -- three of these strings DO appear in comments in these
# files, and an unanchored grep would count those.
# The wait function each shape must live INSIDE, matched on its own signature
# line. Without this the checks were file-scoped: Qodo's review of PR #1009
# pointed out that moving the three statements into unrelated code, while
# deleting them from the real wait loop, left the checker green. Anchoring to
# the function name and requiring the statements within MAX_SPAN lines of it
# closes that -- still positionally, still without parsing C.
#
# MAX_SPAN is 40. Measured distances from the signature line at the head this
# was written against: spi +9..+15, uart +9..+18, i2c +9..+22. The loosest is
# i2c, whose loop carries the longest comments. 40 leaves room for comments to
# grow without letting a statement wander into a different function -- these
# files are 480 to 750 lines, so file-scope was two orders of magnitude looser.
MAX_SPAN = 40

SIGNATURES = {
    "spi": r'^static bool spi_WaitStat\(uint32_t mask, bool want,[ \t]*$',
    "uart": r'^static bool uart_WaitSta\(const UartDesc_t\* u, uint32_t mask, bool want,[ \t]*$',
    "i2c": r'^static bool i2c_WaitMif\(void\) \{[ \t]*$',
}

SHAPES = {
    "spi": [
        ("deadline test",
         r'^[ \t]*if \(\(TickType_t\)\(xTaskGetTickCount\(\) - start\) >= timeoutTicks\) \{[ \t]*$'),
        ("fresh read at expiry",
         r'^[ \t]*return \(\(\(SPI1STAT & mask\) != 0u\) == want\);[ \t]*$'),
        ("yield",
         r'^[ \t]*vTaskDelay\(1\);[ \t]*$'),
    ],
    "uart": [
        ("deadline test",
         r'^[ \t]*if \(\(TickType_t\)\(xTaskGetTickCount\(\) - start\) >= timeoutTicks\) \{[ \t]*$'),
        ("fresh read at expiry",
         r'^[ \t]*return \(\(\(\*\(u->sta\) & mask\) != 0u\) == want\);[ \t]*$'),
        ("yield",
         r'^[ \t]*vTaskDelay\(1\);[ \t]*$'),
    ],
    "i2c": [
        ("deadline test",
         r'^[ \t]*if \(\(uint32_t\)\(_CP0_GET_COUNT\(\) - start\) > I2C_OP_TIMEOUT_CP0\) \{[ \t]*$'),
        ("fresh read at expiry",
         r'^[ \t]*return \(\(IFS4 & _IFS4_I2C2MIF_MASK\) != 0u\);[ \t]*$'),
        ("yield",
         r'^[ \t]*vTaskDelay\(1\);[ \t]*$'),
    ],
}

ADVICE = (
    "       The #913 wait loop must read the status bit FRESH at expiry, decide\n"
    "       the deadline BEFORE yielding, and yield rather than spin. A\n"
    "       hardcoded `false` at expiry reports a COMPLETED operation as a\n"
    "       spurious timeout -- for i2c that fires i2c_BusRecover() on a healthy\n"
    "       bus. tests/host/test_913_spi_wait_stat.c models this shape; if you\n"
    "       changed it deliberately, update that model and this checker\n"
    "       together."
)


def check_text(key, text):
    """Return a list of problem strings for one file's contents."""
    lines = text.splitlines()
    problems = []
    found = {}
    for label, pat in SHAPES[key]:
        rx = re.compile(pat)
        hits = [i + 1 for i, ln in enumerate(lines) if rx.match(ln)]
        if len(hits) != 1:
            problems.append(
                "%s: expected exactly 1 line matching the %s, found %d"
                % (key, label, len(hits)))
        else:
            found[label] = hits[0]

    sig_hits = [i + 1 for i, ln in enumerate(lines)
                if re.match(SIGNATURES[key], ln)]
    if len(sig_hits) != 1:
        problems.append(
            "%s: expected exactly 1 line matching the wait function's "
            "signature, found %d -- the window check below anchors on it and "
            "means nothing without it" % (key, len(sig_hits)))

    if len(found) == len(SHAPES[key]):
        order = [found[label] for label, _ in SHAPES[key]]
        if order != sorted(order):
            names = ", ".join(
                "%s@%d" % (label, found[label]) for label, _ in SHAPES[key])
            problems.append(
                "%s: the three statements are out of order (%s). The deadline "
                "test must come before the fresh read, and both before the "
                "yield." % (key, names))

        if len(sig_hits) == 1:
            sig = sig_hits[0]
            for label, _ in SHAPES[key]:
                line = found[label]
                if not (sig < line <= sig + MAX_SPAN):
                    problems.append(
                        "%s: the %s is at line %d, outside the %d lines after "
                        "the wait function's signature at %d -- it is not in "
                        "that function's loop any more, wherever else it is"
                        % (key, label, line, MAX_SPAN, sig))
    return problems


def self_test():
    """Every check must FAIL on the shape it exists to refuse, for the right
    REASON, and for EVERY per-item check that reason covers.

    This scoring has been tightened twice, and the shape of both misses is the
    same one, which is why it is recorded rather than quietly corrected:

      * The first version asked only "did any problem come back?". Two fixtures
        were then found to be rejected by the UNIQUENESS check rather than the
        window check they were written for, so deleting the window check
        outright left --self-test green.
      * The second version asked for a matching problem SUBSTRING. But
        check_text() runs its uniqueness and window checks INDEPENDENTLY PER
        STATEMENT, so one shared substring is produced by any of three or four
        separate checks. Deleting one of them still satisfied the assertion.
        Found by review of PR #1009, twice in a row.

    So each case now declares the EXACT SET of problems it expects: the count,
    and one distinct substring per problem, matched one-to-one in both
    directions. That TERMINATES the tightening rather than continuing it --
    nothing about the reported list is left unchecked, so there is no looser
    form to slip through. A per-item check that is deleted changes the count; a
    check that fires when it should not shows up as an unclaimed problem.
    """
    sig = ("static bool spi_WaitStat(uint32_t mask, bool want,\n"
           "                         TickType_t start, TickType_t timeoutTicks) {\n")
    deadline = "        if ((TickType_t)(xTaskGetTickCount() - start) >= timeoutTicks) {\n"
    fresh = "            return (((SPI1STAT & mask) != 0u) == want);\n"
    yield_ = "        vTaskDelay(1);\n"

    base = (sig
            + "    for (;;) {\n"
            + "        for (uint32_t s = 0; s < 8000u; ++s) {\n"
            + "            if (((SPI1STAT & mask) != 0u) == want) { return true; }\n"
            + "        }\n"
            + "        if (((SPI1STAT & mask) != 0u) == want) { return true; }\n"
            + deadline + fresh + "        }\n" + yield_
            + "    }\n}\n")

    # The same function with all THREE statements gone, so a fixture can place
    # exactly one of each somewhere else and leave the window as the only check
    # that can object.
    stripped = (sig
                + "    for (;;) {\n"
                + "        for (uint32_t s = 0; s < 8000u; ++s) {\n"
                + "            if (((SPI1STAT & mask) != 0u) == want) { return true; }\n"
                + "        }\n"
                + "        if (((SPI1STAT & mask) != 0u) == want) { return true; }\n"
                + "        return false;\n"
                + "    }\n}\n")
    far = ("\n" * 60
           + "static bool spi_SomethingElse(uint32_t mask, bool want,\n"
             "                              TickType_t start, TickType_t timeoutTicks) {\n"
           + deadline + fresh + "        }\n" + yield_
           + "    return false;\n}\n")

    # Each case declares the EXACT set of problems expected: one substring per
    # problem, matched one-to-one. check_text() reports uniqueness and window
    # violations PER STATEMENT, so a fixture that trips several of them must
    # name several -- that is what makes deleting one of them visible.
    cases = [
        ("clean source passes", base, []),
        ("the pre-#913 hardcoded false at expiry",
         base.replace(fresh, "            return false;\n"),
         ["fresh read at expiry, found 0"]),
        ("the yield removed (back to a pure busy-spin)",
         base.replace(yield_, ""), ["the yield, found 0"]),
        ("the deadline test removed (never times out)",
         base.replace(deadline, "        if (0) {\n"),
         ["deadline test, found 0"]),
        ("yield moved BEFORE the deadline test",
         sig + "    for (;;) {\n" + yield_ + deadline + fresh + "        }\n"
         + "    }\n}\n", ["out of order"]),
        # Four problems, not one: every statement AND the signature is now
        # duplicated, and each is reported by its own uniqueness check.
        ("a second copy of the whole loop", base + base,
         ["deadline test, found 2",
          "fresh read at expiry, found 2",
          "the yield, found 2",
          "signature, found 2"]),
        # Three problems: the window is checked per statement.
        ("the three statements moved OUT of the wait function, one copy each",
         stripped + far,
         ["the deadline test is at line 73, outside the 40 lines",
          "the fresh read at expiry is at line 74, outside the 40 lines",
          "the yield is at line 76, outside the 40 lines"]),
        ("the wait function renamed away (signature gone)",
         base.replace("static bool spi_WaitStat(", "static bool spi_WaitStatus("),
         ["signature, found 0"]),
        ("the expiry read left only in a comment",
         base.replace(fresh,
                      "            /* return (((SPI1STAT & mask) != 0u) == want); */\n"
                      "            return false;\n"),
         ["fresh read at expiry, found 0"]),
    ]

    failures = 0
    for name, text, expect in cases:
        got = check_text("spi", text)
        problems = []

        if len(got) != len(expect):
            problems.append("expected exactly %d problem(s), got %d"
                            % (len(expect), len(got)))

        # One-to-one in BOTH directions. Left to right catches a per-item check
        # that was deleted; right to left catches one that fires when it should
        # not, which a substring search would have ignored.
        unclaimed = list(got)
        for want in expect:
            hits = [g for g in unclaimed if want in g]
            if len(hits) != 1:
                problems.append("expected exactly 1 problem containing %r, "
                                "found %d" % (want, len(hits)))
            if hits:
                unclaimed.remove(hits[0])
        for leftover in unclaimed:
            problems.append("unexpected problem: %r" % (leftover,))

        print("  [%s] %s" % ("ok" if not problems else "FAIL", name))
        if problems:
            failures += 1
            for pr in problems:
                print("        " + pr)
    print("self-test: %d/%d checks passed" % (len(cases) - failures, len(cases)))
    return 1 if failures else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    for key, path in REPO_DEFAULTS.items():
        ap.add_argument("--" + key, default=path)
    ap.add_argument("--self-test", action="store_true",
                    help="run the device-free checks and exit")
    a = ap.parse_args()

    if a.self_test:
        return self_test()

    problems = []
    for key in SHAPES:
        path = getattr(a, key)
        try:
            with open(path, encoding="utf-8", errors="replace") as fh:
                text = fh.read()
        except OSError as exc:
            problems.append("%s: cannot read %s (%s)" % (key, path, exc))
            continue
        problems.extend("%s [%s]" % (p, path) for p in check_text(key, text))

    if problems:
        print("ERROR: the #913 wait-loop shape has drifted from what")
        print("       tests/host/test_913_spi_wait_stat.c models:")
        for p in problems:
            print("         - " + p)
        print(ADVICE)
        return 1

    print("wait-loop shape: %d file(s) carry the #913 spin-then-yield shape "
          "in the required order." % len(SHAPES))
    return 0


if __name__ == "__main__":
    sys.exit(main())
