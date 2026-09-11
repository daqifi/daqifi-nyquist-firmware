/* ==========================================================================
 * test_981_sd_failnext_real_consume -- REAL, not modeled, execution of the
 * #981 FAILNext hook, and of its OBSERVABLE EFFECT.
 *
 * test_981_sd_failnext_hook.c re-implements the one-shot arm/consume shape
 * against a HAND-TYPED mock (mock_consume_if_armed()), and the Makefile's
 * $(FAILNEXT_BIN) recipe greps sd_card_manager.c to check that mock still
 * matches the real source's SHAPE. Both are text-level checks: they prove a
 * token or an ordering is present, never that the real statements actually
 * DO the right thing when run. This file is the other half -- it hand-types
 * no part of the function under test.
 *
 * WHY THIS FILE LOOKS NOTHING LIKE ITS PREVIOUS REVISION. It has now had
 * three rounds of the same defect, each a narrower version of "the check
 * cannot fail on a real regression":
 *
 *   round 1  $(FAILNEXT_BIN) was four independent `grep -qF` presence checks.
 *            An audit on PR #1013 mutated the real source in memory --
 *            reordering taskEXIT_CRITICAL() before the armed test, and
 *            separately flipping `injectWriteFailure = true;` to `= false;`
 *            -- and all four kept passing. Fixed by making that guard
 *            POSITIONAL (and by adding this binary).
 *
 *   round 2  this binary, whose extraction stopped at `taskEXIT_CRITICAL();`
 *            and whose tail closed the spliced function with a hand-written
 *            `return injectWriteFailure;`. So the PAYLOAD -- the
 *            `if (injectWriteFailure) { LOG_E(...); writeLen = -1;
 *            goto __exit; }` block, the real SYS_FS_FileWrite() call it is
 *            supposed to skip, and the `__exit: return writeLen;` that
 *            delivers the verdict -- was never extracted, never compiled and
 *            never run. What the test observed was a LOCAL BOOL: a proxy,
 *            the exact shape the two previous rounds were spent on. A
 *            skeptic deleted ONLY the `goto __exit;` line from the real
 *            source and rebuilt: exit 0, this binary printed PASS, all eight
 *            host binaries green. On real hardware that mutation makes the
 *            hook clear its arm, log "consumed", set writeLen = -1, then FALL
 *            THROUGH into the real write, which succeeds and overwrites
 *            writeLen with a byte count -- fault injection entirely dead, a
 *            "failed" write silently succeeding, and nothing anywhere red.
 *
 *   round 3  this revision. The extraction no longer stops anywhere inside
 *            the function: the Makefile splices in ALL of SDCardWrite(),
 *            signature through closing brace, verbatim. Nothing about the
 *            function is hand-typed here any more -- not `int writeLen = -1;`,
 *            not the SYS_FS_HANDLE_INVALID early-out, not the consume, not
 *            the payload block, not the write call, not the return. The tail
 *            file asserts the function's REAL `int` return value and a mock
 *            filesystem-write COUNTER, which is the effect the hook exists to
 *            produce; there is no longer a local of any kind for a test to
 *            settle for.
 *
 * WHAT IS ASSERTED (see the tail file for the cases):
 *   fired      -> SDCardWrite() returns -1  AND  zero calls reached the mock
 *                 SYS_FS_FileWrite(). Both halves are needed: under the
 *                 round-2 mutation the return was briefly -1 before the write
 *                 ran and overwrote it, so only the counter distinguishes
 *                 "skipped the write" from "fell through to it".
 *   not fired  -> SDCardWrite() returns the mock's byte count (never -1) AND
 *                 exactly one call reached the mock. The first case in the
 *                 tail is a CONTROL that asserts the write IS reachable, so
 *                 the "zero writes" assertions above cannot pass vacuously.
 *
 * ON THE STAND-INS BELOW. The previous revision warned that adding a third
 * real symbol to the extracted range would need "rework, not a silent stub".
 * That rework is what this is, and it also removes the hazard that warning
 * was about: with the WHOLE function extracted, any symbol the real
 * SDCardWrite() gains that is not stubbed here is a COMPILE ERROR in the
 * generated file, so the build fails loudly rather than quietly covering
 * less. The compiler is the guard, and it is strictly stronger than a grep --
 * which is why this file no longer asks the Makefile to grep for each stubbed
 * name. What the compiler CANNOT check is a stub's SEMANTICS, so every stub
 * below records what it mirrors and where. One of them is load-bearing:
 *
 *   MOCK_FS_WRITE_RETURN MUST NEVER BE -1. -1 is the value the hook injects
 *   and the value the real SYS_FS_FileWrite() reports a genuine failure with.
 *   If the mock returned it, "the hook fired" and "the write ran and failed"
 *   would be the same observation and every assertion in the tail would go
 *   blind.
 *
 * Do not hand-edit the generated file. Edit sd_card_manager.c; this
 * scaffolding follows automatically.
 *
 * The ORDERING mutation from round 1 is still deliberately NOT this file's
 * job: whether the clear happens strictly between ENTER and EXIT is a
 * concurrency property, and no single-threaded host run can observe it. It
 * stays $(FAILNEXT_BIN)'s positional grep.
 * ========================================================================== */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* Single-threaded host: no-ops. See the closing paragraph above for why the
 * atomicity these delimit is checked structurally instead, not here. */
