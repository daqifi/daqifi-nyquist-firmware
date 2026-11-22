/* ==========================================================================
 * test_1197_isr_log_init_order.c -- issue #1197
 *
 * WHAT IS UNDER TEST
 *
 * firmware/src/Util/Logger.c's LogIsrInit() and LogIsrDrainTask() -- THE REAL
 * ONES. Logger.c is not host-includable (xc.h, SYSKEY, PIC32 __builtin_*
 * intrinsics, Harmony's clock_config.h), so the Makefile recipe extracts both
 * function bodies verbatim into gen_1197_isr_log_init.h, which this file
 * #includes against mocked FreeRTOS primitives.
 *
 * THE DEFECT
 *
 * LogIsrInit created the drain task BEFORE it published gIsrLogQueue, and the
 * task read the global. LogIsrInit is reached lazily from the first LOG_x of a
 * boot, which can be issued from a priority-1 task (#908/#1077's blank-cal-bank
 * LOG_E) -- the same priority as the new drain task. If the scheduler ran the
 * new task before xTaskCreate returned to LogIsrInit, the task called
 * xQueueReceive(NULL, ...): configASSERT, vAssertCalled spins, the WDT is off,
 * the board hangs at boot.
 *
 * HOW A SINGLE-THREADED HOST REPRODUCES IT
 *
 * Unlike a test-and-set race, this ordering IS reproducible on a host: the
 * mocked xTaskCreate can do what a preempting scheduler does -- run the new
 * task's entry function BEFORE returning. The task runs until its first
 * xQueueReceive, which the mock records and then escapes with longjmp (the
 * real task loops forever). So "the scheduler ran the task first" is a mode of
 * the mock, and the assertion is on the handle the task actually drained.
 * Against the pre-#1197 Logger.c, task_run_before_create_returns_drains_a_
 * real_queue fails (the task drains NULL); after it, every case passes.
 * ========================================================================== */

#include "test_framework.h"
#include <setjmp.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* ── Mocked FreeRTOS types and constants ─────────────────────────────── */
typedef long BaseType_t;
typedef unsigned long UBaseType_t;
typedef unsigned long TickType_t;
typedef void *QueueHandle_t;
typedef void *TaskHandle_t;
typedef void (*TaskFunction_t)(void *);
#define pdPASS        ((BaseType_t)1)
#define pdFAIL        ((BaseType_t)0)
#define pdTRUE        ((BaseType_t)1)
#define portMAX_DELAY ((TickType_t)~0ul)

/* Only the size matters to LogIsrInit (sizeof(LogEntry) is passed to
 * xQueueCreate); the drain task hands entry.message to LogMessageAdd. */
typedef struct { char message[128]; } LogEntry;
#define LOG_ISR_QUEUE_DEPTH 8

/* ── Mock state ──────────────────────────────────────────────────────── */
static int  g_queue_storage[4];           /* distinct non-NULL handles */
static int  g_task_storage[4];
static int  g_queues_created;
static int  g_tasks_created;
static bool g_queue_create_fails;
static bool g_task_create_fails;

/* Scheduler model: when set, xTaskCreate runs the new task immediately --
 * before it returns to its caller -- as a preempting scheduler may. */
static bool          g_run_task_inside_create;
static jmp_buf       g_task_escape;
static bool          g_task_ran;
static QueueHandle_t g_task_drained;      /* handle passed to xQueueReceive */
static TaskFunction_t g_task_fn;          /* what xTaskCreate was given, so */
static void          *g_task_param;       /* a test can run the task LATER  */

/* Simulates another caller winning LogIsrInit's race between this caller's
 * xTaskCreate and its critical section. */
static QueueHandle_t g_rival_publishes;

/* Deletion order, so the race-loser path can be checked: its task must be
 * deleted before the queue that task may be blocked on. */
static char g_events[8];
static int  g_n_events;
static TaskHandle_t  g_deleted_task;
static QueueHandle_t g_deleted_queue;

static QueueHandle_t gIsrLogQueue;
static TaskHandle_t  gIsrLogTaskHandle;
#define LOG_ISR_TASK_STACK 256
#define LOG_ISR_TASK_PRIO  1

/* Never reached: the xQueueReceive mock escapes before a message exists. */
static int LogMessageAdd(const char *message) { (void)message; return 0; }

static QueueHandle_t xQueueCreate(UBaseType_t depth, UBaseType_t item) {
    (void)depth; (void)item;
    if (g_queue_create_fails) return NULL;
    return &g_queue_storage[g_queues_created++];
}

static BaseType_t xQueueReceive(QueueHandle_t q, void *buf, TickType_t wait) {
    (void)buf; (void)wait;
    g_task_ran = true;
    g_task_drained = q;
    longjmp(g_task_escape, 1);            /* leave the task's for(;;) */
}

static void run_task_once(TaskFunction_t fn, void *param) {
    if (setjmp(g_task_escape) == 0) fn(param);
}

