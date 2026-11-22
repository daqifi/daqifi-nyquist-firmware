#!/usr/bin/env python3
"""Pin that #1199's guard is actually WIRED, not merely present.

The C test next door (test_1199_sample_emit.c) proves SampleEmit.h decides
correctly. That is a different claim from the encoders using it, and the
second claim is the one that has historically failed: a correct predicate
nobody calls is worthless.

Three properties, none of them presence-only:

  1. ORDERING inside SampleEmit.h -- the finiteness test must precede the
     range comparisons. This is the half of the defect the C test CANNOT
     observe: if the order is wrong, NaN reaches the cast, and executing that
     cast to detect it is the undefined behaviour under test. So it is pinned
     structurally instead.

  2. THE OLD INLINE CLAMP IS GONE from both encoders. An absence check on the
     DEFECT's own shape, not on a helper's name: if anyone reintroduces
     `> (double)INT32_MAX` beside a cast in an encoder, that is the bug
     coming back regardless of what else the file calls.

  3. CORRESPONDENCE in JSON_Encoder.c -- every numeric `"val":` emitter has a
     `"val":null` counterpart. Counting the guard's *call sites* would be
     presence-only; counting emitters against their fallbacks is a property of
     the output shape, which is what actually matters on the wire.

Run: python3 tests/host/check_1199_emitter_wiring.py
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
HDR = ROOT / "firmware/src/Util/SampleEmit.h"
JSONC = ROOT / "firmware/src/services/JSON_Encoder.c"
CSVC = ROOT / "firmware/src/services/csv_encoder.c"

fails = []


def check(ok, msg):
    print(("  [OK]   " if ok else "  [FAIL] ") + msg)
    if not ok:
        fails.append(msg)


def body_of(src, name):
    """The text of a brace-balanced function body, by name. Crude but exact
    enough: these are small static inline functions with no nested braces in
    their signatures."""
    i = src.index(name + "(")
    j = src.index("{", i)
    depth, k = 0, j
    while k < len(src):
        if src[k] == "{":
            depth += 1
        elif src[k] == "}":
            depth -= 1
            if depth == 0:
                return src[j:k + 1]
        k += 1
    raise AssertionError("unbalanced braces in " + name)


def main():
    for p in (HDR, JSONC, CSVC):
        if not p.exists():
            print("  [FAIL] missing file: %s" % p)
            return 1

    hdr = HDR.read_text()
    jsonc = JSONC.read_text()
    csvc = CSVC.read_text()

    # ---- 1. ordering inside the helper -------------------------------------
    body = body_of(hdr, "sample_emit_to_int32")
    i_fin = body.find("isfinite")
    i_max = body.find("INT32_MAX")
    check(i_fin != -1 and i_max != -1,
          "sample_emit_to_int32 tests isfinite AND compares against INT32_MAX")
    check(i_fin != -1 and i_max != -1 and i_fin < i_max,
          "the finiteness test PRECEDES the range comparisons -- ordering them "
          "the other way is the original defect, because every comparison "
          "with NaN is false and NaN would reach the cast")

    # ---- 2. the old inline clamp is gone from both encoders ----------------
    # The defect's shape: a range comparison against the int32 limits sitting
    # in an encoder, which is where the unguarded cast lived.
    for name, src in (("JSON_Encoder.c", jsonc), ("csv_encoder.c", csvc)):
        stray = re.findall(r">\s*\(double\)\s*INT32_MAX", src)
        check(not stray,
              "%s no longer clamps against (double)INT32_MAX inline "
              "(found %d) -- that comparison is SampleEmit.h's job now, and "
              "reintroducing it here reintroduces the NaN hole"
              % (name, len(stray)))

    # ---- 3. both encoders include the header and call it -------------------
    for name, src in (("JSON_Encoder.c", jsonc), ("csv_encoder.c", csvc)):
        check('#include "Util/SampleEmit.h"' in src,
              "%s includes Util/SampleEmit.h" % name)
        calls = len(re.findall(r"sample_emit_(?:to_int32|is_numeric)\s*\(", src))
        check(calls >= 2,
              "%s calls the guard on both of its value paths (int + decimal); "
              "found %d call(s)" % (name, calls))

    # ---- 4. correspondence: every numeric val emitter has a null fallback --
    # Keyed on the OUTPUT SHAPE rather than on the guard's name, so a refactor
    # that keeps the wire format honest still passes, and one that drops a
    # fallback fails even if it still calls the helper.
    # ⚠ NOT "every numeric emitter needs a null". The first version of this
    # check asserted that and FAILED on correct code: JSON_Encoder.c has THREE
    # numeric "val" emitters, and the third is the raw-mode path, which prints
    # the ADC code itself -- an integer, no cal, no double math (#158/#270).
    # A value that was never a double cannot be non-finite, so it needs no
    # fallback and must not be made to have one. The third emitter is
    # ACCOUNTED FOR below rather than quietly excluded from the count.
    numeric = len(re.findall(r'\\"val\\":%(?:d|\.\*f)', jsonc))
    nulls = len(re.findall(r'\\"val\\":null', jsonc))
    guards = len(re.findall(r"sample_emit_(?:to_int32|is_numeric)\s*\(", jsonc))
    check(numeric > 0, "JSON_Encoder.c still has numeric \"val\" emitters "
                       "(found %d) -- otherwise this pin is vacuous" % numeric)
    check(guards == nulls,
          "each guarded value path in JSON_Encoder.c has exactly one "
          "\"val\":null fallback (%d guards, %d nulls) -- a new converted "
          "emitter without a fallback fails here" % (guards, nulls))
    check(numeric == guards + 1,
          "and the one UNGUARDED numeric emitter is accounted for: %d numeric "
          "= %d guarded + 1 raw-mode. If you added another unguarded emitter, "
          "it must be integer-only by construction -- say so here and bump "
          "the 1." % (numeric, guards))
    # The accounting above is only true if that third emitter really is the raw
    # integer path, so assert what it prints rather than trusting the count.
    raw_block = jsonc[jsonc.index("if (rawMode)"):]
    raw_block = raw_block[:raw_block.index("} else")]
    check('(int32_t)rawValue' in raw_block and 'ADC_ConvertToVoltage' not in raw_block,
          "the raw-mode emitter prints the ADC code directly and performs no "
          "voltage conversion, so its value cannot be non-finite")

    print()
    if fails:
        print("FAIL -- %d wiring check(s) failed:" % len(fails))
        for f in fails:
            print("  - " + f)
        return 1
    print("OK -- #1199's guard is wired at every emitter.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
