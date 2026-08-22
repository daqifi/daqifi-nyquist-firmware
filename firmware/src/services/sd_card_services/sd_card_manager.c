#define LOG_LVL LOG_LEVEL_SD
#define LOG_MODULE LOG_MODULE_SD

#include <string.h>
#include "Util/Logger.h"
#include "Util/SpiBusHealth.h"
#include "app_freertos.h"   /* #589: app_SDCard_SpiOwnedByWifi (live owner) */
#include "Util/CoherentPool.h"
#include "Util/StreamingBufferPool.h"
#include "sd_card_manager.h"
#include "services/UsbCdc/UsbCdc.h"
#include "Util/CRC32.h"   /* #306 */
#include "services/streaming.h"  // For Streaming_ResetSdPbMetadata on file rotation
#include <stddef.h>
#include "ff.h"   /* #810: FILINFO, for the layout assert below */

/* #810: FATFS_readdir (sys_fs_fat_interface.c) hands ONE buffer to f_readdir as a
 * FILINFO and then reinterprets it as a SYS_FS_FSTAT, reading lfname out of it and
 * writing through it:
 *
 *     res = f_readdir(dp, finfo);
 *     if ((res == FR_OK) && (fileStat->lfname != NULL)) { fileStat->lfname[0] = '\0'; }
 *
 * That is safe only while lfname sits past everything f_readdir writes. If it ever
 * lands inside fname[], lfname becomes four FILENAME CHARACTERS reinterpreted as a
 * pointer and the line above is a wild one-byte write to a card-content-derived
 * address -- intermittent, far from its cause, and with no diagnostic.
 *
 * Today it holds by coincidence: SYS_FS_FILE_NAME_LEN (MCC-generated
 * configuration.h) happens to agree with FF_LFN_BUF (ffconf.h). Nothing ties those
 * two files together, so an MCC regeneration, a FatFs bump, or someone shortening
 * the name limit to reclaim RAM would break it silently.
 *
 * This is a compile-time invariant check, not a defensive runtime guard: the
 * invariant is undocumented, unenforced, and split across two generated files. */
_Static_assert(offsetof(SYS_FS_FSTAT, lfname) >= sizeof(FILINFO),
               "SYS_FS_FSTAT.lfname overlaps what f_readdir writes: FATFS_readdir "
               "would write through a pointer built from filename bytes");

#define SD_CARD_MANAGER_CIRCULAR_BUFFER_SIZE SD_CARD_MANAGER_DEFAULT_CIRCULAR_SIZE
#define SD_CARD_MANAGER_FILE_PATH_LEN_MAX (SYS_FS_FILE_NAME_LEN*2)
#define SD_CARD_MANAGER_DISK_MOUNT_NAME    "/mnt/DAQiFi"
// SD_CARD_MANAGER_DISK_DEV_NAME is now in header for external use
#define SD_CARD_MANAGER_MAX_SPLIT_FILES    9999
/* #689: defensive files-per-directory ceiling. FatFs file-create is O(directory
 * size) — creating a file in a directory holding many hundreds of entries can
 * exceed the SD op timeout and wedge the manager (reads keep working). Refuse to
 * create a WRITE-mode file when the target directory already holds this many
 * files, so the failure is a clean, diagnosable error instead of a silent wedge.
 * Set below the observed directory-size wedge onset. Bench 2026-07-14 (SanDisk
 * SR256, gentle rotation): 75 files built clean, ~125 files wedged — the onset
 * tracks the FatFs directory-cluster grow (dir_clear write burst) and the O(N)
 * create scan, so it is CARD-DEPENDENT. 64 keeps the directory within its first
 * FAT cluster (no grow burst) with margin below the proven-clean 75. Not a full
 * fix on its own — it is the per-BUCKET ceiling that the #689 bucketing below
 * rolls over at, and the hard refuse only when buckets are exhausted. */
#define SD_CARD_MANAGER_MAX_DIR_FILES      64u

/* #689: subdirectory bucketing. A long split-file session used to pile every
 * part into one directory; FatFs file-create is O(directory size) (dir_find
 * scans to EOF, then dir_alloc rescans from 0 for contiguous free entries),
 * so past a few hundred files a create exceeds the SD op timeout and wedges
 * the manager. Bucketing keeps every directory we write into small, so the
 * create scan stays bounded no matter how long the session runs.
 *
 * Bucket 0 IS the configured directory — sessions that never exceed one
 * bucket are byte-for-byte identical to the pre-#689 layout, so the common
 * case gains no new paths and host tooling sees no change. Only once a
 * directory reaches MAX_DIR_FILES do we roll into 'P001', 'P002', ...
 *
 * The names are deliberately 8.3-clean (uppercase, <=8 chars, no extension)
 * so FatFs stores each as a SINGLE directory entry rather than the 3 an LFN
 * name costs — the parent holds bucket entries too, and they must not become
 * the next O(N) problem. */
#define SD_CARD_MANAGER_BUCKET_PREFIX      "P"
/* The parent holds one entry per bucket, so the bucket ceiling IS a bound on
 * the parent's size — get it wrong and creating a late bucket re-enters the
 * very O(N) scan this fix exists to avoid. Worst-case parent entry budget:
 *
 *   <= MAX_DIR_FILES files      x 3 entries (2 LFN + 1 SFN)  = 192
 *   + <= MAX_BUCKET subdirs     x 1 entry  (8.3-clean names) =  64
 *                                                             ----
 *                                                              256 entries
 *                                                            ~8 KB, ~16 sectors
 *
 * The bench wedge (#689) was observed at ~500 LFN files ~= 1,536 entries
 * (~48 KB, ~94 sectors), so 256 stays ~6x below it. Matching MAX_BUCKET to
 * MAX_DIR_FILES keeps that arithmetic honest and still allows bucket 0 plus
 * P001..P064 = 65 x 64 = 4,160 files before the clean refuse. Raising it
 * without
 * re-deriving the table above would silently walk the parent back toward the
 * wedge. */
#define SD_CARD_MANAGER_MAX_BUCKET         SD_CARD_MANAGER_MAX_DIR_FILES
/* Advancing a bucket costs a DirectoryMake + a CountDirEntries that is itself
 * bounded to MAX_DIR_FILES entries (~2 sectors for a full bucket), so one
 * advance is a few milliseconds.
 *
 * This MUST be able to cross every bucket. An earlier value of 16 looked safely
 * conservative and was actively wrong: a session starting on a card whose first
 * 60-odd buckets are full could not reach the free ones, and was refused at
 * START even though space existed. Bench-found 2026-08-13 -- the simulation
 * that covered this case scored it a "designed backstop", which it is not.
 *
 * Worst case is therefore ~65 bounded scans, a few hundred milliseconds, paid
 * on the first create of each SESSION -- every session rescans from bucket 0.
 * An earlier revision persisted the cursor across sessions to avoid exactly
 * that rescan; it was removed because a cursor that cannot be stale is worth
 * more than the time it saves (see the fileCounter==0 branch for the three
 * ways the stale one went wrong). */
#define SD_CARD_MANAGER_BUCKET_ADVANCE_MAX (SD_CARD_MANAGER_MAX_BUCKET + 1u)
/* #780: how long the pumped wait blocks between USB write pumps. Small enough
 * that a filling circular buffer is serviced promptly, large enough that the
 * wait is not a busy-spin against the SD task.
 *
 * The slice is clamped to >= 1 tick at use. At the current 1000 Hz tick this is
 * 2 ticks, but if configTICK_RATE_HZ ever dropped below 500, pdMS_TO_TICKS(2)
 * would round to 0 -- a zero-timeout take never blocks, so the pri-7 waiter
 * would busy-spin and starve the pri-5 SD task that has to give the semaphore.
 * That is a livelock for the whole timeout, and forever when timeoutMs == 0. */
#define SD_WAIT_PUMP_SLICE_MS              2u

// Iterative directory listing — bounded BSS-backed stack instead of recursion.
// 16 levels covers any realistic FAT32 tree without growing the task stack.
#define SD_CARD_MANAGER_MAX_LIST_DEPTH     16

// File read transfer constants
#define SD_READ_MAX_CHUNK_SIZE      16384U  // Maximum read size (16KB) - tested maximum for stability
#define SD_READ_ALIGNMENT_SIZE      4096U   // Chunk alignment (4KB) - matches USB transfer granularity
#define SD_FLUSH_THRESHOLD          4096U   // Minimum bytes to trigger flush before unmount

/* #703: SD:GET reads into the streaming pool's SD circular buffer; the read
 * bails (maxRead==0) if that buffer is below one alignment unit. Pin the pool's
 * inactive SD floor at/above the read minimum so a non-SD stream can never
 * shrink SD:GET into a silent failure. If a future RAM-pressure trim drops
 * STREAMING_SD_CIRCULAR_MIN below the alignment, this fails the build. */
_Static_assert(STREAMING_SD_CIRCULAR_MIN >= SD_READ_ALIGNMENT_SIZE,
               "SD circular floor must be >= SD read alignment (#703)");

// =============================================================================
// Debug Timeout Configuration
// =============================================================================
// Long timeouts for detecting hangs without perturbing normal operation.
// These should be many times longer than expected operation duration.
// Only logs error when timeout is hit, indicating a real problem.
#define SD_MOUNT_MAX_RETRIES        10      // Max mount attempts before giving up (incompatible FS, etc.)
#define SD_UNMOUNT_MAX_RETRIES        (40U)   /* #603: bounded, then force-clear */
#define SD_UNMOUNT_RETRY_DELAY_MS     (50U)   /* #603: 40 x 50 ms = 2 s ceiling */
#define SD_MOUNT_RETRY_DELAY_MS     100     // Delay between mount retries (total budget: retries * delay = 1s)
#define SD_SECTOR_SIZE_BYTES        512U    // FAT sector size (must match ffconf.h FF_MIN_SS/FF_MAX_SS)
#define SD_DEBUG_TIMEOUT_MS         60000U  // 60 seconds - filesystem operations
#define SD_DEBUG_MUTEX_TIMEOUT_MS   30000U  // 30 seconds - mutex acquisition

/**
 * @brief Take a semaphore with timeout, log error if timeout occurs.
 *
 * Wraps xSemaphoreTake with debug timeout. On timeout, logs the location
 * but continues waiting (returns result of infinite wait for compatibility).
 *
 * @param sem       Semaphore handle
 * @param location  String identifying the call location for logging
 * @return pdTRUE if semaphore taken, pdFALSE on failure
 */
static inline BaseType_t SD_TakeMutexDebug(SemaphoreHandle_t sem, const char* location) {
    TickType_t timeout = pdMS_TO_TICKS(SD_DEBUG_MUTEX_TIMEOUT_MS);
    BaseType_t result = xSemaphoreTake(sem, timeout);
    if (result != pdTRUE) {
        LOG_E("[SD] HANG DETECTED: Mutex timeout at %s after %u ms",
              location, SD_DEBUG_MUTEX_TIMEOUT_MS);
        // Continue waiting indefinitely to preserve original behavior
        result = xSemaphoreTake(sem, portMAX_DELAY);
    }
    return result;
}

/**
 * @brief Log if a filesystem operation took longer than expected.
 *
 * Call after FS operation completes to detect slow operations.
 *
 * @param startTick  Tick count before operation started
 * @param operation  String describing the operation
 * @param result     Result code from the operation (-1 typically means error)
 */
static inline void SD_CheckFsOpDuration(TickType_t startTick, const char* operation, int result) {
    uint32_t elapsed = pdTICKS_TO_MS(xTaskGetTickCount() - startTick);
    if (elapsed > SD_DEBUG_TIMEOUT_MS) {
        LOG_E("[SD] HANG DETECTED: %s took %u ms (limit %u ms), result=%d",
              operation, elapsed, SD_DEBUG_TIMEOUT_MS, result);
    } else if (elapsed > 5000) {
        // Also log warning for operations > 5 seconds (unusual but not critical)
        LOG_I("[SD] Slow operation: %s took %u ms, result=%d", operation, elapsed, result);
    }
}

// Shared coherent buffer for all SD operations (write, read, list)
// Allocated from coherent pool at init (DMA-safe for SPI transfers).
static uint8_t* gSdSharedBuffer = NULL;
static uint32_t gSdSharedBufferSize = 0;

// Mutex to serialize SD operations (READ, WRITE, LIST) on shared buffer
// Prevents race conditions from cross-task state changes via UpdateSettings()
static SemaphoreHandle_t gSDOpMutex = NULL;
static bool gLoggedUnmountFail = false;
// gLoggedWriteBufferTimeout removed — WriteToBuffer is now non-blocking
static volatile bool gTransferAbortRequested = false;

/* #757: true from the instant the old file is closed for a size rotation until
 * the new file's open has resolved. During that window there is no valid file
 * handle, so sd_card_manager_IsWriteReady() is false -- but the circular buffer
 * is empty (the rotation drained it) and will be written to the NEW file, so it
 * can safely keep ACCEPTING encoder output. That is the whole fix: without it
 * the encoder is told sdSize == 0 for the entire open, and every packet encoded
 * in that window is discarded and counted in SdDroppedBytes.
 *
 * Single writer (the SD task); read by the streaming task via
 * sd_card_manager_IsBufferAccepting(). A plain aligned bool is atomic on
 * PIC32MZ; volatile is here because the two contexts differ, not to imply
 * read-modify-write safety. */
static volatile bool gSdRotating = false;
static int gFormatStatus = 0;  // 0=idle, 1=in progress, 2=success, -1=failed
static uint32_t gFormatSectorsEstimate = 0;  // Estimated total sectors written during format

/* Extern: sector write counter in diskio.c for format progress tracking */
extern volatile uint32_t gDiskFormatSectorsWritten;
extern volatile bool gDiskFormatTracking;

/**
 * Helper function: Wait for USB buffer to drain before EOF/close
 * Prevents race condition where EOF arrives before last data is processed
 * Waits up to 50ms for USB buffer to be >50% drained
 */
static void sd_wait_usb_drain(void) {
    TickType_t start = xTaskGetTickCount();
    while ((xTaskGetTickCount() - start) < pdMS_TO_TICKS(50)) {
        // If buffer is mostly drained, assume progress and exit
        if (UsbCdc_WriteBuffFreeSize(NULL) > (USBCDC_WBUFFER_SIZE / 2)) {
            break;
        }
        vTaskDelay(1);
    }
}

typedef enum {
    SD_CARD_MANAGER_PROCESS_STATE_INIT,
    SD_CARD_MANAGER_PROCESS_STATE_MOUNT_DISK,
    SD_CARD_MANAGER_PROCESS_STATE_UNMOUNT_DISK,
    SD_CARD_MANAGER_PROCESS_STATE_CURRENT_DRIVE,
    SD_CARD_MANAGER_PROCESS_STATE_CHECK_DISK_FULL,  /* #503: consolidated WRITE pre-check */
    SD_CARD_MANAGER_PROCESS_STATE_CREATE_DIRECTORY,
    SD_CARD_MANAGER_PROCESS_STATE_OPEN_FILE,
    SD_CARD_MANAGER_PROCESS_STATE_WRITE_TO_FILE,
    SD_CARD_MANAGER_PROCESS_STATE_READ_FROM_FILE,
    SD_CARD_MANAGER_PROCESS_STATE_LIST_DIR,
    SD_CARD_MANAGER_PROCESS_STATE_DELETE_FILE,
    SD_CARD_MANAGER_PROCESS_STATE_FORMAT,
    SD_CARD_MANAGER_PROCESS_STATE_GET_SPACE,
    SD_CARD_MANAGER_PROCESS_STATE_COMPUTE_CRC,   /* #306 */
    SD_CARD_MANAGER_PROCESS_STATE_DEINIT,
    SD_CARD_MANAGER_PROCESS_STATE_IDLE,
    SD_CARD_MANAGER_PROCESS_STATE_ERROR,
} sd_card_manager_processState_t;

typedef struct {
    sd_card_manager_processState_t currentProcessState;
    /** Control message buffer (for EOF marker, error messages) */
    uint8_t messageBuffer[SD_CARD_MANAGER_CONF_RBUFFER_SIZE];

    /** The current length of the message buffer */
    size_t messageBufferLength;

    /** Client write buffer (allocated from coherent pool, DMA-safe) */
    uint8_t* writeBuffer;

    /** Size of the write buffer in bytes */
    uint32_t writeBufferSize;

    /** The current length of the write buffer */
    size_t writeBufferLength;

    CircularBuf_t wCirbuf;

    SemaphoreHandle_t wMutex;
    SemaphoreHandle_t opCompleteSemaphore;  // Signals when async operations complete

    char filePath[SD_CARD_MANAGER_FILE_PATH_LEN_MAX + 1];

    SYS_FS_HANDLE fileHandle;

    bool sdCardWritePending;
    uint16_t sdCardWriteBufferOffset;
    uint32_t totalBytesFlushPending;
    uint64_t lastFlushMillis;
    bool discMounted;

    // File splitting state
    char baseFilename[SD_CARD_MANAGER_CONF_FILE_NAME_LEN_MAX + 1];  // Original filename without counter
    uint32_t fileCounter;        // Current file number (1, 2, 3, ...)
    uint64_t currentFileBytes;   // Bytes written to current file
    bool fileSplittingEnabled;   // True if maxFileSizeBytes > 0

    // Operation result tracking
    bool lastOperationSuccess;   // Result of last completed operation

    // Mount retry tracking
    uint8_t mountRetryCount;
    uint8_t unmountRetryCount;   /* #603: bounded unmount retries */     // Number of consecutive mount failures

    // #306: CRC result (populated by COMPUTE_CRC mode)
    volatile bool crcResultValid;
    uint32_t crcResult;
    uint64_t crcLength;
    uint32_t crcRunning;      /* streaming accumulator (task-only access) */

    // Space query result (populated by GET_SPACE mode)
    uint64_t spaceResultFreeBytes;
    uint64_t spaceResultTotalBytes;
    bool spaceResultValid;
    // #503: set true when the WRITE init pre-check found free space
    // below settings.minFreeBytes.  Cleared on every WRITE entry.
    // SCPI side reads this via sd_card_manager_StartupDiskFull() to
    // produce a friendly "out of space" message instead of generic
    // "WRITE timed out."
    //
    // volatile: written by the SD task (priority 5) in CURRENT_DRIVE
    // and CHECK_DISK_FULL states; read by SCPI tasks (USB pri 7, WiFi
    // pri 2) inside the SCPI_StartStreaming poll loop.  Without
    // volatile, -O3 can hoist the read out of the loop or cache it
    // across the vTaskDelay(1), defeating the intended early-exit.
    volatile bool startupDiskFull;

    // #689: set true when a WRITE-mode file create was refused because the
    // target directory already holds >= SD_CARD_MANAGER_MAX_DIR_FILES files
    // (FatFs create is O(dir size) and wedges the SD op timeout on very large
    // directories). Read by SCPI_StartStreaming to surface a precise
    // "directory too full" error instead of a generic "not ready". Same
    // volatile / cross-task (SD task pri 5 writer, SCPI pri 7/2 reader)
    // rationale as startupDiskFull above.
    volatile bool startupDirFull;
    /* #689: which condition set startupDirFull. Same cross-task rationale as
     * the flag above (SD task pri 5 writer, SCPI pri 7/2 readers); a 32-bit
     * enum load/store is atomic on PIC32MZ, so no critical section is needed. */
    volatile SdWriteRefuseReason writeRefuseReason;
    // #689: bucketing state. curBucket is the subdirectory index currently
    // being filled (0 == the configured directory itself); bucketPath is its
    // full path, rebuilt only when the bucket changes. bucketFileCountAtStart
    // is the pre-existing occupancy of that bucket, counted ONCE when we enter
    // it, and filesInCurBucket counts what this session has added since — so
    // the fullness test costs no per-rotation re-scan.
    uint32_t curBucket;
    uint32_t bucketFileCountAtStart;
    uint32_t filesInCurBucket;
    char bucketPath[SD_CARD_MANAGER_FILE_PATH_LEN_MAX + 1];
} sd_card_manager_context_t;

sd_card_manager_context_t gSDCardData;
sd_card_manager_settings_t *gpSDCardSettings;

void __attribute__((weak)) sd_card_manager_DataReadyCB(sd_card_manager_mode_t mode, uint8_t *pDataBuff, size_t dataLen) {

}

static int SDCardWrite() {
    int writeLen = -1;
    if (gSDCardData.fileHandle == SYS_FS_HANDLE_INVALID) {
        goto __exit;
    }

    TickType_t startTick = xTaskGetTickCount();
    writeLen = SYS_FS_FileWrite(gSDCardData.fileHandle,
            (const void *) (gSDCardData.writeBuffer + gSDCardData.sdCardWriteBufferOffset),
            gSDCardData.writeBufferLength);
    SD_CheckFsOpDuration(startTick, "FileWrite", writeLen);
__exit:
    return writeLen;
}

static int CircularBufferToSDWrite(uint8_t* buf, uint32_t len) {
    if (len > gSDCardData.writeBufferSize) {
        LOG_E("[SD] CircularBufferToSDWrite overflow: len=%u, max=%u", (unsigned)len, (unsigned)gSDCardData.writeBufferSize);
        return -1;
    }
    memcpy(gSDCardData.writeBuffer, buf, len);
    gSDCardData.writeBufferLength = len;
    gSDCardData.sdCardWriteBufferOffset = 0;
    // Return length to indicate success - actual write happens in state machine
    // Calling SDCardWrite() here causes duplicate writes!
    return len;
}

/* #689: count entries in a directory, early-exiting once `cap` is reached so the
 * O(N) directory scan cost is bounded to ~cap entries regardless of directory
 * size. Runs in the SD task, which owns the filesystem. "." / ".." are skipped.
 * Returns min(actualFiles, cap); a return of `cap` means "at least cap". A
 * missing directory (created lazily on first write) counts as 0.
 *
 * #689: `cap` is returned BOTH for "at least cap files" and for a real FS
 * error, so callers that act on fullness must be able to tell them apart —
 * rolling to a fresh bucket is right for a full directory and wrong for a
 * broken filesystem. pFsError (optional) reports which happened. */
static uint32_t CountDirEntries(const char* dirPath, uint32_t cap,
                                bool* pFsError) {
    if (pFsError != NULL) {
        *pFsError = false;
    }
    SYS_FS_HANDLE dh = SYS_FS_DirOpen(dirPath);
    if (dh == SYS_FS_HANDLE_INVALID) {
        /* #690 review (Qodo): distinguish "directory doesn't exist yet" (the
         * normal first-write case → 0 files) from a real FS error. FAIL SAFE on
         * a real error — return cap so the guard REFUSES the create rather than
         * fail-open into the very wedge it exists to prevent. */
        SYS_FS_ERROR e = SYS_FS_Error();
        if (e == SYS_FS_ERROR_NO_PATH || e == SYS_FS_ERROR_NO_FILE) {
            return 0;                       // directory absent -> no files
        }
        LOG_E("[SD] CountDirEntries: DirOpen('%s') failed err=%d - failing safe (refuse)",
              dirPath, (int)e);
        if (pFsError != NULL) {
            *pFsError = true;
        }
        return cap;
    }
    uint32_t count = 0;
    SYS_FS_FSTAT st;
    memset(&st, 0, sizeof(st));
    while (count < cap) {
        if (SYS_FS_DirRead(dh, &st) == SYS_FS_RES_FAILURE) {
            /* #690: a mid-scan read error means the count is untrustworthy —
             * fail safe (refuse) rather than proceed on a partial count. Log
             * SYS_FS_Error() so a real media/FS fault is distinguishable in the
             * field from a genuinely full directory (both take the refuse path). */
            LOG_E("[SD] CountDirEntries: DirRead failed at %u err=%d - failing safe",
                  (unsigned)count, (int)SYS_FS_Error());
            (void)SYS_FS_DirClose(dh);
            if (pFsError != NULL) {
                *pFsError = true;
            }
            return cap;
        }
        if (st.fname[0] == '\0') {
            break;                          // end of directory
        }
        if (strcmp(st.fname, ".") == 0 || strcmp(st.fname, "..") == 0) {
            continue;                       // skip dot entries
        }
        count++;
    }
    (void)SYS_FS_DirClose(dh);
    return count;
}

