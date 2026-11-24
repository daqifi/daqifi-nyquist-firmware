/* ==========================================================================
 * test_1206_nvm_bounded_waits.c -- firmware#1206: nvm.c's waits are bounded,
 * and nvm_ErasePage reports a failed erase.
 *
 * WHAT IS UNDER TEST: the REAL firmware/src/HAL/NVM/nvm.c, compiled on the
 * host against stubs/nvm_1206/ (see the README there). Not a model -- the
 * Makefile links this file with $(NVM1206_SRC), which defaults to the real
 * source and can be pointed at any other revision of it.
 *
 * Before #1206, each of nvm_ErasePage / nvm_WriteWordtoAddr /
 * nvm_WriteRowtoAddr spun on three conditions with no exit but success --
 * DMA quiescing after a suspend, the NVM controller going idle, and the
 * completion callback -- with DMA SUSPENDED for the whole time. Nine spins.
 * And nvm_ErasePage returned true from both branches of its erase verify.
 *
 * HOW A HANG IS OBSERVED: every case runs in a forked child. A child that has
 * not reported back within HANG_SECONDS of wall time is killed by the parent
 * and recorded as `completed == 0`. That is the red this file shows against
 * the pre-#1206 source: its stuck cases hang rather than fail.
 *
 * THE CLOCK: simulated "hardware time" is a CP0 count that advances by
 * SIM_STEP (10 us of core-timer cycles at the firmware's own
 * DAQIFI_CORE_TIMER_HZ) on every register / PLIB access, including each
 * _CP0_GET_COUNT(), AND asynchronously from a SIGALRM interval timer that
 * plays the part of time passing while the CPU spins on something that is
 * not a register -- the pre-#1206 `while (gNvmOperationDone == false);` reads
 * only a RAM flag, so without that asynchronous tick an operation would never
 * complete in simulation and even a NORMAL save would "hang" (measured: all
 * 22 cases hung against origin/main before this tick existed, the positive
 * controls included -- an instrument artifact, not the defect). The signal
 * handler is the NVM completion interrupt: it delivers the callback exactly
 * as FLASH_CONTROL_Handler -> NVM_InterruptHandler does on the part, and it is
 * deferred while a stub is running, the way an ISR is held off by a masked
 * section. Operation durations are stated in CP0 cycles from DS60001320G
 * Table 37-12 (typical FRC cycles) at the FRC's slow -10% corner (Table
 * 37-20), so the positive controls are the slowest LEGITIMATE operations the
 * budget must accept.
 * ========================================================================== */
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

#include <stdatomic.h>
#include <time.h>
#include <sys/time.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <poll.h>
#include "test_framework.h"
#include "nvm.h"            /* the real one -- its includes resolve to stubs */
#include "xc.h"
#include "semphr.h"
#include "clock_config.h"   /* forwarded to the real one: DAQIFI_CORE_TIMER_HZ */

/* ---- simulated time ----------------------------------------------------- */

#define CP0_PER_MS     (DAQIFI_CORE_TIMER_HZ / 1000UL)
#define CP0_PER_US     (DAQIFI_CORE_TIMER_HZ / 1000000UL)
#define MS(x)          ((uint32_t)(CP0_PER_MS * (x)))
#define US(x)          ((uint32_t)(CP0_PER_US * (x)))
#define SIM_STEP       US(10)
#define NEVER          0xFFFFFFFFu

/* Slowest legitimate durations (typical FRC cycles / 8 MHz / 0.9):
 *   page erase  D142 128,453 -> 17.8 ms
 *   row program D136  66,813 ->  9.3 ms
 *   word        D138     383 ->   53 us                                    */
#define ERASE_CYCLES   MS(18)
#define ROW_CYCLES     MS(10)
#define WORD_CYCLES    US(60)
#define DMA_QUIESCE    US(20)

/* A timed-out call must come back well inside this. The firmware budget is
 * per wait; one stuck wait plus the others' normal durations is far less. */
