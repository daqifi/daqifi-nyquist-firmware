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
Textual presence and order, nothing more. It cannot see control flow, so it
does not prove the expiry branch is REACHED, nor that the fresh read's value
is what the caller receives. Those are the host test's job, and the host test
is only meaningful while this checker says its model still matches the source.
The two halves are useless apart.
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

    if len(found) == len(SHAPES[key]):
        order = [found[label] for label, _ in SHAPES[key]]
        if order != sorted(order):
            names = ", ".join(
                "%s@%d" % (label, found[label]) for label, _ in SHAPES[key])
            problems.append(
                "%s: the three statements are out of order (%s). The deadline "
                "test must come before the fresh read, and both before the "
                "yield." % (key, names))
    return problems


def self_test():
    """Every check must FAIL on the shape it exists to refuse."""
    base = """static bool spi_WaitStat(uint32_t mask, bool want,
                         TickType_t start, TickType_t timeoutTicks) {
    for (;;) {
        for (uint32_t s = 0; s < 8000u; ++s) {
            if (((SPI1STAT & mask) != 0u) == want) { return true; }
        }
        if (((SPI1STAT & mask) != 0u) == want) { return true; }
        if ((TickType_t)(xTaskGetTickCount() - start) >= timeoutTicks) {
            return (((SPI1STAT & mask) != 0u) == want);
        }
        vTaskDelay(1);
    }
}
"""
    cases = [
        ("clean source passes", base, 0),
        ("the pre-#913 hardcoded false at expiry",
         base.replace("            return (((SPI1STAT & mask) != 0u) == want);",
                      "            return false;"), 1),
        ("the yield removed (back to a pure busy-spin)",
         base.replace("        vTaskDelay(1);\n", ""), 1),
        ("the deadline test removed (never times out)",
         base.replace(
             "        if ((TickType_t)(xTaskGetTickCount() - start) >= timeoutTicks) {\n",
             "        if (0) {\n"), 1),
        ("yield moved BEFORE the deadline test",
         "static bool spi_WaitStat(uint32_t mask, bool want,\n"
         "                         TickType_t start, TickType_t timeoutTicks) {\n"
         "    for (;;) {\n"
         "        vTaskDelay(1);\n"
         "        if ((TickType_t)(xTaskGetTickCount() - start) >= timeoutTicks) {\n"
         "            return (((SPI1STAT & mask) != 0u) == want);\n"
         "        }\n"
         "    }\n"
         "}\n", 1),
        ("a second copy of the loop (ambiguous which one is pinned)",
         base + base, 1),
        ("the expiry read mentioned only in a comment",
         base.replace("            return (((SPI1STAT & mask) != 0u) == want);",
                      "            /* return (((SPI1STAT & mask) != 0u) == want); */\n"
                      "            return false;"), 1),
    ]
    failures = 0
    for name, text, want_problems in cases:
        got = check_text("spi", text)
        ok = (len(got) > 0) == (want_problems > 0)
        print("  [%s] %s" % ("ok" if ok else "FAIL", name))
        if not ok:
            failures += 1
            print("        expected %s, got %r"
                  % ("a problem" if want_problems else "no problem", got))
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
