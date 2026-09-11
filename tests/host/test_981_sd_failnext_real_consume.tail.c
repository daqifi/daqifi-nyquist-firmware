    return injectWriteFailure;
}
/* ---- END verbatim extraction ---- */

int main(void) {
    /* Armed AND in UNMOUNT_DISK (the only state the real gate admits): the
     * real snippet must consume the arm, report it fired, and clear the flag
     * (one-shot). This is the exact case an adversarial audit found the
     * mocked test could not fail on -- flipping the real
     * `injectWriteFailure = true;` to `= false;` makes this branch observe
     * `RealConsumeIfArmed() == false` while armed, and this assertion fires. */
    gFailNextWrite = true;
    gSDCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_UNMOUNT_DISK;
    if (!RealConsumeIfArmed()) {
        fprintf(stderr,
            "FAIL: armed consume in UNMOUNT_DISK did not report "
            "injectWriteFailure=true -- the real SDCardWrite() consume "
            "snippet no longer injects a failure when gFailNextWrite is set "
            "and the manager is tearing down. If you meant to change this, "
            "update test_981_sd_failnext_real_consume.tail.c's assertions to "
            "match; if you did not, this is the #981 hook silently not "
            "firing where it is supposed to.\n");
        return 1;
    }
    if (gFailNextWrite) {
        fprintf(stderr,
            "FAIL: gFailNextWrite is still armed after a consume in "
            "UNMOUNT_DISK -- the one-shot is broken, so a device could be "
            "left failing every future qualifying SD write instead of "
            "exactly one.\n");
        return 2;
    }

    /* One-shot: a second call right after, still in UNMOUNT_DISK, must NOT
     * fire again. */
    if (RealConsumeIfArmed()) {
        fprintf(stderr,
            "FAIL: a second consume in UNMOUNT_DISK fired again immediately "
            "after the first -- the arm is not one-shot.\n");
        return 3;
    }

    /* Never armed, still in UNMOUNT_DISK: must not fire. Also covers "armed
     * then explicitly disarmed before any qualifying write". */
    gFailNextWrite = false;
    if (RealConsumeIfArmed()) {
        fprintf(stderr,
            "FAIL: an unarmed consume in UNMOUNT_DISK fired anyway -- every "
            "teardown-drain write would fail, not just an explicitly armed "
            "one.\n");
        return 4;
    }

    /* THE GATE ITSELF: armed, but the manager is NOT tearing down (the
     * ordinary WRITE_TO_FILE state stands in for "any non-UNMOUNT_DISK
     * state" -- the real source's condition is an equality test against
     * exactly one enumerator, so any other value exercises the same branch).
     * This must NOT fire, or the hook could truncate the operator's log via
     * the pre-existing ERROR -> UNMOUNT_DISK -> INIT -> OPEN_FILE(WRITE_PLUS)
     * remount path -- the HIGH-severity finding this gate exists to close.
     * A regression here (e.g. the condition silently loosened back to
     * "if (gFailNextWrite)") is exactly as dangerous as mutation 1 above and
     * this is the assertion that catches it. */
    gFailNextWrite = true;
    gSDCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_WRITE_TO_FILE;
    if (RealConsumeIfArmed()) {
        fprintf(stderr,
            "FAIL: an armed consume fired OUTSIDE UNMOUNT_DISK -- the "
            "session-integrity gate is gone or was weakened. This is the "
            "condition that can truncate the operator's log file; see the "
            "block comment at the consume site in sd_card_manager.c.\n");
        return 5;
    }
    if (!gFailNextWrite) {
        fprintf(stderr,
            "FAIL: an arm not taken by the gate was consumed (cleared) "
            "anyway -- an arm that misses its only safe window must survive "
            "to be retried, not silently vanish.\n");
        return 6;
    }

    /* And the arm from the previous (correctly refused) attempt is still
     * live: moving into UNMOUNT_DISK now must consume it, proving the arm
     * survived the refusal rather than being lost. */
    gSDCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_UNMOUNT_DISK;
    if (!RealConsumeIfArmed()) {
        fprintf(stderr,
            "FAIL: an arm that survived a refused (non-UNMOUNT_DISK) attempt "
            "did not fire once the manager reached UNMOUNT_DISK -- the arm "
            "was lost instead of surviving to its next opportunity.\n");
        return 7;
    }

    printf("PASS: the REAL (not modeled) FAILNext consume snippet is a "
           "one-shot gated on UNMOUNT_DISK: fires there, never elsewhere, "
           "and survives a refused attempt to fire at its next "
           "opportunity.\n");
    return 0;
}
