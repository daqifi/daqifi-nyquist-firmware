/* ==========================================================================
 * test_1098_scpi_reply_buffer_pairing.c -- issue #1098
 *
 * WHAT IS UNDER TEST
 *
 * Four SCPI reply callbacks used to format their response into a STACK-LOCAL
 * array and write it straight out, instead of using the shared SCPI response
 * scratch buffer (gScpiRespBuf, #347) that exists precisely so a
 * response-sized allocation never lands on a task stack:
 *
 *   SCPIInterface.c  SCPI_SysLogLevelSet     SYST:LOG:LEVel <mod>,<lvl>
 *                                            char buf[80], one write
 *   SCPIInterface.c  SCPI_SysLogLevelGet     SYST:LOG:LEVel?
 *                                            char buf[48], one write PER
 *                                            module in a LOG_MODULE_COUNT loop
 *   SCPIInterface.c  SCPI_SysLogLevelAllSet  SYST:LOG:LEVel:ALL <lvl>
 *                                            char buf[48], same loop shape
 *   SCPIStorageSD.c  SCPI_StorageSDInfo      SYST:STOR:SD:INFO?
 *                                            char result[80], one write, plus
 *                                            a format-error path
 *
 * (Qodo Rule-400154 named the Get and the SDInfo sites on PR #1096; the Set
 * and AllSet twins sit immediately around Get and have the identical shape.)
 *
 * THE FIX, in all four, is the established pattern already used ~17 times
 * across SCPIInterface.c / SCPILAN.c / SCPIStorageSD.c:
 *
 *     char* buf = (char*)SCPI_ResponseBuf_Take();
 *     if (buf == NULL) return SCPI_RES_ERR;   <-- NO Give on this path
 *     ... snprintf into buf, write ...
 *     SCPI_ResponseBuf_Give();                <-- exactly one, every exit
 *
 * The contract (SCPIInterface.h:141-182) is the whole substance of the change:
 * a non-NULL Take MUST be matched by EXACTLY ONE Give on EVERY exit path, and
 * a NULL Take by NONE. Get it wrong in either direction and the failure is not
 * cosmetic -- a missed Give wedges gScpiRespMutex and every SCPI callback on
 * BOTH transports that needs the shared buffer blocks on it forever (they wait
 * portMAX_DELAY), and a Give with nothing taken releases somebody else's hold.
 *
 * TWO SHAPES, DELIBERATELY DIFFERENT
 *
 *   one-shot  (Set, SDInfo)  take -> format -> write -> give
 *   loop      (Get, AllSet)  take ONCE -> format+write per module -> give ONCE
 *
 * The loop sites follow SCPI_SysInfoTextGet, which takes once at entry and
 * reuses the one buffer across ~90 writes. A take/give per ITERATION would
 * also "pair", so a test that only counted take==give would pass it -- but it
 * would block on the mutex LOG_MODULE_COUNT times for one query and let a peer
 * callback interleave its reply between our lines. per_iteration_* below is
 * that mutation, modelled and asserted against, so the single-hold property is
 * pinned rather than merely intended.
 *
 * HOW IT IS TESTED
 *
 * Neither SCPIInterface.c nor SCPIStorageSD.c is includable on a host. For
 * SCPIInterface.c that was established by SCPI999_BIN, which tried: 42 direct
 * includes spanning libscpi, Harmony PLIB, the USB HS and WINC drivers and
 * FreeRTOS's MIPS port layer. For SCPIStorageSD.c it is BENCH_BIN's finding
 * (libscpi + FreeRTOS + the whole SD manager) -- see both Makefile comments. So,
 * the same approach as test_943_bench_stall_bound.c, test_947_sysinfo_write_abort.c,
 * test_953_bench_suspend_diagnosis.c and test_1121_start_streaming_arm_cascade.c:
 * this file RE-IMPLEMENTS the control-flow shape of each callback against a mock
 * Take/Give/write, and compares the pre-fix and post-fix shapes on identical
 * inputs. What is proven is the PAIRING ALGEBRA of the shapes, which is exactly
 * what #1098 is about.
 *
 * The old (stack-buffer) shapes are modelled too, and asserted to call Take and
 * Give ZERO times. That is the canary: it is what makes every "exactly one
 * take, exactly one give" assertion below discriminate the fix from the bug,
 * rather than being trivially satisfiable by a test that never exercised the
 * new code at all.
 *
 * FIDELITY -- what the extracted functions are NOT
 *
 * 1. They model CONTROL FLOW (which paths take, write and give, and how many
 *    times), not reply CONTENT. The bytes are unchanged by #1098 and are not
 *    what the fix is about; mock_write records lengths only.
 * 2. mock_take/mock_give do not model FreeRTOS mutex semantics (blocking,
 *    priority inheritance, recursion). They count calls and hand back a buffer
 *    or NULL. The real xSemaphoreTake/Give pairing is the firmware's; what this
 *    file pins is that the CALL SITES pair.
 * 3. The real snprintf truncation clamp is modelled only where it can change
 *    control flow -- i.e. SDInfo's `len < 0 || len >= CAP` error branch, which
 *    the fix moves to AFTER the take and which therefore GAINS a Give it did
 *    not previously need. The log-level sites' `if (len > 0)` clamp cannot skip
 *    a Give (it guards the write, not the give), and is modelled as a plain
 *    write-or-not.
 * 4. LOG_MODULE_COUNT is not copied as a constant: every loop assertion is
 *    swept over module counts 0..12 instead, so no Makefile grep guard is
 *    needed for it and adding a log module cannot silently stale this file.
 *    The count=0 case matters on its own -- the take/give pair must still be
 *    exactly one each even when the loop body never runs.
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include "test_framework.h"

/* libscpi's scpi_result_t values (firmware/src/libraries/scpi/libscpi/inc/scpi/types.h:167). */
#define RES_OK    1
#define RES_ERR  (-1)

