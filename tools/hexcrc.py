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

**The two record types also do NOT wrap a data record's offset the same
way**, per the Intel Hexadecimal Object File Format Specification (as
reproduced verbatim in `srec_intel(5)`, the SRecord project's Intel HEX man
page -- https://srecord.sourceforge.net/man/man5/srec_intel.5.html):

- **Type 02**: `address = SBA + ((DRLO + DRI) MOD 64K)` -- the data record's
  load offset (DRLO = `addr16`) plus the in-record byte index (DRI = `i`) is
  wrapped modulo 0x10000 **before** it is added to the segment base (SBA).
  A record whose offset+index crosses a 64K boundary wraps back into the
  START of the SAME segment; the segment base itself never changes as a
  result. This mirrors 8086 real-mode segment:offset addressing, which is
  what "extended segment address" originally modeled.
- **Type 04**: `address = (LBA + DRLO + DRI) MOD 4G` -- offset and index are
  added to the linear base (LBA) FIRST, and only the total 32-bit sum wraps,
  at the 4 GiB boundary. A record crossing a 64K boundary therefore CARRIES
  into the next 64K block instead of wrapping back into the current one --
  the opposite of type 02's behavior. For every address this tool's own
  audited regions can reach (`<<` 4 GiB) the modulo-4G wrap is a no-op; it
  is applied for spec-fidelity, not because real firmware images exercise
  it.

A data record's bytes are applied individually, using whichever wrap rule
above matches the addressing mode most recently set by a type 02/04 record
(type 04's rule is also the default before any such record is seen -- the
spec's own default is upper bits zero, i.e. LBA=0, linear mode). Only the
bytes whose wrapped, resolved address falls inside `[base, base+length)` are
written into the image buffer; bytes -- and whole records -- outside that
window are ignored, never misapplied via truncation.

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

# Which wrap formula applies to the currently-active extension base -- see
# the module docstring's "do NOT wrap a data record's offset the same way"
# section. LINEAR is also the correct default before any type 02/04 record
# is seen (spec default: upper bits zero, i.e. an LBA of 0).
ADDR_MODE_LINEAR = "linear"    # type 04: address = (LBA + DRLO + DRI) MOD 4G
ADDR_MODE_SEGMENT = "segment"  # type 02: address = SBA + ((DRLO + DRI) MOD 64K)


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
        # bytes.fromhex() SKIPS ASCII whitespace BETWEEN byte pairs, so a
        # body carrying internal spaces decodes to exactly the expected
        # length and would sail through every check below (Qodo round 4).
        # The round-1 fix guarded only against a decode that came out TOO
        # SHORT, which is the sub-case where the whitespace is odd-placed.
        # An Intel HEX record body is hex digits and nothing else; anything
        # else means the file was damaged in transit, and this tool's whole
        # contract is to refuse a file it cannot vouch for rather than hand
        # back a checksum for it. Checked BEFORE the length tests so those
        # operate on a string that is really hex.
        if any(c not in "0123456789abcdefABCDEF" for c in body):
            raise HexFormatError(
                "line %d: record body contains a non-hex character -- an "
                "Intel HEX body is hex digits only (bytes.fromhex() would "
                "silently skip internal whitespace, so it is refused here)"
                % lineno)
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

        # bytes.fromhex() silently ignores ALL whitespace, including
        # whitespace INSIDE body (str.strip() above only removed the
        # leading/trailing kind) -- so the len(body) >= 10 hex-character
        # check above does not bound len(raw_bytes): a body with internal
        # spaces can decode to far fewer bytes than its character count
        # implies. Validate the decoded length here, BEFORE indexing into
        # it for the header fields below -- indexing first (the previous
        # order) let a too-short raw_bytes raise an uncaught IndexError
        # instead of the HexFormatError this module's contract promises.
        if len(raw_bytes) < 4:
            raise HexFormatError(
                "line %d: record decodes to only %d byte(s) after "
                "internal whitespace is stripped -- too short for the "
                "byte-count/address/type header (need at least 4)"
                % (lineno, len(raw_bytes)))

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

    Requires a structurally valid Intel HEX EOF (type 01, address 0, no
    data) record before returning -- a truncated file (cut off mid-stream,
    or empty) never reaches one, and without this check would otherwise
    silently report a CRC over whatever partial image it had assembled so
    far, indistinguishable from a complete one.
    """
    if region_length <= 0:
        raise ValueError(
            "region_length must be positive, got %d" % region_length)

    # bytearray multiplication (not [fill] * region_length then bytearray())
    # so a real ~2 MB region never builds an intermediate Python list.
    image = bytearray([fill]) * region_length
    # Per-offset "has a record already assigned this byte in THIS file"
    # tracker -- see the REC_DATA branch below (finding: conflicting writes
    # must not silently last-write-wins). Scoped to one compute_image_crc32()
    # call, i.e. one input file, matching main()'s one-CRC-per-file model.
    written = bytearray(region_length)
    ext_base = 0  # accumulated from the most recent type-02/04 record
    addr_mode = ADDR_MODE_LINEAR  # which wrap rule ext_base was set under
    saw_eof = False

    for lineno, addr16, rec_type, data in parse_records(lines):
        # A record after the EOF record is refused rather than ignored (Qodo
        # round 2). Ignoring it is the same failure this tool exists to avoid:
        # two concatenated hex files, or one with appended garbage, would
        # otherwise yield a perfectly plausible checksum computed from the
        # first image alone. parse_records() already skips blank lines, so
        # trailing whitespace after EOF is still accepted -- it is trailing
        # RECORDS that are malformed.
        if saw_eof:
            raise HexFormatError(
                "line %d: a type 0x%02X record follows the EOF (type 01) "
                "record -- input is malformed or is two concatenated files"
                % (lineno, rec_type))
        if rec_type == REC_DATA:
            full_addr = ext_base + addr16
            for i, byte in enumerate(data):
                # Apply the address-wrap rule for the CURRENTLY active
                # addressing mode before region filtering / byte selection
                # -- see the module docstring's "do NOT wrap ... the same
                # way" section, cited from srec_intel(5). Type 02 wraps the
                # offset alone, modulo 64K, BEFORE adding the segment base
                # (so an overflow stays in the same segment); type 04 adds
                # first and only wraps the whole 32-bit sum, at 4 GiB.
                if addr_mode == ADDR_MODE_SEGMENT:
                    a = ext_base + ((addr16 + i) & 0xFFFF)
                else:
                    a = (full_addr + i) & 0xFFFFFFFF
                if region_base <= a < region_base + region_length:
                    offset = a - region_base
                    if written[offset] and image[offset] != byte:
                        raise HexFormatError(
                            "line %d: address 0x%08X was already assigned "
                            "0x%02X by an earlier record in this file; this "
                            "record assigns a conflicting 0x%02X -- refusing "
                            "rather than silently keeping whichever record "
                            "came last" % (lineno, a, image[offset], byte))
                    # A record that re-assigns the SAME value to an
                    # already-written byte is not a conflict -- deliberately
                    # allowed (e.g. overlapping records that happen to
                    # re-emit identical padding/fill bytes are harmless and
                    # common in the output of some hex-merging tools; only a
                    # value MISMATCH indicates two genuinely disagreeing
                    # sources for the same address).
                    image[offset] = byte
                    written[offset] = 1
        elif rec_type == REC_EOF:
            if addr16 != 0 or len(data) != 0:
                raise HexFormatError(
                    "line %d: EOF (type 01) record must have address 0000 "
                    "and no data" % lineno)
            saw_eof = True
            # Deliberately NOT `break`: the loop continues so the guard above
            # can see anything that follows.
        elif rec_type == REC_EXT_LINEAR_ADDR:
            if len(data) != 2:
                raise HexFormatError(
                    "line %d: type 04 record must carry exactly 2 data "
                    "bytes, got %d" % (lineno, len(data)))
            upper16 = (data[0] << 8) | data[1]
            ext_base = upper16 << 16
            addr_mode = ADDR_MODE_LINEAR
        elif rec_type == REC_EXT_SEGMENT_ADDR:
            if len(data) != 2:
                raise HexFormatError(
                    "line %d: type 02 record must carry exactly 2 data "
                    "bytes, got %d" % (lineno, len(data)))
            segment = (data[0] << 8) | data[1]
            ext_base = segment << 4
            addr_mode = ADDR_MODE_SEGMENT
        elif rec_type in (REC_START_SEGMENT_ADDR, REC_START_LINEAR_ADDR):
            # Start Segment/Linear Address records: byte count is always 04
            # (CS:IP or EIP) and the address field is conventionally 0000,
            # exactly like EOF's. Validated the same way EOF, type 02 and
            # type 04 validate their own required shape -- this module's
            # contract is to refuse a malformed record loudly, not to wave
            # one through just because its payload happens not to affect
            # the CRC.
            if len(data) != 4:
                raise HexFormatError(
                    "line %d: type 0x%02X start-address record must carry "
                    "exactly 4 data bytes, got %d"
                    % (lineno, rec_type, len(data)))
            if addr16 != 0:
                raise HexFormatError(
                    "line %d: type 0x%02X start-address record must have "
                    "address 0000, got %04X" % (lineno, rec_type, addr16))
            # entry point value itself is irrelevant to a data CRC
        else:
            raise HexFormatError(
                "line %d: unsupported Intel HEX record type 0x%02X"
                % (lineno, rec_type))

    if not saw_eof:
        raise HexFormatError(
            "no Intel HEX EOF (type 01) record found -- input is "
            "truncated or empty")

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


def _record(addr16, rec_type, data):
    """Build an arbitrary well-CHECKSUMMED record of any type/address/data
    -- used where the type-specific helpers above don't apply (e.g.
    exercising a start-address record's own length/address validation)."""
    body = bytes([len(data), (addr16 >> 8) & 0xFF, addr16 & 0xFF, rec_type]) \
        + bytes(data)
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

    # --- 2c: type-02 offset wraps modulo 0x10000 WITHIN the segment before
    # region filtering / byte selection (SBA + ((DRLO+DRI) MOD 64K),
    # srec_intel(5) "Extended Segment Address Record") -- this is the exact
    # scenario from the audited finding: segment=0x1000 (SBA=0x10000),
    # record starting at offset 0xFFFF with 2 data bytes. Byte 0 resolves to
    # 0x10000+0xFFFF=0x1FFFF (out of the 1-byte region below); byte 1's
    # offset 0xFFFF+1 wraps to 0x0000 within the SAME segment, landing back
    # at 0x10000 -- NOT at 0x20000, which is what unmasked addition (the
    # pre-fix behavior) computes and which falls outside the region,
    # silently dropping the byte. FAILS on 6a6ce6faf (returns FF000000, the
    # all-erased CRC, instead of 8EB18589).
    wrap02_base = 0x10000
    wrap02_length = 1
    lines_wrap02 = [
        _ext_segment_record(0x1000),
        _data_record(0xFFFF, [0xAA, 0xBB]),
        _EOF_RECORD,
    ]
    want_wrap02 = zlib.crc32(bytes([0xBB])) & 0xFFFFFFFF
    _ck("type-02: an offset crossing 0xFFFF wraps back into the SAME "
        "segment (SBA + ((DRLO+DRI) MOD 64K))",
        compute_image_crc32(lines_wrap02, wrap02_base, wrap02_length),
        want_wrap02)
    _ck("...and that is not just the all-erased-region CRC (the check "
        "discriminates from the pre-fix unmasked-addition failure mode)",
        want_wrap02 != zlib.crc32(bytes([0xFF])) & 0xFFFFFFFF, True)

    # --- 2d: type-04 wraps differently from type-02 -- it adds the offset
    # to the linear base FIRST and only wraps the total 32-bit sum, at the
    # 4 GiB boundary ((LBA+DRLO+DRI) MOD 4G, srec_intel(5) "Extended Linear
    # Address Record"), so unlike type-02 it CARRIES into the next 64K
    # block instead of wrapping back into the current one. upper16=1
    # (LBA=0x10000), record starting at offset 0xFFFE with 3 data bytes:
    # byte i=2's offset 0xFFFE+2=0x10000 carries the address to 0x20000.
    # A type-02-style "mask the offset before adding the base" formula
    # (the same rule that is correct for type 02) would instead wrap that
    # byte back to segment-relative 0x0000, landing at 0x10000 -- a
    # DIFFERENT, wrong address -- which is exactly the "conflating the two"
    # mistake the module docstring warns about. This case passes under the
    # correct (carry-then-wrap-at-4G) rule and fails under the type-02
    # rule; it does not need a real firmware image to reach (upper16=1 is
    # an ordinary, small extended-linear-address value).
    carry_base = 0x1FFFE
    carry_length = 4
    lines_carry04 = [
        _ext_linear_record(0x0001),
        _data_record(0xFFFE, [0x11, 0x22, 0x33]),
        _EOF_RECORD,
    ]
    img_carry = bytearray([0xFF] * carry_length)
    img_carry[0:3] = bytes([0x11, 0x22, 0x33])  # 0x1FFFE, 0x1FFFF, 0x20000
    want_carry04 = zlib.crc32(bytes(img_carry)) & 0xFFFFFFFF
    _ck("type-04: an offset crossing a 64K boundary CARRIES into the next "
        "block instead of wrapping back into the current one (unlike "
        "type-02) -- (LBA+DRLO+DRI) MOD 4G",
        compute_image_crc32(lines_carry04, carry_base, carry_length),
        want_carry04)
    img_carry_type02_style = bytearray([0xFF] * carry_length)
    img_carry_type02_style[0:2] = bytes([0x11, 0x22])  # byte i=2 wraps OUT
    want_carry04_type02_style = (
        zlib.crc32(bytes(img_carry_type02_style)) & 0xFFFFFFFF)
    _ck("...and that is not what applying type-02's wrap rule to a type-04 "
        "record would give (the check discriminates the two formulas)",
        want_carry04 != want_carry04_type02_style, True)

    # --- 2e: the 4 GiB wrap that (LBA+DRLO+DRI) MOD 4G itself specifies,
    # for completeness -- upper16=0xFFFF (LBA=0xFFFF0000), offset 0xFFFF,
    # byte i=1's total sum 0x100000000 wraps to 0x00000000. Unrealistic for
    # any image this tool audits (always << 4 GiB), but it is the literal
    # spec formula and the pre-fix code (plain unmasked addition, no wrap
    # at all) computes 0x100000000 -- outside any real region -- instead,
    # so this also FAILS on 6a6ce6faf.
    lines_wrap4g = [
        _ext_linear_record(0xFFFF),
        _data_record(0xFFFF, [0xAA, 0xBB]),
        _EOF_RECORD,
    ]
    img_wrap4g = bytearray([0xFF] * 16)
    img_wrap4g[0] = 0xBB  # byte i=1: 0xFFFF0000+0xFFFF+1 = 0x100000000 MOD 4G = 0
    want_wrap4g = zlib.crc32(bytes(img_wrap4g)) & 0xFFFFFFFF
    _ck("type-04: the address sum itself wraps modulo 4 GiB, per the "
        "literal spec formula",
        compute_image_crc32(lines_wrap4g, 0, 16), want_wrap4g)

    # --- 3: 0xFF fill -- a region with NO records must produce the
    # all-0xFF CRC, asserted against the explicit computed value, not just
    # "nonzero".
    empty_length = 0x10
    expected_ff_crc = zlib.crc32(bytes([0xFF] * empty_length)) & 0xFFFFFFFF
    # This literal is a REGRESSION PIN, not an externally-sourced reference
    # value: it is zlib.crc32(b'\xff' * 16), computed once and hardcoded
    # here so a later change to the fill byte or the region size shows up
    # as a diff against this constant -- catching a bug that broke both the
    # implementation AND the comparison the same way, which "assert equal
    # to a freshly recomputed value" cannot.
    _ck("all-0xFF, 16-byte fill CRC matches the pinned regression constant",
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

    # --- 5a': start-address records (type 03/05) are validated like their
    # 02/04/EOF siblings, not silently waved through by a bare `pass` --
    # even though their contents never affect the CRC. The length case is
    # the audited finding's exact scenario: a type-05 record declaring 1
    # data byte instead of the required 4. FAILS on 6a6ce6faf (accepted,
    # returns AFED0EBE -- identical to the same input with the record
    # deleted).
    _ck("a type-05 (start linear address) record with the wrong data "
        "length is refused",
        _raises([_ext_linear_record(0x1D00),
                  _record(0x0000, REC_START_LINEAR_ADDR, [0xBB]),
                  _EOF_RECORD]),
        True)  # byte_count=1 (not 4)
    _ck("a type-03 (start segment address) record with the wrong data "
        "length is refused",
        _raises([_ext_linear_record(0x1D00),
                  _record(0x0000, REC_START_SEGMENT_ADDR, [0xAA, 0xBB]),
                  _EOF_RECORD]),
        True)  # byte_count=2 (not 4)
    _ck("a type-05 record with a nonzero address is refused",
        _raises([_ext_linear_record(0x1D00),
                  _record(0x0010, REC_START_LINEAR_ADDR,
                           [0xAA, 0xBB, 0xCC, 0xDD]),
                  _EOF_RECORD]),
        True)  # 4 data bytes (correct length), but address 0010 not 0000
    # The positive control: well-formed, correctly-shaped type-05/type-03
    # records (4 data bytes, address 0000) are accepted, so the checks
    # above are really exercising validation and not just "always throws".
    _ck("a well-formed type-05 record (4 data bytes, address 0000) is "
        "accepted",
        _raises([_ext_linear_record(0x1D00),
                  _record(0x0000, REC_START_LINEAR_ADDR,
                           [0x11, 0x22, 0x33, 0x44]),
                  _EOF_RECORD]),
        False)
    _ck("a well-formed type-03 record (4 data bytes, address 0000) is "
        "accepted",
        _raises([_ext_linear_record(0x1D00),
                  _record(0x0000, REC_START_SEGMENT_ADDR,
                           [0x00, 0x00, 0x12, 0x34]),
                  _EOF_RECORD]),
        False)

    # --- 5b: a stream that never reaches a (structurally valid) EOF is
    # refused -- covers a truly empty file and a file truncated after its
    # last data record, both of which would otherwise reach the
    # unconditional CRC return over a partial image.
    _ck("an empty input (no records at all) is refused",
        _raises([]), True)
    _ck("data records with no EOF at all (truncated mid-file) is refused",
        _raises([_ext_linear_record(0x1D00),
                  _data_record(0x0010, [0xAA, 0xBB])]), True)
    _ck("an EOF record with a nonzero address is refused",
        _raises([":00001001EF"]), True)  # type 01, addr=0x0010, no data
    _ck("an EOF record carrying data is refused",
        _raises([":010000019965"]), True)  # type 01, 1 data byte -- malformed

    # And the positive control: a well-formed minimal file must NOT raise,
    # so the above are really exercising validation and not just "any input
    # throws".
    _ck("a well-formed minimal file does not raise",
        _raises([_EOF_RECORD]), False)

    # --- 5b': records AFTER the EOF record are refused, not ignored (Qodo
    # round 2). Ignoring them is how two concatenated hex files, or one with
    # appended garbage, produce a plausible checksum for the first image
    # alone -- the exact "believable wrong number" this tool must never
    # return.
    _ck("a data record after the EOF record is refused",
        _raises([_ext_linear_record(0x1D00),
                 _data_record(0x0010, [0xAA, 0xBB]),
                 _EOF_RECORD,
                 _data_record(0x0020, [0xCC, 0xDD])]), True)
    _ck("a second EOF record after the EOF record is refused",
        _raises([_EOF_RECORD, _EOF_RECORD]), True)
    _ck("two concatenated well-formed files are refused, not silently halved",
        _raises([_ext_linear_record(0x1D00),
                 _data_record(0x0010, [0xAA]),
                 _EOF_RECORD,
                 _ext_linear_record(0x1D00),
                 _data_record(0x0011, [0xBB]),
                 _EOF_RECORD]), True)

    # The discriminating negative: the guard must reject trailing RECORDS,
    # not trailing whitespace. Without this, a fix that refused everything
    # after EOF -- including the blank line most editors leave at EOF --
    # would pass all three checks above while breaking every real file.
    _ck("blank lines and whitespace after the EOF record are still accepted",
        _raises([_ext_linear_record(0x1D00),
                 _data_record(0x0010, [0xAA, 0xBB]),
                 _EOF_RECORD,
                 "",
                 "   ",
                 ""]), False)

    # --- 5c: a non-positive region length is refused by the core function
    # directly (not just by the CLI's argparse layer), so a caller that
    # imports this module cannot construct an invalid audit region either.
    def _raises_bad_length(region_length):
        try:
            compute_image_crc32([_EOF_RECORD], base, region_length)
            return False
        except ValueError:
            return True

    _ck("a zero region length is refused", _raises_bad_length(0), True)
    _ck("a negative region length is refused", _raises_bad_length(-1), True)

    # --- 5d: two checksum-valid records that assign DIFFERENT values to the
    # same address are refused rather than silently resolved by
    # last-write-wins. This is the audited finding's exact scenario:
    # address 0x1D000000..3 assigned 0x00000000 by one record and
    # 0xFFFFFFFF by another. FAILS on 6a6ce6faf (accepted, returns
    # 4D3F7C33 -- byte-identical to the CRC of a wholly-erased region,
    # i.e. the first record's payload is discarded with no signal at all).
    _ck("two records assigning DIFFERENT values to the same address are "
        "refused",
        _raises([_ext_linear_record(0x1D00),
                  _data_record(0x0000, [0x00, 0x00, 0x00, 0x00]),
                  _data_record(0x0000, [0xFF, 0xFF, 0xFF, 0xFF]),
                  _EOF_RECORD]),
        True)
    # The discriminating negative: re-assigning the SAME value to an
    # already-written address is a deliberate exception, not a conflict --
    # e.g. two overlapping records that happen to agree on a byte. Without
    # this, a fix that refused ANY repeated write (not just a conflicting
    # one) would pass the check above while breaking a real, harmless
    # overlap.
    _ck("two records assigning the SAME value to the same address are "
        "still accepted",
        _raises([_ext_linear_record(0x1D00),
                  _data_record(0x0000, [0xAA, 0xAA, 0xAA, 0xAA]),
                  _data_record(0x0002, [0xAA, 0xAA, 0xAA, 0xAA]),
                  _EOF_RECORD]),
        False)  # bytes at 0x0002..0x0003 overlap and agree (0xAA == 0xAA)
    conflict_lines = [
        _ext_linear_record(0x1D00),
        _data_record(0x0000, [0x00, 0x00, 0x00, 0x00]),
        _data_record(0x0000, [0xFF, 0xFF, 0xFF, 0xFF]),
        _EOF_RECORD,
    ]
    _ck("...and the conflicting-write CRC is NOT the same value the tool "
        "used to silently return (the check discriminates the two "
        "records from the all-erased-region fallback)",
        _raises(conflict_lines), True)

    # --- 6: a record whose body has INTERNAL whitespace (distinct from the
    # leading/trailing whitespace str.strip() already handles) decodes, via
    # bytes.fromhex()'s own whitespace-stripping, to fewer bytes than its
    # apparent length implies -- this must raise HexFormatError, not an
    # uncaught IndexError from indexing the header before its length is
    # checked. FAILS on 6a6ce6faf (uncaught IndexError, unhandled by
    # main()'s per-file except clause, which catches only
    # FileNotFoundError/HexFormatError).
    _ck("a record body with internal whitespace is refused with "
        "HexFormatError, not an uncaught IndexError",
        _raises([":00        00"]), True)
    _ck("...and a record body with only leading/trailing whitespace "
        "(already handled by str.strip()) is unaffected",
        _raises(["   " + _EOF_RECORD + "   "]), False)

    # --- 6a': the round-1 guard above only caught whitespace that made the
    # decode come out TOO SHORT. bytes.fromhex() skips whitespace BETWEEN
    # byte pairs, so an EVEN number of spaces in the right places decodes to
    # exactly the expected length and passed every check (Qodo round 4).
    # These FAIL on a8513aaa9: each returns a CRC with exit 0.
    _ck("an EOF body with internal spaces between byte pairs is refused "
        "(decodes to the RIGHT length, so the length guard cannot see it)",
        _raises([":00 00 00 01 FF"]), True)
    _ck("a DATA body with internal spaces between byte pairs is refused",
        _raises([_ext_linear_record(0x1D00),
                 ":02 0010 00 AABB 89",
                 _EOF_RECORD]), True)
    _ck("a body containing a non-hex letter is refused",
        _raises([":0000000G FF"]), True)
    # The discriminating negative for THIS guard: a body of pure hex digits,
    # any case, must still be accepted. A "fix" that rejected anything
    # outside [0-9A-F] would pass all three checks above and break every
    # lowercase hex file in existence.
    _ck("a lowercase-hex body is still accepted",
        _raises([":020000041d00dd",
                 _data_record(0x0010, [0xAA, 0xBB]),
                 _EOF_RECORD]), False)

    # --- 6b: the actual harm named by the finding above is not the crash
    # itself (main()'s per-file loop already turns a HexFormatError into a
    # clean per-file error) but that an UNCAUGHT IndexError used to abort
    # the whole batch, so a well-formed file listed AFTER a malformed one
    # was never even attempted. Exercise main() itself -- not a
    # reimplementation of its loop -- with a bad file followed by a good
    # one, and require BOTH that the run is reported as failed (rc=1, for
    # the bad file) AND that the good file's correct CRC still appears in
    # the output.
    def _batch_continues_past_a_malformed_file():
        import contextlib
        import io
        import os
        import tempfile

        with tempfile.TemporaryDirectory() as d:
            bad_path = os.path.join(d, "bad.hex")
            good_path = os.path.join(d, "good.hex")
            with open(bad_path, "w", encoding="ascii") as f:
                f.write(":00        00\n")
            with open(good_path, "w", encoding="ascii") as f:
                f.write(_EOF_RECORD + "\n")

            old_argv = sys.argv
            sys.argv = ["hexcrc.py", bad_path, good_path]
            out, err = io.StringIO(), io.StringIO()
            try:
                with contextlib.redirect_stdout(out), \
                        contextlib.redirect_stderr(err):
                    rc = main()
            finally:
                sys.argv = old_argv

        expected_good_crc = compute_image_crc32(
            [_EOF_RECORD], STANDALONE_PHYS_BASE, STANDALONE_AUDIT_LENGTH)
        good_crc_line = "%08X" % expected_good_crc
        return rc == 1 and good_crc_line in out.getvalue()

    _ck("a malformed file (bad.hex) does not prevent a later, "
        "well-formed file (good.hex) in the same invocation from being "
        "processed -- rc=1 for the batch, but good.hex's CRC is still "
        "printed",
        _batch_continues_past_a_malformed_file(), True)

    bad = _CHECKS.count(False)
    print("self-test: %d/%d checks passed" % (_CHECKS.count(True), len(_CHECKS)))
    return 1 if bad else 0


def _positive_int(s):
    """argparse type= for --length: a non-positive region length makes
    compute_image_crc32() assemble a (silently) empty image and report
    zlib's empty-input CRC (00000000) as though it were a real result."""
    v = int(s, 0)
    if v <= 0:
        raise argparse.ArgumentTypeError(
            "must be a positive integer, got %r" % s)
    return v


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
    ap.add_argument("--length", type=_positive_int,
                     default=STANDALONE_AUDIT_LENGTH,
                     help="audited region length in bytes, must be > 0 "
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
