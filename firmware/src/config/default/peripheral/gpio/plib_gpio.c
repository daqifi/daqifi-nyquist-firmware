/*******************************************************************************
  SYS PORTS Static Functions for PORTS System Service

  Company:
    Microchip Technology Inc.

  File Name:
    plib_gpio.c

  Summary:
    GPIO function implementations for the GPIO PLIB.

  Description:
    The GPIO PLIB provides a simple interface to manage peripheral
    input-output controller.

*******************************************************************************/

//DOM-IGNORE-BEGIN
/*******************************************************************************
* Copyright (C) 2019 Microchip Technology Inc. and its subsidiaries.
*
* Subject to your compliance with these terms, you may use Microchip software
* and any derivatives exclusively with Microchip products. It is your
* responsibility to comply with third party license terms applicable to your
* use of third party software (including open source software) that may
* accompany Microchip software.
*
* THIS SOFTWARE IS SUPPLIED BY MICROCHIP "AS IS". NO WARRANTIES, WHETHER
* EXPRESS, IMPLIED OR STATUTORY, APPLY TO THIS SOFTWARE, INCLUDING ANY IMPLIED
* WARRANTIES OF NON-INFRINGEMENT, MERCHANTABILITY, AND FITNESS FOR A
* PARTICULAR PURPOSE.
*
* IN NO EVENT WILL MICROCHIP BE LIABLE FOR ANY INDIRECT, SPECIAL, PUNITIVE,
* INCIDENTAL OR CONSEQUENTIAL LOSS, DAMAGE, COST OR EXPENSE OF ANY KIND
* WHATSOEVER RELATED TO THE SOFTWARE, HOWEVER CAUSED, EVEN IF MICROCHIP HAS
* BEEN ADVISED OF THE POSSIBILITY OR THE DAMAGES ARE FORESEEABLE. TO THE
* FULLEST EXTENT ALLOWED BY LAW, MICROCHIP'S TOTAL LIABILITY ON ALL CLAIMS IN
* ANY WAY RELATED TO THIS SOFTWARE WILL NOT EXCEED THE AMOUNT OF FEES, IF ANY,
* THAT YOU HAVE PAID DIRECTLY TO MICROCHIP FOR THIS SOFTWARE.
*******************************************************************************/
//DOM-IGNORE-END

#include "plib_gpio.h"
#include "interrupts.h"



/* Array to store callback objects of each configured interrupt */
static volatile GPIO_PIN_CALLBACK_OBJ portPinCbObj[2];

/* Array to store number of interrupts in each PORT Channel + previous interrupt count */
static uint8_t portNumCb[10 + 1] = { 0, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, };

