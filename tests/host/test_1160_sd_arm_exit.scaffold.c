/* ==========================================================================
 * test_1160_sd_arm_exit.scaffold.c -- host stand-ins and cases for the REAL
 * text of every SD arm site (issue #1160).
 *
 * NOT compiled directly. `check_1160_arm_exit.py` fills the three
 * `@@...@@` placeholders below with text EXTRACTED VERBATIM from
 * SCPIStorageSD.c / SCPIInterface.c, compiles the result and runs it, then
 * does the same for every mutant of every site (see that file's docstring for
 * the contract and for why the extraction lives there).
 *
 * WHAT THIS ESTABLISHES, per arm site: with the arm driven to REFUSE, the
 * handler returns its error result and does not reach the code that follows
 * the arm. The control case (arm accepted) proves that "the code that follows"
 * is reachable at all, so the refusal case cannot pass vacuously.
 *
 * WHAT IT DOES NOT ESTABLISH: everything upstream of the arm (the stand-ins
 * below accept every precondition), the SD manager's own behaviour, and any
 * concurrency property -- that is test_971's territory and hardware's.
 *
 * "WHAT FOLLOWS THE ARM" is observed through counters AS WELL AS the return
 * value: the refusal handlers initialise `result` to SCPI_RES_ERR, so a
 * fall-through that ends in an error for an unrelated reason would still read
 * as a correct refusal by return code alone.
 * ========================================================================== */
#include "test_framework.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define STUB __attribute__((unused)) static

/* ---- libscpi / firmware types, minimal ---------------------------------- */
typedef enum { SCPI_RES_OK = 1, SCPI_RES_ERR = -1 } scpi_result_t;
#define SCPI_ERROR_EXECUTION_ERROR (-200)
#define SCPI_ERROR_ILLEGAL_PARAMETER_VALUE (-224)
/* fw#1265: the tagged-GET operand checks SCPI_SD_GetData now runs. */
#define SCPI_ERROR_DATA_TYPE_ERROR (-104)
#define SCPI_ERROR_DATA_OUT_OF_RANGE (-222)
#define TRUE 1
#define FALSE 0
typedef int scpi_bool_t;
typedef enum { SCPI_TOKEN_UNKNOWN = 0, SCPI_TOKEN_DECIMAL_NUMERIC_PROGRAM_DATA = 1 } scpi_token_type_t;
typedef struct { scpi_token_type_t type; size_t len; } scpi_parameter_t;
typedef struct scpi_t scpi_t;
struct scpi_interface_t {
    size_t (*write)(scpi_t *context, const char *data, size_t len);
};
struct scpi_t {
    struct scpi_interface_t *interface;
    scpi_bool_t cmd_error;
};

#define SD_CARD_MANAGER_CONF_FILE_NAME_LEN_MAX 63
#define SD_CARD_MANAGER_CONF_DIR_NAME_LEN_MAX 63
typedef enum {
    SD_CARD_MANAGER_MODE_NONE = 0,
    SD_CARD_MANAGER_MODE_READ,
    SD_CARD_MANAGER_MODE_WRITE,
    SD_CARD_MANAGER_MODE_LIST_DIRECTORY,
    SD_CARD_MANAGER_MODE_DELETE_FILE,
    SD_CARD_MANAGER_MODE_FORMAT,
    SD_CARD_MANAGER_MODE_GET_SPACE,
    SD_CARD_MANAGER_MODE_COMPUTE_CRC,
} sd_card_manager_mode_t;
typedef enum { SD_CARD_REPLY_USB = 0, SD_CARD_REPLY_WIFI_TCP = 1 } sd_card_reply_t;
typedef struct {
    bool enable;
    sd_card_manager_mode_t mode;
    sd_card_reply_t replyTarget;
    uint32_t replyGeneration;
    char directory[SD_CARD_MANAGER_CONF_DIR_NAME_LEN_MAX + 1];
    char opDirectory[SD_CARD_MANAGER_CONF_DIR_NAME_LEN_MAX + 1];
    char file[SD_CARD_MANAGER_CONF_FILE_NAME_LEN_MAX + 1];
    char opFile[SD_CARD_MANAGER_CONF_FILE_NAME_LEN_MAX + 1];
    uint64_t minFreeBytes;
    uint32_t opGetTag;          /* fw#1265 */
    bool opGetTagValid;
} sd_card_manager_settings_t;

