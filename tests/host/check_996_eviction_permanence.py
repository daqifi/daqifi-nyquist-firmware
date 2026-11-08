#!/usr/bin/env python3
"""Pin that csv_Encode() evicts only a PERMANENTLY unfittable row (#996).

#996 fixed a stall: a row that could never fit any buffer sat at the head of
a FIFO forever, re-peeked and re-rejected, taking every sample behind it and
silently stopping the CSV stream. The fix evicts that row.

⛔ THE RISK THIS PIN GUARDS IS THE OPPOSITE ERROR, and it is the expensive
one. "Does not fit the room left in THIS call" and "does not fit ANY buffer,
ever" are different verdicts. Evicting on the first discards rows that a
later call -- handed a fresh, full buffer -- would have written: silent data
loss, where the defect being fixed was a silent stall. #961, the JSON twin,
shipped exactly that inversion in its round 2 (an off-by-2 that dropped rows
which actually fitted) and needed a third round to undo it.

So the permanence test is three conjuncts, and all three must survive:

    (hadAIN || hadDIO)      something was actually peeked
    && fullCapacityCall     this call was handed the whole configured buffer
    && rem == maxRoom       and none of that room is already spent

csv_encoder.c cannot be host-compiled (board config, ADC HAL, FreeRTOS), so
test_996_csv_eviction_permanence.c models the decision and this reads the
REAL source to keep the model honest -- the same division of labour as
check_1200_eviction_loss_parity.py, which pins the BOOKING for the same
eviction and would pass unchanged if the DECISION were weakened.

⚠ COMMENT STRIPPING IS MANDATORY HERE, not cosmetic. The eviction site's
comment block explains the rejected alternatives BY NAME -- it contains the
strings `rem != maxRoom`, `fullCapacityCall` and `if (hadAIN)` as prose about
what not to do. A raw-text scan would find the shape it is looking for inside
the explanation of why that shape is wrong. Stripped via the compiler
(`-fpreprocessed -E`), not a regex: a regex stripper eats pointer
dereferences, and this file is full of them.

⚠ WHAT A CORRECT CHANGE *WILL* BREAK, stated so it is updated rather than
silenced: this reads source text, so renaming `maxRoom`/`fullCapacityCall`,
or hoisting the permanence test into a helper like `rowCanNeverFit()`, fails
these pins while the behaviour is intact. That is the intended cost -- the
alternative is no guard on a silent data-loss boundary. Update the model and
this pin together; do not relax a conjunct to make it pass.

Run: python3 tests/host/check_996_eviction_permanence.py
"""
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CSV = ROOT / "firmware/src/services/csv_encoder.c"

# The two expressions this pin exists to hold still, verbatim from the source
# with whitespace runs collapsed. Updating either is a deliberate act that
# belongs in the same PR as the firmware change it describes.
EXPECTED_CONDITION = "(hadAIN || hadDIO) && fullCapacityCall && rem == maxRoom"
EXPECTED_FULLCAP = "(buffSize == (size_t) Streaming_GetEncoderBufferSize())"

fails = []


def check(ok, msg):
    print("  [%s] %s" % ("ok" if ok else "FAIL", msg))
    if not ok:
        fails.append(msg)


def strip_comments(src, cc="cc"):
    """The file's own text with comments gone, includes unchased."""
    try:
        out = subprocess.run([cc, "-fpreprocessed", "-E", "-"],
                             input=src, capture_output=True, text=True)
    except (OSError, subprocess.SubprocessError) as exc:
        raise RuntimeError("could not run %s to strip comments: %s" % (cc, exc))
    if out.returncode != 0:
        raise RuntimeError("%s -fpreprocessed -E failed: %s"
                           % (cc, out.stderr.strip()[:200]))
    return out.stdout


def _norm(text):
    """Whitespace runs collapsed to one space, ends stripped.

    So reflowing a long line, or re-indenting it, is not a failure -- and a
    CRLF checkout normalises to the same string as an LF one, which matters
    because CI is LF.
    """
    return re.sub(r"\s+", " ", text).strip()


def _eviction_condition(code):
    """The eviction `if`'s condition text, by paren matching.

    ⚠ NOT a regex. `[^)]*` cannot express "up to the matching paren", and an
    earlier version of this pin used it -- so the condition it examined
    stopped at the first `)`, which is the one closing `(hadAIN || hadDIO)`.
    It was reading a fragment and calling it the condition.
    """
    m = re.search(r"if\s*\(\s*\(\s*hadAIN\s*\|\|\s*hadDIO\s*\)", code)
    if not m:
        return ""
    i = code.index("(", m.start())      # the `if`'s own open paren
    depth, j = 0, i
    while j < len(code):
        if code[j] == "(":
            depth += 1
        elif code[j] == ")":
            depth -= 1
            if depth == 0:
                return code[i + 1:j]
        j += 1
    return ""


