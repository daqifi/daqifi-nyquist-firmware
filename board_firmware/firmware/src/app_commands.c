/*******************************************************************************
  File Name:
    app_commands.c

  Summary:
    Commands for the Wi-Fi Easy Configuration Demo

  Description:

 *******************************************************************************/

// DOM-IGNORE-BEGIN
/*******************************************************************************
Copyright (c) 2013 released Microchip Technology Inc.  All rights reserved.

Microchip licenses to you the right to use, modify, copy and distribute
Software only when embedded on a Microchip microcontroller or digital signal
controller that is integrated into your product or third party product
(pursuant to the sublicense terms in the accompanying license agreement).

You should refer to the license agreement accompanying this Software for
additional information regarding your rights and obligations.

SOFTWARE AND DOCUMENTATION ARE PROVIDED AS IS WITHOUT WARRANTY OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION, ANY WARRANTY OF
MERCHANTABILITY, TITLE, NON-INFRINGEMENT AND FITNESS FOR A PARTICULAR PURPOSE.
IN NO EVENT SHALL MICROCHIP OR ITS LICENSORS BE LIABLE OR OBLIGATED UNDER
CONTRACT, NEGLIGENCE, STRICT LIABILITY, CONTRIBUTION, BREACH OF WARRANTY, OR
OTHER LEGAL EQUITABLE THEORY ANY DIRECT OR INDIRECT DAMAGES OR EXPENSES
INCLUDING BUT NOT LIMITED TO ANY INCIDENTAL, SPECIAL, INDIRECT, PUNITIVE OR
CONSEQUENTIAL DAMAGES, LOST PROFITS OR LOST DATA, COST OF PROCUREMENT OF
SUBSTITUTE GOODS, TECHNOLOGY, SERVICES, OR ANY CLAIMS BY THIRD PARTIES
(INCLUDING BUT NOT LIMITED TO ANY DEFENSE THEREOF), OR OTHER SIMILAR COSTS.
 *******************************************************************************/
// DOM-IGNORE-END

#include <ctype.h>
#include "app_commands.h"

#if defined(TCPIP_STACK_COMMAND_ENABLE) &&                                  \
    defined(TCPIP_STACK_COMMANDS_WIFI_ENABLE)

extern WF_SCAN_CONTEXT g_wifi_scanContext;

static bool s_scanList_signal = false;
static bool s_consoleWriteComplete = true;

static int _APP_Commands_ScanList(                                          \
                        SYS_CMD_DEVICE_NODE *pCmdIO,                        \
                        int argc,                                           \
                        char **argv);

static void _APP_Commands_ConsoleWriteComplete(void *param);

static const SYS_CMD_DESCRIPTOR appCmdTbl[] =
{
    {"scanlist", _APP_Commands_ScanList, ": list stored scan results"},
};

bool APP_Commands_Init(void)
{
    if (                !SYS_CMD_ADDGRP(appCmdTbl,                          \
                        sizeof(appCmdTbl) / sizeof(*appCmdTbl),             \
                        "app",                                              \
                        ": app commands")) 
    {
        SYS_ERROR(SYS_ERROR_ERROR, "Failed to create APP Commands\r\n", 0);
        return false;
    }

    SYS_CMD_RegisterCallback(                                               \
                        _APP_Commands_ConsoleWriteComplete,                 \
                        SYS_CMD_EVENT_WRITE_COMPLETE);

    s_scanList_signal = false;
    s_consoleWriteComplete = true;

    return true;
}

bool APP_Commands_ScanListDisplay_Get(void)
{
    return s_scanList_signal;
}

void APP_Commands_ScanListEntry_Display(void)
{
    static uint8_t i = 0;
    const WF_SCAN_RESULT *p_scanResult;

    if (!s_consoleWriteComplete) {
#if defined(SYS_CONSOLE_UART_IDX)
        static uint8_t temp_fix_uart_console_cnt = 0;
#if defined(OSAL_USE_RTOS)
        if (temp_fix_uart_console_cnt < 20) {
#else
        if (temp_fix_uart_console_cnt < 200) {
#endif
            ++temp_fix_uart_console_cnt;
            return;
        } else {
            temp_fix_uart_console_cnt = 0;
            s_consoleWriteComplete = true;
        }
#else
        return;
#endif
    }

    if (i == 0) {
        const char *scanListHeader =
            "\r\n\r\n"
            "Wi-Fi Scan Results:\r\n\r\n"
            "    SSID                              RSSI  Channel\r\n"
            "    --------------------------------  ----  -------\r\n";
        SYS_CONSOLE_MESSAGE(scanListHeader);
    }

    if (i < g_wifi_scanContext.numberOfResults) {
        p_scanResult = &(g_wifi_scanContext.results[i]);
#if defined(TCPIP_IF_MRF24WN)
        SYS_CONSOLE_PRINT(                                                  \
                        " %2d)%32s  %2d    %u\r\n",                         \
                        ++i,                                                \
                        p_scanResult->ssid,                                 \
                        p_scanResult->rssi,                                 \
                        p_scanResult->channel);
#else
        SYS_CONSOLE_PRINT(                                                  \
                        " %2d)%32s  %3d   %u\r\n",                          \
                        ++i,                                                \
                        p_scanResult->ssid,                                 \
                        p_scanResult->rssi,                                 \
                        p_scanResult->channel);
#endif
        s_consoleWriteComplete = false;
    } else {
        i = 0;
        s_scanList_signal = false;
    }
}

static int _APP_Commands_ScanList(                                          \
                        SYS_CMD_DEVICE_NODE *pCmdIO,                        \
                        int argc,                                           \
                        char **argv)
{
    const void *cmdIoParam = pCmdIO->cmdIoParam;

    if (argc != 1) {
        (*pCmdIO->pCmdApi->msg)(cmdIoParam, "Usage: scanlist\r\n");
        return false;
    }

    if (g_wifi_scanContext.numberOfResults == 0) {
        SYS_CONSOLE_MESSAGE(                                                \
                "No scan result was previously stored, or no AP found\r\n");
        return true;
    }

    s_scanList_signal = true;

    return true;
}

static void _APP_Commands_ConsoleWriteComplete(void *param)
{
    s_consoleWriteComplete = true;
}

#else

bool APP_Commands_ScanListDisplay_Get(void){
    return 0;
}

void APP_Commands_ScanListEntry_Display(void){

}

#endif // #if defined(TCPIP_STACK_COMMAND_ENABLE) && defined(TCPIP_STACK_COMMANDS_WIFI_ENABLE)
