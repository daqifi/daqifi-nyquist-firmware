#define LOG_LVL     LOG_LEVEL_SCPI
#define LOG_MODULE  LOG_MODULE_SCPI

/* ************************************************************************** */
/** Descriptive File Name

  @Company
    Company Name

  @File Name
    filename.c

  @Summary
    Brief description of the file.

  @Description
    Describe the purpose of this file.
 */
/* ************************************************************************** */

/* ************************************************************************** */
/* ************************************************************************** */
/* Section: Included Files                                                    */
/* ************************************************************************** */
/* ************************************************************************** */
#include "SCPIStorageSD.h"
#include "Util/SpiBusHealth.h"
#include "services/streaming.h"
#include "SCPIInterface.h"
#include "../sd_card_services/sd_card_manager.h"
#include "../wifi_services/wifi_tcp_server.h"  /* #598 ContextIsTcp */
#include "../wifi_services/wifi_manager.h"     /* #589 FW-update owner */
#include "app_freertos.h"                       /* #589 live SPI-owner test */
#include "../../state/runtime/BoardRuntimeConfig.h"
#include "system/fs/sys_fs_media_manager.h"
#include "system/fs/sys_fs.h"
#include "Util/Logger.h"
#include "../UsbCdc/UsbCdc.h"
// SPI coordination removed from enable level - both WiFi and SD can be enabled concurrently
// SPI coordination handled at operation level when needed
#include <string.h>
#include "driver/sdspi/drv_sdspi.h"

/* Weak fallback: if Harmony regenerates drv_sdspi.c and removes our custom
 * DRV_SDSPI_GetCID(), the linker uses this stub instead. The SCPI command
 * returns an error rather than crashing. */
bool __attribute__((weak)) DRV_SDSPI_GetCID(uint8_t* cidBuffer, size_t bufLen) {
    (void)cidBuffer; (void)bufLen;
    return false;
}

#define SCPI_SD_LIST_TIMEOUT_MS 10000
#define SCPI_SD_DELETE_TIMEOUT_MS 5000
#define SCPI_SD_FORMAT_TIMEOUT_MS 30000
#define SCPI_SD_SPACE_TIMEOUT_MS 10000

/* ************************************************************************** */
/* ************************************************************************** */
/* Section: File Scope or Global Data                                         */
/* ************************************************************************** */
/* ************************************************************************** */





/* ************************************************************************** */
/* ************************************************************************** */
// Section: Local Functions                                                   */
/* ************************************************************************** */
/* ************************************************************************** */




/* ************************************************************************** */
/* ************************************************************************** */
// Section: Interface Functions                                               */
/* ************************************************************************** */

/* ************************************************************************** */
#define LAN_ACTIVE_ERROR_MSG "\r\nError !! Please Disable LAN\r\n"
#define SD_CARD_NOT_ENABLED_ERROR_MSG "\r\nError !! Please Enabled SD Card\r\n"
#define SD_CARD_NOT_PRESENT_ERROR_MSG "\r\nError !! No SD Card Detected\r\n"

// Log format for SD busy errors - use LOG_SD_BUSY("COMMAND") for consistency.
// #782: the state/mode pair is what makes a refusal actionable - every busy
// state returns the same -200, so without it "busy" cannot be told from
// "wedged". Both are logged because IsBusy() has two independent causes.
#define LOG_SD_BUSY(cmd) LOG_E("SD:" cmd " - SD card busy, state=%s mode=%s\r\n", \
                               sd_card_manager_GetStateName(), \
                               sd_card_manager_GetModeName())

/* #589: refuse immediately when the SD task is suspended.
 *
 * While WiFi streaming (or a WiFi FW update, or the jam quarantine) owns
 * SPI4, app_SDCardTask parks in APP_SD_STATE_SUSPENDED and pumps neither
 * DRV_SDSPI_Tasks() nor sd_card_manager_ProcessState(). Arming an operation
 * then cannot work: nothing advances the state machine, so the caller waits
 * out the whole WaitForCompletion timeout (10 s for LIST/SPACe, measured
 * 10.38 s on the bench) and gets a -200 whose hint blames "#689: directory
 * too large" -- which is wrong, and reproduces on an empty card.
 *
 * Worse, sd_card_manager_UpdateSettings() parks the machine at DEINIT on the
 * way in, so every LATER command fails instantly at the IsBusy guard for the
 * rest of the session and reports a state that looks wedged.
 *
 * Refusing up front costs the caller nothing it could have had, and says
 * something true. Same spirit as #782 adding state/mode to LOG_SD_BUSY: every
 * refusal returns -200, so the log line is what makes it actionable.
 *
 * NOT applied to SYST:STOR:SD:ENAble -- that is the manual escape hatch and
 * must keep working -- nor to pure config/result reads.
 */
const char *SD_SuspendReasonText(void)
{
    if (!app_SDCard_SpiOwnedByWifi() && !SpiBusHealth_IsSdSuspended()) {
        return NULL;
    }
    /* Quarantine first: it is the one that does NOT clear on its own. */
    if (SpiBusHealth_IsSdQuarantined()) {
        return "SD quarantined after a bus jam - reseat or remove the card, "
               "then SYST:STOR:SD:ENAble 1 to retry";
    }
    if (wifi_manager_IsWifiFirmwareUpdateActive()) {
        return "a WiFi firmware update owns SPI4 - retry when it completes";
    }
    return "WiFi streaming owns SPI4 - SYST:STR:STOP first";
}


/* #589: arm an SD operation, or report why it could not be armed.
 *
 * sd_card_manager_UpdateSettings() refuses while the SD task is suspended, and
 * NO caller historically checked its return -- so a command whose guard passed
 * and then lost a race to a WiFi FW-update or a quarantine would either report
 * SUCCESS having armed nothing (FORmat, CRC and GET return OK immediately) or
 * sit out its WaitForCompletion timeout. Both are worse than saying no.
 */
static bool SD_ArmOrRefuse(scpi_t *context, const char *cmd,
                           sd_card_manager_settings_t *cfg)
{
    /* #829: the arm is where ownership hands over from the SCPI claim flag to
     * `mode`. Release on BOTH paths and there is no gap: on success `mode !=
     * MODE_NONE` already keeps IsBusy() true, and on failure the caller clears
     * `mode` too. Centralised here so no entry point can leak the flag. */
    if (sd_card_manager_UpdateSettings(cfg)) {
        sd_card_manager_ReleaseClaim();
        return true;
    }
    sd_card_manager_ReleaseClaim();
    const char *why = SD_SuspendReasonText();
    LOG_E("SD:%s - could not arm the operation: %s\r\n", cmd,
          why ? why : "the SD task is not accepting work");
    SCPI_ErrorPush(context, SCPI_ERROR_EXECUTION_ERROR);
    return false;
}


/* #829: ATOMIC claim of the SD manager, modelled on the #736 BENCH interlock.
 *
 * Every SD entry point checked sd_card_manager_IsBusy() and then wrote `mode`
 * as two separate steps. USB SCPI (pri 7) preempts WiFi SCPI (pri 2) with no
 * shared dispatch mutex, so both callers could pass the check and both write
 * the shared operand fields -- including `replyTarget`/`replyGeneration`,
 * which decide WHICH INTERFACE the reply is written to. #598/#599 guard the
 * delivery of an async reply; they do not guard this window.
 *
 * IsBusy() is true whenever `mode != MODE_NONE`, so writing `mode` IS the
 * claim. Doing both under one critical section makes exactly one caller the
 * owner; the loser refuses instead of overwriting the winner.
 *
 * Ordering matters as much as atomicity -- the #736 comment learned that the
 * hard way. Callers must claim BEFORE touching any shared field. Parsing and
 * validation may precede it (they only read the SCPI context), but every write
 * to opDirectory/opFile/replyTarget/replyGeneration must follow it, and any
 * failure path after a successful claim must release with MODE_NONE.
 *
 * Note the pre-existing taskENTER_CRITICAL around those operand writes guards
 * a TORN write, not this race: it makes each write atomic without deciding
 * which caller's value survives.
 */
static bool SD_ClaimOrRefuse(scpi_t *context, const char *cmd)
{
    /* Takes no cfg/mode: the claim is a flag inside the manager, and `mode`
     * is written by the CALLER after its operands (see sd_card_manager.h).
     * Passing them here would suggest this function arms something. */
    bool busy = !sd_card_manager_TryClaim();
    if (busy) {
        /* LOG_SD_BUSY() concatenates a string LITERAL ("SD:" cmd " - ..."), so
         * it cannot take this const char* -- format the name instead. */
        LOG_E("SD:%s - SD card busy, state=%s mode=%s\r\n", cmd,
              sd_card_manager_GetStateName(),
              sd_card_manager_GetModeName());
        SCPI_ErrorPush(context, SCPI_ERROR_EXECUTION_ERROR);
        return false;
    }
    return true;
}

static bool SD_RefuseIfSuspended(scpi_t *context, const char *cmd)
{
    /* The LIVE condition, not SpiBusHealth_IsSdSuspended(): the SD task runs
     * at priority 5 and this runs at 7, so a single USB packet holding
     * `STR:INT 1`, `STR:START ...` and an SD command can execute end to end
     * before the SD task next runs and publishes anything. The flag is right
     * for REPORTING what the task is doing; for REFUSING, the question is
     * whether WiFi owns the bus now, which is answerable directly. */
    if (!app_SDCard_SpiOwnedByWifi() && !SpiBusHealth_IsSdSuspended()) {
        return false;
    }
    const char *why = SD_SuspendReasonText();
    LOG_E("SD:%s refused - SD suspended: %s\r\n", cmd,
          why ? why : "SPI4 is owned elsewhere");
    SCPI_ErrorPush(context, SCPI_ERROR_EXECUTION_ERROR);
    return true;
}

/**
 * @brief Check if SD card media is present
 * @param context SCPI context for error reporting
 * @return true if present, false if not (error already pushed to context)
 */
static bool SCPI_CheckSDCardPresent(scpi_t *context) {
    if (!SYS_FS_MEDIA_MANAGER_MediaStatusGet(SD_CARD_MANAGER_DISK_DEV_NAME)) {
        LOG_E("SD - No SD card detected\r\n");
        context->interface->write(context, SD_CARD_NOT_PRESENT_ERROR_MSG, strlen(SD_CARD_NOT_PRESENT_ERROR_MSG));
        SCPI_ErrorPush(context, SCPI_ERROR_EXECUTION_ERROR);
        return false;
    }
    return true;
}


/* #612: shared validation for SCPI-provided SD file/directory names.
 * The FS root is the card itself, so exposure is bounded, but '..' can
 * escape the configured directory (GET/DELete/CRC) and stray separators
 * or control characters produce undefined FatFS behavior. Interior '/'
 * stays legal (subdirectory paths like "DAQiFi/sub/file.csv"). */
static bool SD_ValidatePathParam(const char *p, size_t len)
{
    if (len == 0u) {
        return false;
    }
    if (p[0] == '/' || p[0] == '\\') {
        return false;               /* absolute paths */
    }
    /* Reject '.' and '..' only as whole path SEGMENTS (between '/'), not as
     * substrings - Qodo #615: the old substring check false-rejected
     * legitimate names like "data..csv". */
    size_t segStart = 0;
    for (size_t i = 0; i <= len; i++) {
        unsigned char c = (i < len) ? (unsigned char)p[i] : (unsigned char)'/';
        if (i < len && (c < 0x20u || c == 0x7Fu || c == '\\' || c == ':')) {
            return false;           /* control chars, backslash, drive prefix */
        }
        if (c == '/') {
            size_t segLen = i - segStart;
            if (segLen == 1u && p[segStart] == '.') {
                return false;       /* "." segment */
            }
            if (segLen == 2u && p[segStart] == '.' && p[segStart + 1u] == '.') {
                return false;       /* ".." traversal segment */
            }
            segStart = i + 1u;
        }
    }
    return true;
}

