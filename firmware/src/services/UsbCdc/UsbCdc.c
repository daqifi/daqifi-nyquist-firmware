/* USB event handlers run in ISR context. LOG_E/LOG_I/LOG_D are ISR-aware
 * and automatically defer to a queue — safe to use here. Format args are
 * ignored in ISR context (use static strings). See issue #191. */
#define LOG_LVL LOG_LEVEL_USB
#define LOG_MODULE LOG_MODULE_USB
#include "UsbCdc.h"
#include "services/streaming_profile.h"  // PB_PROFILE_COUNTERS gate +
                                          // accumulator hook declarations.
                                          // Avoid pulling in full streaming.h.

#if PB_PROFILE_COUNTERS
#include <xc.h>  // _CP0_GET_COUNT()
// CP0 timestamp captured at USB_DEVICE_CDC_Write() success, consumed at
// WRITE_COMPLETE.  Single-writer / single-reader pattern: write from the
// task-context CircularBufferToUsbWrite path, read from the ISR-context
// WRITE_COMPLETE callback.  32-bit reads/writes are atomic on PIC32MZ.
static volatile uint32_t gUsbWriteStartCycles;

// #388: called from Streaming_ClearStats() so a transfer-in-flight at
// the clear boundary doesn't leak its pre-clear interval into the new
// session's usbDmaPendingCycles accumulator.  After reset, the next
// WRITE_COMPLETE will skip the accumulate (startCycles == 0 branch in
// UsbCdc_FinalizeWrite).
void UsbCdc_Profile_ResetPendingStamp(void) {
    gUsbWriteStartCycles = 0;
}
#endif

// libraries
#include "libraries/microrl/src/microrl.h"
#include "libraries/scpi/libscpi/inc/scpi/scpi.h"

// services
#include "services/SCPI/SCPIInterface.h"
#include "Util/Logger.h"
#include "Util/StreamingBufferPool.h"
#include "Util/CoherentPool.h"
#include "HAL/BQ24297/BQ24297.h"
#include "state/data/BoardData.h"
#include "config/default/driver/usb/usbhs/src/plib_usbhs_header.h"

#define LOG_LEVEL_LOCAL 'D'
#define UNUSED(x) (void)(x)

/**
 * Finalizes a write operation by clearing the buffer for additional content 
 */
static UsbCdcData_t gRunTimeUsbSttings __attribute__((coherent));
static bool UsbCdc_FinalizeWrite(UsbCdcData_t* client);

/**
 * Filters input characters to reject potentially dangerous control characters
 * Used only in normal command mode, NOT in transparent mode
 * 
 * Security rationale:
 * - Allows specific safe ESC sequences (arrow keys, home/end)
 * - Blocks dangerous terminal manipulation sequences
 * - Protects against injection of non-printable characters
 * 
 * @param ch Character to check
 * @return true if character is safe to process, false if it should be rejected
 */
static bool UsbCdc_IsCharacterSafe(UsbCdcData_t* client, uint8_t ch) {
    // State machine for ESC sequence parsing
    switch (client->escapeState) {
        case ESC_STATE_ESC:
            if (ch == '[') {
                client->escapeState = ESC_STATE_BRACKET;
                return true;  // Allow ESC[
            } else {
                client->escapeState = ESC_STATE_NONE;
                return false; // Reject other ESC sequences
            }
            
        case ESC_STATE_BRACKET:
            // Allow specific sequences after ESC[
            switch (ch) {
                case 'A':  // Up arrow
                case 'B':  // Down arrow
                case 'C':  // Right arrow
                case 'D':  // Left arrow
                case 'H':  // Home (not supported by microrl)
                case 'F':  // End (not supported by microrl)
                    client->escapeState = ESC_STATE_NONE;
                    return true;
                default:
                    client->escapeState = ESC_STATE_NONE;
                    return false; // Reject other ESC[ sequences
            }
            
        default:  // ESC_STATE_NONE
            // Allow printable ASCII characters (space through ~)
            if (ch >= 0x20 && ch <= 0x7E) {
                return true;
            }
            
            // Allow specific control characters
            switch (ch) {
                case '\r':   // Carriage return (0x0D) - end of command
                case '\n':   // Line feed (0x0A) - end of command
                case '\t':   // Tab (0x09) - may be used in commands
                case '\b':   // Backspace (0x08) - command editing
                case 0x7F:   // DEL (127) - delete character
                case 0x1B:   // ESC (27) - start of escape sequence
                    if (ch == 0x1B) {
                        client->escapeState = ESC_STATE_ESC;
                    }
                    return true;
                case 0x10:   // Ctrl+P (DLE) - command history up
                case 0x0E:   // Ctrl+N (SO) - command history down
                    return true;
                default:
                    // Reject other control characters
                    return false;
            }
    }
}

__WEAK void UsbCdc_SleepStateUpdateCB(bool state) {
    UNUSED(state);
}

__WEAK bool UsbCdc_TransparentReadCmpltCB(uint8_t* pBuff, size_t buffLen) {
    return true;
}

