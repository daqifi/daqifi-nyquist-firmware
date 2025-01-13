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

/* *****************************************************************************
 End of File
 */
