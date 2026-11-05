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

#ifndef _NVM_H    /* Guard against multiple inclusion */
#define _NVM_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include "configuration.h"
#include "definitions.h"



/* Provide C++ Compatibility */
#ifdef __cplusplus
extern "C" {
#endif


    /**
     * Saves data to address specified (test/diagnostic function)
     * @param addr The address of flash to write to
     * @param data Word of data to be written
     * @return True on success, false otherwise
     */
    bool nvm_WriteWordtoAddr(uint32_t addr, uint32_t data);
    //  
    /**
     * Reads data from address specified (test/diagnostic function)
     * @param addr The address of flash to read from
     * @return Data word read from NVM.
     */
    uint32_t nvm_ReadfromAddr(uint32_t addr);

    /**
     * Erases a page of data from address specified (test/diagnostic function)
     * @param addr The address of flash to begin the page erase
     */
    bool nvm_ErasePage(uint32_t addr);
    //    
    /**
     * Saves row of data to address specified
     * @param addr The address of flash to write to
     * @param rowdata[] Row of data to be written
     * @return True on success, false otherwise
     */
    bool nvm_WriteRowtoAddr(uint32_t addr, uint8_t rowdata[]);

    /**
     * #1153: one-time creation of the recursive mutex that guards every
     * write/erase entry point in this file (nvm_ErasePage,
     * nvm_WriteWordtoAddr, nvm_WriteRowtoAddr) plus the gNvmOperationDone
     * completion flag and the DMA-suspend dance they share.
     *
     * MUST be called once, from task context, before the first call to any
     * of the three functions above -- app_SystemInit() does this at its
     * very top (alongside SCPI_ResponseBuf_Init()/SCPI_StatusLock_Init()),
     * strictly before the boot-time settings load/save sequences further
     * down that same function ever run. Deliberately NOT a lazy
     * create-on-first-use: the first real caller is that same boot save,
     * which is single-threaded (app_SystemInit runs before
     * app_TasksCreate()), so a lazy create would happen to be race-free
     * there too -- but relying on that silently would leave nothing
     * guaranteeing it stays true as callers are added. An explicit,
     * provably-first init makes the ordering a fact instead of an
     * accident.
     *
     * Recursive because daqifi_settings_SaveToNvm() calls
     * daqifi_settings_ClearNvm() (an erase) itself, and
     * daqifi_settings_SaveADCCalSettings() calls SaveToNvm() -- a plain
     * mutex taken at both levels self-deadlocks on EVERY save, including
     * the boot save, which means the device would never leave
     * app_SystemInit(). FreeRTOSConfig.h already has
     * configUSE_RECURSIVE_MUTEXES set to 1.
     *
     * Safe to call more than once (idempotent no-op after the first) so an
     * accidental second call cannot leak or replace a semaphore other
     * code may already be holding.
     */
    void nvm_MutexInit(void);

    /**
     * #1153: take the NVM recursive mutex. Called internally by
     * nvm_ErasePage / nvm_WriteWordtoAddr / nvm_WriteRowtoAddr, and exposed
     * here as an ACCESSOR so a caller that needs to bracket a whole
     * load-modify-save sequence -- not just one erase or one write -- can
     * hold the same lock across it (daqifi_settings_Update(), and
     * ADCUseCalSetClaimed() in SCPIADC.c, both do this). Recursive: a
     * caller already holding the lock may take it again (e.g. SaveToNvm()
     * calling ClearNvm() from inside a daqifi_settings_Update() span);
     * give it back exactly as many times as it was taken.
     *
     * Blocks forever (portMAX_DELAY) rather than timing out -- NVM access
     * is not expected to be contended for long, and a timeout path here
     * would need its own failure handling that nothing today exercises.
     *
     * Task context only, and only after nvm_MutexInit() has run (see its
     * doc comment). Calling this before nvm_MutexInit() is undefined --
     * deliberately not guarded with a lazy create here; see that
     * function's comment for why a missing init should fail loudly rather
     * than silently construct an uncoordinated mutex instance per caller.
     */
    void nvm_Lock(void);

    /**
     * #1153: release the NVM recursive mutex taken by nvm_Lock(). See that
     * function's doc comment.
     */
    void nvm_Unlock(void);

    /* Provide C++ Compatibility */
#ifdef __cplusplus
}
#endif

#endif /* _EXAMPLE_FILE_NAME_H */

/* *****************************************************************************
 End of File
 */