USB_DEVICE_CDC_EVENT_RESPONSE UsbCdc_CDCEventHandler
(
        USB_DEVICE_CDC_INDEX index,
        USB_DEVICE_CDC_EVENT event,
        void * pData,
        uintptr_t userData
        ) {
    UsbCdcData_t * pUsbCdcDataObject;
    pUsbCdcDataObject = (UsbCdcData_t *) userData;
    USB_CDC_CONTROL_LINE_STATE * controlLineStateData;

    switch (event) {
        case USB_DEVICE_CDC_EVENT_GET_LINE_CODING:

            /* This means the host wants to know the current line
             * coding. This is a control transfer request. Use the
             * USB_DEVICE_ControlSend() function to send the data to
             * host.  */

            USB_DEVICE_ControlSend(pUsbCdcDataObject->deviceHandle,
                    &pUsbCdcDataObject->deviceLineCodingData, sizeof (USB_CDC_LINE_CODING));

            break;

        case USB_DEVICE_CDC_EVENT_SET_LINE_CODING:

            /* This means the host wants to set the line coding.
             * This is a control transfer request. Use the
             * USB_DEVICE_ControlReceive() function to receive the
             * data from the host */

            USB_DEVICE_ControlReceive(pUsbCdcDataObject->deviceHandle,
                    &pUsbCdcDataObject->hostSetLineCodingData, sizeof (USB_CDC_LINE_CODING));

            break;

        case USB_DEVICE_CDC_EVENT_SET_CONTROL_LINE_STATE:

            /* This means the host is setting the control line state.
             * Read the control line state. We will accept this request
             * for now. */

            controlLineStateData = (USB_CDC_CONTROL_LINE_STATE *) pData;
            pUsbCdcDataObject->controlLineStateData.dtr = controlLineStateData->dtr;
            pUsbCdcDataObject->controlLineStateData.carrier = controlLineStateData->carrier;

            if (pUsbCdcDataObject->controlLineStateData.dtr == 0) {
                if (gRunTimeUsbSttings.state == USB_CDC_STATE_PROCESS) {
                    gRunTimeUsbSttings.state = USB_CDC_STATE_WAIT;
                }
                gRunTimeUsbSttings.isCdcHostConnected=0;
            } else {
                gRunTimeUsbSttings.state = USB_CDC_STATE_PROCESS;
                gRunTimeUsbSttings.isCdcHostConnected=1;
            }

            USB_DEVICE_ControlStatus(pUsbCdcDataObject->deviceHandle, USB_DEVICE_CONTROL_STATUS_OK);

            break;

        case USB_DEVICE_CDC_EVENT_SEND_BREAK:

            /* This means that the host is requesting that a break of the
             * specified duration be sent. Read the break duration */

            pUsbCdcDataObject->breakData = ((USB_DEVICE_CDC_EVENT_DATA_SEND_BREAK *) pData)->breakDuration;
            break;

        case USB_DEVICE_CDC_EVENT_READ_COMPLETE:
        {
            /* This means that the host has sent some data*/
            USB_DEVICE_CDC_EVENT_DATA_READ_COMPLETE* readResult = (USB_DEVICE_CDC_EVENT_DATA_READ_COMPLETE*) pData;
            pUsbCdcDataObject->readBufferLength = readResult->length;
            pUsbCdcDataObject->readTransferHandle = USB_DEVICE_CDC_TRANSFER_HANDLE_INVALID;
            break;
        }
        case USB_DEVICE_CDC_EVENT_CONTROL_TRANSFER_DATA_RECEIVED:

            /* The data stage of the last control transfer is
             * complete. For now we accept all the data */

            USB_DEVICE_ControlStatus(pUsbCdcDataObject->deviceHandle, USB_DEVICE_CONTROL_STATUS_OK);
            break;

        case USB_DEVICE_CDC_EVENT_CONTROL_TRANSFER_DATA_SENT:

            /* This means the GET LINE CODING function data is valid. We dont
             * do much with this data in this demo. */
            break;

        case USB_DEVICE_CDC_EVENT_WRITE_COMPLETE:
        {
            /* This means that the data write got completed. We can schedule
             * the next write. */
            USB_DEVICE_CDC_EVENT_DATA_WRITE_COMPLETE val = *(USB_DEVICE_CDC_EVENT_DATA_WRITE_COMPLETE*) (pData);
            if (val.handle == pUsbCdcDataObject->writeTransferHandle) {
                // Log warning if actual transferred length differs from requested
                if (val.length != pUsbCdcDataObject->writeBufferLength) {
                    LOG_E_ONCE(LOG_ONCE_USB_WRITE_MISMATCH, "USB write length mismatch");
                }
                // Always finalize to prevent stuck state, even on partial write
                UsbCdc_FinalizeWrite(pUsbCdcDataObject);
            }
            break;
        }
        default:
            break;
    }

    return USB_DEVICE_CDC_EVENT_RESPONSE_NONE;
}

/***********************************************
 * Application USB Device Layer Event Handler.
 ***********************************************/
void UsbCdc_EventHandler(USB_DEVICE_EVENT event, void * eventData, uintptr_t context) {
    USB_DEVICE_EVENT_DATA_CONFIGURED *configuredEventData;

    switch (event) {
        case USB_DEVICE_EVENT_DECONFIGURED:
        case USB_DEVICE_EVENT_RESET:
            gRunTimeUsbSttings.isConfigured = false;
            // Cable unplug / re-enumeration produces DECONFIGURED/RESET but NOT
            // a DTR-deassert SET_CONTROL_LINE_STATE, so the host-connected flag
            // would otherwise stay set after the host is gone — and
            // NanoPB_Encoder reads it directly for the streaming device_status
            // "USB connected" bit. Clear it on real teardown (stale-global audit).
            gRunTimeUsbSttings.isCdcHostConnected = 0;
            if (gRunTimeUsbSttings.deviceHandle != USB_DEVICE_HANDLE_INVALID) {
                gRunTimeUsbSttings.state = USB_CDC_STATE_BEGIN_CLOSE;
            }
#if PB_PROFILE_COUNTERS
            // #388: USB layer is tearing down — any in-flight DMA
            // transfer's WRITE_COMPLETE may fire after this point (or
            // not at all).  Clear the pending-cycles start timestamp so
            // a delayed event after the disconnect doesn't accumulate
            // disconnect-duration into the next session's counter.
            UsbCdc_Profile_ResetPendingStamp();
#endif
            break;
        case USB_DEVICE_EVENT_CONFIGURED:

            /* Check the configuration. We only support configuration 1 */
            configuredEventData = (USB_DEVICE_EVENT_DATA_CONFIGURED*) eventData;
            if (configuredEventData->configurationValue == 1) {
                /* Register the CDC Device application event handler here.
                 * Note how the usbCdcData object pointer is passed as the
                 * user data */
                USB_DEVICE_CDC_EventHandlerSet(
                        USB_DEVICE_CDC_INDEX_0,
                        UsbCdc_CDCEventHandler,
                        (uintptr_t) & gRunTimeUsbSttings);

                /* Mark that the device is now configured */
                gRunTimeUsbSttings.state = USB_CDC_STATE_WAIT;
                gRunTimeUsbSttings.isConfigured = true;
            }
            break;

        case USB_DEVICE_EVENT_POWER_DETECTED:

            /* VBUS was detected. This callback runs in the USB device task context,
             * so vTaskDelay yields CPU to other tasks (does not block the system).
             *
             * 100ms delay: lets the RC low-pass filter on the VBUS sense line
             * settle before we attach. Previously 1000ms to wait for BQ24297
             * DPDM detection; reduced now that ManageIINLIM handles IINLIM
             * independently via I2C.
             *
             * Re-check isVbusDetected after the delay in case POWER_REMOVED
             * fired while we were waiting (cable yanked during settle). */
            gRunTimeUsbSttings.isVbusDetected = true;
            gRunTimeUsbSttings.isConfigured = false;

            vTaskDelay(pdMS_TO_TICKS(100));
            if (gRunTimeUsbSttings.isVbusDetected &&
                gRunTimeUsbSttings.deviceHandle != USB_DEVICE_HANDLE_INVALID) {
                USB_DEVICE_Attach(gRunTimeUsbSttings.deviceHandle);
            }
            break;

        case USB_DEVICE_EVENT_POWER_REMOVED:
        {
            /* VBUS Sag vs Real Removal Detection:
             *
             * Power sag: Internal power rail transitions can cause VBUS to
             * momentarily sag from ~5V to ~4V, triggering a false removal event.
             * VBUS recovers within ~20ms for sag events.
             *
             * Real removal: The low-pass RC filter on VBUS into the PIC32 causes
             * the voltage to discharge over 20-50ms when power is unplugged.
             *
             * Solution: If VBUS is still elevated, wait 50ms for the RC filter
             * to discharge and re-check. If VBUS recovered, it was a sag. If
             * VBUS dropped, it was a real removal. */
            USBHS_VBUS_LEVEL vbusLevel = PLIB_USBHS_VBUSLevelGet(USBHS_ID_0);

            if (vbusLevel >= USBHS_VBUS_BELOW_VBUSVALID) {
                /* VBUS still elevated - wait for RC filter discharge */
                vTaskDelay(pdMS_TO_TICKS(50));
                vbusLevel = PLIB_USBHS_VBUSLevelGet(USBHS_ID_0);

                if (vbusLevel >= USBHS_VBUS_BELOW_VBUSVALID) {
                    /* VBUS recovered - power sag, not real removal */
                    break;
                }
            }

            /* VBUS truly removed */
            gRunTimeUsbSttings.isVbusDetected = false;
            gRunTimeUsbSttings.isConfigured = false;
            gRunTimeUsbSttings.isCdcHostConnected = 0;  // host gone (stale-global audit)
            if (gRunTimeUsbSttings.deviceHandle != USB_DEVICE_HANDLE_INVALID) {
                USB_DEVICE_Detach(gRunTimeUsbSttings.deviceHandle);
            }
            break;
        }

        case USB_DEVICE_EVENT_SUSPENDED:
            // Transfer handles become invalid during suspend - host stops all transfers
            // Current implementation transitions to WAIT state which will re-establish transfers
            UsbCdc_SleepStateUpdateCB(true);
            gRunTimeUsbSttings.state = USB_CDC_STATE_WAIT;
            break;
        case USB_DEVICE_EVENT_RESUMED:
            // Suspend/resume only occurs after device is configured and enumerated
            UsbCdc_SleepStateUpdateCB(false);
            gRunTimeUsbSttings.state = USB_CDC_STATE_WAIT;
            break;
        case USB_DEVICE_EVENT_ERROR:
            // Some errors may be non-fatal (transient bus errors, CRC errors)
            // but conservative approach is to reset the interface
            gRunTimeUsbSttings.isConfigured = false;
            gRunTimeUsbSttings.isCdcHostConnected = 0;  // host link reset (stale-global audit)
            gRunTimeUsbSttings.state = USB_CDC_STATE_BEGIN_CLOSE;
            break;
        default:
            break;
    }
}