#define BOUNDED_WITHIN MS(1000)

#define HANG_SECONDS   2

/* ---- fake flash ---------------------------------------------------------- */

#define FLASH_BASE     0x9D1E0000u
#define FLASH_SIZE     (2u * NVM_FLASH_ROWSIZE)
static uint8_t g_flash[FLASH_SIZE];

/* ---- scenario ------------------------------------------------------------ */

typedef enum { ERASE_OK, ERASE_REFUSED, ERASE_LOW_BYTE_ONLY } EraseEffect;

typedef struct {
    bool        dmaSuspendedOnEntry; /* caller already had DMA suspended */
    uint32_t    dmaQuiesceCycles;    /* NEVER: DMABUSY stays set */
    uint32_t    preBusyCycles;       /* controller busy at entry; NEVER: stuck */
    uint32_t    opCycles;            /* 0: per-kind default; NEVER: WR stuck */
    bool        irqLost;             /* completion interrupt never delivered */
    bool        staleIrqAfterStart;  /* a stale completion lands mid-operation */
    EraseEffect eraseEffect;
    uint32_t    startNow;            /* initial CP0 count */
    uint32_t    preemptCycles;       /* one CP0 read mid-operation jumps this far */
} Scenario;

typedef enum { OP_ERASE, OP_WORD, OP_ROW } OpKind;

static Scenario S;
static uint32_t g_now;
static uint32_t g_scenarioStart;

static nvm1206_DmaconBits_t g_dmacon;
static uint32_t g_setSink;
static uint32_t g_clrSink;
static uint32_t g_suspendAt;

static bool           g_opActive;
static uint32_t       g_opStart;
static uint32_t       g_opDuration;
static OpKind         g_opKind;
static uint32_t       g_opAddr;
static uint32_t       g_opWord;
static const uint8_t *g_opSrc;
static bool           g_staleFired;
static bool           g_preempted;
static int            g_cp0ReadsDuringOp;

static NVM_CALLBACK g_cb;
static uintptr_t    g_cbCtx;

static int  g_opsStarted;
static bool g_readWhileBusy;
static int  g_lockDepth;
static int  g_lockMax;

static uint8_t *flash_ptr(uint32_t addr)
{
    if (addr < FLASH_BASE || addr - FLASH_BASE >= FLASH_SIZE) {
        fprintf(stderr, "flash address 0x%08x out of the fake range\n", addr);
        abort();
    }
    return &g_flash[addr - FLASH_BASE];
}

/* A write to DMACONSET/DMACONCLR lands in a sink; apply it before the next
 * access, which is what a SET/CLR alias register does to DMACON. */
static void commit(void)
{
    if (g_setSink & _DMACON_SUSPEND_MASK) {
        if (!g_dmacon.SUSPEND) {
            g_dmacon.SUSPEND = 1;
            g_suspendAt = g_now;
        }
    }
    g_setSink = 0;
    if (g_clrSink & _DMACON_SUSPEND_MASK) {
        g_dmacon.SUSPEND = 0;
        g_dmacon.DMABUSY = 1;   /* DMA runs again */
    }
    g_clrSink = 0;
}

static bool controller_busy(void)
{
    if (g_opActive) {
        return true;
    }
    if (S.preBusyCycles == NEVER) {
        return true;
    }
    return (uint32_t)(g_now - g_scenarioStart) < S.preBusyCycles;
}

static void fire_completion_irq(void)
{
    if (g_cb != NULL) {
        g_cb(g_cbCtx);
    }
}

static void complete_op(void)
{
    switch (g_opKind) {
        case OP_ERASE:
            if (S.eraseEffect == ERASE_REFUSED) {
                break;                      /* write-protected: page intact */
            }
            memset(g_flash, 0xFF, sizeof(g_flash));
            if (S.eraseEffect == ERASE_LOW_BYTE_ONLY) {
                /* the word reads 0x000000FF: its first BYTE looks erased */
                memset(g_flash + 1, 0x00, 3);
            }
            break;
        case OP_WORD:
            memcpy(flash_ptr(g_opAddr), &g_opWord, sizeof(g_opWord));
            break;
        case OP_ROW:
            memcpy(flash_ptr(g_opAddr), g_opSrc, NVM_FLASH_ROWSIZE);
            break;
    }
}