static BaseType_t xTaskCreate(TaskFunction_t fn, const char *name, uint32_t stack,
                              void *param, UBaseType_t prio, TaskHandle_t *out) {
    (void)name; (void)stack; (void)prio;
    if (g_task_create_fails) return pdFAIL;
    *out = &g_task_storage[g_tasks_created++];
    g_task_fn = fn;
    g_task_param = param;
    if (g_run_task_inside_create) run_task_once(fn, param);
    if (g_rival_publishes != NULL) gIsrLogQueue = g_rival_publishes;
    return pdPASS;
}

static void vTaskDelete(TaskHandle_t t)  { g_deleted_task = t;  g_events[g_n_events++] = 'T'; }
static void vQueueDelete(QueueHandle_t q) { g_deleted_queue = q; g_events[g_n_events++] = 'Q'; }
static void taskENTER_CRITICAL(void) {}
static void taskEXIT_CRITICAL(void) {}

static void reset(void) {
    g_queues_created = g_tasks_created = 0;
    g_queue_create_fails = g_task_create_fails = false;
    g_run_task_inside_create = false;
    g_task_ran = false;
    g_task_drained = NULL;
    g_task_fn = NULL;
    g_task_param = NULL;
    g_rival_publishes = NULL;
    memset(g_events, 0, sizeof g_events);
    g_n_events = 0;
    g_deleted_task = NULL;
    g_deleted_queue = NULL;
    gIsrLogQueue = NULL;
    gIsrLogTaskHandle = NULL;
}

/* THE REAL FUNCTIONS, extracted verbatim by the Makefile recipe. */
#include "gen_1197_isr_log_init.h"

/* The #1197 defect: the scheduler runs the new drain task before xTaskCreate
 * returns, i.e. before LogIsrInit has published anything. */
TEST(task_run_before_create_returns_drains_a_real_queue) {
    reset();
    g_run_task_inside_create = true;
    LogIsrInit();
    ASSERT_TRUE(g_task_ran);
    ASSERT_TRUE(g_task_drained != NULL);
    ASSERT_TRUE(g_task_drained == (QueueHandle_t)&g_queue_storage[0]);
    ASSERT_TRUE(gIsrLogQueue == g_task_drained);   /* ISR side sends here */
}

/* The ordinary order: the task first runs after LogIsrInit returns. */
TEST(task_run_after_init_drains_the_published_queue) {
    reset();
    LogIsrInit();
    ASSERT_FALSE(g_task_ran);
    ASSERT_TRUE(gIsrLogQueue != NULL);
    ASSERT_TRUE(gIsrLogTaskHandle == (TaskHandle_t)&g_task_storage[0]);
    ASSERT_TRUE(g_task_fn != NULL);
    run_task_once(g_task_fn, g_task_param);
    ASSERT_TRUE(g_task_ran);
    ASSERT_TRUE(g_task_drained == gIsrLogQueue);
}

TEST(second_call_is_a_no_op) {
    reset();
    LogIsrInit();
    QueueHandle_t q = gIsrLogQueue;
    LogIsrInit();
    ASSERT_TRUE(gIsrLogQueue == q);
    ASSERT_EQ(g_queues_created, 1);
    ASSERT_EQ(g_tasks_created, 1);
}

/* Another caller won between this caller's xTaskCreate and its critical
 * section: the loser must leave the winner's queue published and delete its
 * own task BEFORE its own queue (the task may be blocked on that queue). */
TEST(race_loser_deletes_its_task_before_its_queue) {
    static int rival_queue;
    reset();
    g_rival_publishes = (QueueHandle_t)&rival_queue;
    g_run_task_inside_create = true;
    LogIsrInit();
    ASSERT_TRUE(gIsrLogQueue == (QueueHandle_t)&rival_queue);
    ASSERT_TRUE(g_task_drained == (QueueHandle_t)&g_queue_storage[0]);
    ASSERT_TRUE(g_deleted_task == (TaskHandle_t)&g_task_storage[0]);
    ASSERT_TRUE(g_deleted_queue == (QueueHandle_t)&g_queue_storage[0]);
    ASSERT_EQ(g_n_events, 2);
    ASSERT_EQ(g_events[0], 'T');
    ASSERT_EQ(g_events[1], 'Q');
}

TEST(task_create_failure_publishes_nothing) {
    reset();
    g_task_create_fails = true;
    LogIsrInit();
    ASSERT_TRUE(gIsrLogQueue == NULL);
    ASSERT_TRUE(gIsrLogTaskHandle == NULL);
    ASSERT_TRUE(g_deleted_queue == (QueueHandle_t)&g_queue_storage[0]);
}

TEST(queue_create_failure_creates_no_task) {
    reset();
    g_queue_create_fails = true;
    LogIsrInit();
    ASSERT_TRUE(gIsrLogQueue == NULL);
    ASSERT_EQ(g_tasks_created, 0);
}

int main(void) {
    printf("#1197 LogIsrInit / LogIsrDrainTask init order\n");
    RUN(task_run_before_create_returns_drains_a_real_queue);
    RUN(task_run_after_init_drains_the_published_queue);
    RUN(second_call_is_a_no_op);
    RUN(race_loser_deletes_its_task_before_its_queue);
    RUN(task_create_failure_publishes_nothing);
    RUN(queue_create_failure_creates_no_task);
    return test_summary();
}