int UsbCdc_Wrapper_Write(uint8_t* buf, uint32_t len) {
    // Validate length against buffer size to prevent overflow
    if (len == 0 || len > gRunTimeUsbSttings.dmaWriteBufferSize) {
        LOG_D("USB write: invalid length %lu", (unsigned long)len);
        return -1;  // Invalid length
    }

    // Validate buffer pointer to prevent null dereference
    if (buf == NULL) {
        LOG_E("USB write: NULL buffer");
        return -1;  // Invalid buffer pointer
    }

    // Ensure device is configured before attempting write
    if (gRunTimeUsbSttings.state != USB_CDC_STATE_PROCESS) {
        LOG_D("USB write: not ready (state=%d)", gRunTimeUsbSttings.state);
        return -1;  // USB not configured/ready
    }

    // Begin atomic section to prevent race conditions with concurrent writes
    taskENTER_CRITICAL();

    // Check if previous write is still pending to prevent buffer corruption
    if (gRunTimeUsbSttings.writeTransferHandle != USB_DEVICE_CDC_TRANSFER_HANDLE_INVALID) {
        taskEXIT_CRITICAL();
        LOG_D("USB write: previous transfer pending");
        return -1;  // Previous write still in progress
    }

    // Prepare buffer while in atomic section to prevent another task from corrupting it
    memcpy(gRunTimeUsbSttings.dmaWriteBuffer, buf, (size_t)len);
    gRunTimeUsbSttings.writeBufferLength = len;
    gRunTimeUsbSttings.writeTransferHandle = USB_DEVICE_CDC_TRANSFER_HANDLE_INVALID;

    taskEXIT_CRITICAL();

    // Call USB driver outside critical section (may block/take time)
    USB_DEVICE_CDC_RESULT writeResult = USB_DEVICE_CDC_Write(USB_DEVICE_CDC_INDEX_0,
            &gRunTimeUsbSttings.writeTransferHandle,
            gRunTimeUsbSttings.dmaWriteBuffer,
            len,
            USB_DEVICE_CDC_TRANSFER_FLAGS_DATA_COMPLETE);

#if PB_PROFILE_COUNTERS
    // #388: stamp the start time only on successful submission so a
    // failed Write() doesn't pollute the pending-cycles accumulator.
    // Race note: WRITE_COMPLETE ISR can fire before this store retires,
    // but that's OK — the ISR reads writeTransferHandle invalid first
    // and skips the accumulator unless gUsbWriteStartCycles is nonzero
    // (the test below).  Single-writer single-reader pattern, no need
    // for a critical section on a 32-bit value.
    if (writeResult == USB_DEVICE_CDC_RESULT_OK) {
        gUsbWriteStartCycles = _CP0_GET_COUNT();
    }
#endif

    if (writeResult != USB_DEVICE_CDC_RESULT_OK) {
        LOG_E("USB CDC write API failed");
        // Write failed - ensure handle is invalid (atomic update)
        taskENTER_CRITICAL();
        gRunTimeUsbSttings.writeTransferHandle = USB_DEVICE_CDC_TRANSFER_HANDLE_INVALID;
        gRunTimeUsbSttings.writeBufferLength = 0;
        taskEXIT_CRITICAL();
#if PB_PROFILE_COUNTERS
        // #388: ensure no stale start cycles linger after a partial
        // failure (the conditional set above only runs on OK, but if
        // a prior successful write left a value and was canceled
        // before WRITE_COMPLETE could fire, this is the path that
        // would observe it next.)
        UsbCdc_Profile_ResetPendingStamp();
#endif
        return -1;
    }

    // Success: report bytes accepted for transfer
    // Note: Don't check if handle is still valid here - the write complete interrupt
    // can fire so fast that UsbCdc_FinalizeWrite already reset it to INVALID
    return (int)len;
}

/**
 * Enqueues client data for writing 
 */