static void sim_tick(void)
{
    commit();
    g_now += SIM_STEP;

    if (g_dmacon.SUSPEND && g_dmacon.DMABUSY &&
        S.dmaQuiesceCycles != NEVER &&
        (uint32_t)(g_now - g_suspendAt) >= S.dmaQuiesceCycles) {
        g_dmacon.DMABUSY = 0;
    }

    if (g_opActive) {
        if (S.staleIrqAfterStart && !g_staleFired) {
            g_staleFired = true;
            fire_completion_irq();          /* an EARLIER op's late interrupt */
        }
        if (g_opDuration != NEVER &&
            (uint32_t)(g_now - g_opStart) >= g_opDuration) {
            complete_op();
            g_opActive = false;             /* WR hardware-cleared */
            if (!S.irqLost) {
                fire_completion_irq();
            }
        }
    }
}

static void start_op(OpKind kind, uint32_t addr)
{
    commit();
    g_opsStarted++;
    g_opActive = true;      /* WR is set before the PLIB start call returns */
    g_opStart = g_now;
    g_opKind = kind;
    g_opAddr = addr;
    if (S.opCycles != 0) {
        g_opDuration = S.opCycles;
    } else {
        g_opDuration = (kind == OP_ERASE) ? ERASE_CYCLES
                     : (kind == OP_ROW)   ? ROW_CYCLES : WORD_CYCLES;
    }
}

/* ---- the asynchronous clock / completion interrupt ----------------------
 * SIGALRM from an interval timer advances hardware time while the code under
 * test is not inside a stub; while it IS (g_inStub), the tick is deferred and
 * run at stub exit, so the simulation state is never touched by two contexts
 * at once. The handler runs sim_tick(), which delivers nvm.c's completion
 * callback when an operation finishes -- the ISR. */
static volatile sig_atomic_t g_inStub;
static volatile sig_atomic_t g_tickPending;

static void stub_enter(void)
{
    g_inStub = 1;
    atomic_signal_fence(memory_order_seq_cst);
}

static void stub_exit(void)
{
    atomic_signal_fence(memory_order_seq_cst);
    g_inStub = 0;
    atomic_signal_fence(memory_order_seq_cst);
    while (g_tickPending) {
        g_inStub = 1;
        atomic_signal_fence(memory_order_seq_cst);
        g_tickPending = 0;
        sim_tick();
        atomic_signal_fence(memory_order_seq_cst);
        g_inStub = 0;
        atomic_signal_fence(memory_order_seq_cst);
    }
}

static void on_async_tick(int sig)
{
    (void)sig;
    if (g_inStub) {
        g_tickPending = 1;
        return;
    }
    g_inStub = 1;
    atomic_signal_fence(memory_order_seq_cst);
    sim_tick();
    atomic_signal_fence(memory_order_seq_cst);
    g_inStub = 0;
}

static void async_clock(bool on)
{
    struct itimerval it;
    memset(&it, 0, sizeof(it));
    if (on) {
        it.it_interval.tv_usec = 50;     /* one SIM_STEP per 50 us of wall */
        it.it_value.tv_usec = 50;
    }
    setitimer(ITIMER_REAL, &it, NULL);
}

/* ---- stub implementations (declared in stubs/nvm_1206) ------------------- */