#define BOARDRUNTIME_SD_CARD_SETTINGS 1
#define SCPI_SD_LIST_TIMEOUT_MS 10000
#define SCPI_SD_DELETE_TIMEOUT_MS 5000
#define SCPI_SD_SPACE_TIMEOUT_MS 10000
#define SD_CARD_NOT_ENABLED_ERROR_MSG "\r\nError !! Please Enabled SD Card\r\n"
#define taskENTER_CRITICAL() ((void)0)
#define taskEXIT_CRITICAL() ((void)0)
#define LOG_E(...) ((void)0)
#define LOG_D(...) ((void)0)
#define LOG_SD_BUSY(cmd) ((void)0)

/* ---- observation counters ------------------------------------------------ */
static int gArmCalls;        /* every arm entry, any of the three real names  */
static int gReleaseCalls;    /* sd_card_manager_ReleaseClaim                  */
static int gErrorPushes;     /* SCPI_ErrorPush                                */
static int gWaitCalls;       /* WaitForCompletion[Pumped] -- what follows LIST/DEL/SPACe */
static int gResultCalls;     /* GetLastOperationResult / GetSpaceInfo / ResultUInt64 */
static int gFellThrough;     /* the slices' marker for "execution continued"  */
static int gClearFormat;     /* onRefused callback                            */
static bool gArmAccepts;

static sd_card_manager_settings_t gCfg;
static size_t gWriteStub(scpi_t *c, const char *d, size_t l) { (void)c; (void)d; return l; }
static struct scpi_interface_t gIface = { gWriteStub };
static scpi_t gCtx = { &gIface };

STUB void *BoardRunTimeConfig_Get(int which) { (void)which; return &gCfg; }
STUB void SCPI_ErrorPush(scpi_t *c, int e) { (void)c; (void)e; gErrorPushes++; }
STUB void SCPI_ExecutionError(scpi_t *c, const char *m) { (void)c; (void)m; gErrorPushes++; }
STUB void sd_card_manager_ReleaseClaim(void) { gReleaseCalls++; }
STUB bool sd_card_manager_TryClaim(void) { return true; }
STUB bool sd_card_manager_IsBusy(void) { return false; }
STUB void sd_card_manager_InvalidateCrcResult(void) {}
STUB void sd_card_manager_SetFormatPending(void) {}
STUB void sd_card_manager_ClearFormatStatus(void) { gClearFormat++; }
STUB bool sd_card_manager_ReadBufferReady(void) { return true; }
STUB bool wifi_tcp_server_ContextIsTcp(scpi_t *c) { (void)c; return false; }
STUB uint32_t wifi_tcp_server_GetConnGeneration(void) { return 0u; }
STUB bool DRV_SDSPI_CrcModePending(int i) { (void)i; return false; }
STUB const char *SD_SuspendReasonText(void) { return "stand-in reason"; }

/* The arm. Like the real refusal (sd_card_manager.c's #589 branch) it clears
 * `mode` itself and returns false; the three real entry names share it. */
static bool arm(sd_card_manager_settings_t *cfg) {
    gArmCalls++;
    if (!gArmAccepts) {
        cfg->mode = SD_CARD_MANAGER_MODE_NONE;
        return false;
    }
    return true;
}
STUB bool sd_card_manager_UpdateSettings(sd_card_manager_settings_t *c) { return arm(c); }
STUB bool sd_card_manager_UpdateSettingsForPlainWrite(sd_card_manager_settings_t *c) { return arm(c); }
STUB bool sd_card_manager_UpdateSettingsForStreamingLog(sd_card_manager_settings_t *c) { return arm(c); }

/* What FOLLOWS the arm in LIST / DELete / SPACe. Everything reports success,
 * so a handler that falls through after a refusal returns OK (a wrong answer)
 * AND moves the counters; the counters are the finer oracle, and are what keeps
 * this honest if a later edit makes a fall-through end in an error anyway. */
STUB bool sd_card_manager_WaitForCompletion(int ms) { (void)ms; gWaitCalls++; return true; }
STUB bool sd_card_manager_WaitForCompletionPumped(int ms) { (void)ms; gWaitCalls++; return true; }
STUB bool sd_card_manager_GetLastOperationResult(void) { gResultCalls++; return true; }
STUB bool sd_card_manager_GetSpaceInfo(uint64_t *f, uint64_t *t) { gResultCalls++; *f = 0; *t = 0; return true; }
STUB void SCPI_ResultUInt64(scpi_t *c, uint64_t v) { (void)c; (void)v; gResultCalls++; }