static bool UsbCdc_BeginWrite(UsbCdcData_t* client) {

    USB_DEVICE_CDC_RESULT writeResult = USB_DEVICE_CDC_RESULT_OK;

    if (client->state != USB_CDC_STATE_PROCESS) {
        LOG_D("USB BeginWrite: not in PROCESS state");
        return false;
    }

    // make sure there is no write transfer in progress
    if (client->writeTransferHandle == USB_DEVICE_CDC_TRANSFER_HANDLE_INVALID) {
        xSemaphoreTake(client->wMutex, portMAX_DELAY);
        if (CircularBuf_NumBytesAvailable(&client->wCirbuf) > 0) {
#if PB_PROFILE_COUNTERS
            uint32_t dcStart = _CP0_GET_COUNT();
            CircularBuf_ProcessBytes(&client->wCirbuf, NULL, client->dmaWriteBufferSize, &writeResult);
            Streaming_AddProfileSample_DmaCopy(_CP0_GET_COUNT() - dcStart);
#else
            CircularBuf_ProcessBytes(&client->wCirbuf, NULL, client->dmaWriteBufferSize, &writeResult);
#endif
        } else {
            // No data to write, return true (success - nothing to do)
            xSemaphoreGive(client->wMutex);
            return true;
        }
        xSemaphoreGive(client->wMutex);

        // CircularBuffer callback now returns bytes written (>= 0) on success, < 0 on error
        // Handle errors
        if (writeResult < 0) {
            switch (writeResult) {
                case USB_DEVICE_CDC_RESULT_ERROR_INSTANCE_NOT_CONFIGURED:
                case USB_DEVICE_CDC_RESULT_ERROR_INSTANCE_INVALID:
                case USB_DEVICE_CDC_RESULT_ERROR_PARAMETER_INVALID:
                case USB_DEVICE_CDC_RESULT_ERROR_ENDPOINT_HALTED:
                case USB_DEVICE_CDC_RESULT_ERROR_TERMINATED_BY_HOST:
                    // Reset the interface
                    gRunTimeUsbSttings.state = USB_CDC_STATE_BEGIN_CLOSE;
                    return false;

                case USB_DEVICE_CDC_RESULT_ERROR_TRANSFER_SIZE_INVALID: // Bad input (GIGO)
                    SYS_DEBUG_MESSAGE(SYS_ERROR_ERROR, "Bad USB write size");
                    return false;
                case USB_DEVICE_CDC_RESULT_ERROR_TRANSFER_QUEUE_FULL: // Too many pending requests. Just wait.
                case USB_DEVICE_CDC_RESULT_ERROR: // Concurrency issue. Just wait.
                default:
                    // No action
                    return false;
            }
        }

        // Success: callback returned bytes written (>= 0)
        return true;
    }

    // writeTransferHandle != INVALID — prior DMA still in flight.  See
    // the USB_CDC_STATE_PROCESS caller for the actual idle-count
    // instrumentation (it gates BeginWrite on the same handle check, so
    // counting here is unreachable — Qodo finding #3).
    return false;
}

/**
 * Waits for a write operation to complete
 */
static bool UsbCdc_WaitForWrite(UsbCdcData_t* client) {
    if (client->state != USB_CDC_STATE_PROCESS) {
        return false;
    }

    while (client->writeTransferHandle != USB_DEVICE_CDC_TRANSFER_HANDLE_INVALID) {
        if (client->state != USB_CDC_STATE_PROCESS) {
            return false;
        }

        vTaskDelay(100);
    }

    return true;
}

/**
 * Finalizes a write operation by clearing the buffer for additional content 
 */
static bool UsbCdc_FinalizeWrite(UsbCdcData_t* client) {
#if PB_PROFILE_COUNTERS
    // #388: accumulate wire-time per DMA transfer.  This runs in ISR
    // context (WRITE_COMPLETE event) — accumulator uses FROM_ISR variant.
    // Guard against gUsbWriteStartCycles being zero (which means the
    // start was never captured, e.g. on initial pre-stream FinalizeWrite
    // calls — produces a single junk reading otherwise).
    uint32_t startCycles = gUsbWriteStartCycles;
    if (startCycles != 0) {
        Streaming_AddProfileSample_DmaPending_FromISR(_CP0_GET_COUNT() - startCycles);
        gUsbWriteStartCycles = 0;
    }
#endif
    client->writeTransferHandle = USB_DEVICE_CDC_TRANSFER_HANDLE_INVALID;
    client->writeBufferLength = 0;
    return true;
}

/**
 * Starts listening for a read
 */
static bool UsbCdc_BeginRead(UsbCdcData_t* client) {
    if (client->state != USB_CDC_STATE_PROCESS) {
        return false;
    }
    // Schedule the next read
    if (gRunTimeUsbSttings.readTransferHandle ==
            USB_DEVICE_CDC_TRANSFER_HANDLE_INVALID &&
            gRunTimeUsbSttings.readBufferLength == 0) {
        USB_DEVICE_CDC_RESULT readResult = USB_DEVICE_CDC_Read(
                USB_DEVICE_CDC_INDEX_0,
                &gRunTimeUsbSttings.readTransferHandle,
                client->readBuffer, USBCDC_RBUFFER_SIZE);

        switch (readResult) {
            case USB_DEVICE_CDC_RESULT_OK:
                // Normal operation
                break;

            case USB_DEVICE_CDC_RESULT_ERROR_INSTANCE_NOT_CONFIGURED:
            case USB_DEVICE_CDC_RESULT_ERROR_INSTANCE_INVALID:
            case USB_DEVICE_CDC_RESULT_ERROR_PARAMETER_INVALID:
            case USB_DEVICE_CDC_RESULT_ERROR_ENDPOINT_HALTED:
            case USB_DEVICE_CDC_RESULT_ERROR_TERMINATED_BY_HOST:
                // Reset the interface
                gRunTimeUsbSttings.state = USB_CDC_STATE_BEGIN_CLOSE;
                return false;

            case USB_DEVICE_CDC_RESULT_ERROR_TRANSFER_SIZE_INVALID: // Bad input (GIGO)
                SYS_DEBUG_MESSAGE(SYS_ERROR_ERROR, "Bad USB read size");
                return false;
            case USB_DEVICE_CDC_RESULT_ERROR_TRANSFER_QUEUE_FULL: // Too many pending requests. Just wait.
            case USB_DEVICE_CDC_RESULT_ERROR: // Concurrency issue. Just wait. 
            default:
                // No action
                return false;
        }

        if (gRunTimeUsbSttings.readTransferHandle ==
                USB_DEVICE_CDC_TRANSFER_HANDLE_INVALID) {
            SYS_DEBUG_MESSAGE(SYS_ERROR_ERROR, "Non-error w/ invalid transfer handle");
            return false;
        }
    }

    return true;

}

/**
 * Waits for a read operation to complete
 */
static bool UsbCdc_WaitForRead(UsbCdcData_t* client) {
    if (client->state != USB_CDC_STATE_PROCESS) {
        return false;
    }

    while (client->readTransferHandle != USB_DEVICE_CDC_TRANSFER_HANDLE_INVALID) {
        if (client->state != USB_CDC_STATE_PROCESS) {
            return false;
        }

        vTaskDelay(100);
    }

    return true;
}

/**
 * Case-insensitive byte-by-byte compare. Returns true if all n bytes match
 * (treating ASCII A-Z and a-z as equal).
 */
static bool UsbCdc_CaseInsensitiveMatch(const char* p, const uint8_t* b, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        char pc = p[i];
        char bc = (char)b[i];
        if (pc >= 'A' && pc <= 'Z') pc = (char)(pc - 'A' + 'a');
        if (bc >= 'A' && bc <= 'Z') bc = (char)(bc - 'A' + 'a');
        if (pc != bc) return false;
    }
    return true;
}

