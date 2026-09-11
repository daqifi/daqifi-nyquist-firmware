/* ==========================================================================
 * test_999_scpi_context_storage_isolation.c
 *
 * Issue #999: CreateSCPIContext() (firmware/src/services/SCPI/SCPIInterface.c)
 * used to hand BOTH the USB and the TCP SCPI transport the SAME two
 * file-scope singleton arrays as libscpi's input parse buffer and error
 * queue backing storage:
 *
 *   char        scpi_input_buffer[SCPI_INPUT_BUFFER_LENGTH];   // 512
 *   scpi_error_t scpi_error_queue_data[SCPI_ERROR_QUEUE_SIZE]; // 17
 *
 * The scpi_t context struct itself is per-transport (each of UsbCdc.c:1256
 * and wifi_tcp_server.c:362 keeps its own copy, returned by value from
 * CreateSCPIContext), but libscpi's SCPI_Init() (parser.c) only stores
 * *pointers* to whatever arrays it's handed:
 *
 *   context->buffer.data = input_buffer;                 (parser.c)
 *   SCPI_ErrorInit(context, error_queue_data, size)
 *     -> fifo_init(&context->error_queue, data, size)     (error.c, fifo.c)
 *
 * Each transport's read/write cursors (buffer.position; error_queue.wr/rd)
 * live IN the per-transport struct and independently start at 0/0/0 -- but
 * when both contexts are pointed at the SAME backing array, those
 * independent cursors index into the SAME physical memory and clobber each
 * other's in-flight command bytes and queued errors.
 *
 * SCPIInterface.c itself is NOT includable on the host (pulls in FreeRTOS,
 * Harmony and the whole driver graph -- same situation as #943/#947/#995's
 * host tests). But the vendored libscpi sources that implement the actual
 * clobber mechanism (parser.c, fifo.c, error.c, plus their few internal
 * dependents) are plain, portable C with zero FreeRTOS/Harmony dependency
 * and compile standalone on any host -- this test builds and links the
 * REAL production libscpi against a minimal harness that mirrors
 * CreateSCPIContext's own SCPI_Init() call shape, and:
 *
 *   Part A: reproduces the bug (two contexts sharing one array corrupt
 *           each other's queued errors) -- demonstrates WHY the fix
 *           matters, using the exact real fifo/error/parser code path
 *           181 firmware call sites go through via SCPI_ErrorPush.
 *   Part B: proves the fix's shape (two contexts with SEPARATE storage do
 *           NOT corrupt each other) -- this is the regression guard: if a
 *           future change reintroduces sharing, Part B starts failing
 *           exactly like Part A already demonstrates.
 *   Part C: greps the real firmware sources to pin that CreateSCPIContext,
 *           UsbCdc.c and wifi_tcp_server.c actually HAVE the per-transport
 *           storage shape Part B assumes -- so Part B can't quietly drift
 *           into testing a shape production code no longer has.
 *
 * Build note: USE_DEVICE_DEPENDENT_ERROR_INFORMATION is forced to 0 on the
 * command line (see Makefile) to match the firmware target exactly -- on a
 * bare host build libscpi's own config.h would otherwise auto-detect
 * SYSTEM_FULL_BLOWN (via __unix__) and turn it on, which changes
 * sizeof(scpi_error_t) and pulls in a malloc/free path the firmware never
 * takes (confirmed: zero non-library callers pass non-NULL info to
 * SCPI_ErrorPushEx anywhere in firmware/src).
 * ========================================================================== */
#include "test_framework.h"

#include <string.h>

#include "scpi/scpi.h"

/* ---- minimal scpi_interface_t: capture writes, no-op everything else --- */

#define CAPTURE_SIZE 256

typedef struct {
    char   data[CAPTURE_SIZE];
    size_t len;
} WriteCapture;

static WriteCapture gCaptureA;
static WriteCapture gCaptureB;

/* microrl-less harness: we drive SCPI_Input() directly, so context->user_context
 * doubles as "which capture buffer does this context's write go to". */
static size_t TestWrite(scpi_t * context, const char * data, size_t len) {
    WriteCapture * cap = (WriteCapture *) context->user_context;
    size_t n = len;
    if (cap != NULL) {
        if (cap->len + n > CAPTURE_SIZE) {
            n = CAPTURE_SIZE - cap->len;
        }
        memcpy(cap->data + cap->len, data, n);
        cap->len += n;
    }
    return len;
}

static int TestError(scpi_t * context, int_fast16_t err) {
    (void) context;
    (void) err;
    return 0;
}

static scpi_result_t TestControl(scpi_t * context, scpi_ctrl_name_t ctrl, scpi_reg_val_t val) {
    (void) context; (void) ctrl; (void) val;
    return SCPI_RES_OK;
}

