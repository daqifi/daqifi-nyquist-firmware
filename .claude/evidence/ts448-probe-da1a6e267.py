#!/usr/bin/env python3
"""Drive the REAL io_bytes() at head da1a6e267474 with scripted frames.

Read-only: imports the harness, never writes to it. Carries a known positive
and a known negative so both directions of the instrument are proven before
the unknown case is believed.
"""
import sys, time

sys.path.insert(0, "/mnt/c/daqifi/wt/audit-ts448")
import test_harness as H

LIST_TERMS = ['__END_OF_LIST__ OK', '__END_OF_LIST__ INCOMPLETE',
              '__END_OF_LIST__ FAILED']
ONE_TERM = ['__END_OF_LIST__ OK']


class FakeSerial:
    """Hands over `frame` in one chunk, then stays quiet forever."""
    def __init__(self, frame):
        self.buf = bytearray(frame)
        self.written = []

    @property
    def in_waiting(self):
        return len(self.buf)

    def read(self, n):
        out = bytes(self.buf[:n])
        del self.buf[:n]
        return out

    def write(self, b):
        self.written.append(b)
        return len(b)

    def flush(self):
        pass

    def reset_input_buffer(self):
        pass


def run(frame, until, whole_line=True):
    s = FakeSerial(frame)
    r = H.io_bytes(s, "SYST:STOR:SD:LISt?", quiet=0.2, hard=3.0,
                   until=until, purge=True, until_whole_line=whole_line)
    return r.confirmed


def window_of(until):
    return max(len(m) for m in until) + len(b"DAQIFI>") + 8


CASES = [
    # (label, frame, until, expectation, is_control)
    ("KP  clean frame, marker on its own line",
     b"aaa.csv\r\n__END_OF_LIST__ OK\r\nDAQIFI>\r\n", LIST_TERMS, True, True),

    ("KN  GLUED marker, small trailing (11 B)",
     b"aaa.csv__END_OF_LIST__ OK\r\nDAQIFI>\r\n", LIST_TERMS, False, True),

    ("??  GLUED marker, longest terminator, 15 B trailing",
     b"aaa.csv__END_OF_LIST__ INCOMPLETE\r\nDAQIFI>\t\t\t\t\r\n",
     LIST_TERMS, None, False),

    ("CTL same bytes, separator PRESENT (placement only)",
     b"aaa.csv\r\n__END_OF_LIST__ INCOMPLETE\r\nDAQIFI>\t\t\t\t\r\n",
     LIST_TERMS, True, True),

    ("??  GLUED marker, single-terminator set, 15 B trailing",
     b"aaa.csv__END_OF_LIST__ OK\r\nDAQIFI>\t\t\t\t\r\n", ONE_TERM, None, False),

    ("??  GLUED, single-term set, NO padding (11 B trailing)",
     b"aaa.csv__END_OF_LIST__ OK\r\nDAQIFI>\r\n", ONE_TERM, None, False),

    ("??  GLUED, longest term, doubled prompt instead of tabs",
     b"aaa.csv__END_OF_LIST__ INCOMPLETE\r\nDAQIFI>\r\nDAQIFI>\r\n",
     LIST_TERMS, None, False),
]

print("head      : da1a6e267474eacc686cfc585c9e4c7d6b1459b3")
print("window(3) :", window_of(LIST_TERMS), " window(1):", window_of(ONE_TERM))
print()
fails = 0
for label, frame, until, expect, is_ctl in CASES:
    w = window_of(until)
    trailing = len(frame) - (frame.rfind(b"__END_OF_LIST__")
                             + len(max((m for m in until), key=len)))
    got = run(frame, until)
    verdict = "confirmed=True " if got else "confirmed=False"
    tag = ""
    if expect is not None:
        ok = (got == expect)
        tag = "  [control OK]" if ok else "  [⛔ CONTROL FAILED]"
        if not ok:
            fails += 1
    print(f"{verdict}  {label}{tag}")
    print(f"                 window={w}  slice starts at "
          f"out[-{w}:] of {len(frame)} B")
print()
if fails:
    print("⛔ A CONTROL FAILED — the instrument is not trustworthy; "
          "no conclusion may be drawn from the unknown rows.")
else:
    print("Controls both directions OK (clean frame confirms, "
          "small-trailing glued frame refused).")
