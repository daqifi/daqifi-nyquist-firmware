#!/usr/bin/env bash
# #1235 -- mutation harness for the resync's token scan.
#
# test_1217_abandoned_write.c claims that every byte the SD driver receives
# while a filler acceptance is unresolved is scanned for the token BEFORE any
# error branch can discard it. A test that cannot fail proves nothing, so this
# builds the real driver with one site's scan removed (a COPY of drv_sdspi.c in
# a scratch directory placed first on the include path: the worktree's sources
# are never touched) and requires the NAMED test to fail. The unmutated build
# must pass, or the run is meaningless.
#
# Usage:  bash tests/host/pin1235_mutate.sh      (from anywhere)
set -u

refuse() {      # refuse <exit-code> <reason>
    echo "RESULT: 0 passed, 0 failed (REFUSED: $2)"
    exit "$1"
}

HERE="$(cd "$(dirname "$0")" && pwd)"
SRC="$HERE/../../firmware/src/config/default/driver/sdspi/src"
[ -f "$SRC/drv_sdspi.c" ] || refuse 2 "driver source missing"
T="$(mktemp -d)" || refuse 2 "mktemp failed"
trap 'rm -rf "$T"' EXIT

# build <label> <dir holding drv_sdspi.c>  ->  $T/<label>.out
build_and_run() {
    local label="$1" dir="$2"
    ( cd "$HERE" && cc -std=c11 -w -O0 -g -Istubs/sdspi_1217 -I../../firmware/src/config/default \
        -I"$dir" -I"$SRC" -c test_1217_driver_uut.c -o "$T/$label.uut.o" ) || return 2
    ( cd "$HERE" && cc -std=c11 -Wall -Wextra -O0 -g -Werror=unused-function -Istubs/sdspi_1217 \
        -I../../firmware/src/config/default -o "$T/$label.bin" test_1217_abandoned_write.c "$T/$label.uut.o" ) || return 2
    "$T/$label.bin" > "$T/$label.out" 2>&1
    return 0
}

mutate() {      # mutate <python-expr-old> <new> -> writes $T/mut/drv_sdspi.c
    python3 - "$SRC/drv_sdspi.c" "$T/mut/drv_sdspi.c" "$1" "$2" <<'PY'
import sys
src, dst, old, new = sys.argv[1:5]
s = open(src).read()
if s.count(old) != 1:
    sys.exit("mutant anchor matched %d times: %r" % (s.count(old), old[:70]))
open(dst, "w").write(s.replace(old, new))
PY
}

failures=0
passed=0

# Baseline: the real driver passes.
mkdir -p "$T/base"
cp "$SRC/drv_sdspi.c" "$T/base/drv_sdspi.c"
build_and_run base "$T/base" || refuse 2 "baseline build failed"
if ! grep -q " 0 failed" "$T/base.out"; then
    cat "$T/base.out" | tail -20
    refuse 1 "the UNMUTATED driver fails its own tests"
fi
echo "baseline: $(tail -1 "$T/base.out")"

# run_mutant <name> <expected failing test(s), space-separated> <old> <new>
run_mutant() {
    local name="$1" expect="$2" old="$3" new="$4"
    mkdir -p "$T/mut"
    if ! mutate "$old" "$new"; then
        echo "MUTANT $name: ANCHOR NOT FOUND -- the harness is stale"
        failures=$((failures + 1))
        return
    fi
    if ! build_and_run "m_$name" "$T/mut"; then
        echo "MUTANT $name: did not build"
        failures=$((failures + 1))
        return
    fi
    local missed=""
    for t in $expect; do
        grep -q "^\[FAIL\] $t\$" "$T/m_$name.out" || missed="$missed $t"
    done
    if [ -z "$missed" ]; then
        echo "MUTANT $name: caught by $expect"
        passed=$((passed + 1))
    else
        echo "MUTANT $name: SURVIVED (did not fail:$missed)"
        grep -E "^\[FAIL\]|tests run" "$T/m_$name.out"
        failures=$((failures + 1))
    fi
}