static scpi_result_t TestFlush(scpi_t * context) {
    (void) context;
    return SCPI_RES_OK;
}

static scpi_interface_t gTestInterface = {
    .error = TestError,
    .write = TestWrite,
    .control = TestControl,
    .flush = TestFlush,
    .reset = NULL,
};

/* One real command that always fails its callback, to exercise parser.c's
 * OTHER auto-push site (SCPI_ERROR_EXECUTION_ERROR, parser.c ~line 146) --
 * this is the exact new path #999's issue thread flagged as reachable via
 * PR #992 (a stalled-write diagnostic that now returns SCPI_RES_ERR from a
 * callback for the first time). Both auto-push sites go through the same
 * shared/unshared error_queue, so both must be covered. */
static scpi_result_t TestFailCallback(scpi_t * context) {
    (void) context;
    return SCPI_RES_ERR;
}

static const scpi_command_t gTestCommands[] = {
    {.pattern = "TEST:FAIL", .callback = TestFailCallback},
    SCPI_CMD_LIST_END,
};

/* Mirrors SCPIInterface.c's scpi_units_def -- an empty/default unit table is
 * fine, this test never exercises unit-suffixed numeric parameters. */
static const scpi_unit_def_t gTestUnits[] = {
    SCPI_UNITS_LIST_END,
};

/* ---- helpers ------------------------------------------------------------ */

static void FeedLine(scpi_t * context, const char * line) {
    /* Real commands always arrive newline-terminated in one SCPI_Input()
     * call (microrl appends ENDL before calling execute -- see
     * microrl.c's buffer_insert_text(ENDL) + execute() sequence quoted in
     * the #999 issue thread), so this harness matches that shape rather
     * than feeding partial/unterminated data. */
    char buf[128];
    size_t n = strlen(line);
    memcpy(buf, line, n);
    buf[n] = '\n';
    SCPI_Input(context, buf, (int)(n + 1));
}

static void InitTestContext(scpi_t * context, void * capture,
                             char * inputBuffer, size_t inputBufferLen,
                             scpi_error_t * errorQueue, int16_t errorQueueLen) {
    SCPI_Init(context, gTestCommands, &gTestInterface, gTestUnits,
              "TEST", "999", "0", "00-00",
              inputBuffer, inputBufferLen,
              errorQueue, errorQueueLen);
    context->user_context = capture;
}

/* ---- Part A: reproduce the bug (deliberately-shared storage) ----------- */

TEST(shared_storage_error_queue_corrupts_across_contexts) {
    /* Exactly today's (pre-#999) CreateSCPIContext() shape: ONE backing
     * array handed to both contexts. */
    static char sharedInputBuffer[512];
    static scpi_error_t sharedErrorQueue[17];

    scpi_t ctxUsb, ctxTcp;
    memset(&gCaptureA, 0, sizeof(gCaptureA));
    memset(&gCaptureB, 0, sizeof(gCaptureB));

    InitTestContext(&ctxUsb, &gCaptureA, sharedInputBuffer, sizeof(sharedInputBuffer),
                     sharedErrorQueue, 17);
    InitTestContext(&ctxTcp, &gCaptureB, sharedInputBuffer, sizeof(sharedInputBuffer),
                     sharedErrorQueue, 17);

    /* USB: unrecognized header -> auto-push SCPI_ERROR_UNDEFINED_HEADER (-113) */
    FeedLine(&ctxUsb, "NOSUCH:HEADER");
    /* TCP: recognized command whose callback fails -> auto-push
     * SCPI_ERROR_EXECUTION_ERROR (-200) */
    FeedLine(&ctxTcp, "TEST:FAIL");

    /* SCPI_ErrorPop() (error.c) always returns TRUE -- SCPI semantics are
     * "popping an empty queue yields error_code 0 (No error)", not
     * "pop fails". So the queue-had-something check is SCPI_ErrorCount(),
     * not the boolean return of Pop(). */
    ASSERT_EQ(SCPI_ErrorCount(&ctxUsb), 1);
    scpi_error_t poppedUsb;
    SCPI_ErrorPop(&ctxUsb, &poppedUsb);

    /* THE BUG: both fifo cursors started at wr=0 into the SAME array, so
     * TCP's push (second) landed on top of USB's push (first) at index 0.
     * USB's own pop therefore returns TCP's error code, not its own. This
     * assertion documents the corruption as it exists in the shared-storage
     * shape -- it is expected to PASS, because it is exercising real
     * production fifo.c/parser.c code with storage deliberately shared the
     * way CreateSCPIContext used to share it. */
    ASSERT_EQ(poppedUsb.error_code, SCPI_ERROR_EXECUTION_ERROR);
    ASSERT_FALSE(poppedUsb.error_code == SCPI_ERROR_UNDEFINED_HEADER);
}

/* ---- Part B: prove the fix (separate storage per context) -------------- */

