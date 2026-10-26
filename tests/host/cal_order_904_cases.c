/* #904: fixtures for the order guard's OWN arms. NEVER COMPILED -- no target
 * lists this file, and the Makefile's only wildcards match headers. It exists
 * so the guard's ability to accept and to REJECT is asserted on every build
 * against named, reviewable shapes, instead of being claimed in a commit
 * message.
 *
 * Each function's expected sequence lives in the table in tests/host/Makefile
 * next to $(TEAR904_BIN). E=taskENTER_CRITICAL M=.CalM B=.CalB X=taskEXIT_CRITICAL.
 *
 * The two ACCEPT cases matter as much as the rejects: a checker tightened
 * until it rejects everything is not a checker, and `case_oneline_correct` is
 * specifically there because the fix for the one-line defect could so easily
 * have been "reject anything with several events on a line".
 */

/* ACCEPT -- the ordinary multi-line shape the firmware actually uses, with the
 * field named TWICE per statement (LHS and RHS). Adjacent repeats collapse, so
 * this is EMBX and not EMMBBX: multiplicity is not the property. */
bool case_multiline_correct(int n, int *cfg) {
    for (int x = 0; x < n; x++) {
        taskENTER_CRITICAL();
        dst->Data[x].CalM = src->Data[x].CalM;
        dst->Data[x].CalB = src->Data[x].CalB;
        taskEXIT_CRITICAL();
    }
    return true;
}

/* ACCEPT -- correct, but everything on ONE line. Must still pass: the
 * one-line defect was about ORDER within a line, not about line count. */
bool case_oneline_correct(int n, int *cfg) {
    taskENTER_CRITICAL(); dst->CalM = v; dst->CalB = v; taskEXIT_CRITICAL();
    return true;
}

/* REJECT (the r4 finding) -- one line, and the CalM store is OUTSIDE the
 * section. Four fixed `if`s tested per line scored this EMBX and passed it,
 * because they emitted in the CHECKER's order rather than the source's. */
bool case_oneline_store_outside(int n, int *cfg) {
    dst->CalM = v; taskENTER_CRITICAL(); dst->CalB = v; taskEXIT_CRITICAL();
    return true;
}

/* REJECT -- the enter call is commented out. A count over raw text passed
 * this, because the string survives in the comment. */
bool case_enter_commented(int n, int *cfg) {
    /* taskENTER_CRITICAL(); */
    dst->CalM = v;
    dst->CalB = v;
    taskEXIT_CRITICAL();
    return true;
}

/* REJECT -- opened and never closed. The original guard never looked at the
 * exit call at all. */
bool case_missing_exit(int n, int *cfg) {
    taskENTER_CRITICAL();
    dst->CalM = v;
    dst->CalB = v;
    return true;
}

/* REJECT -- multi-line reorder. Same TOKEN COUNTS as the correct shape, which
 * is why counting cannot see it and the sequence can. */
bool case_reordered_multiline(int n, int *cfg) {
    dst->CalM = v;
    taskENTER_CRITICAL();
    dst->CalB = v;
    taskEXIT_CRITICAL();
    return true;
}

/* REJECT -- a LATER access outside the section. This is the case that proves
 * collapsing adjacent repeats did not throw away the property: the extra M
 * falls on the far side of the X, so it is not adjacent to the first M and
 * does not collapse into it. */
bool case_access_after_exit(int n, int *cfg) {
    taskENTER_CRITICAL();
    dst->CalM = v;
    dst->CalB = v;
    taskEXIT_CRITICAL();
    dst->CalM = v;
    return true;
}
