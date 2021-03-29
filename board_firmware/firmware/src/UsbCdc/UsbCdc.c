#include "UsbCdc.h"

// 3rd party
#include "scpi/scpi.h"

// App
#include "SCPI/SCPIInterface.h"
#include "HAL/Power/PowerApi.h"
#include "state/board/BoardConfig.h"
#include "state/data/BoardData.h"
#include "state/runtime/BoardRuntimeConfig.h"

#define UNUSED(x) (void)(x)

/**
 * Finalizes a write operation by clearing the buffer for additional content 
 */
static bool UsbCdc_FinalizeWrite(UsbCdcData* client);

USB_DEVICE_CDC_EVENT_RESPONSE UsbCdc_CDCEventHandler
(
    USB_DEVICE_CDC_INDEX index ,
    USB_DEVICE_CDC_EVENT event ,
    void * pData,
    uintptr_t userData
)
{
    UsbCdcData * usbCdcDataObject;
    usbCdcDataObject = (UsbCdcData *)userData;
    USB_CDC_CONTROL_LINE_STATE * controlLineStateData;

    switch ( event )
    {
        case USB_DEVICE_CDC_EVENT_GET_LINE_CODING:

            /* This means the host wants to know the current line
             * coding. This is a control transfer request. Use the
             * USB_DEVICE_ControlSend() function to send the data to
             * host.  */

            USB_DEVICE_ControlSend(usbCdcDataObject->deviceHandle,
                    &usbCdcDataObject->getLineCodingData, sizeof(USB_CDC_LINE_CODING));

            break;

        case USB_DEVICE_CDC_EVENT_SET_LINE_CODING:

            /* This means the host wants to set the line coding.
             * This is a control transfer request. Use the
             * USB_DEVICE_ControlReceive() function to receive the
             * data from the host */

            USB_DEVICE_ControlReceive(usbCdcDataObject->deviceHandle,
                    &usbCdcDataObject->setLineCodingData, sizeof(USB_CDC_LINE_CODING));

            break;

        case USB_DEVICE_CDC_EVENT_SET_CONTROL_LINE_STATE:

            /* This means the host is setting the control line state.
             * Read the control line state. We will accept this request
             * for now. */

            controlLineStateData = (USB_CDC_CONTROL_LINE_STATE *)pData;
            usbCdcDataObject->controlLineStateData.dtr = controlLineStateData->dtr;
            usbCdcDataObject->controlLineStateData.carrier = controlLineStateData->carrier;

            if (usbCdcDataObject->controlLineStateData.dtr == 0)
            {
                if (g_BoardRuntimeConfig.usbSettings.state == USB_CDC_STATE_PROCESS)
                {
                    g_BoardRuntimeConfig.usbSettings.state = USB_CDC_STATE_WAIT;
                }
            }
            else
            {
                g_BoardRuntimeConfig.usbSettings.state = USB_CDC_STATE_PROCESS;
            }
            
            USB_DEVICE_ControlStatus(usbCdcDataObject->deviceHandle, USB_DEVICE_CONTROL_STATUS_OK);

            break;

        case USB_DEVICE_CDC_EVENT_SEND_BREAK:

            /* This means that the host is requesting that a break of the
             * specified duration be sent. Read the break duration */

            usbCdcDataObject->breakData = ((USB_DEVICE_CDC_EVENT_DATA_SEND_BREAK *)pData)->breakDuration;
            break;

        case USB_DEVICE_CDC_EVENT_READ_COMPLETE:
        {
            /* This means that the host has sent some data*/
            USB_DEVICE_CDC_EVENT_DATA_READ_COMPLETE* readResult = (USB_DEVICE_CDC_EVENT_DATA_READ_COMPLETE*)pData;
            usbCdcDataObject->readBufferLength = readResult->length;
            usbCdcDataObject->readTransferHandle = USB_DEVICE_CDC_TRANSFER_HANDLE_INVALID;
            break;
        }
        case USB_DEVICE_CDC_EVENT_CONTROL_TRANSFER_DATA_RECEIVED:

            /* The data stage of the last control transfer is
             * complete. For now we accept all the data */

            USB_DEVICE_ControlStatus(usbCdcDataObject->deviceHandle, USB_DEVICE_CONTROL_STATUS_OK);
            break;

        case USB_DEVICE_CDC_EVENT_CONTROL_TRANSFER_DATA_SENT:

            /* This means the GET LINE CODING function data is valid. We dont
             * do much with this data in this demo. */
            break;

        case USB_DEVICE_CDC_EVENT_WRITE_COMPLETE:
        {
            /* This means that the data write got completed. We can schedule
             * the next write. */
            UsbCdc_FinalizeWrite(usbCdcDataObject);
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
void UsbCdc_EventHandler ( USB_DEVICE_EVENT event, void * eventData, uintptr_t context )
{
    USB_DEVICE_EVENT_DATA_CONFIGURED *configuredEventData;
    switch ( event )
    {
        case USB_DEVICE_EVENT_DECONFIGURED:
        case USB_DEVICE_EVENT_RESET:
            if (g_BoardRuntimeConfig.usbSettings.deviceHandle != USB_DEVICE_HANDLE_INVALID)
            {
                g_BoardRuntimeConfig.usbSettings.state = USB_CDC_STATE_BEGIN_CLOSE;
            }
            break;
        case USB_DEVICE_EVENT_CONFIGURED:

            /* Check the configuration. We only support configuration 1 */
            configuredEventData = (USB_DEVICE_EVENT_DATA_CONFIGURED*)eventData;
            if ( configuredEventData->configurationValue == 1)
            {
                /* Register the CDC Device application event handler here.
                 * Note how the usbCdcData object pointer is passed as the
                 * user data */
                USB_DEVICE_CDC_EventHandlerSet(USB_DEVICE_CDC_INDEX_0, UsbCdc_CDCEventHandler, (uintptr_t)&g_BoardRuntimeConfig.usbSettings);

                /* Mark that the device is now configured */
                g_BoardRuntimeConfig.usbSettings.state = USB_CDC_STATE_WAIT;
            }
            break;

        case USB_DEVICE_EVENT_POWER_DETECTED:

            /* VBUS was detected. Wait 100ms for battery management to detect USB power source.  Then we can attach the device */
            vTaskDelay(100 / portTICK_PERIOD_MS);
            USB_DEVICE_Attach(g_BoardRuntimeConfig.usbSettings.deviceHandle);
            break;

        case USB_DEVICE_EVENT_POWER_REMOVED:

            /* VBUS is not available any more. Detach the device. */
            USB_DEVICE_Detach(g_BoardRuntimeConfig.usbSettings.deviceHandle);
            break;

        case USB_DEVICE_EVENT_SUSPENDED:
            // TODO: Are the transfer handles still valid?
            Power_USB_Sleep_Update( true );
            g_BoardRuntimeConfig.usbSettings.state = USB_CDC_STATE_WAIT;
            break;
        case USB_DEVICE_EVENT_RESUMED:
            // TODO: Does suspend only occur after initialization?
            Power_USB_Sleep_Update( false );
            g_BoardRuntimeConfig.usbSettings.state = USB_CDC_STATE_WAIT;
            break;
        case USB_DEVICE_EVENT_ERROR:
            // TODO: Are there non-fatal errors?
            g_BoardRuntimeConfig.usbSettings.state = USB_CDC_STATE_BEGIN_CLOSE;
            break;
        default:
            break;
    }
}

/**
 * Enqueues client data for writing 
 */
static bool UsbCdc_BeginWrite(UsbCdcData* client)
{
    if (client->state != USB_CDC_STATE_PROCESS)
    {
        return false;
    }
    
    if(client->writeTransferHandle == USB_DEVICE_CDC_TRANSFER_HANDLE_INVALID &&
       client->writeBufferLength > 0)
    {
        USB_DEVICE_CDC_RESULT writeResult = USB_DEVICE_CDC_Write(USB_DEVICE_CDC_INDEX_0,
            &client->writeTransferHandle, client->writeBuffer, client->writeBufferLength,
            USB_DEVICE_CDC_TRANSFER_FLAGS_DATA_COMPLETE);

        switch (writeResult)
        {
        case USB_DEVICE_CDC_RESULT_OK:
            // Normal operation
            break;
        
        case USB_DEVICE_CDC_RESULT_ERROR_INSTANCE_NOT_CONFIGURED:
        case USB_DEVICE_CDC_RESULT_ERROR_INSTANCE_INVALID:
        case USB_DEVICE_CDC_RESULT_ERROR_PARAMETER_INVALID:
        case USB_DEVICE_CDC_RESULT_ERROR_ENDPOINT_HALTED:
        case USB_DEVICE_CDC_RESULT_ERROR_TERMINATED_BY_HOST:
            // Reset the interface
            g_BoardRuntimeConfig.usbSettings.state = USB_CDC_STATE_BEGIN_CLOSE;
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
        
        if (g_BoardRuntimeConfig.usbSettings.writeTransferHandle == USB_DEVICE_CDC_TRANSFER_HANDLE_INVALID)
        {
            // SYS_DEBUG_MESSAGE(SYS_ERROR_ERROR, "Non-error w/ invalid transfer handle"); // Means USB write could not be scheduled
            return false;
        }
    }
    
    return true;
}

/**
 * Waits for a write operation to complete
 */
static bool UsbCdc_WaitForWrite(UsbCdcData* client)
{
    if (client->state != USB_CDC_STATE_PROCESS)
    {
        return false;
    }
            
    while (client->writeTransferHandle != USB_DEVICE_CDC_TRANSFER_HANDLE_INVALID)
    {
        if (client->state != USB_CDC_STATE_PROCESS)
        {
            return false;
        }
        
        vTaskDelay(100);
    }
    
    return true;
}

/**
 * Finalizes a write operation by clearing the buffer for additional content 
 */
static bool UsbCdc_FinalizeWrite(UsbCdcData* client)
{
    client->writeTransferHandle = USB_DEVICE_CDC_TRANSFER_HANDLE_INVALID;
    if (client->writeBufferLength > 0)
    {
        client->writeBufferLength = 0;
        return true; 
    }
    
    return false;
}

/**
 * Starts listening for a read
 */
static bool UsbCdc_BeginRead(UsbCdcData* client)
{
    if (client->state != USB_CDC_STATE_PROCESS)
    {
        return false;
    }
    
    // Schedule the next read
    if(g_BoardRuntimeConfig.usbSettings.readTransferHandle == USB_DEVICE_CDC_TRANSFER_HANDLE_INVALID &&
       g_BoardRuntimeConfig.usbSettings.readBufferLength == 0)
    {
        USB_DEVICE_CDC_RESULT readResult = USB_DEVICE_CDC_Read (USB_DEVICE_CDC_INDEX_0,
            &g_BoardRuntimeConfig.usbSettings.readTransferHandle, g_BoardRuntimeConfig.usbSettings.readBuffer,
            USB_BUFFER_SIZE);

        switch (readResult)
        {
        case USB_DEVICE_CDC_RESULT_OK:
            // Normal operation
            break;
        
        case USB_DEVICE_CDC_RESULT_ERROR_INSTANCE_NOT_CONFIGURED:
        case USB_DEVICE_CDC_RESULT_ERROR_INSTANCE_INVALID:
        case USB_DEVICE_CDC_RESULT_ERROR_PARAMETER_INVALID:
        case USB_DEVICE_CDC_RESULT_ERROR_ENDPOINT_HALTED:
        case USB_DEVICE_CDC_RESULT_ERROR_TERMINATED_BY_HOST:
            // Reset the interface
            g_BoardRuntimeConfig.usbSettings.state = USB_CDC_STATE_BEGIN_CLOSE;
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
        
        if (g_BoardRuntimeConfig.usbSettings.readTransferHandle == USB_DEVICE_CDC_TRANSFER_HANDLE_INVALID)
        {
            SYS_DEBUG_MESSAGE(SYS_ERROR_ERROR, "Non-error w/ invalid transfer handle");
            return false;
        }
    }
    
    return true;
    
}

/**
 * Waits for a read operation to complete
 */
static bool UsbCdc_WaitForRead(UsbCdcData* client)
{
    if (client->state != USB_CDC_STATE_PROCESS)
    {
        return false;
    }
            
    while (client->readTransferHandle != USB_DEVICE_CDC_TRANSFER_HANDLE_INVALID)
    {
        if (client->state != USB_CDC_STATE_PROCESS)
        {
            return false;
        }
        
        vTaskDelay(100);
    }
    
    return true;
}

/**
 * Called to complete a read operation, feeding data to the rest of the system
 */
static bool UsbCdc_FinalizeRead(UsbCdcData* client)
{
    if (client->readBufferLength > 0)
    {
        size_t i = 0;
        for (i=0; i<client->readBufferLength; ++i)
        {
            microrl_insert_char(&client->console, client->readBuffer[i]);
        }

        client->readBufferLength = 0;
        return true; 
    }
    
    return false;
}

size_t UsbCdc_Write(UsbCdcData* client, const char* data, size_t len)
{
    size_t current = 0;
    while (current < len)
    {
        size_t remainder = len - current;
        size_t size = min(remainder, USB_BUFFER_SIZE - client->writeBufferLength);
        memcpy(client->writeBuffer + client->writeBufferLength, data + current, size);
        client->writeBufferLength += size;
        
        if (size < remainder)
        {
            if (!UsbCdc_BeginWrite(client))
            {
                break;
            }
            
            if (!UsbCdc_WaitForWrite(client))
            {
                break;
            }
        }
        
        current += size;
    }

    return current;
}

size_t UsbCdc_WriteDefault(const char* data, size_t len)
{
    return UsbCdc_Write(&g_BoardRuntimeConfig.usbSettings, data, len);
}

/**
 * Flushes data from the provided client
 * @param client The client to flush
 * @return  True if data is flushed, false otherwise
 */
static bool UsbCdc_Flush(UsbCdcData* client)
{
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
static size_t SCPI_USB_Write(scpi_t * context, const char* data, size_t len)
{
    UNUSED(context);
    
    return UsbCdc_Write(&g_BoardRuntimeConfig.usbSettings, data, len);
}

/**
 * Callback from libscpi: Implements the flush operation
 * @param context The scpi context
 * @return always SCPI_RES_OK
 */
static scpi_result_t SCPI_USB_Flush(scpi_t * context)
{
    UNUSED(context);
    
    if (UsbCdc_Flush(&g_BoardRuntimeConfig.usbSettings))
    {
        return SCPI_RES_OK;
    }
    else
    {
        return SCPI_RES_ERR;
    }
}

/**
 * Callback from libscpi: Implements the error operation
 * @param context The scpi context
 * @param err The scpi error code
 * @return always 0
 */
static int SCPI_USB_Error(scpi_t * context, int_fast16_t err)
{
    char ip[100];
    // If we wanted to do something in response to an error, we could do so here.
    // I'm expecting the client to call 'SYSTem:ERRor?' if they want error information

    sprintf(ip, "**ERROR: %d, \"%s\"\r\n", (int32_t) err, SCPI_ErrorTranslate(err));
    context->interface->write(context, ip, strlen(ip));
    return 0;
}

/**
 * Callback from libscpi: Implements the control operation
 * @param context The scpi context
 * @param ctrl The control name
 * @param val The new value for the control
 * @return alwasy SCPI_RES_OK
 */
static scpi_result_t SCPI_USB_Control(scpi_t * context, scpi_ctrl_name_t ctrl, scpi_reg_val_t val)
{
    UNUSED(context);
    UNUSED(val);
    if (SCPI_CTRL_SRQ == ctrl)
    {
        //fprintf(stderr, "**SRQ: 0x%X (%d)\r\n", val, val);
    }
    else
    {
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
static void microrl_echo(microrl_t* context, size_t textLen, const char* text)
{
    UNUSED(context);
    
    UsbCdc_Write(&g_BoardRuntimeConfig.usbSettings, text, textLen);
}

/**
 * Called to process a completed command
 * @param context The console theat made this call
 * @param commandLen The length of the command
 * @param command The command to process
 * @return The result of processing the command, or -1 if the command is invalid;
 */
static int microrl_commandComplete(microrl_t* context, size_t commandLen, const char* command)
{
    UNUSED(context);
    
    if (command != NULL && commandLen > 0)
    {
        return SCPI_Input(&g_BoardRuntimeConfig.usbSettings.scpiContext, command, commandLen);
    }
    
    SYS_DEBUG_MESSAGE(SYS_ERROR_ERROR, "NULL or zero length command.");
    return -1;
}

void UsbCdc_Initialize()
{
    g_BoardRuntimeConfig.usbSettings.state = USB_CDC_STATE_INIT;

    g_BoardRuntimeConfig.usbSettings.deviceHandle = USB_DEVICE_HANDLE_INVALID ;

    g_BoardRuntimeConfig.usbSettings.getLineCodingData.dwDTERate = 9600;
    g_BoardRuntimeConfig.usbSettings.getLineCodingData.bParityType =  0;
    g_BoardRuntimeConfig.usbSettings.getLineCodingData.bParityType = 0;
    g_BoardRuntimeConfig.usbSettings.getLineCodingData.bDataBits = 8;

    g_BoardRuntimeConfig.usbSettings.readTransferHandle = USB_DEVICE_CDC_TRANSFER_HANDLE_INVALID;
    g_BoardRuntimeConfig.usbSettings.writeTransferHandle = USB_DEVICE_CDC_TRANSFER_HANDLE_INVALID;
    g_BoardRuntimeConfig.usbSettings.readBufferLength = 0;
    g_BoardRuntimeConfig.usbSettings.writeBufferLength = 0;
    
    microrl_init(&g_BoardRuntimeConfig.usbSettings.console, microrl_echo);
    microrl_set_echo(&g_BoardRuntimeConfig.usbSettings.console, true);
    microrl_set_execute_callback(&g_BoardRuntimeConfig.usbSettings.console, microrl_commandComplete);
    g_BoardRuntimeConfig.usbSettings.scpiContext = CreateSCPIContext(&scpi_interface, &g_BoardRuntimeConfig.usbSettings);
}

void UsbCdc_ProcessState()
{
    UNUSED(UsbCdc_WaitForRead); // We dont want to block on the read so this is currently not used

    switch(g_BoardRuntimeConfig.usbSettings.state)
    {
        case USB_CDC_STATE_INIT:
            //GPIO_WritePin(g_boardState.led2Id, false);
            
            /* Open the device layer */
            g_BoardRuntimeConfig.usbSettings.deviceHandle = USB_DEVICE_Open( USB_DEVICE_INDEX_0, DRV_IO_INTENT_READWRITE );

            if(g_BoardRuntimeConfig.usbSettings.deviceHandle != USB_DEVICE_HANDLE_INVALID)
            {
                /* Register a callback with device layer to get event notification (for end point 0) */
                USB_DEVICE_EventHandlerSet(g_BoardRuntimeConfig.usbSettings.deviceHandle, UsbCdc_EventHandler, 0);

                g_BoardRuntimeConfig.usbSettings.state = USB_CDC_STATE_WAIT;
            }
            else
            {
                /* The Device Layer is not ready to be opened. We should try
                 * again later. */
            }

            break;
        case USB_CDC_STATE_PROCESS:           
            // If a write operation is not in progress
            if(g_BoardRuntimeConfig.usbSettings.readTransferHandle == USB_DEVICE_CDC_TRANSFER_HANDLE_INVALID)
            {
                // Process any input
                UsbCdc_FinalizeRead(&g_BoardRuntimeConfig.usbSettings);
                
                // Schedule the next read
                if (!UsbCdc_BeginRead(&g_BoardRuntimeConfig.usbSettings))
                {
                    break;
                }
            }
            
            // I a read operation is not in progress
            if(g_BoardRuntimeConfig.usbSettings.writeTransferHandle == USB_DEVICE_CDC_TRANSFER_HANDLE_INVALID)
            {
                // Schedule any output
                if (!UsbCdc_BeginWrite(&g_BoardRuntimeConfig.usbSettings))
                {
                    break;
                }
            }
            
            break;
        case USB_CDC_STATE_BEGIN_CLOSE:
            //GPIO_WritePin(g_boardState.led2Id, false);
            
            if(g_BoardRuntimeConfig.usbSettings.deviceHandle != USB_DEVICE_HANDLE_INVALID)
            {
                USB_DEVICE_Close(g_BoardRuntimeConfig.usbSettings.deviceHandle);
                g_BoardRuntimeConfig.usbSettings.deviceHandle = USB_DEVICE_HANDLE_INVALID;
                
                g_BoardRuntimeConfig.usbSettings.state = USB_CDC_STATE_WAIT;
            }
            
            g_BoardRuntimeConfig.usbSettings.state = USB_CDC_STATE_CLOSED;
            
            break;
        case USB_CDC_STATE_CLOSED:
            //GPIO_WritePin(g_boardState.led2Id, false);
            
            g_BoardRuntimeConfig.usbSettings.deviceHandle = USB_DEVICE_HANDLE_INVALID;

            g_BoardRuntimeConfig.usbSettings.readTransferHandle = USB_DEVICE_CDC_TRANSFER_HANDLE_INVALID;
            g_BoardRuntimeConfig.usbSettings.writeTransferHandle = USB_DEVICE_CDC_TRANSFER_HANDLE_INVALID;
            g_BoardRuntimeConfig.usbSettings.readBufferLength = 0;
            g_BoardRuntimeConfig.usbSettings.writeBufferLength = 0; 
        
            g_BoardRuntimeConfig.usbSettings.state = USB_CDC_STATE_INIT;
            
            break;
        case USB_CDC_STATE_WAIT: // No action
        default:
            break;
    }
}

bool UsbCdc_IsActive()
{
    return (g_BoardRuntimeConfig.usbSettings.state == USB_CDC_STATE_PROCESS);
}