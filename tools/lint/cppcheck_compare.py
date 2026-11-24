#!/usr/bin/env python3
"""Line-insensitive comparison of cppcheck baseline vs current output (#1309).

Usage: cppcheck_compare.py BASELINE CURRENT
       cppcheck_compare.py --count-added BASELINE CURRENT
       cppcheck_compare.py --self-test

Each line has the template `{file}:{line}:{column}: {severity}: {message} [{id}]`.
The `:<line>:<col>` after the file path is stripped before comparing, so an
edit that merely shifts code does not fail the gate. (Audited on the #1309
baseline: no cppcheck message text embeds a line number, so nothing else is
normalised.) The comparison is over the MULTISET of normalised lines: a second
identical finding in the same file counts as a new finding.

Known residual: within one file, one finding removed and another added with the
same message and id nets to zero and passes.

--count-added prints how many findings in CURRENT are not in BASELINE (same
comparison) and exits 0; cppcheck.sh --write-baseline refuses unless it is 0.

Exit: 0 equal, 1 differ, 2 usage/IO error.
"""
import re
import sys
from collections import Counter

_POS = re.compile(r"^(.*?):\d+:\d+: (?=\w+: )")


def normalise(line):
    return _POS.sub(r"\1: ", line.rstrip("\r\n"), count=1)


def _read(path):
    with open(path, encoding="utf-8", errors="replace") as f:
        return [l.rstrip("\r\n") for l in f if l.strip()]


def compare(base_lines, cur_lines):
    """Return (added, removed, added_keys, removed_keys); added/removed are ORIGINAL lines."""
    bc = Counter(normalise(l) for l in base_lines)
    cc = Counter(normalise(l) for l in cur_lines)
    added_keys = cc - bc
    removed_keys = bc - cc
    added = [l for l in cur_lines if normalise(l) in added_keys]
    removed = [l for l in base_lines if normalise(l) in removed_keys]
    return added, removed, added_keys, removed_keys


def count_added(base_lines, cur_lines):
    _, _, added_keys, _ = compare(base_lines, cur_lines)
    return sum(added_keys.values())


def self_test():
    a = "f.c:10:5: style: Variable 'x' can be declared as pointer to const [constVariablePointer]"
    b = "g.c:7:1: error: Array 'a[4]' accessed at index 5, which is out of bounds. [arrayIndexOutOfBounds]"

    def shift(s):
        return re.sub(r":\d+:\d+:", ":99:3:", s, count=1)

    def differs(base, cur):
        _, _, ak, rk = compare(base, cur)
        return bool(ak or rk)

    added, _, _, _ = compare([a], [a, shift(a)])
    c = "f.c:20:5: warning: Memory leak: p [memleak]"
    checks = [
        ("count-added: equal and line shift are 0", count_added([a, b], [shift(b), a]) == 0),
        ("count-added: a resolved finding is 0", count_added([a, b], [a]) == 0),
        ("count-added: a swap counts the new one", count_added([a], [c]) == 1),
        ("count-added: duplicates count each", count_added([a], [a, shift(a), c]) == 2),
        ("count-added: empty baseline counts all", count_added([], [a, b]) == 2),
        ("equal", not differs([a, b], [b, a])),
        ("line shift is equal", not differs([a, b], [shift(a), shift(b)])),
        ("new finding differs", differs([a], [a, b])),
        ("duplicate in same file differs (multiset)", differs([a], [a, shift(a)])),
        ("removed finding differs", differs([a, b], [a])),
        ("empty vs empty equal", not differs([], [])),
        ("CRLF ignored", not differs([a], [a + "\r"])),
        ("added entries keep original line numbers", len(added) == 2 and any(":99:3:" in x for x in added)),
    ]
    for n, ok in checks:
        print(("PASS " if ok else "FAIL ") + n)
    return 0 if all(ok for _, ok in checks) else 1


def main(argv):
    if argv[1:] == ["--self-test"]:
        return self_test()
    counting = argv[1:2] == ["--count-added"]
    if counting:
        argv = argv[:1] + argv[2:]
    if len(argv) != 3:
        print("usage: cppcheck_compare.py [--count-added] BASELINE CURRENT | --self-test", file=sys.stderr)
        return 2
    try:
        base, cur = _read(argv[1]), _read(argv[2])
    except OSError as e:
        print(f"cppcheck_compare: {e}", file=sys.stderr)
        return 2
    if counting:
        print(count_added(base, cur))
        return 0
    added, removed, ak, rk = compare(base, cur)
    if not ak and not rk:
        print(f"cppcheck baseline unchanged ({len(base)} findings, line-insensitive)")
        return 0
    print("cppcheck findings differ from the committed baseline (line-insensitive)")
    if ak:
        print(f"\nNEW findings ({sum(ak.values())}); every entry whose message matches is listed:")
        print("real bug: fix it; false positive: suppress with a reason")
        for l in added:
            print("  + " + l)
    if rk:
        print(f"\nRESOLVED findings ({sum(rk.values())}); regenerate: bash tools/lint/cppcheck.sh --write-baseline, commit the baseline:")
        for l in removed:
            print("  - " + l)
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
