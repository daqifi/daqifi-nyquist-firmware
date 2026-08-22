/*******************************************************************************
  Main Source File

  Company:
    Microchip Technology Inc.

  File Name:
    main.c

  Summary:
    This file contains the "main" function for a project.

  Description:
    This file contains the "main" function for a project.  The
    "main" function calls the "SYS_Initialize" function to initialize the state
    machines of all modules in the system
 *******************************************************************************/

// *****************************************************************************
// *****************************************************************************
// Section: Included Files
// *****************************************************************************
// *****************************************************************************

#include <stddef.h>                     // Defines NULL
#include <stdbool.h>                    // Defines true
#include <stdlib.h>                     // Defines EXIT_FAILURE
#include "definitions.h"                // SYS function prototypes


// *****************************************************************************
// *****************************************************************************
// Section: Main Entry Point
// *****************************************************************************
// *****************************************************************************

#include "services/SCPI/SCPIInterface.h"

int main ( void )
{
    /* Initialize all modules */
    SYS_Initialize ( NULL );

    /* #833: fold the program image into its CRC fingerprint here, before
     * SYS_Tasks() starts the scheduler. It is ~120 ms of straight-line work,
     * which is nothing against a multi-second boot but would be a poor thing
     * to do inside a SCPI query -- and doing it with no task running means
     * the value is published before anything can read it. */
    SCPI_PrecomputeFirmwareImageCrc32();

    while ( true )
    {
        /* Maintain state machines of all polled MPLAB Harmony modules. */
        SYS_Tasks ( );
    }

    /* Execution should not come here during normal operation */

    return ( EXIT_FAILURE );
}


/*******************************************************************************
 End of File
*/