nvm1206_DmaconBits_t *nvm1206_DmaconBits(void)
{
    stub_enter(); sim_tick(); stub_exit();
    return &g_dmacon;
}
uint32_t *nvm1206_DmaconSet(void) { stub_enter(); commit(); stub_exit(); return &g_setSink; }
uint32_t *nvm1206_DmaconClr(void) { stub_enter(); commit(); stub_exit(); return &g_clrSink; }
uint32_t nvm1206_Cp0Count(void)
{
    stub_enter();
    /* The SECOND read during an operation: the first is the wait's own
     * start stamp, and a jump before it would hide inside `start`. */
    if (S.preemptCycles != 0 && g_opActive && !g_preempted &&
        ++g_cp0ReadsDuringOp == 2) {
        /* The waiting task was preempted (or the CPU stalled) for longer than
         * the whole budget, and the operation finished meanwhile. */
        g_preempted = true;
        g_now += S.preemptCycles;
    }
    sim_tick();
    uint32_t now = g_now;
    stub_exit();
    return now;
}

void NVM_CallbackRegister(NVM_CALLBACK callback, uintptr_t context)
{
    stub_enter();
    g_cb = callback;
    g_cbCtx = context;
    stub_exit();
}

bool NVM_IsBusy(void)
{
    stub_enter(); sim_tick(); bool busy = controller_busy(); stub_exit();
    return busy;
}

bool NVM_PageErase(uint32_t address)
{
    stub_enter(); start_op(OP_ERASE, address); stub_exit();
    return true;
}

bool NVM_WordWrite(uint32_t data, uint32_t address)
{
    stub_enter(); g_opWord = data; start_op(OP_WORD, address); stub_exit();
    return true;
}

bool NVM_RowWrite(uint32_t *data, uint32_t address)
{
    stub_enter(); g_opSrc = (const uint8_t *)data; start_op(OP_ROW, address); stub_exit();
    return true;
}

bool NVM_Read(uint32_t *data, uint32_t length, const uint32_t address)
{
    stub_enter();
    if (controller_busy()) {
        g_readWhileBusy = true;
    }
    memcpy(data, flash_ptr(address), length);   /* length is BYTES, as in plib_nvm.c */
    stub_exit();
    return true;
}

void *nvm1206_FlashKva1(uint32_t address)
{
    stub_enter();
    if (controller_busy()) {
        g_readWhileBusy = true;
    }
    void *p = flash_ptr(address);
    stub_exit();
    return p;
}

static StaticSemaphore_t *g_mutexBuf;
SemaphoreHandle_t xSemaphoreCreateRecursiveMutexStatic(StaticSemaphore_t *buf)
{
    g_mutexBuf = buf;
    return (SemaphoreHandle_t)buf;
}
int xSemaphoreTakeRecursive(SemaphoreHandle_t m, TickType_t wait)
{
    (void)wait;
    if (m == NULL || m != (SemaphoreHandle_t)g_mutexBuf) {
        abort();                        /* nvm_MutexInit() was not called */
    }
    g_lockDepth++;
    if (g_lockDepth > g_lockMax) {
        g_lockMax = g_lockDepth;
    }
    return 1;
}
int xSemaphoreGiveRecursive(SemaphoreHandle_t m)
{
    (void)m;
    g_lockDepth--;
    return 1;
}

/* ---- per-case harness ---------------------------------------------------- */

typedef struct {
    int      completed;      /* the NVM call returned (did not hang) */
    int      ret;
    int      lockDepth;      /* after the call */
    int      lockMax;
    int      suspendAfter;   /* DMACON.SUSPEND after the call */
    int      opsStarted;
    int      readWhileBusy;  /* verify read while the controller was busy */
    uint32_t elapsed;        /* simulated CP0 cycles spent in the call */
    int      ret2;           /* second call, for the sequence cases */
} Outcome;