/**
 * SCPI-aware keyword match for the transparent-mode escape detector.
 *
 * Matches libscpi's `matchPattern` semantics (see firmware/src/libraries/scpi/
 * libscpi/src/utils.c around line 478): for each colon/space-separated
 * keyword in the pattern, accept EITHER the full keyword OR the required
 * (leading-uppercase) prefix only — nothing in between. This is stricter
 * than IEEE 488.2's "continuous prefix of optional tail" interpretation
 * but matches libscpi exactly, so the escape detector never consumes
 * bytes that the SCPI parser would later reject (which would leave
 * transparent mode active and silently drop the user's bytes).
 *
 * Patterns with mid-keyword uppercase (e.g. "SetTransparentMode") are
 * handled correctly: only "S" (required prefix) and "SetTransparentMode"
 * (full) match — NOT "Set" or "SetTrans". libscpi's
 * patternSeparatorShortPos cuts the keyword at the first lowercase letter,
 * so "S" is the short form. We mirror that.
 *
 * @param pattern Canonical pattern, e.g. "SYSTem:USB:TRANSparent:MODE 0".
 *                ':' and ' ' are keyword separators; digits and other
 *                non-letter bytes are required literals.
 * @param buf     The raw read buffer (no NUL terminator required).
 * @param bufLen  Number of valid bytes in buf to consider.
 *
 * @return Number of buf bytes consumed by a successful match (>0), or 0
 *         if no valid SCPI abbreviation of pattern matches a prefix of buf.
 *
 * Examples for pattern "SYSTem:USB:TRANSparent:MODE 0":
 *   "SYST:USB:TRANS:MODE 0"           → matches (short form for each keyword)
 *   "SYSTem:USB:TRANSparent:MODE 0"   → matches (full form for each keyword)
 *   "syst:usb:trans:mode 0"           → matches (case-insensitive)
 *   "SYSTE:USB:TRANS:MODE 0"          → REJECTED (partial optional tail)
 *   "SYSTm:USB:TRANS:MODE 0"          → REJECTED (skipped 'e' but used 'm')
 */
/**
 * Separator characters between SCPI keywords and between header and
 * parameter. We accept ' ' but NOT '\t' here, even though libscpi's
 * lexer treats them as equivalent whitespace, because USB CDC packets
 * may be delivered in chunks at TAB boundaries (the host driver flushes
 * on whitespace) — that splits "cmd\t0\r" across two reads, and the
 * detector is stateless per-call so a fragment never matches. Users
 * who need tab-separated SCPI must use normal mode (where microrl
 * accumulates input across reads). The full PuTTY workflow in the
 * utilities README only uses space, so this is a non-issue in practice.
 */
static bool UsbCdc_IsScpiSeparator(char c) {
    return c == ':' || c == ' ';
}

static size_t UsbCdc_ScpiKeywordMatch(const char* pattern, const uint8_t* buf, size_t bufLen) {
    size_t pi = 0;
    size_t bi = 0;

    while (pattern[pi] != '\0') {
        char pc = pattern[pi];
        bool is_keyword_start = (pc >= 'A' && pc <= 'Z') || (pc >= 'a' && pc <= 'z');

        if (!is_keyword_start) {
            // Required literal byte (':', ' ', digit, etc.) — must match exactly.
            if (bi >= bufLen) return 0;
            if (buf[bi] != pc) return 0;
            ++pi; ++bi;
            continue;
        }

        // Find end of this keyword in the pattern (next ':' / ' ' / '\0').
        // (Pattern strings here are author-controlled and use ' ', not '\t'.)
        size_t kw_start = pi;
        while (pattern[pi] != '\0' && pattern[pi] != ':' && pattern[pi] != ' ') {
            ++pi;
        }
        size_t kw_full_len = pi - kw_start;

        // Short form = leading uppercase run (mirrors libscpi's
        // patternSeparatorShortPos: stop at first lowercase letter).
        size_t kw_short_len = 0;
        while (kw_short_len < kw_full_len &&
               pattern[kw_start + kw_short_len] >= 'A' &&
               pattern[kw_start + kw_short_len] <= 'Z') {
            ++kw_short_len;
        }
        if (kw_short_len == 0) {
            // Pattern keyword without leading uppercase — reject (malformed).
            return 0;
        }

        // After matching, the next byte in buf must be a keyword separator
        // (':' or ' ' — see UsbCdc_IsScpiSeparator), or end-of-input, or a
        // line terminator ('\r', '\n'). Letter/digit immediately after means
        // buf has more characters than the matched form claimed —
        // partial-match rejection.
        bool matched = false;
        if (bufLen - bi >= kw_full_len &&
            UsbCdc_CaseInsensitiveMatch(&pattern[kw_start], &buf[bi], kw_full_len)) {
            char next = (bi + kw_full_len < bufLen) ? (char)buf[bi + kw_full_len] : '\0';
            if (UsbCdc_IsScpiSeparator(next) || next == '\0' || next == '\r' || next == '\n') {
                bi += kw_full_len;
                matched = true;
            }
        }
        if (!matched && bufLen - bi >= kw_short_len &&
            UsbCdc_CaseInsensitiveMatch(&pattern[kw_start], &buf[bi], kw_short_len)) {
            char next = (bi + kw_short_len < bufLen) ? (char)buf[bi + kw_short_len] : '\0';
            if (UsbCdc_IsScpiSeparator(next) || next == '\0' || next == '\r' || next == '\n') {
                bi += kw_short_len;
                matched = true;
            }
        }
        if (!matched) return 0;
    }
    return bi;
}

/**
 * Called to complete a read operation, feeding data to the rest of the system
 */
