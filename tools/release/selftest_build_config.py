#!/usr/bin/env python3
"""Self-test for check_build_config.py (#764).

Exists because of a real defect the pre-merge audit caught on PR #765: the
guard searched the WHOLE configurations.xml for the linker <item> entries,
but that file carries a full item set PER CONFIGURATION — three copies of
each script (default, Nq1, Nq3). Prune the default-conf entry and the search
fell through to the Nq1 copy, so the guard reported OK on precisely the
pruned-project state it exists to catch, while cut_release.sh (which scopes
its own lookup with awk bounded by <conf name="default">) would die on its
precondition at release time.

Every fixture below therefore carries a decoy Nq1 block in the OPPOSITE
state. If the conf scoping regresses, the decoy is what gets read and the
verdict flips — the test fails rather than silently passing.

Usage: selftest_build_config.py       (exit 0 = all cases pass)
"""
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
GUARD = HERE / 'check_build_config.py'

# Nq1 decoy: BOTH included (= "ambiguous"), which is never the expected
# verdict for any default-conf state below. Reading it by mistake is visible.
DECOY = '''      <item path="../src/config/default/p32MZ2048EFM144.ld" ex="false"></item>
      <item path="../src/config/default/old_hv2_bootld.ld" ex="false"></item>'''

STANDALONE = '''      <item path="../src/config/default/p32MZ2048EFM144.ld" ex="true"></item>
      <item path="../src/config/default/old_hv2_bootld.ld" ex="true"></item>'''
BOOTLOADER = '''      <item path="../src/config/default/p32MZ2048EFM144.ld" ex="true"></item>
      <item path="../src/config/default/old_hv2_bootld.ld" ex="false"></item>'''
BOTH_IN = '''      <item path="../src/config/default/p32MZ2048EFM144.ld" ex="false"></item>
      <item path="../src/config/default/old_hv2_bootld.ld" ex="false"></item>'''
PRUNED = '''      <item path="../src/config/default/p32MZ2048EFM144.ld" ex="true"></item>'''
# p32 selected alone: looks like a harmless "standalone" choice in the IDE, but
# cut_release.sh only ever flips old_hv2_bootld.ld and never inspects p32, so a
# release cut from here leaves BOTH scripts selected. Tracked as #767.
P32_ONLY = '''      <item path="../src/config/default/p32MZ2048EFM144.ld" ex="false"></item>
      <item path="../src/config/default/old_hv2_bootld.ld" ex="true"></item>'''

# Two entries for one script (merge artifact): the effective flag would depend
# on which one the toolchain reads, so the guard must refuse rather than pick.
DUPLICATE = '''      <item path="../src/config/default/p32MZ2048EFM144.ld" ex="true"></item>
      <item path="../src/config/default/old_hv2_bootld.ld" ex="true"></item>
      <item path="../src/config/default/old_hv2_bootld.ld" ex="false"></item>'''
# Attribute order is not significant in XML; the guard must not depend on it.
ATTR_REVERSED = '''      <item ex="true" path="../src/config/default/p32MZ2048EFM144.ld"></item>
      <item ex="true" path="../src/config/default/old_hv2_bootld.ld"></item>'''

# ex= values the guard must refuse rather than silently read as "excluded":
# a bare `ex == 'false'` test maps every unknown string to True (excluded).
MALFORMED_EX = '''      <item path="../src/config/default/p32MZ2048EFM144.ld" ex="maybe"></item>
      <item path="../src/config/default/old_hv2_bootld.ld" ex="true"></item>'''


# --- #791: inert per-file optimization overrides --------------------------
# The historical defect: #426 replaced FreeRTOS_tasks.c's -O1 clamp by BLANKING
# the C32 value instead of deleting the <item>. An active override with an
# empty value emits no -O flag, so the file fell to -O0 while the conf built at
# -O3. The stale '-O1' that looks like the intent lives in <C32CPP>, which this
# C project never invokes -- reading THAT is how the state was misread, so a
# case below pins that the C++ value is ignored.
INERT_OPT = STANDALONE + '''
      <item path="../src/third_party/rtos/FreeRTOS/Source/FreeRTOS_tasks.c"
            ex="false" overriding="true">
        <C32><property key="optimization-level" value=""/></C32>
        <C32CPP><property key="optimization-level" value="-O1"/></C32CPP>
      </item>'''

# An explicit value is a legitimate override (tfm.c really does need one).
EXPLICIT_OPT = STANDALONE + '''
      <item path="../src/third_party/wolfssl/wolfcrypt/src/tfm.c"
            ex="false" overriding="true">
        <C32><property key="optimization-level" value="-O3"/></C32>
      </item>'''

# overriding="false": the block is inactive, so its empty value emits nothing.
# Flagging it would fire on ordinary project files.
INACTIVE_OPT = STANDALONE + '''
      <item path="../src/third_party/rtos/FreeRTOS/Source/FreeRTOS_tasks.c"
            ex="false" overriding="false">
        <C32><property key="optimization-level" value=""/></C32>
      </item>'''

