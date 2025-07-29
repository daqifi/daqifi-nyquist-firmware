/*! @file UsbReconnection.c
 * 
 * This file implements USB reconnection detection functionality in OTG mode
 */

#include "UsbReconnection.h"
#include "HAL/BQ24297/BQ24297.h"
#include "services/UsbCdc/UsbCdc.h"
#include "state/data/BoardData.h"
#include "config/default/peripheral/gpio/plib_gpio.h"
#include "config/default/peripheral/evic/plib_evic.h"
#include "config/default/peripheral/coretimer/plib_coretimer.h"
#include "config/default/definitions.h"
#include "Util/Logger.h"
#include "FreeRTOS.h"
#include "task.h"

// Detection state
typedef struct {
    bool initialized;           // Module initialized flag
    bool intTriggered;          // BQ24297 INT pin triggered
    bool usbReconnected;        // USB reconnection detected
    uint32_t lastCheckTime;     // Last OTG toggle check time
    uint32_t intTriggerTime;    // When INT was triggered
    bool otgTogglePending;      // Need to check with OTG toggle
    uint32_t lastReconnectTime; // Time of last reconnection detection
} tUsbReconnectionState;

static tUsbReconnectionState gReconnectionState = {0};

// Timing constants
#define OTG_TOGGLE_CHECK_INTERVAL_MS    5000    // Check every 5 seconds max
#define OTG_TOGGLE_DURATION_MS          5       // Brief 5ms toggle
#define INT_DEBOUNCE_TIME_MS            100     // Debounce INT pin
#define RECONNECT_COOLDOWN_MS           3000    // Cooldown after reconnection detected

// Forward declarations
static void CheckUsbStackState(void);
static bool SafeOtgToggleCheck(void);
static void UsbReconnection_GPIOCallback(GPIO_PIN pin, uintptr_t context);

void UsbReconnection_Init(void) {
    // Prevent duplicate initialization
    if (gReconnectionState.initialized) {
        LOG_D("UsbReconnection_Init: Already initialized, skipping");
        return;
    }
    
    LOG_D("UsbReconnection_Init: Initializing USB reconnection detection");
    
    // Initialize state
    gReconnectionState.initialized = true;
    gReconnectionState.intTriggered = false;
    gReconnectionState.usbReconnected = false;
    gReconnectionState.lastCheckTime = 0;
    gReconnectionState.intTriggerTime = 0;
    gReconnectionState.otgTogglePending = false;
    gReconnectionState.lastReconnectTime = 0;
    
    // Configure BQ24297 INT pin (RA4) as input with pull-up (INT is active low)
    BATT_MAN_INT_InputEnable();
    
    // Register GPIO callback for INT pin with both edge detection
    GPIO_PinInterruptCallbackRegister(BATT_MAN_INT_PIN, UsbReconnection_GPIOCallback, 0);
    
    // Enable Change Notice interrupt for RA4
    BATT_MAN_INT_InterruptEnable();
    
    // Read initial INT state
    bool intState = BATT_MAN_INT_Get();
    LOG_D("UsbReconnection_Init: INT pin enabled on RA4, initial state = %d", intState);
}