static bool UsbCdc_FinalizeRead(UsbCdcData_t* client) {
    // Escape-hatch command(s) accepted while transparent mode is active.
    // The SCPI parser is bypassed in transparent mode, so the alias table
    // can't help us here — the raw detector must accept BOTH the legacy
    // and the new canonical command names directly, including any valid
    // SCPI keyword abbreviations of either. Up to 2 terminating chars
    // (\r, \r\n) are tolerated after the matched command (PuTTY sends \r,
    // others send \r\n).
    //
    // Listed in canonical form; UsbCdc_ScpiKeywordMatch handles all valid
    // abbreviations and case variants per IEEE 488.2 keyword rules.
    static const char* const UNSET_TRANSPARENT_FORMS[] = {
        "SYSTem:USB:TRANSparent:MODE 0",  // canonical (#311 round 3)
        "SYSTem:USB:SetTransparentMode 0",  // legacy alias
    };

    if (client->readBufferLength > 0) {
        if (client->isTransparentModeActive == 0) {
            for (size_t i = 0; i < client->readBufferLength; ++i) {
                // Filter out potentially dangerous characters
                if (UsbCdc_IsCharacterSafe(client, client->readBuffer[i])) {
                    microrl_insert_char(&client->console, client->readBuffer[i]);
                } else {
                    // Silently reject unsafe characters to avoid information disclosure
                    // Character filtering is documented in the system design
                }
            }
            client->readBufferLength = 0;
            return true;
        } else {
            // IMPORTANT: Transparent mode should NOT filter characters.
            // This mode is used for raw binary data passthrough (e.g.,
            // firmware updates). Filtering would corrupt the data stream.
            //
            // Try each accepted escape form. The escape path triggers ONLY
            // when the matched command is followed by 0/1/2 actual CR/LF
            // bytes — checking length alone is unsafe in transparent mode
            // because any 2 random binary bytes following a coincidental
            // command-prefix would otherwise silently drop into the escape
            // path and be lost to the binary passthrough.
            for (size_t k = 0; k < (sizeof(UNSET_TRANSPARENT_FORMS) / sizeof(UNSET_TRANSPARENT_FORMS[0])); ++k) {
                size_t consumed = UsbCdc_ScpiKeywordMatch(
                    UNSET_TRANSPARENT_FORMS[k],
                    client->readBuffer, client->readBufferLength);
                if (consumed == 0) continue;

                size_t trailing = client->readBufferLength - consumed;
                // Require at least one explicit terminator: trailing==0 means
                // the keyword landed at end-of-buffer with no CR/LF, which is
                // not a real line. trailing>2 means more bytes follow than a
                // CR+LF pair — likely raw payload, not a clean exit.
                if (trailing == 0 || trailing > 2) continue;

                // Validate trailing bytes are actual terminators (CR or LF).
                bool terminators_ok = true;
                for (size_t t = 0; t < trailing; ++t) {
                    uint8_t b = client->readBuffer[consumed + t];
                    if (b != '\r' && b != '\n') {
                        terminators_ok = false;
                        break;
                    }
                }
                if (!terminators_ok) continue;

                // Match confirmed. Forward the matched bytes to the SCPI
                // parser via microrl, supplying canonical \n\r terminators
                // (the SCPI parser will then recognize the command — alias
                // or canonical, abbreviated or full — and route it to
                // SCPI_UsbSetTransparentMode).
                for (size_t i = 0; i < consumed; ++i) {
                    microrl_insert_char(&client->console, (char)client->readBuffer[i]);
                }
                microrl_insert_char(&client->console, '\n');
                microrl_insert_char(&client->console, '\r');
                client->readBufferLength = 0;
                return true;
            }
            if (UsbCdc_TransparentReadCmpltCB(client->readBuffer, client->readBufferLength) == true) {
                client->readBufferLength = 0;
                return true;
            }
        }
    }
    return false;

}

size_t UsbCdc_WriteBuffFreeSize(UsbCdcData_t* client) {
    if (client == NULL) {
        client = &gRunTimeUsbSttings;
    }
    if (gRunTimeUsbSttings.state != USB_CDC_STATE_PROCESS)
        return 0;
    
    // Must protect circular buffer access with mutex
    xSemaphoreTake(client->wMutex, portMAX_DELAY);
    size_t freeSize = CircularBuf_NumBytesFree(&client->wCirbuf);
    xSemaphoreGive(client->wMutex);
    
    return freeSize;
}

size_t UsbCdc_WriteToBuffer(UsbCdcData_t* client, const char* data, size_t len) {
#if PB_PROFILE_COUNTERS
    uint32_t wbStart = _CP0_GET_COUNT();
#endif
    if (client == NULL) {
        client = &gRunTimeUsbSttings;
    }

    if (len == 0) return 0;

    // Non-blocking check for buffer space
    // Use try-lock (timeout=0) to keep function truly non-blocking
    // If mutex is busy or buffer is full, return 0 immediately
    if (xSemaphoreTake(client->wMutex, 0) != pdTRUE) {
        return 0;  // Mutex busy, can't write now
    }

    // All-or-nothing: only write if full packet fits. Prevents convoy effect
    // where tiny partial writes burn CPU on mutex cycles. Callers retry via
    // Streaming_WriteWithRetry — no data loss.
    size_t currentFree = CircularBuf_NumBytesFree(&client->wCirbuf);
    if (currentFree < len) {
        xSemaphoreGive(client->wMutex);
        return 0;
    }

    size_t bytesAdded = CircularBuf_AddBytes(&client->wCirbuf, (uint8_t*) data, len);
    xSemaphoreGive(client->wMutex);

#if PB_PROFILE_COUNTERS
    // #388: only count successful writes (bytesAdded > 0) — failed/empty
    // writes are short-circuit returns that don't represent encoder→buffer
    // copy work.
    if (bytesAdded > 0) {
        Streaming_AddProfileSample_WriteBuf(_CP0_GET_COUNT() - wbStart);
    }
#endif
    return bytesAdded;
}

/*
size_t UsbCdc_WriteDefault(const char* data, size_t len)
{
    return UsbCdc_Write(&g_BoardRuntimeConfig.usbSettings, data, len);
}*/

/**
 * Flushes data from the provided client
 * @param client The client to flush
 * @return  True if data is flushed, false otherwise
 */
static bool UsbCdc_Flush(UsbCdcData_t* client) {
    return UsbCdc_BeginWrite(client) &&
            UsbCdc_WaitForWrite(client);
}

bool UsbCdc_FlushWriteBuffer(void) {
    UsbCdcData_t* client = &gRunTimeUsbSttings;
    if (client->state != USB_CDC_STATE_PROCESS) {
        return false;
    }

    // Drain all data from the circular buffer within a total 500ms deadline.
    // Loops until the circular buffer is empty: wait for any in-flight DMA,
    // then start the next write chunk, repeat until nothing left to write.
    // All waits (both initial DMA and each new write DMA) share the deadline,
    // so the function is fully bounded even if the host disconnects mid-flush.
    TickType_t start = xTaskGetTickCount();

    while (true) {
        // Wait for any in-flight DMA transfer to complete.
        while (client->writeTransferHandle != USB_DEVICE_CDC_TRANSFER_HANDLE_INVALID) {
            if (client->state != USB_CDC_STATE_PROCESS) {
                return false;
            }
            if ((xTaskGetTickCount() - start) > pdMS_TO_TICKS(500)) {
                LOG_E("USB flush: timeout waiting for DMA");
                return false;
            }
            vTaskDelay(pdMS_TO_TICKS(10));
        }

        // No in-flight DMA — start the next write chunk from the circular buffer.
        if (!UsbCdc_BeginWrite(client)) {
            return false;
        }

        // If no new DMA was started, the circular buffer is now empty — done.
        if (client->writeTransferHandle == USB_DEVICE_CDC_TRANSFER_HANDLE_INVALID) {
            return true;
        }
    }
}

/**
 * Thin wrapper to match ScpiTransportWriteFn signature.
 */
static size_t UsbCdc_ScpiWrite(const char* data, size_t len) {
    return UsbCdc_WriteToBuffer(NULL, data, len);
}

/**
 * Callback from libscpi: Implements the write operation
 * @param context The scpi context
 * @param data The data to write
 * @param len The length of 'data'
 * @return The number of characters written
 */
static size_t SCPI_USB_Write(scpi_t * context, const char* data, size_t len) {
    UNUSED(context);
    return SCPI_WriteWithRetry(UsbCdc_ScpiWrite, data, len);
}

/**
 * Callback from libscpi: Implements the flush operation
 * @param context The scpi context
 * @return always SCPI_RES_OK
 */