/* #747: SYST:STOR:SD:LISt? prints every entry as "<directory>/<name>", but the
 * operand sites below prepend the configured directory themselves — so handing
 * back the exact string the device just printed builds "DAQiFi/DAQiFi/<name>",
 * the open fails, and for SD:GET the host receives a bare __END_OF_FILE__ with
 * SYST:ERR? still reading "No error". That is indistinguishable from an empty
 * file, which is what made #747 read as a device fault rather than a rejected
 * path.
 *
 * Accept the round-trip form by dropping ONE leading "<directory>/". Traversal
 * stays impossible: the prefix must match the device's own configured
 * directory exactly, and SD_ValidatePathParam still rejects absolute paths and
 * "." / ".." segments in what remains.
 *
 * Case-sensitive on purpose — this normalizes the exact form the device emits,
 * not arbitrary user spellings. Returns the (possibly advanced) pointer and
 * updates *pLen. */
static const char* SD_StripConfiguredDir(const char *p, size_t *pLen,
                                         const char *directory)
{
    if (p == NULL || pLen == NULL || directory == NULL) {
        return p;
    }
    size_t dirLen = strlen(directory);
    while (dirLen > 0u && directory[dirLen - 1u] == '/') {
        dirLen--;                    /* configured dir may carry a trailing '/' */
    }
    /* Need at least one character after "<dir>/" — a bare "DAQiFi/" is not a
     * filename and must fall through to the existing validation. */
    if (dirLen == 0u || *pLen <= dirLen + 1u) {
        return p;
    }
    if (strncmp(p, directory, dirLen) != 0 || p[dirLen] != '/') {
        return p;
    }
    *pLen -= (dirLen + 1u);
    p += dirLen + 1u;
    /* A configured directory carrying its own trailing slash makes the listing
     * print "<dir>//<name>" (it concatenates dirPath verbatim with "%s/%s"),
     * so one more separator can be left over here. Without this the remainder
     * starts with '/' and SD_ValidatePathParam rejects it as an absolute path
     * — the device's own output still would not round-trip (audit #749).
     * Traversal is unaffected: the validator below still runs, and it rejects
     * "." and ".." as whole segments regardless of how many slashes precede
     * them. */
    while (*pLen > 1u && *p == '/') {
        p++;
        (*pLen)--;
    }
    return p;
}

scpi_result_t SCPI_StorageSDEnableSet(scpi_t * context){
    int param1;
    scpi_result_t result = SCPI_RES_ERR;
    sd_card_manager_settings_t* pSDCardRuntimeConfig = BoardRunTimeConfig_Get(BOARDRUNTIME_SD_CARD_SETTINGS);

    if (!SCPI_ParamInt32(context, &param1, TRUE)) {
        result = SCPI_RES_ERR;
        goto __exit_point;
    }

    if (param1 != 0) {
        // Enable SD card
        LOG_D("SD:ENAble - Enabling SD card manager\r\n");
        pSDCardRuntimeConfig->enable = true;
        // #589: manual re-enable clears a bus-jam quarantine (user has
        // presumably reseated or replaced the card).
        if (SpiBusHealth_IsSdQuarantined()) {
            SpiBusHealth_SetSdQuarantine(false);
            Streaming_QuesExternalClear(STREAMING_QUES_SPI_BUS_FAULT);
        }
    } else {
        // Disable SD card - check if busy first to prevent data loss
        if (sd_card_manager_IsBusy()) {
            LOG_SD_BUSY("ENAble");
            SCPI_ErrorPush(context, SCPI_ERROR_EXECUTION_ERROR);
            result = SCPI_RES_ERR;
            goto __exit_point;
        }
        LOG_D("SD:ENAble - Disabling SD card manager\r\n");
        pSDCardRuntimeConfig->enable = false;

        /* #759: the card is going away -- do not leave streaming aimed at it.
         * The same release is needed from the SD task's two internal
         * auto-disables (mount failure, unsupported filesystem), which an
         * adversarial audit found the first version of this fix had missed, so
         * the logic lives in one helper rather than three copies. */
        Streaming_SdInterfaceReleased();
    }
    pSDCardRuntimeConfig->mode = SD_CARD_MANAGER_MODE_NONE;
    sd_card_manager_UpdateSettings(pSDCardRuntimeConfig);
    result = SCPI_RES_OK;
__exit_point:
    return result;
}
scpi_result_t SCPI_StorageSDEnableGet(scpi_t * context){
    sd_card_manager_settings_t* pSDCardRuntimeConfig = BoardRunTimeConfig_Get(BOARDRUNTIME_SD_CARD_SETTINGS);
    SCPI_ResultInt32(context, pSDCardRuntimeConfig->enable ? 1 : 0);
    return SCPI_RES_OK;
}

// Global variables for benchmark results.
// Defined here rather than beside SCPI_StorageSDBenchmark because
// SCPI_StorageSDLoggingSet below reads testInProgress to reject an SD:FILE
// that would race the benchmark's target (#736).
typedef struct {
    uint32_t totalBytesWritten;
    uint32_t totalTimeMs;
    uint32_t writeSpeedBps;
    bool testInProgress;
    bool resultAvailable;
} SDBenchmarkResults_t;

/* volatile: written by the SCPI task running a benchmark and read by the
 * OTHER transport's SCPI task (SCPI_StorageSDLoggingSet's guard below, and
 * the benchmark's own re-entrancy claim). Per CLAUDE.md a value written by
 * one task and read by another needs volatile so the compiler cannot cache
 * it in a register. volatile does NOT make the read-modify-write atomic —
 * the claim still takes a critical section (#736). */
static volatile SDBenchmarkResults_t gSDBenchmarkResults = {0};

scpi_result_t SCPI_StorageSDLoggingSet(scpi_t * context) {
    /* #747: NULL, not indeterminate. SCPI_ParamCharacters(..., mandatory=false)
     * leaves pBuff untouched when the argument is omitted, and the operand
     * normalizer is called unconditionally. fileLen is 0 in that case so the
     * normalizer returns before dereferencing anything, but merely READING an
     * indeterminate pointer is undefined — and this file is built at -O3.
     * Initializing costs nothing and makes the NULL guard meaningful. */
    const char* pBuff = NULL;
    size_t fileLen = 0;
 
    scpi_result_t result = SCPI_RES_ERR;
    sd_card_manager_settings_t* pSDCardRuntimeConfig = BoardRunTimeConfig_Get(BOARDRUNTIME_SD_CARD_SETTINGS);

    if (!pSDCardRuntimeConfig->enable) {
        context->interface->write(context, SD_CARD_NOT_ENABLED_ERROR_MSG, strlen(SD_CARD_NOT_ENABLED_ERROR_MSG));
        result = SCPI_RES_ERR;
        goto __exit_point;
    }

    // Check if SD card is busy with another operation
    /* NOT gated on the #589 suspension: this command only STAGES the logging
     * target name. It sets no mode and calls no sd_card_manager_UpdateSettings,
     * so it needs nothing from the suspended SD task, and refusing it would
     * stop a client preparing the next session while WiFi streams. */

    if (sd_card_manager_IsBusy()) {
        LOG_SD_BUSY("FILE");
        SCPI_ErrorPush(context, SCPI_ERROR_EXECUTION_ERROR);
        result = SCPI_RES_ERR;
        goto __exit_point;
    }

    /* #736: a running benchmark OWNS the logging target. sd_card_manager_IsBusy()
     * above does not cover it — the benchmark publishes its temp name and only
     * then sets mode=WRITE and calls sd_card_manager_UpdateSettings(), and IsBusy()
     * stays false across that whole span (mode NONE, state IDLE), so an SD:FILE
     * from the other transport is accepted right in the middle of it.
     * UpdateSettings() snapshots the settings by memcpy at call time, so such a
     * command could make the benchmark open and overwrite the USER'S file instead
     * of benchmark_XXXX.dat.
     *
     * Rejecting is the honest answer rather than a smaller race window: the
     * benchmark restores the target on exit, so a set accepted during it would be
     * discarded moments later anyway — the caller would be told "OK" about a
     * change that does not survive. Failing tells them to retry, and it makes the
     * benchmark's save/restore pairing airtight instead of merely narrow. */
    if (gSDBenchmarkResults.testInProgress) {
        SCPI_ExecutionError(context,
                            "SYST:STOR:SD:FILE: rejected, benchmark in progress");
        result = SCPI_RES_ERR;
        goto __exit_point;
    }

    // Check if SD card is present
    if (!SCPI_CheckSDCardPresent(context)) {
        result = SCPI_RES_ERR;
        goto __exit_point;
    }

    SCPI_ParamCharacters(context, &pBuff, &fileLen, false);
    /* #747: accept the "<directory>/<name>" form SD:LISt? prints. Before the
     * length check, so a max-length name is not rejected for the prefix. */
    pBuff = SD_StripConfiguredDir(pBuff, &fileLen, pSDCardRuntimeConfig->directory);

    if (fileLen > 0) {
        if (fileLen > SD_CARD_MANAGER_CONF_FILE_NAME_LEN_MAX) {
            LOG_E("SD:FILE - Filename too long: %zu bytes, max: %zu\r\n",
                  fileLen, (size_t)SD_CARD_MANAGER_CONF_FILE_NAME_LEN_MAX);
            result = SCPI_RES_ERR;
            goto __exit_point;
        }
        if (!SD_ValidatePathParam(pBuff, fileLen)) {   /* #612 */
            SCPI_ErrorPush(context, SCPI_ERROR_ILLEGAL_PARAMETER_VALUE);
            result = SCPI_RES_ERR;
            goto __exit_point;
        }
        /* #736: make the write atomic w.r.t. the other SCPI transport and the
         * benchmark's restore. Without this the copy can be preempted
         * mid-name — USB SCPI (pri 7) preempts WiFi SCPI (pri 2) and there is
         * no cross-transport dispatch mutex — and a reader that takes a
         * critical section still sees the write RESUME afterwards, splicing
         * the tail of one name onto another. Two fixed-size ops on a 41-byte
         * field; the reader side is SCPI_StorageSDBenchmark's restore. */
        /* Re-validate the benchmark guard HERE, under the same critical
         * section as the write. The check near the top of this function is
         * only a fast path: SCPI_CheckSDCardPresent, SCPI_ParamCharacters and
         * SD_ValidatePathParam all run between it and this point, and none of
         * them is in a critical section. So WiFi SCPI (pri 2) can pass the
         * early check while no benchmark exists, be preempted by USB SCPI
         * (pri 7) starting one, and then resume PAST the now-stale check and
         * overwrite `file` while the benchmark is mid-session.
         *
         * That is not a cosmetic race. gpSDCardSettings is the SAME struct
         * instance (BoardRunTimeConfig_Get returns the one object), so the SD
         * task's next pass would open the name written here and stream the
         * benchmark's synthetic pattern into the user's real log — the exact
         * silent-corruption class #728 exists to close, reopened through this
         * guard. The exit compare would then legitimately not match and skip
         * the restore, so neither transport ever learns of the collision.
         *
         * Checking and writing under one critical section makes it atomic,
         * mirroring the benchmark's own claim (#736 audit round 7). */
        bool benchOwnsTarget;
        taskENTER_CRITICAL();
        benchOwnsTarget = gSDBenchmarkResults.testInProgress;
        if (!benchOwnsTarget) {
            memcpy(pSDCardRuntimeConfig->file, pBuff, fileLen);
            pSDCardRuntimeConfig->file[fileLen] = '\0';
        }
        taskEXIT_CRITICAL();
        if (benchOwnsTarget) {
            SCPI_ExecutionError(context,
                                "SYST:STOR:SD:FILE: rejected, benchmark claimed "
                                "the target during validation");
            result = SCPI_RES_ERR;
            goto __exit_point;
        }
        LOG_D("SD:FILE - Set filename to '%s' (%zu bytes) dir='%s'\r\n",
              pSDCardRuntimeConfig->file, fileLen, pSDCardRuntimeConfig->directory);
    } else {
        LOG_D("SD:FILE - No filename provided, using existing: '%s'\r\n", pSDCardRuntimeConfig->file);
    }

    // Note: Mode is set to WRITE when streaming actually starts (in SCPI_StartStreaming)
    // This allows other SD operations (list, delete, etc.) to work after setting filename
    result = SCPI_RES_OK;
__exit_point:
    return result;
}

