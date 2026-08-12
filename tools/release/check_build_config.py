#!/usr/bin/env python3
"""Assert the MPLAB X project selects exactly ONE linker script (#764 item 3).

configurations.xml lists both scripts and marks one excluded. If BOTH are
included (or both excluded) the produced layout depends on regeneration order,
and this session saw it silently flip between standalone and bootloader-linked
between builds — which is precisely how a release can be cut in the wrong shape.

MPLAB X also prunes this file when the build config is switched: on 2026-08-03
it stripped eight recently-added source files, and separately dropped the
UserUart entries, producing 'undefined reference' errors for code that plainly
exists.

Usage: check_build_config.py [configurations.xml]
Exit:  0 = release-compatible: NO custom linker script selected. This is the
           only committable state — cut_release.sh selects old_hv2_bootld.ld
           itself and assumes (without checking) p32 stays excluded.
       1 = any selection at all: bootloader-linked, p32-only, or both. Each is
           fine locally; none may land, because each breaks the release cut.
       2 = could not check (entry or default conf missing)
"""
import re
import sys
from pathlib import Path

DEFAULT = Path(__file__).resolve().parents[2] / \
    'firmware/daqifi.X/nbproject/configurations.xml'
SCRIPTS = ('p32MZ2048EFM144.ld', 'old_hv2_bootld.ld')
CONF = 'default'   # the configuration cut_release.sh builds


def conf_block(text, conf):
    """Return just the <conf name="conf"> ... section, or None if absent.

    Bounded by the NEXT <conf name=...> (or end of file) rather than by a
    </conf> tag, so a nested close tag cannot end the block early.
    """
    m = re.search(r'<conf\s+name="' + re.escape(conf) + r'"', text)
    if not m:
        return None
    rest = text[m.end():]
    nxt = re.search(r'<conf\s+name="', rest)
    return rest[:nxt.start()] if nxt else rest


def main():
    path = Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT
    try:
        text = path.read_text(encoding='utf-8', errors='ignore')
    except OSError as exc:
        print(f'CANNOT CHECK: {exc}')
        return 2

    # Scope to the conf actually built. configurations.xml carries a full <item>
    # set PER CONFIGURATION — three copies of each linker script (default, Nq1,
    # Nq3). An unscoped whole-file search silently falls through to the Nq1 copy
    # when the default-conf entry is the one that got pruned, which is exactly
    # the state this guard exists to catch: it would report OK while
    # cut_release.sh (which scopes its own lookup with awk bounded by
    # <conf name="default">) dies at release time on its precondition.
    block = conf_block(text, CONF)
    if block is None:
        print(f'CANNOT CHECK: no <conf name="{CONF}"> block in {path}')
        return 2

    state = {}
    for name in SCRIPTS:
        # attributes span multiple lines in MPLAB X output, so DOTALL and a
        # bounded gap rather than a single-line match
        m = re.search(r'<item\s+path="[^"]*' + re.escape(name) +
                      r'"\s+ex="(true|false)"', block, re.S)
        state[name] = (m.group(1) == 'false') if m else None

    print(f'checking {path}')
    for name, included in state.items():
        if included is None:
            print(f'  ??   {name}: not listed in the project')
        else:
            print(f'  {"IN " if included else "OUT"}  {name}')

    missing = [n for n, v in state.items() if v is None]
    if missing:
        # An unlisted script is not "excluded" — the project has lost the entry
        # (MPLAB X prunes this file on config switches; it stripped eight source
        # files earlier in this work). Treat it as a failure rather than
        # inferring intent from an absence.
        print(f'\nCANNOT CHECK: {missing} not listed in the project at all.\n'
              'MPLAB X prunes configurations.xml on config switches — restore it\n'
              '(git checkout -- firmware/daqifi.X/nbproject/configurations.xml)\n'
              'and regenerate the makefiles before trusting a build.')
        # 2, not 1: the input is unusable, which is a different problem from a
        # readable project that names two scripts. 1 must stay specific to
        # "ambiguous selection" so a caller can tell "fix your config" from
        # "restore the project file".
        return 2

    included = [n for n, v in state.items() if v]

    # BOTH excluded is the STANDALONE BENCH DEFAULT, not an error: with no
    # custom script selected XC32 falls back to the device linker script, which
    # links the app at 0x9D000000. tools/release/cut_release.sh depends on this
    # exact state — it asserts old_hv2_bootld.ld is ex="true" and flips it for
    # the release build, then restores it. Reporting this as a failure would
    # have been wrong, and permanently flipping the file (my first attempt)
    # would have made cut_release.sh die on its own precondition.
    if not included:
        print('\nOK: no custom linker script selected -> standalone (bench '
              'default).\n'
              'Release builds flip old_hv2_bootld.ld on via '
              'tools/release/cut_release.sh, which\nalso verifies the '
              'resulting hex layout and restores this state afterwards.')
        return 0

    if len(included) == 1:
        if included[0] == SCRIPTS[1]:
            # cut_release.sh asserts old_hv2_bootld.ld is ex="true" (line ~120),
            # flips it for the release build, then restores it. A committed
            # bootloader-linked project therefore breaks the release path: the
            # script dies on its own precondition.
            print('\nRELEASE PRECONDITION BROKEN: old_hv2_bootld.ld is selected '
                  '(ex="false") in the\ncommitted project. '
                  'tools/release/cut_release.sh requires ex="true" as its\n'
                  'starting state — it flips the script on itself and restores '
                  'it afterwards —\nso a release cut from this state aborts '
                  'immediately.')
        else:
            # p32 selected on its own is a legitimate way to express "standalone"
            # in the IDE, and it looks harmless — but cut_release.sh only ever
            # inspects old_hv2_bootld.ld (cut_release.sh:113-121). Its "keep
            # p32MZ excluded" note at line 77 is an ASSUMPTION, not a check. So
            # from this state the release flip selects old_hv2 while p32 is
            # still selected, leaving BOTH custom scripts active — exactly the
            # ambiguous layout this guard exists to prevent. Tracked separately
            # for cut_release.sh itself; here we simply refuse to let the state
            # land.
            print('\nRELEASE PRECONDITION BROKEN: p32MZ2048EFM144.ld is selected '
                  '(ex="false") in the\ncommitted project. '
                  'tools/release/cut_release.sh does not inspect this script — '
                  'it\nonly flips old_hv2_bootld.ld — so a release cut from '
                  'here ends up with BOTH\ncustom linker scripts selected, and '
                  'the layout depends on makefile\nregeneration order.')
        print('Restore the bench default before merging:\n'
              '  git checkout -- firmware/daqifi.X/nbproject/configurations.xml')
        return 1

    print(f'\nAMBIGUOUS: {len(included)} linker scripts selected {included}.\n'
          'The produced layout then depends on makefile regeneration order.\n'
          'Select exactly one: old_hv2_bootld.ld for a release,\n'
          'p32MZ2048EFM144.ld for a standalone bench build.')
    return 1


if __name__ == '__main__':
    sys.exit(main())
