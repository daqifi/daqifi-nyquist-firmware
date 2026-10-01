#!/usr/bin/env bash
# Self-test for REPAIR 11: the two `[^.]` spans in RE_DISCLAIMED became `.`.
#
# PROVES BOTH DIRECTIONS, because a rule held is not a rule run and a pass/fail
# instrument must be shown to do both before any unknown input:
#   * the bug case now HITS   (it missed before -- that is the repair)
#   * the no-dot cases still HIT  (no regression)
#   * genuine non-disclaimers still MISS (the widening did not swallow everything)
#
# PARAMETERISED ON THE GREP BINARY ON PURPOSE. `grep` in an agent's Bash-tool
# one-liner is a shell FUNCTION wrapping ugrep 7.8.4; inside `bash x.sh` it is
# /usr/bin/grep (GNU 3.11). A fix verified in one path is UNVERIFIED in the other,
# so this runs under whichever binary the caller names and the caller runs it twice.
#   GREPBIN=/usr/bin/grep bash SELFTEST-disclaimer-dot-span.sh
set -u
GREPBIN="${GREPBIN:-/usr/bin/grep}"
SPEC="$(dirname "$0")/SPEC-firmware-prose-vs-verdict-census.sh"

# Pull the LIVE pattern out of the spec rather than restating it. A test that
# restates its subject's pattern passes when the subject is wrong.
#
# ⚠ eval ONLY THAT ONE LINE -- do NOT `source` the spec. The spec is the census and
# sourcing it would execute it, firing gh API calls against 30 PRs. Caught while
# writing this test, which is the same shape as the test it contains: a convenience
# that silently does far more than it says.
eval "$(/usr/bin/grep -m1 '^RE_DISCLAIMED=' "$SPEC")" 2>/dev/null || true
[ -z "${RE_DISCLAIMED:-}" ] && { echo "REFUSE: could not extract RE_DISCLAIMED from $SPEC"; exit 2; }

pass=0; fail=0
check() { # check <expect 1|0> <label> <sentence>
  local want="$1" label="$2" s="$3" got
  if printf '%s\n' "$s" | "$GREPBIN" -qiE -- "$RE_DISCLAIMED"; then got=1; else got=0; fi
  if [ "$got" -eq "$want" ]; then pass=$((pass+1)); printf '  ok   want=%s %s\n' "$want" "$label"
  else fail=$((fail+1)); printf '  FAIL want=%s got=%s %s\n      subject: %s\n' "$want" "$got" "$label" "$s"; fi
}

echo "== $("$GREPBIN" --version | head -1)"

# 1. THE BUG. Dot inside the span; this returned 0 before the repair.
check 1 "dot in span (THE REPAIR)"        "Do not read the regression_gate.py result as a pass"
check 1 "dot in span, second alternative" "Please read this before treating anything in audit_x.json here as a pass"
check 1 "version dot in span"             "Do not read v1.2 of this artifact as a clean verdict"

# 2. NO REGRESSION. These matched before and must still match.
check 1 "no dot, first alternative"       "Do not read this as a pass"
check 1 "no dot, second alternative"      "read this before treating anything here as a pass"
check 1 "field form still required"       "noProvenance: true"
check 1 "unrelated alternative untouched" "The sanctioned orchestrator did not run"

# 3. THE WIDENING DID NOT SWALLOW EVERYTHING. Genuine non-disclaimers.
check 0 "plain instruction, no verdict"   "Do not read the datasheet section 4.2 before wiring it"
check 0 "field form NEGATED still misses" "noProvenance: false"
check 0 "ordinary prose"                  "The manifest participates in rotation and bucketing"
check 0 "pass too far away (>40 chars)"   "Do not read any of the following material at all until somebody has confirmed a pass"

echo "  -- $GREPBIN: $pass passed, $fail failed"
[ "$fail" -eq 0 ] || exit 1