def main():
    if not CSV.exists():
        print("  [FAIL] missing: %s" % CSV)
        return 1

    raw = CSV.read_text()
    code = strip_comments(raw)

    # ---- the rejected shape is not in the CODE ---------------------------
    # ⚠ ASSERTS THE CODE, REPORTS THE PROSE. An earlier draft asserted
    # `raw_prose > 0` too -- "the comment really does name the rejected
    # shape" -- which a CORRECT change breaks: trim that rationale and the
    # pin fails while the code is perfect. The invariant is that the
    # rejected shape is absent from the code; the raw count is diagnostic,
    # and its only job is to show whether stripping was load-bearing today.
    raw_prose = len(re.findall(r"rem\s*!=\s*maxRoom", raw))
    code_prose = len(re.findall(r"rem\s*!=\s*maxRoom", code))
    print("  [--] stripping: `rem != maxRoom` appears %d time(s) raw, %d in "
          "code -- raw>0 means a raw-text pin would have read the "
          "explanation of the defect as the defect" % (raw_prose, code_prose))
    check(code_prose == 0,
          "the rejected `rem != maxRoom` shape is absent from the code")

    # ---- exactly one eviction site, so the pins below are unambiguous ----
    # Anchored on the pop pair that defines the site, not on a line number.
    evict_sites = re.findall(r"if\s*\(\s*\(\s*hadAIN\s*\|\|\s*hadDIO\s*\)", code)
    check(len(evict_sites) == 1,
          "exactly one eviction decision in csv_Encode (found %d) -- more "
          "than one means two places decide permanence and can disagree"
          % len(evict_sites))

    # ---- the two expressions, pinned EXACTLY ----------------------------
    # ⛔ THIS USED TO RECOGNISE STRUCTURE AND KEPT LOSING. Three audit rounds,
    # same shape every time: the pin was taught the mutants it had been shown
    # and the next round found the next one. Operand text missed `&&` -> `||`
    # (r1). Structural matching missed `==` -> `!=` inside fullCapacityCall
    # and an EXTRA `&& hadDIO` conjunct, which with DIO disabled restores the
    # original #996 stall (r2). The mutant space for "recognise the shape" is
    # open-ended, so recognition cannot win.
    #
    # So these are EXACT compares against canonical text, whitespace
    # normalised so reflowing a line is not a failure. Any other edit fails
    # the build -- including a correct one, which is the point: a deliberate
    # firmware change updates this string in the same PR, and that diff is
    # the review moment. Do not relax this back into matching.
    cond_text = _norm(_eviction_condition(code))
    check(bool(cond_text),
          "the eviction condition was located (paren-matched, so the whole "
          "expression is compared and not a fragment)")
    check(cond_text == EXPECTED_CONDITION,
          "the eviction condition is EXACTLY the pinned expression"
          if cond_text == EXPECTED_CONDITION else
          "the eviction condition CHANGED\n"
          "         expected: %s\n"
          "         found:    %s" % (EXPECTED_CONDITION, cond_text or "<none>"))

    decl = re.search(r"fullCapacityCall\s*=([^;]+);", code)
    decl_text = _norm(decl.group(1)) if decl else ""
    check(bool(decl_text),
          "fullCapacityCall is declared at all -- the first thing that "
          "disappears if the permanence test is reverted")
    check(decl_text == EXPECTED_FULLCAP,
          "its initialiser is EXACTLY the pinned expression"
          if decl_text == EXPECTED_FULLCAP else
          "the fullCapacityCall initialiser CHANGED\n"
          "         expected: %s\n"
          "         found:    %s" % (EXPECTED_FULLCAP, decl_text or "<none>"))

    # ---- the log that makes the eviction observable ----------------------
    check("LOG_SESSION_CSV_SAMPLE_TOO_LARGE" in code,
          "an evicted row is logged (LOG_SESSION_CSV_SAMPLE_TOO_LARGE), so "
          "a stream losing rows says so instead of going quiet")

    print()
    if fails:
        print("FAILED: %d check(s)" % len(fails))
        print("If csv_Encode's permanence test was changed deliberately, "
              "update test_996_csv_eviction_permanence.c's model and this "
              "pin together -- they are two halves of one claim.")
        return 1
    print("OK: csv_Encode evicts only a permanently unfittable row")
    return 0


if __name__ == "__main__":
    sys.exit(main())