/* #306: start an async CRC32 computation over a stored file. Result via
 * SYST:STOR:SD:CRC? - mirrors the GET_SPACE request/query split so the SCPI
 * task never blocks on a multi-second file scan. */
scpi_result_t SCPI_StorageSDCrcStart(scpi_t * context) {
    /* #747: NULL, not indeterminate. SCPI_ParamCharacters(..., mandatory=false)
     * leaves pBuff untouched when the argument is omitted, and the operand
     * normalizer is called unconditionally. fileLen is 0 in that case so the
     * normalizer returns before dereferencing anything, but merely READING an
     * indeterminate pointer is undefined — and this file is built at -O3.
     * Initializing costs nothing and makes the NULL guard meaningful. */
    const char* pBuff = NULL;
    size_t fileLen = 0;
    sd_card_manager_settings_t* pSDCardRuntimeConfig =
            (sd_card_manager_settings_t*) BoardRunTimeConfig_Get(BOARDRUNTIME_SD_CARD_SETTINGS);

    if (!pSDCardRuntimeConfig->enable) {
        context->interface->write(context, SD_CARD_NOT_ENABLED_ERROR_MSG, strlen(SD_CARD_NOT_ENABLED_ERROR_MSG));
        return SCPI_RES_ERR;
    }

    if (sd_card_manager_IsBusy()) {
        LOG_SD_BUSY("CRC");
        SCPI_ErrorPush(context, SCPI_ERROR_EXECUTION_ERROR);
        return SCPI_RES_ERR;
    }
    if (!SCPI_ParamCharacters(context, &pBuff, &fileLen, TRUE)) {
        SCPI_ErrorPush(context, SCPI_ERROR_ILLEGAL_PARAMETER_VALUE);
        return SCPI_RES_ERR;
    }
    /* #747: accept the "<directory>/<name>" form SD:LISt? prints. */
    pBuff = SD_StripConfiguredDir(pBuff, &fileLen, pSDCardRuntimeConfig->directory);
    if (fileLen == 0 || fileLen > SD_CARD_MANAGER_CONF_FILE_NAME_LEN_MAX) {
        SCPI_ErrorPush(context, SCPI_ERROR_ILLEGAL_PARAMETER_VALUE);
        return SCPI_RES_ERR;
    }
    if (!SD_ValidatePathParam(pBuff, fileLen)) {   /* #612: CRC was the deferred site (post-#610-merge) */
        SCPI_ErrorPush(context, SCPI_ERROR_ILLEGAL_PARAMETER_VALUE);
        return SCPI_RES_ERR;
    }
    /* #829: claim before writing the shared operand. Validation above only
     * reads the SCPI context, so it stays ahead of the claim -- which also
     * preserves the #610 behaviour that a MALFORMED request is rejected
     * without disturbing a cached CRC. */
    if (!SD_ClaimOrRefuse(context, "CRC")) {
        return SCPI_RES_ERR;
    }
    /* #724: transient operand, not the logging target `file`. */
    memcpy(pSDCardRuntimeConfig->opFile, pBuff, fileLen);
    pSDCardRuntimeConfig->opFile[fileLen] = '\0';
    /* Suspension is checked HERE, after the operand has been parsed and
     * validated. Checking earlier meant a MALFORMED CRC request also hit the
     * refusal -- and the refusal invalidates the cached result, so a typo
     * issued during a WiFi stream destroyed a perfectly good CRC the user had
     * already computed. A request that was never well-formed should not have
     * that side effect.
     *
     * The invalidation itself is required: the refusal short-circuits
     * sd_card_manager_UpdateSettings, and the #306 fix that clears
     * crcResultValid when a new CRC is armed lives inside it. Without this,
     * SYST:STOR:SD:CRC? would hand back the PREVIOUS file's checksum as the
     * answer to the request just refused (bench-confirmed before the fix). */
    if (SD_RefuseIfSuspended(context, "CRC")) {
        sd_card_manager_InvalidateCrcResult();
        sd_card_manager_ReleaseClaim();  /* #829 release */
        return SCPI_RES_ERR;
    }

    /* Presence is probed AFTER the suspension check, not before. While the SD
     * task is suspended nothing refreshes the SDSPI attach cache that this
     * reads, so a card inserted during a WiFi stream still reads DETACHED --
     * and answering "No SD Card Detected" sends the user after a card that is
     * sitting in the slot. "SD suspended" is both true and actionable; the
     * stale probe is neither. */
    if (!SCPI_CheckSDCardPresent(context)) {
        sd_card_manager_ReleaseClaim();  /* #829 release */
        return SCPI_RES_ERR;
    }

    pSDCardRuntimeConfig->mode = SD_CARD_MANAGER_MODE_COMPUTE_CRC;  /* #829: LAST write */
    if (!SD_ArmOrRefuse(context, "CRC", pSDCardRuntimeConfig)) {
        pSDCardRuntimeConfig->mode = SD_CARD_MANAGER_MODE_NONE;
        /* #829: SD_ArmOrRefuse already released the claim */  /* mode must still be cleared */
        return SCPI_RES_ERR;
    }
    return SCPI_RES_OK;
}

/* #306: CRC result query. "BUSY" while computing; "0xXXXXXXXX,<bytes>" when
 * done; -230 (data corrupt/stale) if no valid result exists. */
scpi_result_t SCPI_StorageSDCrcGet(scpi_t * context) {
    uint32_t crc;
    uint64_t len;
    if (sd_card_manager_GetCrcResult(&crc, &len)) {
        char out[40];
        snprintf(out, sizeof(out), "0x%08lX,%llu",
                 (unsigned long)crc, (unsigned long long)len);
        SCPI_ResultText(context, out);
        return SCPI_RES_OK;
    }
    if (sd_card_manager_IsBusy()) {
        SCPI_ResultText(context, "BUSY");
        return SCPI_RES_OK;
    }
    SCPI_ErrorPush(context, SCPI_ERROR_DATA_CORRUPT);
    return SCPI_RES_ERR;
}

scpi_result_t SCPI_StorageSDGetData(scpi_t * context) {
    /* #747: NULL, not indeterminate. SCPI_ParamCharacters(..., mandatory=false)
     * leaves pBuff untouched when the argument is omitted, and the operand
     * normalizer is called unconditionally. fileLen is 0 in that case so the
     * normalizer returns before dereferencing anything, but merely READING an
     * indeterminate pointer is undefined — and this file is built at -O3.
     * Initializing costs nothing and makes the NULL guard meaningful. */
    const char* pBuff = NULL;
    size_t fileLen = 0;
    scpi_result_t result = SCPI_RES_ERR;
    sd_card_manager_settings_t* pSDCardRuntimeConfig = (sd_card_manager_settings_t*) BoardRunTimeConfig_Get(BOARDRUNTIME_SD_CARD_SETTINGS);
  
    if (!pSDCardRuntimeConfig->enable) {
        context->interface->write(context, SD_CARD_NOT_ENABLED_ERROR_MSG, strlen(SD_CARD_NOT_ENABLED_ERROR_MSG));
        result = SCPI_RES_ERR;
        goto __exit_point;
    }

    // Check if SD card is busy with another operation
    if (SD_RefuseIfSuspended(context, "GET")) {
        result = SCPI_RES_ERR;
        goto __exit_point;
    }

    if (sd_card_manager_IsBusy()) {
        LOG_SD_BUSY("GET");
        SCPI_ErrorPush(context, SCPI_ERROR_EXECUTION_ERROR);
        result = SCPI_RES_ERR;
        goto __exit_point;
    }

    // Check if SD card is present
    if (!SCPI_CheckSDCardPresent(context)) {
        result = SCPI_RES_ERR;
        goto __exit_point;
    }

    /* #703: SD:GET reads into the streaming pool's SD circular buffer, which the
     * auto-balancer shrinks after a non-SD stream. If it collapsed below the read
     * minimum, fail loudly and synchronously here (SCPI -200 + LOG_E) rather than
     * arming an async read that bails on the SD task. The floor fix keeps this
     * from ever tripping in practice; it's the host-visible mirror of the SD-task
     * terminal bail so a client gets an error instead of a bare EOF marker. */
    if (!sd_card_manager_ReadBufferReady()) {
        LOG_E("[SD] GET rejected: read buffer too small - start an SD-logging "
              "session or reboot to restore the SD buffer");
        SCPI_ErrorPush(context, SCPI_ERROR_EXECUTION_ERROR);
        result = SCPI_RES_ERR;
        goto __exit_point;
    }

    SCPI_ParamCharacters(context, &pBuff, &fileLen, false);
    /* #747: accept the "<directory>/<name>" form SD:LISt? prints. Before the
     * length check, so a max-length name is not rejected for carrying the
     * prefix the device itself emitted. */
    pBuff = SD_StripConfiguredDir(pBuff, &fileLen, pSDCardRuntimeConfig->directory);

    /* #829: claim before the operand and replyTarget writes below. replyTarget
     * decides which interface this file is delivered to, so a lost race here
     * sends a client's file to the OTHER transport. The two validation paths
     * inside the block release the claim before bailing. */
    if (!SD_ClaimOrRefuse(context, "GET")) {
        result = SCPI_RES_ERR;
        goto __exit_point;
    }

    if (fileLen > 0) {
        if (fileLen > SD_CARD_MANAGER_CONF_FILE_NAME_LEN_MAX) {
            sd_card_manager_ReleaseClaim();  /* #829 release */
            result = SCPI_RES_ERR;
            goto __exit_point;
        }
        if (!SD_ValidatePathParam(pBuff, fileLen)) {   /* #612 */
            sd_card_manager_ReleaseClaim();  /* #829 release */
            SCPI_ErrorPush(context, SCPI_ERROR_ILLEGAL_PARAMETER_VALUE);
            result = SCPI_RES_ERR;
            goto __exit_point;
        }
        /* #724: write the transient operand, NOT the logging target `file`. */
        memcpy(pSDCardRuntimeConfig->opFile, pBuff, fileLen);
        pSDCardRuntimeConfig->opFile[fileLen] = '\0';
    } else {
        /* #724: legacy no-argument GET operated on the current filename; preserve
         * that by copying the logging target into opFile (without clobbering it).
         * snprintf guarantees NUL-termination within the destination bound. */
        snprintf(pSDCardRuntimeConfig->opFile, sizeof(pSDCardRuntimeConfig->opFile),
                 "%s", pSDCardRuntimeConfig->file);
    }
    /* #598: route the async file data back to the interface that asked.
     * #599: capture the requesting TCP connection's generation so the SD
     * reply can't leak into a different client that later inherits the slot. */
    const bool getOverTcp = wifi_tcp_server_ContextIsTcp(context);
    pSDCardRuntimeConfig->replyTarget = getOverTcp
            ? SD_CARD_REPLY_WIFI_TCP : SD_CARD_REPLY_USB;
    pSDCardRuntimeConfig->replyGeneration =
            getOverTcp ? wifi_tcp_server_GetConnGeneration() : 0u;
    pSDCardRuntimeConfig->mode = SD_CARD_MANAGER_MODE_READ;  /* #829: LAST write */
    if (!SD_ArmOrRefuse(context, "GET", pSDCardRuntimeConfig)) {
        pSDCardRuntimeConfig->mode = SD_CARD_MANAGER_MODE_NONE;
        result = SCPI_RES_ERR;
        goto __exit_point;
    }
    result = SCPI_RES_OK;
__exit_point:
    return result;
}