/******************************************************************************
  Function:
    GPIO_Initialize ( void )

  Summary:
    Initialize the GPIO library.

  Remarks:
    See plib_gpio.h for more details.
*/
void GPIO_Initialize ( void )
{

    /* PORTA Initialization */
    ODCASET = 0xcU; /* Open Drain Enable */
    LATA = 0x0U; /* Initial Latch Value */
    TRISACLR = 0x20U; /* Direction Control */
    ANSELACLR = 0x20U; /* Digital Mode Enable */
    CNPUASET = 0xcU; /* Pull-Up Enable */

    /* Change Notice Enable */
    CNCONASET = _CNCONA_EDGEDETECT_MASK | _CNCONA_ON_MASK;
    IEC3SET = _IEC3_CNAIE_MASK;
    /* PORTB Initialization */
    LATB = 0x0U; /* Initial Latch Value */
    TRISBCLR = 0x4000U; /* Direction Control */
    ANSELBCLR = 0x4000U; /* Digital Mode Enable */
    /* PORTC Initialization */
    LATC = 0x0U; /* Initial Latch Value */
    TRISCCLR = 0x8U; /* Direction Control */
    ANSELCCLR = 0xeU; /* Digital Mode Enable */
    /* PORTD Initialization */
    LATD = 0x0U; /* Initial Latch Value */
    TRISDCLR = 0x2285U; /* Direction Control */
    CNPUDSET = 0x800U; /* Pull-Up Enable */

    /* Change Notice Enable */
    CNCONDSET = _CNCOND_EDGEDETECT_MASK | _CNCOND_ON_MASK;
    IEC3SET = _IEC3_CNDIE_MASK;
    /* PORTE Initialization */
    LATE = 0x0U; /* Initial Latch Value */
    TRISECLR = 0x85U; /* Direction Control */
    ANSELECLR = 0xf0U; /* Digital Mode Enable */
    /* PORTF Initialization */
    LATF = 0x20U; /* Initial Latch Value */
    TRISFCLR = 0x20U; /* Direction Control */
    /* PORTG Initialization */
    LATG = 0x0U; /* Initial Latch Value */
    TRISGCLR = 0x8000U; /* Direction Control */
    ANSELGCLR = 0x8000U; /* Digital Mode Enable */
    /* PORTH Initialization */
    ODCHSET = 0x8000U; /* Open Drain Enable */
    LATH = 0xa110U; /* Initial Latch Value */
    TRISHCLR = 0xa110U; /* Direction Control */
    ANSELHCLR = 0x10U; /* Digital Mode Enable */
    /* PORTJ Initialization */
    LATJ = 0x0U; /* Initial Latch Value */
    TRISJCLR = 0x94b5U; /* Direction Control */
    /* PORTK Initialization */
    LATK = 0x30U; /* Initial Latch Value */
    TRISKCLR = 0xb0U; /* Direction Control */

    /* Unlock system for PPS configuration */
    SYSKEY = 0x00000000U;
    SYSKEY = 0xAA996655U;
    SYSKEY = 0x556699AAU;

    CFGCONbits.IOLOCK = 0U;

    /* PPS Input Remapping */
    SDI4R = 13;

    /* PPS Output Remapping */
    RPC13R = 8;
    RPD12R = 12;
    RPF0R = 11;
    RPF1R = 12;
    RPG1R = 11;
    RPG0R = 12;
    RPD1R = 12;

        /* Lock back the system after PPS configuration */
    CFGCONbits.IOLOCK = 1U;

    SYSKEY = 0x00000000U;

    uint32_t i;
    /* Initialize Interrupt Pin data structures */
    portPinCbObj[0 + 0].pin = GPIO_PIN_RA4;
    
    portPinCbObj[1 + 0].pin = GPIO_PIN_RD11;
    
    for(i=0U; i<2U; i++)
    {
        portPinCbObj[i].callback = NULL;
    }
}

// *****************************************************************************
// *****************************************************************************
// Section: GPIO APIs which operates on multiple pins of a port
// *****************************************************************************
// *****************************************************************************

// *****************************************************************************
/* Function:
    uint32_t GPIO_PortRead ( GPIO_PORT port )

  Summary:
    Read all the I/O lines of the selected port.

  Description:
    This function reads the live data values on all the I/O lines of the
    selected port.  Bit values returned in each position indicate corresponding
    pin levels.
    1 = Pin is high.
    0 = Pin is low.

    This function reads the value regardless of pin configuration, whether it is
    set as as an input, driven by the GPIO Controller, or driven by a peripheral.

  Remarks:
    If the port has less than 32-bits, unimplemented pins will read as
    low (0).
    Implemented pins are Right aligned in the 32-bit return value.
*/
uint32_t GPIO_PortRead(GPIO_PORT port)
{
    return (*(volatile uint32_t *)(&PORTA + (port * 0x40U)));
}

// *****************************************************************************
/* Function:
    void GPIO_PortWrite (GPIO_PORT port, uint32_t mask, uint32_t value);

  Summary:
    Write the value on the masked I/O lines of the selected port.

  Remarks:
    See plib_gpio.h for more details.
*/
void GPIO_PortWrite(GPIO_PORT port, uint32_t mask, uint32_t value)
{
    *(volatile uint32_t *)(&LATA + (port * 0x40U)) = (*(volatile uint32_t *)(&LATA + (port * 0x40U)) & (~mask)) | (mask & value);
}

// *****************************************************************************
/* Function:
    uint32_t GPIO_PortLatchRead ( GPIO_PORT port )

  Summary:
    Read the latched value on all the I/O lines of the selected port.

  Remarks:
    See plib_gpio.h for more details.
*/
uint32_t GPIO_PortLatchRead(GPIO_PORT port)
{
    return (*(volatile uint32_t *)(&LATA + (port * 0x40U)));
}

