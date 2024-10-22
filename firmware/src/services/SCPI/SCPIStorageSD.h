/* ************************************************************************** */
/** Descriptive File Name

  @Company
    Company Name

  @File Name
    filename.h

  @Summary
    Brief description of the file.

  @Description
    Describe the purpose of this file.
 */
/* ************************************************************************** */

#ifndef _SCPI_STORAGE_SD_H    /* Guard against multiple inclusion */
#define _SCPI_STORAGE_SD_H


#include "SCPIInterface.h"


/* Provide C++ Compatibility */
#ifdef __cplusplus
extern "C" {
#endif

//scpi_result_t SCPI_StorageSDCreateFolder(scpi_t * context);
scpi_result_t SCPI_StorageSDLoggingSet(scpi_t * context);
scpi_result_t SCPI_StorageSDLoggingGet(scpi_t * context);
scpi_result_t SCPI_StorageSDListDir(scpi_t * context);
scpi_result_t SCPI_StorageSDGetData(scpi_t * context);
scpi_result_t SCPI_StorageSDEnableSet(scpi_t * context);

    /* Provide C++ Compatibility */
#ifdef __cplusplus
}
#endif

#endif /* _SCPI_STORAGE_SD_H */

/* *****************************************************************************
 End of File
 */