/* SCPI_RESPONSE_BUF_SIZE (SCPIInterface.h:114). Pinned by the Makefile grep. */
#define FW_RESPONSE_BUF_SIZE  2048U

/* ==========================================================================
 * Mock environment
 * ========================================================================== */

typedef struct {
    int      takeCalls;      /* SCPI_ResponseBuf_Take() invocations */
    int      giveCalls;      /* SCPI_ResponseBuf_Give() invocations */
    int      takeReturnsNull;/* 1 => every Take yields NULL (mutex missing) */
    int      held;           /* running take-give balance; must never go < 0 */
    int      heldMax;        /* high-water mark of `held` (1 = single hold) */
    int      giveWithoutHold;/* Gives observed while held == 0 (a real bug) */
    int      writes;         /* interface->write() invocations */
    unsigned bytes;          /* total bytes handed to write() */
    int      wroteWhileUnheld; /* writes issued with no buffer held */
    unsigned now;            /* mock tick clock, ms (configTICK_RATE_HZ 1000) */
    int      shortWrites;    /* writes the transport refused to complete */
    char     buf[FW_RESPONSE_BUF_SIZE];
} MockEnv;

static void mock_init(MockEnv* e, int takeReturnsNull)
{
    e->takeCalls = 0;
    e->giveCalls = 0;
    e->takeReturnsNull = takeReturnsNull;
    e->held = 0;
    e->heldMax = 0;
    e->giveWithoutHold = 0;
    e->writes = 0;
    e->bytes = 0;
    e->wroteWhileUnheld = 0;
    e->now = 0;
    e->shortWrites = 0;
    e->buf[0] = '\0';
}

static char* mock_take(MockEnv* e)
{
    e->takeCalls++;
    if (e->takeReturnsNull) {
        return NULL;
    }
    e->held++;
    if (e->held > e->heldMax) {
        e->heldMax = e->held;
    }
    return e->buf;
}

static void mock_give(MockEnv* e)
{
    e->giveCalls++;
    if (e->held == 0) {
        e->giveWithoutHold++;   /* releasing a hold we do not own */
    } else {
        e->held--;
    }
}

/* Records a write. `len` is the clamped length the call site passes. */
static void mock_write(MockEnv* e, unsigned len)
{
    e->writes++;
    e->bytes += len;
    if (e->held == 0) {
        /* Writing from a buffer we do not hold -- the torn-output hazard the
         * shared buffer's mutex exists to prevent (SCPIInterface.c's REJECTED
         * note above SysInfoText_Write). The old stack-buffer shapes do this
         * legitimately (their buffer is private), so it is only asserted on
         * the fixed shapes. */
        e->wroteWhileUnheld++;
    }
}

/* Stand-in for one formatted line. Returns snprintf's would-be length. The
 * value is injected rather than computed so the SDInfo error branch can be
 * driven deterministically; content is irrelevant to #1098 (fidelity note 1). */
static int format_line(MockEnv* e, int injectedLen)
{
    (void)e;
    return injectedLen;
}

/* The clamp the log-level call sites apply before write():
 *   (len < CAP - 1) ? len : CAP - 1        */
static unsigned clamp_len(int len)
{
    return ((unsigned)len < FW_RESPONSE_BUF_SIZE - 1U)
            ? (unsigned)len : FW_RESPONSE_BUF_SIZE - 1U;
}

/* ==========================================================================
 * SHAPE 1 -- one-shot, one write: SCPI_SysLogLevelSet
 * ========================================================================== */

/* PRE-FIX: char buf[80] on the stack. Never touches Take/Give. */
static int old_syslog_set(MockEnv* e, int injectedLen)
{
    int len = format_line(e, injectedLen);
    if (len > 0) {
        mock_write(e, clamp_len(len));
    }
    return RES_OK;
}

/* POST-FIX. */
static int new_syslog_set(MockEnv* e, int injectedLen)
{
    char* buf = mock_take(e);
    if (buf == NULL) {
        return RES_ERR;             /* NULL take -> NO give */
    }
    int len = format_line(e, injectedLen);
    if (len > 0) {
        mock_write(e, clamp_len(len));
    }
    mock_give(e);
    return RES_OK;
}