static void sim_reset(Scenario s)
{
    S = s;
    if (S.dmaQuiesceCycles == 0) {
        S.dmaQuiesceCycles = DMA_QUIESCE;
    }
    g_now = s.startNow;
    g_scenarioStart = g_now;
    g_dmacon.SUSPEND = s.dmaSuspendedOnEntry ? 1u : 0u;
    g_dmacon.DMABUSY = s.dmaSuspendedOnEntry ? 0u : 1u;
    g_setSink = 0;
    g_clrSink = 0;
    g_suspendAt = 0;
    g_opActive = false;
    g_staleFired = false;
    g_preempted = false;
    g_cp0ReadsDuringOp = 0;
    g_cb = NULL;
    g_opsStarted = 0;
    g_readWhileBusy = false;
    g_lockDepth = 0;
    g_lockMax = 0;
    memset(g_flash, 0x5A, sizeof(g_flash));   /* NOT erased */
}

static bool call_op(OpKind kind)
{
    static uint8_t row[NVM_FLASH_ROWSIZE];
    switch (kind) {
        case OP_ERASE:
            return nvm_ErasePage(FLASH_BASE);
        case OP_WORD:
            return nvm_WriteWordtoAddr(FLASH_BASE, 0x12345678u);
        case OP_ROW:
        default:
            for (size_t i = 0; i < sizeof(row); i++) {
                row[i] = (uint8_t)(i * 7u + 1u);
            }
            return nvm_WriteRowtoAddr(FLASH_BASE, row);
    }
}

/* The child's work: one call, or the two-call stale-completion sequence. */
typedef enum { SEQ_SINGLE, SEQ_LATE_IRQ_THEN_SECOND } Sequence;

static OpKind   g_caseKind;
static Scenario g_caseScenario;
static Sequence g_caseSeq;
static bool     g_caseStaleOnSecond;   /* SEQ_LATE_IRQ_THEN_SECOND only */

static void child_body(Outcome *o)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_async_tick;
    sa.sa_flags = SA_RESTART;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGALRM, &sa, NULL);

    stub_enter();
    sim_reset(g_caseScenario);
    nvm_MutexInit();
    uint32_t t0 = g_now;
    stub_exit();

    async_clock(true);
    o->ret = call_op(g_caseKind);
    async_clock(false);

    stub_enter();
    commit();
    o->elapsed = (uint32_t)(g_now - t0);

    if (g_caseSeq == SEQ_LATE_IRQ_THEN_SECOND) {
        /* The first op's completion interrupt was masked; the operation
         * itself finished in hardware. Interrupts come back and the pending
         * interrupt is delivered NOW -- after the first call gave up. */
        while (g_opActive) {
            sim_tick();
        }
        if (!g_caseStaleOnSecond) {
            fire_completion_irq();
        }

        /* The second call is a normal operation, except that -- when the
         * case asks for it -- that stale interrupt is still PENDING when the
         * second operation starts and lands mid-operation instead. nvm.c
         * registers its handler again on entry, so clearing g_cb here hides
         * nothing. */
        Scenario second = g_caseScenario;
        second.irqLost = false;
        second.staleIrqAfterStart = g_caseStaleOnSecond;
        second.startNow = g_now;
        sim_reset(second);
        stub_exit();

        async_clock(true);
        o->ret2 = call_op(g_caseKind);
        async_clock(false);
        stub_enter();
        commit();
    }

    o->lockDepth = g_lockDepth;
    o->lockMax = g_lockMax;
    o->suspendAfter = (int)g_dmacon.SUSPEND;
    o->opsStarted = g_opsStarted;
    o->readWhileBusy = g_readWhileBusy ? 1 : 0;
    o->completed = 1;
    stub_exit();
}

