#!/usr/bin/env python3
"""Assert the MPLAB X project is in a committable build state (#764, #791).

Two independent checks, both on <conf name="default">:

  1. Exactly ONE linker script selected (#764 item 3) -- see below.
  2. No INERT per-file optimization override (#791). An <item overriding="true">
     whose <C32> optimization-level is EMPTY emits no -O flag at all, so that
     file silently compiles at -O0 while the rest of the project is at -O3.
     This is not hypothetical: #426 replaced the FreeRTOS_tasks.c -O1 clamp by
     blanking its value instead of deleting the <item>, and the production
     kernel shipped unoptimized for two years while CLAUDE.md stated twice
     that it built at -O3. Nothing in the IDE shows it -- the stale "-O1" that
     looks like the intent sits in the <C32CPP> block, which this C project
     never invokes. Deleting the <item> is the fix; the file then inherits the
     conf-level flag.

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
       1 = a config state that must not land: any linker selection at all
           (bootloader-linked, p32-only, or both -- each breaks the release
           cut), or an inert optimization override.
       2 = could not check (entry or default conf missing)
"""
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

DEFAULT = Path(__file__).resolve().parents[2] / \
    'firmware/daqifi.X/nbproject/configurations.xml'
SCRIPTS = ('p32MZ2048EFM144.ld', 'old_hv2_bootld.ld')
CONF = 'default'   # the configuration cut_release.sh builds
MAX_BYTES = 8 * 1024 * 1024   # ~20x the real file; a cap, not a tight bound


def conf_elements(root, conf):
    """Every <conf name="conf"> element. A LIST, for the same reason
    script_entries returns one: two blocks with the same name means the
    effective config depends on which the toolchain reads, and picking the
    first would hide that."""
    return [el for el in root.iter('conf') if el.get('name') == conf]


def script_entries(conf_el, name):
    """Every <item> under `conf_el` whose path basename is `name`.

    Returns a LIST of included-flags, so the caller can reject duplicates
    rather than silently taking whichever entry appears first.
    """
    out = []
    for item in conf_el.iter('item'):
        path, ex = item.get('path'), item.get('ex')
        if not path or path.rsplit('/', 1)[-1] != name:
            continue
        if ex not in ('true', 'false'):
            # Anything else (absent, empty, a typo) is NOT "excluded". A bare
            # `ex == 'false'` test would silently read it that way and report a
            # confident verdict on a value it did not understand.
            raise ValueError(f'{name}: ex={ex!r} is neither "true" nor "false"')
        out.append(ex == 'false')
    return out


def inert_optimization_overrides(conf_el):
    """Every <item> in `conf_el` that overrides optimization to NOTHING.

    Returns [(path, value)]. An override with an EMPTY optimization-level is
    always a defect: MPLAB emits no -O flag for it, so the file drops to the
    compiler default (-O0) rather than inheriting the conf-level setting. If
    the conf-level setting was what you wanted, the <item> should be deleted.

    Only <C32> is consulted. The <C32CPP> block carries its own
    optimization-level and this is a C project, so a value there compiles
    nothing -- reading it is how #426's leftover "-O1" was mistaken for the
    effective setting in the first place.

    A NON-empty override is left alone: tfm.c legitimately overrides (it needs
    -Wno-error=array-bounds) and states "-O3" explicitly.
    """
    out = []
    for item in conf_el.iter('item'):
        if item.get('overriding') != 'true':
            continue
        # An EXCLUDED file is not compiled, so a stale blank override on it
        # emits nothing and changes nothing. Flagging it would refuse a
        # perfectly releasable project over a dead entry.
        if item.get('ex') == 'true':
            continue
        # Only C sources reach the C compiler. An assembler item (.S/.s) is
        # assembled, not compiled, so its <C32> optimization-level -- blank or
        # otherwise -- selects nothing; the assembler settings live in
        # <C32-AS>. Flagging those would be a false positive on a file whose
        # code generation this property cannot affect.
        path = item.get('path') or ''
        if not path.lower().endswith('.c'):
            continue
        c32 = item.find('C32')
        if c32 is None:
            continue
        for prop in c32.iter('property'):
            if prop.get('key') != 'optimization-level':
                continue
            value = prop.get('value')
            # We are already inside the optimization-level property, so the
            # override IS expressed; the question is only whether it names a
            # flag. Empty ("") and a missing value= attribute (None) are the
            # same thing to the toolchain -- neither emits a -O option -- so
            # both count. (An absent PROPERTY is different: that inherits
            # normally and never reaches this loop.)
            if not (value or '').strip():
                out.append((item.get('path') or '<no path>', value))
    return out


