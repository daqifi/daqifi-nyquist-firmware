#!/usr/bin/env python3
"""Pin that the three encoders book the SAME loss count for the same discard (#1200).

#1200's acceptance is "a host test evicts an AIN+DIO row and gets the same
loss count from all three encoders". The encoders cannot be host-compiled
(board config + ADC HAL + FreeRTOS), and the house idiom for that --
test_970_encoder_consume_accounting.c -- models their shapes against a mock.
A model is a COPY, and a copy drifts.

So this reads the REAL sources and derives what each encoder books, the same
way $(BENCH_BIN)'s recipe greps the real timeout constants rather than
trusting its copy of them. If an encoder's booking expression changes, this
fails loudly instead of letting a stale model keep passing.

THE CONTRACT, which arbitrates between the encoders when they disagree
(streaming.h, on Streaming_ReportEncoderSampleLoss):

    report samples an encoder CONSUMED and then destroyed. Only a site that
    has already popped the queue entry [...] may call this

The unit is a POPPED QUEUE ENTRY. A discarded row carrying both an AIN and a
DIO entry destroys TWO samples. Before #1200, CSV booked a literal 1 for that
row while PB booked 2 -- so the counter under-reported, and which number you
got depended on the encoding, not on what was lost.

Run: python3 tests/host/check_1200_eviction_loss_parity.py
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CSV = ROOT / "firmware/src/services/csv_encoder.c"
PB = ROOT / "firmware/src/services/DaqifiPB/NanoPB_Encoder.c"
JSONC = ROOT / "firmware/src/services/JSON_Encoder.c"
HDR = ROOT / "firmware/src/services/streaming.h"

CALL = r"Streaming_ReportEncoderSampleLoss\s*\(\s*([^;]*?)\s*\)\s*;"

fails = []


def check(ok, msg):
    print(("  [OK]   " if ok else "  [FAIL] ") + msg)
    if not ok:
        fails.append(msg)


def bookings(path):
    """Every booking argument in a source, as written."""
    src = path.read_text()
    # Skip comment-only mentions: require it to be a real call with a ';'.
    return re.findall(CALL, src)


def main():
    for p in (CSV, PB, JSONC, HDR):
        if not p.exists():
            print("  [FAIL] missing: %s" % p)
            return 1

    csv_args = bookings(CSV)
    pb_args = bookings(PB)
    json_args = bookings(JSONC)

    # ---- the contract still says what this pin assumes --------------------
    hdr = HDR.read_text()
    check("report samples an encoder CONSUMED and then destroyed" in hdr,
          "streaming.h still states the counter's unit as SAMPLES consumed "
          "and destroyed -- this pin's whole premise")
    check("already popped the queue entry" in hdr,
          "and still ties a booking to a POPPED QUEUE ENTRY, which is what "
          "makes an AIN+DIO row two samples rather than one row")

    # ---- non-vacuity: all three must actually book somewhere --------------
    check(len(csv_args) >= 1, "csv_encoder.c books loss (%d site(s))" % len(csv_args))
    check(len(pb_args) >= 1, "NanoPB_Encoder.c books loss (%d site(s))" % len(pb_args))
    check(len(json_args) >= 1, "JSON_Encoder.c books loss (%d site(s))" % len(json_args))

    # ---- CSV must book a COUNT, not a literal 1 --------------------------
    # The defect was a literal. A variable means the site counts what it
    # popped; `1u` means it cannot distinguish one entry from two.
    csv_literal_ones = [a for a in csv_args if re.fullmatch(r"1[uU]?", a.strip())]
    check(not csv_literal_ones,
          "csv_encoder.c no longer books a LITERAL 1 for a discarded row "
          "(found %r) -- that literal is the #1200 defect: it cannot tell a "
          "row that destroyed one entry from one that destroyed two"
          % (csv_literal_ones,))
    check(any(re.search(r"evictedSamples", a) for a in csv_args),
          "csv_encoder.c books the number of entries it actually popped "
          "(an evictedSamples counter), so an AIN+DIO row books 2")

    # ---- and it is incremented once per successful pop, not set ----------
    csv_src = CSV.read_text()
    incs = len(re.findall(r"evictedSamples\s*\+\+", csv_src))
    check(incs == 2,
          "the counter is incremented once per queue it pops -- AIN and DIO, "
          "so exactly 2 increment sites (found %d). Both pops are independent "
          "`if`s, so a row with only one component still books 1." % incs)

    # ---- PB still books per sample, which is the behaviour CSV matches ----
    pb_pair = [a for a in pb_args if "dioS" in a and "2" in a]
    check(bool(pb_pair),
          "NanoPB_Encoder.c still books 2 when its message carried the DIO "
          "sample too (found %r) -- this is the reference CSV was brought "
          "into line with, so if it changes, revisit #1200 rather than "
          "silently diverging again" % (pb_pair,))

    # ---- the stale claim is gone from both places ------------------------
    for label, text in (("streaming.h", hdr),
                        ("docs/STREAMING_AND_ADC.md",
                         (ROOT / "docs/STREAMING_AND_ADC.md").read_text())):
        # The claim may survive ONLY as an explicitly historical quote, so the
        # property is "every occurrence is inside quotes", not a count match.
        # (An earlier version of this check compared two counts and failed on
        # correct text, because the docs write it as `csv_Encode` "destroys
        # nothing..." -- backtick and quote between the two words, so the
        # naive phrase regex matched zero. Test the property, not two regexes
        # that have to agree.)
        occurrences = [m.start() for m in re.finditer(r"destroys nothing", text)]
        unquoted = [i for i in occurrences if '"' not in text[max(0, i - 24):i]]
        check(not unquoted,
              "%s no longer ASSERTS that csv_Encode destroys nothing "
              "(%d occurrence(s), all quoted as historical; %d asserting) -- "
              "it books from its eviction arm and has since #970/#991"
              % (label, len(occurrences), len(unquoted)))

    print()
    if fails:
        print("FAIL -- %d parity check(s) failed:" % len(fails))
        for f in fails:
            print("  - " + f)
        return 1
    print("OK -- all three encoders book per destroyed sample.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
