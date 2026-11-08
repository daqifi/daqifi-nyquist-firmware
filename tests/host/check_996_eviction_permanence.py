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


def _split_top_level(text, op):
    """`text` split on `op` only where parenthesis depth is zero."""
    parts, depth, start, i = [], 0, 0, 0
    while i < len(text):
        ch = text[i]
        if ch == "(":
            depth += 1
        elif ch == ")":
            depth -= 1
        elif depth == 0 and text.startswith(op, i):
            parts.append(text[start:i])
            i += len(op)
            start = i
            continue
        i += 1
    parts.append(text[start:])
    return parts


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

    # ---- the condition's STRUCTURE, not just its operands ----------------
    # ⛔ r1 MEDIUM: an earlier version of this pin searched the condition for
    # the operand STRINGS. That is blind to the operator joining them, so
    #
    #     if ((hadAIN || hadDIO) && fullCapacityCall || rem == maxRoom)
    #
    # passed every check while evicting a 100-byte row on a full-capacity
    # call with 40 bytes left -- a row the next fresh buffer would have
    # written. `&&` binds tighter than `||`, so that reads as
    # `(peeked && fullCapacity) || (rem == maxRoom)` and the first arm alone
    # is enough. The C model cannot see it either: it executes its own COPY
    # of the condition. So the structure has to be pinned here.
    cond_text = _eviction_condition(code)
    check(bool(cond_text),
          "the eviction condition was located (if its shape changed beyond "
          "recognition, update the model and this pin together)")
    top_or = _split_top_level(cond_text, "||")
    check(len(top_or) == 1,
          "the condition is a pure conjunction at the top level -- no `||` "
          "outside parentheses. A top-level `||` makes any one arm "
          "sufficient, which is how a row that would fit a later buffer "
          "gets evicted (found %d top-level `||` arm(s))" % (len(top_or) - 1))
    parts = [c.strip() for c in _split_top_level(cond_text, "&&")]
    check(len(parts) >= 3,
          "and joins at least three conjuncts with `&&` (found %d: %r)"
          % (len(parts), parts))
    check(any("hadAIN" in c and "hadDIO" in c for c in parts),
          "one conjunct requires something to have been peeked "
          "(hadAIN/hadDIO)")
    check(any(re.fullmatch(r"\(*\s*fullCapacityCall\s*\)*", c) for c in parts),
          "one conjunct is fullCapacityCall itself -- without it a caller "
          "with its own small buffer evicts rows the streaming path would "
          "have written")
    check(any(re.fullmatch(r"\(*\s*rem\s*==\s*maxRoom\s*\)*", c)
              for c in parts),
          "and one is `rem == maxRoom` -- the literal claim that this "
          "attempt already had the largest room any call will ever offer. "
          "`>=`, `!=`, or dropping it turns 'never fits' into 'did not fit "
          "this time', which discards rows that would have fitted")

    # ---- fullCapacityCall is DERIVED, not a hardcoded size ---------------
    decl = re.search(r"fullCapacityCall\s*=([^;]+);", code)
    decl_text = decl.group(1) if decl else ""
    check(bool(decl_text),
          "fullCapacityCall is declared at all -- the first thing that "
          "disappears if the permanence test is reverted")
    # ⚠ BOTH ARMS GUARDED ON THE DECLARATION EXISTING. An earlier draft
    # tested only "no hardcoded byte count in decl_text", which PASSES
    # VACUOUSLY when there is no declaration: measured on the pre-fix
    # revision, where decl_text is '' and the digit regex finds nothing. A
    # check that cannot fail on the revision it exists to catch is noise.
    # ⚠ AN INDIRECTION IS ACCEPTED, because a correct refactor may hoist the
    # accessor into a local and compare against that. Requiring the call
    # INSIDE the declaration would fail such a change while the behaviour is
    # identical. What must not happen is the size becoming a constant.
    via_local = False
    if decl_text and "Streaming_GetEncoderBufferSize" not in decl_text:
        for ident in set(re.findall(r"[A-Za-z_]\w*", decl_text)):
            if re.search(r"\b%s\s*=[^;]*Streaming_GetEncoderBufferSize"
                         % re.escape(ident), code):
                via_local = True
                break
    check(bool(decl_text) and ("Streaming_GetEncoderBufferSize" in decl_text
                               or via_local),
          "and is derived from Streaming_GetEncoderBufferSize(), directly or "
          "through a local assigned from it%s -- not compared against a "
          "literal, which would stop being true the moment the configured "
          "buffer size changed, and silently"
          % (" (via a local)" if via_local else ""))
    check(bool(decl_text) and re.search(r"\b\d{3,}\b", decl_text) is None,
          "and compares against no hardcoded byte count (%r)" % decl_text.strip())

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
