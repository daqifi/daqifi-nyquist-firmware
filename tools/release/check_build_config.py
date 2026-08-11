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
Exit:  0 = exactly one selected, 1 = ambiguous, 2 = could not check.
"""
import re
import sys
from pathlib import Path

DEFAULT = Path(__file__).resolve().parents[2] / \
    'firmware/daqifi.X/nbproject/configurations.xml'
SCRIPTS = ('p32MZ2048EFM144.ld', 'old_hv2_bootld.ld')


def main():
    path = Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT
    try:
        text = path.read_text(encoding='utf-8', errors='ignore')
    except OSError as exc:
        print(f'CANNOT CHECK: {exc}')
        return 2

    state = {}
    for name in SCRIPTS:
        # attributes span multiple lines in MPLAB X output, so DOTALL and a
        # bounded gap rather than a single-line match
        m = re.search(r'<item\s+path="[^"]*' + re.escape(name) +
                      r'"\s+ex="(true|false)"', text, re.S)
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
        print(f'\nNOT DETERMINABLE: {missing} not listed in the project at all.\n'
              'MPLAB X prunes configurations.xml on config switches — restore it\n'
              '(git checkout -- firmware/daqifi.X/nbproject/configurations.xml)\n'
              'and regenerate the makefiles before trusting a build.')
        return 1

    included = [n for n, v in state.items() if v]
    if len(included) == 1:
        layout = ('bootloader-linked (release)' if included[0] == SCRIPTS[1]
                  else 'standalone (bench only — NEVER ship)')
        print(f'\nOK: exactly one linker script selected -> {layout}')
        return 0

    print(f'\nAMBIGUOUS: {len(included)} linker scripts selected {included}.\n'
          'The produced layout then depends on makefile regeneration order.\n'
          'Select exactly one: old_hv2_bootld.ld for a release,\n'
          'p32MZ2048EFM144.ld for a standalone bench build.')
    return 1


if __name__ == '__main__':
    sys.exit(main())