static Outcome run_isolated(OpKind kind, Scenario s, Sequence seq)
{
    Outcome o;
    memset(&o, 0, sizeof(o));
    g_caseKind = kind;
    g_caseScenario = s;
    g_caseSeq = seq;

    int fds[2];
    if (pipe(fds) != 0) {
        perror("pipe");
        exit(2);
    }
    fflush(stdout);
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        exit(2);
    }
    if (pid == 0) {
        close(fds[0]);
        Outcome c;
        memset(&c, 0, sizeof(c));
        child_body(&c);
        ssize_t w = write(fds[1], &c, sizeof(c));
        _exit(w == (ssize_t)sizeof(c) ? 0 : 3);
    }
    close(fds[1]);
    struct pollfd pfd = { .fd = fds[0], .events = POLLIN };
    int ready = poll(&pfd, 1, HANG_SECONDS * 1000);
    ssize_t n = -1;
    if (ready == 1) {
        n = read(fds[0], &o, sizeof(o));
    } else {
        kill(pid, SIGKILL);
    }
    close(fds[0]);
    int status = 0;
    waitpid(pid, &status, 0);
    if (n != (ssize_t)sizeof(o)) {
        memset(&o, 0, sizeof(o));
        if (ready != 1) {
            printf("    (child HUNG in the NVM call -- killed after %d s)\n",
                   HANG_SECONDS);
        } else {
            printf("    (child died, status 0x%x)\n", status);
        }
    }
    return o;
}

/* A wait that never completes must come back false, quickly, with DMA put
 * back the way the caller had it and the NVM mutex released. */
static void assert_bounded_failure(Outcome o, bool dmaSuspendedOnEntry,
                                   bool opMustNotStart)
{
    ASSERT_TRUE(o.completed);
    if (!o.completed) {
        return;
    }
    ASSERT_FALSE(o.ret);
    ASSERT_EQ(o.lockDepth, 0);
    ASSERT_TRUE(o.lockMax >= 1);
    ASSERT_EQ(o.suspendAfter, dmaSuspendedOnEntry ? 1 : 0);
    ASSERT_TRUE(o.elapsed <= BOUNDED_WITHIN);
    if (opMustNotStart) {
        ASSERT_EQ(o.opsStarted, 0);
    }
}

static void assert_clean_success(Outcome o, bool dmaSuspendedOnEntry)
{
    ASSERT_TRUE(o.completed);
    if (!o.completed) {
        return;
    }
    ASSERT_TRUE(o.ret);
    ASSERT_EQ(o.lockDepth, 0);
    ASSERT_EQ(o.suspendAfter, dmaSuspendedOnEntry ? 1 : 0);
    ASSERT_EQ(o.opsStarted, 1);
    ASSERT_EQ(o.readWhileBusy, 0);
}

static const Scenario NORMAL = { 0 };

/* ==========================================================================
 * Positive controls: the slowest legitimate operations still succeed.
 * ========================================================================== */

TEST(erase_at_slowest_datasheet_time_succeeds) {
    assert_clean_success(run_isolated(OP_ERASE, NORMAL, SEQ_SINGLE), false);
}

TEST(row_write_at_slowest_datasheet_time_succeeds) {
    assert_clean_success(run_isolated(OP_ROW, NORMAL, SEQ_SINGLE), false);
}

TEST(word_write_succeeds) {
    assert_clean_success(run_isolated(OP_WORD, NORMAL, SEQ_SINGLE), false);
}

TEST(erase_five_times_slower_than_datasheet_still_succeeds) {
    /* Margin, not just the datasheet point: a budget sized at "typical"
     * would fail a slow part. */
    Scenario s = NORMAL;
    s.opCycles = 5u * ERASE_CYCLES;
    assert_clean_success(run_isolated(OP_ERASE, s, SEQ_SINGLE), false);
}

TEST(erase_succeeds_across_the_cp0_wrap) {
    Scenario s = NORMAL;
    s.startNow = 0xFFFFFFFFu - MS(5);  /* the 18 ms erase straddles the wrap */
    assert_clean_success(run_isolated(OP_ERASE, s, SEQ_SINGLE), false);
}

TEST(completion_seen_after_long_preemption_is_not_a_timeout) {
    /* The wait is preempted for 2 s -- far past its budget -- and the erase
     * completes during that gap. The condition must be re-read at expiry, or
     * a SUCCESSFUL erase is reported as a failure. */
    Scenario s = NORMAL;
    s.preemptCycles = MS(2000);
    Outcome o = run_isolated(OP_ERASE, s, SEQ_SINGLE);
    ASSERT_TRUE(o.completed);
    ASSERT_TRUE(o.ret);
    ASSERT_EQ(o.lockDepth, 0);
    ASSERT_EQ(o.suspendAfter, 0);
}