scpi_result_t SCPI_StorageSDListDir(scpi_t * context){
    /* #747: NULL, not indeterminate. SCPI_ParamCharacters(..., mandatory=false)
     * leaves pBuff untouched when the argument is omitted, and the operand
     * normalizer is called unconditionally. fileLen is 0 in that case so the
     * normalizer returns before dereferencing anything, but merely READING an
     * indeterminate pointer is undefined — and this file is built at -O3.
     * Initializing costs nothing and makes the NULL guard meaningful. */
    const char* pBuff = NULL;
    size_t fileLen = 0;
    scpi_result_t result = SCPI_RES_ERR;
    sd_card_manager_settings_t* pSDCardRuntimeConfig = (sd_card_manager_settings_t*) BoardRunTimeConfig_Get(BOARDRUNTIME_SD_CARD_SETTINGS);
   

    if (!pSDCardRuntimeConfig->enable) {
        LOG_E("SD:LIST? - SD card not enabled\r\n");
        SCPI_ErrorPush(context, SCPI_ERROR_EXECUTION_ERROR);
        result = SCPI_RES_ERR;
        goto __exit_point;
    }

    // Refuse before probing the card: while the SD task is suspended the
    // presence check reads a cached attach flag that nothing is refreshing,
    // so asking it first would answer from stale state.
    if (SD_RefuseIfSuspended(context, "LISt")) {
        result = SCPI_RES_ERR;
        goto __exit_point;
    }

    // Check if SD card is actually present and mounted
    if (!SCPI_CheckSDCardPresent(context)) {
        result = SCPI_RES_ERR;
        goto __exit_point;
    }

    // Check if SD card is busy with another operation
    if (sd_card_manager_IsBusy()) {
        LOG_SD_BUSY("LISt");
        SCPI_ErrorPush(context, SCPI_ERROR_EXECUTION_ERROR);
        result = SCPI_RES_ERR;
        goto __exit_point;
    }

    // Get optional directory parameter
    SCPI_ParamCharacters(context, &pBuff, &fileLen, false);

    /* #829: claim BEFORE the operand and replyTarget writes below. Parsing
     * above only reads the SCPI context, so it is safe ahead of the claim;
     * everything from here down is owner-only, and the two validation failures
     * inside the block release the claim before bailing. */
    if (!SD_ClaimOrRefuse(context, "LISt")) {
        result = SCPI_RES_ERR;
        goto __exit_point;
    }

    if (fileLen > 0) {
        /* #799: bound against the field actually written (opDirectory), not
         * its sibling. They are the same size today, so this is not a live
         * overflow -- but checking one buffer and filling another is how a
         * later divergence becomes one silently. */
        if (fileLen >= sizeof(pSDCardRuntimeConfig->opDirectory)) {
            LOG_E("SD:LIST? - Directory path too long: %d bytes, max: %d\r\n", 
                  fileLen, sizeof(pSDCardRuntimeConfig->opDirectory) - 1);
            sd_card_manager_ReleaseClaim();  /* #829 release */
            result = SCPI_RES_ERR;
            goto __exit_point;
        }
        if (!SD_ValidatePathParam(pBuff, fileLen)) {   /* #612 */
            sd_card_manager_ReleaseClaim();  /* #829 release */
            SCPI_ErrorPush(context, SCPI_ERROR_ILLEGAL_PARAMETER_VALUE);
            result = SCPI_RES_ERR;
            goto __exit_point;
        }
        /* #799 part 3: the operand goes to the TRANSIENT field, not to the
         * persistent working directory. A query must not change the device.
         *
         * This used to memcpy into pSDCardRuntimeConfig->directory and leave it
         * there, which made SD:LISt? the only way to move the working
         * directory -- and it moved it as a SIDE EFFECT. One browse, or a typo,
         * silently redirected SD:GET, SD:DELete, SD:CRC and the next logging
         * session. The DELete case was the sharp one: the caller named a file,
         * the device deleted a same-named file somewhere else, and replied
         * success. Callers that want to MOVE the working directory now use
         * SD:DIRectory (part 2, already shipped).
         *
         * The client survey the issue asked for came back clean: daqifi-core
         * only ever sends the no-operand form, and python-core's
         * sd_list_files(dir) is a standalone listing that nothing chains into a
         * GET.
         *
         * On the critical section: it is NOT doing what the same guard does for
         * `directory`, and the rationale copied from parts 1-2 would be wrong
         * here. That field has a getter (SD:DIRectory?) reachable from the
         * OTHER SCPI transport, so an unguarded two-step write really can be
         * observed torn. `opDirectory` has no getter -- nothing outside the SD
         * manager reads it -- and a writer-only critical section cannot make a
         * reader's read coherent anyway; it only makes the WRITE indivisible
         * with respect to task switches.
         *
         * What actually keeps the SD task from reading this mid-update is the
         * IsBusy() interlock above: a second listing is refused while one is in
         * flight, so the field is not rewritten under a running LIST_DIRECTORY.
         *
         * The guard is kept regardless -- it costs a few instructions and keeps
         * all three sites that touch these path fields symmetric, so the next
         * person to add one inherits the safe shape rather than having to
         * work out which fields have external readers. */
        taskENTER_CRITICAL();
        memcpy(pSDCardRuntimeConfig->opDirectory, pBuff, fileLen);
        pSDCardRuntimeConfig->opDirectory[fileLen] = '\0';
        taskEXIT_CRITICAL();
    } else {
        /* No operand: list the working directory. Clearing is what makes the
         * operand transient -- otherwise the PREVIOUS listing's operand would
         * silently serve this one, which is the same defect wearing a
         * different hat. */
        taskENTER_CRITICAL();
        pSDCardRuntimeConfig->opDirectory[0] = '\0';
        taskEXIT_CRITICAL();
    }
    
    // Set mode to LIST_DIRECTORY and let sd_card_manager handle it
    const bool listOverTcp = wifi_tcp_server_ContextIsTcp(context);   /* #598 */
    pSDCardRuntimeConfig->replyTarget = listOverTcp
            ? SD_CARD_REPLY_WIFI_TCP : SD_CARD_REPLY_USB;
    pSDCardRuntimeConfig->replyGeneration =
            listOverTcp ? wifi_tcp_server_GetConnGeneration() : 0u;   /* #599 */
    pSDCardRuntimeConfig->mode = SD_CARD_MANAGER_MODE_LIST_DIRECTORY;  /* #829: LAST write */
    if (!SD_ArmOrRefuse(context, "LISt", pSDCardRuntimeConfig)) {
        pSDCardRuntimeConfig->mode = SD_CARD_MANAGER_MODE_NONE;
        /* #829: SD_ArmOrRefuse already released the claim */  /* mode must still be cleared */
        goto __exit_point;
    }

    // Wait for sd_card_manager to complete listing (up to 10 seconds for large
    // directories). #780: PUMPED — a listing is delivered through DataReadyCB
    // while we wait, and over USB this same task is what drains it. A plain
    // block deadlocks the two until the timeout fires, so every listing larger
    // than the idle USB buffer took exactly 10 s and reported failure.
    if (!sd_card_manager_WaitForCompletionPumped(SCPI_SD_LIST_TIMEOUT_MS)) {
        LOG_E("SD:LIST? - Operation timeout\r\n");
        pSDCardRuntimeConfig->mode = SD_CARD_MANAGER_MODE_NONE;
        sd_card_manager_UpdateSettings(pSDCardRuntimeConfig);
        SCPI_ErrorPush(context, SCPI_ERROR_EXECUTION_ERROR);
        result = SCPI_RES_ERR;
        goto __exit_point;
    }

    result = SCPI_RES_OK;
__exit_point:
    return result;

}

/**
 * @brief Perform SD card write speed benchmark
 * 
 * This function tests SD card write performance by writing a specified amount
 * of test data to the SD card and measuring the time taken.
 * 
 * Usage: STOR:SD:BENCH <size_kb> [,<pattern>]
 *   size_kb: Size of test data in kilobytes (1-1024)
 *   pattern: Optional test pattern (0=zeros, 1=sequential, 2=random)
 * 
 * Example: STOR:SD:BENCH 1024      # Write 1MB of zeros
 *          STOR:SD:BENCH 512,1     # Write 512KB of sequential data
 */
