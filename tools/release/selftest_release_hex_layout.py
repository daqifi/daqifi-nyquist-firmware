#!/usr/bin/env python3
"""Self-test for the release-hex layout validator inside cut_release.sh.

Runs the REAL validator rather than a copy: the python block is extracted from
cut_release.sh between its heredoc markers and executed. A duplicated copy
would drift from the shipping check, and this repo has been bitten by exactly
that before (a from-scratch check_release_hex.py that re-implemented checks
cut_release.sh already made).

Cases, all synthetic Intel-HEX:
  1 a well-formed BOOTLOADER-LINKED image                 -> pass
  2 the same image plus one boot-flash record @0x1FC00000 -> FAIL (#764)
  3 a STANDALONE image (bulk at 0x1D000000)               -> fail
  4 a truncated reset vector (407 bytes)                  -> fail

Usage: python3 tools/release/selftest_release_hex_layout.py
"""
import io, os, re, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
CUT = os.path.join(HERE, "cut_release.sh")


def extract_validator():
    src = io.open(CUT, encoding="utf-8", errors="replace").read().replace("\r\n", "\n")
    m = re.search(r"python3 - \"\$HEX\" <<'PY'.*?\n(.*?)\nPY\n", src, re.S)
    if not m:
        raise SystemExit("FAIL: could not locate the validator heredoc in cut_release.sh")
    return m.group(1)


def hex_records(chunks):
    """chunks: list of (addr, nbytes). Emits minimal Intel-HEX."""
    out = []
    cur_base = None
    for addr, n in chunks:
        base = addr >> 16
        if base != cur_base:
            payload = "%04X" % base
            b = 2 + 0 + 4 + int(payload[0:2], 16) + int(payload[2:4], 16)
            out.append(":02000004%s%02X" % (payload, ((~b + 1) & 0xFF)))
            cur_base = base
        off = addr & 0xFFFF
        while n > 0:
            take = min(n, 16)
            data = "00" * take
            s = take + ((off >> 8) & 0xFF) + (off & 0xFF) + 0 + 0
            out.append(":%02X%04X00%s%02X" % (take, off, data, ((~s + 1) & 0xFF)))
            off += take
            n -= take
    out.append(":00000001FF")
    return "\n".join(out) + "\n"


def run(validator, chunks):
    with tempfile.NamedTemporaryFile("w", suffix=".hex", delete=False) as f:
        f.write(hex_records(chunks))
        path = f.name
    try:
        p = subprocess.run([sys.executable, "-c", validator, path],
                           capture_output=True, text=True)
        # Return BOTH streams. If the extracted validator raises, the traceback
        # goes to stderr -- dropping it leaves a failing test with nothing to
        # debug from, which is the failure mode this self-test exists to catch.
        return p.returncode, (p.stdout or '') + (p.stderr or '')
    finally:
        os.unlink(path)


GOOD = [(0x1D000000, 408), (0x1D000480, 256)]
CASES = [
    ("bootloader-linked image",              GOOD,                                   0),
    ("boot-flash record present (#764)",     GOOD + [(0x1FC00000, 16)],              1),
    ("standalone image (bulk at base)",      [(0x1D000000, 256)],                    1),
    ("truncated reset vector (407 B)",       [(0x1D000000, 407), (0x1D000480, 256)], 1),
]

def main():
    validator = extract_validator()
    if "BOOT_FLASH_LO" not in validator:
        print("FAIL: the extracted validator has no boot-flash check (#764)")
        return 1
    failures = 0
    for name, chunks, want in CASES:
        rc, out = run(validator, chunks)
        ok = (rc == 0) if want == 0 else (rc != 0)
        print("  %-38s rc=%d want=%s -> %s" % (name, rc, "pass" if want == 0 else "fail",
                                               "PASS" if ok else "FAIL"))
        if not ok:
            failures += 1
            print(out.rstrip())
    print("\n===== selftest_release_hex_layout: %d PASS, %d FAIL ====="
          % (len(CASES) - failures, failures))
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