static scpi_result_t SCPI_USB_Flush(scpi_t * context) {
    UNUSED(context);

    if (UsbCdc_Flush(&gRunTimeUsbSttings)) {
        return SCPI_RES_OK;
    } else {
        return SCPI_RES_ERR;
    }
}

/**
 * Callback from libscpi: Implements the error operation
 * @param context The scpi context
 * @param err The scpi error code
 * @return always 0
 */
static int SCPI_USB_Error(scpi_t * context, int_fast16_t err) {
    char ip[100];
    // Don't print "No error" messages - err code 0 is used internally by SCPI lib
    if (err != 0) {
        const char *err_str = SCPI_ErrorTranslate(err);
        if (err_str == NULL) {
            err_str = "Unknown";
        }
        snprintf(ip, sizeof(ip), "**ERROR: %d, \"%s\"\r\n", (int32_t) err, err_str);
        size_t len = strlen(ip);
        size_t written = context->interface->write(context, ip, len);
        if (written != len) {
            // Write error occurred, but we can't do much about it in error handler
            // The error has been formatted, partial write is better than none
        }
        // Also log to our Logger so errors appear in SYST:LOG?
        LOG_E("SCPI Error %d: %s\r\n", (int32_t) err, err_str);
    }
    return 0;
}

/**
 * Callback from libscpi: Implements the control operation
 * @param context The scpi context
 * @param ctrl The control name
 * @param val The new value for the control
 * @return alwasy SCPI_RES_OK
 */
static scpi_result_t SCPI_USB_Control(scpi_t * context, scpi_ctrl_name_t ctrl, scpi_reg_val_t val) {
    UNUSED(context);
    UNUSED(val);
    if (SCPI_CTRL_SRQ == ctrl) {
        //fprintf(stderr, "**SRQ: 0x%X (%d)\r\n", val, val);
    } else {
        //fprintf(stderr, "**CTRL %02x: 0x%X (%d)\r\n", ctrl, val, val);
    }
    return SCPI_RES_OK;
}

static scpi_interface_t scpi_interface = {
    .write = SCPI_USB_Write,
    .error = SCPI_USB_Error,
    .reset = NULL,
    .control = SCPI_USB_Control,
    .flush = SCPI_USB_Flush,
};

/**
 * Called to echo commands to the console
 * @param context The console theat made this call
 * @param textLen The length of the text to echo
 * @param text The text to echo
 */
static void microrl_echo(microrl_t* context, size_t textLen, const char* text) {
    UNUSED(context);
    UsbCdc_WriteToBuffer(&gRunTimeUsbSttings, text, textLen);
}

/**
 * Called to process a completed command
 * @param context The console theat made this call
 * @param commandLen The length of the command
 * @param command The command to process
 * @return The result of processing the command, or -1 if the command is invalid;
 */
static int microrl_commandComplete(microrl_t* context, size_t commandLen, const char* command) {
    UNUSED(context);

    if (command != NULL && commandLen > 0) {
        // Store command in history buffer
        size_t copyLen = (commandLen < SCPI_CMD_MAX_LENGTH - 1) ? commandLen : SCPI_CMD_MAX_LENGTH - 1;
        memcpy(gRunTimeUsbSttings.cmdHistory[gRunTimeUsbSttings.cmdHistoryHead], command, copyLen);
        gRunTimeUsbSttings.cmdHistory[gRunTimeUsbSttings.cmdHistoryHead][copyLen] = '\0';
        
        // Update circular buffer indices
        gRunTimeUsbSttings.cmdHistoryHead = (gRunTimeUsbSttings.cmdHistoryHead + 1) % SCPI_CMD_HISTORY_SIZE;
        if (gRunTimeUsbSttings.cmdHistoryCount < SCPI_CMD_HISTORY_SIZE) {
            gRunTimeUsbSttings.cmdHistoryCount++;
        }
        
        // Log the SCPI command received
        LOG_D("SCPI CMD: %.*s\r\n", commandLen, command);
        
        int result = SCPI_Input(
                &gRunTimeUsbSttings.scpiContext,
                command,
                commandLen);
        
        // Ensure any SCPI responses are flushed to the USB immediately
        // This prevents the desktop app from hanging when waiting for responses
        if (result > 0) {
            UsbCdc_BeginWrite(&gRunTimeUsbSttings);
        }
        
        return result;
    }

    SYS_DEBUG_MESSAGE(SYS_ERROR_ERROR, "NULL or zero length command.");
    return -1;
}

UsbCdcData_t* UsbCdc_GetSettings() {
    return &gRunTimeUsbSttings;
}

void UsbCdc_Initialize() {

    gRunTimeUsbSttings.state = USB_CDC_STATE_INIT;

    gRunTimeUsbSttings.deviceHandle = USB_DEVICE_HANDLE_INVALID;
    gRunTimeUsbSttings.isVbusDetected = false;
    gRunTimeUsbSttings.isConfigured = false;

    gRunTimeUsbSttings.deviceLineCodingData.dwDTERate = 9600;
    gRunTimeUsbSttings.deviceLineCodingData.bParityType = 0;
    gRunTimeUsbSttings.deviceLineCodingData.bParityType = 0;
    gRunTimeUsbSttings.deviceLineCodingData.bDataBits = 8;

    gRunTimeUsbSttings.readTransferHandle =
            USB_DEVICE_CDC_TRANSFER_HANDLE_INVALID;
    gRunTimeUsbSttings.writeTransferHandle =
            USB_DEVICE_CDC_TRANSFER_HANDLE_INVALID;
    gRunTimeUsbSttings.readBufferLength = 0;

    microrl_init(&gRunTimeUsbSttings.console, microrl_echo);
    microrl_set_echo(&gRunTimeUsbSttings.console, true);
    microrl_set_execute_callback(
            &gRunTimeUsbSttings.console,
            microrl_commandComplete);
    gRunTimeUsbSttings.scpiContext = CreateSCPIContext(&scpi_interface, &gRunTimeUsbSttings);

    // Allocate DMA write staging buffer from coherent pool (auto-sized at stream start)
    gRunTimeUsbSttings.dmaWriteBufferSize = USBCDC_DMA_WBUFFER_MAX;
    gRunTimeUsbSttings.dmaWriteBuffer = CoherentPool_Alloc("USB_write",
                                            gRunTimeUsbSttings.dmaWriteBufferSize);
    if (gRunTimeUsbSttings.dmaWriteBuffer == NULL) {
        LOG_E("[USB] Failed to allocate DMA write buffer from coherent pool");
    }

    // Initialize circular buffer from streaming buffer pool (partitioned at boot).
    // Re-partitioned at each stream start via UsbCdc_SetWriteBuffer.
    {
        uint8_t* buf; uint32_t len;
        StreamingBufferPool_GetUsb(&buf, &len);
        CircularBuf_InitExternal(&gRunTimeUsbSttings.wCirbuf,
            UsbCdc_Wrapper_Write, buf, len);
    }
    /* Create a mutex type semaphore. */
    gRunTimeUsbSttings.wMutex = xSemaphoreCreateMutex();

    if (gRunTimeUsbSttings.wMutex == NULL) {
        /* The semaphore was created successfully and
        can be used. */
    }

    //Release ownership of the mutex object
    xSemaphoreGive(gRunTimeUsbSttings.wMutex);
    
    // Initialize escape sequence state
    gRunTimeUsbSttings.escapeState = ESC_STATE_NONE;

}