def main():
    path = Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT
    # Parse as XML, because it IS XML. Every defect review found in this guard
    # was a regex artifact a real parser does not have: an unscoped search
    # reading the Nq1 copy of a pruned entry, a fixed path-then-ex attribute
    # order, duplicate entries resolved by document position, and a
    # commented-out block shadowing the live one. ElementTree scopes by tree
    # structure, ignores attribute order, and discards comments outright.
    try:
        # Cap the input first: CI runs this on PRs that can edit the file, and
        # an oversized one should produce a verdict rather than exhaust the
        # runner. The real file is ~400 KB.
        size = path.stat().st_size
        if size > MAX_BYTES:
            print(f'CANNOT CHECK: {path} is {size} bytes (cap {MAX_BYTES}).')
            return 2
        root = ET.parse(path).getroot()
    except OSError as exc:
        print(f'CANNOT CHECK: {exc}')
        return 2
    except ET.ParseError as exc:
        print(f'CANNOT CHECK: {path} is not well-formed XML: {exc}')
        return 2

    # Scope to the conf actually built. configurations.xml carries a full <item>
    # set PER CONFIGURATION — one copy of each linker script per conf (today:
    # default and Nq3; the stale Nq1 conf was deleted in #771/#775). An unscoped
    # whole-file search silently falls through to another conf's copy when the
    # default-conf entry is the one that got pruned, which is exactly the state
    # this guard exists to catch: it would report OK while
    # cut_release.sh (which scopes its own lookup with awk bounded by
    # <conf name="default">) dies at release time on its precondition.
    confs = conf_elements(root, CONF)
    if not confs:
        print(f'CANNOT CHECK: no <conf name="{CONF}"> block in {path}')
        return 2
    if len(confs) > 1:
        print(f'CANNOT CHECK: {len(confs)} <conf name="{CONF}"> blocks in '
              f'{path}.\nThe effective configuration then depends on which one '
              'the toolchain reads.')
        return 2
    block = confs[0]

    state = {}
    dupes = []
    try:
        for name in SCRIPTS:
            found = script_entries(block, name)
            if len(found) > 1:
                dupes.append((name, len(found)))
            state[name] = found[0] if len(found) == 1 else None
    except ValueError as exc:
        print(f'CANNOT CHECK: {exc}')
        return 2

    print(f'checking {path}')
    for name, included in state.items():
        if included is None:
            print(f'  ??   {name}: not listed exactly once in the project')
        else:
            print(f'  {"IN " if included else "OUT"}  {name}')

    if dupes:
        # Two entries for one script (a merge artifact, or a hand-edit) means
        # the effective flag depends on which one the toolchain reads. Refuse
        # rather than report whichever appears first.
        detail = ', '.join(f'{n} x{c}' for n, c in dupes)
        print(f'\nCANNOT CHECK: duplicate linker-script entries in the '
              f'<conf name="{CONF}"> block ({detail}).\n'
              'The effective selection then depends on which entry the '
              'toolchain reads.\nRestore the project file:\n'
              '  git checkout -- firmware/daqifi.X/nbproject/configurations.xml')
        return 2

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

    # Checked AFTER the linker entries, not before: those can return 2
    # ("cannot check" -- pruned, duplicated or malformed project), and an
    # unusable input is the more fundamental problem. Reporting 1 first
    # would hide it and break the exit-code contract, which a caller uses
    # to tell "fix your config" from "restore the project file".
    inert = inert_optimization_overrides(block)
    if inert:
        print(f'checking {path}')
        for item_path, value in inert:
            print(f'  DEAD  {item_path.rsplit("/", 1)[-1]}: '
                  f'optimization-level={value!r} (emits no -O flag)')
        print(f'\nINERT OPTIMIZATION OVERRIDE in <conf name="{CONF}">.\n'
              'An active override with an empty value emits NO -O flag, so '
              'these files\ncompile at -O0 while the rest of the project is '
              'at -O3 — silently, and\nwithout anything in the IDE showing '
              'it (#791; #426 shipped the kernel this\nway).\n'
              'Fix: delete the <item> so the file inherits the conf-level '
              'optimization,\nor give the override an explicit value if it '
              'genuinely needs a different one.')
        return 1

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
