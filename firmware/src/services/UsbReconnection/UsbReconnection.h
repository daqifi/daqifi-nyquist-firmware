/*! @file UsbReconnection.h
 * 
 * This file provides USB reconnection detection functionality in OTG mode
 * 
 * When OTG mode is enabled, the BQ24297 cannot detect USB power (pgStat),
 * preventing normal USB reconnection detection. This module implements a
 * hybrid approach using multiple detection methods.
 */

#ifndef USB_RECONNECTION_H
#define USB_RECONNECTION_H

#include <stdint.h>
#include <stdbool.h>

/*! Initialize USB reconnection detection
 * 
 * Configures BQ24297 INT pin interrupt and initializes detection state
 */
void UsbReconnection_Init(void);

/*! USB reconnection detection task
 * 
 * Called periodically to check for USB reconnection using multiple methods:
 * 1. BQ24297 INT pin state (hardware interrupt)
 * 2. USB stack enumeration state
 * 3. Conditional OTG toggle (last resort)
 */
void UsbReconnection_Task(void);

/*! Check if USB reconnection is detected
 * 
 * @return true if USB has been reconnected, false otherwise
 */
bool UsbReconnection_IsDetected(void);

/*! Clear USB reconnection detection flag
 * 
 * Called after USB reconnection has been handled
 */
void UsbReconnection_Clear(void);

/*! Handle BQ24297 INT pin interrupt
 * 
 * Called from ISR when BQ24297 INT pin changes state
 */
void UsbReconnection_HandleInterrupt(void);

#endif /* USB_RECONNECTION_H */