scpi_result_t SCPI_StorageSDBenchmark(scpi_t * context) {
    /* #589: the benchmark arms a WRITE like any other SD operation, so
     * it is refused while the SD task is suspended for the same reason.
     * It has no IsBusy guard of its own (#736: a running benchmark OWNS
     * the logging target), which is why it needed naming separately. */
    if (SD_RefuseIfSuspended(context, "BENCHmark")) {
        return SCPI_RES_ERR;
    }
    int32_t testSizeKB = 0;
    int32_t pattern = 0;
    scpi_result_t result = SCPI_RES_ERR;
    sd_card_manager_settings_t* pSDCardRuntimeConfig = BoardRunTimeConfig_Get(BOARDRUNTIME_SD_CARD_SETTINGS);

    /* #728: the benchmark writes its temp name into the SHARED logging target
     * (`file`) because it runs through mode=WRITE — the real logging path —
     * so it cannot use the `opFile` side-channel #724 added for the READ /
     * CRC / DELETE operands. Left as-is, a later SD-armed stream that does not
     * re-issue SYST:STOR:SD:FILE logs into benchmark_XXXX.dat instead of the
     * user's target: the same silent data-loss class as #724.
     *
     * The whole benchmark runs synchronously inside this callback (write loop,
     * then mode=NONE), so a save/restore is sufficient — no completion
     * callback needed. Restored at __exit_point on every path. */
    char savedLogFile[SD_CARD_MANAGER_CONF_FILE_NAME_LEN_MAX + 1];
    char benchLogFile[SD_CARD_MANAGER_CONF_FILE_NAME_LEN_MAX + 1] = {0};
    bool logFileClobbered = false;
    bool ownsBenchFlag = false;   /* #736: did THIS call claim testInProgress? */
    /* Size MUST match the struct field, which is [LEN_MAX + 1] — LEN_MAX (40)
     * is the longest ACCEPTED name and the field carries a 41st byte for the
     * terminator. Sizing this [LEN_MAX] silently dropped the 40th character of
     * a legal max-length target on restore (#736 audit).
     * memcpy, not snprintf("%s"): equal-sized buffers make GCC flag the format
     * as possibly truncating (-Werror=format-truncation). Copying the whole
     * fixed-size buffer carries the terminator with it; the explicit NUL is
     * belt-and-braces if `file` were ever unterminated. */
    /* Under the same critical section the SD:FILE setter and the restore path
     * use. A guarded writer does not protect an unguarded reader: this is a
     * 41-byte multi-word copy, so USB SCPI (pri 7) landing a SYST:STOR:SD:FILE
     * mid-copy would leave savedLogFile holding a prefix of the old name and a
     * suffix of the new one — a filename no command ever selected, which the
     * restore would then write back. All three sites that touch `file` are now
     * symmetric (#736 audit). */
    /* NB: the snapshot itself happens AFTER the ownership claim below, not
     * here — see the comment at that point for why. */
    
    // Check if SD card is enabled
    if (!pSDCardRuntimeConfig->enable) {
        context->interface->write(context, SD_CARD_NOT_ENABLED_ERROR_MSG, strlen(SD_CARD_NOT_ENABLED_ERROR_MSG));
        result = SCPI_RES_ERR;
        goto __exit_point;
    }
    
    // Double-check that SD card is actually present
    if (!SCPI_CheckSDCardPresent(context)) {
        result = SCPI_RES_ERR;
        goto __exit_point;
    }
    
    // Check if a test is already in progress
    if (gSDBenchmarkResults.testInProgress) {
        SCPI_ExecutionError(context, "SYST:STOR:SD:BENCH: benchmark already in progress");
        result = SCPI_RES_ERR;
        goto __exit_point;
    }
    
    // Get test size parameter (required)
    if (!SCPI_ParamInt32(context, &testSizeKB, TRUE)) {
        SCPI_ErrorPush(context, SCPI_ERROR_MISSING_PARAMETER);
        result = SCPI_RES_ERR;
        goto __exit_point;
    }
    
    // Validate test size (1KB to 1024KB)
    if (testSizeKB < 1 || testSizeKB > 1024) {
        SCPI_ErrorPush(context, SCPI_ERROR_ILLEGAL_PARAMETER_VALUE);
        context->interface->write(context, "\r\nError: Test size must be 1-1024 KB\r\n", 38);
        result = SCPI_RES_ERR;
        goto __exit_point;
    }
    
    // Get optional pattern parameter
    if (!SCPI_ParamInt32(context, &pattern, FALSE)) {
        pattern = 0; // Default to zeros
    }
    
    // Validate pattern
    if (pattern < 0 || pattern > 2) {
        SCPI_ErrorPush(context, SCPI_ERROR_ILLEGAL_PARAMETER_VALUE);
        context->interface->write(context, "\r\nError: Pattern must be 0-2\r\n", 30);
        result = SCPI_RES_ERR;
        goto __exit_point;
    }
    
    /* #736 audit: ATOMIC test-and-set, and it must come BEFORE any write to
     * gSDBenchmarkResults. The early `if (testInProgress)` reject above is only
     * a fast path — parameter parsing sits between it and this point, and USB
     * SCPI (pri 7) preempts WiFi SCPI (pri 2) with no shared dispatch mutex, so
     * two BENCH invocations could both pass that check and both enter. Claiming
     * the flag under a critical section makes exactly one of them the owner; the
     * loser rejects here instead of running a second benchmark concurrently
     * (which would also corrupt the save/restore pairing this PR relies on).
     *
     * Ordering matters as much as atomicity: the counter reset used to run
     * ahead of the claim, so a loser zeroed totalBytesWritten/totalTimeMs/
     * writeSpeedBps and then bailed — destroying a completed run's results, or
     * letting BENCH? read resultAvailable=true beside zeroed counters, from a
     * call that never ran. Everything below this point is owner-only. */
    bool benchAlreadyRunning;
    taskENTER_CRITICAL();
    benchAlreadyRunning = gSDBenchmarkResults.testInProgress;
    if (!benchAlreadyRunning) {
        gSDBenchmarkResults.testInProgress = true;
    }
    taskEXIT_CRITICAL();
    if (benchAlreadyRunning) {
        SCPI_ExecutionError(context, "SYST:STOR:SD:BENCH: benchmark already in progress");
        result = SCPI_RES_ERR;
        goto __exit_point;
    }
    ownsBenchFlag = true;

    /* Snapshot the logging target HERE, after the claim — not at function
     * entry. SCPI_StorageSDLoggingSet rejects SD:FILE only once testInProgress
     * is set, so an SD:FILE from the other transport arriving during parameter
     * parsing is legitimately ACCEPTED. Snapshotting at entry captured the
     * pre-command name, and the exit restore then reverted the change the
     * device had just acknowledged — a silent lost update, and an arbitrary
     * boundary (rejected after the claim, silently discarded before it).
     *
     * Taking it after the claim makes the rule consistent in both directions:
     * an SD:FILE that is ACCEPTED always takes effect, and one that is
     * REJECTED never does. Under the same critical section as every other
     * reader of the field (Qodo #736). */
    taskENTER_CRITICAL();
    memcpy(savedLogFile, pSDCardRuntimeConfig->file, sizeof(savedLogFile));
    taskEXIT_CRITICAL();
    savedLogFile[sizeof(savedLogFile) - 1] = '\0';

    // Initialize benchmark results (owner-only — see the ordering note above)
    gSDBenchmarkResults.totalBytesWritten = 0;
    gSDBenchmarkResults.totalTimeMs = 0;
    gSDBenchmarkResults.writeSpeedBps = 0;
    gSDBenchmarkResults.resultAvailable = false;
    
    /* Create the test file name in our OWN buffer first, then publish it under
     * one critical section.
     *
     * benchLogFile is the sentinel the exit compare uses to tell "still ours"
     * from "someone else set a new target while we ran" (#728). Building it by
     * writing `file` and then reading `file` back was two separate unguarded
     * statements, so an SD:FILE landing between them would seed the sentinel
     * with the INTERLOPER's name — and sd_card_manager_IsBusy() is still false
     * here (mode is not set to WRITE until below), so such a command IS
     * accepted. The exit compare would then match and silently revert the
     * user's accepted target. Seeding from our own string instead makes the
     * sentinel un-poisonable, and makes this the fourth symmetric `file` site
     * (#736 audit round 6). */
    snprintf(benchLogFile, SD_CARD_MANAGER_CONF_FILE_NAME_LEN_MAX,
             "benchmark_%d.dat", (int)(xTaskGetTickCount() & 0xFFFF));
    benchLogFile[SD_CARD_MANAGER_CONF_FILE_NAME_LEN_MAX] = '\0';
    taskENTER_CRITICAL();
    memcpy(pSDCardRuntimeConfig->file, benchLogFile, sizeof(benchLogFile));
    pSDCardRuntimeConfig->file[SD_CARD_MANAGER_CONF_FILE_NAME_LEN_MAX] = '\0';
    taskEXIT_CRITICAL();
    logFileClobbered = true;   /* #728: restore the logging target on exit */
    
    // Set SD card to write mode
    /* #690: this WRITE-mode open goes through the #689 dir-file-cap guard too.
     * Clear the flag first so the poll observes only THIS request's outcome
     * (mirrors SCPI_StartStreaming / the #503 disk-full pattern). */
    sd_card_manager_ClearStartupDirFull();
    pSDCardRuntimeConfig->mode = SD_CARD_MANAGER_MODE_WRITE;
    sd_card_manager_UpdateSettings(pSDCardRuntimeConfig);

    // Wait for file to be open and ready before writing
    {
        int readyWait = 0;
        while (!sd_card_manager_IsWriteReady() && readyWait < 500) {
            if (sd_card_manager_StartupDirFull()) {   /* #690: early-exit */
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(10));
            readyWait++;
        }
        if (!sd_card_manager_IsWriteReady()) {
            if (sd_card_manager_StartupDirFull()) {
                /* #690: name the real cause instead of the card advisory.
                 * #689: the flag covers every "no writable location" cause, not
                 * just fullness — an FS fault reaching here would otherwise be
                 * reported as a full directory and send the operator to clear a
                 * card that is not the problem. */
                LOG_E("SD:BENCH refused (#689): %s\r\n",
                      sd_card_manager_WriteRefuseText());
            } else {
                LOG_E("SD:BENCH - File not ready after timeout\r\n");
                LOG_E("SD:BENCH - if reads/LIST work but writes hang, the card is "
                      "likely SPI-mode incompatible (wiki: SD-Card-Compatibility)\r\n");
            }
            SCPI_ErrorPush(context, SCPI_ERROR_EXECUTION_ERROR);
            pSDCardRuntimeConfig->mode = SD_CARD_MANAGER_MODE_NONE;
            sd_card_manager_UpdateSettings(pSDCardRuntimeConfig);
            result = SCPI_RES_ERR;
            goto __exit_point;
        }
    }

    // Start benchmark timing
    uint32_t startTime = xTaskGetTickCount();

    // #347 / #350: take and release the shared SCPI response buffer PER
    // CHUNK. A large benchmark can run for many seconds; holding the shared
    // mutex across the whole loop would make every other SCPI command
    // (on either transport) wait for the benchmark to finish. Per-chunk
    // take/give lets other SCPI callbacks interleave between 512-byte
    // SD writes without losing the stack-safety properties of the shared
    // buffer pattern.
    const uint32_t kTestBufferChunk = 512;  // benchmark writes in 512 B blocks
    uint32_t bytesToWrite = testSizeKB * 1024;
    uint32_t bytesWritten = 0;
    
    while (bytesWritten < bytesToWrite) {
        uint32_t chunkSize = (bytesToWrite - bytesWritten > kTestBufferChunk) ?
                            kTestBufferChunk : (bytesToWrite - bytesWritten);

        uint8_t* testBuffer = (uint8_t*)SCPI_ResponseBuf_Take();
        if (testBuffer == NULL) {
            LOG_E("SD:BENCH - Could not acquire SCPI response buffer\r\n");
            SCPI_ErrorPush(context, SCPI_ERROR_EXECUTION_ERROR);
            pSDCardRuntimeConfig->mode = SD_CARD_MANAGER_MODE_NONE;
            sd_card_manager_UpdateSettings(pSDCardRuntimeConfig);
            result = SCPI_RES_ERR;
            goto __exit_point;  // Take failed — nothing to Give
        }

        // Fill buffer based on pattern
        switch (pattern) {
            case 0: // All zeros
                memset(testBuffer, 0x00, chunkSize);
                break;
            case 1: // Sequential
                for (uint32_t i = 0; i < chunkSize; i++) {
                    testBuffer[i] = (uint8_t)((bytesWritten + i) & 0xFF);
                }
                break;
            case 2: // Pseudo-random
                for (uint32_t i = 0; i < chunkSize; i++) {
                    testBuffer[i] = (uint8_t)(((bytesWritten + i) * 1103515245 + 12345) & 0xFF);
                }
                break;
        }

        // Write to SD card, pacing the producer to the drain rate: this
        // task (USB SCPI, pri 7) fills the SD circular buffer far faster
        // than the SD task (pri 5) drains it, so "buffer momentarily full"
        // is the NORMAL steady state of a throughput benchmark - not a
        // failure. The pre-2026-07-04 code aborted on the first short
        // write, which killed every benchmark at exactly the circular
        // buffer size (32768) and was misdiagnosed as a card problem.
        // Only a sustained stall (no drain progress for 10 s) is an error.
        size_t written = 0;
        uint32_t stallMs = 0;
        while ((written < chunkSize) && (stallMs < 10000U)) {
            size_t w = sd_card_manager_WriteToBuffer(
                (const char*)testBuffer + written, chunkSize - written);
            if (w == 0U) {
                vTaskDelay(pdMS_TO_TICKS(5));
                stallMs += 5U;
            } else {
                written += w;
                stallMs = 0U;
            }
        }
        SCPI_ResponseBuf_Give();

        if (written != chunkSize) {
            LOG_E("SD:BENCH - drain stalled >10s at %u/%u bytes\r\n", bytesWritten, bytesToWrite);
            LOG_E("SD:BENCH - if reads/LIST work but writes stall, the card is "
                  "likely SPI-mode incompatible (wiki: SD-Card-Compatibility)\r\n");
            SCPI_ErrorPush(context, SCPI_ERROR_EXECUTION_ERROR);
            pSDCardRuntimeConfig->mode = SD_CARD_MANAGER_MODE_NONE;
            sd_card_manager_UpdateSettings(pSDCardRuntimeConfig);
            result = SCPI_RES_ERR;
            goto __exit_point;
        }

        bytesWritten += written;
    }

    // Trigger flush: set mode to NONE so SD task drains buffer and closes file
    pSDCardRuntimeConfig->mode = SD_CARD_MANAGER_MODE_NONE;
    sd_card_manager_UpdateSettings(pSDCardRuntimeConfig);

    // Wait for SD card manager to fully drain, close file, and go idle
    {
        int idleWait = 0;
        while (!sd_card_manager_IsIdle() && idleWait < 500) {
            vTaskDelay(pdMS_TO_TICKS(10));
            idleWait++;
        }
        if (idleWait >= 500) {
            LOG_E("SD:BENCH - SD idle timeout after 5s\r\n");
            LOG_E("SD:BENCH - if reads/LIST work but writes hang, the card is "
                  "likely SPI-mode incompatible (wiki: SD-Card-Compatibility)\r\n");
        }
    }

    // Capture end time after full pipeline completion
    uint32_t endTime = xTaskGetTickCount();
    gSDBenchmarkResults.totalTimeMs = (endTime - startTime) * portTICK_PERIOD_MS;
    gSDBenchmarkResults.totalBytesWritten = bytesWritten;
    
    if (gSDBenchmarkResults.totalTimeMs > 0) {
        gSDBenchmarkResults.writeSpeedBps = (bytesWritten * 1000) / gSDBenchmarkResults.totalTimeMs;
    }
    
    gSDBenchmarkResults.resultAvailable = true;
    
    // Send immediate results
    char resultStr[128];
    snprintf(resultStr, sizeof(resultStr), 
             "\r\nBenchmark complete: %u bytes in %u ms = %u bytes/sec\r\n",
             (unsigned int)gSDBenchmarkResults.totalBytesWritten,
             (unsigned int)gSDBenchmarkResults.totalTimeMs,
             (unsigned int)gSDBenchmarkResults.writeSpeedBps);
    context->interface->write(context, resultStr, strlen(resultStr));
    
    result = SCPI_RES_OK;
    
__exit_point:
    /* #736 audit r4: clear the re-entrancy guard only AFTER the target is
     * restored, and clear it on every path from one place.
     *
     * It used to be cleared before this block — with a full
     * context->interface->write() of the result string in between — so the
     * `if (testInProgress)` guard at the top of this function was open for
     * milliseconds while `file` still held benchmark_XXXX.dat. A second
     * SYST:STOR:SD:BENCH from the other transport could enter there, capture
     * the UNRESTORED benchmark name as "the user's target", and overwrite
     * `file` with its own; this callback would then find the name no longer
     * its own, skip the restore by the deliberate "theirs wins" rule, and the
     * real target would be gone for good. Keeping the flag set until the
     * restore completes closes that window.
     *
     * #728: put the user's logging target back before returning, on EVERY
     * path — normal completion, the not-ready timeout, the buffer-take
     * failure and the write failure all land here. Re-publishing through
     * UpdateSettings matters as much as the field itself: the manager keeps
     * its own memcpy'd copy, so restoring only the runtime config would leave
     * the benchmark name live inside sd_card_manager.
     * The early parameter/enable/present errors return before the name is
     * overwritten, so the flag keeps this a no-op for them. */
    /* #736 audit: restore ONLY if the field still holds the name we wrote.
     * Once the manager reaches IDLE (polled just above), SD:FILE from the
     * OTHER transport is accepted — USB and WiFi run separate SCPI contexts
     * with no shared dispatch mutex, and this callback yields in vTaskDelay.
     * An unconditional restore would silently revert a target the user just
     * set successfully. If someone changed it, theirs wins and we leave it. */
    if (logFileClobbered) {
        /* Compare AND restore inside one critical section. Split, this is a
         * check-then-act on a field another SCPI context can rewrite: the
         * compare could read a half-written name, or a new target could land
         * between the compare and the memcpy and be silently clobbered
         * (#736 audit r2 + Qodo TOCTOU). Both transports' SCPI tasks are
         * below the syscall-priority ceiling, so a critical section excludes
         * them; the region is two fixed-size buffer ops on a 41-byte field.
         *
         * Terminator goes at [LEN_MAX], the field's LAST byte, not
         * [LEN_MAX - 1]: LEN_MAX (40) is the longest name the setter ACCEPTS
         * and the field is [LEN_MAX + 1] to hold its NUL. */
        taskENTER_CRITICAL();
        if (strncmp(pSDCardRuntimeConfig->file, benchLogFile,
                    SD_CARD_MANAGER_CONF_FILE_NAME_LEN_MAX + 1) == 0) {
            memcpy(pSDCardRuntimeConfig->file, savedLogFile, sizeof(savedLogFile));
            pSDCardRuntimeConfig->file[SD_CARD_MANAGER_CONF_FILE_NAME_LEN_MAX] = '\0';
        }
        taskEXIT_CRITICAL();
        /* Deliberately NOT sd_card_manager_UpdateSettings(): it unconditionally
         * forces currentProcessState = DEINIT, so publishing here would kick the
         * manager out of the IDLE state this callback just waited for, and
         * follow-on SD commands that gate on IsBusy() could be refused
         * (Qodo #736). Not publishing is also what SD:FILE itself does — the
         * setter only writes the runtime field and lets whoever arms SD
         * publish it (SCPI_StartStreaming and friends all call UpdateSettings).
         * So the restored name reaches the manager the same way any other
         * SD:FILE would. */
    }
    /* Only the invocation that CLAIMED the flag may clear it. The
     * "benchmark already in progress" early-reject also lands here, and an
     * unconditional clear would wipe the OTHER benchmark's in-progress flag —
     * turning the re-entrancy guard into a way to defeat itself. */
    if (ownsBenchFlag) {
        gSDBenchmarkResults.testInProgress = false;
    }
    return result;
}

