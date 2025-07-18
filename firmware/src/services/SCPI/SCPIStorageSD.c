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
    sd_card_manager_settings_t* pSdCardRuntimeConfig = BoardRunTimeConfig_Get(BOARDRUNTIME_SD_CARD_SETTINGS);
    wifi_manager_settings_t * pRunTimeWifiSettings = BoardRunTimeConfig_Get(BOARDRUNTIME_WIFI_SETTINGS);    

    if (!SCPI_ParamInt32(context, &param1, TRUE)) {
        result = SCPI_RES_ERR;
        goto __exit_point;
    }
    //sd card cannot be enabled if wifi is enabled
    if (pRunTimeWifiSettings->isEnabled && param1!=0) {
        context->interface->write(context, LAN_ACTIVE_ERROR_MSG, strlen(LAN_ACTIVE_ERROR_MSG));
        result = SCPI_RES_ERR;
        goto __exit_point;
    }
    if (param1 != 0) {
        // Check if SD card is actually present before enabling
        if (!SYS_FS_MEDIA_MANAGER_MediaStatusGet("/mnt/Daqifi")) {
            context->interface->write(context, SD_CARD_NOT_PRESENT_ERROR_MSG, strlen(SD_CARD_NOT_PRESENT_ERROR_MSG));
            result = SCPI_RES_ERR;
            goto __exit_point;
        }
        pSdCardRuntimeConfig->enable = true;
    } else {
        pSdCardRuntimeConfig->enable = false;       
    }
    pSdCardRuntimeConfig->mode = SD_CARD_MANAGER_MODE_NONE;
    sd_card_manager_UpdateSettings(pSdCardRuntimeConfig);
    result = SCPI_RES_OK;  
__exit_point:
    return result;
}
scpi_result_t SCPI_StorageSDLoggingSet(scpi_t * context) {
    const char* pBuff;
    size_t fileLen = 0;
 
    scpi_result_t result = SCPI_RES_ERR;
    sd_card_manager_settings_t* pSdCardRuntimeConfig = BoardRunTimeConfig_Get(BOARDRUNTIME_SD_CARD_SETTINGS);

    if (!pSdCardRuntimeConfig->enable) {
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
        memcpy(pSdCardRuntimeConfig->file, pBuff, fileLen);
        pSdCardRuntimeConfig->file[fileLen] = '\0';
    }
   
    pSdCardRuntimeConfig->mode = SD_CARD_MANAGER_MODE_WRITE;
    
    sd_card_manager_UpdateSettings(pSdCardRuntimeConfig);
    result = SCPI_RES_OK;
__exit_point:
    return result;
}

scpi_result_t SCPI_StorageSDGetData(scpi_t * context) {
    const char* pBuff;
    size_t fileLen = 0;
    scpi_result_t result = SCPI_RES_ERR;
    sd_card_manager_settings_t* pSdCardRuntimeConfig = (sd_card_manager_settings_t*) BoardRunTimeConfig_Get(BOARDRUNTIME_SD_CARD_SETTINGS);
  
    if (!pSdCardRuntimeConfig->enable) {
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
        memcpy(pSdCardRuntimeConfig->file, pBuff, fileLen);
        pSdCardRuntimeConfig->file[fileLen] = '\0';
    }

    if (fileLen > 0) {
        if (fileLen > SD_CARD_MANAGER_CONF_FILE_NAME_LEN_MAX) {
            result = SCPI_RES_ERR;
            goto __exit_point;
        }
        memcpy(pSdCardRuntimeConfig->file, pBuff, fileLen);
        pSdCardRuntimeConfig->file[fileLen] = '\0';
    }
    pSdCardRuntimeConfig->mode = SD_CARD_MANAGER_MODE_READ;
    sd_card_manager_UpdateSettings(pSdCardRuntimeConfig);
    result = SCPI_RES_OK;
__exit_point:
    return result;
}
scpi_result_t SCPI_StorageSDListDir(scpi_t * context){
    const char* pBuff;
    size_t fileLen = 0;
    scpi_result_t result = SCPI_RES_ERR;
    sd_card_manager_settings_t* pSdCardRuntimeConfig = (sd_card_manager_settings_t*) BoardRunTimeConfig_Get(BOARDRUNTIME_SD_CARD_SETTINGS);
   

    if (!pSdCardRuntimeConfig->enable) {
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
        memcpy(pSdCardRuntimeConfig->file, pBuff, fileLen);
        pSdCardRuntimeConfig->file[fileLen] = '\0';
    }

    if (fileLen > 0) {
        if (fileLen > SD_CARD_MANAGER_CONF_FILE_NAME_LEN_MAX) {
            result = SCPI_RES_ERR;
            goto __exit_point;
        }
        memcpy(pSdCardRuntimeConfig->file, pBuff, fileLen);
        pSdCardRuntimeConfig->file[fileLen] = '\0';
    }
    pSdCardRuntimeConfig->mode = SD_CARD_MANAGER_MODE_LIST_DIRECTORY;
    sd_card_manager_UpdateSettings(pSdCardRuntimeConfig);
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
    sd_card_manager_settings_t* pSdCardRuntimeConfig = BoardRunTimeConfig_Get(BOARDRUNTIME_SD_CARD_SETTINGS);
    
    // Check if SD card is enabled
    if (!pSdCardRuntimeConfig->enable) {
        context->interface->write(context, SD_CARD_NOT_ENABLED_ERROR_MSG, strlen(SD_CARD_NOT_ENABLED_ERROR_MSG));
        result = SCPI_RES_ERR;
        goto __exit_point;
    }
    
    // Double-check that SD card is actually present
    if (!SYS_FS_MEDIA_MANAGER_MediaStatusGet("/mnt/Daqifi")) {
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
    snprintf(pSdCardRuntimeConfig->file, SD_CARD_MANAGER_CONF_FILE_NAME_LEN_MAX, 
             "benchmark_%d.dat", (int)(xTaskGetTickCount() & 0xFFFF));
    
    // Set SD card to write mode
    pSdCardRuntimeConfig->mode = SD_CARD_MANAGER_MODE_WRITE;
    sd_card_manager_UpdateSettings(pSdCardRuntimeConfig);
    
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
        
        // Write to SD card
        size_t written = sd_card_manager_WriteToBuffer((const char*)testBuffer, chunkSize);
        if (written != chunkSize) {
            context->interface->write(context, "\r\nError: Write failed\r\n", 22);
            gSDBenchmarkResults.testInProgress = false;
            result = SCPI_RES_ERR;
            goto __exit_point;
        }
        
        bytesWritten += written;
        
        // Allow other tasks to run
        vTaskDelay(1);
    }
    
    // Force flush to ensure all data is written
    pSdCardRuntimeConfig->mode = SD_CARD_MANAGER_MODE_NONE;
    sd_card_manager_UpdateSettings(pSdCardRuntimeConfig);
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

/* *****************************************************************************
 End of File
 */
