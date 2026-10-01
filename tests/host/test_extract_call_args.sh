#!/bin/bash
# ==========================================================================
# test_extract_call_args.sh
#
# Self-test for extract_call_args.pl, which is the ENGINE of two guards:
# tests/host/Makefile's CreateSCPIContext() caller checks and
# check_999_createscpicontext_wiring.sh's SCPI_Init() wiring check. Both
# ask it "what are this call's own arguments?" and then require EQUALITY of
# one whole argument to an expected token. If the parser silently returned
# the wrong argument list -- or the whole call text as one argument -- those
# guards would still run, still print green, and establish nothing. A
# parser that decides whether a guard can fail needs its own test.
#
# Run standalone or via `make run` (the SCPI999_BIN recipe invokes it before
# building, so a broken parser fails the build rather than passing it).
# ==========================================================================
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")" && pwd)"
EXTRACTOR="$SCRIPT_DIR/extract_call_args.pl"
PASS=0
FAIL=0

# check <name> <fn> <input> <expected-exit> <expected-output...>
check() {
    local name="$1" fn="$2" input="$3" want_rc="$4"
    shift 4
    local want
    want=$(printf '%s\n' "$@")
    [ "$#" -eq 0 ] && want=""
    local got rc
    got=$(printf '%s' "$input" | perl "$EXTRACTOR" "$fn")
    rc=$?
    if [ "$rc" -eq "$want_rc" ] && [ "$got" = "$want" ]; then
        echo "[ OK ] $name"
        PASS=$((PASS + 1))
    else
        echo "[FAIL] $name"
        echo "         input:    $input"
        echo "         want rc=$want_rc out=[$(printf '%s' "$want" | tr '\n' '|')]"
        echo "         got  rc=$rc out=[$(printf '%s' "$got" | tr '\n' '|')]"
        FAIL=$((FAIL + 1))
    fi
}

# ---- the shapes the two guards depend on being parsed correctly ---------
check "plain arguments split at top level" \
      F 'F(a, b, c);' 0 a b c

check "nested call is ONE argument, not three" \
      F 'F(a, g(b, c), d);' 0 a 'g(b,c)' d

check "subscript is ONE argument (bracket depth counts)" \
      F 'F(a, d[e, f], c);' 0 a 'd[e,f]' c

# The bypass this parser was written to close: a ternary's own text can
# never EQUAL either of its branches, so a containment check passes where
# an equality check must not.
check "ternary stays whole, so it cannot equal its own branch" \
      F 'F(a, cond ? &shared : &own, c);' 0 a 'cond?&shared:&own' c

check "internal whitespace collapses (survives flattening)" \
      F 'F(  a , & own  , c );' 0 a '&own' c

check "whitespace between name and paren is allowed" \
      F 'F (a, b);' 0 a b

check "first call wins when several are present" \
      F 'F(a, b); F(c, d);' 0 a b

# ---- fail-CLOSED cases: callers treat empty output as a failure ---------
# Each of these must exit non-zero AND print nothing. Printing a partial
# argument list with exit 0 is the dangerous shape -- a caller would read
# it as a complete answer.
check "a name that is only a SUFFIX of an identifier is not this call" \
      F 'XF(a, b);' 1

check "absent function refuses rather than returning nothing-as-success" \
      NoSuchFn 'F(a, b);' 1

check "unbalanced parens refuse (could-not-verify is not clean)" \
      F 'F(a, b;' 1

# The scan starts AT the call's own '(' (depth 1) and stops the moment it
# returns to 0, so trailing text after the call -- including a stray closer
# belonging to an enclosing expression -- is simply never read.
check "text after the call's closing paren is not read" \
      F 'F(a, b));' 0 a b

check "the call inside an enclosing expression is still extracted whole" \
      F 'if (G(F(a, b))) {' 0 a b

# ---- documented limitation, pinned so it cannot regress silently -------
# The parser is NOT string/char-literal aware: it counts parens and
# brackets inside "..." as if they were code. Neither of the two real call
# sites passes a string literal today, and the failure direction is
# fail-CLOSED for both consumers -- a literal paren truncates the argument
# list, so the expected whole argument goes missing and the guard turns
# RED. These cases pin that CURRENT behaviour rather than endorse it: if
# someone later adds a string argument to either call, the guard will fail
# loudly and this test says why. See extract_call_args.pl's header.
check "LIMITATION: a ')' inside a string literal truncates the list" \
      F 'F(a, ")", c);' 0 a '"'

check "LIMITATION: a ',' inside a string literal splits the argument" \
      F 'F(a, "x,y", c);' 0 a '"x' 'y"' c

echo
echo "---------------------------------------------"
echo "$((PASS + FAIL)) checks run, $FAIL failed"
[ "$FAIL" -eq 0 ] || exit 1
exit 0