/**
 * @brief Recursively lists files and directories, storing the output in a buffer.
 *
 * This function traverses the directory specified by `dirPath`, listing all files and directories
 * within it, including those in subdirectories. The output is formatted and stored in the buffer
 * pointed to by `pStrBuff`. It adjusts the buffer pointer and size during recursive calls to
 * ensure that data is appended correctly without overwriting existing content.
 *
 * @param[in]  dirPath     The path of the directory to list.
 * @param[out] pStrBuff    Pointer to the buffer where the output will be stored.
 *                         The buffer should be large enough to hold the expected output.
 * @param[in]  strBuffSize The total size of the buffer pointed to by `pStrBuff`.
 *
 * @return The total number of bytes written to `pStrBuff`.
 *
 * @note
 * - The function uses recursion to traverse subdirectories.
 * - It maintains a local `strBuffIndex` to keep track of the current position in the buffer.
 * - During recursive calls, the buffer pointer `pStrBuff` and buffer size `strBuffSize` are
 *   adjusted to prevent buffer overflows.
 * - If the buffer becomes full during execution, the function stops writing further data to prevent overflow.
 *
 * @warning
 * - Ensure that `strBuffSize` is sufficient to hold the entire output; otherwise, buffer overflows
 *   or incomplete output may occur.
 * - Be cautious with deeply nested directories, as excessive recursion can lead to stack overflow.
 *
 * @example
 * @code
 * uint8_t outputBuffer[1024];
 * size_t totalBytes = ListFilesInDirectory("/mnt/myDrive", outputBuffer, sizeof(outputBuffer));
 *
 * // Ensure null termination if needed
 * if (totalBytes < sizeof(outputBuffer)) {
 *     outputBuffer[totalBytes] = '\0';
 * } else {
 *     outputBuffer[sizeof(outputBuffer) - 1] = '\0'; // Ensure null termination
 * }
 *
 * printf("%s", outputBuffer);
 * @endcode
 */
// Callback function type for sending directory listing chunks
typedef void (*ListChunkCallback)(const uint8_t* data, size_t len);

/* #794: how a directory walk ended, so the reply can say so.
 *
 * SYST:STOR:SD:LISt? streamed "<path> <size>\r\n" per file and then simply
 * stopped, which made a complete listing and one cut short by a read timeout,
 * a buffer boundary or a stall-abort byte-identical: a run of well-formed
 * entries that ends. A host could therefore never prove ABSENCE -- "is my
 * capture still on the card?" answered "no files" from a truncated read.
 *
 * Every walk that can still talk to the host now ends with one marker line
 * carrying a status word. ABORTED is the deliberate exception: the abort
 * exists because the peer is not draining (#754), and each further send burns
 * USB_TRANSFER_MAX_RETRIES (~10 s) before giving up -- so an aborted listing
 * ends with NO marker, and its absence is what tells the host the reply is
 * incomplete. */
typedef enum {
    SD_LISTDIR_OK = 0,      /* walked the whole tree, every entry emitted */
    SD_LISTDIR_INCOMPLETE,  /* walk finished, but entries were skipped */
    SD_LISTDIR_FAILED,      /* nothing could be listed at all */
    SD_LISTDIR_ABORTED      /* peer stalled -- no marker is sent */
} ListDirResult;

#define SD_LIST_END_OK          "\r\n__END_OF_LIST__ OK"
#define SD_LIST_END_INCOMPLETE  "\r\n__END_OF_LIST__ INCOMPLETE"
#define SD_LIST_END_FAILED      "\r\n__END_OF_LIST__ FAILED"

// Static callback for sending directory listing chunks
static void sd_listdir_send_chunk(const uint8_t* data, size_t len) {
    /* #754: once an abort is pending, stop handing chunks to a transport that
     * is not draining. Each DataReadyCB burns USB_TRANSFER_MAX_RETRIES (~10 s)
     * before giving up, so continuing to send is what turns one stalled peer
     * into a multi-minute SD lockout for every other command. */
    if (gTransferAbortRequested) {
        return;
    }
    if (len > 0) {
        LOG_D("[SD] Sending chunk: %d bytes\r\n", (int)len);
        sd_card_manager_DataReadyCB(SD_CARD_MANAGER_MODE_LIST_DIRECTORY, (uint8_t*)data, len);
    }
}

// Iterative directory listing — explicit stack instead of function recursion.
// Each frame holds the open directory handle plus the path string used to
// build child paths during enumeration.  The stack lives in BSS so deep
// trees don't grow the SD task stack (see #351).
typedef struct {
    SYS_FS_HANDLE handle;
    char path[SD_CARD_MANAGER_FILE_PATH_LEN_MAX + 1];
} ListDirFrame;

static ListDirFrame gListDirStack[SD_CARD_MANAGER_MAX_LIST_DEPTH];

/* #795: true if every byte of `name` is one FAT/exFAT can actually store.
 * Illegal in a long file name: the C0 controls (< 0x20), DEL, and
 * these eight: " * / : < > ? \ | -- note '.' is NOT among them, it is the
 * ordinary extension separator. Bytes >= 0x80 are also legal (OEM code
 * page / UTF-8) and must not be rejected.
 *
 * `/` is rejected here because this sees ONE entry NAME from
 * SYS_FS_DirRead, never a path -- a slash inside a name is as impossible
 * as a newline. A host-side check on the printed reply must allow it,
 * since by then it is the separator in `DAQiFi/file.csv`.
 *
 * This exists because a name FAT cannot store did not come from a directory
 * entry, so emitting it tells the host a file exists that does not. #795
 * observed 45 such entries after a day of logging -- names carrying a raw
 * 0x0A and "sizes" that decode to this device's own CSV header text, while
 * SPACe? reported the card nearly empty. Whatever produced them (still open:
 * five hypotheses eliminated, the card has not been read on a PC), a host
 * cannot reject them by shape -- `DAQiFi/3214741.021 825373450` parses as a
 * perfectly well-formed `<path> <size>` line. This filter is correct
 * independent of that root cause. */
static bool sd_listdir_name_is_storable(const char *name, size_t maxLen)
{
    for (size_t i = 0; i < maxLen; i++) {
        const unsigned char c = (const unsigned char)name[i];
        if (c == '\0') {
            return true;                /* terminated, every byte legal */
        }
        if (c < 0x20u || c == 0x7Fu) {
            return false;
        }
        if (strchr("\"*/:<>?\\|", (int)c) != NULL) {
            return false;
        }
    }
    /* No terminator inside the field. SYS_FS_FSTAT.fname is a fixed 256-byte
     * array followed by lfname/lfsize, so walking to a NUL would run off it
     * into adjacent struct members and then the stack -- and this helper is
     * called precisely on entries already suspected of being corrupt. An
     * unterminated name is also not a real directory entry, so rejecting it
     * is both the safe answer and the correct one. */
    return false;
}

static ListDirResult ListFilesInDirectoryChunked(const char* dirPath, uint8_t *pStrBuff, size_t strBuffSize, ListChunkCallback sendChunk) {
    SYS_FS_FSTAT stat;
    size_t strBuffIndex = 0;
    /* #794: set wherever the walk drops something -- a depth cap, a directory
     * it could not read or open, a path or entry that did not fit. The walk
     * still finishes; the reply just cannot claim to be the whole card. */
    bool skipped = false;
    /* #795: entries dropped for carrying a name FAT cannot store. Counted
     * separately from `skipped` so the summary can report how many, and so
     * the detailed diagnostic below is emitted only for the first one -- a
     * corrupt directory produced 45 of them, which would evict every other
     * message from the 64-entry log buffer. */
    unsigned rejectedNames = 0;
    ListDirResult result = SD_LISTDIR_OK;
    char newPath[SD_CARD_MANAGER_FILE_PATH_LEN_MAX + 1];
    int sp = -1;  // Stack pointer: -1 = empty, 0..MAX-1 = current frame

    memset(newPath, 0, sizeof (newPath));
    memset(&stat, 0, sizeof (stat));

    /* Clear on entry, exactly as the READ_FROM_FILE loop does. An abort
     * requested while no transfer was running would otherwise be latched and
     * kill the NEXT listing instead of the one it was meant for (#754). */
    gTransferAbortRequested = false;

    LOG_D("[SD] ListFiles: Opening directory '%s'\r\n", dirPath);

    SYS_FS_HANDLE rootHandle = SYS_FS_DirOpen(dirPath);
    if (rootHandle == SYS_FS_HANDLE_INVALID) {
        SYS_FS_ERROR err = SYS_FS_Error();
        LOG_E("[SD] ListFiles: Failed to open directory '%s', error=%d\r\n", dirPath, err);
        strBuffIndex += snprintf((char *) pStrBuff + strBuffIndex, strBuffSize - strBuffIndex,
                "\r\n[Error:%d]Failed to open directory [%s]\r\n", err, dirPath);
        if (strBuffIndex > 0 && sendChunk) {
            sendChunk(pStrBuff, strBuffIndex);
        }
        strBuffIndex = 0;
        result = SD_LISTDIR_FAILED;
        goto done;
    }

    sp = 0;
    gListDirStack[sp].handle = rootHandle;
    int rootPn = snprintf(gListDirStack[sp].path, sizeof(gListDirStack[sp].path), "%s", dirPath);
    if (rootPn < 0 || (size_t)rootPn >= sizeof(gListDirStack[sp].path)) {
        LOG_E("[SD] ListFiles: Root path too long, aborting list");
        if (SYS_FS_DirClose(rootHandle) == SYS_FS_RES_FAILURE) {
            LOG_E("[SD] ListFiles: Failed to close directory, error=%d", SYS_FS_Error());
        }
        strBuffIndex += snprintf((char *)pStrBuff + strBuffIndex, strBuffSize - strBuffIndex,
                "\r\n[Error]Directory path too long\r\n");
        if (sendChunk && strBuffIndex > 0) {
            sendChunk(pStrBuff, strBuffIndex);
        }
        strBuffIndex = 0;
        result = SD_LISTDIR_FAILED;
        goto done;
    }

    while (sp >= 0) {
        /* #754: the LIST path never consumed this flag — #753 added it with a
         * single consumer at the top of the READ_FROM_FILE loop. So a listing
         * to a connected-but-stalled peer walked the entire tree, ~10 s per
         * chunk, holding gSDOpMutex throughout and rejecting every other SD
         * command with "SD card busy".
         *
         * Unwind the WHOLE stack rather than returning directly: each frame
         * holds an open SYS_FS directory handle, and bailing out without
         * closing them leaks handles for the rest of the session. */
        if (gTransferAbortRequested) {
            gTransferAbortRequested = false;
            LOG_E("[SD] ListFiles: ABORTED (peer not draining), closing %d dir handle(s)",
                  sp + 1);
            while (sp >= 0) {
                if (SYS_FS_DirClose(gListDirStack[sp].handle) == SYS_FS_RES_FAILURE) {
                    LOG_E("[SD] ListFiles: Failed to close directory on abort, error=%d",
                          SYS_FS_Error());
                }
                sp--;
            }
            /* Drop whatever was still buffered. Before #794 this path returned
             * outright and never flushed it; routing every exit through `done`
             * would have handed those entries to sd_listdir_send_chunk, and
             * the abort just cleared the flag that makes it a no-op -- so a
             * peer that stopped draining would get one more chunk and one more
             * ~10 s of transfer retries, which is the lockout #754 exists to
             * end. */
            strBuffIndex = 0;
            result = SD_LISTDIR_ABORTED;
            goto done;
        }

        if (SYS_FS_DirRead(gListDirStack[sp].handle, &stat) == SYS_FS_RES_FAILURE) {
            SYS_FS_ERROR err = SYS_FS_Error();
            LOG_E("[SD] ListFiles: Failed to read directory '%s', error=%d",
                  gListDirStack[sp].path, err);
            // Pre-flush if the diagnostic won't fit, so it's never silently dropped.
            // Worst-case length: sizeof literal (already includes both CRLFs and NUL) - 1 + 10 digits (err).
            const size_t needed = sizeof("\r\n[Error:]Failed to read directory\r\n") - 1U + 10U;
            if (sendChunk && strBuffIndex > 0 && needed >= (strBuffSize - strBuffIndex)) {
                pStrBuff[strBuffIndex] = '\0';
                sendChunk(pStrBuff, strBuffIndex);
                strBuffIndex = 0;
            }
            int errN = snprintf((char *) pStrBuff + strBuffIndex, strBuffSize - strBuffIndex,
                    "\r\n[Error:%d]Failed to read directory\r\n", err);
            if (errN > 0 && (size_t)errN < strBuffSize - strBuffIndex) {
                strBuffIndex += (size_t)errN;
            }
            if (SYS_FS_DirClose(gListDirStack[sp].handle) == SYS_FS_RES_FAILURE) {
                LOG_E("[SD] ListFiles: Failed to close directory '%s', error=%d",
                      gListDirStack[sp].path, SYS_FS_Error());
            }
            skipped = true;     /* #794: the rest of this directory is missing */
            sp--;
            continue;
        }

        if (stat.fname[0] == '\0') {
            LOG_D("[SD] ListFiles: End of directory '%s'\r\n", gListDirStack[sp].path);
            if (SYS_FS_DirClose(gListDirStack[sp].handle) == SYS_FS_RES_FAILURE) {
                LOG_E("[SD] ListFiles: Failed to close directory, error=%d", SYS_FS_Error());
            }
            sp--;
            continue;
        }

        if (!sd_listdir_name_is_storable(stat.fname, sizeof(stat.fname))) {
            /* Log the raw bytes once. This is the observation #795 has been
             * missing: if they decode to file content the read path handed
             * back a data sector, if they look like FAT structures the
             * on-card directory is damaged. The issue cannot currently tell
             * those apart, and they lead opposite ways. */
            if (rejectedNames == 0) {
                char hex[3 * 16 + 1];
                size_t i = 0;
                while (i < 16u && stat.fname[i] != '\0') {
                    snprintf(hex + i * 3u, 4u, "%02X ", (unsigned)(unsigned char)stat.fname[i]);
                    i++;
                }
                hex[i * 3u] = '\0';
                LOG_E("[SD] ListFiles: #795 unstorable name in '%s' -- bytes: %s(attrib=0x%02X size=%u)",
                      gListDirStack[sp].path, hex, (unsigned)stat.fattrib,
                      (unsigned)stat.fsize);
            }
            rejectedNames++;
            skipped = true;     /* #794: the reply is not the whole card */
            continue;
        }

        /* Logged AFTER validation, deliberately. This is a %s on a name the
         * walk does not yet trust: before the check it could be unterminated
         * (a read past the field) and could carry control bytes that corrupt
         * the terminal. A name that fails validation is reported above as hex
         * instead, which is the useful form for #795 anyway. */
        LOG_D("[SD] ListFiles: Read entry '%s'\r\n", stat.fname);

        if (strcmp(stat.fname, ".") == 0 || strcmp(stat.fname, "..") == 0) {
            continue;
        }

        int pn = snprintf(newPath, sizeof(newPath), "%s/%s",
                          gListDirStack[sp].path, stat.fname);
        if (pn < 0 || (size_t)pn >= sizeof(newPath)) {
            LOG_E("[SD] ListFiles: Path too long, skipping '%s/%s'",
                  gListDirStack[sp].path, stat.fname);
            skipped = true;     /* #794 */
            continue;
        }

        if (stat.fattrib & SYS_FS_ATTR_DIR) {
            if (sp + 1 >= SD_CARD_MANAGER_MAX_LIST_DEPTH) {
                LOG_E("[SD] ListFiles: max depth %d reached at '%s', listing truncated",
                      SD_CARD_MANAGER_MAX_LIST_DEPTH, newPath);
                // Pre-flush if the diagnostic won't fit, so it's never silently dropped.
                // Worst-case length: sizeof literal (already includes both CRLFs and NUL) - 1 + 10 digits + path.
                const size_t needed = sizeof("\r\n[Error]Listing truncated: max depth  reached at []\r\n")
                                    - 1U + 10U + strlen(newPath);
                if (sendChunk && strBuffIndex > 0 && needed >= (strBuffSize - strBuffIndex)) {
                    pStrBuff[strBuffIndex] = '\0';
                    sendChunk(pStrBuff, strBuffIndex);
                    strBuffIndex = 0;
                }
                int depthN = snprintf((char *) pStrBuff + strBuffIndex,
                        strBuffSize - strBuffIndex,
                        "\r\n[Error]Listing truncated: max depth %d reached at [%s]\r\n",
                        SD_CARD_MANAGER_MAX_LIST_DEPTH, newPath);
                if (depthN > 0 && (size_t)depthN < strBuffSize - strBuffIndex) {
                    strBuffIndex += (size_t)depthN;
                }
                skipped = true;     /* #794: this subtree is not in the reply */
                continue;
            }
            LOG_D("[SD] ListFiles: Descending into '%s'\r\n", newPath);
            SYS_FS_HANDLE childHandle = SYS_FS_DirOpen(newPath);
            if (childHandle == SYS_FS_HANDLE_INVALID) {
                SYS_FS_ERROR err = SYS_FS_Error();
                LOG_E("[SD] ListFiles: Failed to open subdir '%s', error=%d", newPath, err);
                skipped = true;     /* #794: that subtree is not in the reply */
                continue;
            }
            sp++;
            gListDirStack[sp].handle = childHandle;
            snprintf(gListDirStack[sp].path, sizeof(gListDirStack[sp].path), "%s", newPath);
            continue;
        }

        LOG_D("[SD] ListFiles: Found file '%s'\r\n", newPath);
        // Conservative length estimate for the entry "<path> <size>\r\n":
        // path + ' ' + up to 10 digits (UINT32 max) + \r\n
        size_t estLen = strlen(newPath) + 1U + 10U + 2U;
        int n;

        // Check if buffer is getting full - need space for this entry
        if ((strBuffIndex + estLen) >= (strBuffSize - 4)) {
            // Send current chunk before adding this entry
            if (sendChunk && strBuffIndex > 0) {
                pStrBuff[strBuffIndex] = '\0';
                sendChunk(pStrBuff, strBuffIndex);
                strBuffIndex = 0;  // Reset buffer for next chunk
            }
        }

        // Now add the filename and size to buffer (space-separated)
        n = snprintf((char *) pStrBuff + strBuffIndex, strBuffSize - strBuffIndex,
                "%s %u\r\n", newPath, (unsigned)stat.fsize);
        if (n > 0 && (size_t)n < strBuffSize - strBuffIndex) {
            strBuffIndex += (size_t)n;
        } else {
            // Entry doesn't fit - flush buffer and retry with fresh buffer.
            //
            // Every way out of here that does NOT get the entry into the buffer
            // has to set `skipped`, or the walk reports OK for a listing the
            // host never received in full. Three of them are real: no callback
            // to flush through, a buffer that was already empty (so the
            // snprintf above WAS the empty-buffer attempt), and an entry too
            // long even then. The last is reachable, not theoretical --
            // newPath is SYS_FS_FILE_NAME_LEN*2+1 = 511 bytes against a
            // 512-byte chunk buffer, so one deep path plus its size and CRLF
            // does not fit at any buffer occupancy, and #689's bucket
            // subdirectories make paths deeper.
            bool appended = false;
            if (sendChunk && strBuffIndex > 0) {
                pStrBuff[strBuffIndex] = '\0';
                sendChunk(pStrBuff, strBuffIndex);
                strBuffIndex = 0;

                // Retry with fresh buffer
                n = snprintf((char *) pStrBuff, strBuffSize,
                            "%s %u\r\n", newPath, (unsigned)stat.fsize);
                if (n > 0 && (size_t)n < strBuffSize) {
                    strBuffIndex = (size_t)n;
                    appended = true;
                }
            }
            if (!appended) {
                LOG_E("[SD] ListFiles: entry does not fit, omitting '%s'", newPath);
                skipped = true;     /* #794 */
            }
        }
    }

    if (rejectedNames > 0) {
        LOG_E("[SD] ListFiles: #795 dropped %u entry/entries with unstorable names",
              rejectedNames);
    }

    if (skipped && result == SD_LISTDIR_OK) {
        result = SD_LISTDIR_INCOMPLETE;
    }

done:
    // Send final chunk if any data remains
    if (sendChunk && strBuffIndex > 0) {
        // Remove trailing CRLF from final chunk to avoid extra blank line before prompt
        while (strBuffIndex > 0 && (pStrBuff[strBuffIndex - 1] == '\r' || pStrBuff[strBuffIndex - 1] == '\n')) {
            strBuffIndex--;
        }
        if (strBuffIndex > 0) {
            pStrBuff[strBuffIndex] = '\0';
            sendChunk(pStrBuff, strBuffIndex);
        }
    }

    /* #794: one marker line, so a host can tell a finished listing from a
     * truncated one. Deliberately NOT sent on abort: the peer is not draining
     * (that is why the walk aborted), and every further chunk burns
     * USB_TRANSFER_MAX_RETRIES (~10 s) before it gives up -- the multi-minute
     * SD lockout #754 was filed for. An aborted listing therefore ends with
     * no marker at all, and that absence is the signal. */
    /* The marker is a flash-resident string literal, like the SD:GET path's
     * __END_OF_FILE__ a few hundred lines below. That is safe here because
     * the reply path COPIES: DataReadyCB hands the pointer to
     * sd_reply_write_usb / _tcp, which write into their own buffers rather
     * than DMA-ing from the caller's. */
    if (sendChunk && result != SD_LISTDIR_ABORTED) {
        const char *marker = (result == SD_LISTDIR_OK)         ? SD_LIST_END_OK
                           : (result == SD_LISTDIR_INCOMPLETE) ? SD_LIST_END_INCOMPLETE
                                                               : SD_LIST_END_FAILED;
        sendChunk((const uint8_t *)marker, strlen(marker));
    }

    /* The peer can stop draining while these last sends are in flight:
     * DataReadyCB requests the abort synchronously, and sd_listdir_send_chunk
     * then drops the marker on the floor rather than burning another
     * USB_TRANSFER_MAX_RETRIES on it. That is the right transport behaviour,
     * but it leaves the walk's earlier verdict describing a reply the host
     * did not get. What the host actually saw is a listing that ended with no
     * marker -- which is ABORTED -- so say that, and let the log agree with
     * the wire. */
    if (gTransferAbortRequested) {
        result = SD_LISTDIR_ABORTED;
    }
    return result;
}

bool sd_card_manager_Init(sd_card_manager_settings_t *pSettings) {
    static bool isInitDone = false;
    if (!isInitDone) {
        // Get SD circular buffer from streaming pool (CPU-only, no DMA needed)
        StreamingBufferPool_GetSdCircular(&gSdSharedBuffer, &gSdSharedBufferSize);
        if (gSdSharedBuffer == NULL) {
            LOG_E("[SD] Failed to get SD circular buffer from streaming pool");
            return false;
        }

        // Allocate SD DMA write buffer from coherent pool (DMA-safe for SPI/FatFS)
        gSDCardData.writeBufferSize = SD_CARD_MANAGER_CONF_WBUFFER_SIZE;
        gSDCardData.writeBuffer = CoherentPool_Alloc("SD_write", gSDCardData.writeBufferSize);
        if (gSDCardData.writeBuffer == NULL) {
            LOG_E("[SD] Failed to allocate %u bytes from coherent pool for write buffer",
                  (unsigned)gSDCardData.writeBufferSize);
            return false;
        }

        // Initialize circular buffer using streaming pool memory
        CircularBuf_InitExternal(&gSDCardData.wCirbuf, CircularBufferToSDWrite,
                                 gSdSharedBuffer, gSdSharedBufferSize);

        gSDCardData.wMutex = xSemaphoreCreateMutex();
        xSemaphoreGive(gSDCardData.wMutex);
        gSDCardData.opCompleteSemaphore = xSemaphoreCreateBinary();
        gSDCardData.lastOperationSuccess = true;  // Initialize to success

        // Create operation mutex to serialize READ/WRITE/LIST on gSDSharedBuffer
        if (gSDOpMutex == NULL) {
            gSDOpMutex = xSemaphoreCreateMutex();
        }
        isInitDone = true;
        gpSDCardSettings = pSettings;
        gSDCardData.fileHandle = SYS_FS_HANDLE_INVALID;
        gSDCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_INIT;

        // Initialize file splitting state
        memset(gSDCardData.baseFilename, 0, sizeof(gSDCardData.baseFilename));
        gSDCardData.fileCounter = 0;
        gSDCardData.currentFileBytes = 0;
        gSDCardData.fileSplittingEnabled = false;
    }
    return true;
}

/**
 * @brief Extracts base filename (without extension) from given filename.
 *
 * Example: "data.csv" → "data" stored in baseFilename field
 *
 * @param filename Input filename (may include extension)
 */
static void extractBaseFilename(const char* filename) {
    strncpy(gSDCardData.baseFilename, filename, sizeof(gSDCardData.baseFilename) - 1);
    gSDCardData.baseFilename[sizeof(gSDCardData.baseFilename) - 1] = '\0';

    // Find last dot for extension
    char* ext = strrchr(gSDCardData.baseFilename, '.');
    if (ext != NULL) {
        *ext = '\0';  // Remove extension from base filename
    }
}

/* #689: build the path of bucket `bucket` under `dir`. Bucket 0 IS `dir`, so
 * a session that never fills one bucket produces exactly the pre-#689 paths. */
static bool sd_BuildBucketPath(char* out, size_t outLen, const char* dir,
                               uint32_t bucket) {
    int written;
    if (bucket == 0u) {
        written = snprintf(out, outLen, "%s", dir);
    } else {
        written = snprintf(out, outLen, "%s/" SD_CARD_MANAGER_BUCKET_PREFIX "%03u",
                           dir, (unsigned)bucket);
    }
    if (written < 0 || (size_t)written >= outLen) {
        LOG_E("[%s:%d]Bucket path too long: dir='%s' bucket=%u (max %zu)",
              __FILE__, __LINE__, dir, (unsigned)bucket, outLen);
        out[0] = '\0';
        return false;
    }
    return true;
}



