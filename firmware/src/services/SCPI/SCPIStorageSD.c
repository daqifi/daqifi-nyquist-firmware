#define LOG_LVL     LOG_LEVEL_SCPI

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
#include "../sd_card_services/sd_card_manager.h"
#include "../../state/runtime/BoardRuntimeConfig.h"
#include "system/fs/sys_fs_media_manager.h"
#include "system/fs/sys_fs.h"
#include "Util/Logger.h"
#include "../UsbCdc/UsbCdc.h"
// SPI coordination removed from enable level - both WiFi and SD can be enabled concurrently
// SPI coordination handled at operation level when needed
#include <string.h>

#define SCPI_SD_LIST_TIMEOUT_MS 10000
#define SCPI_SD_DELETE_TIMEOUT_MS 5000
#define SCPI_SD_FORMAT_TIMEOUT_MS 30000

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
scpi_result_t SCPI_StorageSDEnableSet(scpi_t * context){
    int param1;
    scpi_result_t result = SCPI_RES_ERR;
    sd_card_manager_settings_t* pSDCardRuntimeConfig = BoardRunTimeConfig_Get(BOARDRUNTIME_SD_CARD_SETTINGS);

    if (!SCPI_ParamInt32(context, &param1, TRUE)) {
        result = SCPI_RES_ERR;
        goto __exit_point;
    }
    // Simple enable/disable - no mutex blocking for concurrent operations
    // SPI coordination handled at operation level, not enable level
    if (param1 != 0) {
        // Don't check for SD card presence here - let the SD card manager task handle it
        // This prevents blocking the SCPI handler if no card is present
        LOG_D("SD:ENAble - Enabling SD card manager\r\n");
        pSDCardRuntimeConfig->enable = true;
    } else {
        LOG_D("SD:ENAble - Disabling SD card manager\r\n");
        pSDCardRuntimeConfig->enable = false;
    }
    pSDCardRuntimeConfig->mode = SD_CARD_MANAGER_MODE_NONE;
    sd_card_manager_UpdateSettings(pSDCardRuntimeConfig);
    result = SCPI_RES_OK;  
__exit_point:
    return result;
}
scpi_result_t SCPI_StorageSDLoggingSet(scpi_t * context) {
    const char* pBuff;
    size_t fileLen = 0;
 
    scpi_result_t result = SCPI_RES_ERR;
    sd_card_manager_settings_t* pSDCardRuntimeConfig = BoardRunTimeConfig_Get(BOARDRUNTIME_SD_CARD_SETTINGS);

    if (!pSDCardRuntimeConfig->enable) {
        context->interface->write(context, SD_CARD_NOT_ENABLED_ERROR_MSG, strlen(SD_CARD_NOT_ENABLED_ERROR_MSG));
        result = SCPI_RES_ERR;
        goto __exit_point;
    }

    SCPI_ParamCharacters(context, &pBuff, &fileLen, false);

    if (fileLen > 0) {
        if (fileLen > SD_CARD_MANAGER_CONF_FILE_NAME_LEN_MAX) {
            LOG_E("SD:LOGging - Filename too long: %d bytes, max: %d\r\n", fileLen, SD_CARD_MANAGER_CONF_FILE_NAME_LEN_MAX);
            result = SCPI_RES_ERR;
            goto __exit_point;
        }
        memcpy(pSDCardRuntimeConfig->file, pBuff, fileLen);
        pSDCardRuntimeConfig->file[fileLen] = '\0';
        LOG_D("SD:LOGging - Set filename to '%s' (%d bytes) dir='%s'\r\n",
              pSDCardRuntimeConfig->file, fileLen, pSDCardRuntimeConfig->directory);
    } else {
        LOG_D("SD:LOGging - No filename provided, using existing: '%s'\r\n", pSDCardRuntimeConfig->file);
    }

    pSDCardRuntimeConfig->mode = SD_CARD_MANAGER_MODE_WRITE;

    sd_card_manager_UpdateSettings(pSDCardRuntimeConfig);
    result = SCPI_RES_OK;
__exit_point:
    return result;
}