/* ==========================================================================
 * SHAPE 2 -- branching query with a loop arm: SCPI_SysLogLevelGet
 *
 * Three paths:
 *   named module, unknown   -> SCPI_ErrorPush + RES_ERR, BEFORE any take
 *   named module, known     -> SCPI_ResultInt32, no shared buffer at all
 *   no parameter            -> the LOG_MODULE_COUNT dump loop (the fixed arm)
 * ========================================================================== */

typedef enum {
    GET_NAMED_UNKNOWN = 0,
    GET_NAMED_KNOWN,
    GET_DUMP_ALL
} GetPath;

/* PRE-FIX: char buf[48] declared inside the else arm. Never takes. */
static int old_syslog_get(MockEnv* e, GetPath path, int moduleCount, int injectedLen)
{
    if (path != GET_DUMP_ALL) {
        if (path == GET_NAMED_UNKNOWN) {
            return RES_ERR;
        }
        /* SCPI_ResultInt32 -- libscpi's own output path, not our buffer. */
        return RES_OK;
    }
    for (int i = 0; i < moduleCount; i++) {
        int len = format_line(e, injectedLen);
        if (len > 0) {
            mock_write(e, clamp_len(len));
        }
    }
    return RES_OK;
}

/* POST-FIX: one take before the loop, one give after it. */
static int new_syslog_get(MockEnv* e, GetPath path, int moduleCount, int injectedLen)
{
    if (path != GET_DUMP_ALL) {
        if (path == GET_NAMED_UNKNOWN) {
            return RES_ERR;         /* returns before the take -> no give */
        }
        return RES_OK;              /* ditto */
    }
    char* buf = mock_take(e);
    if (buf == NULL) {
        return RES_ERR;             /* NULL take -> NO give */
    }
    for (int i = 0; i < moduleCount; i++) {
        int len = format_line(e, injectedLen);
        if (len > 0) {
            mock_write(e, clamp_len(len));
        }
    }
    mock_give(e);
    return RES_OK;
}

/* The BOUNDED dump, as shipped: SysLogLevelWrite's two guards latch through
 * `ok`, and the loop runs to completion either way (the helper short-circuits
 * once latched -- the #1004 ScpiHelpWrite shape, chosen so no call site can
 * skip the single Give). Returns RES_ERR when the reply was truncated.
 *
 * `attemptsPerWrite`/`budgetMs` model SCPI_WriteWithRetry's ~1 s per-call cost
 * and SCPI_LOGLEVEL_WRITE_BUDGET_MS; `shortWriteAt` injects guard 1. */
static int new_syslog_get_bounded(MockEnv* e, int moduleCount, int injectedLen,
                                  unsigned msPerWrite, unsigned budgetMs,
                                  int shortWriteAt)
{
    char* buf = mock_take(e);
    if (buf == NULL) {
        return RES_ERR;
    }
    unsigned startTick = e->now;
    int ok = 1;
    for (int i = 0; i < moduleCount; i++) {
        int len = injectedLen;
        if (len > 0) {
            /* SysLogLevelWrite, inlined */
            if (ok) {
                if ((unsigned)(e->now - startTick) >= budgetMs) {
                    ok = 0;                      /* guard 2: deadline */
                } else {
                    e->now += msPerWrite;
                    if (shortWriteAt >= 0 && i >= shortWriteAt) {
                        e->shortWrites++;
                        ok = 0;                  /* guard 1: short write */
                    } else {
                        mock_write(e, clamp_len(len));
                    }
                }
            }
        }
    }
    mock_give(e);                                 /* ALWAYS, latched or not */
    return ok ? RES_OK : RES_ERR;
}

/* The UNBOUNDED dump this PR shipped first: no guards, every write attempted,
 * result ignored. Kept so the bound is proven to be what changed -- it pairs
 * identically, so only the HOLD DURATION distinguishes it. */
static int new_syslog_get_unbounded(MockEnv* e, int moduleCount,
                                    int injectedLen, unsigned msPerWrite)
{
    char* buf = mock_take(e);
    if (buf == NULL) {
        return RES_ERR;
    }
    for (int i = 0; i < moduleCount; i++) {
        int len = injectedLen;
        if (len > 0) {
            e->now += msPerWrite;
            mock_write(e, clamp_len(len));
        }
    }
    mock_give(e);
    return RES_OK;
}

/* MUTATION (not a historical shape): take/give per ITERATION. It PAIRS -- so a
 * test that only asserted takeCalls == giveCalls would accept it -- but it
 * blocks on the mutex once per module and drops the hold between lines, which
 * is neither what SCPI_SysInfoTextGet does nor what the fix does. Modelled so
 * the single-hold property is an executable assertion. */
static int per_iteration_syslog_get(MockEnv* e, int moduleCount, int injectedLen)
{
    for (int i = 0; i < moduleCount; i++) {
        char* buf = mock_take(e);
        if (buf == NULL) {
            return RES_ERR;
        }
        int len = format_line(e, injectedLen);
        if (len > 0) {
            mock_write(e, clamp_len(len));
        }
        mock_give(e);
    }
    return RES_OK;
}

