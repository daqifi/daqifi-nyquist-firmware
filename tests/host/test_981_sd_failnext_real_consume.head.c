/* ==========================================================================
 * test_981_sd_failnext_real_consume -- REAL, not modeled, execution of the
 * #981 FAILNext consume snippet.
 *
 * test_981_sd_failnext_hook.c re-implements the one-shot arm/consume shape
 * against a HAND-TYPED mock (mock_consume_if_armed()), and the Makefile's
 * $(FAILNEXT_BIN) recipe greps sd_card_manager.c to check the mock still
 * matches the real source's SHAPE. Both are text-level checks: they prove a
 * token or an ordering is present, never that the real statements actually
 * compute the right thing when run. That is a real gap -- an adversarial
 * audit on PR #1013 found two in-memory mutations of the real source that
 * left every grep guard AND every mock-based test passing:
 *   1. changing the real `injectWriteFailure = true;` to `= false;` (so an
 *      armed FAILNext consumes the arm but injects no failure at all), and
 *   2. reordering the real taskEXIT_CRITICAL() to before the armed-test
 *      (destroying the read-modify-write atomicity across the arming SCPI
 *      task and this one).
 * Fixed the grep-guard side of that in the Makefile ($(FAILNEXT_BIN) now
 * checks POSITION, not just presence, and additionally requires the
 * `injectWriteFailure = true;` token that the old guard never checked at
 * all). This file is the other half: it does not re-implement anything.
 * The Makefile's $(FAILNEXT_REAL_BIN) recipe extracts the REAL, VERBATIM
 * consume snippet from sd_card_manager.c (the lines between
 * `bool injectWriteFailure = false;` and its `taskEXIT_CRITICAL();`) and
 * splices it, unmodified, into the body of RealConsumeIfArmed() below by
 * concatenating this head, the extraction, and
 * test_981_sd_failnext_real_consume.tail.c. What gets compiled and run is
 * the production statement sequence itself, so mutation 1 above is caught
 * BEHAVIOURALLY (the real code, run for real, reports the wrong thing) —
 * see the tail file for the assertions and why mutation 2 (a concurrency
 * property) is out of reach for a single-threaded host run and stays the
 * Makefile order-check's job, not this file's.
 *
 * Do not hand-edit the body of RealConsumeIfArmed() in the generated file:
 * edit sd_card_manager.c, and this scaffolding follows automatically. Do not
 * add anything to sd_card_manager.c's snippet that isn't already
 * dependency-free modulo the two stand-ins below (gFailNextWrite and
 * gSDCardData.currentProcessState) -- that region is deliberately bounded so
 * it stays host-compilable standalone; if a future change needs a THIRD real
 * symbol (LOG_E, a goto, anything else from gSDCardData), the extraction
 * range and this harness need rework together, not a silent stub.
 * ========================================================================== */
#include <stdbool.h>
#include <stdio.h>

/* Host stand-ins. Single-threaded, so these do not need to do anything for
 * this file's purpose: the CONCURRENCY property (does the clear happen
 * strictly between ENTER and EXIT) cannot be observed by any single-threaded
 * run and is checked structurally instead, by the Makefile's $(FAILNEXT_BIN)
 * ordering guard. What this file proves is a different, non-concurrency
 * property: given the real statement sequence, does an armed flag actually
 * get consumed (and only in the state it is supposed to be consumed in) --
 * a property a no-op stand-in does not hide. */
#define taskENTER_CRITICAL() ((void)0)
#define taskEXIT_CRITICAL() ((void)0)

/* Mirrors the real declaration `static volatile bool gFailNextWrite = false;`
 * (sd_card_manager.c). $(FAILNEXT_BIN)'s first grep guard already verifies
 * that exact text still exists in the real source; `volatile` is dropped
 * here because it is a cross-context-visibility qualifier and this is a
 * single-threaded host binary with no other context to be visible to. */
static bool gFailNextWrite = false;

/* #981 (post-audit): the real consume site gates on
 * `gSDCardData.currentProcessState == SD_CARD_MANAGER_PROCESS_STATE_UNMOUNT_DISK`
 * -- the arm may only be taken by a write issued while the manager is already
 * tearing down, never by an ordinary write or a rotation-drain write, because
 * those three (and only those three) can start the pre-existing
 * ERROR -> UNMOUNT_DISK -> INIT -> OPEN_FILE(WRITE_PLUS) remount that
 * truncates the log file (see gFailNextWrite's block comment in
 * sd_card_manager.c for the full argument). This minimal stand-in mirrors
 * only the one field and one enumerator the extracted snippet references;
 * the Makefile's $(FAILNEXT_REAL_BIN) recipe greps the real source to confirm
 * both names still exist before trusting this model of them. */
typedef enum {
    SD_CARD_MANAGER_PROCESS_STATE_WRITE_TO_FILE,
    SD_CARD_MANAGER_PROCESS_STATE_UNMOUNT_DISK,
} sd_card_manager_processState_t;

static struct {
    sd_card_manager_processState_t currentProcessState;
} gSDCardData;

/* ---- BEGIN verbatim extraction of sd_card_manager.c's SDCardWrite()
 * consume snippet (see $(FAILNEXT_REAL_BIN) in the Makefile for the sed
 * command that performs the splice). Everything between this line and the
 * matching END marker in the .tail.c file is generated, not hand-typed. */
static bool RealConsumeIfArmed(void) {