void UsbReconnection_Task(void) {
    // Get current system time
    uint32_t currentTime = xTaskGetTickCount() * portTICK_PERIOD_MS;
    
    // Method 1: Check if BQ24297 INT was triggered
    if (gReconnectionState.intTriggered) {
        // Debounce the interrupt
        if (currentTime - gReconnectionState.intTriggerTime >= INT_DEBOUNCE_TIME_MS) {
            LOG_D("UsbReconnection_Task: Processing INT trigger after debounce");
            
            // Update BQ24297 status to see what changed
            BQ24297_UpdateStatus();
            
            // Get power data
            tPowerData* pPowerData = BoardData_Get(BOARDDATA_POWER_DATA, 0);
            if (pPowerData) {
                // Log the interrupt status for debugging
                LOG_D("UsbReconnection_Task: INT - pgStat=%d, vBusStat=0x%02X, otg=%d, chgStat=%d",
                      pPowerData->BQ24297Data.status.pgStat,
                      pPowerData->BQ24297Data.status.vBusStat,
                      pPowerData->BQ24297Data.status.otg,
                      pPowerData->BQ24297Data.status.chgStat);
                
                // Check if OTG GPIO is enabled (HIGH = enabled)
                bool otgGpioState = BATT_MAN_OTG_Get();
                LOG_D("UsbReconnection_Task: OTG GPIO state = %d", otgGpioState);
                
                // In OTG mode, pgStat won't go high even if USB is connected
                if (pPowerData->BQ24297Data.status.otg || otgGpioState) {
                    // Check multiple indicators for USB connection
                    // vBusStat: 0x01=USB host, 0x02=Adapter, 0x03=OTG
                    if (pPowerData->BQ24297Data.status.vBusStat == 0x01 || 
                        pPowerData->BQ24297Data.status.vBusStat == 0x02) {
                        // Check cooldown before declaring reconnection
                        if (!gReconnectionState.usbReconnected || 
                            (currentTime - gReconnectionState.lastReconnectTime) >= RECONNECT_COOLDOWN_MS) {
                            // USB or adapter detected while in OTG mode
                            LOG_E("UsbReconnection_Task: USB reconnected! (vBusStat=0x%02X, otg=1)",
                                  pPowerData->BQ24297Data.status.vBusStat);
                            gReconnectionState.usbReconnected = true;
                            gReconnectionState.lastReconnectTime = currentTime;
                        }
                    } else if (pPowerData->BQ24297Data.status.chgStat != 0) {
                        // Charging status changed - might indicate USB connection
                        LOG_D("UsbReconnection_Task: Charge status changed in OTG mode, checking USB");
                        gReconnectionState.otgTogglePending = true;
                    }
                } else if (pPowerData->BQ24297Data.status.pgStat) {
                    // Check cooldown before declaring reconnection
                    if (!gReconnectionState.usbReconnected || 
                        (currentTime - gReconnectionState.lastReconnectTime) >= RECONNECT_COOLDOWN_MS) {
                        // Not in OTG mode and power is good - USB is connected!
                        LOG_E("UsbReconnection_Task: USB reconnected! (pgStat=1, otg=0)");
                        gReconnectionState.usbReconnected = true;
                        gReconnectionState.lastReconnectTime = currentTime;
                    }
                }
            }
            
            gReconnectionState.intTriggered = false;
        }
    }
    
    // Method 2: Check USB stack state
    // Only check if we haven't already detected a reconnection recently
    if (!gReconnectionState.usbReconnected || 
        (currentTime - gReconnectionState.lastReconnectTime) >= RECONNECT_COOLDOWN_MS) {
        CheckUsbStackState();
    }
    
    // Method 3: Conditional OTG toggle (last resort)
    if (gReconnectionState.otgTogglePending && !gReconnectionState.usbReconnected) {
        // Only toggle if enough time has passed since last check
        if (currentTime - gReconnectionState.lastCheckTime >= OTG_TOGGLE_CHECK_INTERVAL_MS) {
            LOG_D("UsbReconnection_Task: Performing safe OTG toggle check");
            if (SafeOtgToggleCheck()) {
                gReconnectionState.usbReconnected = true;
                gReconnectionState.lastReconnectTime = currentTime;
            }
            gReconnectionState.lastCheckTime = currentTime;
            gReconnectionState.otgTogglePending = false;
        }
    }
}

bool UsbReconnection_IsDetected(void) {
    // Add cooldown period to prevent multiple detections
    uint32_t currentTime = xTaskGetTickCount() * portTICK_PERIOD_MS;
    if (gReconnectionState.usbReconnected && 
        (currentTime - gReconnectionState.lastReconnectTime) < RECONNECT_COOLDOWN_MS) {
        // Still in cooldown period, ignore
        return false;
    }
    return gReconnectionState.usbReconnected;
}

void UsbReconnection_Clear(void) {
    LOG_D("UsbReconnection_Clear: Clearing reconnection flag");
    gReconnectionState.usbReconnected = false;
}