/* ==========================================================================
 * SHAPE 3 -- unconditional loop: SCPI_SysLogLevelAllSet
 * ========================================================================== */

static int old_syslog_all_set(MockEnv* e, int moduleCount, int injectedLen)
{
    for (int i = 0; i < moduleCount; i++) {
        int len = format_line(e, injectedLen);
        if (len > 0) {
            mock_write(e, clamp_len(len));
        }
    }
    return RES_OK;
}

static int new_syslog_all_set(MockEnv* e, int moduleCount, int injectedLen)
{
    char* buf = mock_take(e);
    if (buf == NULL) {
        return RES_ERR;
    }
    for (int i = 0; i < moduleCount; i++) {
        int len = format_line(e, injectedLen);
        if (len > 0) {
            mock_write(e, clamp_len(len));
        }
    }
    mock_give(e);
    return RES_OK;
}

/* ==========================================================================
 * SHAPE 4 -- one-shot with an error path BELOW the take: SCPI_StorageSDInfo
 *
 * This is the site where the port is not mechanical. Pre-fix, the
 * `len < 0 || len >= sizeof(result)` branch returned with NO buffer held, so
 * it needed no release. Post-fix that same branch runs while the shared buffer
 * IS held -- so it GAINS a Give the original code did not have. Forget it and
 * a malformed CID wedges gScpiRespMutex permanently.
 * ========================================================================== */

/* The CID read failing returns before the take in both shapes. */
static int old_sd_info(MockEnv* e, int cidOk, int injectedLen)
{
    if (!cidOk) {
        return RES_ERR;
    }
    int len = format_line(e, injectedLen);
    if (len < 0 || (unsigned)len >= FW_RESPONSE_BUF_SIZE) {
        return RES_ERR;             /* nothing held, nothing to release */
    }
    mock_write(e, (unsigned)len);
    mock_write(e, 2U);              /* the trailing "\r\n" */
    return RES_OK;
}

static int new_sd_info(MockEnv* e, int cidOk, int injectedLen)
{
    if (!cidOk) {
        return RES_ERR;             /* before the take -> no give */
    }
    char* buf = mock_take(e);
    if (buf == NULL) {
        return RES_ERR;             /* NULL take -> NO give */
    }
    int len = format_line(e, injectedLen);
    if (len < 0 || (unsigned)len >= FW_RESPONSE_BUF_SIZE) {
        mock_give(e);               /* THE ADDED RELEASE */
        return RES_ERR;
    }
    mock_write(e, (unsigned)len);
    mock_write(e, 2U);
    mock_give(e);
    return RES_OK;
}

/* MUTATION: the mechanical port that moves the buffer but forgets the added
 * release on the format-error path. Modelled so that "SDInfo's error branch
 * gives" is proven by a shape that fails it, not only by one that passes. */
static int leaky_sd_info(MockEnv* e, int cidOk, int injectedLen)
{
    if (!cidOk) {
        return RES_ERR;
    }
    char* buf = mock_take(e);
    if (buf == NULL) {
        return RES_ERR;
    }
    int len = format_line(e, injectedLen);
    if (len < 0 || (unsigned)len >= FW_RESPONSE_BUF_SIZE) {
        return RES_ERR;             /* LEAKS the hold */
    }
    mock_write(e, (unsigned)len);
    mock_write(e, 2U);
    mock_give(e);
    return RES_OK;
}

/* ==========================================================================
 * Shared assertion: a fixed shape's invocation left the buffer balanced.
 * ========================================================================== */

static void assert_balanced_single_hold(MockEnv* e)
{
    ASSERT_EQ(e->takeCalls, 1);
    ASSERT_EQ(e->giveCalls, 1);
    ASSERT_EQ(e->held, 0);               /* nothing leaked */
    ASSERT_EQ(e->heldMax, 1);            /* and it was ONE hold, not nested */
    ASSERT_EQ(e->giveWithoutHold, 0);    /* never released what we did not own */
    ASSERT_EQ(e->wroteWhileUnheld, 0);   /* every write happened under the hold */
}

/* ==========================================================================
 * (c) THE CANARY -- the old shapes never touch Take/Give at all.
 *
 * Without this, every "exactly one take, exactly one give" assertion below
 * would also be satisfied by a test that had simply not exercised the fix.
 * ========================================================================== */