/* Upstream of the arm: every precondition passes. */
static const char *gParam = "a.csv";
STUB int SCPI_ParamCharacters(scpi_t *c, const char **buf, size_t *len, int mandatory) {
    (void)c; (void)mandatory;
    *buf = gParam;
    *len = strlen(gParam);
    return 1;
}
/* fw#1265: NO TAG on the command line, so every case below exercises the
 * untagged GET -- the path whose refusal-exit these cases pin. The tagged
 * operand checks are stubbed to accept so a stray tag would not mask it. */
STUB int SCPI_Parameter(scpi_t *c, scpi_parameter_t *tok, int mandatory) {
    (void)c; (void)mandatory;
    tok->type = SCPI_TOKEN_UNKNOWN + 1;
    tok->len = 0;
    return 0;
}
STUB bool SCPI_ParamIsNumber(scpi_parameter_t *tok, int suffixAllowed) { (void)tok; (void)suffixAllowed; return true; }
STUB bool SCPI_ParamToInt32(scpi_t *c, scpi_parameter_t *tok, int32_t *v) { (void)c; (void)tok; *v = 0; return true; }
STUB bool SCPI_ParamErrorOccurred(scpi_t *c) { (void)c; return false; }
STUB bool SD_TagEchoesExactly(scpi_parameter_t *tok, int32_t v) { (void)tok; (void)v; return true; }
STUB uint32_t UsbCdc_GetSessionGeneration(void) { return 0u; }
STUB bool Streaming_SessionIsLive(void) { return false; }
STUB bool Streaming_SessionStartInProgress(void) { return false; }
STUB bool SD_ClaimOrRefuse(scpi_t *c, const char *cmd) { (void)c; (void)cmd; return true; }
STUB bool SD_RefuseIfSuspended(scpi_t *c, const char *cmd) { (void)c; (void)cmd; return false; }
STUB bool SCPI_CheckSDCardPresent(scpi_t *c) { (void)c; return true; }
STUB bool SD_ValidatePathParam(const char *p, size_t n) { (void)p; (void)n; return true; }
STUB const char *SD_StripConfiguredDir(const char *p, size_t *n, const char *dir) {
    (void)n; (void)dir;
    return p;
}

/* Streaming-site stand-ins. */
STUB void SCPI_ClearStreamingOperBits(void *s) { (void)s; }
STUB void SCPI_UnpublishStartInterface(void *s, int a, int b, int c, int d) {
    (void)s; (void)a; (void)b; (void)c; (void)d;
}
STUB scpi_result_t SCPI_StartFailedAfterStop(void) { return SCPI_RES_ERR; }
/* #1279: SD:BENCH and SD:GET restore the idle SD buffers before arming. */
STUB bool SCPI_RestoreIdleSdBuffers(void) { return true; }

/* ======================= REAL TEXT, extracted verbatim ===================== */
/*@@FUNCS@@*/

/* The two captured-form sites cannot be compiled as whole functions (the
 * streaming handler is ~1250 lines of FreeRTOS/WiFi/nanopb), so each is a
 * SLICE of the real function: from the captured arm to the success-path
 * release that follows the refusal block. The wrapper declares the locals the
 * slice reads and plants `gFellThrough++` where the real function continues. */
STUB scpi_result_t bench_arm_slice(scpi_t *context) {
    scpi_result_t result = SCPI_RES_ERR;
    sd_card_manager_settings_t *pSDCardRuntimeConfig = &gCfg;
    /*@@SLICE_BENCH@@*/
    gFellThrough++;
    result = SCPI_RES_OK;
__exit_point:
    return result;
}

STUB scpi_result_t stream_arm_slice(scpi_t *context) {
    sd_card_manager_settings_t *pSDCardSettings = &gCfg;
    void *pRunTimeStreamConfig = NULL;
    int ifaceForStart = 0, ifaceAtDetect = 0, ifaceGenPinned = 0, ifaceSetsPinned = 0;
    /*@@SLICE_STREAM@@*/
    gFellThrough++;
    return SCPI_RES_OK;
}

/* ============================== the cases ================================== */
typedef scpi_result_t (*site_fn)(scpi_t *);
typedef struct {
    const char *name;
    site_fn fn;
    int waitsOnSuccess;   /* gWaitCalls expected when the arm is accepted */
    bool slice;           /* uses gFellThrough rather than a return value  */
} site_t;