void UsbReconnection_HandleInterrupt(void) {
    // Called from ISR - keep it short!
    // Just record that INT was triggered, process in task context
    if (!gReconnectionState.intTriggered) {
        gReconnectionState.intTriggered = true;
        gReconnectionState.intTriggerTime = xTaskGetTickCountFromISR() * portTICK_PERIOD_MS;
    }
}

static void CheckUsbStackState(void) {
    // DISABLED: This check causes false positives when in OTG mode
    // The USB stack reports activity even when no external USB is connected
    // We rely on BQ24297 interrupt-based detection instead
    return;
    
    /* Original code commented out to prevent false detections:
    // Check if USB stack indicates connection
    tRunTimeUsbSettings* pUsbSettings = UsbCdc_GetRuntimeSettings();
    if (pUsbSettings) {
        // Check for any USB activity indicating connection
        bool usbActive = (pUsbSettings->state == USB_DEVICE_STATE_CONFIGURED) ||
                        (pUsbSettings->state == USB_DEVICE_STATE_ATTACHED) ||
                        pUsbSettings->isVbusDetected;
        
        if (usbActive) {
            // USB shows activity - check if we're in OTG mode
            tPowerData* pPowerData = BoardData_Get(BOARDDATA_POWER_DATA, 0);
            if (pPowerData && pPowerData->BQ24297Data.status.otg) {
                // Check if we're in cooldown period
                uint32_t currentTime = xTaskGetTickCount() * portTICK_PERIOD_MS;
                if (!gReconnectionState.usbReconnected || 
                    (currentTime - gReconnectionState.lastReconnectTime) >= RECONNECT_COOLDOWN_MS) {
                    // USB active while in OTG mode - USB must be reconnected!
                    LOG_E("UsbReconnection: USB activity detected in OTG mode - reconnection detected!");
                    LOG_E("UsbReconnection: USB state=%d, vbus=%d", 
                          pUsbSettings->state, pUsbSettings->isVbusDetected);
                    gReconnectionState.usbReconnected = true;
                    gReconnectionState.lastReconnectTime = currentTime;
                }
            }
        }
    }
    */
}

static bool SafeOtgToggleCheck(void) {
    bool usbDetected = false;
    
    // Get power data
    tPowerData* pPowerData = BoardData_Get(BOARDDATA_POWER_DATA, 0);
    if (!pPowerData || !pPowerData->BQ24297Data.status.otg) {
        // Not in OTG mode, no need to toggle
        return false;
    }
    
    LOG_D("SafeOtgToggleCheck: Checking for USB by reading vBusStat");
    
    // First, just check vBusStat without disabling OTG
    // This is safer than toggling OTG which risks power loss
    BQ24297_UpdateStatus();
    
    // vBusStat values: 0x00=Unknown, 0x01=USB host, 0x02=Adapter, 0x03=OTG
    if (pPowerData->BQ24297Data.status.vBusStat == 0x01 || 
        pPowerData->BQ24297Data.status.vBusStat == 0x02) {
        LOG_E("SafeOtgToggleCheck: USB/Adapter detected via vBusStat=0x%02X",
              pPowerData->BQ24297Data.status.vBusStat);
        usbDetected = true;
    } else {
        // As a last resort, check if USB stack shows activity
        tRunTimeUsbSettings* pUsbSettings = UsbCdc_GetRuntimeSettings();
        if (pUsbSettings && (pUsbSettings->state != USB_DEVICE_STATE_DETACHED)) {
            LOG_E("SafeOtgToggleCheck: USB stack active, assuming USB connected");
            usbDetected = true;
        }
    }
    
    return usbDetected;
}

// GPIO callback for BQ24297 INT pin
static void UsbReconnection_GPIOCallback(GPIO_PIN pin, uintptr_t context) {
    // INT pin changed state - BQ24297 has something to report
    // Note: INT is active low and goes low on any status change
    // It stays low until registers are read
    
    // Always handle the interrupt regardless of pin state
    // The INT pin might already be back high by the time we read it
    UsbReconnection_HandleInterrupt();
}