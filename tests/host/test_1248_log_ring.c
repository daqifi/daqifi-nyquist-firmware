/* ==========================================================================
 * test_1248_log_ring.c -- SYSTem:LOG? must not lose a record to a short write
 *
 * #1248: LogMessageDump popped each record BEFORE writing it and ignored the
 * byte count, so a short write lost the record silently.
 *
 * Two layers, both REAL code:
 *   1. LogRing.h, included unchanged -- the peek / pop-if-unchanged / push /
 *      clear bookkeeping and the dumping-task set.
 *   2. LogMessageDump itself, spliced out of Util/Logger.c by the Makefile into
 *      gen_1248_log_dump.h and compiled against the prelude below (a stub
 *      transport, a stub mutex, a counting Logger_ResetOneShots). So "a short
 *      write keeps the record" and "one-shots are re-armed only by a complete
 *      dump" are assertions on the shipped function, not on a copy of it.
 *
 * Concurrency is modelled by the stub transport: its write callback runs with
 * the log mutex RELEASED, exactly where another task can run in firmware, so
 * that is where the tests push, clear or pop.
 * ========================================================================== */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "test_framework.h"

/* ---- sizes: the firmware's, read from Logger.h by the Makefile ----------- */
#ifndef LOG_MAX_ENTRY_COUNT
#error "the Makefile passes -DLOG_MAX_ENTRY_COUNT read from Logger.h"
#endif
#ifndef LOG_MESSAGE_SIZE
#error "the Makefile passes -DLOG_MESSAGE_SIZE read from Logger.h"
#endif
#define LOG_RING_CAPACITY LOG_MAX_ENTRY_COUNT
#define LOG_RING_MSG_SIZE LOG_MESSAGE_SIZE
#include "LogRing.h"

/* ---- prelude the spliced LogMessageDump compiles against ----------------- */
typedef int scpi_result_t;
struct scpi_t;
typedef struct {
    size_t (*write)(struct scpi_t *ctx, const char *data, size_t len);
    scpi_result_t (*flush)(struct scpi_t *ctx);
} scpi_interface_t;
typedef struct scpi_t {
    scpi_interface_t *interface;
} scpi_t;

typedef void *SemaphoreHandle_t;
#define portMAX_DELAY 0xFFFFFFFFu
#define pdTRUE 1
static int g_mutex_held = 0;
static int xSemaphoreTake(SemaphoreHandle_t m, uint32_t t) {
    (void)m; (void)t;
    g_mutex_held++;
    return pdTRUE;
}
static int xSemaphoreGive(SemaphoreHandle_t m) {
    (void)m;
    g_mutex_held--;
    return pdTRUE;
}

typedef struct {
    LogRing ring;
    LogOwnerSet dumping;
    SemaphoreHandle_t mutex;
} LogBuffer;
static LogBuffer logBuffer;

static int g_reset_one_shots = 0;
static void Logger_ResetOneShots(void) { g_reset_one_shots++; }

#include "gen_1248_log_dump.h"   /* bool LogMessageDump(scpi_t *context) */

/* ---- the stub transport -------------------------------------------------- */
#define MAX_SENT 512
static char g_sent[MAX_SENT][LOG_MESSAGE_SIZE];
static int g_nsent;
static int g_writes;
/* Write number (1-based) that comes up short; 0 = never. */
static int g_short_on;
/* Bytes a short write accepts (a short write can deliver a prefix). */
static size_t g_short_takes;
/* Called inside the write, mutex RELEASED: a concurrent task's turn. */
static void (*g_during_write)(int writeNo);
static int g_write_saw_mutex_held;

static size_t stub_write(struct scpi_t *ctx, const char *data, size_t len) {
    (void)ctx;
    g_writes++;
    if (g_mutex_held != 0) {
        g_write_saw_mutex_held++;
    }
    if (g_during_write) {
        g_during_write(g_writes);
    }
    if (g_short_on == g_writes) {
        return g_short_takes < len ? g_short_takes : (len ? len - 1 : 0);
    }
    if (g_nsent < MAX_SENT) {
        size_t n = len < LOG_MESSAGE_SIZE - 1 ? len : LOG_MESSAGE_SIZE - 1;
        memcpy(g_sent[g_nsent], data, n);
        g_sent[g_nsent][n] = '\0';
        g_nsent++;
    }
    return len;
}
static scpi_result_t stub_flush(struct scpi_t *ctx) { (void)ctx; return 0; }

static scpi_interface_t g_iface = { stub_write, stub_flush };
static scpi_t g_ctx = { &g_iface };

