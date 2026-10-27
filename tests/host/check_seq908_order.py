#!/usr/bin/env python3
"""#908 r11: assert the cal-source seqlock's bank read stays INSIDE its window.

The property under test exists only AFTER optimisation, so it cannot be
checked by reading C or by running a test binary: `volatile` on the generation
counter does not stop the compiler moving the NON-volatile `gActiveCalBank`
load across it, and the moved load is still perfectly correct C. So this
compiles the fixture to assembly and asks the optimiser what it did.

DIFFERENTIAL, with the defect as the known positive:

  reader_unbarriered  the bank load MUST escape the window  (instrument live)
  reader_barriered    the bank load MUST stay inside it     (firmware correct)

The first half is what makes the second half evidence. A checker that only
asserted the good ordering would also pass on a compiler that never reorders,
and would hand a clean bill to a firmware that had lost its barriers.

Run: python3 check_seq908_order.py <compiler> <fixture.c> [-- extra flags]
"""
import re
import subprocess
import sys
import tempfile
from pathlib import Path


def body(asm, fn):
    """The instructions of one function, by label, up to the next .size."""
    out, on = [], False
    for line in asm.split('\n'):
        if re.match(rf'^{re.escape(fn)}:', line.strip()):
            on = True
            continue
        if on:
            if re.match(r'^\s*\.size', line) and fn in line:
                break
            out.append(line)
    return out


def order(asm, fn):
    """Sequence of ('gen'|'bank'|'call'|'branch') events, in emitted order.

    Keyed on the SYMBOL NAMES, which survive every target's syntax, rather than
    on a register or addressing form -- the same checker then works for x86
    `gCalGeneration(%rip)` and MIPS `4(gp)` without knowing either.
    """
    ev = []
    for line in body(asm, fn):
        s = line.strip()
        if s.startswith(('.', '#')) or not s:
            continue
        has_gen = 'gCalGeneration' in s
        has_bank = 'gActiveCalBank' in s
        if has_gen and has_bank:
            return None, f'{fn}: one instruction touches both symbols: {s!r}'
        if has_gen:
            ev.append('gen')
        elif has_bank:
            ev.append('bank')
        elif re.match(r'^(call|jal)\b', s):
            ev.append('call')
        elif re.match(r'^(j|b)[a-z]*\b', s) and 'bank' not in s:
            ev.append('branch')
    return ev, None


def bank_inside_window(ev):
    """Is the bank read between the opening and closing generation reads?

    The window is [first 'gen', second 'gen']. Returns (verdict, why).
    """
    gens = [i for i, e in enumerate(ev) if e == 'gen']
    banks = [i for i, e in enumerate(ev) if e == 'bank']
    if len(gens) < 2:
        return None, f'expected >= 2 generation accesses, saw {len(gens)}'
    if len(banks) != 1:
        return None, f'expected exactly 1 bank access, saw {len(banks)}'
    return gens[0] < banks[0] < gens[1], (
        f'gen@{gens[0]} bank@{banks[0]} gen@{gens[1]}')


def main():
    argv = sys.argv[1:]
    extra = []
    if '--' in argv:
        i = argv.index('--')
        argv, extra = argv[:i], argv[i + 1:]
    if len(argv) != 2:
        print(__doc__)
        return 2
    cc, src = argv
    if not Path(src).is_file():
        print(f'ERROR: fixture not found: {src}')
        return 1

    with tempfile.TemporaryDirectory() as td:
        out = Path(td) / 'fixture.s'
        cmd = [cc, '-O3', '-S', '-o', str(out), src] + extra
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode != 0:
            print(f'ERROR: could not compile the fixture to assembly.')
            print(f'       {" ".join(cmd)}')
            print(f'       {(r.stderr or r.stdout).strip()[:400]}')
            return 1
        asm = out.read_text()

    rc = 0
    expect = {'reader_unbarriered': False, 'reader_barriered': True}
    for fn, want_inside in expect.items():
        ev, err = order(asm, fn)
        if err:
            print(f'ERROR: {err}')
            rc = 1
            continue
        if not ev:
            print(f'ERROR: {fn} not found in the generated assembly -- the '
                  f'fixture or its function names changed, so this guard is '
                  f'measuring nothing.')
            rc = 1
            continue
        got, why = bank_inside_window(ev)
        if got is None:
            print(f'ERROR: {fn}: cannot locate the window ({why}). The guard '
                  f'refuses rather than guessing: a missing access could be a '
                  f'folded load, which is a fixture problem, not a verdict.')
            rc = 1
            continue
        label = 'INSIDE' if got else 'OUTSIDE'
        ok = (got == want_inside)
        print(f'  [{"OK" if ok else "FAIL"}] {fn}: bank read {label} the '
              f'validated window ({why}); wanted '
              f'{"INSIDE" if want_inside else "OUTSIDE"}')
        if not ok:
            rc = 1
            if want_inside:
                print('        ⛔ THE FIRMWARE PROPERTY IS BROKEN. The bank is '
                      'sampled after the closing generation check, so a '
                      'writer landing in between installs a bank the check '
                      'cannot see: the reader reports the NEW bank against '
                      'coefficients scanned from the OLD one -- neither the '
                      'before answer nor the after answer. Restore the '
                      'CAL_BARRIER() around the snapshot in '
                      'daqifi_settings_GetCalSource.')
            else:
                print('        ⚠ THE INSTRUMENT IS DEAD, NOT THE FIRMWARE. The '
                      'unbarriered reader was supposed to reproduce the '
                      'defect and did not, so this compiler is not reordering '
                      'here and a pass on the barriered reader proves '
                      'nothing. Do not read the other line as a clean bill.')
    print('seq908 ordering guard: ' + ('OK' if rc == 0 else 'FAILED'))
    return rc


if __name__ == '__main__':
    sys.exit(main())
