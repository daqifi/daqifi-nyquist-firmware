#!/usr/bin/env python3
"""Reproduce SCPI_FirmwareImageCrc32() offline from a built Intel HEX file.

## What this computes, and where each constant comes from

`SCPI_FirmwareImageCrc32()` (`firmware/src/services/SCPI/SCPIInterface.c`)
computes:

    CRC32_Compute(__KSEG0_PROGRAM_MEM_BASE,
                   __KSEG0_PROGRAM_MEM_LENGTH - RESERVED_SETTINGS_SPACE)

`__KSEG0_PROGRAM_MEM_BASE` / `__KSEG0_PROGRAM_MEM_LENGTH` are XC32's
auto-generated symbols for the `kseg0_program_mem` MEMORY region. For the
**standalone (bench) build** -- `firmware/src/config/default/p32MZ2048EFM144.ld`,
the layout every lane's PICkit flash uses --
`kseg0_program_mem (rx) : ORIGIN = 0x9D000000, LENGTH = 0x200000` (line 117).
KSEG0 virtual addresses map to physical by subtracting 0x80000000, and PIC32
`.hex` output is already physical, so the audited region starts at
`0x9D000000 - 0x80000000 = 0x1D000000`.

`RESERVED_SETTINGS_SPACE` = `128*1024` = `0x20000`
(`firmware/src/services/daqifi_settings.h:55`), so the audited length is
`0x200000 - 0x20000 = 0x1E0000`.

(The bootloader-linked release build, `old_hv2_bootld.ld`, offsets
`kseg0_program_mem` by `0x480` for the reset vector -- `ORIGIN = 0x9D000000 +
0x480, LENGTH = 0x200000 - 0x480` -- so its own firmware_crc32 covers a
different, 0x480-byte-shorter region starting at physical 0x1D000480. That
layout is out of scope here: every current use of this tool (bench image
identification, pre-flash/post-flash provenance, toolchain-determinism
A/B) is against standalone builds. `--base`/`--length` are exposed below if
a bootloader-linked comparison is ever needed.)

Unprogrammed flash reads `0xFF`, not `0x00` -- the destination buffer is
0xFF-filled before any HEX record is applied, exactly like the physical part
would read if left unprogrammed.

`firmware/src/Util/CRC32.c` / `CRC32.h`: nibble-table CRC-32, reflected
polynomial `0xEDB88320`, init `0xFFFFFFFF`, final XOR `0xFFFFFFFF` -- by the
header's own docstring, "Matches zlib crc32() / Python binascii.crc32 /
`crc32` coreutils output." So this tool uses the stdlib `zlib.crc32()`
directly rather than re-implementing the table.

## The two Intel-HEX address-extension records

- **Type 04** (extended linear address): the record's 2 data bytes are the
  UPPER 16 bits of a 32-bit base; `base = upper16 << 16`.
- **Type 02** (extended segment address): the record's 2 data bytes are a
  16-bit segment; `base = segment << 4`.

Conflating the two produces a silently wrong (not obviously-broken) address
for every subsequent data record -- this is called out explicitly because an
earlier hand-written version of this tool got exactly this wrong. Real PIC32
`.hex` output only ever emits type 04; type 02 is exercised by `--self-test`
alone, precisely because nothing in this codebase's own fixtures would ever
catch a swap.

A data record's bytes are applied individually: only the bytes whose
resolved address falls inside `[base, base+length)` are written into the
image buffer. Bytes -- and whole records -- outside that window are ignored,
never misapplied via truncation or wraparound.

## Usage

    python3 tools/hexcrc.py path/to/image.hex [more.hex ...]
    python3 tools/hexcrc.py --self-test

## Reference pairs (values only -- the .hex files that produced them are
## multi-megabyte build artifacts and are deliberately not committed here;
## see daqifi-nyquist-firmware#1127 for how they were produced)

    hex built at daqifi-nyquist-firmware#1092 (e66725c9) -> A7823538
    hex built at daqifi-nyquist-firmware#965  (ee43eb0c8) -> A849A017
"""
import argparse
import sys
import zlib

