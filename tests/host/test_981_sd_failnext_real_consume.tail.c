/* ---- END verbatim extraction ---- */

/* One call to the real SDCardWrite(), reporting BOTH observables: what it
 * returned, and how many calls actually reached the mock SYS_FS_FileWrite().
 * Every case below checks both, because either one alone is a proxy -- see
 * the head file's "WHAT IS ASSERTED" paragraph for why the round-2 revision,
 * which checked neither, could not fail on a real regression. */
static int RunWrite(unsigned *writesOut) {
    unsigned before = gMockFsWriteCalls;
    int ret = SDCardWrite();
    *writesOut = gMockFsWriteCalls - before;
    return ret;
}

int main(void) {
    unsigned writes;
    int ret;

    /* Preconditions shared by every case. The valid fileHandle is
     * load-bearing: SDCardWrite()'s first statement returns -1 with zero
     * writes when the handle is SYS_FS_HANDLE_INVALID -- the SAME signature
     * the hook produces. Keeping the handle valid throughout is what makes
     * "-1, zero writes" attributable to the hook and nothing else. */
    gSDCardData.fileHandle = (SYS_FS_HANDLE)1;
    gSDCardData.writeBuffer = gMockWriteBuffer;
    gSDCardData.sdCardWriteBufferOffset = 0u;
    gSDCardData.writeBufferLength = 64u;

    /* CASE 0 -- CONTROL. Unarmed, ordinary WRITE_TO_FILE: the real write must
     * be REACHED and its byte count returned. This case exists so the
     * "zero writes" assertions in the firing cases cannot pass vacuously: if
     * the mock were unreachable for any reason (the extraction lost the write
     * call, the harness mis-set a precondition), every firing case would
     * still "pass" and this one would fail instead. */
    gFailNextWrite = false;
    gSDCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_WRITE_TO_FILE;
    ret = RunWrite(&writes);
    if (ret != (int)MOCK_FS_WRITE_RETURN || writes != 1u) {
        fprintf(stderr,
            "FAIL (control): an ordinary unarmed write did not reach the real "
            "SYS_FS_FileWrite() and return its byte count (ret=%d expected=%d, "
            "writes=%u expected=1). Every 'the hook skipped the write' "
            "assertion below is vacuous until this passes -- fix this first; "
            "it is a harness/extraction fault, not a hook fault.\n",
            ret, (int)MOCK_FS_WRITE_RETURN, writes);
        return 1;
    }

    /* CASE 1 -- armed AND in UNMOUNT_DISK, the only state the real gate
     * admits. The hook must inject the failure AND skip the write entirely.
     *
     * `writes == 0` is the assertion this whole revision exists for. A
     * skeptic deleted ONLY the `goto __exit;` from the real payload block:
     * the hook still cleared the arm, still logged "consumed", still set
     * writeLen = -1 -- and then fell through into the real write, which
     * succeeded and overwrote writeLen with a byte count. The injected fault
     * was dead on hardware while the host suite stayed green, because the
     * previous revision extracted none of this and asserted on a local bool.
     * Now that mutation lands here: the return is the mock's byte count and
     * the counter moved. */
    gFailNextWrite = true;
    gSDCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_UNMOUNT_DISK;
    ret = RunWrite(&writes);
    if (ret != -1) {
        fprintf(stderr,
            "FAIL: an armed write in UNMOUNT_DISK returned %d, not the -1 the "
            "hook is supposed to inject (%u call(s) reached the real "
            "SYS_FS_FileWrite()). %s Either way SYST:STOR:SD:FAILNext is a "
            "no-op on hardware: the arm is consumed and the write succeeds "
            "anyway.\n",
            ret, writes,
            writes != 0u
                ? "The write was REACHED and its return is what you are "
                  "seeing. Two mutations produce exactly this and the host "
                  "cannot tell them apart from outside: the payload block was "
                  "never entered (`injectWriteFailure = true;` flipped to "
                  "`= false;` -- the arm is burned, no failure injected), or "
                  "it was entered and control fell out of it instead of "
                  "leaving (its `goto __exit;` is gone). Check both."
                : "The write was NOT reached, so the payload block did run "
                  "and leave -- but it no longer hands back -1; check the "
                  "`writeLen = -1;` inside it.");
        return 2;
    }
    if (writes != 0u) {
        fprintf(stderr,
            "FAIL: an armed write in UNMOUNT_DISK returned -1 but still "
            "reached the real SYS_FS_FileWrite() %u time(s). The hook must "
            "skip the write, not perform it and relabel the result -- bytes "
            "the test believes were abandoned were actually committed to the "
            "card, so the #825/#838/#915/#979 accounting the hook exists to "
            "exercise is being tested against a write that did happen. Check "
            "that the payload block still ends in `goto __exit;`.\n",
            writes);
        return 3;
    }
    if (gFailNextWrite) {
        fprintf(stderr,
            "FAIL: gFailNextWrite is still armed after a consume in "
            "UNMOUNT_DISK -- the one-shot is broken, so a device could be "
            "left failing every future qualifying SD write instead of "
            "exactly one.\n");
        return 4;
    }

    /* CASE 2 -- one-shot. A second call immediately after, still in
     * UNMOUNT_DISK, must NOT fire: the write runs normally. */
    ret = RunWrite(&writes);
    if (ret != (int)MOCK_FS_WRITE_RETURN || writes != 1u) {
        fprintf(stderr,
            "FAIL: a second write in UNMOUNT_DISK right after a consume was "
            "injected again (ret=%d expected=%d, writes=%u expected=1) -- the "
            "arm is not one-shot.\n",
            ret, (int)MOCK_FS_WRITE_RETURN, writes);
        return 5;
    }

    /* CASE 3 -- never armed, still in UNMOUNT_DISK: must not fire. Also
     * covers "armed, then explicitly disarmed before any qualifying write". */
    gFailNextWrite = false;
    ret = RunWrite(&writes);
    if (ret != (int)MOCK_FS_WRITE_RETURN || writes != 1u) {
        fprintf(stderr,
            "FAIL: an unarmed write in UNMOUNT_DISK was injected anyway "
            "(ret=%d expected=%d, writes=%u expected=1) -- every teardown-"
            "drain write would fail, not just an explicitly armed one.\n",
            ret, (int)MOCK_FS_WRITE_RETURN, writes);
        return 6;
    }

    /* CASE 4 -- THE GATE ITSELF: armed, but the manager is NOT tearing down.
     * WRITE_TO_FILE stands in for "any non-UNMOUNT_DISK state" (the real
     * condition is an equality test against exactly one enumerator, so any
     * other value exercises the same branch). This must NOT fire -- the write
     * must go through untouched -- or the hook could truncate the operator's
     * log via the pre-existing ERROR -> UNMOUNT_DISK -> INIT ->
     * OPEN_FILE(WRITE_PLUS) remount, the HIGH-severity finding this gate
     * exists to close. And the arm must SURVIVE: an arm that misses its only
     * safe window is retried, never silently burned. */
    gFailNextWrite = true;
    gSDCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_WRITE_TO_FILE;
    ret = RunWrite(&writes);
    if (ret != (int)MOCK_FS_WRITE_RETURN || writes != 1u) {
        fprintf(stderr,
            "FAIL: an armed write OUTSIDE UNMOUNT_DISK was injected "
            "(ret=%d expected=%d, writes=%u expected=1) -- the "
            "session-integrity gate is gone or was weakened. This is the "
            "condition that can truncate the operator's log file; see the "
            "block comment at the consume site in sd_card_manager.c.\n",
            ret, (int)MOCK_FS_WRITE_RETURN, writes);
        return 7;
    }
    if (!gFailNextWrite) {
        fprintf(stderr,
            "FAIL: an arm not taken by the gate was consumed (cleared) "
            "anyway -- an arm that misses its only safe window must survive "
            "to be retried, not silently vanish.\n");
        return 8;
    }

    /* CASE 5 -- the arm from the previous (correctly refused) attempt is
     * still live: reaching UNMOUNT_DISK now must consume it, with the same
     * full effect as CASE 1, proving the arm survived the refusal rather than
     * being lost. */
    gSDCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_UNMOUNT_DISK;
    ret = RunWrite(&writes);
    if (ret != -1 || writes != 0u) {
        fprintf(stderr,
            "FAIL: an arm that survived a refused (non-UNMOUNT_DISK) attempt "
            "did not inject at its next UNMOUNT_DISK opportunity (ret=%d "
            "expected=-1, writes=%u expected=0) -- the arm was lost, or it "
            "fired without skipping the write.\n",
            ret, writes);
        return 9;
    }

    printf("PASS: the REAL (not modeled) SDCardWrite() -- whole function, "
           "spliced verbatim -- injects -1 with ZERO filesystem writes when "
           "armed in UNMOUNT_DISK, performs the write and returns its byte "
           "count in every other case, is one-shot, and survives a refused "
           "attempt to fire at its next opportunity.\n");
    return 0;
}