/* #689: make `bucket` the active one — create its directory if needed and
 * count what it already holds, ONCE, so the per-rotation fullness test is
 * arithmetic rather than another O(N) scan.
 *
 * An existing directory is success, not an error: buckets are reused across
 * sessions by design (the same base filename overwrites the same part names,
 * matching the pre-#689 single-directory behaviour). */
static bool sd_EnterBucket(const char* dir, uint32_t bucket) {
    if (!sd_BuildBucketPath(gSDCardData.bucketPath,
                            sizeof(gSDCardData.bucketPath), dir, bucket)) {
        /* Keep the invariant "refused => a reason is recorded". Without this the
         * caller sets startupDirFull while WriteRefuseText says "no refusal
         * recorded" -- precisely the misdirection the reason codes exist to
         * remove. Unreachable today (a 40-char directory cannot overflow a
         * 510-byte path) but the invariant should not depend on that. */
        gSDCardData.writeRefuseReason = SD_REFUSE_BUCKET_MKDIR;
        return false;
    }
    if (bucket != 0u) {
        if (SYS_FS_DirectoryMake(gSDCardData.bucketPath) == SYS_FS_RES_FAILURE) {
            SYS_FS_ERROR e = SYS_FS_Error();
            if (e != SYS_FS_ERROR_EXIST) {
                LOG_E("[SD] #689 bucket mkdir '%s' failed err=%d",
                      gSDCardData.bucketPath, (int)e);
                gSDCardData.writeRefuseReason = SD_REFUSE_BUCKET_MKDIR;
                return false;
            }
            /* EXIST means the NAME is taken, not that a directory is there --
             * FatFs returns it for any collision, including a regular file. If
             * a file occupies the bucket name, the count below opens it, gets
             * NO_PATH, and reports "absent -> 0 files", so the roll stops here
             * and every later bucket becomes unreachable while the open fails
             * in a loop. Reachable by an operator-placed extensionless file, or
             * self-inflicted by pointing SD:FILe at a bucket name.
             *
             * Check what is actually there and refuse precisely instead. */
            SYS_FS_FSTAT st;
            memset(&st, 0, sizeof(st));
            if (SYS_FS_FileStat(gSDCardData.bucketPath, &st) != SYS_FS_RES_SUCCESS ||
                (st.fattrib & SYS_FS_ATTR_DIR) == 0) {
                LOG_E("[SD] #689 bucket name '%s' is taken by a non-directory - "
                      "rename or remove it, or use a different directory",
                      gSDCardData.bucketPath);
                gSDCardData.writeRefuseReason = SD_REFUSE_BUCKET_NOT_DIR;
                return false;
            }
        }
    }
    gSDCardData.curBucket = bucket;
    /* An FS error must NOT be read as "this bucket is full": that would roll us
     * forward, creating a spurious empty directory per attempt on a filesystem
     * that is already failing. Refuse instead -- the caller's clean stop is the
     * right answer for a broken card. */
    bool fsError = false;
    gSDCardData.bucketFileCountAtStart =
        CountDirEntries(gSDCardData.bucketPath, SD_CARD_MANAGER_MAX_DIR_FILES,
                        &fsError);
    if (fsError) {
        LOG_E("[SD] #689 bucket '%s' unreadable - refusing rather than rolling",
              gSDCardData.bucketPath);
        gSDCardData.writeRefuseReason = SD_REFUSE_BUCKET_UNREADABLE;
        return false;
    }
    gSDCardData.filesInCurBucket = 0u;
    gSDCardData.writeRefuseReason = SD_REFUSE_NONE;
    LOG_D("[SD] #689 bucket '%s' active (holds %u)\r\n", gSDCardData.bucketPath,
          (unsigned)gSDCardData.bucketFileCountAtStart);
    return true;
}

/**
 * @brief Generates filename with sequential numbering.
 *
 * Format: basename-N.ext, NOT zero-padded (e.g., "data-1.csv", "data-2.csv",
 * ... "data-10.csv") -- the counter is written with a plain %u below.
 *
 * @param outPath Output buffer for full path
 * @param maxLen Maximum length of output buffer
 * @param counter File counter (1, 2, 3, ...)
 * @param directory Directory path
 * @param baseFilename Base filename (without extension)
 * @param originalFilename Original filename (used to extract extension)
 */
static void generateFilename(char* outPath, size_t maxLen, uint32_t counter,
                             const char* directory, const char* baseFilename,
                             const char* originalFilename) {
    // Extract extension (includes leading dot if present)
    const char* ext = strrchr(originalFilename, '.');
    const char* useExt = (ext != NULL) ? ext : "";

    // Pre-validate length to avoid truncation
    size_t dirLen = strlen(directory);
    size_t baseLen = strlen(baseFilename);
    size_t origLen = strlen(originalFilename);
    if (counter == 0) {
        if (dirLen + 1 + origLen + 1 > maxLen) {
            LOG_E("[%s:%d]Filename too long: dir='%s' file='%s' (max %zu)",
                  __FILE__, __LINE__, directory, originalFilename, maxLen);
            outPath[0] = '\0';
            return;
        }
    } else {
        if (dirLen + 1 + baseLen + 1 + 10 + strlen(useExt) + 1 > maxLen) {
            LOG_E("[%s:%d]Filename too long for split file: dir='%s' base='%s' ext='%s' (max %zu)",
                  __FILE__, __LINE__, directory, baseFilename, useExt, maxLen);
            outPath[0] = '\0';
            return;
        }
    }

    int written;
    if (counter == 0) {
        written = snprintf(outPath, maxLen, "%s/%s", directory, originalFilename);
    } else {
        written = snprintf(outPath, maxLen, "%s/%s-%u%s", directory, baseFilename, counter, useExt);
    }

    if (written < 0 || (size_t)written >= maxLen) {
        LOG_E("[%s:%d]Filename buffer overflow (needed %d bytes, have %zu): dir='%s' base='%s' cnt=%u ext='%s'",
              __FILE__, __LINE__, written, maxLen, directory, baseFilename, counter, useExt);
        outPath[0] = '\0';
    }
}

/* #689: is a bucket directory present? Buckets are created in ascending order,
 * so the first absent one ends a forward search. Checks the DIR attribute for
 * the same reason sd_EnterBucket does: a regular file wearing a bucket name
 * must not read as a bucket.
 *
 * *fsError is set when the stat failed for a reason OTHER than "not there".
 * Collapsing those into "absent" would end the search early on a failing card
 * and silently create a duplicate part -- the same conflation CountDirEntries
 * refuses to make, and the caller fails safe the same way it does. */
static bool sd_BucketDirExists(const char* bucketPath, bool* fsError) {
    *fsError = false;
    SYS_FS_FSTAT st;
    memset(&st, 0, sizeof(st));
    if (SYS_FS_FileStat(bucketPath, &st) != SYS_FS_RES_SUCCESS) {
        SYS_FS_ERROR e = SYS_FS_Error();
        if (e != SYS_FS_ERROR_NO_PATH && e != SYS_FS_ERROR_NO_FILE) {
            LOG_E("[SD] #689 bucket stat '%s' failed err=%d - failing safe "
                  "rather than reading it as absent", bucketPath, (int)e);
            *fsError = true;
        }
        return false;
    }
    return ((st.fattrib & SYS_FS_ATTR_DIR) != 0);
}

/* #689: does this bucket already hold the part about to be opened? Used to
 * reopen a part in place instead of creating a duplicate in a later bucket.
 *
 * A DIRECTORY at the target path is not a reopenable part. Accepting one
 * would set reopenExisting, suppress the roll, and then fail the
 * FileOpen(...WRITE_PLUS) that follows -- turning an operator-created name
 * collision into a stuck open instead of letting the roll move past it. This
 * is the mirror of the check sd_EnterBucket makes on SYS_FS_ERROR_EXIST,
 * where the ambiguity runs the other way (a regular file wearing a bucket
 * name); FatFs reports presence, never kind, so both sites must ask.
 *
 * *fsError follows the same rule as sd_BucketDirExists and CountDirEntries: a
 * stat that fails for a reason OTHER than "not there" must not be reported as
 * absence. Reading a fault as "no such part" makes the caller create a SECOND
 * copy of a part that does exist, which is the one outcome this whole search
 * is here to prevent -- and it would do so silently, on a card that is telling
 * us it is unwell. */
static bool sd_TargetExistsInBucketPath(const char* bucketPath, bool* fsError) {
    *fsError = false;
    /* Scratch in gSDCardData.filePath rather than a ~511-byte stack local.
     * Two such buffers would otherwise live at once during the reuse pre-walk
     * -- this one and the caller's probePath -- against an SD task stack sized
     * on a PROFILED peak (see the SDCardTask xTaskCreate note in
     * app_freertos.c). A static is not an option: BSS is at its ceiling on
     * this target, and adding ~1 KB fails the link outright with "Not enough
     * memory for stack".
     *
     * Safe, and narrowly so -- keep it that way: filePath is memset at
     * OPEN_FILE entry, this function is called only from the WRITE branch's
     * pre-walk, and the REAL path is written over it by generateFilename
     * further down before anything reads it. Do not add a read of filePath
     * between those two points. */
    char* candidate = gSDCardData.filePath;
    /* sizeof(gSDCardData.filePath), NOT sizeof(candidate) -- candidate is a
     * pointer here, so sizeof would be 4 and generateFilename would reject
     * every path as too long. */
    generateFilename(candidate, sizeof(gSDCardData.filePath),
                     gSDCardData.fileCounter,
                     bucketPath, gSDCardData.baseFilename,
                     gpSDCardSettings->file);
    if (candidate[0] == '\0') {
        return false;
    }
    SYS_FS_FSTAT st;
    memset(&st, 0, sizeof(st));
    if (SYS_FS_FileStat(candidate, &st) != SYS_FS_RES_SUCCESS) {
        SYS_FS_ERROR e = SYS_FS_Error();
        if (e != SYS_FS_ERROR_NO_PATH && e != SYS_FS_ERROR_NO_FILE) {
            LOG_E("[SD] #689 part stat '%s' failed err=%d - failing safe "
                  "rather than reading it as absent", candidate, (int)e);
            *fsError = true;
        }
        return false;
    }
    return ((st.fattrib & SYS_FS_ATTR_DIR) == 0);
}

/* #800: a teardown raised while the SD task is mid-transition must not be lost.
 *
 * sd_card_manager_UpdateSettings() and sd_card_manager_Deinit() run on the
 * CALLER's task and tear a session down by forcing
 * currentProcessState = DEINIT. The SD task writes that same field
 * unconditionally from ~56 sites, in the shape "decide, then store NEXT_STATE".
 * A teardown landing between a decision and its store is therefore silently
 * OVERWRITTEN: the operation the teardown existed to stop carries on, does the
 * filesystem work, and ends in ERROR instead of unmounting cleanly. Nothing
 * logs it, because from the SD task's point of view nothing happened.
 *
 * A flag cannot be clobbered that way. The store that loses the state does not
 * touch it, so the request survives and is honoured at the next iteration.
 *
 * Why this rather than a critical section at each store: #782 fixed exactly one
 * site that way (see OPEN_FILE), and its comment is right that the compare and
 * the store must be one atomic step. Doing that ~56 times would be 56 chances
 * to get it wrong, and it would still leave any site added later unprotected.
 * This is one check, at the one place every iteration passes through.
 *
 * The residual, stated rather than glossed: the teardown is honoured on the
 * NEXT iteration, so at most one further state transition runs first. That
 * bounds the damage to a single step instead of a whole operation -- it does
 * not make the handoff instantaneous. A truly synchronous teardown needs the
 * per-site atomicity above, which is a bigger change than this issue warrants.
 *
 * volatile: written by the caller's task, read by the SD task. A bool store is
 * atomic on PIC32MZ, and the read-then-clear below is single-writer (SD task
 * only), so a concurrent set is preserved for the next pass rather than lost --
 * no critical section is needed for the flag itself. */
static volatile bool gSdTeardownRequested = false;

/* #757: end a rotation's open window on a path that will never write.
 *
 * Closing the window is not optional. While gSdRotating is set the streaming
 * task keeps filling the circular buffer on the strength of a promise that
 * something will drain it; if the open ends without a handle -- a bucket
 * refusal, a teardown, a failed create -- that promise is broken and the flag
 * must be cleared or the encoder buffers forever into a dead path.
 *
 * Whatever it already buffered cannot be recovered: there is no file to write
 * it to. It is still real SD loss, so it is counted rather than dropped
 * quietly, and the buffer is reset so the next session does not inherit a
 * half-written file's header.
 *
 * The success path does NOT come here -- it just clears the flag, because the
 * WRITE_TO_FILE arm of IsBufferAccepting() takes over and the buffered bytes
 * are about to be written, not lost. */
/* #799: which directory an operation should act on.

 * Only SD:LISt? sets `opDirectory`, and only for the duration of that one
 * listing -- it is cleared when the command is armed without an operand. Every
 * other operation (GET / DELete / CRC / logging) reads `directory`, the
 * persistent working directory, which now changes ONLY through SD:DIRectory.
 *
 * Before this, the listing copied its operand into `directory` and left it
 * there, so a query silently repositioned all of those. */
static const char* sd_ListDirTarget(const sd_card_manager_settings_t* cfg) {
    if (cfg == NULL) {
        return "";
    }
    if (cfg->mode == SD_CARD_MANAGER_MODE_LIST_DIRECTORY
            && cfg->opDirectory[0] != '\0') {
        return cfg->opDirectory;
    }
    return cfg->directory;
}

static void sd_AbandonRotationWindow(const char* why) {
    /* Clear INSIDE the mutex, not before taking it. sd_card_manager_WriteToBuffer
     * re-checks sd_card_manager_IsBufferAccepting() under this same mutex, so
     * ordering them this way leaves no gap: a writer that gets the mutex first
     * writes, and the count below includes its bytes; a writer that gets it
     * after sees the flag already false and refuses. Clearing before the take
     * would let a writer blocked on the mutex append AFTER the reset, and those
     * bytes would be neither drained nor counted. */
    SD_TakeMutexDebug(gSDCardData.wMutex, "abandon_rotation");
    gSdRotating = false;
    size_t buffered = CircularBuf_NumBytesAvailable(&gSDCardData.wCirbuf);
    CircularBuf_Reset(&gSDCardData.wCirbuf);
    xSemaphoreGive(gSDCardData.wMutex);
    if (buffered > 0u) {
        Streaming_ReportSdDiscard(buffered);
        LOG_E("[SD] rotation open abandoned (%s): discarded %u buffered byte(s)",
              why, (unsigned)buffered);
    }
}