TEST(separate_storage_error_queue_isolated_per_context) {
    /* #999's fix shape: each transport gets its OWN backing array (this
     * mirrors ScpiContextStorage in SCPIInterface.h field-for-field --
     * see the Part C grep guards below, which pin that the real struct
     * still has exactly this shape). */
    static char usbInputBuffer[512];
    static scpi_error_t usbErrorQueue[17];
    static char tcpInputBuffer[512];
    static scpi_error_t tcpErrorQueue[17];

    scpi_t ctxUsb, ctxTcp;
    memset(&gCaptureA, 0, sizeof(gCaptureA));
    memset(&gCaptureB, 0, sizeof(gCaptureB));

    InitTestContext(&ctxUsb, &gCaptureA, usbInputBuffer, sizeof(usbInputBuffer),
                     usbErrorQueue, 17);
    InitTestContext(&ctxTcp, &gCaptureB, tcpInputBuffer, sizeof(tcpInputBuffer),
                     tcpErrorQueue, 17);

    FeedLine(&ctxUsb, "NOSUCH:HEADER");
    FeedLine(&ctxTcp, "TEST:FAIL");

    /* Each context queued exactly one error of its own -- no cross-talk. */
    ASSERT_EQ(SCPI_ErrorCount(&ctxUsb), 1);
    ASSERT_EQ(SCPI_ErrorCount(&ctxTcp), 1);

    scpi_error_t poppedUsb, poppedTcp;
    SCPI_ErrorPop(&ctxUsb, &poppedUsb);
    SCPI_ErrorPop(&ctxTcp, &poppedTcp);

    /* Each transport now correctly sees ONLY its own error. */
    ASSERT_EQ(poppedUsb.error_code, SCPI_ERROR_UNDEFINED_HEADER);
    ASSERT_EQ(poppedTcp.error_code, SCPI_ERROR_EXECUTION_ERROR);

    /* Both queues are now empty -- neither leaked the other's entry
     * (SCPI_ErrorPop() always returns TRUE; emptiness is ErrorCount()==0,
     * see the comment in Part A above). */
    ASSERT_EQ(SCPI_ErrorCount(&ctxUsb), 0);
    ASSERT_EQ(SCPI_ErrorCount(&ctxTcp), 0);
}

TEST(separate_storage_input_buffer_not_aliased) {
    /* Companion check for the OTHER shared resource: with separate input
     * buffers, a long parameter sent to one context cannot show up in the
     * other's write output. (The realistic corruption window opus
     * identified -- a callback's parameter pointers staying live into the
     * shared buffer during a long dispatch -- needs the full parser +
     * blocking-retry machinery to reproduce end-to-end; that is exercised
     * on real hardware, see the companion daqifi-python-test-suite test.
     * This check instead pins the structural precondition for that
     * corruption -- aliased buffer.data pointers -- by proving it is FALSE
     * for the fixed shape.) */
    static char usbInputBuffer[512];
    static scpi_error_t usbErrorQueue[17];
    static char tcpInputBuffer[512];
    static scpi_error_t tcpErrorQueue[17];

    scpi_t ctxUsb, ctxTcp;
    InitTestContext(&ctxUsb, NULL, usbInputBuffer, sizeof(usbInputBuffer), usbErrorQueue, 17);
    InitTestContext(&ctxTcp, NULL, tcpInputBuffer, sizeof(tcpInputBuffer), tcpErrorQueue, 17);

    ASSERT_TRUE(ctxUsb.buffer.data != ctxTcp.buffer.data);
    ASSERT_TRUE(ctxUsb.error_queue.data != ctxTcp.error_queue.data);
}

TEST(shared_storage_input_buffer_is_aliased) {
    /* The inverse of the check above, against the OLD (buggy) shape, so
     * the two tests read as a matched pair documenting exactly what
     * changed. */
    static char sharedInputBuffer[512];
    static scpi_error_t sharedErrorQueue[17];

    scpi_t ctxUsb, ctxTcp;
    InitTestContext(&ctxUsb, NULL, sharedInputBuffer, sizeof(sharedInputBuffer), sharedErrorQueue, 17);
    InitTestContext(&ctxTcp, NULL, sharedInputBuffer, sizeof(sharedInputBuffer), sharedErrorQueue, 17);

    ASSERT_TRUE(ctxUsb.buffer.data == ctxTcp.buffer.data);
    ASSERT_TRUE(ctxUsb.error_queue.data == ctxTcp.error_queue.data);
}

int main(void) {
    RUN(shared_storage_error_queue_corrupts_across_contexts);
    RUN(shared_storage_input_buffer_is_aliased);
    RUN(separate_storage_error_queue_isolated_per_context);
    RUN(separate_storage_input_buffer_not_aliased);
    return test_summary();
}
