/*******************************************************************************
  Early 3.3V Enable

  File Name:
    early_3v3_enable.h

  Summary:
    Early initialization to enable 3.3V power rail to prevent power-off on button release

  Description:
    This header provides an inline function to enable the 3.3V power rail early
    in the boot process. This prevents the device from powering off when the user
    releases the power button before normal power initialization occurs.
    
    Reference: GitHub issue #23 - Device powers off when USB disconnected
*******************************************************************************/

#ifndef EARLY_3V3_ENABLE_H
#define EARLY_3V3_ENABLE_H

#include "peripheral/gpio/plib_gpio.h"

/* 
 * Enable 3.3V power rail early in boot sequence
 * Must be called immediately after GPIO_Initialize()
 * 
 * Hardware notes from schematic:
 * - USB power forces 3.3V EN
 * - Power switch pulses 3.3V EN  
 * - Micro can power 3.3V EN from RH12
 * - Micro should NOT pull RH12 low!
 */
static inline void Early_3V3_Enable(void)
{
    /* Set RH12 high to latch 3.3V power rail
     * This must happen before user releases power button */
    GPIO_PinSet(PWR_3_3V_EN_PIN);
}

#endif /* EARLY_3V3_ENABLE_H */