/**
 * @brief Query SD card benchmark results
 * 
 * Returns the results of the last benchmark test in CSV format:
 * <bytes_written>,<time_ms>,<speed_bps>
 * 
 * Usage: STOR:SD:BENCH?
 */
scpi_result_t SCPI_StorageSDBenchmarkQuery(scpi_t * context) {
    char resultStr[128];
    
    if (!gSDBenchmarkResults.resultAvailable) {
        SCPI_ExecutionError(context, "SYST:STOR:SD:BENCH?: no benchmark result available");
        context->interface->write(context, "0,0,0\r\n", 7);
        return SCPI_RES_ERR;
    }
    
    // Return results in CSV format
    snprintf(resultStr, sizeof(resultStr), "%u,%u,%u\r\n",
             (unsigned int)gSDBenchmarkResults.totalBytesWritten,
             (unsigned int)gSDBenchmarkResults.totalTimeMs,
             (unsigned int)gSDBenchmarkResults.writeSpeedBps);
    
    context->interface->write(context, resultStr, strlen(resultStr));
    
    return SCPI_RES_OK;
}

/**
 * @brief Delete a file from the SD card
 *
 * Usage: SYST:STOR:SD:DELete "filename"
 *
 * Example: SYST:STOR:SD:DEL "test.csv"
 */
scpi_result_t SCPI_StorageSDDelete(scpi_t * context) {
    /* #747: NULL, not indeterminate. SCPI_ParamCharacters(..., mandatory=false)
     * leaves pBuff untouched when the argument is omitted, and the operand
     * normalizer is called unconditionally. fileLen is 0 in that case so the
     * normalizer returns before dereferencing anything, but merely READING an
     * indeterminate pointer is undefined — and this file is built at -O3.
     * Initializing costs nothing and makes the NULL guard meaningful. */
    const char* pBuff = NULL;
    size_t fileLen = 0;
    scpi_result_t result = SCPI_RES_ERR;
    sd_card_manager_settings_t* pSDCardRuntimeConfig = BoardRunTimeConfig_Get(BOARDRUNTIME_SD_CARD_SETTINGS);

    if (!pSDCardRuntimeConfig->enable) {
        LOG_E("SD:DELete - SD card not enabled\r\n");
        SCPI_ErrorPush(context, SCPI_ERROR_EXECUTION_ERROR);
        result = SCPI_RES_ERR;
        goto __exit_point;
    }

    // Check if SD card is busy with another operation
    if (SD_RefuseIfSuspended(context, "DELete")) {
        result = SCPI_RES_ERR;
        goto __exit_point;
    }

    if (sd_card_manager_IsBusy()) {
        LOG_SD_BUSY("DELete");
        SCPI_ErrorPush(context, SCPI_ERROR_EXECUTION_ERROR);
        result = SCPI_RES_ERR;
        goto __exit_point;
    }

    // Check if SD card is present
    if (!SCPI_CheckSDCardPresent(context)) {
        result = SCPI_RES_ERR;
        goto __exit_point;
    }

    // Get filename parameter (required)
    SCPI_ParamCharacters(context, &pBuff, &fileLen, false);
    /* #747: accept the "<directory>/<name>" form SD:LISt? prints. BEFORE the
     * length check, like the other three operand sites: the prefix costs 7
     * bytes against a 40-byte limit, so checking first rejects listed paths
     * whose filename is 34-40 characters — a length SD:FILE accepts and which
     * GET/CRC round-trip, leaving DELETE the odd one out (audit #749). */
    pBuff = SD_StripConfiguredDir(pBuff, &fileLen, pSDCardRuntimeConfig->directory);

    if (fileLen == 0 || fileLen > SD_CARD_MANAGER_CONF_FILE_NAME_LEN_MAX) {
        LOG_E("SD:DELete - Invalid filename length: %d\r\n", fileLen);
        SCPI_ErrorPush(context, SCPI_ERROR_ILLEGAL_PARAMETER_VALUE);
        result = SCPI_RES_ERR;
        goto __exit_point;
    }

    // Set the filename
    if (!SD_ValidatePathParam(pBuff, fileLen)) {   /* #612 */
        SCPI_ErrorPush(context, SCPI_ERROR_ILLEGAL_PARAMETER_VALUE);
        goto __exit_point;
    }
    /* #829: claim before writing the shared operand -- otherwise a second
     * caller could overwrite opFile between this write and the mode
     * assignment, and the winner would delete the LOSER's filename. */
    if (!SD_ClaimOrRefuse(context, "DELete")) {
        result = SCPI_RES_ERR;
        goto __exit_point;
    }
    /* #724: delete the transient operand, not the logging target `file`. */
    memcpy(pSDCardRuntimeConfig->opFile, pBuff, fileLen);
    pSDCardRuntimeConfig->opFile[fileLen] = '\0';
    LOG_D("SD:DELete - Deleting file '%s'\r\n", pSDCardRuntimeConfig->opFile);

    // Set mode to DELETE and trigger the operation
    pSDCardRuntimeConfig->mode = SD_CARD_MANAGER_MODE_DELETE_FILE;  /* #829: LAST write */
    if (!SD_ArmOrRefuse(context, "DELete", pSDCardRuntimeConfig)) {
        pSDCardRuntimeConfig->mode = SD_CARD_MANAGER_MODE_NONE;
        /* #829: SD_ArmOrRefuse already released the claim */  /* mode must still be cleared */
        result = SCPI_RES_ERR;
        goto __exit_point;
    }

    // Wait for sd_card_manager to complete deletion (up to 5 seconds)
    if (!sd_card_manager_WaitForCompletion(SCPI_SD_DELETE_TIMEOUT_MS)) {
        LOG_E("SD:DELete - Operation timeout\r\n");
        pSDCardRuntimeConfig->mode = SD_CARD_MANAGER_MODE_NONE;
        sd_card_manager_UpdateSettings(pSDCardRuntimeConfig);
        SCPI_ErrorPush(context, SCPI_ERROR_EXECUTION_ERROR);
        result = SCPI_RES_ERR;
        goto __exit_point;
    }

    // Check if the operation succeeded
    if (!sd_card_manager_GetLastOperationResult()) {
        SCPI_ExecutionError(context, "SYST:STOR:SD:DELete: delete operation failed");
        result = SCPI_RES_ERR;
        goto __exit_point;
    }

    result = SCPI_RES_OK;
__exit_point:
    return result;
}

