#!/usr/bin/env bash
# #1235 -- mutation harness for the resync's token scan.
#
# test_1217_abandoned_write.c claims that every byte the SD driver receives
# while a filler acceptance is unresolved is scanned for the token BEFORE any
# error branch can discard it, and (r5) that no resync receive buffer is reused
# before what the previous transfer left in it was scanned. A test that cannot
# fail proves nothing, so this builds the real driver with one site's scan (or
# one site's guard) removed -- a COPY of the source in a scratch directory
# placed first on the include path: the worktree's sources are never touched --
# and requires the NAMED test to fail. A mutant whose expected set names PIN
# must also be rejected by check_1235_rx_buffer_writes.py, the static pin that
# finds every write into the buffer and requires the guarded helper. The
# unmutated build must pass both, or the run is meaningless.
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

# build <label> <dir holding the (possibly mutated) sources>  ->  $T/<label>.out
build_and_run() {
    local label="$1" dir="$2"
    ( cd "$HERE" && cc -std=c11 -w -O0 -g -Istubs/sdspi_1217 -I../../firmware/src/config/default \
        -I"$dir" -I"$SRC" -c test_1217_driver_uut.c -o "$T/$label.uut.o" ) || return 2
    ( cd "$HERE" && cc -std=c11 -Wall -Wextra -O0 -g -Werror=unused-function -Istubs/sdspi_1217 \
        -I../../firmware/src/config/default -o "$T/$label.bin" test_1217_abandoned_write.c "$T/$label.uut.o" ) || return 2
    "$T/$label.bin" > "$T/$label.out" 2>&1
    return 0
}

# The file a mutant edits (drv_sdspi.c unless a mutant says otherwise).
MUTFILE=drv_sdspi.c