/* Records are "r<N>\r\n" so order and identity are checkable. */
static void push_id(int id) {
    char m[32];
    int n = snprintf(m, sizeof m, "r%d\r\n", id);
    LogRing_Push(&logBuffer.ring, m, (size_t)n);
}
static int sent_id(int i) {
    int id = -1;
    sscanf(g_sent[i], "r%d", &id);
    return id;
}
static int tail_id(void) {
    char buf[LOG_MESSAGE_SIZE];
    uint32_t seq;
    int id = -1;
    if (!LogRing_Peek(&logBuffer.ring, buf, sizeof buf, &seq)) {
        return -1;
    }
    sscanf(buf, "r%d", &id);
    return id;
}

static void reset(void) {
    memset(&logBuffer, 0, sizeof logBuffer);
    LogRing_Init(&logBuffer.ring);
    logBuffer.mutex = (SemaphoreHandle_t)&logBuffer;   /* non-NULL */
    g_nsent = 0;
    g_writes = 0;
    g_short_on = 0;
    g_short_takes = 0;
    g_during_write = NULL;
    g_write_saw_mutex_held = 0;
    g_reset_one_shots = 0;
    g_mutex_held = 0;
}

/* ===================================================================== 1
 * The dump, end to end
 * ===================================================================== */

TEST(a_healthy_dump_sends_everything_in_order_and_empties_the_ring) {
    reset();
    for (int i = 0; i < 5; i++) push_id(i);
    ASSERT_TRUE(LogMessageDump(&g_ctx));
    ASSERT_EQ(g_nsent, 5);
    for (int i = 0; i < 5; i++) ASSERT_EQ(sent_id(i), i);
    ASSERT_EQ(logBuffer.ring.count, 0);
    ASSERT_EQ(g_reset_one_shots, 1);
    ASSERT_EQ(g_write_saw_mutex_held, 0);   /* no I/O under the mutex */
    ASSERT_EQ(g_mutex_held, 0);
}

TEST(a_short_write_stops_the_dump_and_keeps_the_record) {
    /* ⛔ The #1248 defect. FAILS on the old pop-before-write loop: it removed
     * r2 before writing it, returned nothing, and carried on to r3/r4. */
    reset();
    for (int i = 0; i < 5; i++) push_id(i);
    g_short_on = 3;          /* r2's write */
    g_short_takes = 2;       /* a prefix went out */
    ASSERT_FALSE(LogMessageDump(&g_ctx));
    ASSERT_EQ(g_nsent, 2);                  /* r0, r1 */
    ASSERT_EQ(g_writes, 3);                 /* stopped: no r3, r4 attempts */
    ASSERT_EQ(logBuffer.ring.count, 3);     /* r2, r3, r4 kept */
    ASSERT_EQ(tail_id(), 2);
    ASSERT_EQ(g_mutex_held, 0);
}

TEST(the_next_dump_resumes_with_the_kept_record) {
    reset();
    for (int i = 0; i < 5; i++) push_id(i);
    g_short_on = 3;
    ASSERT_FALSE(LogMessageDump(&g_ctx));
    g_short_on = 0;
    g_writes = 0;
    ASSERT_TRUE(LogMessageDump(&g_ctx));
    ASSERT_EQ(g_nsent, 5);
    for (int i = 0; i < 5; i++) ASSERT_EQ(sent_id(i), i);   /* nothing lost */
}

TEST(one_shots_are_reset_only_by_a_complete_dump) {
    reset();
    for (int i = 0; i < 3; i++) push_id(i);
    g_short_on = 1;
    ASSERT_FALSE(LogMessageDump(&g_ctx));
    ASSERT_EQ(g_reset_one_shots, 0);
    g_short_on = 0;
    ASSERT_TRUE(LogMessageDump(&g_ctx));
    ASSERT_EQ(g_reset_one_shots, 1);
}

TEST(an_empty_ring_is_a_complete_dump) {
    reset();
    ASSERT_TRUE(LogMessageDump(&g_ctx));
    ASSERT_EQ(g_writes, 0);
    ASSERT_EQ(g_reset_one_shots, 1);
}

/* ===================================================================== 2
 * Concurrent changes while the record is being written
 * ===================================================================== */

static int g_next_id;
static void overwrite_once_on_first_write(int writeNo) {
    if (writeNo == 1) push_id(g_next_id++);
}

TEST(an_overwrite_during_the_write_is_not_double_popped) {
    /* Full ring. During r0's write another task logs, which overwrites r0
     * (the oldest) and moves the tail to r1. r0 was delivered; the dump must
     * not ALSO pop r1, which it has not sent. */
    reset();
    for (int i = 0; i < LOG_MAX_ENTRY_COUNT; i++) push_id(i);
    g_next_id = LOG_MAX_ENTRY_COUNT;
    g_during_write = overwrite_once_on_first_write;
    ASSERT_TRUE(LogMessageDump(&g_ctx));
    /* r0 .. r64: every record exactly once, in order. */
    ASSERT_EQ(g_nsent, LOG_MAX_ENTRY_COUNT + 1);
    for (int i = 0; i < g_nsent; i++) ASSERT_EQ(sent_id(i), i);
    ASSERT_EQ(logBuffer.ring.count, 0);
}