void sd_card_manager_ProcessState() {
    /* #800: honour a teardown that raced a state store, before dispatching. */
    if (gSdTeardownRequested) {
        gSdTeardownRequested = false;
        /* #757: this override can land while a rotation window is open --
         * a STOP arriving between the rotation's close and OPEN_FILE. Forcing
         * DEINIT here means OPEN_FILE never runs, so the abort path that would
         * have closed the window and accounted for its bytes never runs
         * either: the flag stays set and the bytes vanish from SdDroppedBytes.
         * Route it through the same accounting every other abandon uses. */
        if (gSdRotating) {
            sd_AbandonRotationWindow("teardown during rotation");
        }
        gSDCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_DEINIT;
    }

    /* Check the application's current state. */

    switch (gSDCardData.currentProcessState) {
        case SD_CARD_MANAGER_PROCESS_STATE_INIT:
            /* #757: a rotation window never spans an INIT -- a rotation goes
             * WRITE_TO_FILE -> OPEN_FILE directly -- so reaching here with the
             * flag still set means the previous session was interrupted between
             * the rotation's close and the open that would have cleared it.
             * app_SDCardTask parking in SUSPENDED for a WiFi streaming session
             * (#589) is one way to get there, since it pumps neither this state
             * machine nor the driver.
             *
             * mode != WRITE already makes IsBufferAccepting() false in that
             * state, so nothing streams into a dead buffer -- but a STALE true
             * would survive into the next write session and make OPEN_FILE skip
             * the metadata and buffer reset on a FIRST open, which is exactly
             * the case the skip is not meant to cover. Clear it here so the
             * flag cannot outlive the session that set it. */
            gSdRotating = false;
            // Only initialize if SD is enabled AND has a valid operation mode
            // Just enabling SD without setting a mode (WRITE/READ/LIST) is valid - don't spam errors
            // GET_SPACE is allowed even when disabled (transient read-only query)
            if ((gpSDCardSettings->enable || gpSDCardSettings->mode == SD_CARD_MANAGER_MODE_GET_SPACE)
                && gpSDCardSettings->mode != SD_CARD_MANAGER_MODE_NONE) {
                // Validate directory and file settings.
                // #724: READ/CRC/DELETE operate on the transient `opFile`; WRITE
                // (logging) uses the persistent `file`. Validate the field the
                // active mode will actually open.
                bool isTransientOp =
                        (gpSDCardSettings->mode == SD_CARD_MANAGER_MODE_READ ||
                         gpSDCardSettings->mode == SD_CARD_MANAGER_MODE_COMPUTE_CRC ||
                         gpSDCardSettings->mode == SD_CARD_MANAGER_MODE_DELETE_FILE);
                const char* opName = isTransientOp ? gpSDCardSettings->opFile
                                                   : gpSDCardSettings->file;
                /* #799: validate the directory this op will actually use. For
                 * LIST that is the transient operand when present, so a listing
                 * of a valid directory is not refused because the persistent
                 * one happens to be empty (and vice versa). */
                const char* dirName = sd_ListDirTarget(gpSDCardSettings);
                bool dirValid = strlen(dirName) > 0 &&
                               strlen(dirName) <= SD_CARD_MANAGER_CONF_DIR_NAME_LEN_MAX;
                bool fileValid = strlen(opName) > 0 &&
                                strlen(opName) <= SD_CARD_MANAGER_CONF_FILE_NAME_LEN_MAX;

                // LIST and FORMAT modes don't need a filename
                // DELETE, READ, WRITE, CRC need a filename. #724 (Qodo): include
                // COMPUTE_CRC so its opFile is validated upfront (consistent with
                // isTransientOp above) rather than only failing at file open.
                bool needsFile = (gpSDCardSettings->mode == SD_CARD_MANAGER_MODE_DELETE_FILE ||
                                 gpSDCardSettings->mode == SD_CARD_MANAGER_MODE_READ ||
                                 gpSDCardSettings->mode == SD_CARD_MANAGER_MODE_COMPUTE_CRC ||
                                 gpSDCardSettings->mode == SD_CARD_MANAGER_MODE_WRITE);

                // GET_SPACE only needs to mount - no directory or file needed
                bool needsDir = (gpSDCardSettings->mode != SD_CARD_MANAGER_MODE_GET_SPACE);

                // Reset error flag on valid configuration (allows new errors to be logged after fix)
                static bool errorLogged = false;

                if ((!needsDir || dirValid) && (!needsFile || fileValid)) {
                    errorLogged = false;  // Reset flag on successful validation
                    gSDCardData.mountRetryCount = 0;
                    gSDCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_MOUNT_DISK;
                } else {
                    // Only log error once per enable, not continuously
                    if (!errorLogged) {
                        LOG_E("[%s:%d]Invalid SD Card Directory or file name (dir='%s', file='%s')",
                              __FILE__, __LINE__,
                              dirName,
                              opName);
                        errorLogged = true;
                    }
                }
            }
            break;
        case SD_CARD_MANAGER_PROCESS_STATE_MOUNT_DISK:
            gSDCardData.sdCardWritePending = 0;
            gSDCardData.writeBufferLength = 0;
            gSDCardData.sdCardWriteBufferOffset = 0;
            {
                TickType_t mountStart = xTaskGetTickCount();
                int mountResult = SYS_FS_Mount(SD_CARD_MANAGER_DISK_DEV_NAME, SD_CARD_MANAGER_DISK_MOUNT_NAME, FAT, 0, NULL);
                SD_CheckFsOpDuration(mountStart, "SYS_FS_Mount", mountResult);
                if (mountResult != 0) {
                    gSDCardData.mountRetryCount++;
                    if (gSDCardData.mountRetryCount >= SD_MOUNT_MAX_RETRIES) {
                        LOG_E("[SD] Mount failed after %d attempts - check SD card is FAT32 formatted",
                              gSDCardData.mountRetryCount);
                        /* #613: roll the enable back so the armed-but-broken
                         * state can't persist - it blocked ALL streaming
                         * starts (SD logging rides alongside USB when
                         * enabled) with a bare -200, and left the detect
                         * path unable to recover a later hot-insert without
                         * a reboot. User re-runs ENAble 1 after fixing the
                         * card - which also restarts detection cleanly. */
                        /* #616: only the streaming/write arm path may clear
                         * enable here. A transient read-only query
                         * (GET_SPACE/READ/LIST/DELETE) also reaches
                         * MOUNT_DISK and must NOT disarm the user's SD. */
                        if (gpSDCardSettings->mode == SD_CARD_MANAGER_MODE_WRITE) {
                            LOG_E("[SD] SD disabled - re-run SYST:STOR:SD:ENAble 1 after inserting/fixing the card");
                            gpSDCardSettings->enable = false;
                            /* #759: the card is gone; do not leave streaming
                             * aimed at it (see Streaming_SdInterfaceReleased). */
                            Streaming_SdInterfaceReleased();
                        }
                        gSDCardData.lastOperationSuccess = false;
                        gpSDCardSettings->mode = SD_CARD_MANAGER_MODE_NONE;
                        gSDCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_ERROR;
                    } else {
                        // Retry mount after delay
                        vTaskDelay(pdMS_TO_TICKS(SD_MOUNT_RETRY_DELAY_MS));
                        gSDCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_MOUNT_DISK;
                    }
                } else {
                    /* Mount was successful. Validate filesystem type. */
                    FATFS *fs = NULL;
                    uint32_t freeClusters = 0;
                    if (FATFS_getfree(SD_CARD_MANAGER_DISK_MOUNT_NAME, &freeClusters, &fs) == 0
                        && fs != NULL
                        && (fs->fs_type == FS_FAT16 || fs->fs_type == FS_FAT32)) {
                        gSDCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_CURRENT_DRIVE;
                        gSDCardData.discMounted = true;
                    } else {
                        LOG_E("[SD] Unsupported filesystem (type=%d) - reformat as FAT32",
                              fs ? fs->fs_type : 0);
                        /* #613/#616: only the streaming/write arm path may clear
                         * enable; a read-only query must not disarm SD. */
                        if (gpSDCardSettings->mode == SD_CARD_MANAGER_MODE_WRITE) {
                            gpSDCardSettings->enable = false;
                            /* #759: unusable filesystem is unusable card. */
                            Streaming_SdInterfaceReleased();
                        }
                        gSDCardData.lastOperationSuccess = false;
                        gpSDCardSettings->mode = SD_CARD_MANAGER_MODE_NONE;
                        gSDCardData.discMounted = true;  // Mark mounted so ERROR→UNMOUNT can clean up
                        gSDCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_ERROR;
                    }
                }
            }
            break;

        case SD_CARD_MANAGER_PROCESS_STATE_UNMOUNT_DISK:
            if (gSDCardData.fileHandle != SYS_FS_HANDLE_INVALID) {
                // --- Drain all in-flight data before closing file ---
                // 1. Flush pending writeBuffer (already extracted but not yet written)
                {
                    int drainIter = 0;
                    bool unmountDrainErrorLogged = false;
                    while (gSDCardData.sdCardWritePending == 1 && drainIter < 100) {
                        int pendingLen = SDCardWrite();
                        if (pendingLen > 0 && (size_t)pendingLen >= gSDCardData.writeBufferLength) {
                            gSDCardData.currentFileBytes += gSDCardData.writeBufferLength;
                            SD_TakeMutexDebug(gSDCardData.wMutex, "unmount_pending_write");
                            gSDCardData.sdCardWritePending = 0;
                            gSDCardData.writeBufferLength = 0;
                            gSDCardData.sdCardWriteBufferOffset = 0;
                            xSemaphoreGive(gSDCardData.wMutex);
                        } else if (pendingLen > 0) {
                            gSDCardData.currentFileBytes += pendingLen;
                            SD_TakeMutexDebug(gSDCardData.wMutex, "unmount_partial_write");
                            gSDCardData.writeBufferLength -= pendingLen;
                            gSDCardData.sdCardWriteBufferOffset += pendingLen;
                            xSemaphoreGive(gSDCardData.wMutex);
                        } else {
                            if (!unmountDrainErrorLogged) {
                                unmountDrainErrorLogged = true;
                                LOG_E("[SD] Error flushing pending write before unmount");
                            }
                            gSDCardData.sdCardWritePending = 0;
                            gSDCardData.writeBufferLength = 0;
                            gSDCardData.sdCardWriteBufferOffset = 0;
                            break;
                        }
                        drainIter++;
                    }

                    // 2. Drain circular buffer — extract remaining data (NOT sector-aligned)
                    drainIter = 0;
                    while (CircularBuf_NumBytesAvailable(&gSDCardData.wCirbuf) > 0
                           && drainIter < 100) {
                        int writeLen = -2;
                        SD_TakeMutexDebug(gSDCardData.wMutex, "unmount_drain_loop");
                        if (gSDCardData.sdCardWritePending != 1) {
                            gSDCardData.sdCardWritePending = 1;
                            /* #738: runtime size, not the compile-time
                             * ceiling — see the WRITE_TO_FILE extract. This is
                             * the close-time drain, so an over-large extract
                             * loses the tail of the file outright. */
                            CircularBuf_ProcessBytes(&gSDCardData.wCirbuf, NULL,
                                gSDCardData.writeBufferSize, &writeLen);
                            gSDCardData.totalBytesFlushPending += gSDCardData.writeBufferLength;
                            xSemaphoreGive(gSDCardData.wMutex);

                            // Write immediately, loop for partial writes
                            int innerIter = 0;
                            while (gSDCardData.sdCardWritePending == 1 && innerIter < 100) {
                                writeLen = SDCardWrite();
                                if (writeLen > 0 && (size_t)writeLen >= gSDCardData.writeBufferLength) {
                                    gSDCardData.currentFileBytes += gSDCardData.writeBufferLength;
                                    SD_TakeMutexDebug(gSDCardData.wMutex, "unmount_drain_complete");
                                    gSDCardData.sdCardWritePending = 0;
                                    gSDCardData.writeBufferLength = 0;
                                    gSDCardData.sdCardWriteBufferOffset = 0;
                                    xSemaphoreGive(gSDCardData.wMutex);
                                } else if (writeLen > 0) {
                                    gSDCardData.currentFileBytes += writeLen;
                                    SD_TakeMutexDebug(gSDCardData.wMutex, "unmount_drain_partial");
                                    gSDCardData.writeBufferLength -= writeLen;
                                    gSDCardData.sdCardWriteBufferOffset += writeLen;
                                    xSemaphoreGive(gSDCardData.wMutex);
                                } else {
                                    if (!unmountDrainErrorLogged) {
                                        unmountDrainErrorLogged = true;
                                        LOG_E("[SD] Error draining buffer before unmount");
                                    }
                                    gSDCardData.sdCardWritePending = 0;
                                    gSDCardData.writeBufferLength = 0;
                                    gSDCardData.sdCardWriteBufferOffset = 0;
                                    break;
                                }
                                innerIter++;
                            }
                        } else {
                            xSemaphoreGive(gSDCardData.wMutex);
                            break;
                        }
                        drainIter++;
                    }
                }

                // Flush filesystem buffers before closing
                SD_TakeMutexDebug(gSDCardData.wMutex, "unmount_pending_check");
                bool hasPendingData = gSDCardData.totalBytesFlushPending > 0;
                xSemaphoreGive(gSDCardData.wMutex);

                if (hasPendingData) {
                    LOG_D("[SD] Flushing %u bytes before unmount\r\n", (unsigned)gSDCardData.totalBytesFlushPending);
                    TickType_t syncStart = xTaskGetTickCount();
                    int syncResult = SYS_FS_FileSync(gSDCardData.fileHandle);
                    SD_CheckFsOpDuration(syncStart, "FileSync(unmount)", syncResult);
                    if (syncResult != -1) {
                        SD_TakeMutexDebug(gSDCardData.wMutex, "unmount_flush");
                        gSDCardData.totalBytesFlushPending = 0;
                        xSemaphoreGive(gSDCardData.wMutex);
                        LOG_D("[SD] Flushed pending data before unmount\r\n");
                    } else {
                        LOG_E("[%s:%d]Error flushing before unmount", __FILE__, __LINE__);
                    }
                }

                LOG_D("[SD] Closing file '%s'\r\n", gSDCardData.filePath);
                if (SYS_FS_FileClose(gSDCardData.fileHandle) == SYS_FS_RES_FAILURE) {
                    LOG_E("[SD] Failed to close file during unmount: '%s', error=%d", gSDCardData.filePath, SYS_FS_Error());
                }
                gSDCardData.fileHandle = SYS_FS_HANDLE_INVALID;
            }
            if (SYS_FS_Unmount(SD_CARD_MANAGER_DISK_MOUNT_NAME) == 0) {
                gSDCardData.discMounted = false;
                gLoggedUnmountFail = false;
                LOG_D("[SD] Unmounted successfully\r\n");
            }
            if (gSDCardData.discMounted == true) {
                /* #603: retrying "untill success" wedged the manager forever
                 * when the card was physically removed while mounted - the
                 * unmount can never succeed, IsBusy() stays true, and every
                 * SD SCPI (including ENAble 0) rejects until reboot. Bound
                 * the retries, and skip them entirely when the card is
                 * confirmed absent; force-clear our mount state so the
                 * manager returns to idle (next insertion mounts fresh). */
                extern bool DRV_SDSPI_IsCardAttached(SYS_MODULE_OBJ object);
                gSDCardData.unmountRetryCount++;
                if (!DRV_SDSPI_IsCardAttached(0) ||
                    (gSDCardData.unmountRetryCount >= SD_UNMOUNT_MAX_RETRIES)) {
                    LOG_E("[SD] Unmount unrecoverable (card %s, %u attempts) - forcing state clear",
                          DRV_SDSPI_IsCardAttached(0) ? "present" : "absent",
                          (unsigned)gSDCardData.unmountRetryCount);
                    gSDCardData.discMounted = false;
                    gSDCardData.unmountRetryCount = 0;
                    gLoggedUnmountFail = false;
                    /* fall through to the cleanup branch below on next pass */
                    break;
                }
                if (!gLoggedUnmountFail) {
                    gLoggedUnmountFail = true;
                    LOG_E("[SD] Unmount failed, retrying");
                }
                vTaskDelay(pdMS_TO_TICKS(SD_UNMOUNT_RETRY_DELAY_MS));
                gSDCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_UNMOUNT_DISK;
            } else {
                gSDCardData.unmountRetryCount = 0;
                // Reset file splitting state for next session
                /* #689: curBucket/bucketPath are left alone here, and that is
                 * harmless rather than deliberate -- resetting fileCounter is
                 * what matters, because the next session's first open takes the
                 * fileCounter==0 branch and re-enters bucket 0, re-establishing
                 * both.
                 *
                 * An earlier revision DID persist the cursor on purpose, to
                 * resume where the last session stopped instead of rescanning
                 * full buckets. That was removed: it survived a card swap or a
                 * PC-side reformat, it ignored space freed by deleting files,
                 * and it stopped the same base filename from overwriting. Do
                 * not reintroduce it without re-reading those three cases. */
                gSDCardData.fileCounter = 0;
                gSDCardData.currentFileBytes = 0;
                memset(gSDCardData.baseFilename, 0, sizeof(gSDCardData.baseFilename));
                gSDCardData.fileSplittingEnabled = false;
                LOG_D("[SD] File splitting state reset for next session\r\n");

                // Always go back to INIT after unmounting
                // INIT will decide whether to mount based on enable flag and mode
                gSDCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_INIT;
            }
            break;

        case SD_CARD_MANAGER_PROCESS_STATE_CURRENT_DRIVE:
            if (SYS_FS_CurrentDriveSet(SD_CARD_MANAGER_DISK_MOUNT_NAME) == SYS_FS_RES_FAILURE) {
                /* Error while setting current drive */
                LOG_E("[%s:%d]Error Setting SD Card drive", __FILE__, __LINE__);
                gSDCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_ERROR;
            } else if (gpSDCardSettings->mode == SD_CARD_MANAGER_MODE_GET_SPACE) {
                gSDCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_GET_SPACE;
            } else if (gpSDCardSettings->mode == SD_CARD_MANAGER_MODE_WRITE) {
                /* #503: always clear startupDiskFull at the start of any
                 * WRITE attempt — both the check-skipped (floor == 0) and
                 * check-runs (floor > 0) paths must enter with a clean
                 * flag, otherwise a stale `true` from a previous
                 * rejection would mislabel a subsequent generic open
                 * failure as "disk full" in the SCPI log.  Also invalidate
                 * any cached space-query result from a prior op so
                 * SYST:STOR:SD:SPACe? doesn't surface a stale free-space
                 * value when the floor-check is bypassed (floor==0).
                 * Snapshot minFreeBytes under a critical section per
                 * CLAUDE.md atomicity rules (64-bit read shared with the
                 * SCPI setter that writes under taskENTER_CRITICAL). */
                /* Reset 64-bit cached space fields under a single critical
                 * section so concurrent SCPI readers (e.g. SYST:STOR:SD:
                 * SPACe? from USB pri 7) can't observe a torn intermediate
                 * value while the bool + two uint64 stores happen on a
                 * 32-bit bus.  Same critical section captures the
                 * minFreeBytes snapshot — pairs with the SCPI setter's
                 * critical-section write. */
                uint64_t floor;
                taskENTER_CRITICAL();
                gSDCardData.startupDiskFull = false;
                gSDCardData.spaceResultValid = false;
                gSDCardData.spaceResultFreeBytes = 0;
                gSDCardData.spaceResultTotalBytes = 0;
                floor = gpSDCardSettings->minFreeBytes;
                taskEXIT_CRITICAL();
                if (floor > 0) {
                    /* Consolidate disk-full check into WRITE init so a
                     * single UpdateSettings(WRITE) does one
                     * mount+check+open cycle instead of two. */
                    gSDCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_CHECK_DISK_FULL;
                } else {
                    gSDCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_CREATE_DIRECTORY;
                }
            } else {
                /* READ/LIST/DELETE path: skip the floor check and
                 * proceed to file open. */
                gSDCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_CREATE_DIRECTORY;
            }
            break;

        case SD_CARD_MANAGER_PROCESS_STATE_CHECK_DISK_FULL:
        {
            /* #503: in-line disk-full check that replaces the prior
             * SCPI-side GET_SPACE preflight.  Same SYS_FS API as the
             * GET_SPACE state — only invoked when minFreeBytes > 0.
             * startupDiskFull was already cleared in CURRENT_DRIVE on
             * the WRITE branch (above). */
            uint32_t totalSectors = 0;
            uint32_t freeSectors = 0;
            if (SYS_FS_DriveSectorGet(SD_CARD_MANAGER_DISK_MOUNT_NAME, &totalSectors, &freeSectors) != SYS_FS_RES_SUCCESS) {
                LOG_E("[SD] STR:START disk-full preflight: SYS_FS_DriveSectorGet failed; continuing without check");
                /* Query failed — fall through to normal open path so the
                 * caller still sees a real WRITE attempt (not a silent
                 * disk-full rejection on a transient mount glitch). */
                gSDCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_CREATE_DIRECTORY;
                break;
            }
            uint64_t freeBytes = (uint64_t)freeSectors * SD_SECTOR_SIZE_BYTES;
            uint64_t totalBytes = (uint64_t)totalSectors * SD_SECTOR_SIZE_BYTES;
            /* Single critical section covers all the cross-task state
             * touched here:
             *   - Snapshot the 64-bit floor (CLAUDE.md atomicity — SCPI
             *     setter writes under taskENTER_CRITICAL).
             *   - Publish the 64-bit space cache (free/total/valid) so a
             *     concurrent reader sees the new triple coherently
             *     instead of half-published intermediate state.
             * Cached BEFORE the reject branch so the SCPI side's
             * friendly "disk full" log reads the real values via
             * sd_card_manager_GetSpaceInfo() regardless of pass/fail. */
            uint64_t floor;
            taskENTER_CRITICAL();
            floor = gpSDCardSettings->minFreeBytes;
            gSDCardData.spaceResultFreeBytes = freeBytes;
            gSDCardData.spaceResultTotalBytes = totalBytes;
            gSDCardData.spaceResultValid = true;
            taskEXIT_CRITICAL();
            if (freeBytes < floor) {
                LOG_E("[SD] STR:START refused: %llu B free < %llu B floor",
                      (unsigned long long)freeBytes,
                      (unsigned long long)floor);
                gSDCardData.startupDiskFull = true;
                gSDCardData.lastOperationSuccess = false;
                gpSDCardSettings->mode = SD_CARD_MANAGER_MODE_NONE;
                gSDCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_ERROR;
                break;
            }
            gSDCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_CREATE_DIRECTORY;
        }
            break;

        case SD_CARD_MANAGER_PROCESS_STATE_CREATE_DIRECTORY:
            /* #797: only a WRITE may create the directory. Every mode routes
             * through this state, so a read-only operation used to CREATE the
             * thing it had been asked to look at -- `SD:LISt? "TYPO"` made
             * TYPO, then truthfully reported it empty, and left it on the
             * card. Two bugs in one: a query that modifies the card, and a
             * host that cannot tell "that directory is empty" from "that
             * directory is not there", because the firmware had just made the
             * second answer into the first.
             *
             * With the create gone, DirOpen fails for a path that is not
             * there and the walk reports SD_LISTDIR_FAILED -- `I could not
             * look`, which is what #796's FAILED marker already means. A
             * WRITE still creates its target directory, so the first stream
             * after a format works exactly as before. */
            /* #797: only a WRITE may create the directory. Every mode routes
             * through this state, so a read-only operation used to CREATE the
             * thing it had been asked to look at -- `SD:LISt? "TYPO"` made
             * TYPO, then truthfully reported it empty, and left it on the
             * card. Two bugs in one: a query that modifies the card, and a
             * host that cannot tell "that directory is empty" from "that
             * directory is not there", because the firmware had just made the
             * second answer into the first.
             *
             * With the create gone, DirOpen fails for a path that is not there
             * and the walk reports SD_LISTDIR_FAILED -- `I could not look`,
             * which is what #796's FAILED marker already means. A WRITE still
             * creates its target directory, so the first stream after a format
             * works exactly as before.
             *
             * The state stores below are plain, like the other ~56 in this
             * function: #800's gSdTeardownRequested flag, consumed once per
             * iteration at the top, is what keeps a teardown from being
             * clobbered. An earlier revision of this PR guarded each store
             * here with its own critical section, which duplicated that
             * mechanism using precisely the per-site approach #800 argued
             * against.
             *
             * The critical section that DOES remain covers only the snapshot,
             * and for a different hazard: sd_card_manager_UpdateSettings()
             * memcpy's the WHOLE settings struct from SCPI (pri 7), so reading
             * the mode and the name separately -- or handing FatFs a pointer
             * into the live struct across a filesystem call -- can create a
             * directory under a torn name. Read both once, together. */
            {
                sd_card_manager_mode_t dirMode;
                char dirName[SD_CARD_MANAGER_CONF_DIR_NAME_LEN_MAX + 1];

                taskENTER_CRITICAL();
                dirMode = gpSDCardSettings->mode;
                (void)strncpy(dirName, gpSDCardSettings->directory,
                              sizeof(dirName) - 1u);
                dirName[sizeof(dirName) - 1u] = '\0';
                taskEXIT_CRITICAL();

                if (dirMode != SD_CARD_MANAGER_MODE_WRITE) {
                    LOG_D("[SD] Not creating '%s': mode %d is read-only\r\n",
                          dirName, (int)dirMode);
                    gSDCardData.currentProcessState =
                            SD_CARD_MANAGER_PROCESS_STATE_OPEN_FILE;
                    break;
                }

                if (SYS_FS_DirectoryMake(dirName) == SYS_FS_RES_FAILURE) {
                    /* Read the error ONCE. SYS_FS_Error() returns a shared
                     * global, so calling it again for the log can report a
                     * different fault than the one that selected the branch --
                     * worse than no code at all, because it looks
                     * authoritative. */
                    SYS_FS_ERROR mkdirErr = SYS_FS_Error();

                    if (mkdirErr == SYS_FS_ERROR_EXIST) {
                        LOG_D("[SD] Directory '%s' already exists\r\n", dirName);
                        gSDCardData.currentProcessState =
                                SD_CARD_MANAGER_PROCESS_STATE_OPEN_FILE;
                    } else {
                        /* This branch is reached for EVERY DirectoryMake
                         * failure that is not EXIST -- media faults, a full
                         * root directory, an I/O error -- so the old
                         * "Invalid SD Card Directory name" sent operators
                         * after a naming problem that usually is not there. */
                        gSDCardData.currentProcessState =
                                SD_CARD_MANAGER_PROCESS_STATE_ERROR;
                        LOG_E("[%s:%d]SD DirectoryMake('%s') failed, SYS_FS_Error=%d",
                              __FILE__, __LINE__, dirName, (int)mkdirErr);
                    }
                } else {
                    LOG_D("[SD] Created directory '%s'\r\n", dirName);
                    gSDCardData.currentProcessState =
                            SD_CARD_MANAGER_PROCESS_STATE_OPEN_FILE;
                }
            }
            break;

        case SD_CARD_MANAGER_PROCESS_STATE_OPEN_FILE:
            memset(gSDCardData.filePath, 0, sizeof (gSDCardData.filePath));
            LOG_D("[SD] Opening file, mode=%d\r\n", gpSDCardSettings->mode);
            if (gpSDCardSettings->mode == SD_CARD_MANAGER_MODE_WRITE) {
                // Initialize file splitting if enabled
                gSDCardData.fileSplittingEnabled = (gpSDCardSettings->maxFileSizeBytes > 0);

                // Extract base filename and generate actual filename with counter
                bool bucketOk = true;
                if (gSDCardData.fileCounter == 0) {
                    // First time opening - extract base filename
                    extractBaseFilename(gpSDCardSettings->file);
                    /* #689: ALWAYS scan from bucket 0. An earlier revision
                     * persisted a cursor across sessions to avoid re-walking
                     * buckets already passed, and that optimisation produced
                     * three separate defects: it survived a card swap or a
                     * PC-side reformat (first session on a fresh card landed in
                     * P0xx instead of the configured directory); it ignored
                     * space freed by deleting older files, refusing with "every
                     * bucket is full" while a writable one existed; and it made
                     * the same base filename stop overwriting, so repeated runs
                     * accumulated duplicate part names across buckets.
                     *
                     * The scan it avoided is cheap and bounded: at most
                     * MAX_BUCKET+1 CountDirEntries calls, each itself bounded to
                     * MAX_DIR_FILES entries -- a few hundred milliseconds once
                     * per session start, against sessions that run for minutes
                     * to hours. Paying it buys a cursor that cannot be stale,
                     * which is worth far more than the time it saves. */
                    bucketOk = sd_EnterBucket(gpSDCardSettings->directory, 0u);
                }

                // #689: roll into the next bucket while the active one is at its
                // ceiling, so FatFs never sees a large directory to scan. This
                // replaces the old hard refuse, which merely converted the wedge
                // into a dead stop: a long split session now keeps logging into
                // P001, P002, ... instead of ending at 64 files.
                //
                // A `while` rather than an `if` because the bucket we land in may
                // itself be populated from an earlier session. It is bounded on
                // BOTH axes — the highest bucket index, and how many we may skip
                // in a single open — because unbounded synchronous FS work inside
                // the SD task is the failure mode this issue is about.
                /* Re-opening a file that ALREADY EXISTS in this bucket adds no
                 * directory entry, so it must not be treated as a new one.
                 *
                 * STR:START opens the file once to prove readiness, then
                 * PrepareStreamingBuffers closes it and re-opens for the actual
                 * stream. With the directory at exactly MAX-1 entries, that
                 * first open filled the bucket, and the re-open then rolled the
                 * live stream into P001 -- leaving a zero-byte file at the path
                 * the caller configured while the samples went elsewhere.
                 *
                 * Cheaper and more precise than tracking the pre-open: ask the
                 * filesystem whether the exact target is already there.
                 *
                 * The search spans buckets, forward from the ACTIVE one -- a
                 * session only ever moves forward -- and stops at the first
                 * ABSENT bucket, because buckets are created in ascending
                 * order. A fresh card therefore costs one directory stat.
                 *
                 * Stopping at the first gap is a DELIBERATE bound, not an
                 * oversight. Removing it means stat-ing all MAX_BUCKET+1
                 * buckets on EVERY open -- reintroducing exactly the
                 * O(directory) synchronous FS work inside the SD task that
                 * this issue exists to remove. Nothing in this firmware can
                 * create a gap (buckets are made in ascending order and only
                 * files are deleted), so reaching that state needs a whole
                 * P0xx directory removed on a PC while a later one survives,
                 * and the consequence is bounded: one extra copy of one part
                 * name, nothing overwritten.
                 *
                 * It must NOT be folded into the roll below, which was tried
                 * and is wrong: the roll only advances while a bucket is FULL,
                 * so it never runs when the active bucket has room -- and a
                 * bucket can have room while a LATER one still holds this
                 * part, e.g. after files are deleted from it PC-side. The roll
                 * would then skip the search entirely and create a duplicate
                 * part alongside the surviving original. Searching here covers
                 * both shapes with one mechanism; a newly created bucket is
                 * empty, so nothing the roll creates can hold the part. */
                bool reopenExisting = false;
                if (bucketOk) {
                    uint32_t reuseBucket = gSDCardData.curBucket;
                    char probePath[SD_CARD_MANAGER_FILE_PATH_LEN_MAX + 1];
                    for (uint32_t probe = gSDCardData.curBucket;
                         probe <= SD_CARD_MANAGER_MAX_BUCKET; probe++) {
                        if (!sd_BuildBucketPath(probePath, sizeof(probePath),
                                                gpSDCardSettings->directory,
                                                probe)) {
                            break;
                        }
                        if (probe != gSDCardData.curBucket) {
                            bool probeFsError = false;
                            bool present = sd_BucketDirExists(probePath,
                                                              &probeFsError);
                            if (probeFsError) {
                                /* An unreadable bucket is NOT an absent one.
                                 * Ending the search here would create a
                                 * duplicate part on a card that is merely
                                 * failing, so refuse instead -- the same
                                 * fail-safe sd_EnterBucket applies to an
                                 * unreadable count. */
                                gSDCardData.writeRefuseReason =
                                        SD_REFUSE_BUCKET_UNREADABLE;
                                bucketOk = false;
                                break;
                            }
                            if (!present) {
                                break;
                            }
                        }
                        bool targetFsError = false;
                        bool found = sd_TargetExistsInBucketPath(probePath,
                                                                 &targetFsError);
                        if (targetFsError) {
                            gSDCardData.writeRefuseReason =
                                    SD_REFUSE_BUCKET_UNREADABLE;
                            bucketOk = false;
                            break;
                        }
                        if (found) {
                            reopenExisting = true;
                            reuseBucket = probe;
                            break;
                        }
                    }
                    /* Only re-enter when the part lives elsewhere: sd_EnterBucket
                     * re-counts and zeroes filesInCurBucket, so calling it for
                     * the bucket we are already in would forget the files this
                     * session has added and overshoot the per-bucket ceiling. */
                    if (reopenExisting && reuseBucket != gSDCardData.curBucket) {
                        bucketOk = sd_EnterBucket(gpSDCardSettings->directory,
                                                  reuseBucket);
                        reopenExisting = bucketOk;
                    }
                    /* The pre-walk borrowed filePath as scratch (see
                     * sd_TargetExistsInBucketPath). Hand it back EMPTY rather
                     * than holding the last probe path.
                     *
                     * Nothing reads it before generateFilename rewrites it
                     * below -- that invariant is why borrowing is safe at all
                     * -- but a later edit that adds a log or branch in between
                     * would silently pick up a plausible-looking WRONG path.
                     * Clearing costs one store and turns that from a silent
                     * wrong-file bug into an obviously empty one. */
                    gSDCardData.filePath[0] = '\0';
                }

                uint32_t advanced = 0u;
                while (bucketOk && !reopenExisting &&
                       (gSDCardData.bucketFileCountAtStart +
                        gSDCardData.filesInCurBucket) >= SD_CARD_MANAGER_MAX_DIR_FILES) {
                    if (gSDCardData.curBucket >= SD_CARD_MANAGER_MAX_BUCKET ||
                        advanced >= SD_CARD_MANAGER_BUCKET_ADVANCE_MAX) {
                        /* Genuinely out of buckets: the scan started at 0
                         * this session, so there is nothing behind us to find. */
                        gSDCardData.writeRefuseReason = SD_REFUSE_BUCKETS_EXHAUSTED;
                        bucketOk = false;
                        break;
                    }
                    bucketOk = sd_EnterBucket(gpSDCardSettings->directory,
                                              gSDCardData.curBucket + 1u);
                    advanced++;
                }

                // Buckets exhausted (or a bucket could not be created). Clean-stop
                // to IDLE with startupDirFull set, so SCPI_StartStreaming reports a
                // precise error rather than a silent wedge — mirrors the #503
                // disk-full clean-stop pattern.
                if (!bucketOk) {
                    LOG_E("[SD] WRITE refused: no writable bucket under '%s' "
                          "(active '%s', bucket %u, %u per bucket, max %u) - FatFs "
                          "file-create wedges large directories (#689). Use a "
                          "larger SD:MAXSize, a different directory, or clear the "
                          "card.", gpSDCardSettings->directory,
                          gSDCardData.bucketPath, (unsigned)gSDCardData.curBucket,
                          (unsigned)SD_CARD_MANAGER_MAX_DIR_FILES,
                          (unsigned)SD_CARD_MANAGER_MAX_BUCKET);
                    gSDCardData.startupDirFull = true;
                    gSDCardData.lastOperationSuccess = false;
                    if (gSDCardData.fileHandle != SYS_FS_HANDLE_INVALID) {
                        (void)SYS_FS_FileClose(gSDCardData.fileHandle);
                        gSDCardData.fileHandle = SYS_FS_HANDLE_INVALID;
                    }
                    gpSDCardSettings->mode = SD_CARD_MANAGER_MODE_NONE;
                    gSDCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_IDLE;
                    /* #757: this exits OPEN_FILE without ever reaching the
                     * open, so the rotation window has to be closed here too --
                     * otherwise the encoder keeps filling a buffer that nothing
                     * will drain. */
                    sd_AbandonRotationWindow("no writable bucket");
                    xSemaphoreGive(gSDCardData.opCompleteSemaphore);
                    break;
                }

                // #689: create into the ACTIVE BUCKET, not the raw configured
                // directory. For bucket 0 these are the same string, which is why
                // short sessions keep their existing on-card layout exactly.
                generateFilename(gSDCardData.filePath, sizeof(gSDCardData.filePath),
                               gSDCardData.fileCounter, gSDCardData.bucketPath,
                               gSDCardData.baseFilename, gpSDCardSettings->file);
                // Count the attempt, not the success: a failed create still costs
                // a directory entry scan, and over-counting only rolls us to a
                // fresh bucket sooner, which is the safe direction.
                /* A re-open of an existing target adds no directory entry, so
                 * it must not be counted -- the same reason the roll above skips
                 * it. Counting it inflated the estimate by one per session on
                 * the STR:START readiness path. */
                if (!reopenExisting) {
                    gSDCardData.filesInCurBucket++;
                }

                LOG_D("[SD] Opening file '%s' (counter=%u, splitting=%s)\r\n",
                     gSDCardData.filePath, gSDCardData.fileCounter,
                     gSDCardData.fileSplittingEnabled ? "enabled" : "disabled");

                // Reset SD metadata flags FIRST, before clearing the buffer.
                // This sets gSdFileWasReady = false, which causes the
                // streaming task's SD write guard (hasSD && gSdFileWasReady)
                // to fail — preventing non-metadata data from being written
                // to the freshly-cleared buffer before the new file is ready.
                //
                // Race condition without this ordering:
                //   1. CircularBuf_Reset() clears buffer (space available)
                //   2. Streaming task sees gSdFileWasReady=true (stale),
                //      encodes WITHOUT metadata, writes to buffer
                //   3. Streaming_ResetSdPbMetadata() resets flags (too late)
                //   => Non-metadata data at byte 0 of new file
                /* #757: skipped during a rotation -- the close path above
                 * already reset the metadata and the buffer, and the encoder
                 * has been filling that buffer with the new file's header and
                 * data ever since. Repeating the reset here would throw away
                 * exactly the bytes this fix exists to keep. On a FIRST open
                 * (session start, not rotation) gSdRotating is false and this
                 * runs as it always did. */
                if (!gSdRotating) {
                    Streaming_ResetSdPbMetadata();

                    // Now clear the buffer — streaming task won't write here
                    // because gSdFileWasReady is already false.
                /* #757: reset the buffer's BOOKKEEPING, not its bytes.
                 *
                 * Neither zero was load-bearing: nothing reads either buffer
                 * past a length that is reset alongside it. CircularBuf_Reset
                 * already makes the circular buffer logically empty (head,
                 * tail, count) and no reader looks beyond count; the write
                 * buffer is only ever emitted as its first writeBufferLength
                 * bytes, reset a few lines below -- and the comment there
                 * records that the "junk bytes in the new file" bug was fixed
                 * by resetting sdCardWritePending/writeBufferLength, NOT by
                 * this memset.
                 *
                 * The cost was real: writeBuffer is the COHERENT (KSEG1,
                 * uncached) DMA buffer, ~78 KB when SD is the active
                 * interface, so this was an uncached write of every byte with
                 * no cache-line benefit -- inside the very window this issue
                 * is about. Measured ~4% of the loss on its own.
                 *
                 * The mutex is still taken: CircularBuf_Reset mutates state
                 * the streaming task reads. */
                    SD_TakeMutexDebug(gSDCardData.wMutex, "open_file_clear_buffer");
                    CircularBuf_Reset(&gSDCardData.wCirbuf);
                    xSemaphoreGive(gSDCardData.wMutex);
                }

                // Reset write pipeline state for clean start.
                // Without this, a stale sdCardWritePending=1 from a previous
                // session causes the zeroed writeBuffer to be written to the
                // new file (producing junk bytes before real data).
                gSDCardData.sdCardWritePending = 0;
                gSDCardData.writeBufferLength = 0;
                gSDCardData.sdCardWriteBufferOffset = 0;

                // Use WRITE_PLUS to create/truncate file (overwrite mode)
                gSDCardData.fileHandle = SYS_FS_FileOpen(gSDCardData.filePath,
                        (SYS_FS_FILE_OPEN_WRITE_PLUS));

                /* #782: a teardown may have landed while this open was in
                 * flight. SCPI (pri 7) preempts this task (pri 5), and
                 * sd_card_manager_UpdateSettings() tears the session down by
                 * clearing mode and forcing currentProcessState = DEINIT. An
                 * unconditional assignment here overwrites that DEINIT, and
                 * the session is then stranded in WRITE_TO_FILE with
                 * mode == NONE: nothing re-arms a write, so IsBusy() stays
                 * true and every SD command is refused until something
                 * re-initialises the manager. Rotation made this likely
                 * because it re-enters OPEN_FILE roughly every MAXSize bytes.
                 * Re-check the mode we were dispatched on and honour the
                 * teardown instead of clobbering it.
                 *
                 * The check and the state write must be ONE atomic step. A
                 * plain "if (mode != WRITE) ... else state = WRITE_TO_FILE"
                 * only narrows the window: SCPI can still land between the
                 * comparison and the assignment, and the assignment then
                 * clobbers the DEINIT exactly as before. Both operands are
                 * 32-bit and individually atomic on PIC32MZ, but this is a
                 * read-decide-write across two variables, which is the case
                 * the project's atomicity rules reserve a critical section
                 * for. It spans one compare and one store; the file close and
                 * the log stay outside it. */
                bool openAborted;
                taskENTER_CRITICAL();
                openAborted = (gpSDCardSettings->mode
                               != SD_CARD_MANAGER_MODE_WRITE);
                gSDCardData.currentProcessState = openAborted
                        ? SD_CARD_MANAGER_PROCESS_STATE_DEINIT
                        : SD_CARD_MANAGER_PROCESS_STATE_WRITE_TO_FILE;
                /* #757: close the window in the SAME critical section that
                 * opens WRITE_TO_FILE, so the handoff is atomic.
                 *
                 * Clearing it any earlier reopens this issue in miniature.
                 * Between a clear and the state assignment, gSdRotating is
                 * false while currentProcessState is still OPEN_FILE, so
                 * IsWriteReady() is false too and IsBufferAccepting() returns
                 * FALSE -- the encoder (pri 6) preempts this task (pri 5)
                 * right there and its packets are discarded. That gap was
                 * measurable: it is where the last few stray SdDroppedBytes in
                 * an otherwise clean rotation run came from.
                 *
                 * Only the success arm clears. On abort the flag stays set
                 * until sd_AbandonRotationWindow() below clears it AND
                 * accounts for what was buffered -- clearing it here would
                 * silently strand those bytes instead. */
                if (!openAborted) {
                    gSdRotating = false;
                }
                taskEXIT_CRITICAL();

                if (openAborted) {
                    /* Deliberately do NOT close here. DEINIT -> UNMOUNT_DISK
                     * runs next and is the single owner of closing this
                     * handle: it drains, closes, and invalidates in one
                     * place. Closing here meant reasoning about UNMOUNT's
                     * gate (it skips its whole block when the handle is
                     * already INVALID), which is what produced two separate
                     * defects in review -- a discarded close result, then an
                     * invalidation that suppressed UNMOUNT's retry.
                     *
                     * Note this is about single ownership, not data
                     * recovery.
                     *
                     * #757 CHANGED THE SECOND HALF OF THAT REASONING. This
                     * comment used to say nothing could be pending, because
                     * the abort path never sets WRITE_TO_FILE so
                     * IsWriteReady() stays false and the streaming task
                     * cannot have written. That is no longer true: across a
                     * rotation the encoder writes on IsBufferAccepting(),
                     * which IS true during the open, so the buffer can hold
                     * the new file's header and some samples by the time we
                     * get here.
                     *
                     * Those bytes are unrecoverable -- the session is being
                     * torn down and there is no handle to write them to -- but
                     * they must not vanish silently. Count them as SD drops so
                     * they appear in SdDroppedBytes like any other SD loss,
                     * and reset the buffer so the next session starts clean
                     * rather than inheriting a partial file's header. */
                    sd_AbandonRotationWindow("session torn down mid-open");
                    LOG_I("[SD] open aborted: session torn down mid-open "
                          "(mode=%s)", sd_card_manager_GetModeName());
                    break;
                }

                /* State is already WRITE_TO_FILE from the block above --
                 * IsWriteReady() becomes true at that point, and the
                 * streaming task detects the transition and writes SD-only
                 * headers at byte 0. */
                gSDCardData.totalBytesFlushPending = 0;
                gSDCardData.currentFileBytes = 0;  // Reset byte counter for new file
                gSDCardData.lastFlushMillis = pdTICKS_TO_MS(xTaskGetTickCount());

                if (gSDCardData.fileHandle == SYS_FS_HANDLE_INVALID) {
                    /* Could not open the file. Error out*/
                    /* #757: nothing will ever drain what the encoder buffered
                     * during this open, so account for it instead of leaving
                     * it to be silently overwritten by the next session. */
                    sd_AbandonRotationWindow("file open failed");
                    gSDCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_ERROR;
                    LOG_E("[%s:%d]Failed to open SD Card file for writing: '%s'", __FILE__, __LINE__, gSDCardData.filePath);
                }
            } else if (gpSDCardSettings->mode == SD_CARD_MANAGER_MODE_READ ||
                       gpSDCardSettings->mode == SD_CARD_MANAGER_MODE_COMPUTE_CRC) {
                // READ/CRC: construct filename directly from settings (no splitting).
                // #724: open the transient operand (opFile), not the logging target.
                snprintf(gSDCardData.filePath, SD_CARD_MANAGER_FILE_PATH_LEN_MAX, "%s/%s",
                        gpSDCardSettings->directory, gpSDCardSettings->opFile);
                LOG_D("[SD] Opening file for read: '%s'\r\n", gSDCardData.filePath);

                gSDCardData.fileHandle = SYS_FS_FileOpen(gSDCardData.filePath,
                        (SYS_FS_FILE_OPEN_READ));
                if (gpSDCardSettings->mode == SD_CARD_MANAGER_MODE_COMPUTE_CRC) {
                    /* #306: invalidate prior result, arm the accumulator */
                    taskENTER_CRITICAL();
                    gSDCardData.crcResultValid = false;
                    taskEXIT_CRITICAL();
                    gSDCardData.crcRunning = CRC32_Init();
                    gSDCardData.crcLength = 0;
                    gSDCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_COMPUTE_CRC;
                } else {
                    gSDCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_READ_FROM_FILE;
                }

                if (gSDCardData.fileHandle == SYS_FS_HANDLE_INVALID) {
                    /* Could not open the file. Error out*/
                    /* #306 fix (wedge): reset mode so this op is terminal.
                     * This branch is shared by READ and COMPUTE_CRC; on a
                     * missing/unopenable file the INIT guard
                     * ((enable || GET_SPACE) && mode != NONE) would otherwise
                     * re-arm the op every ERROR->UNMOUNT->INIT cycle, wedging
                     * the SD subsystem (IsBusy() stuck true) until reboot.
                     * Mirrors the CRC read-error terminal path. Fixes the
                     * SYST:STOR:SD:CRC "missing" wedge and the pre-existing
                     * latent SD:GET open-fail wedge. */
                    /* #703: for a READ (SD:GET) the host is blocked waiting for
                     * the file bytes + __END_OF_FILE__ terminator; without a
                     * terminator it hangs on a missing/unopenable file. Emit the
                     * marker so the GET terminates as an empty transfer. (CRC has
                     * no streamed terminator — its result is queried via SD:CRC?
                     * — so only send it for READ.) */
                    if (gpSDCardSettings->mode == SD_CARD_MANAGER_MODE_READ) {
                        sd_card_manager_DataReadyCB(SD_CARD_MANAGER_MODE_READ,
                                (uint8_t*)"__END_OF_FILE__",
                                sizeof("__END_OF_FILE__") - 1);
                    }
                    gpSDCardSettings->mode = SD_CARD_MANAGER_MODE_NONE;
                    gSDCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_ERROR;
                    LOG_E("[%s:%d]Failed to open SD Card file for reading: '%s'", __FILE__, __LINE__, gSDCardData.filePath);
                }
            } else if (gpSDCardSettings->mode == SD_CARD_MANAGER_MODE_LIST_DIRECTORY) {
                // LIST mode doesn't need to open a file, just list the directory
                gSDCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_LIST_DIR;
            } else if (gpSDCardSettings->mode == SD_CARD_MANAGER_MODE_DELETE_FILE) {
                // DELETE mode - construct file path and delete the specified file
                // Initialize to false to prevent reading stale success from previous operation
                gSDCardData.lastOperationSuccess = false;
                // Strip trailing slash from directory to avoid double slashes
                size_t dirLen = strlen(gpSDCardSettings->directory);
                while (dirLen > 0 && gpSDCardSettings->directory[dirLen - 1] == '/') {
                    dirLen--;
                }
                // #724: delete the transient operand (opFile), not the logging target.
                int pathLen = snprintf(gSDCardData.filePath, SD_CARD_MANAGER_FILE_PATH_LEN_MAX, "%.*s/%s",
                        (int)dirLen, gpSDCardSettings->directory, gpSDCardSettings->opFile);

                // Validate path was not truncated
                if (pathLen < 0 || pathLen >= (int)SD_CARD_MANAGER_FILE_PATH_LEN_MAX) {
                    LOG_E("[SD] Delete path too long (need %d, max %d)\r\n", pathLen, SD_CARD_MANAGER_FILE_PATH_LEN_MAX - 1);
                    gSDCardData.lastOperationSuccess = false;
                    gpSDCardSettings->mode = SD_CARD_MANAGER_MODE_NONE;
                    gSDCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_IDLE;
                    xSemaphoreGive(gSDCardData.opCompleteSemaphore);
                    break;
                }

                // Reject path traversal attempts (e.g., "../" or "/..")
                if (strstr(gSDCardData.filePath, "..") != NULL) {
                    LOG_E("[SD] Delete path rejected (traversal attempt): '%s'\r\n", gSDCardData.filePath);
                    gSDCardData.lastOperationSuccess = false;
                    gpSDCardSettings->mode = SD_CARD_MANAGER_MODE_NONE;
                    gSDCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_IDLE;
                    xSemaphoreGive(gSDCardData.opCompleteSemaphore);
                    break;
                }

                LOG_D("[SD] Preparing to delete file: '%s'\r\n", gSDCardData.filePath);
                gSDCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_DELETE_FILE;
            } else if (gpSDCardSettings->mode == SD_CARD_MANAGER_MODE_FORMAT) {
                // FORMAT mode - erase all files on SD card
                gSDCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_FORMAT;
            } else if (gpSDCardSettings->mode == SD_CARD_MANAGER_MODE_NONE) {
                gSDCardData.fileHandle = SYS_FS_FileOpen(gSDCardData.filePath,
                        (SYS_FS_FILE_OPEN_READ));
                gSDCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_IDLE;

                if (gSDCardData.fileHandle == SYS_FS_HANDLE_INVALID) {
                    /* Could not open the file. Error out*/
                    gSDCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_ERROR;
                    LOG_E("[%s:%d]Failed to open SD Card file: '%s'", __FILE__, __LINE__, gSDCardData.filePath);
                }
            }
            break;
        case SD_CARD_MANAGER_PROCESS_STATE_WRITE_TO_FILE:
        {
            /* #782: this state had no exit for a cleared mode. Its only
             * transition to IDLE is the split-limit path below, which needs
             * writes that will never arrive once the session is torn down --
             * so a teardown that landed here left the manager busy forever,
             * refusing every SD command until something re-initialised it.
             *
             * The guard in OPEN_FILE should keep us out of that window, but
             * this is the state that has to be survivable: it is reachable
             * from any future caller that clears the mode, and being wrong
             * here is permanent rather than momentary.
             *
             * Route to DEINIT (-> UNMOUNT_DISK) rather than draining here:
             * that path already flushes the pending write buffer AND the
             * circular buffer before closing the file, without the
             * sector-alignment the steady-state loop uses, which is exactly
             * what a stop needs to avoid truncating a sub-sector tail. */
            if (gpSDCardSettings->mode != SD_CARD_MANAGER_MODE_WRITE) {
                LOG_I("[SD] write session ended (mode=%s) - finalising",
                      sd_card_manager_GetModeName());
                gSDCardData.currentProcessState =
                        SD_CARD_MANAGER_PROCESS_STATE_DEINIT;
                break;
            }

            /* If read was success, try writing to the new file */
            int writeLen = -2;
            
            // Process multiple chunks per cycle for better throughput
            int chunksProcessed = 0;
            
            while (chunksProcessed < SD_CARD_MANAGER_MAX_CHUNKS_PER_CYCLE) {
                SD_TakeMutexDebug(gSDCardData.wMutex, "write_loop_check");
                uint32_t availBytes = CircularBuf_NumBytesAvailable(&gSDCardData.wCirbuf);
                if (availBytes >= SD_SECTOR_SIZE_BYTES
                        && gSDCardData.sdCardWritePending != 1) {
                    /* #738: bound by the buffer we ACTUALLY have, not the
                     * compile-time maximum. writeBufferSize is the runtime
                     * size auto-balance assigned (SD_CARD_MANAGER_CONF_WBUFFER_SIZE
                     * is only the 64 KB ceiling), and it collapses to the
                     * inactive-interface minimum whenever SD is not the active
                     * streaming interface. Extracting the ceiling into a
                     * smaller buffer is refused by CircularBufferToSDWrite, so
                     * the write fails and the data is dropped. Read under
                     * wMutex, which is the mutex sd_card_manager_SetWriteBuffer
                     * takes to change it. */
                    uint32_t wbufCap = gSDCardData.writeBufferSize;
                    uint32_t maxExtract = (availBytes < wbufCap) ? availBytes
                                                                 : wbufCap;
                    maxExtract = (maxExtract / SD_SECTOR_SIZE_BYTES) * SD_SECTOR_SIZE_BYTES;
                    /* Unreachable today: the branch above requires availBytes
                     * >= one sector, and writeBufferSize is floored at 512 (by
                     * PrepareStreamingBuffers, twice, and by the 64 KB init).
                     * Guarded anyway because this loop now DEPENDS on that
                     * floor, and the public setter only rejects size == 0 — a
                     * future caller passing a sub-sector size would truncate to
                     * 0 here, and extracting 0 after setting sdCardWritePending
                     * would leave the state machine waiting on a write that
                     * never had data (Qodo #748). */
                    if (maxExtract == 0) {
                        xSemaphoreGive(gSDCardData.wMutex);
                        LOG_E_ONCE(LOG_ONCE_SD_WBUF_SUBSECTOR,
                                   "[SD] write buffer below one sector - drain stalled");
                        break;
                    }
                    gSDCardData.sdCardWritePending = 1;
                    CircularBuf_ProcessBytes(&gSDCardData.wCirbuf, NULL, maxExtract, &writeLen);
                    gSDCardData.totalBytesFlushPending += gSDCardData.writeBufferLength;
                    xSemaphoreGive(gSDCardData.wMutex);
                    chunksProcessed++;
                } else {
                    // No data available or write already pending, release mutex
                    xSemaphoreGive(gSDCardData.wMutex);
                    
                    // Note: sdCardWritePending check outside mutex is safe here because:
                    // 1. Only this task modifies sdCardWritePending
                    // 2. We're coordinating work within a single task, not between tasks
                    // If this changes in future, this will need mutex protection
                    if (gSDCardData.sdCardWritePending == 1) {
                        writeLen = SDCardWrite();
                        // Check error first: SDCardWrite returns int, but
                        // writeBufferLength is size_t (unsigned). Without this
                        // guard, -1 converts to SIZE_MAX and passes the >= check,
                        // silently discarding the data.
                        if (writeLen < 0) {
                            gSDCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_ERROR;
                            LOG_E("[%s:%d]Error Writing to SD Card", __FILE__, __LINE__);
                            break;
                        } else if ((size_t)writeLen >= gSDCardData.writeBufferLength) {
                            // Track bytes written for file splitting
                            gSDCardData.currentFileBytes += gSDCardData.writeBufferLength;

                            SD_TakeMutexDebug(gSDCardData.wMutex, "write_complete");
                            gSDCardData.sdCardWritePending = 0;
                            gSDCardData.writeBufferLength = 0;
                            gSDCardData.sdCardWriteBufferOffset = 0;
                            xSemaphoreGive(gSDCardData.wMutex);
                        } else if (writeLen > 0) {
                            // Partial write
                            gSDCardData.currentFileBytes += writeLen;

                            SD_TakeMutexDebug(gSDCardData.wMutex, "partial_write");
                            gSDCardData.writeBufferLength -= writeLen;
                            gSDCardData.sdCardWriteBufferOffset += writeLen;
                            xSemaphoreGive(gSDCardData.wMutex);
                            break;  // Partial write, don't process more chunks
                        } else {
                            // writeLen == 0: no progress — error out to avoid infinite retry
                            gSDCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_ERROR;
                            LOG_E("[%s:%d]Zero-byte write to SD Card", __FILE__, __LINE__);
                            break;
                        }
                    } else {
                        break;  // No more data to process
                    }
                }
            }

            // Check if file size limit reached and rotation is needed
            if (gSDCardData.fileSplittingEnabled &&
                gSDCardData.currentFileBytes >= gpSDCardSettings->maxFileSizeBytes) {
                bool rotationDrainErrorLogged = false;
                LOG_D("[SD] File size limit reached (%llu >= %llu), rotating to next file\r\n",
                     gSDCardData.currentFileBytes, gpSDCardSettings->maxFileSizeBytes);

                // Complete any pending write from the chunk processing loop.
                // The loop can exit with sdCardWritePending=1 (4th chunk read
                // but not yet written). Flush it to the OLD file before closing.
                // Loop to handle partial writes (same logic as normal write path).
                while (gSDCardData.sdCardWritePending == 1) {
                    int pendingLen = SDCardWrite();
                    if (pendingLen < 0) {
                        LOG_E("[SD] Error flushing pending write before rotation");
                        gSDCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_ERROR;
                        gSDCardData.sdCardWritePending = 0;
                        gSDCardData.writeBufferLength = 0;
                        gSDCardData.sdCardWriteBufferOffset = 0;
                        break;
                    } else if ((size_t)pendingLen >= gSDCardData.writeBufferLength) {
                        gSDCardData.currentFileBytes += gSDCardData.writeBufferLength;
                        SD_TakeMutexDebug(gSDCardData.wMutex, "rotation_pending_write");
                        gSDCardData.sdCardWritePending = 0;
                        gSDCardData.writeBufferLength = 0;
                        gSDCardData.sdCardWriteBufferOffset = 0;
                        xSemaphoreGive(gSDCardData.wMutex);
                    } else if (pendingLen > 0) {
                        // Partial write — update offset and retry
                        gSDCardData.currentFileBytes += pendingLen;
                        SD_TakeMutexDebug(gSDCardData.wMutex, "rotation_partial_write");
                        gSDCardData.writeBufferLength -= pendingLen;
                        gSDCardData.sdCardWriteBufferOffset += pendingLen;
                        xSemaphoreGive(gSDCardData.wMutex);
                    } else {
                        LOG_E("[SD] Zero-byte write during rotation flush");
                        gSDCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_ERROR;
                        gSDCardData.sdCardWritePending = 0;
                        gSDCardData.writeBufferLength = 0;
                        gSDCardData.sdCardWriteBufferOffset = 0;
                        break;
                    }
                }

                // Drain circular buffer completely before rotation to prevent data loss
                SD_TakeMutexDebug(gSDCardData.wMutex, "drain_buffer_check");
                size_t bufferBytes = CircularBuf_NumBytesAvailable(&gSDCardData.wCirbuf);
                xSemaphoreGive(gSDCardData.wMutex);

                if (bufferBytes > 0) {
                    LOG_D("[SD] Draining %zu bytes from circular buffer before rotation\r\n", bufferBytes);
                    /* #822: drain the SNAPSHOT, not the live level.
                     *
                     * This loop used to run `while NumBytesAvailable() > 0`,
                     * i.e. until the buffer was empty. The streaming task keeps
                     * FILLING that buffer throughout, so above roughly
                     * 340 KB/s the drain never catches up: the loop keeps
                     * writing into the file it is trying to retire, and the
                     * rotation never completes. Measured on 11 channels at
                     * 2 kHz, one file grew to 8.7 MB against a 20,000 byte
                     * limit and only a single "size limit reached" ever
                     * appeared. Below that rate it merely overshoots -- 1.5x at
                     * 170 KB/s, 53x at 266 KB/s -- which is the same defect
                     * losing the race by less.
                     *
                     * Bytes that arrive after this point belong to the NEXT
                     * file, which is exactly the model #757 established when
                     * the rotation window began accepting encoder output for
                     * the new file. Draining the snapshot makes rotation
                     * bounded and its timing independent of throughput.
                     *
                     * `drained` counts what CircularBuf_ProcessBytes actually
                     * extracted, so a partial write cannot spin the loop. */
                    size_t drained = 0;
                    while (drained < bufferBytes
                           && gSDCardData.currentProcessState
                              != SD_CARD_MANAGER_PROCESS_STATE_ERROR
                           && CircularBuf_NumBytesAvailable(&gSDCardData.wCirbuf) > 0) {
                        int writeLen = -2;
                        SD_TakeMutexDebug(gSDCardData.wMutex, "drain_loop");
                        if (gSDCardData.sdCardWritePending != 1) {
                            gSDCardData.sdCardWritePending = 1;
                            /* #738: runtime size, not the compile-time
                             * ceiling — see the WRITE_TO_FILE extract. */
                            /* #822: cap the EXTRACTION at the snapshot too,
                             * not just the loop condition.
                             *
                             * Testing `drained < bufferBytes` before each
                             * iteration bounds how many times we extract, but
                             * not how much: ProcessBytes would take up to
                             * writeBufferSize, which is auto-balanced to ~78 KB
                             * (USB+SD) or ~124 KB (SD only). Against a snapshot
                             * of a few tens of KB a single extraction therefore
                             * swallowed the whole buffer INCLUDING bytes the
                             * producer added after the snapshot -- so the drain
                             * was still effectively unbounded in one pass, and
                             * that is where the residual file-size overshoot
                             * came from. */
                            size_t remaining = bufferBytes - drained;
                            uint32_t extractLen =
                                (remaining < (size_t)gSDCardData.writeBufferSize)
                                    ? (uint32_t)remaining
                                    : gSDCardData.writeBufferSize;
                            CircularBuf_ProcessBytes(&gSDCardData.wCirbuf, NULL,
                                extractLen, &writeLen);
                            gSDCardData.totalBytesFlushPending += gSDCardData.writeBufferLength;
                            /* Advance by what was actually EXTRACTED, not by
                             * what the write below reports -- a partial write is
                             * retried against the same extracted chunk, and
                             * counting it there would let the loop run past the
                             * snapshot. */
                            drained += gSDCardData.writeBufferLength;
                            xSemaphoreGive(gSDCardData.wMutex);

                            // Write immediately, loop for partial writes
                            while (gSDCardData.sdCardWritePending == 1) {
                                writeLen = SDCardWrite();
                                if (writeLen < 0) {
                                    if (!rotationDrainErrorLogged) {
                                        rotationDrainErrorLogged = true;
                                        LOG_E("[SD] Error draining buffer before rotation");
                                    }
                                    gSDCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_ERROR;
                                    gSDCardData.sdCardWritePending = 0;
                                    /* #825: this chunk already left wCirbuf via CircularBuf_ProcessBytes,
                                     * so these bytes are gone whether or not they reached the card.
                                     * Count them BEFORE the length is cleared, or the loss is silent --
                                     * the same class #823 closed for the stranded remainder. */
                                    Streaming_ReportSdDiscard(gSDCardData.writeBufferLength);
                                    gSDCardData.writeBufferLength = 0;
                                    gSDCardData.sdCardWriteBufferOffset = 0;
                                    break;
                                } else if ((size_t)writeLen >= gSDCardData.writeBufferLength) {
                                    gSDCardData.currentFileBytes += gSDCardData.writeBufferLength;
                                    SD_TakeMutexDebug(gSDCardData.wMutex, "drain_complete");
                                    gSDCardData.sdCardWritePending = 0;
                                    gSDCardData.writeBufferLength = 0;
                                    gSDCardData.sdCardWriteBufferOffset = 0;
                                    xSemaphoreGive(gSDCardData.wMutex);
                                } else if (writeLen > 0) {
                                    gSDCardData.currentFileBytes += writeLen;
                                    gSDCardData.writeBufferLength -= writeLen;
                                    gSDCardData.sdCardWriteBufferOffset += writeLen;
                                } else {
                                    // writeLen == 0: no progress
                                    if (!rotationDrainErrorLogged) {
                                        rotationDrainErrorLogged = true;
                                        LOG_E("[SD] Zero-byte write draining buffer before rotation");
                                    }
                                    gSDCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_ERROR;
                                    gSDCardData.sdCardWritePending = 0;
                                    /* #825: this chunk already left wCirbuf via CircularBuf_ProcessBytes,
                                     * so these bytes are gone whether or not they reached the card.
                                     * Count them BEFORE the length is cleared, or the loss is silent --
                                     * the same class #823 closed for the stranded remainder. */
                                    Streaming_ReportSdDiscard(gSDCardData.writeBufferLength);
                                    gSDCardData.writeBufferLength = 0;
                                    gSDCardData.sdCardWriteBufferOffset = 0;
                                    break;
                                }
                            }
                        } else {
                            xSemaphoreGive(gSDCardData.wMutex);
                            break;
                        }
                    }
                }

                /* #757: arm the NEW file's buffer BEFORE the sync and close,
                 * not after.
                 *
                 * IsWriteReady() stays true right up to the FileClose below,
                 * so the streaming task keeps enqueuing throughout the sync --
                 * which is the slow part. Doing the reset afterwards meant
                 * those bytes were sitting in the buffer when it was cleared,
                 * and they were DISCARDED: ~95 bytes per rotation, invisible,
                 * because nothing counted them. (Measured by counting them:
                 * 3,702 bytes across 39 rotations in one 25 s arm. The old
                 * code discarded them too, just as silently.)
                 *
                 * Resetting here instead keeps them. The buffer is empty at
                 * this point -- the drain above just emptied it into the old
                 * file -- so clearing the metadata latch now means the encoder
                 * writes the NEW file's header first and everything enqueued
                 * during the sync, the close and the open follows it, in
                 * order, into the new file. Nothing is dropped and the header
                 * is still at byte 0.
                 *
                 * Conditional on actually rotating: the split-limit branch
                 * below never opens a new file, so promising a drain there
                 * would strand whatever accumulated. */
                const bool willRotate =
                        (gSDCardData.fileCounter < SD_CARD_MANAGER_MAX_SPLIT_FILES);
                if (willRotate) {
                    /* The metadata clear MUST be inside the same mutex as the
                     * buffer reset, not before it.
                     *
                     * Streaming_ResetSdPbMetadata() sets gSdFileWasReady=false,
                     * which is the encoder's cue to emit the next file's
                     * header. The old file is still open here, so
                     * IsBufferAccepting() is true; if the pri-6 streaming task
                     * preempts this pri-5 one between that clear and the reset,
                     * it generates the header, writes it into the buffer, and
                     * sets gSdFileWasReady=true -- and then the reset below
                     * wipes it. Nothing re-emits it (OPEN_FILE skips its own
                     * reset while gSdRotating is true) and nothing counts it,
                     * so the split file starts with data rows and no header,
                     * silently. That is the same class of defect this issue is
                     * about, arriving through the fix for it.
                     *
                     * Holding wMutex across both closes it, because
                     * sd_card_manager_WriteToBuffer takes the same mutex: an
                     * encoder that has already decided to write the header
                     * blocks until the reset is done and then lands it in the
                     * emptied buffer, still at byte 0. */
                    SD_TakeMutexDebug(gSDCardData.wMutex, "rotation_open_window");
                    Streaming_ResetSdPbMetadata();
                    /* #822: whatever the producer appended DURING the drain is
                     * still here, and this reset is about to destroy it.
                     *
                     * It cannot be saved. It is newer than everything drained,
                     * so it cannot go into the old file without reopening the
                     * unbounded-drain livelock that #822 is; and it cannot be
                     * carried into the new one, because ResetSdPbMetadata()
                     * above has just armed the next header and these bytes
                     * would land AHEAD of it.
                     *
                     * So it is dropped -- but it is COUNTED. The bounded drain
                     * traded #822's visible overshoot for a discard, and an
                     * uncounted discard would be the worse of the two: the
                     * bytes were ACCEPTED by WriteToBuffer, so without this
                     * they appear in neither file nor in SdDroppedBytes, and
                     * the gap is invisible. Same reasoning, same accounting, as
                     * sd_AbandonRotationWindow(). */
                    size_t stranded =
                            CircularBuf_NumBytesAvailable(&gSDCardData.wCirbuf);
                    CircularBuf_Reset(&gSDCardData.wCirbuf);
                    gSdRotating = true;
                    xSemaphoreGive(gSDCardData.wMutex);
                    if (stranded > 0u) {
                        Streaming_ReportSdDiscard(stranded);
                        LOG_D("[SD] rotation dropped %u byte(s) buffered during drain\r\n",
                              (unsigned)stranded);
                    }
                }

                // Flush and close current file
                if (gSDCardData.fileHandle != SYS_FS_HANDLE_INVALID) {
                    // Always sync before close - ensures all filesystem buffers flushed
                    // Not just our counter, but also FS driver and SD card controller caches
                    TickType_t syncStart = xTaskGetTickCount();
                    int syncResult = SYS_FS_FileSync(gSDCardData.fileHandle);
                    SD_CheckFsOpDuration(syncStart, "FileSync(rotation)", syncResult);
                    if (syncResult == -1) {
                        LOG_E("[%s:%d]Error flushing before file rotation", __FILE__, __LINE__);
                    } else {
                        SD_TakeMutexDebug(gSDCardData.wMutex, "rotation_flush");
                        gSDCardData.totalBytesFlushPending = 0;
                        xSemaphoreGive(gSDCardData.wMutex);
                    }

                    // Close current file with error checking
                    if (SYS_FS_FileClose(gSDCardData.fileHandle) == SYS_FS_RES_FAILURE) {
                        LOG_E("[%s:%d]Error closing file before rotation", __FILE__, __LINE__);
                        /* #757: the window was opened above, BEFORE this close,
                         * so that bytes produced during the sync are kept. This
                         * exit never reaches OPEN_FILE, which means nothing
                         * will ever drain them -- close the window here or the
                         * encoder buffers into a dead path until it fills.
                         * This is the one early exit the reordering introduced;
                         * the sync failure above only logs and falls through. */
                        sd_AbandonRotationWindow("file close failed");
                        gSDCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_ERROR;
                        break;
                    }
                    gSDCardData.fileHandle = SYS_FS_HANDLE_INVALID;
                    LOG_D("[SD] Closed file '%s' (wrote %llu bytes)\r\n",
                         gSDCardData.filePath, gSDCardData.currentFileBytes);
                }

                /* #757: open the buffer for the NEW file before the slow open.
                 *
                 * The previous ordering deferred both resets to OPEN_FILE so
                 * the streaming task could not burn its one-shot header flag
                 * before the new file existed. That ordering is precisely what
                 * made this window lossy: the flag stayed set, no handle
                 * existed, so the encoder was told sdSize == 0 and everything
                 * it produced during the open was discarded and counted.
                 *
                 * Reversing it is safe because the drain above emptied the
                 * buffer into the OLD file. Resetting the metadata here means
                 * the first thing the encoder puts into the now-empty buffer
                 * is the NEW file's header -- still at byte 0 -- and whatever
                 * it encodes during the open follows in order. The buffer
                 * reset is kept for the error paths where the drain could not
                 * complete; on the normal path it is a no-op. */
                if (willRotate) {
                    /* The window was already opened above, before the sync and
                     * close, so that everything the encoder produced during
                     * them is kept rather than discarded. All that remains is
                     * to advance to the next file. */
                    if (gSDCardData.currentProcessState
                        == SD_CARD_MANAGER_PROCESS_STATE_ERROR) {
                        /* #825: the drain above could not write its backlog and set
                         * ERROR. Advancing to OPEN_FILE would overwrite that state and
                         * report a failed rotation as a successful one -- the ERROR is
                         * what routes the session through UNMOUNT_DISK with
                         * lastOperationSuccess = false. Leave it standing.
                         *
                         * The #757 window was opened above, before the sync and close,
                         * and skipping OPEN_FILE means nothing will ever drain it, so
                         * close it here exactly as the file-close failure does. */
                        sd_AbandonRotationWindow("drain write failed");
                    } else {
                        gSDCardData.fileCounter++;
                        gSDCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_OPEN_FILE;
                    }
                } else {
                    LOG_E("[%s:%d]File counter limit reached (%d files). Stopping streaming.",
                          __FILE__, __LINE__, SD_CARD_MANAGER_MAX_SPLIT_FILES);
                    // Cleanly stop: close file if open and signal completion to prevent deadlock
                    if (gSDCardData.fileHandle != SYS_FS_HANDLE_INVALID) {
                        TickType_t syncStart = xTaskGetTickCount();
                        int syncResult = SYS_FS_FileSync(gSDCardData.fileHandle);
                        SD_CheckFsOpDuration(syncStart, "FileSync(limit_stop)", syncResult);
                        if (syncResult == -1) {
                            LOG_E("[SD] Failed to sync file at split limit, error=%d", SYS_FS_Error());
                        }
                        if (SYS_FS_FileClose(gSDCardData.fileHandle) == SYS_FS_RES_FAILURE) {
                            LOG_E("[SD] Failed to close file at split limit, error=%d", SYS_FS_Error());
                        }
                        gSDCardData.fileHandle = SYS_FS_HANDLE_INVALID;
                    }
                    /* #825: the split limit means no further file will EVER be opened, so
                     * anything still in the circular buffer is unwritable -- and routing
                     * ERROR onward does not save it either: UNMOUNT_DISK guards its whole
                     * drain on `fileHandle != SYS_FS_HANDLE_INVALID` and the handle was
                     * just closed above. Count it here rather than let the next reset
                     * discard it silently, which is the same obligation the drain has.
                     * A clean stop already drained the buffer, so this is a no-op then.
                     * Mirrors the #823 stranded-remainder accounting in the rotation path. */
                    SD_TakeMutexDebug(gSDCardData.wMutex, "split_limit_strand");
                    size_t splitStranded = CircularBuf_NumBytesAvailable(&gSDCardData.wCirbuf);
                    CircularBuf_Reset(&gSDCardData.wCirbuf);
                    xSemaphoreGive(gSDCardData.wMutex);
                    if (splitStranded > 0u) {
                        Streaming_ReportSdDiscard(splitStranded);
                        LOG_E("[SD] split limit reached: discarded %u unwritable buffered byte(s)",
                              (unsigned)splitStranded);
                    }
                    /* #825: do not clobber an ERROR the drain above set. This branch is
                     * the twin of the rotation tail: it too transitioned unconditionally,
                     * so a stop that failed to write its backlog reported as a clean one.
                     * Leaving ERROR standing routes the session through UNMOUNT_DISK with
                     * lastOperationSuccess = false.
                     *
                     * No sd_AbandonRotationWindow() here, unlike the rotation tail: the
                     * #757 window is armed only under `willRotate`, and this is the branch
                     * where that is false, so there is no open window to close. */
                    if (gSDCardData.currentProcessState
                        != SD_CARD_MANAGER_PROCESS_STATE_ERROR) {
                        gSDCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_IDLE;
                    }
                    /* Signal either way -- the operation IS over, and skipping the give
                     * would hang the waiter, which is what this give exists to prevent. */
                    xSemaphoreGive(gSDCardData.opCompleteSemaphore);
                }
                break;  // Exit to reopen with new filename or error
            }
            uint64_t currentMillis = pdTICKS_TO_MS(xTaskGetTickCount());

            SD_TakeMutexDebug(gSDCardData.wMutex, "periodic_flush_check");
            bool needsFlush = (currentMillis - gSDCardData.lastFlushMillis > 5000 ||
                    gSDCardData.totalBytesFlushPending > SD_FLUSH_THRESHOLD) &&
                    gSDCardData.totalBytesFlushPending > 0;
            if (needsFlush) {
                xSemaphoreGive(gSDCardData.wMutex);

                TickType_t syncStart = xTaskGetTickCount();
                int syncResult = SYS_FS_FileSync(gSDCardData.fileHandle);
                SD_CheckFsOpDuration(syncStart, "FileSync(periodic)", syncResult);
                if (syncResult == -1) {
                    gSDCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_ERROR;
                    LOG_E("[%s:%d]Error flushing to SD Card", __FILE__, __LINE__);
                } else {
                    // Only reset counter after successful flush
                    SD_TakeMutexDebug(gSDCardData.wMutex, "periodic_flush_reset");
                    gSDCardData.totalBytesFlushPending = 0;
                    gSDCardData.lastFlushMillis = currentMillis;
                    xSemaphoreGive(gSDCardData.wMutex);
                }
            } else {
                xSemaphoreGive(gSDCardData.wMutex);
            }

        }
            break;
        case SD_CARD_MANAGER_PROCESS_STATE_READ_FROM_FILE:
        {
            // Take mutex to serialize access to gSDSharedBuffer
            if (gSDOpMutex) {
                SD_TakeMutexDebug(gSDOpMutex, "read_operation");
            }

            // Continuous loop for file transfer instead of one chunk per task tick.
            // Yields every 1 second to other tasks. Priority boosted to prevent preemption.
            // Diagnostic logging added for GitHub #146.

            uint32_t totalBytesRead = 0;
            uint32_t readCount = 0;

            // Boost task priority to match USB tasks for balanced time slicing
            TaskHandle_t currentTask = xTaskGetCurrentTaskHandle();
            UBaseType_t originalPriority = uxTaskPriorityGet(currentTask);
            vTaskPrioritySet(currentTask, 7);  // Same as USB tasks for round-robin scheduling

            // Track time for periodic yielding
            TickType_t lastYieldTime = xTaskGetTickCount();
            const TickType_t yieldInterval = pdMS_TO_TICKS(1000);

            // EOF marker as literal constant (safer than sprintf).
            // Declared before the buffer-size check so the terminal bail
            // below (#703) can also emit it.
            static const char eofMarker[] = "__END_OF_FILE__";
            /* #725: mid-transfer failure terminator. Distinct from the EOF
             * marker so a host can tell "complete" from "aborted with partial
             * data" -- see the read-error path below. */
            static const char transferErrorMarker[] = "__TRANSFER_ERROR__";

            // Calculate safe read size based on buffer capacity
            size_t maxRead = gSdSharedBufferSize;
            if (maxRead > SD_READ_MAX_CHUNK_SIZE) maxRead = SD_READ_MAX_CHUNK_SIZE;
            maxRead = (maxRead / SD_READ_ALIGNMENT_SIZE) * SD_READ_ALIGNMENT_SIZE;
            if (maxRead == 0U) {
                /* #703: terminal, host-safe bail. This path previously skipped
                 * the mode=NONE reset (SD subsystem latched busy — every later
                 * SD command rejected), leaked the open file handle, and sent
                 * NO __END_OF_FILE__ (the host hung waiting for a terminator).
                 * With the SD-circular floor pinned >= SD_READ_ALIGNMENT_SIZE
                 * this is now practically unreachable, but keep it terminal as
                 * defense in depth. Mirror the normal-exit ordering:
                 * drain -> marker -> close -> priority -> mutex -> mode/state. */
                LOG_E("[SD] Buffer too small for read (%u < %u) - aborting GET",
                      (unsigned)gSdSharedBufferSize, (unsigned)SD_READ_ALIGNMENT_SIZE);
                sd_wait_usb_drain();
                sd_card_manager_DataReadyCB(SD_CARD_MANAGER_MODE_READ,
                        (uint8_t*)eofMarker, sizeof(eofMarker) - 1);
                if (gSDCardData.fileHandle != SYS_FS_HANDLE_INVALID) {
                    (void)SYS_FS_FileClose(gSDCardData.fileHandle);
                    gSDCardData.fileHandle = SYS_FS_HANDLE_INVALID;
                }
                vTaskPrioritySet(currentTask, originalPriority);

                // Release mutex on error path
                if (gSDOpMutex) {
                    xSemaphoreGive(gSDOpMutex);
                }

                gpSDCardSettings->mode = SD_CARD_MANAGER_MODE_NONE;
                gSDCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_IDLE;
                break;
            }

            // Clear abort flag at start of transfer
            gTransferAbortRequested = false;

            // Read entire file in continuous loop
            while (1) {
                // Check for user-requested abort
                if (gTransferAbortRequested) {
                    gTransferAbortRequested = false;
                    LOG_E("[SD] Transfer ABORTED at %u bytes", totalBytesRead);
                    sd_wait_usb_drain();
                    /* Emit the terminator ONLY if no file content went out.
                     *
                     * That is #723's actual precedent: it made the PRE-transfer
                     * failures terminal (buffer-too-small, open-failure) — both
                     * of which have sent nothing — and #725 records why the
                     * mid-transfer case was deliberately left out:
                     *
                     *   "sending a plain EOF marker after partial data would
                     *    convert a detectable hang into a silently truncated
                     *    file that looks complete — the wrong trade for a
                     *    data-acquisition product"
                     *
                     * An earlier revision of this fix emitted it
                     * unconditionally, which contradicts that decision: a host
                     * would have accepted a truncated capture as a whole one.
                     * A hang is recoverable and visible; a short file that
                     * looks complete is neither.
                     *
                     * So: nothing sent -> terminate cleanly (the host learns
                     * the transfer produced no data). Partial data sent -> stay
                     * silent until #725 gives us a DISTINGUISHABLE terminator,
                     * which is the only thing that makes this case honest.
                     *
                     * Skipping the emit on the partial path also avoids a
                     * second stall: DataReadyCB retries for 10 s, and the abort
                     * is reached precisely when the peer is not draining. */
                    if (totalBytesRead == 0u) {
                        sd_card_manager_DataReadyCB(SD_CARD_MANAGER_MODE_READ,
                                (uint8_t*)eofMarker, sizeof(eofMarker) - 1);
                    } else {
                        LOG_E("[SD] aborted after %u bytes - no terminator sent "
                              "(a plain EOF would look like a complete file; "
                              "#725 tracks a distinguishable one)",
                              (unsigned)totalBytesRead);
                    }
                    if (gSDCardData.fileHandle != SYS_FS_HANDLE_INVALID) {
                        SYS_FS_FileClose(gSDCardData.fileHandle);
                        gSDCardData.fileHandle = SYS_FS_HANDLE_INVALID;
                    }
                    totalBytesRead = 0;
                    readCount = 0;
                    break;
                }

                // Abort if file handle became invalid
                if (gSDCardData.fileHandle == SYS_FS_HANDLE_INVALID) {
                    LOG_E("[SD] Transfer ABORTED: file handle invalid");
                    totalBytesRead = 0;
                    readCount = 0;
                    break;
                }

                // Read at maximum rate (backpressure handled by callback retry logic)
                size_t bytesRead = SYS_FS_FileRead(gSDCardData.fileHandle, gSdSharedBuffer, maxRead);

                if (bytesRead == (size_t) - 1) {
                    LOG_E("[SD] Transfer ERROR: %u MB, read#%u", totalBytesRead/(1024*1024), readCount);

                    // Wait for USB to drain any pending data before closing
                    sd_wait_usb_drain();

                    /* #725: send a DISTINGUISHABLE terminator, not silence and
                     * not __END_OF_FILE__.
                     *
                     * Sending nothing (the old behaviour) leaves the host
                     * waiting forever for a terminator that never arrives -- a
                     * hang, with no way to tell it from a slow transfer.
                     * Sending the normal EOF marker would be worse: the host
                     * would accept a TRUNCATED file as complete, which on a
                     * data-acquisition product means silently losing the tail
                     * of a measurement. #703/PR #723 made the PRE-transfer
                     * failures terminal for the same reason but deliberately
                     * left this path alone rather than take that trade.
                     *
                     * A separate marker lets the host do the right thing: stop
                     * waiting, and know the data is incomplete. */
                    sd_card_manager_DataReadyCB(SD_CARD_MANAGER_MODE_READ,
                            (uint8_t*)transferErrorMarker,
                            sizeof(transferErrorMarker) - 1);

                    // Close file handle to prevent resource leak
                    if (SYS_FS_FileClose(gSDCardData.fileHandle) == SYS_FS_RES_FAILURE) {
                        LOG_E("[SD] Failed to close file after read error, error=%d", SYS_FS_Error());
                    }
                    gSDCardData.fileHandle = SYS_FS_HANDLE_INVALID;
                    totalBytesRead = 0;
                    readCount = 0;
                    break;

                } else if (bytesRead == 0) {
                    // End of file - wait for USB to drain before sending EOF marker
                    sd_wait_usb_drain();

                    // Send EOF marker using literal constant (safer than sprintf)
                    sd_card_manager_DataReadyCB(SD_CARD_MANAGER_MODE_READ,
                            (uint8_t*)eofMarker,
                            sizeof(eofMarker) - 1);

                    // Close file handle
                    if (SYS_FS_FileClose(gSDCardData.fileHandle) == SYS_FS_RES_FAILURE) {
                        LOG_E("[SD] Failed to close file after read complete, error=%d", SYS_FS_Error());
                    }
                    gSDCardData.fileHandle = SYS_FS_HANDLE_INVALID;

                    totalBytesRead = 0;
                    readCount = 0;
                    break;

                } else {
                    // Data chunk read successfully
                    totalBytesRead += bytesRead;
                    readCount++;

                    sd_card_manager_DataReadyCB(SD_CARD_MANAGER_MODE_READ,
                            gSdSharedBuffer,
                            bytesRead);

                    // Delay every 1 second to allow lower priority tasks to run
                    if ((xTaskGetTickCount() - lastYieldTime) >= yieldInterval) {
                        vTaskDelay(pdMS_TO_TICKS(1));
                        lastYieldTime = xTaskGetTickCount();
                    }
                }
            }

            // Restore original task priority
            vTaskPrioritySet(currentTask, originalPriority);

            // Release mutex - operation complete
            if (gSDOpMutex) {
                xSemaphoreGive(gSDOpMutex);
            }

            // Reset mode so IsBusy() returns false
            gpSDCardSettings->mode = SD_CARD_MANAGER_MODE_NONE;
            gSDCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_IDLE;
        }
            break;
        case SD_CARD_MANAGER_PROCESS_STATE_LIST_DIR:
        {
            // Take mutex to serialize access to gSDSharedBuffer (used in callbacks)
            if (gSDOpMutex) {
                SD_TakeMutexDebug(gSDOpMutex, "list_operation");
            }

            /* #799: the operand directory when one was given, else the working
             * directory. The operand is NOT copied into the working directory
             * -- see sd_ListDirTarget(). */
            const char* listTarget = sd_ListDirTarget(gpSDCardSettings);
            LOG_D("[SD] Listing directory: '%s'\r\n", listTarget);

            // List files in chunks using static callback
            ListDirResult listResult = ListFilesInDirectoryChunked(
                    listTarget,
                    gSDCardData.messageBuffer,
                    SD_CARD_MANAGER_CONF_RBUFFER_SIZE,
                    sd_listdir_send_chunk);
            LOG_D("[SD] Listing ended: %d (0=OK 1=INCOMPLETE 2=FAILED 3=ABORTED)\r\n",
                  (int)listResult);

            // Release mutex - operation complete
            if (gSDOpMutex) {
                xSemaphoreGive(gSDOpMutex);
            }

            /* Unchanged on purpose. This flag is file-local (no reader outside
             * this module) and no caller distinguishes list outcomes through
             * it; #794 puts the outcome where the HOST can see it, in the
             * reply's terminator, rather than in a flag nothing reads. */
            gSDCardData.lastOperationSuccess = true;  // List always succeeds (may return empty)
            // Reset mode to prevent re-triggering
            gpSDCardSettings->mode = SD_CARD_MANAGER_MODE_NONE;
            gSDCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_IDLE;
            // Signal completion
            xSemaphoreGive(gSDCardData.opCompleteSemaphore);
        }
            break;

        case SD_CARD_MANAGER_PROCESS_STATE_DELETE_FILE:
        {
            LOG_D("[SD] Deleting file: '%s'\r\n", gSDCardData.filePath);

            // Delete the file (FAT f_unlink already calls sync_fs internally)
            if (SYS_FS_FileDirectoryRemove(gSDCardData.filePath) == SYS_FS_RES_SUCCESS) {
                LOG_D("[SD] File deleted successfully\r\n");
                gSDCardData.lastOperationSuccess = true;
            } else {
                SYS_FS_ERROR err = SYS_FS_Error();
                LOG_E("[SD] Failed to delete file '%s', error=%d\r\n", gSDCardData.filePath, err);
                gSDCardData.lastOperationSuccess = false;
            }

            // Reset mode to prevent re-triggering
            gpSDCardSettings->mode = SD_CARD_MANAGER_MODE_NONE;
            gSDCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_IDLE;
            // Signal completion
            xSemaphoreGive(gSDCardData.opCompleteSemaphore);
        }
            break;

        case SD_CARD_MANAGER_PROCESS_STATE_FORMAT:
        {
            LOG_D("[SD] Formatting SD card at '%s'\r\n", SD_CARD_MANAGER_DISK_MOUNT_NAME);

            // Estimate total sector writes for progress tracking
            // f_mkfs writes: VBR + FSINFO (4 sectors) + 1 FAT table + root dir cluster
            // FatFS defaults to n_fat=1 when SYS_FS_FORMAT_PARAM.n_fat is 0
            // Cluster size auto-selected by f_mkfs: sz_vol/0x20000 indexes cst32 table
            {
                uint32_t totalSec = 0, freeSec = 0;
                if (SYS_FS_DriveSectorGet(SD_CARD_MANAGER_DISK_MOUNT_NAME,
                                           &totalSec, &freeSec) == SYS_FS_RES_SUCCESS
                    && totalSec > 0) {
                    // Replicate f_mkfs auto-selection: n = sz_vol / 0x20000
                    // cst32[] = {1, 2, 4, 8, 16, 32, 0}, pau starts at 1 and doubles
                    uint32_t n = totalSec / 0x20000;
                    static const uint32_t cst32[] = {1, 2, 4, 8, 16, 32, 0};
                    uint32_t spc = 1;
                    for (int i = 0; cst32[i] && cst32[i] <= n; i++) {
                        spc <<= 1;
                    }
                    uint32_t clusters = totalSec / spc;
                    uint32_t fatSectors = (clusters * 4 + 8 + 511) / 512;
                    // reserved(32) + 1 FAT copy + root dir (1 cluster) + VBR/FSINFO (~4)
                    gFormatSectorsEstimate = 32 + fatSectors + spc + 10;
                    // Add 10% buffer so progress reaches ~90% before completion
                    gFormatSectorsEstimate += gFormatSectorsEstimate / 10;
                } else {
                    gFormatSectorsEstimate = 0;  // Unknown - progress unavailable
                }
            }

            // Enable disk write tracking and start format
            gDiskFormatSectorsWritten = 0;
            gDiskFormatTracking = true;
            gFormatStatus = 1;  // In progress

            // Format the drive using FAT filesystem
            // Use SYS_FS_FORMAT_ANY to auto-select FAT16/FAT32 based on card size
            SYS_FS_FORMAT_PARAM opt = {
                .fmt = SYS_FS_FORMAT_ANY,
                .au_size = 0  // Auto-select allocation unit size
            };

            // Work buffer for format operation (512 bytes required)
            static uint8_t formatWorkBuffer[512];

            if (SYS_FS_DriveFormat(SD_CARD_MANAGER_DISK_MOUNT_NAME, &opt, formatWorkBuffer, sizeof(formatWorkBuffer)) == SYS_FS_RES_SUCCESS) {
                LOG_D("[SD] Format completed successfully\r\n");
                /* #689: a format destroys every bucket directory, so the
                 * bucketing state must go back to 0 rather than describe
                 * directories that no longer exist. The next session would
                 * re-enter bucket 0 anyway (it rescans from 0), so this is
                 * belt-and-braces for anything that reads the state before
                 * then -- not the load-bearing reset it was when the cursor
                 * persisted across sessions. */
                gSDCardData.curBucket = 0u;
                gSDCardData.bucketFileCountAtStart = 0u;
                gSDCardData.filesInCurBucket = 0u;
                /* bucketPath too, or the refusal LOG_E and the "bucket active"
                 * trace keep naming a P0xx directory the format just deleted --
                 * the state is cleared but its label still describes the old
                 * card, which is exactly the misdirection the reason codes
                 * exist to remove. */
                gSDCardData.bucketPath[0] = '\0';
                gSDCardData.lastOperationSuccess = true;
                gFormatStatus = 2;  // Success
            } else {
                SYS_FS_ERROR err = SYS_FS_Error();
                LOG_E("[SD] Format failed, error=%d\r\n", err);
                gSDCardData.lastOperationSuccess = false;
                gFormatStatus = -1;  // Failed
            }

            gDiskFormatTracking = false;

            // Reset mode to prevent re-triggering
            gpSDCardSettings->mode = SD_CARD_MANAGER_MODE_NONE;
            gSDCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_IDLE;
            // Signal completion
            xSemaphoreGive(gSDCardData.opCompleteSemaphore);
        }
            break;

        case SD_CARD_MANAGER_PROCESS_STATE_GET_SPACE:
        {
            uint32_t totalSectors = 0;
            uint32_t freeSectors = 0;
            /* Invalidate cache before query under critical section so a
             * concurrent reader can't observe stale data labeled valid. */
            taskENTER_CRITICAL();
            gSDCardData.spaceResultValid = false;
            taskEXIT_CRITICAL();

            if (SYS_FS_DriveSectorGet(SD_CARD_MANAGER_DISK_MOUNT_NAME, &totalSectors, &freeSectors) == SYS_FS_RES_SUCCESS) {
                /* Publish the bool + two uint64 triple atomically so a
                 * concurrent SCPI reader (USB pri 7) doesn't see a torn
                 * intermediate state on the 32-bit bus.  Same pattern as
                 * CHECK_DISK_FULL's cache publish (#503). */
                uint64_t freeBytes = (uint64_t)freeSectors * SD_SECTOR_SIZE_BYTES;
                uint64_t totalBytes = (uint64_t)totalSectors * SD_SECTOR_SIZE_BYTES;
                taskENTER_CRITICAL();
                gSDCardData.spaceResultFreeBytes = freeBytes;
                gSDCardData.spaceResultTotalBytes = totalBytes;
                gSDCardData.spaceResultValid = true;
                taskEXIT_CRITICAL();
                gSDCardData.lastOperationSuccess = true;
            } else {
                LOG_E("[SD] Failed to query drive space");
                /* spaceResultValid stays false from the pre-query
                 * invalidate above — no further critical section needed
                 * for a single-bool no-op store. */
                gSDCardData.lastOperationSuccess = false;
            }

            gpSDCardSettings->mode = SD_CARD_MANAGER_MODE_NONE;
            gSDCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_IDLE;
            xSemaphoreGive(gSDCardData.opCompleteSemaphore);
        }
            break;

        case SD_CARD_MANAGER_PROCESS_STATE_COMPUTE_CRC:
        {
            /* #306: fold the file into the CRC in bounded chunks per task
             * pass - no priority boost, no long loop: CRC is a background
             * diagnostic and must not starve streaming/WiFi. ~8 KB per pass
             * via the shared op buffer (serialized by gSDOpMutex). */
            if (gSDOpMutex) {
                SD_TakeMutexDebug(gSDOpMutex, "crc_operation");
            }
            /* #306 fix (OOB write) + Qodo #610: clamp each read to the ACTUAL
             * shared-buffer size, never past it. gSdSharedBuffer is the
             * SD-circular partition, which shrinks to STREAMING_SD_CIRCULAR_MIN
             * (512) whenever SD is not the active streaming target - a hardcoded
             * 2048 overruns into the adjacent streaming sample pool. */
            size_t crcChunk = (gSdSharedBufferSize < 2048u)
                              ? (size_t)gSdSharedBufferSize : 2048u;
            bool done = false;
            /* Qodo #610: if the pool has not been partitioned yet the buffer is
             * NULL / size 0. A 0-length read returns 0 and would falsely report
             * "read error before EOF" for a perfectly good file - fail the CRC
             * cleanly instead (mirrors the read-error terminal path below). */
            if (gSdSharedBuffer == NULL || crcChunk == 0u) {
                LOG_E("[SD] CRC: shared buffer unavailable (size=%u)",
                      (unsigned)gSdSharedBufferSize);
                if (gSDOpMutex) {
                    xSemaphoreGive(gSDOpMutex);
                }
                SYS_FS_FileClose(gSDCardData.fileHandle);
                gSDCardData.fileHandle = SYS_FS_HANDLE_INVALID;
                gSDCardData.lastOperationSuccess = false;
                gpSDCardSettings->mode = SD_CARD_MANAGER_MODE_NONE;
                gSDCardData.currentProcessState =
                        SD_CARD_MANAGER_PROCESS_STATE_ERROR;
                break;
            }
            for (int pass = 0; pass < 4 && !done; pass++) {
                size_t rd = SYS_FS_FileRead(gSDCardData.fileHandle,
                                            gSdSharedBuffer, crcChunk);
                if ((int32_t)rd > 0) {
                    gSDCardData.crcRunning = CRC32_Update(gSDCardData.crcRunning,
                                                          gSdSharedBuffer, rd);
                    gSDCardData.crcLength += rd;
                } else {
                    done = true;
                }
            }
            if (gSDOpMutex) {
                xSemaphoreGive(gSDOpMutex);
            }
            if (done) {
                bool eof = SYS_FS_FileEOF(gSDCardData.fileHandle);
                SYS_FS_FileClose(gSDCardData.fileHandle);
                gSDCardData.fileHandle = SYS_FS_HANDLE_INVALID;
                if (eof) {
                    uint32_t crc = CRC32_Finalize(gSDCardData.crcRunning);
                    taskENTER_CRITICAL();
                    gSDCardData.crcResult = crc;
                    gSDCardData.crcResultValid = true;
                    taskEXIT_CRITICAL();
                    gSDCardData.lastOperationSuccess = true;
                    gpSDCardSettings->mode = SD_CARD_MANAGER_MODE_NONE;
                    gSDCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_IDLE;
                    xSemaphoreGive(gSDCardData.opCompleteSemaphore);
                } else {
                    LOG_E("[SD] CRC read error before EOF on '%s'", gSDCardData.filePath);
                    /* #306 fix (wedge): reset mode so this op is terminal -
                     * otherwise IsBusy() stays true forever and the INIT
                     * dispatch keeps re-arming COMPUTE_CRC, wedging the SD
                     * subsystem until reboot. Mirrors every other terminal
                     * error path. */
                    gpSDCardSettings->mode = SD_CARD_MANAGER_MODE_NONE;
                    gSDCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_ERROR;
                }
            }
        }
            break;

        case SD_CARD_MANAGER_PROCESS_STATE_IDLE:

            break;
        case SD_CARD_MANAGER_PROCESS_STATE_DEINIT:
            gSDCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_UNMOUNT_DISK;
            break;
        case SD_CARD_MANAGER_PROCESS_STATE_ERROR:
            gSDCardData.lastOperationSuccess = false;
            xSemaphoreGive(gSDCardData.opCompleteSemaphore);
            gSDCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_UNMOUNT_DISK;
            break;

        default:
            break;
    }
}

