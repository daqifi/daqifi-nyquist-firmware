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
scpi_result_t SCPI_StorageSDCrcStart(scpi_t * context);   /* #306 */
scpi_result_t SCPI_StorageSDCrcGet(scpi_t * context);     /* #306 */
scpi_result_t SCPI_StorageSDEnableSet(scpi_t * context);
scpi_result_t SCPI_StorageSDEnableGet(scpi_t * context);

/* #981: SYSTem:STORage:SD:FAILNext <0|1> / ? -- BENCH/TEST-ONLY.
 *
 * Arms a one-shot that forces the next real SD write to fail, so the SD
 * write-failure accounting paths can be regression-tested without filling the
 * card. Same "ships in the release binary, documented bench-use-only" category
 * as SYSTem:STReam:BENCHmark and SYSTem:STORage:SD:BENCHmark. No production
 * client should ever send this. See sd_card_manager.h's
 * sd_card_manager_SetFailNextWrite for the rails and the full rationale. */
scpi_result_t SCPI_StorageSDFailNextSet(scpi_t * context);
scpi_result_t SCPI_StorageSDFailNextGet(scpi_t * context);

// SD Card Benchmarking Commands
scpi_result_t SCPI_StorageSDBenchmark(scpi_t * context);
scpi_result_t SCPI_StorageSDBenchmarkQuery(scpi_t * context);

// SD Card File Management Commands
scpi_result_t SCPI_StorageSDDelete(scpi_t * context);
scpi_result_t SCPI_StorageSDFormat(scpi_t * context);

// SD Card File Splitting Commands
scpi_result_t SCPI_StorageSDMaxSizeSet(scpi_t * context);
scpi_result_t SCPI_StorageSDMaxSizeGet(scpi_t * context);

// SD Working Directory (#799) - the folder logging writes into, and the one
// SD:GET / SD:DELete / SD:CRC resolve a bare filename against.
scpi_result_t SCPI_StorageSDDirectoryGet(scpi_t * context);
scpi_result_t SCPI_StorageSDDirectorySet(scpi_t * context);

// SD Disk-Full Pre-Start Gate (#498)
scpi_result_t SCPI_StorageSDMinFreeSet(scpi_t * context);
scpi_result_t SCPI_StorageSDMinFreeGet(scpi_t * context);

// SD Card Space Query
scpi_result_t SCPI_StorageSDSpaceGet(scpi_t * context);

// SD Card Transfer Abort
scpi_result_t SCPI_StorageSDAbort(scpi_t * context);

// SD Card Format Status Query
scpi_result_t SCPI_StorageSDFormatQuery(scpi_t * context);

// SD Card Identification Info
scpi_result_t SCPI_StorageSDInfo(scpi_t * context);

/**
 * #589: why the SD stack is currently suspended, as user-facing text, or NULL
 * when it is not. Shared so every refusal names the SAME cause -- quarantine,
 * a WiFi firmware update, and WiFi streaming each need a different action
 * from the user, and a guard that guesses sends them after the wrong one.
 */
const char *SD_SuspendReasonText(void);

    /* Provide C++ Compatibility */
#ifdef __cplusplus
}
#endif

#endif /* _SCPI_STORAGE_SD_H */

/* *****************************************************************************
 End of File
 */