# Empty ONLY in C++ -- the C compilation is untouched, so this must pass.
CPP_ONLY_EMPTY = STANDALONE + '''
      <item path="../src/third_party/rtos/FreeRTOS/Source/FreeRTOS_tasks.c"
            ex="false" overriding="true">
        <C32><property key="optimization-level" value="-O3"/></C32>
        <C32CPP><property key="optimization-level" value=""/></C32CPP>
      </item>'''


CASES = [
    (0, 'standalone', STANDALONE,
     'both excluded -> bench default (what main has)'),
    (1, 'bootloader', BOOTLOADER,
     'bootloader-linked committed -> breaks cut_release.sh precondition'),
    (1, 'ambiguous', BOTH_IN,
     'both included -> layout depends on regen order'),
    (1, 'p32-only', P32_ONLY,
     'p32 selected alone -> cut_release.sh flip leaves BOTH selected'),
    (2, 'duplicate', DUPLICATE,
     'two entries for one script -> refuse, do not read the first'),
    (0, 'attr-order', ATTR_REVERSED,
     'ex= before path= -> XML attribute order is not significant'),
    (0, 'comment-shadow', STANDALONE,
     'commented-out conf must not shadow the live one'),
    (2, 'dup-conf', STANDALONE,
     'two <conf name="default"> blocks -> refuse, do not read the first'),
    (2, 'malformed-ex', MALFORMED_EX,
     'ex="maybe" must not silently read as excluded'),
    (2, 'pruned', PRUNED,
     'default-conf entry gone, Nq1 copy present -> must NOT fall through'),
    (1, 'inert-opt', INERT_OPT,
     '#791: overriding=true with an EMPTY C32 value -> file builds at -O0'),
    (0, 'explicit-opt', EXPLICIT_OPT,
     'an override with a real value (-O3, as tfm.c has) is legitimate'),
    (0, 'inactive-opt', INACTIVE_OPT,
     'overriding=false emits nothing -> must not fire on ordinary items'),
    (0, 'cpp-only-empty', CPP_ONLY_EMPTY,
     'empty in <C32CPP> only -> C build unaffected, and C++ is never invoked'),
]


# A commented-out <conf name="default"> preceding the real one. Raised by the
# pre-merge audit against the old regex parser, which matched text inside the
# comment and reported the fake block's state. An XML parser discards comments,
# so this is now structurally impossible — the case pins that.


def project(default_items, shadow=False, twice=False):
    ghost = ('  <!-- <conf name="default" type="2">'
             '<item path="../src/config/default/p32MZ2048EFM144.ld" ex="false">'
             '</item>'
             '<item path="../src/config/default/old_hv2_bootld.ld" ex="false">'
             '</item></conf> -->\n') if shadow else ''
    dup = (f'    <conf name="default" type="2">\n{default_items}\n    </conf>\n'
           if twice else '')
    return f'''<configurationDescriptor version="65">
  <confs>
{ghost}{dup}    <conf name="default" type="2">
{default_items}
    </conf>
    <conf name="Nq1" type="2">
{DECOY}
    </conf>
  </confs>
</configurationDescriptor>
'''


def main():
    failures = 0
    with tempfile.TemporaryDirectory() as td:
        for want, name, items, why in CASES:
            path = Path(td) / f'{name}.xml'
            path.write_text(project(items,
                                    shadow=(name == 'comment-shadow'),
                                    twice=(name == 'dup-conf')),
                            encoding='utf-8')
            run = subprocess.run([sys.executable, str(GUARD), str(path)],
                                 capture_output=True, text=True)
            got = run.returncode
            ok = got == want
            failures += 0 if ok else 1
            print(f'  [{"PASS" if ok else "FAIL"}] {name:<11} '
                  f'exit={got} want={want}  {why}')
            if not ok:
                # Echo what the guard actually said. The CI step runs under
                # `set -e` with the selftest BEFORE the standalone guard, so a
                # failure here aborts the step and the guard's own explanation
                # would otherwise never be printed.
                for stream, label in ((run.stdout, 'stdout'), (run.stderr, 'stderr')):
                    for line in (stream or '').splitlines():
                        print(f'        {label}| {line}')

    # The committed project file must be checkable and coherent, or CI is
    # guarding nothing.
    real = subprocess.run([sys.executable, str(GUARD)],
                          capture_output=True, text=True).returncode
    ok = real == 0
    failures += 0 if ok else 1
    print(f'  [{"PASS" if ok else "FAIL"}] real-project exit={real} want=0  '
          'committed configurations.xml is coherent')

    print(f'\n{"SELFTEST PASS" if not failures else f"SELFTEST FAIL ({failures})"}'
          f' — {len(CASES) + 1} cases')
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