TEST(canary_old_stack_buffer_shapes_never_take_or_give)
{
    MockEnv e;

    mock_init(&e, 0);
    ASSERT_EQ(old_syslog_set(&e, 24), RES_OK);
    ASSERT_EQ(e.takeCalls, 0);
    ASSERT_EQ(e.giveCalls, 0);
    ASSERT_EQ(e.writes, 1);              /* it DID reply -- from the stack */

    mock_init(&e, 0);
    ASSERT_EQ(old_syslog_get(&e, GET_DUMP_ALL, 10, 24), RES_OK);
    ASSERT_EQ(e.takeCalls, 0);
    ASSERT_EQ(e.giveCalls, 0);
    ASSERT_EQ(e.writes, 10);

    mock_init(&e, 0);
    ASSERT_EQ(old_syslog_all_set(&e, 10, 18), RES_OK);
    ASSERT_EQ(e.takeCalls, 0);
    ASSERT_EQ(e.giveCalls, 0);
    ASSERT_EQ(e.writes, 10);

    mock_init(&e, 0);
    ASSERT_EQ(old_sd_info(&e, 1, 40), RES_OK);
    ASSERT_EQ(e.takeCalls, 0);
    ASSERT_EQ(e.giveCalls, 0);
    ASSERT_EQ(e.writes, 2);

    /* And the old SDInfo error path -- the one that GAINS a give in the fix --
     * likewise released nothing, because it held nothing. */
    mock_init(&e, 0);
    ASSERT_EQ(old_sd_info(&e, 1, (int)FW_RESPONSE_BUF_SIZE), RES_ERR);
    ASSERT_EQ(e.takeCalls, 0);
    ASSERT_EQ(e.giveCalls, 0);
    ASSERT_EQ(e.writes, 0);
}

/* ==========================================================================
 * (a) EXACTLY ONE GIVE PER NON-NULL TAKE, ON EVERY RETURN PATH
 * ========================================================================== */

TEST(new_one_shot_shapes_pair_exactly_once)
{
    MockEnv e;

    mock_init(&e, 0);
    ASSERT_EQ(new_syslog_set(&e, 24), RES_OK);
    assert_balanced_single_hold(&e);
    ASSERT_EQ(e.writes, 1);

    /* snprintf returning 0/negative skips the WRITE but must not skip the
     * give -- the clamp guards the write, not the release. */
    mock_init(&e, 0);
    ASSERT_EQ(new_syslog_set(&e, 0), RES_OK);
    assert_balanced_single_hold(&e);
    ASSERT_EQ(e.writes, 0);

    mock_init(&e, 0);
    ASSERT_EQ(new_syslog_set(&e, -1), RES_OK);
    assert_balanced_single_hold(&e);
    ASSERT_EQ(e.writes, 0);

    mock_init(&e, 0);
    ASSERT_EQ(new_sd_info(&e, 1, 40), RES_OK);
    assert_balanced_single_hold(&e);
    ASSERT_EQ(e.writes, 2);
    ASSERT_EQ(e.bytes, 42U);             /* 40 + the trailing "\r\n" */
}

TEST(new_sd_info_format_error_path_still_gives)
{
    MockEnv e;

    /* snprintf truncated (len >= capacity): the branch the fix moves BELOW the
     * take, and which therefore needs a release the pre-fix code never had. */
    mock_init(&e, 0);
    ASSERT_EQ(new_sd_info(&e, 1, (int)FW_RESPONSE_BUF_SIZE), RES_ERR);
    assert_balanced_single_hold(&e);
    ASSERT_EQ(e.writes, 0);              /* errored before writing anything */

    /* snprintf's own encoding error (negative return): same branch. */
    mock_init(&e, 0);
    ASSERT_EQ(new_sd_info(&e, 1, -1), RES_ERR);
    assert_balanced_single_hold(&e);
    ASSERT_EQ(e.writes, 0);

    /* Exactly at the boundary: len == CAP - 1 is the largest ACCEPTED length,
     * so this one writes and gives on the success path, not the error path. */
    mock_init(&e, 0);
    ASSERT_EQ(new_sd_info(&e, 1, (int)FW_RESPONSE_BUF_SIZE - 1), RES_OK);
    assert_balanced_single_hold(&e);
    ASSERT_EQ(e.writes, 2);
}

TEST(mutation_sd_info_that_forgets_the_error_give_leaks_the_hold)
{
    MockEnv e;

    /* The success path is indistinguishable -- which is exactly why the error
     * path needs its own test. */
    mock_init(&e, 0);
    ASSERT_EQ(leaky_sd_info(&e, 1, 40), RES_OK);
    ASSERT_EQ(e.takeCalls, 1);
    ASSERT_EQ(e.giveCalls, 1);
    ASSERT_EQ(e.held, 0);

    /* The error path is where it diverges: the hold is never released, which
     * on the device wedges gScpiRespMutex for every later SCPI callback on
     * both transports. */
    mock_init(&e, 0);
    ASSERT_EQ(leaky_sd_info(&e, 1, (int)FW_RESPONSE_BUF_SIZE), RES_ERR);
    ASSERT_EQ(e.takeCalls, 1);
    ASSERT_EQ(e.giveCalls, 0);           /* <-- the bug */
    ASSERT_EQ(e.held, 1);                /* <-- leaked, and never recovered */
}