# --- Standalone (bench) build layout -- see module docstring for citations.
STANDALONE_KSEG0_BASE = 0x9D000000
STANDALONE_KSEG0_LENGTH = 0x200000
RESERVED_SETTINGS_SPACE = 128 * 1024  # daqifi_settings.h:55

KSEG0_TO_PHYSICAL = 0x80000000  # MIPS convention: KSEG0 virtual - physical

STANDALONE_PHYS_BASE = STANDALONE_KSEG0_BASE - KSEG0_TO_PHYSICAL   # 0x1D000000
STANDALONE_AUDIT_LENGTH = STANDALONE_KSEG0_LENGTH - RESERVED_SETTINGS_SPACE  # 0x1E0000

UNPROGRAMMED_FILL = 0xFF

# Intel HEX record types this tool understands.
REC_DATA = 0x00
REC_EOF = 0x01
REC_EXT_SEGMENT_ADDR = 0x02
REC_START_SEGMENT_ADDR = 0x03
REC_EXT_LINEAR_ADDR = 0x04
REC_START_LINEAR_ADDR = 0x05


class HexFormatError(ValueError):
    """Raised on a malformed or unsupported Intel HEX record.

    Deliberately loud: a malformed record must abort the run, not fall
    through to a CRC computed over a partially-applied (and therefore
    plausible-looking) image.
    """


def parse_records(lines):
    """Yield (lineno, addr16, rec_type, data) for every non-blank line.

    Validates the byte-count field, the declared-vs-actual data length, and
    the checksum byte on every record -- any of the three failing raises
    HexFormatError rather than silently accepting a truncated/corrupt line.
    """
    for lineno, raw in enumerate(lines, 1):
        line = raw.strip()
        if not line:
            continue
        if not line.startswith(":"):
            raise HexFormatError(
                "line %d: record does not start with ':'" % lineno)
        body = line[1:]
        # 10 hex chars is the minimum: byte-count(2) + address(4) + type(2)
        # + checksum(2), for a zero-length data record.
        if len(body) < 10 or len(body) % 2 != 0:
            raise HexFormatError(
                "line %d: record too short or odd-length hex" % lineno)
        try:
            raw_bytes = bytes.fromhex(body)
        except ValueError as exc:
            raise HexFormatError(
                "line %d: not valid hex: %s" % (lineno, exc)) from exc

        byte_count = raw_bytes[0]
        addr16 = (raw_bytes[1] << 8) | raw_bytes[2]
        rec_type = raw_bytes[3]
        expected_len = 4 + byte_count + 1  # header(4) + data + checksum(1)
        if len(raw_bytes) != expected_len:
            raise HexFormatError(
                "line %d: declares %d data byte(s) but record has %d "
                "byte(s) after the byte-count field (expected %d total)"
                % (lineno, byte_count, len(raw_bytes) - 5, expected_len))

        data = raw_bytes[4:4 + byte_count]
        checksum = raw_bytes[4 + byte_count]
        computed = (-sum(raw_bytes[:4 + byte_count])) & 0xFF
        if computed != checksum:
            raise HexFormatError(
                "line %d: checksum mismatch (record says 0x%02X, "
                "computed 0x%02X)" % (lineno, checksum, computed))

        yield lineno, addr16, rec_type, data