static const site_t SITES[] = {
    { "CrcStart", SCPI_StorageSDCrcStart,  0, false },
    { "GetData",  SCPI_StorageSDGetData,   0, false },
    { "ListDir",  SCPI_StorageSDListDir,   1, false },
    { "Delete",   SCPI_StorageSDDelete,    1, false },
    { "Format",   SCPI_StorageSDFormat,    0, false },
    { "SpaceGet", SCPI_StorageSDSpaceGet,  1, false },
    { "Benchmark", bench_arm_slice,        0, true },
    { "Streaming", stream_arm_slice,       0, true },
};

static void reset(bool accepts) {
    gArmCalls = gReleaseCalls = gErrorPushes = gWaitCalls = gResultCalls = 0;
    gFellThrough = gClearFormat = 0;
    gArmAccepts = accepts;
    memset(&gCfg, 0, sizeof gCfg);
    gCfg.enable = true;
}

/* CONTROL: the arm accepts, and the code after it IS reached. Without this a
 * refusal case asserting "nothing after the arm ran" could pass because the
 * counters are never incremented by anything. */
static void control(const site_t *s) {
    reset(true);
    scpi_result_t r = s->fn(&gCtx);
    printf("  control %s\n", s->name);
    ASSERT_EQ(gArmCalls, 1);
    ASSERT_EQ(gErrorPushes, 0);
    ASSERT_EQ(gReleaseCalls, 1);
    if (s->slice) {
        ASSERT_EQ(gFellThrough, 1);
    } else {
        ASSERT_EQ(gWaitCalls, s->waitsOnSuccess);
    }
    ASSERT_EQ(r, SCPI_RES_OK);
}

/* REFUSAL: arm refused -> error result, nothing after the arm ran, exactly one
 * error pushed (the helper's), exactly one release (the helper's). A mutant
 * that falls through shows up in at least one of these for EVERY site. */
static void refusal(const site_t *s) {
    reset(false);
    scpi_result_t r = s->fn(&gCtx);
    printf("  refusal %s\n", s->name);
    ASSERT_EQ(r, SCPI_RES_ERR);
    ASSERT_EQ(gArmCalls, 1);
    ASSERT_EQ(gErrorPushes, 1);
    ASSERT_EQ(gReleaseCalls, 1);
    ASSERT_EQ(gWaitCalls, 0);
    ASSERT_EQ(gResultCalls, 0);
    ASSERT_EQ(gFellThrough, 0);
    ASSERT_EQ(gCfg.mode, SD_CARD_MANAGER_MODE_NONE);
}

/* One TEST per site, named for the site, so a mutant's failure is attributable:
 * the driver requires the NAMED site to be the one that went red. */
#define SITE_TESTS(ix, tag)                                  \
    TEST(tag##_control) { control(&SITES[ix]); }             \
    TEST(tag##_refusal_exits) { refusal(&SITES[ix]); }
SITE_TESTS(0, CrcStart)
SITE_TESTS(1, GetData)
SITE_TESTS(2, ListDir)
SITE_TESTS(3, Delete)
SITE_TESTS(4, Format)
SITE_TESTS(5, SpaceGet)
SITE_TESTS(6, Benchmark)
SITE_TESTS(7, Streaming)

/* FORmat's refusal also retracts what it published (#964); cheap to assert and
 * it proves the callback form of the arm is the one being driven. */
TEST(Format_refusal_retracts_via_the_callback) {
    reset(false);
    (void)SCPI_StorageSDFormat(&gCtx);
    ASSERT_EQ(gClearFormat, 1);
}

int main(void) {
    RUN(CrcStart_control);   RUN(CrcStart_refusal_exits);
    RUN(GetData_control);    RUN(GetData_refusal_exits);
    RUN(ListDir_control);    RUN(ListDir_refusal_exits);
    RUN(Delete_control);     RUN(Delete_refusal_exits);
    RUN(Format_control);     RUN(Format_refusal_exits);
    RUN(SpaceGet_control);   RUN(SpaceGet_refusal_exits);
    RUN(Benchmark_control);  RUN(Benchmark_refusal_exits);
    RUN(Streaming_control);  RUN(Streaming_refusal_exits);
    RUN(Format_refusal_retracts_via_the_callback);
    return TEST_SUMMARY();
}