TEST(new_paths_that_return_before_the_take_neither_take_nor_give)
{
    MockEnv e;

    /* SCPI_SysLogLevelGet's two non-dump arms never reach the buffer. */
    mock_init(&e, 0);
    ASSERT_EQ(new_syslog_get(&e, GET_NAMED_UNKNOWN, 10, 24), RES_ERR);
    ASSERT_EQ(e.takeCalls, 0);
    ASSERT_EQ(e.giveCalls, 0);

    mock_init(&e, 0);
    ASSERT_EQ(new_syslog_get(&e, GET_NAMED_KNOWN, 10, 24), RES_OK);
    ASSERT_EQ(e.takeCalls, 0);
    ASSERT_EQ(e.giveCalls, 0);

    /* SCPI_StorageSDInfo's DRV_SDSPI_GetCID failure, likewise. */
    mock_init(&e, 0);
    ASSERT_EQ(new_sd_info(&e, 0, 40), RES_ERR);
    ASSERT_EQ(e.takeCalls, 0);
    ASSERT_EQ(e.giveCalls, 0);
}

/* ==========================================================================
 * (b) A NULL TAKE IS NEVER FOLLOWED BY A GIVE
 * ========================================================================== */

TEST(null_take_is_never_followed_by_a_give)
{
    MockEnv e;

    mock_init(&e, 1);
    ASSERT_EQ(new_syslog_set(&e, 24), RES_ERR);
    ASSERT_EQ(e.takeCalls, 1);
    ASSERT_EQ(e.giveCalls, 0);
    ASSERT_EQ(e.giveWithoutHold, 0);
    ASSERT_EQ(e.writes, 0);              /* nothing written into a NULL buffer */

    mock_init(&e, 1);
    ASSERT_EQ(new_syslog_get(&e, GET_DUMP_ALL, 10, 24), RES_ERR);
    ASSERT_EQ(e.takeCalls, 1);           /* ONE attempt, not one per module */
    ASSERT_EQ(e.giveCalls, 0);
    ASSERT_EQ(e.writes, 0);

    mock_init(&e, 1);
    ASSERT_EQ(new_syslog_all_set(&e, 10, 18), RES_ERR);
    ASSERT_EQ(e.takeCalls, 1);
    ASSERT_EQ(e.giveCalls, 0);
    ASSERT_EQ(e.writes, 0);

    mock_init(&e, 1);
    ASSERT_EQ(new_sd_info(&e, 1, 40), RES_ERR);
    ASSERT_EQ(e.takeCalls, 1);
    ASSERT_EQ(e.giveCalls, 0);
    ASSERT_EQ(e.writes, 0);
}

/* ==========================================================================
 * THE LOOP SHAPES -- one hold for the whole dump, at any module count
 * ========================================================================== */

TEST(loop_shapes_hold_the_buffer_exactly_once_at_every_module_count)
{
    /* Swept rather than pinned to LOG_MODULE_COUNT (fidelity note 4). The
     * count=0 end matters on its own: the pair must still be exactly one each
     * when the loop body never runs. */
    for (int n = 0; n <= 12; n++) {
        MockEnv e;

        mock_init(&e, 0);
        ASSERT_EQ(new_syslog_get(&e, GET_DUMP_ALL, n, 24), RES_OK);
        assert_balanced_single_hold(&e);
        ASSERT_EQ(e.writes, n);          /* one line per module, one hold total */

        mock_init(&e, 0);
        ASSERT_EQ(new_syslog_all_set(&e, n, 18), RES_OK);
        assert_balanced_single_hold(&e);
        ASSERT_EQ(e.writes, n);
    }
}

TEST(mutation_per_iteration_take_give_pairs_but_is_not_the_fixed_shape)
{
    MockEnv e;
    const int n = 10;

    mock_init(&e, 0);
    ASSERT_EQ(per_iteration_syslog_get(&e, n, 24), RES_OK);

    /* It BALANCES -- take == give, nothing leaked. A test that checked only
     * that would call this correct. */
    ASSERT_EQ(e.held, 0);
    ASSERT_EQ(e.takeCalls, e.giveCalls);
    ASSERT_EQ(e.heldMax, 1);

    /* What separates it from the fix: N acquisitions of the shared mutex for
     * one query, and the hold dropped between lines so a peer callback on the
     * other transport can interleave its own reply into the dump. */
    ASSERT_EQ(e.takeCalls, n);
    ASSERT_EQ(e.giveCalls, n);

    /* The fixed shape, same inputs, same replies, ONE acquisition. */
    MockEnv f;
    mock_init(&f, 0);
    ASSERT_EQ(new_syslog_get(&f, GET_DUMP_ALL, n, 24), RES_OK);
    ASSERT_EQ(f.takeCalls, 1);
    ASSERT_EQ(f.giveCalls, 1);
    ASSERT_EQ(f.writes, e.writes);       /* identical output, fewer locks */
    ASSERT_EQ(f.bytes, e.bytes);
}

/* ==========================================================================
 * WIRE BEHAVIOUR IS UNCHANGED -- #1098 moves storage, nothing else.
 * ========================================================================== */