TEST(success_leaves_caller_suspended_dma_suspended) {
    Scenario s = NORMAL;
    s.dmaSuspendedOnEntry = true;
    assert_clean_success(run_isolated(OP_ERASE, s, SEQ_SINGLE), true);
}

/* ==========================================================================
 * The nine spins: each one stuck, for each entry point.
 * ========================================================================== */

static void stuck_dma_quiesce(OpKind kind) {
    Scenario s = NORMAL;
    s.dmaQuiesceCycles = NEVER;
    assert_bounded_failure(run_isolated(kind, s, SEQ_SINGLE), false, true);
}

static void stuck_controller_busy(OpKind kind) {
    Scenario s = NORMAL;
    s.preBusyCycles = NEVER;
    assert_bounded_failure(run_isolated(kind, s, SEQ_SINGLE), false, true);
}

static void lost_completion_irq(OpKind kind) {
    /* The critical-section case: the operation finishes in hardware but its
     * priority-1 interrupt is masked, so the callback never runs. */
    Scenario s = NORMAL;
    s.irqLost = true;
    assert_bounded_failure(run_isolated(kind, s, SEQ_SINGLE), false, false);
}

TEST(erase_dma_never_quiesces_returns_false) { stuck_dma_quiesce(OP_ERASE); }
TEST(erase_controller_never_idle_returns_false) { stuck_controller_busy(OP_ERASE); }
TEST(erase_completion_irq_lost_returns_false) { lost_completion_irq(OP_ERASE); }
TEST(word_dma_never_quiesces_returns_false) { stuck_dma_quiesce(OP_WORD); }
TEST(word_controller_never_idle_returns_false) { stuck_controller_busy(OP_WORD); }
TEST(word_completion_irq_lost_returns_false) { lost_completion_irq(OP_WORD); }
TEST(row_dma_never_quiesces_returns_false) { stuck_dma_quiesce(OP_ROW); }
TEST(row_controller_never_idle_returns_false) { stuck_controller_busy(OP_ROW); }
TEST(row_completion_irq_lost_returns_false) { lost_completion_irq(OP_ROW); }

TEST(erase_operation_never_completes_returns_false) {
    /* WR never clears AND no interrupt: the controller itself is wedged. */
    Scenario s = NORMAL;
    s.opCycles = NEVER;
    assert_bounded_failure(run_isolated(OP_ERASE, s, SEQ_SINGLE), false, false);
}

TEST(timeout_leaves_caller_suspended_dma_suspended) {
    /* Restore means "as the caller had it", not "resume": a caller that had
     * DMA suspended must not have it resumed under it by a timeout path. */
    Scenario s = NORMAL;
    s.dmaSuspendedOnEntry = true;
    s.irqLost = true;
    assert_bounded_failure(run_isolated(OP_ROW, s, SEQ_SINGLE), true, false);
}

TEST(timeout_is_bounded_across_the_cp0_wrap) {
    Scenario s = NORMAL;
    s.irqLost = true;
    s.startNow = 0xFFFFFFFFu - MS(50);
    assert_bounded_failure(run_isolated(OP_ERASE, s, SEQ_SINGLE), false, false);
}

/* ==========================================================================
 * The erase verify.
 * ========================================================================== */

TEST(erase_refused_page_returns_false) {
    /* A write-protected page: the controller completes the "erase" and the
     * page is untouched. Pre-#1206 this returned true. */
    Scenario s = NORMAL;
    s.eraseEffect = ERASE_REFUSED;
    Outcome o = run_isolated(OP_ERASE, s, SEQ_SINGLE);
    ASSERT_TRUE(o.completed);
    ASSERT_FALSE(o.ret);
    ASSERT_EQ(o.lockDepth, 0);
    ASSERT_EQ(o.suspendAfter, 0);
}