def compute_image_crc32(lines, region_base, region_length,
                         fill=UNPROGRAMMED_FILL):
    """CRC-32 (zlib-compatible) of [region_base, region_base+region_length)
    as assembled from the given Intel HEX lines, 0xFF-filled where no
    record ever wrote.
    """
    # bytearray multiplication (not [fill] * region_length then bytearray())
    # so a real ~2 MB region never builds an intermediate Python list.
    image = bytearray([fill]) * region_length
    ext_base = 0  # accumulated from the most recent type-02/04 record

    for lineno, addr16, rec_type, data in parse_records(lines):
        if rec_type == REC_DATA:
            full_addr = ext_base + addr16
            for i, byte in enumerate(data):
                a = full_addr + i
                if region_base <= a < region_base + region_length:
                    image[a - region_base] = byte
        elif rec_type == REC_EOF:
            break
        elif rec_type == REC_EXT_LINEAR_ADDR:
            if len(data) != 2:
                raise HexFormatError(
                    "line %d: type 04 record must carry exactly 2 data "
                    "bytes, got %d" % (lineno, len(data)))
            upper16 = (data[0] << 8) | data[1]
            ext_base = upper16 << 16
        elif rec_type == REC_EXT_SEGMENT_ADDR:
            if len(data) != 2:
                raise HexFormatError(
                    "line %d: type 02 record must carry exactly 2 data "
                    "bytes, got %d" % (lineno, len(data)))
            segment = (data[0] << 8) | data[1]
            ext_base = segment << 4
        elif rec_type in (REC_START_SEGMENT_ADDR, REC_START_LINEAR_ADDR):
            pass  # entry point only -- irrelevant to a data CRC
        else:
            raise HexFormatError(
                "line %d: unsupported Intel HEX record type 0x%02X"
                % (lineno, rec_type))

    return zlib.crc32(bytes(image)) & 0xFFFFFFFF


def compute_file_crc32(path, region_base=STANDALONE_PHYS_BASE,
                        region_length=STANDALONE_AUDIT_LENGTH):
    with open(path, "r", encoding="ascii", errors="replace") as fh:
        return compute_image_crc32(fh, region_base, region_length)


# ---------------------------------------------------------------------------
# self-test: pure, synthetic fixtures built in-process -- no hardware, no
# build, no multi-megabyte committed .hex. Shape follows
# tools/lint/scpi_claim_path.py / tools/lint/scpi_wiki_sync.py.

_CHECKS = []


def _ck(name, got, want):
    ok = got == want
    _CHECKS.append(ok)
    if not ok:
        print("  self-test FAIL: %s: got %r want %r" % (name, got, want))


def _checksum_byte(rec_bytes):
    return (-sum(rec_bytes)) & 0xFF


def _data_record(addr16, data):
    body = bytes([len(data), (addr16 >> 8) & 0xFF, addr16 & 0xFF, REC_DATA]) \
        + bytes(data)
    return ":" + (body + bytes([_checksum_byte(body)])).hex().upper()


def _ext_linear_record(upper16):
    body = bytes([2, 0, 0, REC_EXT_LINEAR_ADDR,
                  (upper16 >> 8) & 0xFF, upper16 & 0xFF])
    return ":" + (body + bytes([_checksum_byte(body)])).hex().upper()


def _ext_segment_record(segment):
    body = bytes([2, 0, 0, REC_EXT_SEGMENT_ADDR,
                  (segment >> 8) & 0xFF, segment & 0xFF])
    return ":" + (body + bytes([_checksum_byte(body)])).hex().upper()


_EOF_RECORD = ":00000001FF"