TEST(fix_changes_where_the_reply_lives_not_what_is_written)
{
    for (int n = 0; n <= 12; n++) {
        MockEnv oldE, newE;

        mock_init(&oldE, 0);
        mock_init(&newE, 0);
        ASSERT_EQ(old_syslog_get(&oldE, GET_DUMP_ALL, n, 24),
                  new_syslog_get(&newE, GET_DUMP_ALL, n, 24));
        ASSERT_EQ(oldE.writes, newE.writes);
        ASSERT_EQ(oldE.bytes, newE.bytes);

        mock_init(&oldE, 0);
        mock_init(&newE, 0);
        ASSERT_EQ(old_syslog_all_set(&oldE, n, 18),
                  new_syslog_all_set(&newE, n, 18));
        ASSERT_EQ(oldE.writes, newE.writes);
        ASSERT_EQ(oldE.bytes, newE.bytes);
    }

    /* The one-shot sites, across the interesting snprintf returns. The old
     * log-level sites clamped against sizeof(buf) (80 / 48) and the new ones
     * against SCPI_RESPONSE_BUF_SIZE, so the clamp DIVERGES for a would-be
     * line longer than the old array -- but no real line comes close
     * ("GENERAL: 3 (ceiling 3)\r\n" is 24 B), so the lengths compared here are
     * the realistic ones. See the PR body. */
    const int lens[] = { -1, 0, 1, 24, 40, 79 };
    for (unsigned i = 0; i < sizeof(lens) / sizeof(lens[0]); i++) {
        MockEnv oldE, newE;

        mock_init(&oldE, 0);
        mock_init(&newE, 0);
        ASSERT_EQ(old_syslog_set(&oldE, lens[i]), new_syslog_set(&newE, lens[i]));
        ASSERT_EQ(oldE.writes, newE.writes);
        ASSERT_EQ(oldE.bytes, newE.bytes);

        mock_init(&oldE, 0);
        mock_init(&newE, 0);
        ASSERT_EQ(old_sd_info(&oldE, 1, lens[i]), new_sd_info(&newE, 1, lens[i]));
        ASSERT_EQ(oldE.writes, newE.writes);
        ASSERT_EQ(oldE.bytes, newE.bytes);
    }
}

/* ==========================================================================
 * THE HOLD BOUND (SysLogLevelWrite's two guards)
 *
 * Moving the dumps onto the shared buffer put LOG_MODULE_COUNT retrying writes
 * INSIDE a hold that the stack-local version never took. These pin the bound
 * that stops that being a ~10 s cross-transport stall.
 * ========================================================================== */

#define MS_PER_STALLED_WRITE  1000U   /* SCPI_WRITE_MAX_RETRIES x DELAY_MS */
#define FW_LOGLEVEL_BUDGET_MS 2000U   /* SCPI_LOGLEVEL_WRITE_BUDGET_MS */

TEST(healthy_host_is_byte_identical_and_never_trips_a_guard)
{
    /* A draining transport costs ~0 ms per write, so neither guard can fire and
     * the bounded shape must be indistinguishable from the unbounded one. */
    for (int n = 0; n <= 12; n++) {
        MockEnv bounded, unbounded;

        mock_init(&bounded, 0);
        mock_init(&unbounded, 0);
        ASSERT_EQ(new_syslog_get_bounded(&bounded, n, 24, 0U,
                                         FW_LOGLEVEL_BUDGET_MS, -1), RES_OK);
        ASSERT_EQ(new_syslog_get_unbounded(&unbounded, n, 24, 0U), RES_OK);

        ASSERT_EQ(bounded.writes, unbounded.writes);
        ASSERT_EQ(bounded.bytes, unbounded.bytes);
        ASSERT_EQ(bounded.shortWrites, 0);
        assert_balanced_single_hold(&bounded);
    }
}

TEST(guard1_short_write_stops_the_dump_instead_of_buying_nine_more_seconds)
{
    MockEnv e;

    /* Transport refuses from the very first line. */
    mock_init(&e, 0);
    ASSERT_EQ(new_syslog_get_bounded(&e, 10, 24, MS_PER_STALLED_WRITE,
                                     FW_LOGLEVEL_BUDGET_MS, 0), RES_ERR);
    ASSERT_EQ(e.writes, 0);              /* nothing completed */
    ASSERT_EQ(e.shortWrites, 1);         /* exactly one attempt, then latched */
    ASSERT_EQ(e.now, MS_PER_STALLED_WRITE);   /* ~1 s held, not ~10 s */
    assert_balanced_single_hold(&e);     /* and the buffer STILL came back */

    /* Refusing midway: the lines before it went out, the rest are abandoned.
     * msPerWrite is 0 here so guard 2 CANNOT fire -- otherwise the deadline
     * would latch first and this would silently stop testing guard 1 at all.
     * (It did, on the first draft of this case: a transposed argument made the
     * budget 3 ms, guard 2 fired on write 1, and the assertion caught it.) */
    mock_init(&e, 0);
    ASSERT_EQ(new_syslog_get_bounded(&e, 10, 24, 0U,
                                     FW_LOGLEVEL_BUDGET_MS, 3), RES_ERR);
    ASSERT_EQ(e.writes, 3);              /* lines 0,1,2 went out */
    ASSERT_EQ(e.shortWrites, 1);         /* line 3 refused, then latched */
    assert_balanced_single_hold(&e);
}

