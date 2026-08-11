#!/usr/bin/env python3
"""Verify a release HEX is bootloader-linked and updater-shippable (#764).

The in-app updater programs PROGRAM FLASH ONLY (0x9D000000-0x9D1FFFFF, per the
bootloader's APP_FLASH_BASE/END_ADDRESS). Anything a hex places outside that
range is silently dropped on the way to a customer device. That is how a
standalone-linked image can pass every bench test and still ship broken:

  * its boot-flash crt0 at 0x1FC00000 is discarded (harmless - the
    bootloader's own stays), but
  * its CONFIG WORDS are discarded too, so the device keeps whatever the
    bootloader burned. Firmware #761 was exactly this: the bootloader pins
    IOL1WAY/PMDL1WAY = ON, which disabled the whole DIO-terminal family on
    fielded units while bench units (PICkit-flashed standalone, carrying the
    app's own L1WAY=OFF words) worked perfectly.

CLAUDE.md documents the required shape by hand. This checks it.

Usage:  check_release_hex.py <firmware.hex> [--map <firmware.map>]
Exit:   0 = shippable, 1 = NOT shippable, 2 = could not check.
"""
import argparse
import sys

RESET_BASE = 0x1D000000          # virtual 0x9D000000
APP_BASE = 0x1D000480            # virtual 0x9D000480
RESET_BYTES = 408                # the .reset vector, every shipped release
BOOT_FLASH = 0x1FC00000          # config words + crt0 live here


def parse_hex(path):
    """Return {addr: byte}. Raises on a malformed record."""
    mem, hi = {}, 0
    with open(path) as fh:
        for lineno, raw in enumerate(fh, 1):
            ln = raw.strip()
            if not ln or ln[0] != ':':
                continue
            try:
                n = int(ln[1:3], 16)
                off = int(ln[3:7], 16)
                rtype = int(ln[7:9], 16)
                data = ln[9:9 + 2 * n]
            except ValueError as exc:
                raise SystemExit(f'{path}:{lineno}: malformed record: {exc}')
            if rtype == 0x04:            # extended linear address
                hi = int(data, 16)
            elif rtype == 0x02:          # extended segment address
                hi = (int(data, 16) << 4) >> 16
            elif rtype == 0x00:
                base = (hi << 16) | off
                for i in range(n):
                    mem[base + i] = int(data[2 * i:2 * i + 2], 16)
    return mem


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('hex')
    ap.add_argument('--map', help='optional .map to check kseg0_program_mem origin')
    a = ap.parse_args()

    try:
        mem = parse_hex(a.hex)
    except OSError as exc:
        print(f'CANNOT CHECK: {exc}')
        return 2
    if not mem:
        print(f'CANNOT CHECK: {a.hex} contains no data records')
        return 2

    problems, notes = [], []
    lo = min(mem)

    # 1. lowest address must be the reset vector
    if lo != RESET_BASE:
        problems.append(
            f'lowest address is 0x{lo:08X}, expected 0x{RESET_BASE:08X} '
            f'(hex is not linked for the bootloader)')
    else:
        notes.append(f'lowest address 0x{lo:08X}')

    # 2. the .reset vector must be exactly RESET_BYTES
    reset = sum(1 for x in mem if RESET_BASE <= x < APP_BASE)
    if reset != RESET_BYTES:
        problems.append(
            f'{reset} bytes in the reset region [0x{RESET_BASE:08X},'
            f'0x{APP_BASE:08X}), expected exactly {RESET_BYTES}')
    else:
        notes.append(f'reset vector {reset} bytes')

    # 3. NOTHING may live in boot flash - the updater drops it silently
    boot = sorted(x for x in mem if x >= BOOT_FLASH)
    if boot:
        problems.append(
            f'{len(boot)} bytes at/above 0x{BOOT_FLASH:08X} (first 0x{boot[0]:08X}). '
            f'The updater programs program flash only, so these - INCLUDING THE '
            f'CONFIG WORDS - are silently discarded on a customer device. This is '
            f'a standalone-linked image; it must be built with old_hv2_bootld.ld.')
    else:
        notes.append('no boot-flash records (config words correctly absent)')

    # 4. bulk of code above the app base
    if not any(x >= APP_BASE for x in mem):
        problems.append(f'no data at or above 0x{APP_BASE:08X} - hex has no application')

    # 5. optional: the map's origin
    if a.map:
        try:
            with open(a.map, errors='ignore') as fh:
                text = fh.read()
            if '0x9d000480' in text.lower():
                notes.append('map: kseg0_program_mem origin 0x9d000480')
            elif '0x9d000000' in text.lower():
                problems.append(
                    'map shows kseg0_program_mem origin 0x9d000000 (standalone); '
                    'a release must be 0x9d000480')
        except OSError as exc:
            print(f'note: could not read map ({exc})')

    print(f'checking {a.hex}')
    for n in notes:
        print(f'  OK   {n}')
    for p in problems:
        print(f'  FAIL {p}')
    if problems:
        print('\nNOT SHIPPABLE - see CLAUDE.md "Packaging Release Artifacts".')
        return 1
    print('\nSHIPPABLE: bootloader-linked, updater-safe.')
    return 0


if __name__ == '__main__':
    sys.exit(main())