void sd_card_manager_SetWriteBuffer(uint8_t* buf, uint32_t size) {
    if (buf == NULL || size == 0) return;

    SD_TakeMutexDebug(gSDCardData.wMutex, "set_write_buffer");
    gSDCardData.writeBuffer = buf;
    gSDCardData.writeBufferSize = size;
    gSDCardData.writeBufferLength = 0;
    gSDCardData.sdCardWriteBufferOffset = 0;
    xSemaphoreGive(gSDCardData.wMutex);
}

void sd_card_manager_SetCircularBuffer(uint8_t* buf, uint32_t size) {
    if (buf == NULL || size == 0) return;

    SD_TakeMutexDebug(gSDCardData.wMutex, "set_circular_buffer");
    gSdSharedBuffer = buf;
    gSdSharedBufferSize = size;
    CircularBuf_InitExternal(&gSDCardData.wCirbuf, CircularBufferToSDWrite,
                             gSdSharedBuffer, gSdSharedBufferSize);
    xSemaphoreGive(gSDCardData.wMutex);
}

size_t sd_card_manager_WriteToBuffer(const char* pData, size_t len) {
    if (len == 0) return 0;
    if (gpSDCardSettings->enable != 1 || gpSDCardSettings->mode != SD_CARD_MANAGER_MODE_WRITE) {
        return 0;
    }

    // All-or-nothing, NON-BLOCKING: returns 0 immediately when the full
    // packet doesn't fit (prevents the convoy effect of tiny partial
    // writes burning CPU on mutex cycles).  Retry policy is the CALLER's:
    // solo-SD streaming wraps this in Streaming_WriteWithRetry (#520
    // backpressure); multi-output streaming intentionally does NOT retry
    // (#534 — drop+count so a stalled SD can't block the USB path).  The
    // wMutex hold here is microseconds (free-space check + memcpy); the
    // SD task's slow f_write runs OUTSIDE the mutex, so a hung card makes
    // this return 0, never block.
    SD_TakeMutexDebug(gSDCardData.wMutex, "write_buffer_add");
    /* #757: the accept decision must be authoritative HERE, not merely where
     * the caller sized the write.
     *
     * The caller computes sdSize from IsBufferAccepting() at the top of its
     * iteration and writes later. Between those two points the SD task can
     * abandon the rotation window -- a bucket refusal, a teardown, a failed
     * create -- which resets this buffer. Without this re-check the in-flight
     * write lands in the freshly reset buffer, where nothing will drain it: it
     * is lost silently AND can prepend a partial row to the next session's
     * file. The enable/mode test above does not catch it, because mode is
     * still WRITE at that moment.
     *
     * Re-checking under the mutex that sd_AbandonRotationWindow() also holds
     * closes it: this either runs before the abandon (and those bytes are
     * counted by it) or after (and refuses). */
    if (!sd_card_manager_IsBufferAccepting()) {
        xSemaphoreGive(gSDCardData.wMutex);
        return 0;
    }
    if (CircularBuf_NumBytesFree(&gSDCardData.wCirbuf) < len) {
        xSemaphoreGive(gSDCardData.wMutex);
        return 0;
    }
    size_t bytesAdded = CircularBuf_AddBytes(&gSDCardData.wCirbuf, (uint8_t*)pData, len);
    xSemaphoreGive(gSDCardData.wMutex);
    return bytesAdded;
}

