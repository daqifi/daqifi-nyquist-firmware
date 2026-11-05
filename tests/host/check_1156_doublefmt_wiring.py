#!/usr/bin/env python3
"""#1156: prove libscpi's double formatter is ROUTED to Util/DoubleFmt.h.

test_1156_double_fmt.c proves the formatter DECIDES correctly. It cannot
prove anything CALLS it: the header could be perfect and the vendored
SCPIDEFINE_doubleToStr still be `%.15lg`, and every host assertion would
still pass while the device kept answering rounded coefficients. That is the
same inert-pin shape #1199's wiring check exists for.

Runs inside the test binary's own Makefile recipe, not as a sibling target,
so the two halves of one claim -- the formatter is right / the formatter is
reached -- cannot be separately skipped.

Exit 0 = wired. Exit 1 = a finding, printed.
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
CONFIG_H = os.path.join(HERE, '..', '..', 'firmware', 'src', 'libraries',
                        'scpi', 'libscpi', 'inc', 'scpi', 'config.h')
HEADER = os.path.join(HERE, '..', '..', 'firmware', 'src', 'Util',
                      'DoubleFmt.h')

FORMATTER = 'daqifi_double_to_str'


def strip_block_comments(text):
    """Remove /* ... */ so a MENTION in prose is not read as code.

    The replaced block is documented in a comment that necessarily names
    `%.15lg` and HAVE_DTOSTRE -- the very tokens check 2 below forbids. A
    checker that grepped the raw file would fail on its own explanation.
    """
    # Each block becomes its OWN NEWLINES, not a space: collapsing them
    # shifted every reported line number (the define at config.h:235 was
    # reported as :163, and the error grew with the new comment block).
    return re.sub(r'/\*.*?\*/',
                  lambda m: '\n' * m.group(0).count('\n'), text, flags=re.S)


def active_define(code, name):
    """Every #define of `name`, as (line_number, full_line)."""
    out = []
    for i, line in enumerate(code.splitlines(), 1):
        if re.match(r'\s*#\s*define\s+%s\b' % re.escape(name), line):
            out.append((i, line.strip()))
    return out


def main():
    problems = []

    for path in (CONFIG_H, HEADER):
        if not os.path.exists(path):
            print('MISSING: %s' % path)
            return 1

    raw_config = open(CONFIG_H, encoding='utf-8', errors='replace').read()
    code = strip_block_comments(raw_config)
    header = open(HEADER, encoding='utf-8', errors='replace').read()

    # 1. the macro resolves to our formatter, and to nothing else
    defs = active_define(code, 'SCPIDEFINE_doubleToStr')
    if not defs:
        problems.append('config.h defines no SCPIDEFINE_doubleToStr at all')
    else:
        routed = [d for d in defs if FORMATTER in d[1]]
        if not routed:
            problems.append(
                'SCPIDEFINE_doubleToStr does NOT route to %s -- the device '
                'would keep answering rounded coefficients however correct '
                'DoubleFmt.h is. Found: %r' % (FORMATTER, defs))
        if len(defs) > len(routed):
            stray = [d for d in defs if FORMATTER not in d[1]]
            problems.append(
                'SCPIDEFINE_doubleToStr has %d definition(s) that do NOT use '
                '%s. The four-arm #if block was replaced by ONE unconditional '
                'definition precisely so a selector change cannot silently '
                'reintroduce the 15-digit format: %r'
                % (len(stray), FORMATTER, stray))

    # 2. no 15-digit renderer survives for DOUBLES. floatToStr is out of
    #    scope (#1156 is about doubles; a float needs at most 9 digits), so
    #    only lines mentioning doubleToStr are judged.
    for i, line in enumerate(code.splitlines(), 1):
        if 'doubleToStr' not in line:
            continue
        if re.search(r'%\.15l?g', line) or re.search(r'\bdtostre\b', line):
            problems.append(
                'config.h:%d still renders a double at 15 digits, which is '
                'the #1156 defect: %r' % (i, line.strip()))

    # 3. the header is actually included, or the macro expands to an
    #    implicit declaration and the fix compiles to nothing coherent
    inc = re.search(r'#\s*include\s+"([^"]*DoubleFmt\.h)"', code)
    if not inc:
        problems.append(
            'config.h does not #include DoubleFmt.h, so %s is undeclared '
            'where the macro expands' % FORMATTER)
    else:
        # The include must resolve RELATIVE TO config.h itself. The
        # project-rooted form "Util/DoubleFmt.h" compiles for the firmware
        # (../src is on its include path) but NOT for the host tests that
        # build libscpi's sources with only their stub dirs -- so a checker
        # that merely found the word would pass a form that breaks the
        # suite. Resolved on disk, which is the only check that distinguishes
        # them.
        resolved = os.path.normpath(
            os.path.join(os.path.dirname(CONFIG_H), inc.group(1)))
        if not os.path.exists(resolved):
            problems.append(
                'config.h includes %r, which does NOT resolve relative to '
                'config.h (%s). The host tests compile libscpi with only '
                'their stub dirs on the include path, so a project-rooted '
                'include fails there.' % (inc.group(1), resolved))

    # ⛔⛔ CHECKS 4 AND 5 WERE DELETED, and the reason is worth keeping.
    #
    # They grepped DoubleFmt.h for the loop header
    # (`for (digits = DAQIFI_DOUBLE_FMT_MIN_DIGITS`) and for the token
    # `strtod`, claiming to guard "narrow-first" and "the width is chosen by
    # parsing back". An adversarial review showed BOTH were unfalsifiable and
    # that each also broke on correct code:
    #
    #   * `strtod` was matched against the RAW header, so the prose comment
    #     mentioning strtod -- and `daqifi_double_round_trips`, which the
    #     header itself documents as "not used by the SCPI path" -- satisfied
    #     it. Replacing the round-trip test with `if (1)`, i.e. deleting the
    #     entire verification the check is named after, left this script at
    #     exit 0 with a "round-trip-verified" success banner.
    #   * The loop-header grep passed while the body hardcoded
    #     DAQIFI_DOUBLE_FMT_MAX_DIGITS in the snprintf, widening every value
    #     to 17 digits -- the exact regression it claimed to prevent.
    #   * And three semantically identical edits FAILED them, each breaking
    #     `make run` through this recipe: a `#define` split over a line
    #     continuation, `for (int digits = ...)` (legal C11), and renaming
    #     the loop variable to `prec`.
    #
    # Both behaviours ARE covered, by assertions that can fail: the host
    # test's `a_value_that_fits_15_digits_is_rendered_IDENTICALLY_to_before`
    # kills the hardcoded-width mutant, and `the_issues_four_reproduction_
    # rows_round_trip_now` plus the short-buffer oracle kill the removed
    # round-trip check. A source-diction pin added nothing over them and cost
    # three false failures.
    #
    # What survives here is only what the host test CANNOT see: whether the
    # vendored macro routes to this formatter at all (checks 1-3). That is
    # the one claim a host test linking the header directly can never make.

    if problems:
        print('#1156 double-formatter wiring: %d FINDING(S)' % len(problems))
        for p in problems:
            print('  - %s' % p)
        return 1

    print('#1156 double-formatter wiring OK: SCPIDEFINE_doubleToStr -> %s, '
          'one unconditional definition, no 15-digit double renderer '
          'left, and the include resolves relative to config.h. (The '
          'formatter BEHAVIOUR is the host test\'s claim, not this '
          'script\'s.)' % FORMATTER)
    return 0


if __name__ == '__main__':
    sys.exit(main())