scpi_result_t SCPI_StorageSDGetData(scpi_t * context) {
    const char* pBuff;
    size_t fileLen = 0;
    scpi_result_t result = SCPI_RES_ERR;
    sd_card_manager_settings_t* pSDCardRuntimeConfig = (sd_card_manager_settings_t*) BoardRunTimeConfig_Get(BOARDRUNTIME_SD_CARD_SETTINGS);
  
    if (!pSDCardRuntimeConfig->enable) {
        context->interface->write(context, SD_CARD_NOT_ENABLED_ERROR_MSG, strlen(SD_CARD_NOT_ENABLED_ERROR_MSG));
        result = SCPI_RES_ERR;
        goto __exit_point;
    }
    
    SCPI_ParamCharacters(context, &pBuff, &fileLen, false);

    if (fileLen > 0) {
        if (fileLen > SD_CARD_MANAGER_CONF_FILE_NAME_LEN_MAX) {
            result = SCPI_RES_ERR;
            goto __exit_point;
        }
        memcpy(pSDCardRuntimeConfig->file, pBuff, fileLen);
        pSDCardRuntimeConfig->file[fileLen] = '\0';
    }
    pSDCardRuntimeConfig->mode = SD_CARD_MANAGER_MODE_READ;
    sd_card_manager_UpdateSettings(pSDCardRuntimeConfig);
    result = SCPI_RES_OK;
__exit_point:
    return result;
}
scpi_result_t SCPI_StorageSDListDir(scpi_t * context){
    const char* pBuff;
    size_t fileLen = 0;
    scpi_result_t result = SCPI_RES_ERR;
    sd_card_manager_settings_t* pSDCardRuntimeConfig = (sd_card_manager_settings_t*) BoardRunTimeConfig_Get(BOARDRUNTIME_SD_CARD_SETTINGS);
   

    if (!pSDCardRuntimeConfig->enable) {
        LOG_E("SD:LIST? - SD card not enabled\r\n");
        SCPI_ErrorPush(context, SCPI_ERROR_EXECUTION_ERROR);
        result = SCPI_RES_ERR;
        goto __exit_point;
    }

    // Check if SD card is actually present and mounted
    if (!SYS_FS_MEDIA_MANAGER_MediaStatusGet("/dev/mmcblka1")) {
        LOG_E("SD:LIST? - No SD card detected\r\n");
        SCPI_ErrorPush(context, SCPI_ERROR_EXECUTION_ERROR);
        result = SCPI_RES_ERR;
        goto __exit_point;
    }
    
    // Get optional directory parameter
    SCPI_ParamCharacters(context, &pBuff, &fileLen, false);

    if (fileLen > 0) {
        if (fileLen >= sizeof(pSDCardRuntimeConfig->directory)) {
            LOG_E("SD:LIST? - Directory path too long: %d bytes, max: %d\r\n", 
                  fileLen, sizeof(pSDCardRuntimeConfig->directory) - 1);
            result = SCPI_RES_ERR;
            goto __exit_point;
        }
        memcpy(pSDCardRuntimeConfig->directory, pBuff, fileLen);
        pSDCardRuntimeConfig->directory[fileLen] = '\0';
    }
    // If no directory specified, the sd_card_manager will use the default from settings
    
    // Set mode to LIST_DIRECTORY and let sd_card_manager handle it
    pSDCardRuntimeConfig->mode = SD_CARD_MANAGER_MODE_LIST_DIRECTORY;
    sd_card_manager_UpdateSettings(pSDCardRuntimeConfig);

    // Wait for sd_card_manager to complete listing (up to 10 seconds for large directories)
    if (!sd_card_manager_WaitForCompletion(SCPI_SD_LIST_TIMEOUT_MS)) {
        LOG_E("SD:LIST? - Operation timeout\r\n");
        SCPI_ErrorPush(context, SCPI_ERROR_EXECUTION_ERROR);
        result = SCPI_RES_ERR;
        goto __exit_point;
    }

    result = SCPI_RES_OK;
__exit_point:
    return result;

}

// Global variables for benchmark results
typedef struct {
    uint32_t totalBytesWritten;
    uint32_t totalTimeMs;
    uint32_t writeSpeedBps;
    bool testInProgress;
    bool resultAvailable;
} SDBenchmarkResults_t;

