#!/bin/bash
# ==========================================================================
# check_1158_no_ad7609_rate_override.sh -- issue #1158
#
# SCPI_StartStreamingClaimed() (firmware/src/services/SCPI/SCPIInterface.c)
# used to force the streaming rate to 1 Hz whenever an AD7609 channel was
# enabled with no type-1 MC12b channel:
#
#     if (hasActiveAD7609Channels && activeType1ChannelCount == 0 && freq > 1) {
#         freq = 1;
#     }
#
# On NQ3 that is every analog stream, and the same timer triggers the AD7609
# conversions, so NQ3's acquisition path ran at 1 Hz whatever the client
# asked for -- silently: START answered no error. The fix deletes the
# override; the requested rate is bounded only by the cap computation.
#
# No NQ3 board is on the bench, and SCPIInterface.c is not host-compilable,
# so this guards the source: nothing inside SCPI_StartStreamingClaimed's body
# may reference the AD7609 presence flag the override keyed on
# (hasActiveAD7609Channels, or a has*7609 out-parameter). START has no other
# use for that flag, so a reference IS the override coming back.
#
# What it deliberately does NOT do: forbid literal assignments to freq. That
# was tried and rejected -- the benchmark path's `if (freq > 100000)
# freq = 100000;` sanity ceiling is the same shape as the override and is
# correct, so the rule broke on a correct change. A re-introduced override
# keyed on some OTHER predicate is therefore not caught here; the
# python-test-suite companion's NQ3 rate assertion is what catches it.
#
# Usage:  check_1158_no_ad7609_rate_override.sh <SCPIInterface.c>
#         check_1158_no_ad7609_rate_override.sh --self-test
#   --self-test runs the check against a synthetic body WITH the override and
#   one without, and fails unless it rejects the first and accepts the second
#   -- a guard that cannot fail is not a guard.
# ==========================================================================
set -u

body_of() {
    # Signature opens at column 0 and spans several lines; body ends at the
    # first column-0 closing brace.
    sed -n '/^static scpi_result_t SCPI_StartStreamingClaimed(scpi_t \* context,/,/^}/p' "$1" | tr -d '\r'
}

check_body() {
    local body="$1" bad=0
    if [ -z "$body" ]; then
        echo "ERROR: SCPI_StartStreamingClaimed(scpi_t * context, ...) not found at"
        echo "       column 0 -- a check over nothing must fail, not pass."
        return 1
    fi
    local refs
    refs=$(printf '%s\n' "$body" | grep -nE 'hasActiveAD7609|&has[A-Za-z0-9_]*7609')
    if [ -n "$refs" ]; then
        echo "ERROR: SCPI_StartStreamingClaimed references the AD7609 presence flag"
        echo "       the #1158 1 Hz override keyed on:"
        printf '         %s\n' "$refs"
        bad=1
    fi
    return $bad
}

self_test() {
    local with without
    with=$'static scpi_result_t SCPI_StartStreamingClaimed(scpi_t * context,\n        int32_t freq) {\n    bool hasActiveAD7609Channels = false;\n    Streaming_CountActiveChannels(&a, &b, &hasActiveAD7609Channels);\n    if (hasActiveAD7609Channels && activeType1ChannelCount == 0 && freq > 1) {\n        freq = 1;\n    }\n}'
    without=$'static scpi_result_t SCPI_StartStreamingClaimed(scpi_t * context,\n        int32_t freq) {\n    Streaming_CountActiveChannels(&a, &b, NULL);\n    freq = (int32_t)stored;\n    if (freq > 100000) freq = 100000;  // Sanity cap at 100kHz\n    if (freq > (int32_t)maxFreq) { return SCPI_RES_ERR; }\n}'
    local rc=0
    if check_body "$with" >/dev/null; then echo "[FAIL] accepts the original override"; rc=1; else echo "[PASS] rejects the original override"; fi
    if check_body "$without" >/dev/null; then echo "[PASS] accepts the fixed body, incl. the 100 kHz sanity ceiling"; else echo "[FAIL] rejects the fixed body"; rc=1; fi
    if check_body "" >/dev/null; then echo "[FAIL] passes on an empty body"; rc=1; else echo "[PASS] fails on an empty body"; fi
    return $rc
}

if [ "${1:-}" = "--self-test" ]; then
    self_test; exit $?
fi
[ $# -eq 1 ] || { echo "usage: $0 <SCPIInterface.c> | --self-test"; exit 2; }
check_body "$(body_of "$1")" || exit 1
echo "OK: #1158 -- SCPI_StartStreamingClaimed carries no AD7609 rate override"