/**
 * @brief Format the SD card (erase all files)
 *
 * Usage: SYST:STOR:SD:FORmat
 *
 * WARNING: This will erase ALL files on the SD card!
 */
scpi_result_t SCPI_StorageSDFormat(scpi_t * context) {
    scpi_result_t result = SCPI_RES_ERR;
    sd_card_manager_settings_t* pSDCardRuntimeConfig = BoardRunTimeConfig_Get(BOARDRUNTIME_SD_CARD_SETTINGS);

    if (!pSDCardRuntimeConfig->enable) {
        LOG_E("SD:FORmat - SD card not enabled\r\n");
        SCPI_ErrorPush(context, SCPI_ERROR_EXECUTION_ERROR);
        result = SCPI_RES_ERR;
        goto __exit_point;
    }

    // Check if SD card is busy with another operation
    if (SD_RefuseIfSuspended(context, "FORmat")) {
        result = SCPI_RES_ERR;
        goto __exit_point;
    }

    if (sd_card_manager_IsBusy()) {
        LOG_SD_BUSY("FORmat");
        SCPI_ErrorPush(context, SCPI_ERROR_EXECUTION_ERROR);
        result = SCPI_RES_ERR;
        goto __exit_point;
    }

    // Check if SD card is present
    if (!SCPI_CheckSDCardPresent(context)) {
        result = SCPI_RES_ERR;
        goto __exit_point;
    }

    LOG_D("SD:FORmat - Formatting SD card (erasing all files)\r\n");

    // Set mode to FORMAT and trigger the operation (non-blocking)
    // Poll SYST:STOR:SD:FORmat? for status and progress percentage
    /* #829: claim BEFORE SetFormatPending(). That call publishes "format in
     * progress" to FORmat? queries, so a caller that then loses the claim
     * would have advertised a format nobody is going to run. Claiming first
     * means only the owner ever publishes. */
    if (!SD_ClaimOrRefuse(context, "FORmat")) {
        result = SCPI_RES_ERR;
        goto __exit_point;
    }
    sd_card_manager_SetFormatPending();  // Immediately visible to FORmat? queries
    pSDCardRuntimeConfig->mode = SD_CARD_MANAGER_MODE_FORMAT;  /* #829: LAST write */
    /* This one reported SUCCESS on a refused arm -- it returns OK without
     * waiting, so the client believed a format had started when nothing had
     * been queued at all. That is the worst of the three shapes. */
    if (!SD_ArmOrRefuse(context, "FORmat", pSDCardRuntimeConfig)) {
        /* SetFormatPending() above already published "in progress" so
         * FORmat? would answer immediately. Nothing is going to run it now,
         * so clear it -- otherwise FORmat? reports a format in flight
         * forever and a client polling for completion never stops. */
        sd_card_manager_ClearFormatStatus();
        pSDCardRuntimeConfig->mode = SD_CARD_MANAGER_MODE_NONE;
        /* #829: SD_ArmOrRefuse already released the claim */  /* mode must still be cleared */
        result = SCPI_RES_ERR;
        goto __exit_point;
    }

    result = SCPI_RES_OK;
__exit_point:
    return result;
}

/**
 * @brief Set maximum file size for automatic file splitting
 *
 * Command: SYST:STOR:SD:MAXSize <bytes>
 * Example: SYST:STOR:SD:MAXSize 4185448858  (3.9GB)
 *          SYST:STOR:SD:MAXSize 0           (unlimited)
 */
scpi_result_t SCPI_StorageSDMaxSizeSet(scpi_t * context) {
    scpi_result_t result = SCPI_RES_ERR;
    sd_card_manager_settings_t* pSDCardRuntimeConfig = BoardRunTimeConfig_Get(BOARDRUNTIME_SD_CARD_SETTINGS);

    int64_t maxSizeBytes;
    if (!SCPI_ParamInt64(context, &maxSizeBytes, TRUE)) {
        SCPI_ErrorPush(context, SCPI_ERROR_ILLEGAL_PARAMETER_VALUE);
        goto __exit_point;
    }

    // Validate range (0 = unlimited, or >= minimum size)
    if (maxSizeBytes < 0) {
        LOG_E("SD:MAXSize - Invalid size: %lld (must be >= 0)\r\n", maxSizeBytes);
        SCPI_ErrorPush(context, SCPI_ERROR_ILLEGAL_PARAMETER_VALUE);
        goto __exit_point;
    }

    // Minimum file size protection: Prevent rapid rotation and filesystem stress
    const uint64_t MIN_FILE_SIZE = 1000;  // 1000 bytes minimum
    if (maxSizeBytes > 0 && (uint64_t)maxSizeBytes < MIN_FILE_SIZE) {
        LOG_E("[%s:%d]SD:MAXSize - Size %llu too small (minimum %llu bytes)",
              __FILE__, __LINE__, (uint64_t)maxSizeBytes, MIN_FILE_SIZE);
        SCPI_ErrorPush(context, SCPI_ERROR_ILLEGAL_PARAMETER_VALUE);
        goto __exit_point;
    }

    // FAT32 filesystem limit protection: 4GB hard limit
    const uint64_t FAT32_MAX_FILE_SIZE = 4294967295ULL;  // 4GB - 1 byte
    if (maxSizeBytes > 0 && (uint64_t)maxSizeBytes > FAT32_MAX_FILE_SIZE) {
        LOG_E("SD:MAXSize - Requested size %lld exceeds FAT32 limit (%llu bytes).\r\n",
              (long long)maxSizeBytes, (unsigned long long)FAT32_MAX_FILE_SIZE);
        SCPI_ErrorPush(context, SCPI_ERROR_ILLEGAL_PARAMETER_VALUE);
        goto __exit_point;
    }

    // If user sets 0, use safe filesystem maximum (3.9GB for FAT32)
    if (maxSizeBytes == 0) {
        pSDCardRuntimeConfig->maxFileSizeBytes = SD_CARD_MANAGER_FAT32_SAFE_MAX_FILE_SIZE;  // 3.9GB safe default
        LOG_D("SD:MAXSize - Using filesystem maximum: %llu bytes (3.9GB)\r\n",
              pSDCardRuntimeConfig->maxFileSizeBytes);
    } else {
        pSDCardRuntimeConfig->maxFileSizeBytes = (uint64_t)maxSizeBytes;
        LOG_D("SD:MAXSize - Set max file size to %llu bytes\r\n",
              pSDCardRuntimeConfig->maxFileSizeBytes);
    }

    sd_card_manager_UpdateSettings(pSDCardRuntimeConfig);
    result = SCPI_RES_OK;

__exit_point:
    return result;
}

/**
 * @brief Read the SD working directory (#799)
 *
 * Command: SYST:STOR:SD:DIRectory?
 *
 * This field is the device's SD working directory: logging writes into it,
 * and SD:GET, SD:DELete and SD:CRC interpret their operands relative to it.
 * Until now there was no way to READ it -- so a host was subject to a value
 * it could neither observe before an operation nor restore afterwards, and
 * the only way to change it was the side effect of a SD:LISt? query.
 *
 * This getter is deliberately the first of the three pieces in #799: it is
 * purely additive, it breaks nothing, and it is what makes the other two
 * testable at all. Stopping SD:LISt? from writing the field (#799 part 3) is
 * a behaviour change that needs a client survey first and is NOT done here.
 */
scpi_result_t SCPI_StorageSDDirectoryGet(scpi_t * context) {
    sd_card_manager_settings_t* pSDCardRuntimeConfig =
            BoardRunTimeConfig_Get(BOARDRUNTIME_SD_CARD_SETTINGS);
    char snapshot[sizeof(pSDCardRuntimeConfig->directory)];

    /* Copy under a critical section, then emit. SCPI runs on two transports
     * (USB pri 7, WiFi TCP pri 2), so the setter below can be writing this
     * buffer while this reads it. See the setter for what a reader would
     * otherwise observe. */
    taskENTER_CRITICAL();
    memcpy(snapshot, pSDCardRuntimeConfig->directory, sizeof(snapshot));
    taskEXIT_CRITICAL();
    snapshot[sizeof(snapshot) - 1u] = '\0';

    SCPI_ResultText(context, snapshot);
    return SCPI_RES_OK;
}

/**
 * @brief Set the SD working directory explicitly (#799)
 *
 * Command: SYST:STOR:SD:DIRectory "<path>"
 *
 * Before this, the ONLY way to change the working directory was to issue a
 * SD:LISt? for its side effect -- a query changing device state, which also
 * made an ordinary browse silently retarget the next SD:GET, SD:DELete,
 * SD:CRC and capture. Having a real setter means changing it is something a
 * caller ASKS for.
 *
 * Validation is deliberately the same pair SD:LISt? already applies to the
 * same field -- the length bound and SD_ValidatePathParam (#612) -- rather
 * than a second, subtly different rule for the same value.
 */
scpi_result_t SCPI_StorageSDDirectorySet(scpi_t * context) {
    sd_card_manager_settings_t* pSDCardRuntimeConfig =
            BoardRunTimeConfig_Get(BOARDRUNTIME_SD_CARD_SETTINGS);
    const char *pBuff = NULL;
    size_t pathLen = 0;

    if (!SCPI_ParamCharacters(context, &pBuff, &pathLen, TRUE)) {
        return SCPI_RES_ERR;   /* libscpi has already pushed the parse error */
    }

    if (pathLen >= sizeof(pSDCardRuntimeConfig->directory)) {
        LOG_E("SD:DIRectory - path too long: %lu bytes, max %lu",
              (unsigned long)pathLen,
              (unsigned long)(sizeof(pSDCardRuntimeConfig->directory) - 1));
        SCPI_ErrorPush(context, SCPI_ERROR_ILLEGAL_PARAMETER_VALUE);
        return SCPI_RES_ERR;
    }

    if (!SD_ValidatePathParam(pBuff, pathLen)) {   /* #612 */
        SCPI_ErrorPush(context, SCPI_ERROR_ILLEGAL_PARAMETER_VALUE);
        return SCPI_RES_ERR;
    }

    /* Refuse while an SD operation owns the card -- which includes an active
     * logging session, since IsBusy() is true for any mode != NONE.
     *
     * The directory is re-read at every file OPEN, not just the first, so
     * retargeting it mid-session would land the next rotated part
     * ("<name>-1.csv", generateFilename) in a different folder from the part
     * before it. That splits one logical capture across two directories,
     * where the tooling that reassembles a split set groups by name WITHIN a
     * directory. Rejecting costs the caller nothing -- set the directory
     * before starting -- and is the same guard the other SD entry points in
     * this file already apply. */


    /* Config-only write, deliberately NOT followed by
     * sd_card_manager_UpdateSettings(): that call forces the SD state machine
     * to DEINIT -> UNMOUNT_DISK, closing any open file and truncating it on
     * the next open. SCPI_StorageSDMinFreeSet documents the same reasoning for
     * the same struct. The manager holds a POINTER to this very object
     * (gpSDCardSettings = pSettings, sd_card_manager.c), so the write is
     * visible to it without any notification. */
    /* Publish as ONE indivisible update. The copy and the terminator are two
     * steps over a buffer another task reads (the getter above, the SD
     * manager's dirValid check, SD_StripConfiguredDir), and SCPI runs on two
     * transports at different priorities.
     *
     * To be precise about the hazard, because it is NOT an overrun: every
     * writer stores at most 40 characters plus a terminator into a 41-byte
     * array, so a NUL always exists at index <= 40 and strlen cannot run off
     * the end. What a reader CAN see is a SPLICED path -- when the previous
     * name was longer than the new one, the old tail survives past the bytes
     * just copied and the old terminator ends it, so a reader between the two
     * steps observes new-prefix + old-suffix. That is a well-formed string
     * naming the wrong directory, which for SD:DELete is the worse outcome of
     * the two. The copy is <= 41 bytes, so the section is short. */
    /* The busy test and the write share ONE critical section. Checked
     * separately, an SD operation could arm in between and the write would
     * land in a session that had just started -- the exact case the check
     * exists to prevent. IsBusy() only reads mode and currentProcessState,
     * so it is safe here; the log stays outside. */
    bool busy;
    taskENTER_CRITICAL();
    busy = sd_card_manager_IsBusy();
    if (!busy) {
        memcpy(pSDCardRuntimeConfig->directory, pBuff, pathLen);
        pSDCardRuntimeConfig->directory[pathLen] = '\0';
    }
    taskEXIT_CRITICAL();

    if (busy) {
        LOG_SD_BUSY("DIRectory");
        SCPI_ErrorPush(context, SCPI_ERROR_EXECUTION_ERROR);
        return SCPI_RES_ERR;
    }
    return SCPI_RES_OK;
}