def self_test():
    # A small synthetic region -- large enough to hold a few distinct
    # records, tiny compared to the real 0x1E0000-byte audited image, so the
    # self-test runs in milliseconds with no external fixture.
    base = 0x1D000000
    length = 0x40  # 64 bytes

    # --- 1: a type-04 (extended linear) record landing in-region.
    # upper16 = 0x1D00 -> base 0x1D000000; offset 0x0010 -> 0x1D000010.
    lines_04 = [
        _ext_linear_record(0x1D00),
        _data_record(0x0010, [0xAA, 0xBB, 0xCC, 0xDD]),
        _EOF_RECORD,
    ]
    img_04 = bytearray([0xFF] * length)
    img_04[0x10:0x14] = bytes([0xAA, 0xBB, 0xCC, 0xDD])
    want_04 = zlib.crc32(bytes(img_04)) & 0xFFFFFFFF
    _ck("type-04 record resolves upper16<<16 and lands in-region",
        compute_image_crc32(lines_04, base, length), want_04)

    # --- 2: a type-02 (extended segment) record, resolved with segment=0xFFFF
    # (the max 16-bit value) so segment<<4 = 0xFFFF0 -- a small, easily
    # hand-computed address. Compare against a Python-computed expected image
    # that uses the CORRECT (segment<<4) formula.
    #
    # This discriminates a shift-confused implementation (e.g. one that
    # reuses type-04's segment<<16 formula for type-02 records too):
    # segment<<16 = 0xFFFF0000, nowhere near seg_base below, so a
    # shift-confused implementation drops the byte out of region and returns
    # the all-0xFF fill CRC instead of want_02 -- and the very next check
    # proves that fill CRC is a DIFFERENT value, so this comparison cannot
    # pass vacuously regardless of which formula is used.
    seg_base = 0x000FFFF0
    seg_length = 0x20
    segment = 0xFFFF  # segment<<4 = 0xFFFF0 == seg_base
    lines_02 = [
        _ext_segment_record(segment),
        _data_record(0x0000, [0x11, 0x22]),  # -> seg_base + 0 = 0xFFFF0
        _EOF_RECORD,
    ]
    img_02 = bytearray([0xFF] * seg_length)
    img_02[0:2] = bytes([0x11, 0x22])
    want_02 = zlib.crc32(bytes(img_02)) & 0xFFFFFFFF
    _ck("type-02 record resolves segment<<4, not upper16<<16",
        compute_image_crc32(lines_02, seg_base, seg_length), want_02)
    all_ff_crc = zlib.crc32(bytes([0xFF] * seg_length)) & 0xFFFFFFFF
    _ck("...and that value is not just the fill CRC (the check discriminates)",
        want_02 != all_ff_crc, True)

    # --- 2b: the sharper form of the same trap -- encode the SAME physical
    # address via type-04 (upper16<<16 + addr16) and via type-02
    # (segment<<4 + addr16), with the SAME payload, and require the two
    # records to resolve to one identical CRC. 0x000FFFF0 is reachable by
    # both: type-04 as upper16=0x000F, addr16=0xFFF0; type-02 as
    # segment=0xFFFF, addr16=0x0000.
    conv_base = 0x000FFFF0
    conv_length = 0x10
    lines_04_conv = [
        _ext_linear_record(0x000F),
        _data_record(0xFFF0, [0x55, 0x66]),
        _EOF_RECORD,
    ]
    lines_02_conv = [
        _ext_segment_record(0xFFFF),
        _data_record(0x0000, [0x55, 0x66]),
        _EOF_RECORD,
    ]
    crc_04_conv = compute_image_crc32(lines_04_conv, conv_base, conv_length)
    crc_02_conv = compute_image_crc32(lines_02_conv, conv_base, conv_length)
    _ck("type-04 (0x000F<<16 + 0xFFF0) and type-02 (0xFFFF<<4 + 0) "
        "resolve to the same physical address",
        crc_04_conv, crc_02_conv)

    # --- 3: 0xFF fill -- a region with NO records must produce the
    # all-0xFF CRC, asserted against the explicit computed value, not just
    # "nonzero".
    empty_length = 0x10
    expected_ff_crc = zlib.crc32(bytes([0xFF] * empty_length)) & 0xFFFFFFFF
    # zlib.crc32(b'\xff' * 16) is a fixed, well-known value; pin it literally
    # so a change to the fill byte or the region size is caught even if
    # zlib's behaviour were ever wrong in some exotic environment.
    _ck("all-0xFF, 16-byte fill CRC is the literal expected constant",
        expected_ff_crc, 0x3FB3C61A)
    _ck("a region with no records at all produces the all-0xFF CRC",
        compute_image_crc32([_EOF_RECORD], base, empty_length),
        expected_ff_crc)
    _ck("...and that is not the degenerate zero value",
        expected_ff_crc != 0, True)

    # --- 4: a record entirely outside the audited region is ignored, not
    # misapplied (no wraparound, no crash, no effect on the CRC).
    lines_outside = [
        _ext_linear_record(0x1D00),
        _data_record(0x0010, [0xAA, 0xBB, 0xCC, 0xDD]),  # in-region, as (1)
        _ext_linear_record(0x0000),
        _data_record(0x0000, [0x99, 0x99, 0x99, 0x99]),  # far below base
        _EOF_RECORD,
    ]
    _ck("a record outside the region is ignored -- same CRC as region alone",
        compute_image_crc32(lines_outside, base, length), want_04)

    # --- 4b: a record straddling the region boundary only has its
    # in-region bytes applied -- the out-of-region half must not corrupt
    # adjacent bytes or wrap into the buffer.
    straddle_length = 0x8
    lines_straddle = [
        _ext_linear_record(0x1D00),
        # region is [0x1D000000, 0x1D000008); this record covers
        # 0x1D000006..0x1D00000B -- 2 bytes in-region, 3 out.
        _data_record(0x0006, [0x01, 0x02, 0x03, 0x04, 0x05]),
        _EOF_RECORD,
    ]
    img_straddle = bytearray([0xFF] * straddle_length)
    img_straddle[6:8] = bytes([0x01, 0x02])
    want_straddle = zlib.crc32(bytes(img_straddle)) & 0xFFFFFFFF
    _ck("a record straddling the boundary applies only its in-region bytes",
        compute_image_crc32(lines_straddle, base, straddle_length),
        want_straddle)

    # --- 5: malformed / absent-data cases refuse loudly instead of
    # returning a plausible-looking number.
    def _raises(lines):
        try:
            compute_image_crc32(lines, base, length)
            return False
        except HexFormatError:
            return True

    _ck("a line not starting with ':' is refused",
        _raises(["not a hex record"]), True)
    _ck("a truncated record (missing checksum byte) is refused",
        _raises([":04001000AABBCCDD"]), True)  # no checksum byte appended
    _ck("a byte-count that overstates the actual data is refused",
        _raises([":10001000AABB" + "00" * 2]), True)  # says 16, has 4
    _ck("a bad checksum byte is refused",
        _raises([_data_record(0x0000, [0x00])[:-2] + "00"]), True)
    _ck("a type-04 record with the wrong data length is refused",
        _raises([":0100000400FB"]), True)  # byte_count=1 (not 2), checksum valid
    _ck("an unsupported record type is refused",
        _raises([":01000006" + "00" + "F9"]), True)
    # And the positive control: a well-formed minimal file must NOT raise,
    # so the above are really exercising validation and not just "any input
    # throws".
    _ck("a well-formed minimal file does not raise",
        _raises([_EOF_RECORD]), False)

    bad = _CHECKS.count(False)
    print("self-test: %d/%d checks passed" % (_CHECKS.count(True), len(_CHECKS)))
    return 1 if bad else 0