#define taskENTER_CRITICAL() ((void)0)
#define taskEXIT_CRITICAL() ((void)0)

/* Util/Logger.h. The hook's LOG_E sits outside the critical section on
 * purpose (see the consume site's comment); nothing here depends on its text,
 * so a variadic no-op is enough. */
#define LOG_E(...) ((void)0)

/* FreeRTOS. TickType_t is uint32_t in this firmware's port; only
 * SD_CheckFsOpDuration() consumes the value, and that is a no-op here. */
typedef uint32_t TickType_t;
static TickType_t xTaskGetTickCount(void) {
    return (TickType_t)0;
}

/* Mirrors Harmony's sys_fs.h:83 / :99 exactly. The unsigned handle type
 * matters: SDCardWrite()'s early-out compares against this sentinel. */
typedef uintptr_t SYS_FS_HANDLE;
#define SYS_FS_HANDLE_INVALID ((SYS_FS_HANDLE)(-1))

/* THE CRUX OF THIS FILE. Mirrors sys_fs.h:2692 --
 * `size_t SYS_FS_FileWrite(SYS_FS_HANDLE handle, const void *buffer,
 * size_t nbyte)`. The size_t return is mirrored rather than simplified to
 * int because the real call site assigns it straight into `int writeLen`,
 * and that implicit conversion is how Harmony's (size_t)-1 failure sentinel
 * becomes the -1 every caller tests for -- narrowing the stub to int would
 * quietly exercise a different conversion than the firmware performs.
 *
 * The counter is the observable the round-2 revision lacked: it is the only
 * way to tell "the hook skipped the write" from "the hook set writeLen = -1
 * and then fell through and did the write anyway". */
static unsigned gMockFsWriteCalls = 0u;
#define MOCK_FS_WRITE_RETURN ((size_t)128) /* never -1 -- see the header */
static size_t SYS_FS_FileWrite(SYS_FS_HANDLE handle, const void *buffer,
        size_t nbyte) {
    (void)handle;
    (void)buffer;
    (void)nbyte;
    gMockFsWriteCalls++;
    return MOCK_FS_WRITE_RETURN;
}

/* Mirrors sd_card_manager.c:184's signature; it only logs slow FS ops. */
static void SD_CheckFsOpDuration(TickType_t startTick, const char *operation,
        int result) {
    (void)startTick;
    (void)operation;
    (void)result;
}

/* Mirrors `static volatile bool gFailNextWrite = false;` (sd_card_manager.c).
 * $(FAILNEXT_BIN)'s first grep guard verifies that exact text still exists in
 * the real source. `volatile` is dropped here because it is a
 * cross-context-visibility qualifier and this binary has no other context. */
static bool gFailNextWrite = false;

/* The real consume gates on `gSDCardData.currentProcessState ==
 * SD_CARD_MANAGER_PROCESS_STATE_UNMOUNT_DISK` -- the arm may only be taken by
 * a write issued while the manager is ALREADY tearing down, never by an
 * ordinary or rotation-drain write, because those three can start the
 * pre-existing ERROR -> UNMOUNT_DISK -> INIT -> OPEN_FILE(WRITE_PLUS) remount
 * that truncates the log file (full argument at gFailNextWrite's block
 * comment in sd_card_manager.c). Two enumerators are enough: the real
 * condition is an equality test against exactly one of them, so any other
 * value exercises the same branch. */
typedef enum {
    SD_CARD_MANAGER_PROCESS_STATE_WRITE_TO_FILE,
    SD_CARD_MANAGER_PROCESS_STATE_UNMOUNT_DISK,
} sd_card_manager_processState_t;

/* The five fields of sd_card_manager_context_t that SDCardWrite() touches,
 * with the real types (sd_card_manager.c's `typedef struct { ... }
 * sd_card_manager_context_t`). A field the function starts touching that is
 * missing here is a compile error, not a silent gap. */
static struct {
    sd_card_manager_processState_t currentProcessState;
    SYS_FS_HANDLE fileHandle;
    uint8_t *writeBuffer;
    uint16_t sdCardWriteBufferOffset;
    size_t writeBufferLength;
} gSDCardData;

/* Real backing store for gSDCardData.writeBuffer: the extracted code forms
 * `writeBuffer + sdCardWriteBufferOffset`, and pointer arithmetic on a null
 * pointer is undefined even when the result is never dereferenced. */
static uint8_t gMockWriteBuffer[256];

/* ---- BEGIN verbatim extraction of sd_card_manager.c's ENTIRE SDCardWrite()
 * -- signature through closing brace -- spliced in by $(FAILNEXT_REAL_BIN) in
 * the Makefile. Everything between this line and the matching END marker in
 * the .tail.c file is generated, not hand-typed. ---- */