// *****************************************************************************
/* Function:
    void GPIO_PortSet ( GPIO_PORT port, uint32_t mask )

  Summary:
    Set the selected IO pins of a port.

  Remarks:
    See plib_gpio.h for more details.
*/
void GPIO_PortSet(GPIO_PORT port, uint32_t mask)
{
    *(volatile uint32_t *)(&LATASET + (port * 0x40U)) = mask;
}

// *****************************************************************************
/* Function:
    void GPIO_PortClear ( GPIO_PORT port, uint32_t mask )

  Summary:
    Clear the selected IO pins of a port.

  Remarks:
    See plib_gpio.h for more details.
*/
void GPIO_PortClear(GPIO_PORT port, uint32_t mask)
{
    *(volatile uint32_t *)(&LATACLR + (port * 0x40U)) = mask;
}

// *****************************************************************************
/* Function:
    void GPIO_PortToggle ( GPIO_PORT port, uint32_t mask )

  Summary:
    Toggles the selected IO pins of a port.

  Remarks:
    See plib_gpio.h for more details.
*/
void GPIO_PortToggle(GPIO_PORT port, uint32_t mask)
{
    *(volatile uint32_t *)(&LATAINV + (port * 0x40U))= mask;
}

// *****************************************************************************
/* Function:
    void GPIO_PortInputEnable ( GPIO_PORT port, uint32_t mask )

  Summary:
    Enables selected IO pins of a port as input.

  Remarks:
    See plib_gpio.h for more details.
*/
void GPIO_PortInputEnable(GPIO_PORT port, uint32_t mask)
{
    *(volatile uint32_t *)(&TRISASET + (port * 0x40U)) = mask;
}

// *****************************************************************************
/* Function:
    void GPIO_PortOutputEnable ( GPIO_PORT port, uint32_t mask )

  Summary:
    Enables selected IO pins of a port as output(s).

  Remarks:
    See plib_gpio.h for more details.
*/
void GPIO_PortOutputEnable(GPIO_PORT port, uint32_t mask)
{
    *(volatile uint32_t *)(&TRISACLR + (port * 0x40U)) = mask;
}

// *****************************************************************************
/* Function:
    void GPIO_PortInterruptEnable(GPIO_PORT port, uint32_t mask)

  Summary:
    Enables IO interrupt on selected IO pins of a port.

  Remarks:
    See plib_gpio.h for more details.
*/
void GPIO_PortInterruptEnable(GPIO_PORT port, uint32_t mask)
{
    *(volatile uint32_t *)(&CNENASET + (port * 0x40U)) = mask;
}

// *****************************************************************************
/* Function:
    void GPIO_PortInterruptDisable(GPIO_PORT port, uint32_t mask)

  Summary:
    Disables IO interrupt on selected IO pins of a port.

  Remarks:
    See plib_gpio.h for more details.
*/
void GPIO_PortInterruptDisable(GPIO_PORT port, uint32_t mask)
{
    *(volatile uint32_t *)(&CNENACLR + (port * 0x40U)) = mask;
}

// *****************************************************************************
// *****************************************************************************
// Section: GPIO APIs which operates on one pin at a time
// *****************************************************************************
// *****************************************************************************

// *****************************************************************************
/* Function:
    void GPIO_PinIntEnable(GPIO_PIN pin, GPIO_INTERRUPT_STYLE style)

  Summary:
    Enables IO interrupt of particular style on selected IO pins of a port.

  Remarks:
    See plib_gpio.h for more details.
*/
void GPIO_PinIntEnable(GPIO_PIN pin, GPIO_INTERRUPT_STYLE style)
{
    GPIO_PORT port;
    uint32_t mask;

    port = (GPIO_PORT)(pin>>4U);
    mask =  0x1UL << (pin & 0xFU);

    if (style == GPIO_INTERRUPT_ON_MISMATCH)
    {
        *(volatile uint32_t *)(&CNENASET + (port * 0x40U)) = mask;
    }
    else if (style == GPIO_INTERRUPT_ON_RISING_EDGE)
    {
        *(volatile uint32_t *)(&CNENASET + (port * 0x40U)) = mask;
        *(volatile uint32_t *)(&CNNEACLR + (port * 0x40U)) = mask;
    }
    else if (style == GPIO_INTERRUPT_ON_FALLING_EDGE)
    {
        *(volatile uint32_t *)(&CNENACLR + (port * 0x40U)) = mask;
        *(volatile uint32_t *)(&CNNEASET + (port * 0x40U)) = mask;
    }
    else if (style == GPIO_INTERRUPT_ON_BOTH_EDGES)
    {
        *(volatile uint32_t *)(&CNENASET + (port * 0x40U)) = mask;
        *(volatile uint32_t *)(&CNNEASET + (port * 0x40U)) = mask;
    }
    else
    {
        /* Nothing to process */
    }
}