bool sd_card_manager_Deinit() {
    /* enable BEFORE the state, so the SD task can never observe
     * "DEINIT but still enabled" and re-arm off the stale flag. */
    gpSDCardSettings->enable = 0;
    gSdTeardownRequested = true;   /* #800: survives a racing state store */
    gSDCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_DEINIT;
    return true;
}

bool sd_card_manager_UpdateSettings(sd_card_manager_settings_t *pSettings) {
    /* #589 P1: SD activity expected - restore the fast detect-poll cadence.
       (extern kept per the app_freertos.c pattern; prototype now also lives
       in drv_sdspi.h so signatures are checkable.) */
    extern void DRV_SDSPI_DetectPollKick(SYS_MODULE_OBJ object);
    DRV_SDSPI_DetectPollKick(0);

    /* #589: refuse to ARM an operation while the SD task is suspended.
     *
     * Every SCPI entry point checks this first, but the check and the arm are
     * not atomic: a WiFi FW-update or a bus-jam quarantine raised by a
     * higher-priority task in between would otherwise arm work into a stack
     * that is about to stop being pumped. This is the single choke point they
     * all pass through, so refusing here closes that window and, just as
     * importantly, means no operation can leave the state machine parked at
     * DEINIT for the rest of the session.
     *
     * mode NONE is deliberately exempt: it is how the timeout and shutdown
     * paths TEAR DOWN an operation (app_SDCard_GracefulShutdown uses exactly
     * that), and refusing it would strand the machine -- the failure this
     * issue is about.
     *
     * Residual, stated honestly: callers do not check this return, so a raced
     * arm still costs the caller its WaitForCompletion timeout. What it can
     * no longer do is corrupt the manager's state on the way.
     */
    /* The LIVE condition, same as the SCPI guard uses. The published flag
     * alone lags by up to one SD-task iteration, so a caller that lost the
     * race to WiFi claiming the bus could still be accepted here -- which is
     * the exact window this backstop exists to close. */
    if (pSettings != NULL &&
        pSettings->mode != SD_CARD_MANAGER_MODE_NONE &&
        (app_SDCard_SpiOwnedByWifi() || SpiBusHealth_IsSdSuspended())) {
        static bool warned = false;
        if (!warned) {
            warned = true;
            LOG_E("SD op refused at arm time - the SD task is suspended and "
                  "cannot pump it (#589)\r\n");
        }
        /* CLEAR the requested mode, do not merely decline to copy it.
         * gpSDCardSettings is assigned from the caller's pointer on first use
         * below, and every SCPI caller passes
         * BoardRunTimeConfig_Get(BOARDRUNTIME_SD_CARD_SETTINGS) -- the same
         * live object -- having ALREADY written the mode into it before
         * calling. Returning without clearing would leave the refused
         * operation armed in the shared settings, and the SD task would run
         * it when it resumes. */
        /* A refused CRC must also INVALIDATE the cached result. The #306 fix
         * that does this lives further down, inside the block this early
         * return skips -- so without it a refused
         * `SYST:STOR:SD:CRC "new.bin"` would leave the previous file's
         * checksum valid, and `SYST:STOR:SD:CRC?` would hand it back as if it
         * belonged to the file just asked about. Refusing an operation must
         * not resurrect the exact staleness #306 removed. */
        if (pSettings->mode == SD_CARD_MANAGER_MODE_COMPUTE_CRC) {
            taskENTER_CRITICAL();
            gSDCardData.crcResultValid = false;
            taskEXIT_CRITICAL();
        }
        pSettings->mode = SD_CARD_MANAGER_MODE_NONE;
        return false;
    }

    if (pSettings != NULL && gpSDCardSettings != NULL) {
        memcpy(gpSDCardSettings, pSettings, sizeof (sd_card_manager_settings_t));
        /* #306 fix (stale CRC): a new CRC request must invalidate any prior
         * result immediately, so a CRC? query in the window before OPEN_FILE
         * cannot return the previous file's CRC (GetCrcResult is checked
         * before IsBusy in the SCPI query). */
        if (gpSDCardSettings->mode == SD_CARD_MANAGER_MODE_COMPUTE_CRC) {
            taskENTER_CRITICAL();
            gSDCardData.crcResultValid = false;
            taskEXIT_CRITICAL();
        }

    }
    // Drain any stale completion token, but only when idle to avoid
    // consuming a signal that an in-flight WaitForCompletion is expecting
    if (sd_card_manager_IsIdle() && gSDCardData.opCompleteSemaphore != NULL) {
        xSemaphoreTake(gSDCardData.opCompleteSemaphore, 0);
    }
    /* #800: raise the sticky flag ONLY for an actual teardown.
     *
     * This function forces DEINIT on EVERY non-refused call -- arming a new
     * operation goes through here too, and DEINIT is how the machine restarts
     * into it. Raising the flag unconditionally would therefore let a request
     * belonging to one call be consumed during the next, re-asserting DEINIT
     * after the machine had already moved on. It happens to be harmless today
     * (every caller of this function wants DEINIT anyway, and nothing outside
     * the SD task advances the state -- sd_card_manager_Init runs once at
     * boot), but that is an argument from the current call graph, not from the
     * flag's own meaning.
     *
     * mode == NONE is the teardown signature -- it is how the timeout and
     * shutdown paths tear an operation down, and the reason the race in #800 is
     * reachable at all. Scoping the flag to it keeps the mechanism matching its
     * name, and keeps a future caller from inheriting a teardown it never asked
     * for. Arming paths keep the plain (clobberable) DEINIT they had before;
     * protecting those is a different problem from the one #800 describes. */
    if (gpSDCardSettings == NULL ||
        gpSDCardSettings->mode == SD_CARD_MANAGER_MODE_NONE) {
        gSdTeardownRequested = true;
    }
    gSDCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_DEINIT;
    return true;
}

