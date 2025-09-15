#define LOG_LVL LOG_LEVEL_USB
#include "UsbCdc.h"

// libraries
#include "libraries/microrl/src/microrl.h"
#include "libraries/scpi/libscpi/inc/scpi/scpi.h"

// services
#include "services/SCPI/SCPIInterface.h"
#include "Util/Logger.h"
#include "HAL/BQ24297/BQ24297.h"
#include "state/data/BoardData.h"

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
            if (val.handle == pUsbCdcDataObject->writeTransferHandle && val.length == pUsbCdcDataObject->writeBufferLength)
                UsbCdc_FinalizeWrite(pUsbCdcDataObject);
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
            if (gRunTimeUsbSttings.deviceHandle != USB_DEVICE_HANDLE_INVALID) {
                gRunTimeUsbSttings.state = USB_CDC_STATE_BEGIN_CLOSE;
            }
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
            }
            break;

        case USB_DEVICE_EVENT_POWER_DETECTED:

            /* VBUS was detected. Wait 100ms for battery management to detect USB power source.  Then we can attach the device */
            gRunTimeUsbSttings.isVbusDetected = true;
            
            // Don't manipulate OTG here - MCU VBUS detection is unreliable
            // Power management will detect changes via BQ24297 polling
            
            vTaskDelay(100 / portTICK_PERIOD_MS);
            USB_DEVICE_Attach(gRunTimeUsbSttings.deviceHandle);
            break;

        case USB_DEVICE_EVENT_POWER_REMOVED:

            /* VBUS is not available any more. Detach the device. */
            gRunTimeUsbSttings.isVbusDetected = false;
            
            // Check if this is a real disconnect or false trigger at startup
            tPowerData* pPowerData = BoardData_Get(BOARDDATA_POWER_DATA, 0);
            if (pPowerData && pPowerData->BQ24297Data.initComplete) {
                // Only trust this event if BQ24297 is initialized
                BQ24297_UpdateStatusSafe();
                if (pPowerData->BQ24297Data.status.pgStat) {
                    LOG_D("USB: False VBUS removal detected (pgStat=1), ignoring");
                    break;
                }
            }
            
            LOG_E("USB: VBUS removed - USB POWER LOST!");
            
            // OTG mode is now controlled manually via SCPI commands only
            // Automatic OTG transitions have been disabled to prevent unexpected power state changes
            
            
            USB_DEVICE_Detach(gRunTimeUsbSttings.deviceHandle);
            break;

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
            gRunTimeUsbSttings.state = USB_CDC_STATE_BEGIN_CLOSE;
            break;
        default:
            break;
    }
}

int UsbCdc_Wrapper_Write(uint8_t* buf, uint32_t len) {
    // Validate length against buffer size to prevent overflow
    if (len > USBCDC_WBUFFER_SIZE) {
        return -1;  // Buffer too small
    }
    
    // Validate buffer pointer to prevent null dereference
    if (len > 0 && buf == NULL) {
        return -1;  // Invalid buffer pointer
    }
    
    memcpy(gRunTimeUsbSttings.writeBuffer, buf, (size_t)len);
    gRunTimeUsbSttings.writeBufferLength = len;
    USB_DEVICE_CDC_RESULT writeResult = USB_DEVICE_CDC_Write(USB_DEVICE_CDC_INDEX_0,
            &gRunTimeUsbSttings.writeTransferHandle,
            gRunTimeUsbSttings.writeBuffer,
            len,
            USB_DEVICE_CDC_TRANSFER_FLAGS_DATA_COMPLETE);

    return writeResult;
}

/**
 * Enqueues client data for writing 
 */
