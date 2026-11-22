#!/usr/bin/env python3
"""Is this C file's comment structure still intact? One defect class, soundly.

⛔ WHY THIS EXISTS. The #908 guards read daqifi_settings.c, SCPIADC.c and
SCPIInterface.c with `sed`, `grep` and `awk` -- and NOTHING in tests/host
COMPILES them. So on 2026-10-27 an edit that inserted a `#define` into the
middle of a block comment closed that comment early and stranded thirteen
lines of ` *` continuation at file scope. The firmware did not compile. The
host suite reported 31 binaries, 318 tests, 0 failed, and every #908 source
guard PASSED -- because a position pin over `grep -n` line numbers is
perfectly happy with a file that is not valid C.

The real fix would be for a guard to require the file to PARSE, and the host
suite cannot do that: these are Harmony translation units whose include set is
not available here (the project builds them with XC32 and ~17 -I paths). So
this checks the structural property those sed/grep guards silently depend on,
and that the breakage violated:

  * every block comment is terminated
  * no `*/` is reached while not inside a comment

Both are SOUND -- in valid C neither can occur -- and soundness matters more
than coverage here. A third, more direct check ("no line outside a comment
begins with a star") was tried in two forms and DROPPED, because it fires on
real pointer-deref and continued-expression lines; see the note in scan(). A
guard with false positives on files that are fine gets switched off, and then
catches nothing at all. The stray `*/` is what an early-closed block leaves
behind, so the sound check reaches the same defect by its other end.

⚠ A PASS HERE IS NOT A COMPILE, and must never be reported as one. It says the
comment structure is intact. Only a build says the file builds.

String and character literals are tracked, so `"/*"` inside a string does not
open a comment -- otherwise this would invent failures in correct files.
"""
import sys


def scan(path):
    """Return a list of (line_no, kind, text) problems."""
    try:
        src = open(path, 'rb').read().decode('utf-8', errors='replace')
    except OSError as e:
        return [(0, 'unreadable', str(e))]

    problems = []
    in_block = False
    block_start = 0

    # ⛔ NO SHAPE-BASED "orphaned continuation" CHECK, and the history is worth
    # keeping so it is not re-added. Two versions were tried, both unsound on
    # real C in this tree:
    #
    #   startswith('*')            -> fired on `*duration_sec > 3600) {`
    #                                 (SCPIInterface.c: a deref continuing a
    #                                 multi-line condition)
    #   == '*' or startswith('* ') -> still fired on THREE real lines across
    #                                 400 files: `* transferHandle = ...;`
    #                                 (usb_device_cdc.c), `* clockTerm *
    #                                 (uint64_t)timestampHz;` (MC12bADC.c),
    #                                 `* 0 + tag` (nanopb/pb.h)
    #
    # "A line beginning with a star is comment prose" is simply false: `* ptr =
    # x;` and a continued multiplication both occur. Each attempt was caught by
    # running the NEGATIVE control over the whole tree, not only the crafted
    # positive -- which is the only reason neither shipped.
    for n, raw in enumerate(src.split('\n'), 1):
        line = raw.rstrip('\r')
        stripped = line.strip()
        i, in_str, in_chr = 0, False, False
        while i < len(line):
            two = line[i:i + 2]
            c = line[i]
            if in_block:
                if two == '*/':
                    in_block = False
                    i += 2
                    continue
                i += 1
                continue
            if in_str:
                if c == '\\':
                    i += 2
                    continue
                if c == '"':
                    in_str = False
                i += 1
                continue
            if in_chr:
                if c == '\\':
                    i += 2
                    continue
                if c == "'":
                    in_chr = False
                i += 1
                continue
            if two == '*/':
                # A closer with nothing open. In valid C this cannot happen
                # outside a string or a // comment, both tracked here -- so it
                # is the signature of a block closed early, leaving its
                # original terminator stranded. Detected in the walker rather
                # than by line shape because the stranded closer sits at a
                # line's END, which a startswith('*/') test misses.
                problems.append((n, 'stray-close', stripped[:76]))
                i += 2
                continue
            if two == '/*':
                in_block = True
                block_start = n
                i += 2
                continue
            if two == '//':
                break
            if c == '"':
                in_str = True
            elif c == "'":
                in_chr = True
            i += 1

    if in_block:
        problems.append((block_start, 'unterminated-block',
                         'opened here and never closed before EOF'))
    return problems


def main():
    paths = sys.argv[1:]
    if not paths:
        print(__doc__)
        return 2
    rc = 0
    for p in paths:
        probs = scan(p)
        if not probs:
            print(f'  [OK] {p}: comment structure intact')
            continue
        rc = 1
        print(f'  [FAIL] {p}: {len(probs)} problem(s)')
        for n, kind, text in probs[:8]:
            print(f'      {p}:{n}: {kind}: {text}')
        print('      ⛔ This file almost certainly does NOT COMPILE. A `*/` '
              'with nothing open, or an unterminated block, is a syntax '
              'error. The usual cause is inserting a declaration or #define '
              'into the middle of a block comment, which closes it early and '
              'strands the rest -- and every sed/grep source guard will keep '
              'passing, because none of them parse the file.')
    print('comment-integrity guard: ' + ('OK' if rc == 0 else 'FAILED'))
    return rc


if __name__ == '__main__':
    sys.exit(main())