// *****************************************************************************
/* Function:
    void GPIO_PinIntDisable(GPIO_PIN pin)

  Summary:
    Disables IO interrupt on selected IO pins of a port.

  Remarks:
    See plib_gpio.h for more details.
*/
void GPIO_PinIntDisable(GPIO_PIN pin)
{
    GPIO_PORT port;
    uint32_t mask;

    port = (GPIO_PORT)(pin>>4U);
    mask =  0x1UL << (pin & 0xFU);

    *(volatile uint32_t *)(&CNENACLR + (port * 0x40U)) = mask;
    *(volatile uint32_t *)(&CNNEACLR + (port * 0x40U)) = mask;
}
// *****************************************************************************
/* Function:
    bool GPIO_PinInterruptCallbackRegister(
        GPIO_PIN pin,
        const GPIO_PIN_CALLBACK callback,
        uintptr_t context
    );

  Summary:
    Allows application to register callback for configured pin.

  Remarks:
    See plib_gpio.h for more details.
*/
bool GPIO_PinInterruptCallbackRegister(
    GPIO_PIN pin,
    const GPIO_PIN_CALLBACK callback,
    uintptr_t context
)
{
    uint8_t i;
    uint8_t portIndex;

    portIndex = (uint8_t)(pin >> 4U);

    for(i = portNumCb[portIndex]; i < portNumCb[portIndex +1]; i++)
    {
        if (portPinCbObj[i].pin == pin)
        {
            portPinCbObj[i].callback = callback;
            portPinCbObj[i].context  = context;
            return true;
        }
    }
    return false;
}

// *****************************************************************************
// *****************************************************************************
// Section: Local Function Implementation
// *****************************************************************************
// *****************************************************************************


// *****************************************************************************
/* Function:
    void CHANGE_NOTICE_A_InterruptHandler(void)

  Summary:
    Interrupt Handler for change notice interrupt for channel A.

  Remarks:
    It is an internal function called from ISR, user should not call it directly.
*/
void __attribute__((used)) CHANGE_NOTICE_A_InterruptHandler(void)
{
    uint8_t i;
    uint32_t status;
    GPIO_PIN pin;
    uintptr_t context;

    status  = CNFA;
    CNFA = 0U;

    IFS3CLR = _IFS3_CNAIF_MASK;

    /* Check pending events and call callback if registered */
    for(i = 0; i < 1; i++)
    {
        pin = portPinCbObj[i].pin;

        if((portPinCbObj[i].callback != NULL) && ((status & ((uint32_t)1U << (pin & 0xFU))) != 0U))
        {
            context = portPinCbObj[i].context;

            portPinCbObj[i].callback (pin, context);
        }
    }
}

// *****************************************************************************
/* Function:
    void CHANGE_NOTICE_D_InterruptHandler(void)

  Summary:
    Interrupt Handler for change notice interrupt for channel D.

  Remarks:
    It is an internal function called from ISR, user should not call it directly.
*/
void __attribute__((used)) CHANGE_NOTICE_D_InterruptHandler(void)
{
    uint8_t i;
    uint32_t status;
    GPIO_PIN pin;
    uintptr_t context;

    status  = CNFD;
    CNFD = 0U;

    IFS3CLR = _IFS3_CNDIF_MASK;

    /* Check pending events and call callback if registered */
    for(i = 1; i < 2; i++)
    {
        pin = portPinCbObj[i].pin;

        if((portPinCbObj[i].callback != NULL) && ((status & ((uint32_t)1U << (pin & 0xFU))) != 0U))
        {
            context = portPinCbObj[i].context;

            portPinCbObj[i].callback (pin, context);
        }
    }
}


/*******************************************************************************
 End of File
*/