static SDBenchmarkResults_t gSDBenchmarkResults = {0};

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
    int32_t testSizeKB = 0;
    int32_t pattern = 0;
    scpi_result_t result = SCPI_RES_ERR;
    sd_card_manager_settings_t* pSDCardRuntimeConfig = BoardRunTimeConfig_Get(BOARDRUNTIME_SD_CARD_SETTINGS);
    
    // Check if SD card is enabled
    if (!pSDCardRuntimeConfig->enable) {
        context->interface->write(context, SD_CARD_NOT_ENABLED_ERROR_MSG, strlen(SD_CARD_NOT_ENABLED_ERROR_MSG));
        result = SCPI_RES_ERR;
        goto __exit_point;
    }
    
    // Double-check that SD card is actually present
    if (!SYS_FS_MEDIA_MANAGER_MediaStatusGet("/dev/mmcblka1")) {
        context->interface->write(context, SD_CARD_NOT_PRESENT_ERROR_MSG, strlen(SD_CARD_NOT_PRESENT_ERROR_MSG));
        result = SCPI_RES_ERR;
        goto __exit_point;
    }
    
    // Check if a test is already in progress
    if (gSDBenchmarkResults.testInProgress) {
        SCPI_ErrorPush(context, SCPI_ERROR_EXECUTION_ERROR);
        context->interface->write(context, "\r\nError: Benchmark already in progress\r\n", 40);
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
    
    // Initialize benchmark results
    gSDBenchmarkResults.totalBytesWritten = 0;
    gSDBenchmarkResults.totalTimeMs = 0;
    gSDBenchmarkResults.writeSpeedBps = 0;
    gSDBenchmarkResults.testInProgress = true;
    gSDBenchmarkResults.resultAvailable = false;
    
    // Create test file name
    snprintf(pSDCardRuntimeConfig->file, SD_CARD_MANAGER_CONF_FILE_NAME_LEN_MAX, 
             "benchmark_%d.dat", (int)(xTaskGetTickCount() & 0xFFFF));
    
    // Set SD card to write mode
    pSDCardRuntimeConfig->mode = SD_CARD_MANAGER_MODE_WRITE;
    sd_card_manager_UpdateSettings(pSDCardRuntimeConfig);
    
    // Start benchmark timing
    uint32_t startTime = xTaskGetTickCount();
    
    // Generate and write test data
    uint8_t testBuffer[512]; // Write in 512-byte blocks
    uint32_t bytesToWrite = testSizeKB * 1024;
    uint32_t bytesWritten = 0;
    
    while (bytesWritten < bytesToWrite) {
        uint32_t chunkSize = (bytesToWrite - bytesWritten > sizeof(testBuffer)) ? 
                            sizeof(testBuffer) : (bytesToWrite - bytesWritten);
        
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
        
        // Write to SD card (WriteToBuffer has timeout protection)
        size_t written = sd_card_manager_WriteToBuffer((const char*)testBuffer, chunkSize);
        if (written != chunkSize) {
            // Write failed - likely buffer timeout
            char errMsg[80];
            snprintf(errMsg, sizeof(errMsg), 
                     "\r\nError: Write failed at %u/%u bytes (buffer timeout?)\r\n", 
                     bytesWritten, bytesToWrite);
            context->interface->write(context, errMsg, strlen(errMsg));
            gSDBenchmarkResults.testInProgress = false;
            result = SCPI_RES_ERR;
            goto __exit_point;
        }
        
        bytesWritten += written;
        
        // Allow other tasks to run
        vTaskDelay(1);
    }
    
    // Force flush to ensure all data is written
    pSDCardRuntimeConfig->mode = SD_CARD_MANAGER_MODE_NONE;
    sd_card_manager_UpdateSettings(pSDCardRuntimeConfig);
    vTaskDelay(100); // Wait for flush to complete
    
    // Calculate results
    uint32_t endTime = xTaskGetTickCount();
    gSDBenchmarkResults.totalTimeMs = (endTime - startTime) * portTICK_PERIOD_MS;
    gSDBenchmarkResults.totalBytesWritten = bytesWritten;
    
    if (gSDBenchmarkResults.totalTimeMs > 0) {
        gSDBenchmarkResults.writeSpeedBps = (bytesWritten * 1000) / gSDBenchmarkResults.totalTimeMs;
    }
    
    gSDBenchmarkResults.testInProgress = false;
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
        SCPI_ErrorPush(context, SCPI_ERROR_EXECUTION_ERROR);
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
    const char* pBuff;
    size_t fileLen = 0;
    scpi_result_t result = SCPI_RES_ERR;
    sd_card_manager_settings_t* pSDCardRuntimeConfig = BoardRunTimeConfig_Get(BOARDRUNTIME_SD_CARD_SETTINGS);

    if (!pSDCardRuntimeConfig->enable) {
        LOG_E("SD:DELete - SD card not enabled\r\n");
        SCPI_ErrorPush(context, SCPI_ERROR_EXECUTION_ERROR);
        result = SCPI_RES_ERR;
        goto __exit_point;
    }

    // Get filename parameter (required)
    SCPI_ParamCharacters(context, &pBuff, &fileLen, false);

    if (fileLen == 0 || fileLen > SD_CARD_MANAGER_CONF_FILE_NAME_LEN_MAX) {
        LOG_E("SD:DELete - Invalid filename length: %d\r\n", fileLen);
        SCPI_ErrorPush(context, SCPI_ERROR_ILLEGAL_PARAMETER_VALUE);
        result = SCPI_RES_ERR;
        goto __exit_point;
    }

    // Set the filename
    memcpy(pSDCardRuntimeConfig->file, pBuff, fileLen);
    pSDCardRuntimeConfig->file[fileLen] = '\0';
    LOG_D("SD:DELete - Deleting file '%s'\r\n", pSDCardRuntimeConfig->file);

    // Set mode to DELETE and trigger the operation
    pSDCardRuntimeConfig->mode = SD_CARD_MANAGER_MODE_DELETE_FILE;
    sd_card_manager_UpdateSettings(pSDCardRuntimeConfig);

    // Wait for sd_card_manager to complete deletion (up to 5 seconds)
    if (!sd_card_manager_WaitForCompletion(SCPI_SD_DELETE_TIMEOUT_MS)) {
        LOG_E("SD:DELete - Operation timeout\r\n");
        SCPI_ErrorPush(context, SCPI_ERROR_EXECUTION_ERROR);
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

    LOG_D("SD:FORmat - Formatting SD card (erasing all files)\r\n");

    // Set mode to FORMAT and trigger the operation
    pSDCardRuntimeConfig->mode = SD_CARD_MANAGER_MODE_FORMAT;
    sd_card_manager_UpdateSettings(pSDCardRuntimeConfig);

    // Wait for sd_card_manager to complete format (up to 30 seconds - formatting can be very slow on large cards)
    if (!sd_card_manager_WaitForCompletion(SCPI_SD_FORMAT_TIMEOUT_MS)) {
        LOG_E("SD:FORmat - Operation timeout\r\n");
        SCPI_ErrorPush(context, SCPI_ERROR_EXECUTION_ERROR);
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
        LOG_E("SD:MAXSize - Requested size %lld exceeds FAT32 limit (%llu bytes).\r\n", maxSizeBytes, FAT32_MAX_FILE_SIZE);
        SCPI_ErrorPush(context, SCPI_ERROR_ILLEGAL_PARAMETER_VALUE);
        goto __exit_point;
    }

    pSDCardRuntimeConfig->maxFileSizeBytes = (uint64_t)maxSizeBytes;

    LOG_D("SD:MAXSize - Set max file size to %llu bytes (%s)\r\n",
          pSDCardRuntimeConfig->maxFileSizeBytes,
          (pSDCardRuntimeConfig->maxFileSizeBytes == 0) ? "unlimited" : "splitting enabled");

    sd_card_manager_UpdateSettings(pSDCardRuntimeConfig);
    result = SCPI_RES_OK;

__exit_point:
    return result;
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

/* *****************************************************************************
 End of File
 */