TEST(erase_verify_reads_the_whole_word) {
    /* First byte 0xFF, the rest 0x00: a verify that reads one byte (the
     * pre-#1206 NVM_Read(&w, 1, addr)) into an 0xFF-filled word would call
     * this erased. Paired with erase_at_slowest_datasheet_time_succeeds,
     * which a one-byte read into a ZERO-filled word fails. */
    Scenario s = NORMAL;
    s.eraseEffect = ERASE_LOW_BYTE_ONLY;
    Outcome o = run_isolated(OP_ERASE, s, SEQ_SINGLE);
    ASSERT_TRUE(o.completed);
    ASSERT_FALSE(o.ret);
}

/* ==========================================================================
 * A late completion from a timed-out operation must not end the NEXT
 * operation's wait early.
 * ========================================================================== */

TEST(late_irq_from_timed_out_op_does_not_end_next_wait_early) {
    Scenario s = NORMAL;
    s.irqLost = true;                /* first call times out on completion */
    Outcome o = run_isolated(OP_ERASE, s, SEQ_LATE_IRQ_THEN_SECOND);
    ASSERT_TRUE(o.completed);
    if (!o.completed) {
        return;
    }
    ASSERT_FALSE(o.ret);
    ASSERT_TRUE(o.ret2);
    ASSERT_EQ(o.readWhileBusy, 0);   /* the second verify ran after completion */
    ASSERT_EQ(o.lockDepth, 0);
    ASSERT_EQ(o.suspendAfter, 0);
}

TEST(stale_irq_landing_mid_operation_does_not_end_wait_early) {
    /* The stale interrupt is still pending when the next operation starts
     * and lands AFTER nvm.c cleared its flag -- only the WR check stops the
     * wait from returning while the erase is still running. */
    Scenario s = NORMAL;
    s.irqLost = true;
    g_caseStaleOnSecond = true;
    Outcome o = run_isolated(OP_ERASE, s, SEQ_LATE_IRQ_THEN_SECOND);
    g_caseStaleOnSecond = false;
    ASSERT_TRUE(o.completed);
    if (!o.completed) {
        return;
    }
    ASSERT_TRUE(o.ret2);
    ASSERT_EQ(o.readWhileBusy, 0);
    ASSERT_EQ(o.lockDepth, 0);
}

int main(void) {
    RUN(erase_at_slowest_datasheet_time_succeeds);
    RUN(row_write_at_slowest_datasheet_time_succeeds);
    RUN(word_write_succeeds);
    RUN(erase_five_times_slower_than_datasheet_still_succeeds);
    RUN(erase_succeeds_across_the_cp0_wrap);
    RUN(completion_seen_after_long_preemption_is_not_a_timeout);
    RUN(success_leaves_caller_suspended_dma_suspended);
    RUN(erase_dma_never_quiesces_returns_false);
    RUN(erase_controller_never_idle_returns_false);
    RUN(erase_completion_irq_lost_returns_false);
    RUN(word_dma_never_quiesces_returns_false);
    RUN(word_controller_never_idle_returns_false);
    RUN(word_completion_irq_lost_returns_false);
    RUN(row_dma_never_quiesces_returns_false);
    RUN(row_controller_never_idle_returns_false);
    RUN(row_completion_irq_lost_returns_false);
    RUN(erase_operation_never_completes_returns_false);
    RUN(timeout_leaves_caller_suspended_dma_suspended);
    RUN(timeout_is_bounded_across_the_cp0_wrap);
    RUN(erase_refused_page_returns_false);
    RUN(erase_verify_reads_the_whole_word);
    RUN(late_irq_from_timed_out_op_does_not_end_next_wait_early);
    RUN(stale_irq_landing_mid_operation_does_not_end_wait_early);
    return TEST_SUMMARY();
}
