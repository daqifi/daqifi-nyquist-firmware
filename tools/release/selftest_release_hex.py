#!/usr/bin/env python3
"""Self-test for check_release_hex.py — runs in CI with no hardware.

Builds synthetic Intel HEX images of each shape and asserts the guard's verdict.
Without this the guard could silently rot into a rubber stamp.
"""
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).parent
GUARD = HERE / 'check_release_hex.py'


def rec(addr, data):
    """One 00-type data record at a 16-bit offset."""
    n = len(data)
    body = [n, (addr >> 8) & 0xFF, addr & 0xFF, 0x00] + list(data)
    return ':' + ''.join(f'{b:02X}' for b in body) + \
           f'{((-sum(body)) & 0xFF):02X}\n'


def ext(hi):
    body = [2, 0, 0, 0x04, (hi >> 8) & 0xFF, hi & 0xFF]
    return ':' + ''.join(f'{b:02X}' for b in body) + \
           f'{((-sum(body)) & 0xFF):02X}\n'


def build(path, *, reset_bytes=408, boot_flash=False, app=True):
    out = [ext(0x1D00)]
    written = 0
    addr = 0x0000
    while written < reset_bytes:
        n = min(16, reset_bytes - written)
        out.append(rec(addr, [0xAA] * n))
        addr += n
        written += n
    if app:
        out.append(rec(0x0480, [0x55] * 16))
    if boot_flash:
        out.append(ext(0x1FC0))
        out.append(rec(0x0000, [0x77] * 16))
    out.append(':00000001FF\n')
    path.write_text(''.join(out))


def run(path):
    r = subprocess.run([sys.executable, str(GUARD), str(path)],
                       capture_output=True, text=True)
    return r.returncode, r.stdout


CASES = [
    ('bootloader-linked (correct)', dict(), 0),
    ('standalone: has boot flash', dict(boot_flash=True), 1),
    ('wrong reset size', dict(reset_bytes=1152), 1),
    ('no application payload', dict(app=False), 1),
]

def main():
    failures = 0
    with tempfile.TemporaryDirectory() as td:
        for name, kw, want in CASES:
            p = Path(td) / 'case.hex'
            build(p, **kw)
            code, out = run(p)
            ok = code == want
            print(f'  [{"PASS" if ok else "FAIL"}] {name:32s} exit={code} want={want}')
            if not ok:
                failures += 1
                print('\n'.join('        ' + l for l in out.splitlines()))
    print(f'\n===== release_hex_selftest: {len(CASES) - failures} PASS, '
          f'{failures} FAIL =====')
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
