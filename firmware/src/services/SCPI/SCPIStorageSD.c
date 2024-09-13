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
#include "../SDcard/SDCard.h"
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

scpi_result_t SCPI_StorageSDLoggingSet(scpi_t * context) {
    const char* pBuff;
    size_t fileLen = 0;
    int param1;
    scpi_result_t result = SCPI_RES_ERR;
    SDCard_RuntimeConfig_t* pSdCardRuntimeConfig = (SDCard_RuntimeConfig_t*) BoardRunTimeConfig_Get(BOARDRUNTIME_SD_CARD_SETTINGS);

    if (!SCPI_ParamInt32(context, &param1, TRUE)) {
        result = SCPI_RES_ERR;
        goto __exit_point;
    }
    
    SCPI_ParamCharacters(context, &pBuff, &fileLen, false);

    if (fileLen > 0) {
        if (fileLen > SD_CARD_CONF_FILE_NAME_LEN_MAX) {
            result = SCPI_RES_ERR;
            goto __exit_point;
        }
        memcpy(pSdCardRuntimeConfig->file, pBuff, fileLen);
        pSdCardRuntimeConfig->file[fileLen] = '\0';
    } 
    if (param1 != 0) {
        pSdCardRuntimeConfig->enable = true;
        pSdCardRuntimeConfig->mode = SD_CARD_MODE_WRITE;
    } else {
        pSdCardRuntimeConfig->enable = false;
        pSdCardRuntimeConfig->mode = SD_CARD_MODE_NONE;
    }
    SDCard_UpdateSettings(pSdCardRuntimeConfig);
    result=SCPI_RES_OK;
__exit_point:
    return result;
}



/* *****************************************************************************
 End of File
 */