static void clear_on_first_write(int writeNo) {
    if (writeNo == 1) {
        LogRing_Clear(&logBuffer.ring);
        push_id(100);   /* logged after the clear */
    }
}

TEST(a_clear_during_the_write_pops_nothing_and_continues_with_new_records) {
    reset();
    for (int i = 0; i < 3; i++) push_id(i);
    g_during_write = clear_on_first_write;
    ASSERT_TRUE(LogMessageDump(&g_ctx));
    ASSERT_EQ(g_nsent, 2);
    ASSERT_EQ(sent_id(0), 0);      /* delivered before the clear took it */
    ASSERT_EQ(sent_id(1), 100);    /* the post-clear record is NOT skipped */
    ASSERT_EQ(logBuffer.ring.count, 0);
}

static void other_dumper_pops_on_first_write(int writeNo) {
    if (writeNo == 1) {
        char b[LOG_MESSAGE_SIZE];
        uint32_t s;
        ASSERT_TRUE(LogRing_Peek(&logBuffer.ring, b, sizeof b, &s));
        ASSERT_TRUE(LogRing_PopIfUnchanged(&logBuffer.ring, s));
    }
}

TEST(a_second_dumper_popping_first_does_not_cost_a_record) {
    reset();
    for (int i = 0; i < 3; i++) push_id(i);
    g_during_write = other_dumper_pops_on_first_write;
    ASSERT_TRUE(LogMessageDump(&g_ctx));
    ASSERT_EQ(g_nsent, 3);                 /* r0 (shared), r1, r2 */
    for (int i = 0; i < 3; i++) ASSERT_EQ(sent_id(i), i);
}

/* ===================================================================== 3
 * 64/64 + truncation: the dump's OWN error report must not evict the kept
 * record (the coordinator's design finding on #1248)
 * ===================================================================== */

static const void *const DUMPER = (const void *)0x1;

/* What LogMessageAdd does for a record logged by `task`. */
static void log_from(const void *task, int id) {
    if (LogOwnerSet_Contains(&logBuffer.dumping, task)) return;
    push_id(id);
}

TEST(at_64_of_64_the_dumpers_own_error_record_does_not_evict_the_kept_one) {
    reset();
    for (int i = 0; i < LOG_MAX_ENTRY_COUNT; i++) push_id(i);
    ASSERT_TRUE(LogOwnerSet_Add(&logBuffer.dumping, DUMPER));
    g_short_on = 1;                          /* r0's write is short */
    ASSERT_FALSE(LogMessageDump(&g_ctx));
    log_from(DUMPER, 999);                   /* the -200 callback's LOG_E */
    LogOwnerSet_Remove(&logBuffer.dumping, DUMPER);
    ASSERT_EQ(tail_id(), 0);                 /* r0 survived */
    ASSERT_EQ(logBuffer.ring.count, LOG_MAX_ENTRY_COUNT);
}

TEST(control_without_the_bracket_the_same_record_evicts_it) {
    /* Negative control for the test above: proves the hazard is real at
     * 64/64, so the test above cannot pass for an unrelated reason. */
    reset();
    for (int i = 0; i < LOG_MAX_ENTRY_COUNT; i++) push_id(i);
    g_short_on = 1;
    ASSERT_FALSE(LogMessageDump(&g_ctx));
    log_from(DUMPER, 999);
    ASSERT_EQ(tail_id(), 1);                 /* r0 is gone */
}

TEST(other_tasks_still_log_while_one_task_dumps) {
    reset();
    ASSERT_TRUE(LogOwnerSet_Add(&logBuffer.dumping, DUMPER));
    log_from((const void *)0x2, 7);
    ASSERT_EQ(logBuffer.ring.count, 1);
    LogOwnerSet_Remove(&logBuffer.dumping, DUMPER);
}

/* ===================================================================== 4
 * LogRing.h primitives
 * ===================================================================== */

TEST(clear_bumps_tailSeq_even_when_empty) {
    reset();
    uint32_t before = logBuffer.ring.tailSeq;
    LogRing_Clear(&logBuffer.ring);
    ASSERT_EQ(logBuffer.ring.tailSeq, before + 1);
}