/**
 * @brief Query maximum file size setting
 *
 * Command: SYST:STOR:SD:MAXSize?
 * Returns: <bytes> (0 = unlimited)
 */
scpi_result_t SCPI_StorageSDMaxSizeGet(scpi_t * context) {
    sd_card_manager_settings_t* pSDCardRuntimeConfig = BoardRunTimeConfig_Get(BOARDRUNTIME_SD_CARD_SETTINGS);

    SCPI_ResultUInt64(context, pSDCardRuntimeConfig->maxFileSizeBytes);
    return SCPI_RES_OK;
}

/**
 * @brief Set minimum free space floor for SYST:STR:START (#498)
 *
 * Command: SYST:STOR:SD:MINFree <bytes>
 *   Default: 0 (no pre-start check; legacy behavior)
 *   Typical: 52428800 (50 MB) — refuses to start a long log run on a
 *            card with less than 50 MB headroom.
 *
 * When > 0, SYST:STR:START for SD-output sessions runs
 * SYS_FS_DriveSectorGet() before arming the streaming timer and
 * returns SCPI -200 if the free space is below this floor.  Catches
 * the "started a 4-hour log on a near-full card" failure mode that
 * otherwise drops samples silently after disk-full mid-run.
 */
scpi_result_t SCPI_StorageSDMinFreeSet(scpi_t * context) {
    sd_card_manager_settings_t* pSDCardRuntimeConfig = BoardRunTimeConfig_Get(BOARDRUNTIME_SD_CARD_SETTINGS);

    // Parse as uint64 directly — storage is uint64; SCPI_ParamInt64
    // would reject values above INT64_MAX.
    uint64_t minFreeBytes;
    if (!SCPI_ParamUInt64(context, &minFreeBytes, TRUE)) {
        SCPI_ErrorPush(context, SCPI_ERROR_ILLEGAL_PARAMETER_VALUE);
        return SCPI_RES_ERR;
    }
    // 64-bit shared write needs critical section per CLAUDE.md
    // atomicity rules — PIC32MZ's 32-bit data bus tears 64-bit
    // stores under task preemption.  The runtime config is read
    // by SCPI_StartStreaming from a different task and by the
    // SCPI getter from this task; without the critical section,
    // a concurrent reader could see a torn intermediate value.
    taskENTER_CRITICAL();
    pSDCardRuntimeConfig->minFreeBytes = minFreeBytes;
    taskEXIT_CRITICAL();
    // Config-only write — do NOT call sd_card_manager_UpdateSettings()
    // here.  UpdateSettings() unconditionally forces SD state to
    // DEINIT → UNMOUNT_DISK, which closes any active WRITE file and
    // on next open truncates with WRITE_PLUS.  MINFree is consulted
    // only at SYST:STR:START, so a config-only write is correct;
    // bouncing the SD state machine during an active logging session
    // would cause silent data loss.
    return SCPI_RES_OK;
}

/**
 * @brief Query minimum free space floor
 *
 * Command: SYST:STOR:SD:MINFree?
 * Returns: <bytes> (0 = no pre-start check)
 */
scpi_result_t SCPI_StorageSDMinFreeGet(scpi_t * context) {
    sd_card_manager_settings_t* pSDCardRuntimeConfig = BoardRunTimeConfig_Get(BOARDRUNTIME_SD_CARD_SETTINGS);

    // 64-bit shared read also under critical section — pairs with the
    // setter (same field, same PIC32MZ tearing risk under preemption).
    uint64_t minFreeBytes;
    taskENTER_CRITICAL();
    minFreeBytes = pSDCardRuntimeConfig->minFreeBytes;
    taskEXIT_CRITICAL();
    SCPI_ResultUInt64(context, minFreeBytes);
    return SCPI_RES_OK;
}

/**
 * @brief Query SD card free and total space
 *
 * Command: SYST:STOR:SD:SPACe?
 * Returns: <free_bytes>,<total_bytes>
 *
 * Routes through SD card manager state machine for fresh values.
 * Rejected when SD manager is busy (e.g., active streaming session).
 */
scpi_result_t SCPI_StorageSDSpaceGet(scpi_t * context) {
    scpi_result_t result = SCPI_RES_ERR;
    sd_card_manager_settings_t* pSDCardRuntimeConfig = BoardRunTimeConfig_Get(BOARDRUNTIME_SD_CARD_SETTINGS);

    // Refuse before probing the card -- see SCPI_StorageSDListDir.
    if (SD_RefuseIfSuspended(context, "SPACe")) {
        result = SCPI_RES_ERR;
        goto __exit_point;
    }

    if (!SCPI_CheckSDCardPresent(context)) {
        result = SCPI_RES_ERR;
        goto __exit_point;
    }

    if (sd_card_manager_IsBusy()) {
        LOG_SD_BUSY("SPACe");
        SCPI_ErrorPush(context, SCPI_ERROR_EXECUTION_ERROR);
        result = SCPI_RES_ERR;
        goto __exit_point;
    }

    // Set mode to GET_SPACE and let sd_card_manager handle mount/query/unmount
    /* #829: atomic claim -- the plain assignment could race a second caller
     * that had already passed the IsBusy() fast path above. */
    if (!SD_ClaimOrRefuse(context, "SPACe")) {
        result = SCPI_RES_ERR;
        goto __exit_point;
    }
    pSDCardRuntimeConfig->mode = SD_CARD_MANAGER_MODE_GET_SPACE;  /* #829: LAST write */
    if (!SD_ArmOrRefuse(context, "SPACe", pSDCardRuntimeConfig)) {
        pSDCardRuntimeConfig->mode = SD_CARD_MANAGER_MODE_NONE;
        /* #829: SD_ArmOrRefuse already released the claim */  /* mode must still be cleared */
        result = SCPI_RES_ERR;
        goto __exit_point;
    }

    if (!sd_card_manager_WaitForCompletion(SCPI_SD_SPACE_TIMEOUT_MS)) {
        LOG_E("[SD] SPACe? - Operation timeout");
        pSDCardRuntimeConfig->mode = SD_CARD_MANAGER_MODE_NONE;
        sd_card_manager_UpdateSettings(pSDCardRuntimeConfig);
        SCPI_ErrorPush(context, SCPI_ERROR_EXECUTION_ERROR);
        result = SCPI_RES_ERR;
        goto __exit_point;
    }

    if (!sd_card_manager_GetLastOperationResult()) {
        LOG_E("[SD] SPACe? - Query failed");
        SCPI_ErrorPush(context, SCPI_ERROR_EXECUTION_ERROR);
        result = SCPI_RES_ERR;
        goto __exit_point;
    }

    uint64_t freeBytes = 0;
    uint64_t totalBytes = 0;
    if (!sd_card_manager_GetSpaceInfo(&freeBytes, &totalBytes)) {
        LOG_E("[SD] SPACe? - No valid result");
        SCPI_ErrorPush(context, SCPI_ERROR_EXECUTION_ERROR);
        result = SCPI_RES_ERR;
        goto __exit_point;
    }

    SCPI_ResultUInt64(context, freeBytes);
    SCPI_ResultUInt64(context, totalBytes);
    result = SCPI_RES_OK;

__exit_point:
    return result;
}

scpi_result_t SCPI_StorageSDAbort(scpi_t * context) {
    sd_card_manager_AbortTransfer();
    return SCPI_RES_OK;
}

scpi_result_t SCPI_StorageSDFormatQuery(scpi_t * context) {
    int status = sd_card_manager_GetFormatStatus();
    SCPI_ResultInt32(context, status);
    SCPI_ResultInt32(context, sd_card_manager_GetFormatProgress());
    // Clear terminal states after reading to avoid stale results
    if (status == 2 || status == -1) {
        sd_card_manager_ClearFormatStatus();
    }
    return SCPI_RES_OK;
}

/**
 * @brief SCPI handler for SYST:STOR:SD:INFO?
 *
 * Returns SD card identification from the CID register:
 *   MID,OEM,ProductName,Revision,SerialNumber,MfgDate
 *
 * Example response: 27,"SM","EC2QT",8.0,1234567890,2023-07
 */
scpi_result_t SCPI_StorageSDInfo(scpi_t * context) {
    uint8_t cid[16];
    if (!DRV_SDSPI_GetCID(cid, sizeof(cid))) {
        LOG_E("[SD] Card info not available (driver CID support missing or no card)");
        SCPI_ErrorPush(context, SCPI_ERROR_HARDWARE_MISSING);
        return SCPI_RES_ERR;
    }

    /* CID register layout (MSB first as read from card, bytes already in
     * big-endian order from the driver):
     *   [0]      Manufacturer ID (MID)
     *   [1..2]   OEM/Application ID (OID) - 2 ASCII chars
     *   [3..7]   Product Name (PNM) - 5 ASCII chars
     *   [8]      Product Revision (PRV) - BCD: high=major, low=minor
     *   [9..12]  Product Serial Number (PSN) - 32-bit big-endian
     *   [13..14] Manufacturing Date (MDT) - bits [11:4]=year+2000, [3:0]=month
     *   [15]     CRC7 + stop bit
     */
    uint8_t mid = cid[0];

    char oid[3];
    oid[0] = (char)cid[1];
    oid[1] = (char)cid[2];
    oid[2] = '\0';

    char pnm[6];
    memcpy(pnm, &cid[3], 5);
    pnm[5] = '\0';

    uint8_t prv_major = (cid[8] >> 4) & 0x0F;
    uint8_t prv_minor = cid[8] & 0x0F;

    uint32_t psn = ((uint32_t)cid[9] << 24) | ((uint32_t)cid[10] << 16) |
                   ((uint32_t)cid[11] << 8) | (uint32_t)cid[12];

    /* MDT: [13] bits 7..4 are reserved, bits 3..0 are year[11:8]
     *      [14] bits 7..4 are year[7:4], bits 3..0 are month[3:0] */
    uint16_t mdt_raw = ((uint16_t)(cid[13] & 0x0F) << 8) | (uint16_t)cid[14];
    uint16_t mdt_year = 2000 + ((mdt_raw >> 4) & 0xFF);
    uint8_t mdt_month = mdt_raw & 0x0F;

    char result[80];
    int len = snprintf(result, sizeof(result),
                       "%u,\"%s\",\"%s\",%u.%u,%lu,%u-%02u",
                       mid, oid, pnm,
                       prv_major, prv_minor,
                       (unsigned long)psn,
                       mdt_year, mdt_month);

    if (len < 0 || (size_t)len >= sizeof(result)) {
        LOG_E("[SD] CID format error (len=%d)", len);
        SCPI_ErrorPush(context, SCPI_ERROR_SYSTEM_ERROR);
        return SCPI_RES_ERR;
    }

    context->interface->write(context, result, (size_t)len);
    context->interface->write(context, "\r\n", 2);

    return SCPI_RES_OK;
}

/* *****************************************************************************
 End of File
 */
