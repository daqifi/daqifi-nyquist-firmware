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
#include <stdint.h>
#include <stdbool.h>

#include "commTest.h"
#include "FreeRTOSConfig.h"
#include "state/runtime/BoardRuntimeConfig.h"
/* This section lists the other files that are included in this file.
 */

/* TODO:  Include other files here if needed. */


/* ************************************************************************** */
/* ************************************************************************** */
/* Section: File Scope or Global Data                                         */
/* ************************************************************************** */
/* ************************************************************************** */
COMMTEST commTest;
/*  A brief description of a section can be given directly below the section
    banner.
 */

/* ************************************************************************** */
/** Descriptive Data Item Name

  @Summary
    Brief one-line summary of the data item.
    
  @Description
    Full description, explaining the purpose and usage of data item.
    <p>
    Additional description in consecutive paragraphs separated by HTML 
    paragraph breaks, as necessary.
    <p>
    Type "JavaDoc" in the "How Do I?" IDE toolbar for more information on tags.
    
  @Remarks
    Any additional remarks
 */
static uint16_t counter = 0;    

/* ************************************************************************** */
/* ************************************************************************** */
// Section: Local Functions                                                   */
/* ************************************************************************** */
/* ************************************************************************** */

/*  A brief description of a section can be given directly below the section
    banner.
 */

/* ************************************************************************** */




/* ************************************************************************** */
/* ************************************************************************** */
// Section: Interface Functions                                               */
/* ************************************************************************** */
/* ************************************************************************** */

/*  A brief description of a section can be given directly below the section
    banner.
 */

bool CommTest_FillTestData(uint8_t* buffer, uint16_t len)
{

    int i;
    uint32_t timestamp = xTaskGetTickCount();
    uint16_t chksum16=0;
    int min_size;
    // byte[0]           = #                               // 1 bytes
    // byte[1] - [4]     = timer tick                      // 4 bytes
    // byte[5] - [6]     = counter                         // 2 bytes
    // byte[7] - [n]     = padding with .                  // n bytes
    // byte[n+1] - [n+2] = checksum 16                     // 2 bytes
    // byte[n+3]         = \r
    // byte[n+4]         = \n                              // 2 bytes
    
    min_size = sizeof(char) + sizeof(timestamp) + sizeof(counter) +         \
                        sizeof(chksum16) + (2*sizeof(char));
    if(len<min_size) //minimun length required is 11
    return false;
        
    memset(buffer, '.', len);
    buffer[0] = '#';
    memcpy(buffer+1, (uint8_t*)&timestamp, sizeof(uint32_t));
    memcpy(buffer+5, (uint8_t*)&counter,   sizeof(uint16_t));
    for(i=0;i<(len-4);i++){
        chksum16+= (uint16_t)buffer[i];
    }
    memcpy(buffer+(len-4), (uint8_t*)&chksum16, sizeof(uint16_t));
    buffer[len-2] = '\r';
    buffer[len-1] = '\n';
    counter++;
    return true;
}
/* *****************************************************************************
 End of File
 */