TEST(pop_if_unchanged_refuses_a_stale_seq_and_an_empty_ring) {
    reset();
    push_id(1);
    char b[LOG_MESSAGE_SIZE];
    uint32_t s;
    ASSERT_TRUE(LogRing_Peek(&logBuffer.ring, b, sizeof b, &s));
    ASSERT_FALSE(LogRing_PopIfUnchanged(&logBuffer.ring, s + 1));
    ASSERT_TRUE(LogRing_PopIfUnchanged(&logBuffer.ring, s));
    ASSERT_FALSE(LogRing_PopIfUnchanged(&logBuffer.ring, s + 1));
}

TEST(owner_set_holds_two_and_refuses_a_third) {
    LogOwnerSet set;
    memset(&set, 0, sizeof set);
    ASSERT_TRUE(LogOwnerSet_Add(&set, (const void *)0x1));
    ASSERT_TRUE(LogOwnerSet_Add(&set, (const void *)0x2));
    ASSERT_FALSE(LogOwnerSet_Add(&set, (const void *)0x3));
    ASSERT_FALSE(LogOwnerSet_Contains(&set, (const void *)0x3));
    ASSERT_FALSE(LogOwnerSet_Contains(&set, NULL));
    LogOwnerSet_Remove(&set, (const void *)0x1);
    ASSERT_FALSE(LogOwnerSet_Contains(&set, (const void *)0x1));
    ASSERT_TRUE(LogOwnerSet_Contains(&set, (const void *)0x2));
}

/* ===================================================================== 5
 * Property: under arbitrary concurrent pushes / clears / foreign pops during
 * each write, the dumper never sends a record twice and always sends in
 * increasing id order. (Deterministic LCG, so a failure reproduces.)
 * ===================================================================== */

static uint32_t g_lcg = 12345u;
static uint32_t lcg(void) { g_lcg = g_lcg * 1103515245u + 12345u; return g_lcg >> 8; }
static void random_interleave(int writeNo) {
    (void)writeNo;
    switch (lcg() % 6) {
    case 0: case 1: push_id(g_next_id++); break;
    case 2: { int k = (int)(lcg() % 70); while (k--) push_id(g_next_id++); } break;
    case 3: if (lcg() % 8 == 0) LogRing_Clear(&logBuffer.ring); break;
    case 4: { char b[LOG_MESSAGE_SIZE]; uint32_t s;
              if (LogRing_Peek(&logBuffer.ring, b, sizeof b, &s))
                  (void)LogRing_PopIfUnchanged(&logBuffer.ring, s); } break;
    default: break;
    }
}

TEST(property_no_record_is_sent_twice_or_out_of_order) {
    for (int trial = 0; trial < 300; trial++) {
        reset();
        g_next_id = 0;
        int n0 = (int)(lcg() % (LOG_MAX_ENTRY_COUNT + 1));
        while (g_next_id < n0) push_id(g_next_id++);
        g_during_write = random_interleave;
        g_short_on = (lcg() % 3 == 0) ? (int)(1 + lcg() % 20) : 0;
        (void)LogMessageDump(&g_ctx);
        g_during_write = NULL;
        for (int i = 1; i < g_nsent; i++) {
            if (!(sent_id(i) > sent_id(i - 1))) {
                ASSERT_TRUE(sent_id(i) > sent_id(i - 1));
                printf("    trial %d: sent[%d]=r%d after r%d\n",
                       trial, i, sent_id(i), sent_id(i - 1));
                return;
            }
        }
        ASSERT_EQ(g_mutex_held, 0);
    }
}

int main(void) {
    printf("test_1248_log_ring (LOG_MAX_ENTRY_COUNT=%d, LOG_MESSAGE_SIZE=%d)\n",
           LOG_MAX_ENTRY_COUNT, LOG_MESSAGE_SIZE);
    RUN(a_healthy_dump_sends_everything_in_order_and_empties_the_ring);
    RUN(a_short_write_stops_the_dump_and_keeps_the_record);
    RUN(the_next_dump_resumes_with_the_kept_record);
    RUN(one_shots_are_reset_only_by_a_complete_dump);
    RUN(an_empty_ring_is_a_complete_dump);
    RUN(an_overwrite_during_the_write_is_not_double_popped);
    RUN(a_clear_during_the_write_pops_nothing_and_continues_with_new_records);
    RUN(a_second_dumper_popping_first_does_not_cost_a_record);
    RUN(at_64_of_64_the_dumpers_own_error_record_does_not_evict_the_kept_one);
    RUN(control_without_the_bracket_the_same_record_evicts_it);
    RUN(other_tasks_still_log_while_one_task_dumps);
    RUN(clear_bumps_tailSeq_even_when_empty);
    RUN(pop_if_unchanged_refuses_a_stale_seq_and_an_empty_ring);
    RUN(owner_set_holds_two_and_refuses_a_third);
    RUN(property_no_record_is_sent_twice_or_out_of_order);
    return test_summary();
}