def main():
    ap = argparse.ArgumentParser(
        description=__doc__.split("\n")[0],
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("hexfiles", nargs="*",
                     help="Intel HEX file(s) to compute firmware_crc32 for")
    ap.add_argument("--base", type=lambda s: int(s, 0),
                     default=STANDALONE_PHYS_BASE,
                     help="audited region physical base address "
                          "(default: 0x%X, standalone build)"
                          % STANDALONE_PHYS_BASE)
    ap.add_argument("--length", type=lambda s: int(s, 0),
                     default=STANDALONE_AUDIT_LENGTH,
                     help="audited region length in bytes "
                          "(default: 0x%X, standalone build)"
                          % STANDALONE_AUDIT_LENGTH)
    ap.add_argument("--self-test", action="store_true",
                     help="run the built-in checks and exit "
                          "(no hardware, no build, no fixture files needed)")
    args = ap.parse_args()

    if args.self_test:
        return self_test()

    if not args.hexfiles:
        ap.error("at least one HEX file is required (or pass --self-test)")

    rc = 0
    for path in args.hexfiles:
        try:
            crc = compute_file_crc32(path, args.base, args.length)
        except FileNotFoundError:
            print("error: %r not found" % path, file=sys.stderr)
            rc = 1
            continue
        except HexFormatError as exc:
            print("error: %s: %s" % (path, exc), file=sys.stderr)
            rc = 1
            continue
        print("%08X  %s" % (crc, path))
    return rc


if __name__ == "__main__":
    sys.exit(main())