bool sd_card_manager_IsIdle() {
    return (gSDCardData.currentProcessState == SD_CARD_MANAGER_PROCESS_STATE_IDLE ||
            gSDCardData.currentProcessState == SD_CARD_MANAGER_PROCESS_STATE_INIT);
}

/* #782: from outside the manager every stuck state looks identical - the
 * command is refused with -200 and nothing says which state refused it. The
 * switch (rather than a lookup table) is deliberate: -Wswitch is an error
 * here, so a state added without a name fails the build instead of silently
 * reporting "UNKNOWN" from the field. */
const char *sd_card_manager_GetStateName(void) {
    switch (gSDCardData.currentProcessState) {
        case SD_CARD_MANAGER_PROCESS_STATE_INIT:             return "INIT";
        case SD_CARD_MANAGER_PROCESS_STATE_MOUNT_DISK:       return "MOUNT";
        case SD_CARD_MANAGER_PROCESS_STATE_UNMOUNT_DISK:     return "UNMOUNT";
        case SD_CARD_MANAGER_PROCESS_STATE_CURRENT_DRIVE:    return "CURDRIVE";
        case SD_CARD_MANAGER_PROCESS_STATE_CHECK_DISK_FULL:  return "CHKFULL";
        case SD_CARD_MANAGER_PROCESS_STATE_CREATE_DIRECTORY: return "MKDIR";
        case SD_CARD_MANAGER_PROCESS_STATE_OPEN_FILE:        return "OPEN";
        case SD_CARD_MANAGER_PROCESS_STATE_WRITE_TO_FILE:    return "WRITE";
        case SD_CARD_MANAGER_PROCESS_STATE_READ_FROM_FILE:   return "READ";
        case SD_CARD_MANAGER_PROCESS_STATE_LIST_DIR:         return "LISTDIR";
        case SD_CARD_MANAGER_PROCESS_STATE_DELETE_FILE:      return "DELETE";
        case SD_CARD_MANAGER_PROCESS_STATE_FORMAT:           return "FORMAT";
        case SD_CARD_MANAGER_PROCESS_STATE_GET_SPACE:        return "GETSPACE";
        case SD_CARD_MANAGER_PROCESS_STATE_COMPUTE_CRC:      return "CRC";
        case SD_CARD_MANAGER_PROCESS_STATE_DEINIT:           return "DEINIT";
        case SD_CARD_MANAGER_PROCESS_STATE_IDLE:             return "IDLE";
        case SD_CARD_MANAGER_PROCESS_STATE_ERROR:            return "ERROR";
    }
    /* Unreachable while the switch is exhaustive; keeps the compiler happy
     * about a corrupted value read from RAM. */
    return "UNKNOWN";
}