static bool UsbCdc_BeginWrite(UsbCdcData_t* client) {

    USB_DEVICE_CDC_RESULT writeResult = USB_DEVICE_CDC_RESULT_OK;

    if (client->state != USB_CDC_STATE_PROCESS) {
        return false;
    }

    // make sure there is no write transfer in progress
    if (client->writeTransferHandle == USB_DEVICE_CDC_TRANSFER_HANDLE_INVALID) {
        xSemaphoreTake(client->wMutex, portMAX_DELAY);
        if (CircularBuf_NumBytesAvailable(&client->wCirbuf) > 0) {
            CircularBuf_ProcessBytes(&client->wCirbuf, NULL, USBCDC_WBUFFER_SIZE, &writeResult);
        }
        xSemaphoreGive(client->wMutex);

        if (writeResult != USB_DEVICE_CDC_RESULT_OK) {
            //while(1);
        }

        switch (writeResult) {
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
                SYS_DEBUG_MESSAGE(SYS_ERROR_ERROR, "Bad USB write size");
                return false;
            case USB_DEVICE_CDC_RESULT_ERROR_TRANSFER_QUEUE_FULL: // Too many pending requests. Just wait.
            case USB_DEVICE_CDC_RESULT_ERROR: // Concurrency issue. Just wait. 
            default:
                // No action
                return false;
        }

        if (gRunTimeUsbSttings.writeTransferHandle ==
                USB_DEVICE_CDC_TRANSFER_HANDLE_INVALID) {
            // SYS_DEBUG_MESSAGE(SYS_ERROR_ERROR, "Non-error w/ invalid transfer handle"); // Means USB write could not be scheduled
            return false;
        }
    }

    return true;
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
 * Called to complete a read operation, feeding data to the rest of the system
 */
static bool UsbCdc_FinalizeRead(UsbCdcData_t* client) {
    static const char UNSET_TRANSPARENT_MODE_COMMAND[] = "SYSTem:USB:SetTransparentMode 0"; // NOTE: PuTTY sends \r not \r\n by default so we are going to check for all terminating scenarios below
    static uint8_t transparentModeCmdLength = 0;
    
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
            // IMPORTANT: Transparent mode should NOT filter characters
            // This mode is used for raw binary data passthrough (e.g., firmware updates)
            // Filtering would corrupt the data stream
            
            // Check for UNSET_TRANSPARENT_MODE_COMMAND length plus up to two terminating characters (\n\r).
            transparentModeCmdLength = strlen(UNSET_TRANSPARENT_MODE_COMMAND);
            if ((client->readBufferLength == transparentModeCmdLength) ||       \
                (client->readBufferLength == transparentModeCmdLength + 1) ||   \
                (client->readBufferLength == transparentModeCmdLength + 2)) {
                //  Now check to see if the string actually matches the command UNSET_TRANSPARENT_MODE_COMMAND
                if ((strspn(UNSET_TRANSPARENT_MODE_COMMAND, (const char *)client->readBuffer)) == transparentModeCmdLength) {
                    for (size_t i = 0; i < transparentModeCmdLength; ++i) {
                        microrl_insert_char(&client->console, client->readBuffer[i]);
                    }
                    // Since we truncated the read buffer, be sure to pass the proper termination characters
                    microrl_insert_char(&client->console, '\n');
                    microrl_insert_char(&client->console, '\r');
                }
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
    if (client == NULL) {
        client = &gRunTimeUsbSttings;
    }
    size_t bytesAdded = 0;

    if (len == 0)return 0;

    // Add timeout to prevent infinite hang
    // Maximum wait time: 500ms
    TickType_t startTime = xTaskGetTickCount();
    TickType_t timeoutTicks = 500 / portTICK_PERIOD_MS;
    
    // Wait for buffer space with mutex protection
    bool hasSpace = false;
    while (!hasSpace) {
        xSemaphoreTake(client->wMutex, portMAX_DELAY);
        hasSpace = (CircularBuf_NumBytesFree(&client->wCirbuf) >= len);
        size_t currentFree = CircularBuf_NumBytesFree(&client->wCirbuf);
        xSemaphoreGive(client->wMutex);
        
        if (!hasSpace) {
            if ((xTaskGetTickCount() - startTime) >= timeoutTicks) {
                //commTest.USBOverflow++;
                LOG_E("USB: Write buffer timeout - needed %d bytes, only %d free", 
                      len, currentFree);
                return 0;
            }
            vTaskDelay(2 / portTICK_PERIOD_MS);
        }
    }

    //Obtain ownership of the mutex object
    xSemaphoreTake(client->wMutex, portMAX_DELAY);
    bytesAdded = CircularBuf_AddBytes(&client->wCirbuf, (uint8_t*) data, len);
    xSemaphoreGive(client->wMutex);

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

/**
 * Callback from libscpi: Implements the write operation
 * @param context The scpi context
 * @param data The data to write
 * @param len The length of 'data'
 * @return The number of characters written
 */
static size_t SCPI_USB_Write(scpi_t * context, const char* data, size_t len) {

    UNUSED(context);
    UsbCdc_WriteToBuffer(&gRunTimeUsbSttings, data, len);
    return len;
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
        sprintf(ip, "**ERROR: %d, \"%s\"\r\n", (int32_t) err, SCPI_ErrorTranslate(err));
        size_t len = strlen(ip);
        size_t written = context->interface->write(context, ip, len);
        if (written != len) {
            // Write error occurred, but we can't do much about it in error handler
            // The error has been formatted, partial write is better than none
        }
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
        
        // Check for SCPI errors including buffer overrun
        if (SCPI_ErrorCount(&gRunTimeUsbSttings.scpiContext) > 0) {
            scpi_error_t error;
            while (SCPI_ErrorPop(&gRunTimeUsbSttings.scpiContext, &error)) {
                const char* error_str = SCPI_ErrorTranslate(error.error_code);
                LOG_E("SCPI Error %d: %s\r\n", error.error_code, error_str ? error_str : "Unknown");
            }
        }
        
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
    gRunTimeUsbSttings.isVbusDetected = false;  // Initialize VBUS detection state

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

    // reset circular buffer variables to known state. 
    CircularBuf_Init(&gRunTimeUsbSttings.wCirbuf,
            UsbCdc_Wrapper_Write,
            (USBCDC_CIRCULAR_BUFF_SIZE));
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

tRunTimeUsbSettings* UsbCdc_GetRuntimeSettings(void) {
    // Return pointer to runtime settings for monitoring USB state
    return (tRunTimeUsbSettings*)&gRunTimeUsbSttings;
}