void UsbCdc_ProcessState() {

    UNUSED(UsbCdc_WaitForRead); // We dont want to block on the read so this is currently not used

    switch (gRunTimeUsbSttings.state) {
        case USB_CDC_STATE_INIT:
            //GPIO_WritePin(g_boardState.led2Id, false);

            /* Open the device layer */
            gRunTimeUsbSttings.deviceHandle = USB_DEVICE_Open(USB_DEVICE_INDEX_0, DRV_IO_INTENT_READWRITE);

            if (gRunTimeUsbSttings.deviceHandle != USB_DEVICE_HANDLE_INVALID) {
                /* Register a callback with device layer to get event notification (for end point 0) */
                USB_DEVICE_EventHandlerSet(
                        gRunTimeUsbSttings.deviceHandle,
                        UsbCdc_EventHandler,
                        0);

                gRunTimeUsbSttings.state = USB_CDC_STATE_WAIT;
            } else {
                /* The Device Layer is not ready to be opened. We should try
                 * again later. */
            }

            break;
        case USB_CDC_STATE_PROCESS:
            // If a write operation is not in progress
            if (gRunTimeUsbSttings.readTransferHandle ==
                    USB_DEVICE_CDC_TRANSFER_HANDLE_INVALID) {
                // Process any input
                UsbCdc_FinalizeRead(&gRunTimeUsbSttings);

                // Schedule the next read
                if (!UsbCdc_BeginRead(&gRunTimeUsbSttings)) {
                    break;
                }
            }

            // I a read operation is not in progress
            if (gRunTimeUsbSttings.writeTransferHandle ==
                    USB_DEVICE_CDC_TRANSFER_HANDLE_INVALID) {
                // Schedule any output;
                if (!UsbCdc_BeginWrite(&gRunTimeUsbSttings)) {
                    break;
                }

            }
#if PB_PROFILE_COUNTERS
            else {
                // #388: state-machine iteration where the prior DMA
                // transfer hadn't completed — count these to size how
                // often the single-in-flight-DMA pattern stalls progress.
                Streaming_AddProfileSample_DmaIdle();
            }
#endif

            break;
        case USB_CDC_STATE_BEGIN_CLOSE:
            //GPIO_WritePin(g_boardState.led2Id, false);

            if (gRunTimeUsbSttings.deviceHandle != USB_DEVICE_HANDLE_INVALID) {
                USB_DEVICE_Close(gRunTimeUsbSttings.deviceHandle);
                gRunTimeUsbSttings.deviceHandle = USB_DEVICE_HANDLE_INVALID;

                gRunTimeUsbSttings.state = USB_CDC_STATE_WAIT;
            }

            gRunTimeUsbSttings.state = USB_CDC_STATE_CLOSED;

            break;
        case USB_CDC_STATE_CLOSED:
            //GPIO_WritePin(g_boardState.led2Id, false);

            gRunTimeUsbSttings.deviceHandle = USB_DEVICE_HANDLE_INVALID;

            gRunTimeUsbSttings.readTransferHandle =
                    USB_DEVICE_CDC_TRANSFER_HANDLE_INVALID;
            gRunTimeUsbSttings.writeTransferHandle =
                    USB_DEVICE_CDC_TRANSFER_HANDLE_INVALID;
            gRunTimeUsbSttings.readBufferLength = 0;

            gRunTimeUsbSttings.state = USB_CDC_STATE_INIT;

            break;
        case USB_CDC_STATE_WAIT: // No action
        default:
            break;
    }
}

bool UsbCdc_IsActive() {
    return (gRunTimeUsbSttings.state == USB_CDC_STATE_PROCESS);
}

void UsbCdc_SetTransparentMode(bool value) {
    gRunTimeUsbSttings.isTransparentModeActive = value;
}

bool UsbCdc_IsVbusDetected(void) {
    return gRunTimeUsbSttings.isVbusDetected;
}

bool UsbCdc_IsConfigured(void) {
    return gRunTimeUsbSttings.isConfigured;
}

tRunTimeUsbSettings* UsbCdc_GetRuntimeSettings(void) {
    // Return pointer to runtime settings for monitoring USB state
    return (tRunTimeUsbSettings*)&gRunTimeUsbSttings;
}

bool UsbCdc_ResizeWriteBuffer(uint32_t newSize) {
    if (newSize < USBCDC_WBUFFER_SIZE) newSize = USBCDC_WBUFFER_SIZE;
    if (gRunTimeUsbSttings.wCirbuf.buf_size == newSize) return true;

    // Wait for any in-flight USB DMA write to complete.
    TickType_t start = xTaskGetTickCount();
    while (gRunTimeUsbSttings.writeTransferHandle != USB_DEVICE_CDC_TRANSFER_HANDLE_INVALID) {
        if ((xTaskGetTickCount() - start) > pdMS_TO_TICKS(1000)) {
            LOG_E("USB resize aborted: write transfer stuck");
            return false;
        }
        vTaskDelay(1);
    }

    // Use streaming buffer pool — re-partition handles the memory.
    // This is a no-op on the USB side; the actual swap happens in
    // UsbCdc_SetWriteBuffer which is called after pool partition.
    return true;
}

void UsbCdc_SetWriteBuffer(uint8_t* buf, uint32_t size) {
    if (buf == NULL || size == 0) return;

    xSemaphoreTake(gRunTimeUsbSttings.wMutex, portMAX_DELAY);

    uint32_t oldSize = gRunTimeUsbSttings.wCirbuf.buf_size;
    gRunTimeUsbSttings.wCirbuf.buf_ptr = buf;
    gRunTimeUsbSttings.wCirbuf.buf_size = size;
    gRunTimeUsbSttings.wCirbuf.insertPtr = buf;
    gRunTimeUsbSttings.wCirbuf.removePtr = buf;
    gRunTimeUsbSttings.wCirbuf.producedBytes = 0;
    gRunTimeUsbSttings.wCirbuf.consumedBytes = 0;
    gRunTimeUsbSttings.wCirbuf._ownsMemory = false;  // Pool-managed

    xSemaphoreGive(gRunTimeUsbSttings.wMutex);

    LOG_I("USB circular buffer: %u -> %u bytes", (unsigned)oldSize, (unsigned)size);
}

void UsbCdc_SetDmaWriteBuffer(uint8_t* buf, uint32_t size) {
    if (buf == NULL || size == 0) return;
    gRunTimeUsbSttings.dmaWriteBuffer = buf;
    gRunTimeUsbSttings.dmaWriteBufferSize = size;
}