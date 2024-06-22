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


    /* Provide C++ Compatibility */
#ifdef __cplusplus
}
#endif

#endif /* _EXAMPLE_FILE_NAME_H */

/* *****************************************************************************
 End of File
 */