# M1: the flush chunk is not scanned (ok and error paths share the scan).
run_mutant flush_scan resync_block_accepted_after_a_partial_capture_names_the_sector \
"    if (dObj->cmdState == DRV_SDSPI_CMD_RESYNC_FLUSH)
    {
        /* resyncFlushLeft already excludes" \
"    if (false)
    {
        /* resyncFlushLeft already excludes"

# M2: the first busy reads after the flush are not scanned.
run_mutant after_block_scan resync_block_accepted_token_after_the_flush_is_reported \
"        if (dObj->resyncStage == DRV_SDSPI_RESYNC_STAGE_AFTER_BLOCK)
        {
            len = 1U;" \
"        if (false)
        {
            len = 1U;"

# M3: the pending acceptance is not looked for in the next resync's first reads.
run_mutant before_block_scan resync_block_accepted_token_read_in_the_next_requests_resync \
"                (dObj->resyncFillerPending == true))
        {
            len = 1U;" \
"                (false))
        {
            len = 1U;"

# M4: the scan runs only AFTER the error branch has ended the resync, as in
# the round-2 code (a failed transfer's bytes are never looked at).
run_mutant scan_after_error_guard "resync_block_accepted_token_in_a_flush_read_that_then_fails resync_block_accepted_token_in_an_after_block_read_that_then_fails resync_block_accepted_token_in_a_before_block_read_that_then_fails" \
"        lDRV_SDSPI_WriteResyncScanReceived(dObj);
        if (dObj->spiTransferStatus == DRV_SDSPI_SPI_TRANSFER_STATUS_ERROR)
        {
            lDRV_SDSPI_WriteResyncEnd(dObj, false);
            return;
        }" \
"        if (dObj->spiTransferStatus == DRV_SDSPI_SPI_TRANSFER_STATUS_ERROR)
        {
            lDRV_SDSPI_WriteResyncEnd(dObj, false);
            return;
        }
        lDRV_SDSPI_WriteResyncScanReceived(dObj);"

# M5: a resync that ends with the filler programmed and its token unread does
# not leave the acceptance pending.
run_mutant pending_not_set resync_block_accepted_token_read_in_the_next_requests_resync \
"        dObj->resyncFillerPending = true;
    }" \
"        dObj->resyncFillerPending = false;
    }"

# M6: the flush's receive buffer is not cleared before the read.
run_mutant flush_prefill resync_flush_read_failing_part_way_reports_no_stale_byte \
"            (void) memset(dObj->pCmdResp, 0xFF, chunk); /* see WriteResyncBusyRead */" \
"            /* mutant: no prefill */"

# M7: the first busy read is not cleared while an acceptance is pending.
run_mutant busy_prefill resync_before_block_read_failing_with_nothing_delivered_reports_no_stale_byte \
"    if ((dObj->resyncStage == DRV_SDSPI_RESYNC_STAGE_BEFORE_BLOCK) && (dObj->resyncFillerPending == true))
    {
        dObj->pCmdResp[0] = 0xFFU;
    }" \
"    /* mutant: no prefill */"

# M8: DRV_SDSPI_ReleaseBus ends a resync without scanning the completed chunk.
run_mutant releasebus_scan resync_token_in_a_completed_chunk_when_the_bus_is_released_is_reported \
"        if (dObj->spiTransferStatus != DRV_SDSPI_SPI_TRANSFER_STATUS_IN_PROGRESS)
        {
            lDRV_SDSPI_WriteResyncScanReceived(dObj);
        }
        lDRV_SDSPI_WriteResyncNotePending(dObj);" \
"        lDRV_SDSPI_WriteResyncNotePending(dObj);"

# M9: DRV_SDSPI_ReleaseBus does not leave an unread acceptance pending.
run_mutant releasebus_pending resync_token_still_to_come_when_the_bus_is_released_is_reported \
"            lDRV_SDSPI_WriteResyncScanReceived(dObj);
        }
        lDRV_SDSPI_WriteResyncNotePending(dObj);" \
"            lDRV_SDSPI_WriteResyncScanReceived(dObj);
        }"

echo "RESULT: $passed passed, $failures failed"
[ "$failures" -eq 0 ]