TEST(guard2_bounds_the_trickle_transport_guard1_cannot_catch)
{
    /* THE CASE GUARD 1 MISSES: every write COMPLETES, just slowly (900 ms, i.e.
     * inside its own ~1 s retry budget), so no short write ever latches. With
     * guard 1 alone a 10-module dump still holds the mutex ~9 s. */
    MockEnv unbounded;
    mock_init(&unbounded, 0);
    ASSERT_EQ(new_syslog_get_unbounded(&unbounded, 10, 24, 900U), RES_OK);
    ASSERT_EQ(unbounded.writes, 10);
    ASSERT_EQ(unbounded.now, 9000U);     /* ~9 s of held mutex */

    /* Guard 2 stops it at the budget: 2000 ms allows writes at t=0, 900, 1800;
     * the check before the 4th sees 2700 >= 2000 and latches. */
    MockEnv e;
    mock_init(&e, 0);
    ASSERT_EQ(new_syslog_get_bounded(&e, 10, 24, 900U,
                                     FW_LOGLEVEL_BUDGET_MS, -1), RES_ERR);
    ASSERT_EQ(e.writes, 3);
    ASSERT_EQ(e.shortWrites, 0);         /* guard 1 never fired -- guard 2 did */
    ASSERT_TRUE(e.now < unbounded.now);  /* strictly shorter hold */
    ASSERT_TRUE(e.now <= FW_LOGLEVEL_BUDGET_MS + MS_PER_STALLED_WRITE);
    assert_balanced_single_hold(&e);
}

TEST(an_aborted_dump_still_releases_the_buffer_on_every_guard)
{
    /* The property that matters most: a latched dump must not become a leak.
     * Both guards, and the both-at-once case, still give exactly once. */
    MockEnv e;

    mock_init(&e, 0);
    ASSERT_EQ(new_syslog_get_bounded(&e, 10, 24, MS_PER_STALLED_WRITE,
                                     FW_LOGLEVEL_BUDGET_MS, 0), RES_ERR);
    assert_balanced_single_hold(&e);

    mock_init(&e, 0);
    ASSERT_EQ(new_syslog_get_bounded(&e, 10, 24, 900U,
                                     FW_LOGLEVEL_BUDGET_MS, -1), RES_ERR);
    assert_balanced_single_hold(&e);

    /* Zero modules: the loop never runs, so neither guard can fire -- and the
     * pair must still be exactly one each. */
    mock_init(&e, 0);
    ASSERT_EQ(new_syslog_get_bounded(&e, 0, 24, MS_PER_STALLED_WRITE,
                                     FW_LOGLEVEL_BUDGET_MS, 0), RES_OK);
    assert_balanced_single_hold(&e);
    ASSERT_EQ(e.writes, 0);
}

TEST(the_bound_is_what_changed_unbounded_shape_reaches_ten_seconds)
{
    /* The canary for the bound, mirroring the old/new canary above: the shape
     * this PR first shipped pairs perfectly AND holds for ~10 s, so pairing
     * assertions alone could never have caught it. */
    MockEnv unbounded;
    mock_init(&unbounded, 0);
    ASSERT_EQ(new_syslog_get_unbounded(&unbounded, 10, 24,
                                       MS_PER_STALLED_WRITE), RES_OK);
    assert_balanced_single_hold(&unbounded);        /* pairs fine... */
    ASSERT_EQ(unbounded.now, 10U * MS_PER_STALLED_WRITE);   /* ...for ~10 s */

    MockEnv bounded;
    mock_init(&bounded, 0);
    ASSERT_EQ(new_syslog_get_bounded(&bounded, 10, 24, MS_PER_STALLED_WRITE,
                                     FW_LOGLEVEL_BUDGET_MS, 0), RES_ERR);
    assert_balanced_single_hold(&bounded);
    ASSERT_TRUE(bounded.now * 5U < unbounded.now);  /* an order of magnitude */
}

int main(void)
{
    printf("#1098 -- SCPI reply buffers: shared-response-buffer Take/Give pairing\n");
    printf("---------------------------------------------\n");
    RUN(canary_old_stack_buffer_shapes_never_take_or_give);
    RUN(new_one_shot_shapes_pair_exactly_once);
    RUN(new_sd_info_format_error_path_still_gives);
    RUN(mutation_sd_info_that_forgets_the_error_give_leaks_the_hold);
    RUN(new_paths_that_return_before_the_take_neither_take_nor_give);
    RUN(null_take_is_never_followed_by_a_give);
    RUN(loop_shapes_hold_the_buffer_exactly_once_at_every_module_count);
    RUN(mutation_per_iteration_take_give_pairs_but_is_not_the_fixed_shape);
    RUN(fix_changes_where_the_reply_lives_not_what_is_written);
    RUN(healthy_host_is_byte_identical_and_never_trips_a_guard);
    RUN(guard1_short_write_stops_the_dump_instead_of_buying_nine_more_seconds);
    RUN(guard2_bounds_the_trickle_transport_guard1_cannot_catch);
    RUN(an_aborted_dump_still_releases_the_buffer_on_every_guard);
    RUN(the_bound_is_what_changed_unbounded_shape_reaches_ten_seconds);
    return TEST_SUMMARY();
}