/* #782: sd_card_manager_IsBusy() is true when EITHER the requested mode is
 * still set OR the state machine is off idle - two independent causes that
 * produce the identical -200. Reporting the state alone cannot tell
 * "mode left armed with the machine parked at IDLE" from "machine genuinely
 * stuck mid-operation", and those need different fixes. */
const char *sd_card_manager_GetModeName(void) {
    if (gpSDCardSettings == NULL) {
        return "UNINIT";
    }
    switch (gpSDCardSettings->mode) {
        case SD_CARD_MANAGER_MODE_NONE:           return "NONE";
        case SD_CARD_MANAGER_MODE_READ:           return "READ";
        case SD_CARD_MANAGER_MODE_WRITE:          return "WRITE";
        case SD_CARD_MANAGER_MODE_LIST_DIRECTORY: return "LIST";
        case SD_CARD_MANAGER_MODE_DELETE_FILE:    return "DELETE";
        case SD_CARD_MANAGER_MODE_FORMAT:         return "FORMAT";
        case SD_CARD_MANAGER_MODE_GET_SPACE:      return "GETSPACE";
        case SD_CARD_MANAGER_MODE_COMPUTE_CRC:    return "CRC";
    }
    return "UNKNOWN";
}

bool sd_card_manager_WaitForCompletionPumped(uint32_t timeoutMs) {
    /* #780: identical to WaitForCompletion, except it keeps the USB write half
     * running while it waits.
     *
     * The caller here is app_USBDeviceTask, which is BOTH the SCPI host and the
     * task that drains the USB circular buffer. Blocking it outright stops the
     * drain that the SD task needs in order to hand over its reply, so a reply
     * larger than the idle buffer could only complete by burning the full
     * timeout. Pumping breaks that cycle: the producer's chunks keep leaving.
     *
     * Poll in short slices rather than one long block; each slice pumps. The
     * slice is the resolution of the timeout, so keep it small. */
    if (sd_card_manager_IsIdle()) {
        return true;
    }
    TickType_t slice = pdMS_TO_TICKS(SD_WAIT_PUMP_SLICE_MS);
    if (slice == 0u) {
        slice = 1u;                 /* never a non-blocking take -- see above */
    }
    const TickType_t limit = (timeoutMs == 0) ? portMAX_DELAY
                                              : pdMS_TO_TICKS(timeoutMs);
    /* Measure ELAPSED ticks, not slices-attempted. Counting a full slice per
     * failed take over-counts: a 2-tick take returns after between just-over-1
     * and 2 ticks of wall time (the wait starts mid-tick), so the nominal
     * timeout could expire up to ~2x early. The caller documents "up to N ms";
     * honour it. */
    /* Only the USB reply path needs pumping. A TCP-targeted reply is drained by
     * a different task, so pumping USB there is pure waste on every slice -- and
     * it is the case where this runs on app_WifiTask rather than the USB task,
     * so not pumping also keeps the common path single-task. */
    const bool toUsb = (gpSDCardSettings == NULL) ||
                       (gpSDCardSettings->replyTarget != SD_CARD_REPLY_WIFI_TCP);
    const TickType_t start = xTaskGetTickCount();
    for (;;) {
        if (xSemaphoreTake(gSDCardData.opCompleteSemaphore, slice) == pdTRUE) {
            return true;
        }
        if (toUsb) {
            UsbCdc_PumpWrite();
        }
        if (limit != portMAX_DELAY) {
            /* Wrap-safe elapsed comparison (project rule: (now - start) < window). */
            if ((TickType_t)(xTaskGetTickCount() - start) >= limit) {
                break;
            }
        }
    }
    LOG_E("[SD] WaitForCompletion (pumped) timeout after %u ms\r\n", timeoutMs);
    return false;
}

bool sd_card_manager_WaitForCompletion(uint32_t timeoutMs) {
    if (sd_card_manager_IsIdle()) {
        return true;  // Already idle
    }

    TickType_t timeout = (timeoutMs == 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeoutMs);

    // Wait for operation complete semaphore
    if (xSemaphoreTake(gSDCardData.opCompleteSemaphore, timeout) == pdTRUE) {
        return true;  // Operation completed
    } else {
        LOG_E("[SD] WaitForCompletion timeout after %u ms\r\n", timeoutMs);
        // Reads/LIST work but writes hang has two known causes; name both so the
        // advisory isn't misleading (bench 2026-07-14 proved the directory case).
        // #689: creating a file in a directory holding many hundreds of files is
        //       O(dir size) in FatFs and can exceed this timeout — the common
        //       cause with small SD:MAXSize + long sessions. The #689 guard now
        //       refuses before this point, so if you see this, check dir size.
        // #589: a genuinely SPI-mode-incompatible card (e.g. some A2/SDXC) whose
        //       writes hang from power-up regardless of directory size.
        LOG_E("[SD] operations hanging while reads work - likely (a) target "
              "directory too large (#689: larger SD:MAXSize / clear the card) "
              "or (b) an SPI-mode-incompatible card (wiki: SD-Card-Compatibility)"
              "\r\n");
        return false;  // Timeout
    }
}

bool sd_card_manager_GetLastOperationResult(void) {
    // If SD manager hasn't been initialized yet, report failure
    if (gpSDCardSettings == NULL) {
        LOG_D("SD GetLastOperationResult: not initialized");
        return false;
    }
    return gSDCardData.lastOperationSuccess;
}

bool sd_card_manager_StartupDiskFull(void) {
    /* #503: true iff the most recent WRITE-mode entry was rejected by
     * the in-line disk-full pre-check (free space below minFreeBytes).
     * Cleared on every subsequent WRITE attempt (synchronously by
     * sd_card_manager_ClearStartupDiskFull below, OR asynchronously
     * by the SD task on entering CURRENT_DRIVE's WRITE branch). */
    if (gpSDCardSettings == NULL) {
        return false;
    }
    return gSDCardData.startupDiskFull;
}

void sd_card_manager_ClearStartupDiskFull(void) {
    /* #503 follow-up to PR #508: SCPI callers MUST clear this
     * synchronously before kicking off a new WRITE attempt
     * (UpdateSettings + IsWriteReady poll).  Without that, the
     * poll's early-exit branch (introduced in the same PR) reads
     * a stale `true` from a previous disk-full rejection and
     * bails out instantly — a stuck-true flag would make every
     * subsequent STR:START fail with a misleading disk-full error
     * until the SD task asynchronously reaches CURRENT_DRIVE and
     * clears the flag itself.  The SD task still clears it on
     * every WRITE entry (defense-in-depth), but the synchronous
     * pre-clear here guarantees the early-exit poll observes
     * only the CURRENT request's outcome.
     *
     * No critical section: `startupDiskFull` is a single bool
     * stored as a single byte.  PIC32MZ MIPS32 `sb` (store-byte)
     * is one instruction with no read-modify-write, so a write
     * cannot tear across task preemption.  The `volatile`
     * qualifier on the field prevents the compiler from
     * optimising the read away across cross-task boundaries.
     *
     * No NULL guard on gpSDCardSettings: gSDCardData is an
     * independent file-scope static (zero-initialised in BSS),
     * always valid memory regardless of init state.  Clearing
     * the flag pre-init is harmless — and unconditional clear
     * is correct, since gating on settings init could leave the
     * flag stuck-true if SCPI ever runs before sd init. */
    gSDCardData.startupDiskFull = false;
}

SdWriteRefuseReason sd_card_manager_WriteRefuseReason(void) {
    return gSDCardData.writeRefuseReason;
}

const char* sd_card_manager_WriteRefuseText(void) {
    switch (gSDCardData.writeRefuseReason) {
        case SD_REFUSE_BUCKETS_EXHAUSTED:
            return "every directory bucket is full - use a different directory "
                   "or clear the card";
        case SD_REFUSE_BUCKET_MKDIR:
            return "the next directory bucket could not be created - the card "
                   "may be write-protected, full or faulty";
        case SD_REFUSE_BUCKET_UNREADABLE:
            return "a directory bucket could not be read - likely a card or "
                   "filesystem fault, not a full directory";
        case SD_REFUSE_BUCKET_NOT_DIR:
            return "a file is occupying a directory-bucket name - rename or "
                   "remove it, or log to a different directory";
        case SD_REFUSE_NONE:
        default:
            /* The enum documents NONE as "not refused", so returning a refusal
             * phrase here would make a diagnostic read as a failure when none
             * happened -- e.g. a caller logging this outside an active refusal.
             * Say plainly that there is nothing to report. */
            return "no refusal recorded";
    }
}

bool sd_card_manager_StartupDirFull(void) {
    /* #689: true iff the most recent WRITE-mode file create was refused
     * because the target directory already holds >= SD_CARD_MANAGER_MAX_DIR_FILES
     * files. Read by SCPI_StartStreaming to surface a precise error. Same
     * single-byte volatile/no-critical-section rationale as StartupDiskFull. */
    return gSDCardData.startupDirFull;
}

void sd_card_manager_ClearStartupDirFull(void) {
    /* #689: SCPI callers clear this synchronously before each new WRITE attempt
     * (pairs with ClearStartupDiskFull), so the STR:START poll observes only the
     * current request's outcome and a stale `true` can't reject a later start. */
    gSDCardData.startupDirFull = false;
}

void sd_card_manager_InvalidateCrcResult(void) {
    taskENTER_CRITICAL();
    gSDCardData.crcResultValid = false;
    taskEXIT_CRITICAL();
}

bool sd_card_manager_GetCrcResult(uint32_t *crc32, uint64_t *length) {
    bool valid;
    taskENTER_CRITICAL();
    valid = gSDCardData.crcResultValid;
    if (valid) {
        if (crc32 != NULL)  { *crc32  = gSDCardData.crcResult; }
        if (length != NULL) { *length = gSDCardData.crcLength; }
    }
    taskEXIT_CRITICAL();
    return valid;
}

bool sd_card_manager_GetSpaceInfo(uint64_t *freeBytes, uint64_t *totalBytes) {
    /* Snapshot the bool + two 64-bit fields under a single critical
     * section so callers can't observe a torn intermediate value when
     * the SD task is mid-publish in CHECK_DISK_FULL or GET_SPACE.
     * The SD task writes these three fields atomically (as a triple)
     * under the same critical section in those states.  Same pattern
     * as the minFreeBytes snapshot the SD task uses on the other
     * direction. */
    uint64_t freeLocal = 0;
    uint64_t totalLocal = 0;
    bool valid = false;

    taskENTER_CRITICAL();
    valid = gSDCardData.spaceResultValid;
    if (valid) {
        freeLocal = gSDCardData.spaceResultFreeBytes;
        totalLocal = gSDCardData.spaceResultTotalBytes;
    }
    taskEXIT_CRITICAL();

    if (!valid) {
        LOG_I("SD GetSpaceInfo: result not yet available");
        return false;
    }
    if (freeBytes != NULL) {
        *freeBytes = freeLocal;
    }
    if (totalBytes != NULL) {
        *totalBytes = totalLocal;
    }
    return true;
}

/* #829: the busy predicate without the claim flag, so TryClaim can consult it
 * from inside its own critical section. */
static bool sd_card_manager_IsBusyLocked(void);

/* #829: SCPI-side exclusive claim, separate from `mode`.
 *
 * `mode` cannot be used as the claim. gpSDCardSettings ALIASES the runtime
 * config the SCPI handlers write (sd_card_manager_Init takes
 * &gpBoardRuntimeConfig->sdCardConfig, and UpdateSettings' memcpy is a
 * self-copy), and PROCESS_STATE_INIT starts work as soon as
 * `mode != MODE_NONE`. So writing mode early to reserve the manager also
 * makes the operation startable -- and app_SDCardTask (pri 5) preempts
 * app_WifiTask (pri 2), so a TCP command could have its operation begin on
 * STALE opFile/opDirectory/replyTarget before the handler filled them.
 *
 * This flag reserves the manager without arming anything. Handlers claim it,
 * fill the operands, write `mode` LAST as before, arm, and then release the
 * flag -- by which point `mode != MODE_NONE` keeps IsBusy() true, so there is
 * no gap. Error paths release it with mode still MODE_NONE.
 */
static volatile bool gSdScpiClaim = false;

bool sd_card_manager_TryClaim(void) {
    bool got;
    taskENTER_CRITICAL();
    got = !gSdScpiClaim && !sd_card_manager_IsBusyLocked();
    if (got) {
        gSdScpiClaim = true;
    }
    taskEXIT_CRITICAL();
    return got;
}

void sd_card_manager_ReleaseClaim(void) {
    taskENTER_CRITICAL();
    gSdScpiClaim = false;
    taskEXIT_CRITICAL();
}

bool sd_card_manager_IsBusy(void) {
    if (gSdScpiClaim) {
        return true;          /* #829: reserved by a SCPI handler, not yet armed */
    }
    return sd_card_manager_IsBusyLocked();
}

static bool sd_card_manager_IsBusyLocked(void) {
    // If SD manager hasn't been initialized yet, treat as busy/unavailable.
    // Intentionally true: prevents WiFi FW update or other SPI consumers from
    // starting before SD init completes during early boot.
    if (gpSDCardSettings == NULL) {
        return true;
    }

    // Note: This function is not fully atomic (no mutex). The two checks below
    // could see inconsistent state if modified by another task between them.
    // This is acceptable for pre-operation checks where false negatives during
    // brief state transitions are tolerable.

    // Busy if any operation mode is active (WRITE, READ, LIST, DELETE, FORMAT)
    // Mode is set when operation starts and cleared when complete
    if (gpSDCardSettings->mode != SD_CARD_MANAGER_MODE_NONE) {
        return true;
    }

    // Also check state machine for transient states during initialization
    switch (gSDCardData.currentProcessState) {
        case SD_CARD_MANAGER_PROCESS_STATE_IDLE:
        case SD_CARD_MANAGER_PROCESS_STATE_INIT:
            return false;
        default:
            return true;
    }
}


bool sd_card_manager_IsWriteReady(void) {
    return gpSDCardSettings != NULL
        && gpSDCardSettings->enable
        && gpSDCardSettings->mode == SD_CARD_MANAGER_MODE_WRITE
        && gSDCardData.currentProcessState == SD_CARD_MANAGER_PROCESS_STATE_WRITE_TO_FILE
        && gSDCardData.fileHandle != SYS_FS_HANDLE_INVALID;
}

/* #757: may the streaming task keep filling the SD circular buffer?
 *
 * DELIBERATELY WEAKER than sd_card_manager_IsWriteReady(). That one answers
 * "is there a file I can write to right now", which is the correct question
 * for transport health (streaming.c's #397 auto-stop must NOT think a stuck
 * open is healthy) and for the session-start latch. This one answers "will
 * anything ever consume what I put in the buffer", which is the correct
 * question for the ENCODER -- and the two differ for exactly the duration of
 * a size rotation.
 *
 * During a rotation the old handle is closed before the new one is opened, and
 * the open is slow: a FatFs create is O(N) in directory occupancy, which is
 * why the loss this fixes grew with the number of files on the card. The
 * buffer is empty across that window (the rotation drains it before closing)
 * and its contents go to the NEW file, so accepting writes is safe and the
 * header still lands at byte 0 -- Streaming_ResetSdPbMetadata() is called
 * before the window opens, so the first thing the encoder puts in the empty
 * buffer is the new file's header.
 *
 * Returns false once the open resolves, in both directions: on success the
 * WRITE_TO_FILE arm above takes over, and on failure or teardown the flag is
 * cleared so nothing keeps buffering into a path that will never drain. */
bool sd_card_manager_IsBufferAccepting(void) {
    if (sd_card_manager_IsWriteReady()) {
        return true;
    }
    return gpSDCardSettings != NULL
        && gpSDCardSettings->enable
        && gpSDCardSettings->mode == SD_CARD_MANAGER_MODE_WRITE
        && gSdRotating;
}

/* #703: is the SD read scratch large enough for SD:GET to make progress?
 * The read chunk is floor-aligned to SD_READ_ALIGNMENT_SIZE, so anything below
 * that yields maxRead==0 and the read bails. Reads the SAME variable the read
 * loop uses (gSdSharedBufferSize, a 32-bit aligned scalar -> atomic read on
 * PIC32MZ) so the SCPI-side pre-check faithfully mirrors what the SD task will
 * see. The floor fix keeps this true; this is the host-visible fail-loud guard. */
bool sd_card_manager_ReadBufferReady(void) {
    return gSdSharedBufferSize >= SD_READ_ALIGNMENT_SIZE;
}

/* #703: is an async SD op in flight that touches a STREAMING-POOL buffer the
 * partitioner (PrepareStreamingBuffers -> StreamingBufferPool_Partition) re-sizes
 * and pointer-swaps? TWO distinct-but-real hazards, hence three modes:
 *   - READ (SD:GET) and COMPUTE_CRC read the SD *circular* buffer (gSdSharedBuffer)
 *     with a chunk size computed ONCE up front — a mid-transfer swap makes their
 *     next FileRead overflow the shrunk region into adjacent partitions.
 *   - LIST_DIRECTORY does NOT read gSdSharedBuffer (it formats into the fixed
 *     512 B gSDCardData.messageBuffer). Its hazard is on the OUTPUT side: the
 *     listing chunks are written into the streaming-pool USB/WiFi *output*
 *     circular buffer (DataReadyCB -> sd_reply_write_usb/tcp ->
 *     UsbCdc_WriteToBuffer / wifi_tcp_server), the SAME buffer PrepareStreamingBuffers
 *     swaps via UsbCdc_SetWriteBuffer / wifi_tcp_server_SetWriteBuffer — a
 *     mid-listing swap races that output. (Audit note #723: the earlier comment
 *     wrongly attributed LIST's inclusion to the SD-circular read; the real reason
 *     is the output-buffer swap, so LIST correctly stays in the set.)
 * All three must block a re-partition. Deliberately NARROWER than IsBusy():
 * excludes WRITE (logging is the partition's intended consumer), GET_SPACE,
 * DELETE/FORMAT (touch no re-partitioned buffer), and post-stop unmount transients
 * (mode NONE) — so it won't spuriously reject a stream start right after an SD
 * session stops. */
bool sd_card_manager_BufferOpInFlight(void) {
    if (gpSDCardSettings == NULL) {
        return false;
    }
    switch (gpSDCardSettings->mode) {
        case SD_CARD_MANAGER_MODE_READ:
        case SD_CARD_MANAGER_MODE_COMPUTE_CRC:
        case SD_CARD_MANAGER_MODE_LIST_DIRECTORY:
            return true;
        default:
            return false;
    }
}

/* #703: non-blocking acquire of the SD op mutex (the SAME mutex the READ/LIST/CRC
 * ProcessState loops take — read_operation/list_operation — before they touch the
 * buffers the partitioner swaps: READ/CRC the SD circular, LIST the USB/WiFi output
 * circular; see sd_card_manager_BufferOpInFlight above). The streaming partitioner
 * calls this right before it re-partitions and pointer-swaps those buffers, and
 * holds it across the swap, to close the TOCTOU that BufferOpInFlight()-at-entry
 * alone leaves open: an SD op can arm on the OTHER
 * SCPI task (USB pri 7 vs TCP pri 2) between the entry check and the swap. With the
 * lock held: a swap can't begin while an op holds it (try-lock fails -> caller
 * aborts, fail-fast, no multi-second block), and an op that arms during the swap
 * blocks at its read_operation take until the swap completes -> it then computes its
 * read chunk from the NEW buffer size (consistent, no overflow). Returns true if the
 * lock was acquired (caller MUST pair with Unlock) or if the mutex doesn't exist yet
 * (early boot — no SD op machinery, nothing to guard). */
bool sd_card_manager_TryLockBuffer(void) {
    if (gSDOpMutex == NULL) {
        return true;
    }
    return xSemaphoreTake(gSDOpMutex, 0) == pdTRUE;
}

void sd_card_manager_UnlockBuffer(void) {
    if (gSDOpMutex != NULL) {
        xSemaphoreGive(gSDOpMutex);
    }
}

void sd_card_manager_AbortTransfer(void) {
    gTransferAbortRequested = true;
}

int sd_card_manager_GetFormatStatus(void) {
    return gFormatStatus;
}

void sd_card_manager_SetFormatPending(void) {
    gDiskFormatSectorsWritten = 0;
    gFormatSectorsEstimate = 0;
    gFormatStatus = 1;
}

void sd_card_manager_ClearFormatStatus(void) {
    gFormatStatus = 0;
}

int sd_card_manager_GetFormatProgress(void) {
    if (gFormatStatus == 2) return 100;
    if (gFormatStatus != 1 || gFormatSectorsEstimate == 0) return 0;
    uint32_t pct = (gDiskFormatSectorsWritten * 100) / gFormatSectorsEstimate;
    return (pct > 99) ? 99 : (int)pct;  // Cap at 99 until actually complete
}

size_t sd_card_manager_GetWriteBuffFreeSize() {
    static bool logged = false;
    if (!logged) {
        LOG_D("SD_MGR: GetWriteBuffFreeSize: enable=%d, mode=%d (WRITE=%d)",
              gpSDCardSettings->enable, gpSDCardSettings->mode, SD_CARD_MANAGER_MODE_WRITE);
        logged = true;
    }

    if (gpSDCardSettings->enable != 1 || gpSDCardSettings->mode != SD_CARD_MANAGER_MODE_WRITE) {
        return 0;
    }

    // Must protect circular buffer access with mutex
    SD_TakeMutexDebug(gSDCardData.wMutex, "get_free_size");
    size_t freeSize = CircularBuf_NumBytesFree(&gSDCardData.wCirbuf);
    xSemaphoreGive(gSDCardData.wMutex);

    if (!logged) {
        LOG_D("SD_MGR: Returning freeSize=%u", freeSize);
    }

    return freeSize;
}

// --- SD Write Metrics ---
static sd_card_write_metrics_t gSdWriteMetrics = {0};

void sd_card_manager_TrackWrite(uint32_t sectors, bool success, uint32_t elapsedMs, bool alignedCopy) {
    taskENTER_CRITICAL();
    gSdWriteMetrics.writeCallCount++;
    gSdWriteMetrics.writeSectorCount += sectors;
    gSdWriteMetrics.writeBytesTotal += (uint64_t)sectors * 512ULL;
    if (!success) {
        gSdWriteMetrics.writeErrors++;
    }
    if (elapsedMs > gSdWriteMetrics.writeMaxLatencyMs) {
        gSdWriteMetrics.writeMaxLatencyMs = elapsedMs;
    }
    if (alignedCopy) {
        gSdWriteMetrics.writeAlignedCopies++;
    }
    taskEXIT_CRITICAL();
}

void sd_card_manager_GetWriteMetricsSnapshot(sd_card_write_metrics_t* out) {
    taskENTER_CRITICAL();
    *out = gSdWriteMetrics;
    taskEXIT_CRITICAL();
}

void sd_card_manager_ResetWriteMetrics(void) {
    taskENTER_CRITICAL();
    memset(&gSdWriteMetrics, 0, sizeof(gSdWriteMetrics));
    taskEXIT_CRITICAL();
}