mutate() {      # mutate <old> <new> -> writes $T/mut/$MUTFILE
    python3 - "$SRC/$MUTFILE" "$T/mut/$MUTFILE" "$1" "$2" <<'PY'
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

# The static pin must accept the real source.
python3 "$HERE/check_1235_rx_buffer_writes.py" > "$T/pin.base" 2>&1 \
    || { cat "$T/pin.base"; refuse 1 "the UNMUTATED driver fails the static pin"; }

# Baseline: the real driver passes.
mkdir -p "$T/base"
cp "$SRC/drv_sdspi.c" "$T/base/drv_sdspi.c"
build_and_run base "$T/base" || refuse 2 "baseline build failed"
if ! grep -q " 0 failed" "$T/base.out"; then
    cat "$T/base.out" | tail -20
    refuse 1 "the UNMUTATED driver fails its own tests"
fi
echo "baseline: $(tail -1 "$T/base.out")"

# run_mutant <name> <expected: failing test names and/or PIN> <old> <new>
run_mutant() {
    local name="$1" expect="$2" old="$3" new="$4"
    rm -rf "$T/mut"
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
    local missed="" t
    for t in $expect; do
        if [ "$t" = "PIN" ]; then
            if [ "$MUTFILE" = "drv_sdspi.c" ]; then
                python3 "$HERE/check_1235_rx_buffer_writes.py" --source "$T/mut/drv_sdspi.c" >/dev/null 2>&1 \
                    && missed="$missed PIN"
            else
                python3 "$HERE/check_1235_rx_buffer_writes.py" --iface "$T/mut/$MUTFILE" >/dev/null 2>&1 \
                    && missed="$missed PIN"
            fi
        else
            grep -q "^\[FAIL\] $t\$" "$T/m_$name.out" || missed="$missed $t"
        fi
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


# run_equivalent_mutant <name> <old> <new>: a mutant PROVED behaviourally
# equivalent (see its comment). It must still build and pass every test; it is
# reported separately and never counted as caught.
equivalent=0
run_equivalent_mutant() {
    local name="$1" old="$2" new="$3"
    rm -rf "$T/mut"
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
    if grep -q " 0 failed" "$T/m_$name.out"; then
        echo "MUTANT $name: EQUIVALENT (all tests still pass, as argued)"
        equivalent=$((equivalent + 1))
    else
        echo "MUTANT $name: expected EQUIVALENT but a test failed (the argument is wrong):"
        grep -E "^\[FAIL\]" "$T/m_$name.out"
        failures=$((failures + 1))
    fi
}

# ---- what each transfer's scan covers (armed by lDRV_SDSPI_WriteResyncRxReset) ----

# M1: the flush chunk is not scanned (ok and error paths share the scan).
run_mutant flush_scan resync_block_accepted_after_a_partial_capture_names_the_sector \
"lDRV_SDSPI_WriteResyncRxReset(dObj, chunk, chunk, false);" \
"lDRV_SDSPI_WriteResyncRxReset(dObj, chunk, 0U, false);"

# M2: the first busy read after the flush is not scanned.
run_mutant after_block_scan resync_block_accepted_token_after_the_flush_is_reported \
"    if (dObj->resyncStage == DRV_SDSPI_RESYNC_STAGE_AFTER_BLOCK)
    {
        scanLen = 1U;" \
"    if (false)
    {
        scanLen = 1U;"

# M3: the busy reads before the flush are not scanned (the first resync's token,
# and a token an earlier resync left unread).
run_mutant before_block_scan "resync_block_accepted_token_read_in_the_next_requests_resync resync_block_accepted_token_in_the_first_resyncs_busy_read_is_reported" \
"            (dObj->writeInDoubt == DRV_SDSPI_WRITE_IN_DOUBT_SINGLE) && (dObj->writeInDoubtCrc == true))
    {
        scanLen = 1U;" \
"            (false))
    {
        scanLen = 1U;"

# M4: the scan runs only AFTER the error branch has ended the resync, as in the
# round-2 code (a failed transfer's bytes are never looked at).
run_mutant scan_after_error_guard "resync_block_accepted_token_in_a_flush_read_that_then_fails resync_block_accepted_token_in_an_after_block_read_that_then_fails resync_block_accepted_token_in_a_before_block_read_that_then_fails" \
"        lDRV_SDSPI_WriteResyncScanReceived(dObj, false);
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
        lDRV_SDSPI_WriteResyncScanReceived(dObj, false);"

# r4 -- the transfers that CLOCK the card without reading from it. SPI is full
# duplex: a write-only transfer discards what the card answers, so each write
# site is either full duplex with its received byte scanned, or proven unable to
# carry an acceptance (the CMD25 sites: see the table in the PR).

# M5: the 0xFE start token's received byte is not scanned.
run_mutant start_token_scan "resync_block_accepted_token_clocked_by_the_next_resyncs_0xfe_write_261 resync_block_accepted_token_clocked_by_the_next_resyncs_0xfe_write_256 resync_block_accepted_token_in_a_0xfe_write_that_then_fails" \
"lDRV_SDSPI_WriteResyncRxReset(dObj, 1U, 1U, true);" \
"lDRV_SDSPI_WriteResyncRxReset(dObj, 1U, 0U, true);"

# M6: the 0xFE start token is sent write-only again (its received byte is
# discarded by the SPI driver).
run_mutant start_token_write_only "resync_block_accepted_token_clocked_by_the_next_resyncs_0xfe_write_261 resync_block_accepted_token_clocked_by_the_next_resyncs_0xfe_write_256" \
"DRV_SDSPI_SPIWriteRead(dObj, &dObj->pCmdResp[1], dObj->pCmdResp, 1)" \
"DRV_SDSPI_SPIWrite(dObj, &dObj->pCmdResp[1], 1)"

# M7: the byte the 0xFE transfer received is accepted whatever its shape (a
# busy 0x00 or an idle byte is not a data-response token).
run_mutant start_token_shape resync_0xfe_write_clocking_a_byte_that_is_not_a_token_reports_nothing \
"lDRV_SDSPI_WriteResyncRxReset(dObj, 1U, 1U, true);" \
"lDRV_SDSPI_WriteResyncRxReset(dObj, 1U, 1U, false);"

# ---- r5: the buffer-reuse invariant ----

# M8: the guarded helper does not scan what the previous transfer left before it
# clears (inherited scan removed): an aborted or late transfer's bytes are lost.
run_mutant inherited_scan "resync_token_delivered_by_a_read_whose_completion_is_lost_then_the_bus_is_released_is_reported resync_token_delivered_by_a_late_dma_after_the_watchdog_is_reported PIN" \
"    lDRV_SDSPI_WriteResyncScanReceived(dObj, true);
    if (rxBytes > 0U)" \
"    if (rxBytes > 0U)"

# M9: the helper clears FIRST and scans after (the order is the invariant).
run_mutant clear_before_scan "resync_token_delivered_by_a_read_whose_completion_is_lost_then_the_bus_is_released_is_reported resync_token_delivered_by_a_late_dma_after_the_watchdog_is_reported PIN" \
"    lDRV_SDSPI_WriteResyncScanReceived(dObj, true);
    if (rxBytes > 0U)
    {
        (void) memset(dObj->pCmdResp, 0xFF, rxBytes);
    }" \
"    if (rxBytes > 0U)
    {
        (void) memset(dObj->pCmdResp, 0xFF, rxBytes);
    }
    lDRV_SDSPI_WriteResyncScanReceived(dObj, true);"

# M10: a scan of an ABORTED transfer's buffer retires it (the late flag is
# ignored): bytes that land after the watchdog's scan are never looked at.
run_mutant late_retired resync_token_delivered_by_a_late_dma_after_the_watchdog_is_reported \
"    if ((inherited == true) || (dObj->resyncRxLate == false))" \
"    if (true)"

# M11: the abort is not recorded as late (DRV_SDSPI_AbortedXferRecord).
MUTFILE=drv_sdspi_driver_interface.c
run_mutant abort_not_late "resync_token_delivered_by_a_late_dma_after_the_watchdog_is_reported PIN" \
"        dObj->resyncRxLate = true;" \
"        dObj->resyncRxLate = false;"
MUTFILE=drv_sdspi.c

# M12: a refused submission leaves the buffer marked unscanned (a later look
# would read whatever else put a byte there).
run_mutant refused_stays_armed resync_before_block_read_failing_with_nothing_delivered_reports_no_stale_byte \
"    dObj->resyncRxUnscanned = false;
    lDRV_SDSPI_WriteResyncEnd(dObj, false);" \
"    lDRV_SDSPI_WriteResyncEnd(dObj, false);"

# ---- r5: bypass the helper at EACH site (the static pin finds every write) ----

# M13: the 1-byte busy reads reset the buffer without the helper.
run_mutant bypass_busy_read "resync_token_delivered_by_a_read_whose_completion_is_lost_then_the_bus_is_released_is_reported PIN" \
"    lDRV_SDSPI_WriteResyncRxReset(dObj, 1U, scanLen, shaped);" \
"    (void) memset(dObj->pCmdResp, 0xFF, 1U);
    dObj->resyncRxLen = (uint8_t)scanLen;
    dObj->resyncRxShaped = shaped;
    dObj->resyncRxLate = false;
    dObj->resyncRxUnscanned = (scanLen > 0U);"

# M14: the flush chunk resets the buffer without the helper.
run_mutant bypass_flush PIN \
"            lDRV_SDSPI_WriteResyncRxReset(dObj, chunk, chunk, false);" \
"            (void) memset(dObj->pCmdResp, 0xFF, chunk);
            dObj->resyncRxLen = (uint8_t)chunk;
            dObj->resyncRxShaped = false;
            dObj->resyncRxLate = false;
            dObj->resyncRxUnscanned = true;"

# M15: the 0xFE start token's receive byte is armed without the helper.
run_mutant bypass_start_token PIN \
"                lDRV_SDSPI_WriteResyncRxReset(dObj, 1U, 1U, true);" \
"                (void) memset(dObj->pCmdResp, 0xFF, 1U);
                dObj->resyncRxLen = 1U;
                dObj->resyncRxShaped = true;
                dObj->resyncRxLate = false;
                dObj->resyncRxUnscanned = true;"

# M16: the 0xFD stop token is staged without the helper (CMD25 only).
run_mutant bypass_stop_token PIN \
"                lDRV_SDSPI_WriteResyncRxReset(dObj, 0U, 0U, false);
                dObj->pCmdResp[0] = DRV_SDSPI_DATA_STOP_TRAN_TOKEN;" \
"                dObj->pCmdResp[0] = DRV_SDSPI_DATA_STOP_TRAN_TOKEN;"

# M17: the CMD12 frame is staged without the helper (CMD25 only).
run_mutant bypass_cmd12 PIN \
"                lDRV_SDSPI_WriteResyncRxReset(dObj, 0U, 0U, false);
                dObj->pCmdResp[0] = ((uint8_t)CMD_VALUE_STOP_TRANSMISSION | DRV_SDSPI_TRANSMIT_SET);" \
"                dObj->pCmdResp[0] = ((uint8_t)CMD_VALUE_STOP_TRANSMISSION | DRV_SDSPI_TRANSMIT_SET);"

# ---- r5: attribution ----

# M18: the original write's own response is counted as a filler's.
run_mutant attribution_ignored original_writes_own_response_token_is_not_reported_as_filler \
"(dObj->writeInDoubtCrc == true) && (dObj->writeInDoubtBlockSent == false) &&" \
"(dObj->writeInDoubtCrc == true) &&"

# M19: the whole block is never recorded as clocked.
run_mutant block_sent_never_set original_writes_own_response_token_is_not_reported_as_filler \
"            dObj->writeInDoubtBlockSent = true;" \
"            dObj->writeInDoubtBlockSent = false;"

# ---- r6: origin ambiguous (payload fully clocked, CRC unconfirmed) ----

# M20: the "payload fully clocked" check is dropped: such a token is counted as filler.
run_mutant payload_clocked_check_dropped original_payload_with_a_lost_crc_completion_is_not_filler \
"        if ((dObj->writeInDoubtPayloadClocked == true) ||" \
"        if ((false) ||"

# M21: the payload's completion is never recorded.
run_mutant payload_clocked_never_set original_payload_with_a_lost_crc_completion_is_not_filler \
"            dObj->writeInDoubtPayloadClocked = true;" \
"            dObj->writeInDoubtPayloadClocked = false;"


# ---- r7: payload completion unobserved: the CRC16 decides ----

# M22: the "CRC16 is 0xFFFF" condition is dropped: the lost-completion edge is counted as filler.
run_mutant crc_ffff_check_dropped edge_lost_payload_completion_with_a_0xffff_crc_is_recorded \
"        if ((dObj->writeInDoubtPayloadClocked == true) ||
                (dObj->writeBlockCrc16 == 0xFFFFU))" \
"        if (dObj->writeInDoubtPayloadClocked == true)"

# M23: the clear at CMD24 admission is dropped: a write that fails before its
# payload step inherits the previous block's 0xFFFF.
run_mutant admission_clear_dropped a_write_that_fails_before_its_payload_does_not_inherit_the_previous_0xffff_crc \
"                dObj->writeBlockCrc16 = DRV_SDSPI_CRC16_UNSET;
                dObj->resyncRxUnscanned = false;" \
"                dObj->resyncRxUnscanned = false;"

# M24: the clear before the payload is armed is dropped. See the PR body: the
# admission clear (M23) also covers a refused payload submission, so this mutant
# is EQUIVALENT and is expected to pass every test (run_equivalent_mutant).
run_equivalent_mutant payload_clear_dropped \
"            dObj->writeBlockCrc16 = DRV_SDSPI_CRC16_UNSET;
            if (DRV_SDSPI_SPIWrite(dObj, currentBufObj->buffer, 512) == true)" \
"            if (DRV_SDSPI_SPIWrite(dObj, currentBufObj->buffer, 512) == true)"


echo "RESULT: $passed passed, $failures failed ($equivalent proved equivalent)"
[ "$failures" -eq 0 ]
