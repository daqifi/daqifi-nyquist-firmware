#!/usr/bin/env python3
"""Self-test for the #909 lower-panel size guard inside cut_release.sh.

Why this exists
---------------
The bootloader erases only the LOWER program-flash panel (0x1D000000-0x1D0FFFFF)
so that an in-app customer update stops destroying the NVM settings pages at
0x9D1E0000 -- WiFi credentials, voltage precision, and both ADC calibration
slots. That fix is correct only while the application FITS in the lower panel.
If the app ever crosses 1 MB, the bootloader erases just the part below
0x1D100000 and then programs the rest into flash it never erased: a silent
corrupt-image update on a customer device.

cut_release.sh gates that. But the gate only ever runs during a real release
cut, which needs MPLAB X, XC32 and a Windows-side makefile regen -- so in
practice nobody would find out it had rotted until the release it was supposed
to protect. This runs the gate's real logic against synthetic .map files.

Runs the REAL guard rather than a copy: the shell block is extracted from
cut_release.sh between its >>>BEGIN/<<<END markers and executed by bash with
`die` stubbed and `MAP` pointed at a synthetic file. A duplicated copy would
drift from the shipping check -- the same reason selftest_release_hex_layout.py
extracts the hex validator instead of reimplementing it.

Cases, all synthetic .map text:
  1 a real observed usage, 0xb8bbc (756,668 B, 72% of the panel)  -> pass
  2 0xfffff, one byte under the panel                             -> pass
  3 0x100000 exactly, the first unsafe size                       -> FAIL
  4 0x180000, comfortably over                                    -> FAIL
  5 no 'Total kseg0_program_mem used' line at all                  -> FAIL
  6 the line present but with no parseable hex size                -> FAIL

Cases 5 and 6 are the point of the exercise as much as 3 and 4: a guard that
silently skips when it cannot find its input is worse than no guard, because it
reports success. Both must fail closed.

Usage: python3 tools/release/selftest_release_map_size.py
"""
import io
import os
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
CUT = os.path.join(HERE, "cut_release.sh")

BEGIN = ">>>BEGIN #909 lower-panel size guard"
END = "<<<END #909 lower-panel size guard"


def extract_guard():
    src = io.open(CUT, encoding="utf-8", errors="replace").read().replace("\r\n", "\n")
    m = re.search(
        re.escape(BEGIN) + r"\n(.*?)\n[^\n]*" + re.escape(END),
        src,
        re.S,
    )
    if not m:
        raise SystemExit(
            "FAIL: could not locate the #909 size-guard block in cut_release.sh "
            "(markers %r / %r)" % (BEGIN, END)
        )
    return m.group(1)


# A .map excerpt shaped like the real XC32 v4.60 output. The guard must find its
# line in amongst the surrounding noise, not just in a one-line file.
MAP_TEMPLATE = """\
Program Memory Usage
section                    address  length [bytes]      (dec)  Description
-------                 ----------  -------------------------  -----------
.reset                  0x9d000480         0x10          16
.text                   0x9d000600     0xb8000      753664
%s

kseg0 Boot-Memory Usage
section                    address  length [bytes]      (dec)  Description
         Total kseg0_boot_mem used  :           0           0
"""

USED_LINE = "      Total kseg0_program_mem used  :     %s      %d  %.1f%% of 0x200000"


def used_line(hexval, dec):
    return USED_LINE % (hexval, dec, dec * 100.0 / 0x200000)


# (name, map body, expected rc, required substring of the guard's own output)
#
# The substring matters as much as the exit code. Two of the guard's three
# failure paths -- "line missing" and "size unparseable" -- BOTH end with an
# empty size, so an exit-code-only test cannot tell them apart and would still
# pass with the missing-line check deleted (verified by mutation, 2026-09-18).
# Pinning the message is what makes each path independently tested, and it is
# also the thing a release engineer actually reads at 2am.
CASES = [
    # A real observed figure (standalone build of main, 2026-09-08) rather than
    # a made-up one, so this case stays representative of what the guard sees.
    ("a real observed usage, 0xb8bbc", used_line("0xb8bbc", 0xB8BBC), 0,
     "< 0x100000 lower panel OK"),
    ("0xfffff — one byte under the panel", used_line("0xfffff", 0xFFFFF), 0,
     "< 0x100000 lower panel OK"),
    ("0x100000 — first unsafe size", used_line("0x100000", 0x100000), 1,
     "LOWER FLASH PANEL"),
    ("0x180000 — well over the panel", used_line("0x180000", 0x180000), 1,
     "LOWER FLASH PANEL"),
    ("usage line absent entirely", "  (no usage line here)", 1,
     "could not find 'Total kseg0_program_mem used'"),
    ("usage line present, size unparseable", "      Total kseg0_program_mem used  :     ????", 1,
     "could not parse a hex size"),
]


def run(guard, map_body):
    """Run the extracted guard with MAP pointed at a synthetic map.

    `die` is stubbed to exit 1 the way cut_release.sh's own does, and `set -u`
    matches the shipping script's `set -euo pipefail` so an unset-variable bug
    in the guard shows up here rather than at release time.
    """
    with tempfile.NamedTemporaryFile("w", suffix=".map", delete=False) as f:
        f.write(MAP_TEMPLATE % map_body)
        path = f.name
    script = (
        "set -u\n"
        "die() { printf 'FATAL: %s\\n' \"$*\" >&2; exit 1; }\n"
        'MAP="$1"\n'
    ) + guard + "\n"
    try:
        p = subprocess.run(["bash", "-c", script, "bash", path],
                           capture_output=True, text=True)
        return p.returncode, (p.stdout or "") + (p.stderr or "")
    finally:
        os.unlink(path)


def main():
    guard = extract_guard()
    # A guard that lost its threshold would pass every case below for the wrong
    # reason, so assert the constant is still in the extracted text.
    if "0x100000" not in guard:
        print("FAIL: the extracted guard no longer mentions the 0x100000 panel bound")
        return 1
    failures = 0
    for name, body, want, needle in CASES:
        rc, out = run(guard, body)
        rc_ok = (rc == 0) if want == 0 else (rc != 0)
        msg_ok = needle in out
        ok = rc_ok and msg_ok
        why = "" if ok else ("  [rc mismatch]" if not rc_ok else "  [missing %r]" % needle)
        print("  %-38s rc=%d want=%s -> %s%s"
              % (name, rc, "pass" if want == 0 else "fail", "PASS" if ok else "FAIL", why))
        if not ok:
            failures += 1
            print(out.rstrip())
    print("\n===== selftest_release_map_size: %d PASS, %d FAIL ====="
          % (len(CASES) - failures, failures))
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
