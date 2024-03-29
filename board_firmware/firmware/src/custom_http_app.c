/*******************************************************************************
  Application to Demo HTTP Server

  Summary:
    Support for HTTP module in Microchip TCP/IP Stack

  Description:
    -Implements the application
    -Reference: RFC 1002
 *******************************************************************************/

/*******************************************************************************
File Name: custom_http_app.c
Copyright (C) 2012 released Microchip Technology Inc.  All rights
reserved.

Microchip licenses to you the right to use, modify, copy and distribute
Software only when embedded on a Microchip microcontroller or digital signal
controller that is integrated into your product or third party product
(pursuant to the sublicense terms in the accompanying license agreement).

You should refer to the license agreement accompanying this Software for
additional information regarding your rights and obligations.

SOFTWARE AND DOCUMENTATION ARE PROVIDED "AS IS" WITHOUT WARRANTY OF ANY KIND,
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

#include <ctype.h>
#include "system_config.h"
#include "system_definitions.h"
#include "HAL/Wifi/WifiApi.h"

#if defined(TCPIP_STACK_USE_HTTP_SERVER)

#include "crypto/crypto.h"
#include "system/random/sys_random.h"
#include "system/tmr/sys_tmr.h"
#include "tcpip/tcpip.h"
#include "tcpip/src/tcpip_types.h"
#include "tcpip/src/common/helpers.h"

/****************************************************************************
  Section:
    Definitions
 ****************************************************************************/
// Use the web page in the Demo App (~2.5kb ROM, ~0b RAM)
#define HTTP_APP_USE_RECONFIG

#ifndef NO_MD5
// Use the MD5 Demo web page (~5kb ROM, ~160b RAM)
#define HTTP_APP_USE_MD5
#endif

// Use the e-mail demo web page
#if defined(TCPIP_STACK_USE_SMTPC)
#define HTTP_APP_USE_EMAIL
#endif

#if defined(TCPIP_IF_MRF24WN) || defined(TCPIP_IF_WINC1500) ||              \
    defined(TCPIP_IF_WILC1000)
#define HTTP_APP_USE_WIFI
#endif

#define HTTP_APP_REDIRECTION_DELAY_TIME (1ul) /* second */
#define HTTP_APP_IPV4_ADDRESS_BUFFER_SIZE 20

/****************************************************************************
  Section:
    Function Prototypes
 ****************************************************************************/
#if defined(TCPIP_HTTP_USE_POST)
    #if defined(SYS_OUT_ENABLE)
        static HTTP_IO_RESULT HTTPPostLCD(HTTP_CONN_HANDLE connHandle);
    #endif
    #if defined(HTTP_APP_USE_MD5)
        static HTTP_IO_RESULT HTTPPostMD5(HTTP_CONN_HANDLE connHandle);
    #endif
    #if defined(HTTP_APP_USE_RECONFIG)
        static HTTP_IO_RESULT HTTPPostConfig(HTTP_CONN_HANDLE connHandle);
        #if defined(TCPIP_STACK_USE_SNMP_SERVER)
        static HTTP_IO_RESULT HTTPPostSNMPCommunity(                        \
                        HTTP_CONN_HANDLE connHandle);
        #endif
    #endif
    #if defined(HTTP_APP_USE_EMAIL) 
        static HTTP_IO_RESULT HTTPPostEmail(HTTP_CONN_HANDLE connHandle);
    #endif
    #if defined(TCPIP_STACK_USE_DYNAMICDNS_CLIENT)
        static HTTP_IO_RESULT HTTPPostDDNSConfig(                           \
                        HTTP_CONN_HANDLE connHandle);
    #endif
    #if defined(HTTP_APP_USE_WIFI)
        static HTTP_IO_RESULT HTTPPostWIFIConfig(                           \
                        HTTP_CONN_HANDLE connHandle);
    #endif
#endif

/****************************************************************************
  Section:
    Variables
 ****************************************************************************/
extern bool g_wifi_redirection_signal;
extern WF_CONFIG g_wifi_cfg;
extern WF_DEVICE_INFO g_wifi_deviceInfo;
extern WF_REDIRECTION_CONFIG g_wifi_redirectionConfig;
extern WF_SCAN_CONTEXT g_wifi_scanContext;

static const WF_SCAN_RESULT *sp_scanResult;
static bool s_scanResultIsValid = false;
static IWPRIV_GET_PARAM s_httpapp_get_param;
static IWPRIV_SET_PARAM s_httpapp_set_param;
static uint8_t s_buf_ipv4addr[HTTP_APP_IPV4_ADDRESS_BUFFER_SIZE];

extern const char *const ddnsServiceHosts[];
// RAM allocated for DDNS parameters
#if defined(TCPIP_STACK_USE_DYNAMICDNS_CLIENT)
    static uint8_t DDNSData[100];
#endif

// Sticky status message variable.
// This is used to indicated whether or not the previous POST operation was
// successful.  The application uses these to store status messages when a
// POST operation redirects.  This lets the application provide status messages
// after a redirect, when connection instance data has already been lost.
static bool lastSuccess = false;

// Stick status message variable.  See lastSuccess for details.
static bool lastFailure = false;

/****************************************************************************
  Section:
    Helper Functions
 ****************************************************************************/

/*******************************************************************************
 * FUNCTION: Helper_HEXCharToBIN
 *
 * RETURNS: binary value associated with ASCII HEX input value
 *
 * PARAMS: hex_char -- ASCII HEX character
 *
 * NOTES: Converts an input ASCII HEX character to its binary value.  Function
 *         does not error check; it assumes only hex characters are passed in.
 *******************************************************************************/
static uint8_t Helper_HEXCharToBIN(uint8_t hex_char)
{
    if ((hex_char >= 'a') && (hex_char <= 'f'))
    {
        return (0x0a + (hex_char - 'a'));
    }
    else if ((hex_char >= 'A') && (hex_char <= 'F'))
    {
        return (0x0a + (hex_char - 'A'));
    }
    else // ((hex_char >= '0') && (hex_char <= '9'))
    {
        return (0x00 + (hex_char - '0'));
    }
}

/*******************************************************************************
 * FUNCTION: Helper_HEXStrToBIN
 *
 * RETURNS: true if conversion successful, else false
 *
 * PARAMS: p_ascii_hex_str -- ASCII HEX string to be converted
 *         p_bin -- binary value if conversion successful
 *
 * NOTES: Converts an input ASCII HEX string to binary value (up to 32-bit value).
 *******************************************************************************/
static bool Helper_HEXStrToBIN(char *p_ascii_hex_str, uint16_t *p_bin)
{
    int8_t i;
    uint32_t multiplier = 1;

    *p_bin = 0;

    // not allowed to have a string of more than 4 nibbles
    if (strlen((char *)p_ascii_hex_str) > 8u)
    {
        return false;
    }

    // first, ensure all characters are a hex digit
    for (i = (uint8_t)strlen((char *)p_ascii_hex_str) - 1; i >= 0 ; --i)
    {
        if (!isxdigit(p_ascii_hex_str[i]))
        {
            return false;
        }
        *p_bin += multiplier * Helper_HEXCharToBIN(p_ascii_hex_str[i]);
        multiplier *= 16;
    }

    return true;
}

static bool Helper_HEXStrToBINInplace(                                      \
                        char *p_str,                                        \
                        uint8_t expected_binary_size)
{
    char str_buffer[3];
    char *ascii_hex_str_start = p_str;
    uint8_t binary_index = 0;
    uint16_t bin_buffer = 0;

    /* gobble up any hex prefix */
    if (memcmp(ascii_hex_str_start, (const char *)"0x", 2) == 0)
    {
        ascii_hex_str_start += 2;
    }

    if (strlen((char *)ascii_hex_str_start) != (expected_binary_size * 2))
    {
        return false;
    }

    while (binary_index < expected_binary_size)
    {
        memcpy(str_buffer, (const char *)ascii_hex_str_start, 2);
        str_buffer[2] = '\0';

        /* convert the hex string to binary value */
        if (!Helper_HEXStrToBIN(str_buffer, &bin_buffer))
        {
            return false;
        }

        p_str[binary_index++] = (uint8_t)bin_buffer;
        ascii_hex_str_start += 2;
    }

    return true;
}

static bool Helper_WIFI_SecuritySelect(                                     \
                        WF_REDIRECTION_CONFIG *cfg,                         \
                        const char *str)
{
    bool ascii_key = false;
    uint8_t key_size = 0;

    switch (cfg->securityMode) {
        case WF_SECURITY_WEP_40:
            key_size = 10; /* Assume hex size. */
            if (strlen(str) == 5) {
                ascii_key = true;
                key_size = 5; /* ASCII key support. */
            }
            cfg->wepKeyIndex = 0; /* Example only uses key index 0. */
            break;
        case WF_SECURITY_WEP_104:
            key_size = 26; /* Assume hex size. */
            if (strlen(str) == 13) {
                ascii_key = true;
                key_size = 13; /* ASCII key support. */
            }
            cfg->wepKeyIndex = 0; /* Example only uses key index 0. */
            break;
#if defined(TCPIP_IF_MRF24WN)
        case WF_SECURITY_WPA_WITH_PASS_PHRASE:
        case WF_SECURITY_WPA2_WITH_PASS_PHRASE:
#endif
        case WF_SECURITY_WPA_AUTO_WITH_PASS_PHRASE:
            ascii_key = true;
            key_size = strlen(str);
            /* Valid WPA passphrase has 8 - 63 characters. */
            if ((key_size < 8 ) || (key_size > 63))
                return false;
            break;
        default:
            ascii_key = true;
            break;
    }
    if (strlen(str) != key_size) {
        SYS_CONSOLE_MESSAGE("\r\nIncomplete key received\r\n");
        return false;
    }
    memcpy(cfg->securityKey, (void *)str, key_size);
    cfg->securityKey[key_size] = 0; /* terminate string */
    if (!ascii_key) {
        key_size /= 2;
        if (!Helper_HEXStrToBINInplace((char *)cfg->securityKey, key_size)) 
        {
            SYS_CONSOLE_MESSAGE("\r\nFailed to convert ASCII string (\
            representing HEX digits) to real HEX string!\r\n");
            return false;
        }
    }
    cfg->securityKeyLen = key_size;
    return true;
}

static void Helper_WIFI_KeySave(                                            \
                        WF_REDIRECTION_CONFIG *redirectCfg,                 \
                        WF_CONFIG *cfg)
{
    uint8_t key_size =0;

    switch ((uint8_t)redirectCfg->securityMode) {
        case WF_SECURITY_WEP_40:
            key_size = 5;
            break;
        case WF_SECURITY_WEP_104:
            key_size = 13;
            break;
#if defined(TCPIP_IF_MRF24WN)
        case WF_SECURITY_WPA_WITH_PASS_PHRASE:
        case WF_SECURITY_WPA2_WITH_PASS_PHRASE:
#endif
        case WF_SECURITY_WPA_AUTO_WITH_PASS_PHRASE:
             // ascii so use strlen
            key_size = strlen((const char *)(redirectCfg->securityKey));
            break;
        default:
            break;
    }
    memcpy(cfg->securityKey, redirectCfg->securityKey, key_size);
    cfg->securityKey[strlen((const char *)(redirectCfg->securityKey))] = 0;
    cfg->securityKeyLen = key_size;
}

static bool Helper_WIFI_IsScanNeeded(void)
{
    /*
     * If current network type is SoftAP and there are valid scan
     * results stored in buffer, no need to scan again.
     * Otherwise, initiate a new scan.
     */
    iwpriv_get(NETWORKTYPE_GET, &s_httpapp_get_param);
    return !(s_httpapp_get_param.netType.type == WF_NETWORK_TYPE_SOFT_AP && \
                        g_wifi_scanContext.numberOfResults > 0 );
}

// helper triggered by a system timer callback function to set the redirection
// flag which is used in app.c or app_wifi to true
static void HTTP_APP_RedirectionFlagSet(uintptr_t context, uint32_t currTick)
{
    g_wifi_redirection_signal = true;
}

// helper to switch to error.htm webpage
static HTTP_IO_RESULT HTTP_APP_ConfigFailure(                               \
                        HTTP_CONN_HANDLE connHandle,                        \
                        uint8_t *httpDataBuff)
{
    lastFailure = true;
    if (httpDataBuff)
        strcpy((char *)httpDataBuff, "/error.htm");
    TCPIP_HTTP_CurrentConnectionStatusSet(connHandle, HTTP_REDIRECT);
    return HTTP_IO_DONE;
}

/****************************************************************************
  Section:
    GET Form Handlers
 ****************************************************************************/

/****************************************************************************
  Function:
    HTTP_IO_RESULT TCPIP_HTTP_GetExecute(HTTP_CONN_HANDLE connHandle)

  Internal:
    See documentation in the TCP/IP Stack APIs or http.h for details.
 ****************************************************************************/
HTTP_IO_RESULT TCPIP_HTTP_GetExecute(HTTP_CONN_HANDLE connHandle)
{
    const uint8_t *ptr;
    uint8_t *httpDataBuff;
    uint8_t filename[20];

    // Load the file name.
    // Make sure uint8_t filename[] above is large enough for your longest name.
    SYS_FS_FileNameGet( TCPIP_HTTP_CurrentConnectionFileGet(connHandle),    \
                        filename,                                           \
                        20);

    httpDataBuff = TCPIP_HTTP_CurrentConnectionDataBufferGet(connHandle);

    // If its the forms.htm page.
    if(!memcmp(filename, "forms.htm", 9))
    {
        // Seek out each of the four LED strings, and if it exists set
        // the LED states.
        ptr = TCPIP_HTTP_ArgGet(httpDataBuff, (const uint8_t *)"led2");
        //if(ptr)
            //BSP_LEDStateSet(APP_LED_3, (*ptr == '1'));
            //LED2_IO = (*ptr == '1');

        ptr = TCPIP_HTTP_ArgGet(httpDataBuff, (const uint8_t *)"led1");
        //if(ptr)
            //BSP_LEDStateSet(APP_LED_2, (*ptr == '1'));
            //LED1_IO = (*ptr == '1');
    }

    else if(!memcmp(filename, "cookies.htm", 11))
    {
        // This is very simple.  The names and values we want are already in
        // the data array.  We just set the hasArgs value to indicate how many
        // name/value pairs we want stored as cookies.
        // To add the second cookie, just increment this value.
        // remember to also add a dynamic variable callback to control the printout.
        TCPIP_HTTP_CurrentConnectionHasArgsSet(connHandle, 0x01);
    }

    // If it's the LED updater file.
    else if(!memcmp(filename, "leds.cgi", 8))
    {
        // Determine which LED to toggle.
        ptr = TCPIP_HTTP_ArgGet(httpDataBuff, (const uint8_t *)"led");

        // Toggle the specified LED.
        switch(*ptr) {
            case '0':
                //BSP_LEDToggle(APP_LED_1);
                //LED0_IO ^= 1;
                break;
            case '1':
                //BSP_LEDToggle(APP_LED_2);
                //LED1_IO ^= 1;
                break;
            case '2':
                //BSP_LEDToggle(APP_LED_3);
                //LED2_IO ^= 1;
                break;
        }
    }

    else if(!memcmp(filename, "scan.cgi", 8))
    {
        uint8_t bssIdx;
        TCPIP_UINT16_VAL bssIdxStr;

        s_httpapp_get_param.cfg.config = &g_wifi_cfg;
        iwpriv_get(CONFIG_GET, &s_httpapp_get_param);

        ptr = TCPIP_HTTP_ArgGet(httpDataBuff, (const uint8_t *)"scan");
        if (ptr) {
            // scan request
            s_scanResultIsValid = false;

            if (Helper_WIFI_IsScanNeeded()) {
                uint8_t i = 0;
                iwpriv_execute(SCAN_START, NULL);
                do {
                    iwpriv_get(SCANSTATUS_GET, &s_httpapp_get_param);
                } while (s_httpapp_get_param.scan.scanStatus ==             \
                        IWPRIV_SCAN_IN_PROGRESS);
                iwpriv_get(SCANRESULTS_COUNT_GET, &s_httpapp_get_param);
                if (s_httpapp_get_param.scan.numberOfResults >              \
                        WF_SCAN_RESULTS_BUFFER_SIZE)
                {
                    g_wifi_scanContext.numberOfResults =                    \
                        WF_SCAN_RESULTS_BUFFER_SIZE;
                }
                else
                {
                    g_wifi_scanContext.numberOfResults =                    \
                        s_httpapp_get_param.scan.numberOfResults;
                }
                while (i < g_wifi_scanContext.numberOfResults) {
                    s_httpapp_get_param.scan.index = i;
                    s_httpapp_get_param.scan.result =                       \
                        &(g_wifi_scanContext.results[i]);
                    iwpriv_get(SCANRESULT_GET, &s_httpapp_get_param);
                    ++i;
                }
            }
        }

        ptr = TCPIP_HTTP_ArgGet(httpDataBuff, (const uint8_t *)"getBss");
        if (ptr) {
            // getBss request
            // use the value to get the nth BSS stored in buffer
            s_scanResultIsValid = false;

            bssIdxStr.v[1] = *(ptr);
            bssIdxStr.v[0] = *(ptr + 1);
            bssIdx = hexatob(bssIdxStr.Val);

            if (bssIdx < g_wifi_scanContext.numberOfResults) {
                sp_scanResult = &(g_wifi_scanContext.results[bssIdx]);
                s_scanResultIsValid = true;
            }
        }
    }

    return HTTP_IO_DONE;
}

/****************************************************************************
  Section:
    POST Form Handlers
 ****************************************************************************/
#if defined(TCPIP_HTTP_USE_POST)

/****************************************************************************
  Function:
    HTTP_IO_RESULT TCPIP_HTTP_PostExecute(HTTP_CONN_HANDLE connHandle)

  Internal:
    See documentation in the TCP/IP Stack APIs or http.h for details.
 ****************************************************************************/
HTTP_IO_RESULT TCPIP_HTTP_PostExecute(HTTP_CONN_HANDLE connHandle)
{
    // Resolve which function to use and pass along
    uint8_t filename[20];

    // Load the file name
    // Make sure uint8_t filename[] above is large enough for your longest name
    SYS_FS_FileNameGet(TCPIP_HTTP_CurrentConnectionFileGet(connHandle), filename, sizeof(filename));

#if defined(SYS_OUT_ENABLE)
    if(!memcmp(filename, "forms.htm", 9))
        return HTTPPostLCD(connHandle);
#endif

#if defined(HTTP_APP_USE_MD5)
    if(!memcmp(filename, "upload.htm", 10))
        return HTTPPostMD5(connHandle);
#endif

#if defined(HTTP_APP_USE_RECONFIG)
    if(!memcmp(filename, "protect/config.htm", 18))
        return HTTPPostConfig(connHandle);
    #if defined(TCPIP_STACK_USE_SNMP_SERVER)
    else if(!memcmp(filename, "snmp/snmpconfig.htm", 19))
        return HTTPPostSNMPCommunity(connHandle);
    #endif
#endif

#if defined(HTTP_APP_USE_EMAIL)
    if(!strcmp((char *)filename, "email/index.htm"))
        return HTTPPostEmail(connHandle);
#endif

#if defined(TCPIP_STACK_USE_DYNAMICDNS_CLIENT)
    if(!strcmp((char *)filename, "dyndns/index.htm"))
        return HTTPPostDDNSConfig(connHandle);
#endif

#if defined(HTTP_APP_USE_WIFI)
    if(!memcmp(filename, "configure.htm", 13))
        return HTTPPostWIFIConfig(connHandle);
#endif

    return HTTP_IO_DONE;
}

/****************************************************************************
  Function:
    static HTTP_IO_RESULT HTTPPostLCD(HTTP_CONN_HANDLE connHandle)

  Summary:
    Processes the LCD form on forms.htm

  Description:
    Locates the 'lcd' parameter and uses it to update the text displayed
    on the board's LCD display.

    This function has four states.  The first reads a name from the data
    string returned as part of the POST request.  If a name cannot
    be found, it returns, asking for more data.  Otherwise, if the name
    is expected, it reads the associated value and writes it to the LCD.
    If the name is not expected, the value is discarded and the next name
    parameter is read.

    In the case where the expected string is never found, this function
    will eventually return HTTP_IO_NEED_DATA when no data is left.  In that
    case, the HTTP server will automatically trap the error and issue an
    Internal Server Error to the browser.

  Precondition:
    None

  Parameters:
    connHandle  - HTTP connection handle

  Return Values:
    HTTP_IO_DONE - the parameter has been found and saved
    HTTP_IO_WAITING - the function is pausing to continue later
    HTTP_IO_NEED_DATA - data needed by this function has not yet arrived
 ****************************************************************************/
#if defined(SYS_OUT_ENABLE)
static HTTP_IO_RESULT HTTPPostLCD(HTTP_CONN_HANDLE connHandle)
{
    uint8_t *cDest;
    uint8_t *httpDataBuff;
    uint16_t httpBuffSize;

    #define SM_POST_LCD_READ_NAME       (0u)
    #define SM_POST_LCD_READ_VALUE      (1u)

    httpDataBuff = TCPIP_HTTP_CurrentConnectionDataBufferGet(connHandle);
    httpBuffSize = TCPIP_HTTP_CurrentConnectionDataBufferSizeGet(connHandle);
    switch(TCPIP_HTTP_CurrentConnectionPostSmGet(connHandle))
    {
        // Find the name
        case SM_POST_LCD_READ_NAME:

            // Read a name
            if(TCPIP_HTTP_PostNameRead(connHandle, httpDataBuff, httpBuffSize) == HTTP_READ_INCOMPLETE)
                return HTTP_IO_NEED_DATA;

            TCPIP_HTTP_CurrentConnectionPostSmSet(connHandle, SM_POST_LCD_READ_VALUE);
            // No break...continue reading value

        // Found the value, so store the LCD and return
        case SM_POST_LCD_READ_VALUE:

            // If value is expected, read it to data buffer,
            // otherwise ignore it (by reading to NULL)
            if(!strcmp((char *)httpDataBuff, (const char *)"lcd"))
                cDest = httpDataBuff;
            else
                cDest = NULL;

            // Read a value string
            if(TCPIP_HTTP_PostValueRead(connHandle, cDest, httpBuffSize) == HTTP_READ_INCOMPLETE)
                return HTTP_IO_NEED_DATA;

            // If this was an unexpected value, look for a new name
            if(!cDest)
            {
                TCPIP_HTTP_CurrentConnectionPostSmSet(connHandle, SM_POST_LCD_READ_NAME);
                break;
            }

            SYS_OUT_MESSAGE((char *)cDest);

            // This is the only expected value, so callback is done
            strcpy((char *)httpDataBuff, "/forms.htm");
            TCPIP_HTTP_CurrentConnectionStatusSet(connHandle, HTTP_REDIRECT);
            return HTTP_IO_DONE;
    }

    // Default assumes that we're returning for state machine convenience.
    // Function will be called again later.
    return HTTP_IO_WAITING;
}
#endif

/****************************************************************************
  Function:
    static HTTP_IO_RESULT HTTPPostMD5(HTTP_CONN_HANDLE connHandle)

  Summary:
    Processes the file upload form on upload.htm

  Description:
    This function demonstrates the processing of file uploads.  First, the
    function locates the file data, skipping over any headers that arrive.
    Second, it reads the file 64 bytes at a time and hashes that data.  Once
    all data has been received, the function calculates the MD5 sum and
    stores it in current connection data buffer.

    After the headers, the first line from the form will be the MIME
    separator.  Following that is more headers about the file, which we
    discard.  After another CRLFCRLF, the file data begins, and we read
    it 16 bytes at a time and add that to the MD5 calculation.  The reading
    terminates when the separator string is encountered again on its own
    line.  Notice that the actual file data is trashed in this process,
    allowing us to accept files of arbitrary size, not limited by RAM.
    Also notice that the data buffer is used as an arbitrary storage array
    for the result.  The ~uploadedmd5~ callback reads this data later to
    send back to the client.

  Precondition:
    None

  Parameters:
    connHandle  - HTTP connection handle

  Return Values:
    HTTP_IO_DONE - all parameters have been processed
    HTTP_IO_WAITING - the function is pausing to continue later
    HTTP_IO_NEED_DATA - data needed by this function has not yet arrived
 ****************************************************************************/
#if defined(HTTP_APP_USE_MD5)
static HTTP_IO_RESULT HTTPPostMD5(HTTP_CONN_HANDLE connHandle)
{
    static CRYPT_MD5_CTX md5;
    uint8_t *httpDataBuff;
    uint32_t lenA, lenB;
    TCP_SOCKET sktHTTP;

    #define SM_MD5_READ_SEPARATOR   (0u)
    #define SM_MD5_SKIP_TO_DATA     (1u)
    #define SM_MD5_READ_DATA        (2u)
    #define SM_MD5_POST_COMPLETE    (3u)

    sktHTTP = TCPIP_HTTP_CurrentConnectionSocketGet(connHandle);
    switch(TCPIP_HTTP_CurrentConnectionPostSmGet(connHandle))
    {
        // Just started, so try to find the separator string
        case SM_MD5_READ_SEPARATOR:
            // Reset the MD5 calculation
            CRYPT_MD5_Initialize(&md5);

            // See if a CRLF is in the buffer
            lenA = TCPIP_TCP_ArrayFind(                                     \
                        sktHTTP,                                            \
                        (const uint8_t *)"\r\n",                            \
                        2,                                                  \
                        0,                                                  \
                        0,                                                  \
                        false);
            if(lenA == 0xffff)
            {   // if not, ask for more data
                return HTTP_IO_NEED_DATA;
            }

            // If so, figure out where the last byte of data is
            // Data ends at CRLFseparator--CRLF, so len + 6 bytes
            TCPIP_HTTP_CurrentConnectionByteCountDec(connHandle, lenA + 6);

            // Read past the CRLF
            TCPIP_HTTP_CurrentConnectionByteCountDec(                       \
                        connHandle,                                         \
                        TCPIP_TCP_ArrayGet(sktHTTP, NULL, lenA + 2));

            // Save the next state (skip to CRLFCRLF)
            TCPIP_HTTP_CurrentConnectionPostSmSet(                          \
                        connHandle, SM_MD5_SKIP_TO_DATA);

            // No break...continue reading the headers if possible

        // Skip the headers
        case SM_MD5_SKIP_TO_DATA:
            // Look for the CRLFCRLF
            lenA = TCPIP_TCP_ArrayFind(                                     \
                        sktHTTP,                                            \
                        (const uint8_t *)"\r\n\r\n",                        \
                        4,                                                  \
                        0,                                                  \
                        0,                                                  \
                        false);

            if(lenA != 0xffff)
            {// Found it, so remove all data up to and including
                lenA = TCPIP_TCP_ArrayGet(sktHTTP, NULL, lenA + 4);
                TCPIP_HTTP_CurrentConnectionByteCountDec(connHandle, lenA);
                TCPIP_HTTP_CurrentConnectionPostSmSet(                      \
                        connHandle,                                         \
                        SM_MD5_READ_DATA);
            }
            else
            {// Otherwise, remove as much as possible
                lenA = TCPIP_TCP_ArrayGet(                                  \
                        sktHTTP,                                            \
                        NULL,                                               \
                        TCPIP_TCP_GetIsReady(sktHTTP) - 4);
                TCPIP_HTTP_CurrentConnectionByteCountDec(connHandle, lenA);

                // Return the need more data flag
                return HTTP_IO_NEED_DATA;
            }

            // No break if we found the header terminator

        // Read and hash file data
        case SM_MD5_READ_DATA:
            // Find out how many bytes are available to be read
            httpDataBuff = TCPIP_HTTP_CurrentConnectionDataBufferGet(connHandle);
            lenA = TCPIP_TCP_GetIsReady(sktHTTP);
            lenB = TCPIP_HTTP_CurrentConnectionByteCountGet(connHandle);
            if(lenA > lenB)
                lenA = lenB;

            while(lenA > 0u)
            {// Add up to 64 bytes at a time to the sum
                lenB = TCPIP_TCP_ArrayGet(                                  \
                        sktHTTP,                                            \
                        httpDataBuff,                                       \
                        (lenA < 64u)?lenA:64);
                TCPIP_HTTP_CurrentConnectionByteCountDec(connHandle, lenB);
                lenA -= lenB;
                CRYPT_MD5_DataAdd(&md5,httpDataBuff, lenB);
            }

            // If we've read all the data
            if(TCPIP_HTTP_CurrentConnectionByteCountGet(connHandle) == 0u)
            {// Calculate and copy result data buffer for printout
                TCPIP_HTTP_CurrentConnectionPostSmSet(                      \
                        connHandle,                                         \
                        SM_MD5_POST_COMPLETE);
                CRYPT_MD5_Finalize(&md5, httpDataBuff);
                return HTTP_IO_DONE;
            }

            // Ask for more data
            return HTTP_IO_NEED_DATA;
    }

    return HTTP_IO_DONE;
}
#endif // #if defined(HTTP_APP_USE_MD5)

/*******************************************************************************
  Function:
    static HTTP_IO_RESULT HTTPPostConfig(HTTP_CONN_HANDLE connHandle)

  Summary:
    Processes the configuration form on config/index.htm

  Description:
    Accepts configuration parameters from the form, saves them to a
    temporary location in RAM, then eventually saves the data to EEPROM or
    external Flash.

    When complete, this function redirects to config/reboot.htm, which will
    display information on reconnecting to the board.

    This function creates a shadow copy of a network info structure in
    RAM and then overwrites incoming data there as it arrives.  For each
    name/value pair, the name is first read to cur connection data[0:5].  Next, the
    value is read to newNetConfig.  Once all data has been read, the new
    network info structure is saved back to storage and the browser is redirected to
    reboot.htm.  That file includes an AJAX call to reboot.cgi, which
    performs the actual reboot of the machine.

    If an IP address cannot be parsed, too much data is POSTed, or any other
    parsing error occurs, the browser reloads config.htm and displays an error
    message at the top.

  Precondition:
    None

  Parameters:
    connHandle  - HTTP connection handle

  Return Values:
    HTTP_IO_DONE - all parameters have been processed
    HTTP_IO_NEED_DATA - data needed by this function has not yet arrived
 *******************************************************************************/
#if defined(HTTP_APP_USE_RECONFIG)
// network configuration/information storage space
static struct
{
    TCPIP_NET_HANDLE    currNet;            // current working interface + valid flag
    char                ifName[10 + 1];     // interface name
    char                nbnsName[16 + 1];   // host name
    char                ifMacAddr[17 + 1];  // MAC address
    char                ipAddr[15 +1];      // IP address
    char                ipMask[15 + 1];     // mask
    char                gwIP[15 + 1];       // gateway IP address
    char                dns1IP[15 + 1];     // DNS IP address
    char                dns2IP[15 + 1];     // DNS IP address

    TCPIP_NETWORK_CONFIG   netConfig;  // configuration in the interface requested format
}httpNetData;

static HTTP_IO_RESULT HTTPPostConfig(HTTP_CONN_HANDLE connHandle)
{
    bool bConfigFailure = false;
    uint8_t *httpDataBuff = 0;
    uint16_t httpBuffSize;
    uint8_t i;
    uint32_t byteCount;
    IPV4_ADDR newIPAddress, newMask;
    TCP_SOCKET sktHTTP;
    TCPIP_MAC_ADDR newMACAddr;

    httpNetData.currNet = 0; // forget the old settings

    // Check to see if the browser is attempting to submit more data than we
    // can parse at once.  This function needs to receive all updated
    // parameters and validate them all before committing them to memory so that
    // orphaned configuration parameters do not get written (for example, if a
    // static IP address is given, but the subnet mask fails parsing, we
    // should not use the static IP address).  Everything needs to be processed
    // in a single transaction.  If this is impossible, fail and notify the user.
    // As a web devloper, if you add parameters to the network info and run into this
    // problem, you could fix this by to splitting your update web page into two
    // seperate web pages (causing two transactional writes).  Alternatively,
    // you could fix it by storing a static shadow copy of network info someplace
    // in memory and using it when info is complete.
    // Lastly, you could increase the TCP RX FIFO size for the HTTP server.
    // This will allow more data to be POSTed by the web browser before hitting this limit.
    byteCount = TCPIP_HTTP_CurrentConnectionByteCountGet(connHandle);
    sktHTTP = TCPIP_HTTP_CurrentConnectionSocketGet(connHandle);
    if(byteCount > TCPIP_TCP_GetIsReady(sktHTTP) + TCPIP_TCP_FifoRxFreeGet(sktHTTP))
    {   // Configuration Failure
        lastFailure = true;
        TCPIP_HTTP_CurrentConnectionStatusSet(connHandle, HTTP_REDIRECT);
        return HTTP_IO_DONE;
    }

    // Ensure that all data is waiting to be parsed.  If not, keep waiting for
    // all of it to arrive.
    if(TCPIP_TCP_GetIsReady(sktHTTP) < byteCount)
        return HTTP_IO_NEED_DATA;

    // Use current config in non-volatile memory as defaults
    httpDataBuff = TCPIP_HTTP_CurrentConnectionDataBufferGet(connHandle);
    httpBuffSize = TCPIP_HTTP_CurrentConnectionDataBufferSizeGet(connHandle);

    // Read all browser POST data
    while(TCPIP_HTTP_CurrentConnectionByteCountGet(connHandle))
    {
        // Read a form field name
        if(TCPIP_HTTP_PostNameRead(connHandle, httpDataBuff, 6) != HTTP_READ_OK)
        {
            bConfigFailure = true;
            break;
        }

        // Read a form field value
        if(TCPIP_HTTP_PostValueRead(                                        \
                        connHandle,                                         \
                        httpDataBuff + 6,                                   \
                        httpBuffSize - 6 - 2) != HTTP_READ_OK)
        {
            bConfigFailure = true;
            break;
        }

        // Parse the value that was read
        if(!strcmp((char *)httpDataBuff, (const char *)"ip"))
        {   // Save new static IP Address
            if(!TCPIP_Helper_StringToIPAddress(                             \
                        (char *)(httpDataBuff + 6),                         \
                        &newIPAddress))
            {
                bConfigFailure = true;
                break;
            }
            strncpy(    httpNetData.ipAddr,                                 \
                        (char *)httpDataBuff + 6,                           \
                        sizeof(httpNetData.ipAddr));
        }
        else if(!strcmp((char *)httpDataBuff, (const char *)"gw"))
        {   // Read new gateway address
            if(!TCPIP_Helper_StringToIPAddress(                             \
                        (char *)(httpDataBuff + 6),                         \
                        &newIPAddress))
            {
                bConfigFailure = true;
                break;
            }
            strncpy(    httpNetData.gwIP,                                   \
                        (char *)httpDataBuff + 6,                           \
                        sizeof(httpNetData.gwIP));
        }
        else if(!strcmp((char *)httpDataBuff, (const char *)"sub"))
        {   // Read new static subnet
            if(!TCPIP_Helper_StringToIPAddress(                             \
                        (char *)(httpDataBuff + 6),                         \
                        &newMask))
            {
                bConfigFailure = true;
                break;
            }
            strncpy(    httpNetData.ipMask,                                 \
                        (char *)httpDataBuff + 6,                           \
                        sizeof(httpNetData.ipMask));
        }
        else if(!strcmp((char *)httpDataBuff, (const char *)"dns1"))
        {   // Read new primary DNS server
            if(!TCPIP_Helper_StringToIPAddress(                             \
                        (char *)(httpDataBuff + 6),                         \
                        &newIPAddress))
            {
                bConfigFailure = true;
                break;
            }
            strncpy(    httpNetData.dns1IP,                                 \
                        (char *)httpDataBuff + 6,                           \
                        sizeof(httpNetData.dns1IP));
        }
        else if(!strcmp((char *)httpDataBuff, (const char *)"dns2"))
        {   // Read new secondary DNS server
            if(!TCPIP_Helper_StringToIPAddress(                             \
                        (char *)(httpDataBuff + 6),                         \
                        &newIPAddress))
            {
                bConfigFailure = true;
                break;
            }
            strncpy(    httpNetData.dns2IP,                                 \
                        (char *)httpDataBuff + 6,                           \
                        sizeof(httpNetData.dns2IP));
        }
        else if(!strcmp((char *)httpDataBuff, (const char *)"mac"))
        {   // read the new MAC address
            if(!TCPIP_Helper_StringToMACAddress(                            \
                        (char *)(httpDataBuff + 6),                         \
                        newMACAddr.v))
            {
                bConfigFailure = true;
                break;
            }
            strncpy(    httpNetData.ifMacAddr,                              \
                        (char *)httpDataBuff + 6,                           \
                        sizeof(httpNetData.ifMacAddr));
        }
        else if(!strcmp((char *)httpDataBuff, (const char *)"host"))
        {   // Read new hostname
            strncpy(    httpNetData.nbnsName,                               \
                        (char *)httpDataBuff + 6,                           \
                        sizeof(httpNetData.nbnsName));
        }
        else if(!strcmp((char *)httpDataBuff, (const char *)"dhcp"))
        {   // Read new DHCP Enabled flag
            httpNetData.netConfig.startFlags = httpDataBuff[6] == '1' ? TCPIP_NETWORK_CONFIG_DHCP_CLIENT_ON : 0;
        }
    }

    if(bConfigFailure == false)
    {
        // All parsing complete!  Save new settings and force an interface restart
        // Set the interface to restart and display reconnecting information
        strcpy((char *)httpDataBuff, "/protect/reboot.htm?");
        TCPIP_Helper_FormatNetBIOSName((uint8_t *)httpNetData.nbnsName);
        memcpy((void *)(httpDataBuff + 20), httpNetData.nbnsName, 16);
        httpDataBuff[20 + 16] = 0x00; // Force null termination
        for(i = 20; i < 20u + 16u; ++i)
        {
            if(httpDataBuff[i] == ' ')
                httpDataBuff[i] = 0x00;
        }
        // save current interface and mark as valid
        httpNetData.currNet = TCPIP_TCP_SocketNetGet(sktHTTP);
        strncpy(        httpNetData.ifName,                                 \
                        TCPIP_STACK_NetNameGet(httpNetData.currNet),        \
                        sizeof(httpNetData.ifName));
    }
    else
    {   // Configuration error
        lastFailure = true;
        if(httpDataBuff)
        {
            strcpy((char *)httpDataBuff, "/protect/config.htm");
        }
    }

    TCPIP_HTTP_CurrentConnectionStatusSet(connHandle, HTTP_REDIRECT);

    return HTTP_IO_DONE;
}

#if defined(TCPIP_STACK_USE_SNMP_SERVER)
static HTTP_IO_RESULT HTTPPostSNMPCommunity(HTTP_CONN_HANDLE connHandle)
{
    uint8_t vCommunityIndex;
    uint8_t *httpDataBuff;
    uint16_t httpBuffSize;
    uint8_t len = 0;

    #define SM_CFG_SNMP_READ_NAME   (0u)
    #define SM_CFG_SNMP_READ_VALUE  (1u)

    httpDataBuff = TCPIP_HTTP_CurrentConnectionDataBufferGet(connHandle);
    httpBuffSize = TCPIP_HTTP_CurrentConnectionDataBufferSizeGet(connHandle);
    switch(TCPIP_HTTP_CurrentConnectionPostSmGet(connHandle))
    {
        case SM_CFG_SNMP_READ_NAME:
            // If all parameters have been read, end
            if(TCPIP_HTTP_CurrentConnectionByteCountGet(connHandle) == 0u)
            {
                return HTTP_IO_DONE;
            }

            // Read a name
            if(TCPIP_HTTP_PostNameRead(connHandle, httpDataBuff, httpBuffSize) == HTTP_READ_INCOMPLETE)
                return HTTP_IO_NEED_DATA;

            // Move to reading a value, but no break
            TCPIP_HTTP_CurrentConnectionPostSmSet(connHandle, SM_CFG_SNMP_READ_VALUE);

        case SM_CFG_SNMP_READ_VALUE:
            // Read a value
            if(TCPIP_HTTP_PostValueRead(connHandle, httpDataBuff + 6, httpBuffSize) == HTTP_READ_INCOMPLETE)
                return HTTP_IO_NEED_DATA;

            // Default action after this is to read the next name, unless there's an error
            TCPIP_HTTP_CurrentConnectionPostSmSet(connHandle, SM_CFG_SNMP_READ_NAME);

            // See if this is a known parameter and legal (must be null
            // terminator in 4th field name byte, string must no greater than
            // TCPIP_SNMP_COMMUNITY_MAX_LEN bytes long, and TCPIP_SNMP_MAX_COMMUNITY_SUPPORT
            // must not be violated.
            vCommunityIndex = httpDataBuff[3] - '0';
            if(vCommunityIndex >= TCPIP_SNMP_MAX_COMMUNITY_SUPPORT)
                break;
            if(httpDataBuff[4] != 0x00u)
                break;
            len = strlen((char *)httpDataBuff + 6);
            if(len > TCPIP_SNMP_COMMUNITY_MAX_LEN)
            {
                break;
            }
            if(memcmp((void *)httpDataBuff, (const void *)"rcm", 3) == 0)
            {
                if(TCPIP_SNMP_ReadCommunitySet(vCommunityIndex,len,httpDataBuff + 6)!=true)
                    break;
            }
            else if(memcmp((void *)httpDataBuff, (const void *)"wcm", 3) == 0)
            {
                if(TCPIP_SNMP_WriteCommunitySet(vCommunityIndex,len,httpDataBuff + 6) != true)
                    break;
            }
            else
            {
                break;
            }

            break;
    }

    return HTTP_IO_WAITING; // Assume we're waiting to process more data
}
#endif // #if defined(TCPIP_STACK_USE_SNMP_SERVER)
#endif // #if defined(HTTP_APP_USE_RECONFIG)

/****************************************************************************
  Function:
    static HTTP_IO_RESULT HTTPPostEmail(void)

  Summary:
    Processes the e-mail form on email/index.htm

  Description:
    This function sends an e-mail message using the SMTPC client.
    If encryption is needed it is done by the SMTPC module communicating with the SMTP server.
    (the NET_PRES layer has to be configured for encryption support).

    It demonstrates the use of the SMTPC client, waiting for asynchronous
    processes in an HTTP callback.

  Precondition:
    None

  Parameters:
    connHandle  - HTTP connection handle

  Return Values:
    HTTP_IO_DONE - the message has been sent
    HTTP_IO_WAITING - the function is waiting for the SMTP process to complete
    HTTP_IO_NEED_DATA - data needed by this function has not yet arrived
 ****************************************************************************/
#if defined(HTTP_APP_USE_EMAIL)
// size of an email parameter
#define HTTP_APP_EMAIL_PARAM_SIZE           30 
// maximum size of the mail body
#define HTTP_APP_EMAIL_BODY_SIZE            200 
// maximum size of the mail attachment
#define HTTP_APP_EMAIL_ATTACHMENT_SIZE      200 

// handle of the mail message submitted to SMTPC
static TCPIP_SMTPC_MESSAGE_HANDLE postMailHandle = 0;

// structure describing the post email operation
typedef struct
{
    char*   ptrParam;       // pointer to the current parameter being retrieved
    int     paramSize;      // size of the buffer to retrieve the parameter
    int     attachLen;      // length of the attachment buffer
    bool    mailParamsDone; // flag that signals that all parameters were retrieved
    TCPIP_SMTPC_ATTACH_BUFFER attachBuffer; // descriptor for the attachment
    TCPIP_SMTPC_MESSAGE_RESULT mailRes;     // operation outcome

    // storage area
    char serverName[HTTP_APP_EMAIL_PARAM_SIZE + 1];
    char username[HTTP_APP_EMAIL_PARAM_SIZE + 1];
    char password[HTTP_APP_EMAIL_PARAM_SIZE + 1];
    char mailTo[HTTP_APP_EMAIL_PARAM_SIZE + 1];
    char serverPort[10 + 1];
    char mailBody[HTTP_APP_EMAIL_BODY_SIZE + 1];
    char mailAttachment[HTTP_APP_EMAIL_ATTACHMENT_SIZE];

}HTTP_POST_EMAIL_DCPT;

static HTTP_POST_EMAIL_DCPT postEmail;

// callback for getting the signal of mail completion
static void postMailCallback(TCPIP_SMTPC_MESSAGE_HANDLE messageHandle, const TCPIP_SMTPC_MESSAGE_REPORT* pMailReport)
{
    postEmail.mailRes = pMailReport->messageRes;
    if(postEmail.mailRes < 0)
    {
        SYS_CONSOLE_PRINT("SMTPC mail FAILED! Callback result: %d\r\n", postEmail.mailRes);
    }
    else
    {
        SYS_CONSOLE_MESSAGE("SMTPC mail SUCCESS!\r\n");
    }
}

static HTTP_IO_RESULT HTTPPostEmail(HTTP_CONN_HANDLE connHandle)
{

    TCPIP_SMTPC_MAIL_MESSAGE mySMTPMessage;
    char paramName[HTTP_APP_EMAIL_PARAM_SIZE + 1];

    #define SM_EMAIL_INIT                       (0)
    #define SM_EMAIL_READ_PARAM_NAME            (1)
    #define SM_EMAIL_READ_PARAM_VALUE           (2)
    #define SM_EMAIL_SEND_MESSAGE               (3)
    #define SM_EMAIL_WAIT_RESULT                (4)

    switch(TCPIP_HTTP_CurrentConnectionPostSmGet(connHandle))
    {
        case SM_EMAIL_INIT:
            if(postMailHandle != 0)
            {   // some other operation on going
                return HTTP_IO_WAITING;
            }

            memset(&postEmail, 0, sizeof(postEmail));
                TCPIP_HTTP_CurrentConnectionPostSmSet(connHandle, SM_EMAIL_READ_PARAM_NAME);
            return HTTP_IO_WAITING;


        case SM_EMAIL_READ_PARAM_NAME:
            // Search for a parameter name in POST data
            if(TCPIP_HTTP_PostNameRead(connHandle, (uint8_t*)paramName, sizeof(paramName)) == HTTP_READ_INCOMPLETE)
            {
                return HTTP_IO_NEED_DATA;
            }

            // Try to match the name value
            if(!strcmp(paramName, (const char *)"server"))
            {   // Read the server name
                postEmail.ptrParam = postEmail.serverName;
                postEmail.paramSize = sizeof(postEmail.serverName) - 1;
            }
            else if(!strcmp(paramName, (const char *)"user"))
            {   // Read the user name
                postEmail.ptrParam = postEmail.username;
                postEmail.paramSize = sizeof(postEmail.username) - 1;
            }
            else if(!strcmp(paramName, (const char *)"pass"))
            {   // Read the password
                postEmail.ptrParam = postEmail.password;
                postEmail.paramSize = sizeof(postEmail.password) - 1;
            }
            else if(!strcmp(paramName, (const char *)"to"))
            {   // Read the To string
                postEmail.ptrParam = postEmail.mailTo;
                postEmail.paramSize = sizeof(postEmail.mailTo) - 1;
            }
            else if(!strcmp(paramName, (const char *)"port"))
            {   // Read the server port
                postEmail.ptrParam = postEmail.serverPort;
                postEmail.paramSize = sizeof(postEmail.serverPort) - 1;
            }
            else if(!strcmp(paramName, (const char *)"msg"))
            {   // Read the server port
                postEmail.ptrParam = postEmail.mailBody;
                postEmail.paramSize = sizeof(postEmail.mailBody) - 1;
                postEmail.mailParamsDone = true;
            }
            else
            {   // unknown parameter
                postEmail.ptrParam = 0;
                postEmail.paramSize = 0;
            }

            // read the parameter now
            TCPIP_HTTP_CurrentConnectionPostSmSet(connHandle, SM_EMAIL_READ_PARAM_VALUE);
            return HTTP_IO_WAITING;


        case SM_EMAIL_READ_PARAM_VALUE:
            // Search for a parameter value in POST data
            if(TCPIP_HTTP_PostValueRead(connHandle, (uint8_t*)postEmail.ptrParam, postEmail.paramSize) == HTTP_READ_INCOMPLETE)
                return HTTP_IO_NEED_DATA;

            // end parameter properly
            postEmail.ptrParam[postEmail.paramSize] = 0;

            // check if we're done with the parameters
            TCPIP_HTTP_CurrentConnectionPostSmSet(                          \
            connHandle,                                                     \
            postEmail.mailParamsDone == true ? SM_EMAIL_SEND_MESSAGE : SM_EMAIL_READ_PARAM_NAME);
            return HTTP_IO_WAITING;

        case SM_EMAIL_SEND_MESSAGE:
            // prepare the message attachment
            // output the system status as a CSV file.
            // Write the header and button strings
            postEmail.attachLen = sprintf(                                  \
                        postEmail.mailAttachment,                           \
                        "SYSTEM STATUS\r\nButtons:,%c,%c,%c\r\n",           \
                        APP_SWITCH_1StateGet() + '0',                       \
                        APP_SWITCH_2StateGet() + '0',                       \
                        APP_SWITCH_3StateGet() + '0');
            // Write the header and button strings
            postEmail.attachLen += sprintf(                                 \
                        postEmail.mailAttachment + postEmail.attachLen,     \
                        "LEDs:,%c,%c,%c\r\n",                               \
                        BSP_LEDStateGet(APP_LED_1) + '0',                   \
                        BSP_LEDStateGet(APP_LED_2) + '0',                   \
                        BSP_LEDStateGet(APP_LED_3) + '0');
            // add a potentiometer read: a random string
            postEmail.attachLen += sprintf(                                 \
                        postEmail.mailAttachment + postEmail.attachLen,     \
                        "Pot:,%d\r\n",                                      \
                        SYS_RANDOM_PseudoGet());

            // prepare the message itself
            memset(&mySMTPMessage, 0, sizeof(mySMTPMessage));
            mySMTPMessage.body = (const uint8_t*)postEmail.mailBody;
            mySMTPMessage.bodySize = strlen(postEmail.mailBody);
            mySMTPMessage.smtpServer = postEmail.serverName;
            mySMTPMessage.serverPort = (uint16_t)atol(postEmail.serverPort);
            mySMTPMessage.username = postEmail.username;
            mySMTPMessage.password = postEmail.password;
            mySMTPMessage.to = postEmail.mailTo;
            mySMTPMessage.from = "\"SMTP Service\" <mchpboard@picsaregood.com>";
            mySMTPMessage.subject = "Microchip TCP/IP Stack Status Update";

            // set the buffer attachment
            postEmail.attachBuffer.attachType = TCPIP_SMTPC_ATTACH_TYPE_TEXT;
            postEmail.attachBuffer.attachEncode = TCPIP_SMTPC_ENCODE_TYPE_7BIT;
            postEmail.attachBuffer.attachName = "status.csv";
            postEmail.attachBuffer.attachBuffer = (const uint8_t*)postEmail.mailAttachment;
            postEmail.attachBuffer.attachSize = postEmail.attachLen;
            mySMTPMessage.attachBuffers = &postEmail.attachBuffer;
            mySMTPMessage.nBuffers = 1;
            // set the notification function
            mySMTPMessage.messageCallback = postMailCallback;

            postMailHandle = TCPIP_SMTPC_MailMessage(&mySMTPMessage, &postEmail.mailRes);
            if(postMailHandle == 0)
            {   // failed
                SYS_CONSOLE_PRINT("SMTPC mail: Failed to submit message: %d!\r\n", postEmail.mailRes);
            }
            else
            {
                postEmail.mailRes = TCPIP_SMTPC_RES_PENDING;
                SYS_CONSOLE_MESSAGE("SMTPC mail: Submitted the mail message!\r\n");
            }

            TCPIP_HTTP_CurrentConnectionPostSmSet(connHandle, SM_EMAIL_WAIT_RESULT);
                return HTTP_IO_WAITING;

        case SM_EMAIL_WAIT_RESULT:
            // Wait for status done
            if(postEmail.mailRes == TCPIP_SMTPC_RES_PENDING)
            {   // not done yet
                return HTTP_IO_WAITING;
            }

            // done
            postMailHandle = 0;

            if(postEmail.mailRes == TCPIP_SMTPC_RES_OK)
            {
                lastSuccess = true;
            }
            else
            {
                lastFailure = true;
            }

                // Redirect to the page
            strcpy((char *)TCPIP_HTTP_CurrentConnectionDataBufferGet(connHandle), "/email/index.htm");
                TCPIP_HTTP_CurrentConnectionStatusSet(connHandle, HTTP_REDIRECT);
                return HTTP_IO_DONE;
            }

    return HTTP_IO_DONE;
}
#endif // #if defined(HTTP_APP_USE_EMAIL)

/****************************************************************************
  Function:
    HTTP_IO_RESULT HTTPPostDDNSConfig(HTTP_CONN_HANDLE connHandle)

  Summary:
    Parsing and collecting http data received from http form

  Description:
    This routine will be excuted every time the Dynamic DNS Client
    configuration form is submitted.  The http data is received
    as a string of the variables seperated by '&' characters in the TCP RX
    buffer.  This data is parsed to read the required configuration values,
    and those values are populated to the global array (DDNSData) reserved
    for this purpose.  As the data is read, DDNSPointers is also populated
    so that the dynamic DNS client can execute with the new parameters.

  Precondition:
    cur HTTP connection is loaded

  Parameters:
    connHandle  - HTTP connection handle

  Return Values:
    HTTP_IO_DONE        -  Finished with procedure
    HTTP_IO_NEED_DATA   -  More data needed to continue, call again later
    HTTP_IO_WAITING     -  Waiting for asynchronous process to complete,
                            call again later
 ****************************************************************************/
#if defined(TCPIP_STACK_USE_DYNAMICDNS_CLIENT)
static HTTP_IO_RESULT HTTPPostDDNSConfig(HTTP_CONN_HANDLE connHandle)
{
    static uint8_t *ptrDDNS;
    uint8_t *httpDataBuff;
    uint16_t httpBuffSize;
    uint8_t smPost;

    #define SM_DDNS_START           (0u)
    #define SM_DDNS_READ_NAME       (1u)
    #define SM_DDNS_READ_VALUE      (2u)
    #define SM_DDNS_READ_SERVICE    (3u)
    #define SM_DDNS_DONE            (4u)

    #define DDNS_SPACE_REMAINING    (sizeof(DDNSData) - (ptrDDNS - DDNSData))

    httpDataBuff = TCPIP_HTTP_CurrentConnectionDataBufferGet(connHandle);
    httpBuffSize = TCPIP_HTTP_CurrentConnectionDataBufferSizeGet(connHandle);
    smPost = TCPIP_HTTP_CurrentConnectionPostSmGet(connHandle);
    switch(smPost)
    {
        // Sets defaults for the system
        case SM_DDNS_START:
            ptrDDNS = DDNSData;
            TCPIP_DDNS_ServiceSet(0);
            DDNSClient.Host.szROM = NULL;
            DDNSClient.Username.szROM = NULL;
            DDNSClient.Password.szROM = NULL;
            DDNSClient.ROMPointers.Host = 0;
            DDNSClient.ROMPointers.Username = 0;
            DDNSClient.ROMPointers.Password = 0;
            TCPIP_HTTP_CurrentConnectionPostSmSet(connHandle, ++smPost);

        // Searches out names and handles them as they arrive
        case SM_DDNS_READ_NAME:
            // If all parameters have been read, end
            if(TCPIP_HTTP_CurrentConnectionByteCountGet(connHandle) == 0u)
            {
                TCPIP_HTTP_CurrentConnectionPostSmSet(connHandle, SM_DDNS_DONE);
                break;
            }

            // Read a name
            if(TCPIP_HTTP_PostNameRead(connHandle, httpDataBuff, httpBuffSize) == HTTP_READ_INCOMPLETE)
                return HTTP_IO_NEED_DATA;

            if(!strcmp((char *)httpDataBuff, (const char *)"service"))
            {
                // Reading the service (numeric)
                TCPIP_HTTP_CurrentConnectionPostSmSet(connHandle, SM_DDNS_READ_SERVICE);
                break;
            }
            else if(!strcmp((char *)httpDataBuff, (const char *)"user"))
                DDNSClient.Username.szRAM = ptrDDNS;
            else if(!strcmp((char *)httpDataBuff, (const char *)"pass"))
                DDNSClient.Password.szRAM = ptrDDNS;
            else if(!strcmp((char *)httpDataBuff, (const char *)"host"))
                DDNSClient.Host.szRAM = ptrDDNS;

            // Move to reading the value for user/pass/host
            TCPIP_HTTP_CurrentConnectionPostSmSet(connHandle, ++smPost);

        // Reads in values and assigns them to the DDNS RAM
        case SM_DDNS_READ_VALUE:
            // Read a name
            if(TCPIP_HTTP_PostValueRead(connHandle, ptrDDNS, DDNS_SPACE_REMAINING) == HTTP_READ_INCOMPLETE)
                return HTTP_IO_NEED_DATA;

            // Move past the data that was just read
            ptrDDNS += strlen((char *)ptrDDNS);
            if(ptrDDNS < DDNSData + sizeof(DDNSData) - 1)
                ptrDDNS += 1;

            // Return to reading names
            TCPIP_HTTP_CurrentConnectionPostSmSet(connHandle, SM_DDNS_READ_NAME);
            break;

        // Reads in a service ID
        case SM_DDNS_READ_SERVICE:
            // Read the integer id
            if(TCPIP_HTTP_PostValueRead(connHandle, httpDataBuff, httpBuffSize) == HTTP_READ_INCOMPLETE)
                return HTTP_IO_NEED_DATA;

            // Convert to a service ID
            TCPIP_DDNS_ServiceSet((uint8_t)atol((char *)httpDataBuff));

            // Return to reading names
            TCPIP_HTTP_CurrentConnectionPostSmSet(connHandle, SM_DDNS_READ_NAME);
            break;

        // Sets up the DDNS client for an update
        case SM_DDNS_DONE:
            // Since user name and password changed, force an update immediately
            TCPIP_DDNS_UpdateForce();

            // Redirect to prevent POST errors
            lastSuccess = true;
            strcpy((char *)httpDataBuff, "/dyndns/index.htm");
            TCPIP_HTTP_CurrentConnectionStatusSet(connHandle, HTTP_REDIRECT);
            return HTTP_IO_DONE;
    }

    return HTTP_IO_WAITING; // Assume we're waiting to process more data
}
#endif // #if defined(TCPIP_STACK_USE_DYNAMICDNS_CLIENT)

/*******************************************************************************
  Function:
    static HTTP_IO_RESULT HTTPPostWIFIConfig(HTTP_CONN_HANDLE connHandle)

  Summary:
    Processes the Wi-Fi configuration data

  Description:
    Accepts wireless configuration data from the www site and saves them to a
    structure to be applied by the Wi-Fi module configuration manager.

    The following configurations are possible:
         i) Mode: adhoc or infrastructure
        ii) Security:
               - None
               - WEP 64-bit
               - WEP 128-bit
               - WPA Auto pre-calculated key
               - WPA1 passphrase
               - WPA2 passphrase
               - WPA Auto passphrase
       iii) Key material

    If an error occurs, such as data is invalid they will be redirected to a page
    informing the user of such results.

    NOTE: This code is modified originally from HTTPPostWIFIConfig as distributed
           by Microchip.

  Precondition:
    None

  Parameters:
    None

  Return Values:
    HTTP_IO_DONE - all parameters have been processed
    HTTP_IO_NEED_DATA - data needed by this function has not yet arrived
 *******************************************************************************/
#if defined(HTTP_APP_USE_WIFI)
static HTTP_IO_RESULT HTTPPostWIFIConfig(HTTP_CONN_HANDLE connHandle)
{
    uint8_t *httpDataBuff = 0;
    uint8_t ssidLen;
    uint16_t httpBuffSize;
    uint32_t byteCount;
    TCP_SOCKET sktHTTP;

    byteCount = TCPIP_HTTP_CurrentConnectionByteCountGet(connHandle);
    sktHTTP = TCPIP_HTTP_CurrentConnectionSocketGet(connHandle);
    if (byteCount >                                                         \
            TCPIP_TCP_GetIsReady(sktHTTP) + TCPIP_TCP_FifoRxFreeGet(sktHTTP))
        return HTTP_APP_ConfigFailure(connHandle, httpDataBuff);

    // Ensure that all data is waiting to be parsed.  If not, keep waiting for
    // all of it to arrive.
    if (TCPIP_TCP_GetIsReady(sktHTTP) < byteCount)
        return HTTP_IO_NEED_DATA;

    httpDataBuff = TCPIP_HTTP_CurrentConnectionDataBufferGet(connHandle);
    httpBuffSize = TCPIP_HTTP_CurrentConnectionDataBufferSizeGet(connHandle);
    // Read all browser POST data.
    while (TCPIP_HTTP_CurrentConnectionByteCountGet(connHandle))
    {
        // Read a form field name.
        if (HTTP_READ_OK !=                                                 \
                        TCPIP_HTTP_PostNameRead(connHandle, httpDataBuff, 6) )
            return HTTP_APP_ConfigFailure(connHandle, httpDataBuff);

        // Read a form field value.
        if (HTTP_READ_OK !=                                                 \
            TCPIP_HTTP_PostValueRead(                                       \
                        connHandle,                                         \
                        httpDataBuff + 6,                                   \
                        httpBuffSize - 6 - 2))
            return HTTP_APP_ConfigFailure(connHandle, httpDataBuff);

        // Parse the value that was read.
        if (!strcmp((const char *)httpDataBuff, (const char *)"wlan"))
        {
            // Get the network type: Ad hoc or Infrastructure.
            char networkType[6];
            if (strlen((const char *)(httpDataBuff + 6)) > 5) /* Sanity check. */
                return HTTP_APP_ConfigFailure(connHandle, httpDataBuff);

            memcpy(     networkType,                                        \
                        httpDataBuff + 6,                                   \
                        strlen((const char *)(httpDataBuff + 6)));
            /* Terminate string. */
            networkType[strlen((const char *)(httpDataBuff + 6))] = 0; 
            if (!strcmp(networkType, "infra"))
            {
                g_wifi_redirectionConfig.networkType =                      \
                        WF_NETWORK_TYPE_INFRASTRUCTURE;
            }
            else
            {
                // Network type no good. :-(
                SYS_CONSOLE_MESSAGE("\r\nInvalid redirection network type\r\n");
                return HTTP_APP_ConfigFailure(connHandle, httpDataBuff);
            }

            // Save old network type.
            iwpriv_get(NETWORKTYPE_GET, &s_httpapp_get_param);
            g_wifi_redirectionConfig.prevNetworkType =                      \
                        s_httpapp_get_param.netType.type;
        }
        else if (!strcmp((const char *)httpDataBuff, (const char *)"ssid"))
        {
            // Get new SSID and make sure it is valid.
            if (strlen((const char *)(httpDataBuff + 6)) < 33u)
            {
                memcpy( g_wifi_redirectionConfig.ssid,                      \
                        httpDataBuff + 6,                                   \
                        strlen((const char *)(httpDataBuff + 6)));
                g_wifi_redirectionConfig.ssid[strlen((const char *)(httpDataBuff + 6))] = 0; /* Terminate string. */

                /* Save current profile SSID for displaying later. */
                s_httpapp_get_param.ssid.ssid = g_wifi_redirectionConfig.prevSSID;
                iwpriv_get(SSID_GET, &s_httpapp_get_param);
                ssidLen = s_httpapp_get_param.ssid.ssidLen;
                g_wifi_redirectionConfig.prevSSID[ssidLen] = 0;
            }
            else
            {
                // Invalid SSID... :-(
                return HTTP_APP_ConfigFailure(connHandle, httpDataBuff);
            }
        }
        else if (!strcmp((const char *)httpDataBuff, (const char *)"sec"))
        {
            char securityMode[7]; // Read security mode.

            if (strlen((const char *)(httpDataBuff + 6)) > 6) /* Sanity check. */
                return HTTP_APP_ConfigFailure(connHandle, httpDataBuff);

            memcpy(     securityMode,                                       \
                        httpDataBuff + 6,                                   \
                        strlen((const char *)(httpDataBuff + 6)));
            // Terminate string. 
            securityMode[strlen((const char *)(httpDataBuff + 6))] = 0; 

            if (!strcmp(securityMode, "no"))
            {
                g_wifi_redirectionConfig.securityMode = WF_SECURITY_OPEN;
            }
            else if (!strcmp(securityMode, "wep40"))
            {
                g_wifi_redirectionConfig.securityMode = WF_SECURITY_WEP_40;
            }
            else if (!strcmp(securityMode, "wep104"))
            {
                g_wifi_redirectionConfig.securityMode = WF_SECURITY_WEP_104;
            }
            else if (!strcmp(securityMode, "wpa1"))
            {
                if (g_wifi_deviceInfo.deviceType == MRF24WN_MODULE) 
                {
                    g_wifi_redirectionConfig.securityMode =                 \
                        WF_SECURITY_WPA_WITH_PASS_PHRASE;
                } else if (g_wifi_deviceInfo.deviceType == WINC1500_MODULE) 
                {
                    g_wifi_redirectionConfig.securityMode =                 \
                        WF_SECURITY_WPA_AUTO_WITH_PASS_PHRASE;
                } else if (g_wifi_deviceInfo.deviceType == WILC1000_MODULE) 
                {
                    g_wifi_redirectionConfig.securityMode =                 \
                        WF_SECURITY_WPA_AUTO_WITH_PASS_PHRASE;
                } else {
                    WF_ASSERT(false, "Incorrect Wi-Fi Device Info");
                }
            }
            else if (!strcmp(securityMode, "wpa2"))
            {
                if (g_wifi_deviceInfo.deviceType == MRF24WN_MODULE) 
                {
                    g_wifi_redirectionConfig.securityMode =                 \
                        WF_SECURITY_WPA2_WITH_PASS_PHRASE;
                } else if (g_wifi_deviceInfo.deviceType == WINC1500_MODULE) 
                {
                    g_wifi_redirectionConfig.securityMode =                 \
                        WF_SECURITY_WPA_AUTO_WITH_PASS_PHRASE;
                } else if (g_wifi_deviceInfo.deviceType == WILC1000_MODULE) 
                {
                    g_wifi_redirectionConfig.securityMode =                 \
                        WF_SECURITY_WPA_AUTO_WITH_PASS_PHRASE;
                } else {
                    WF_ASSERT(false, "Incorrect Wi-Fi Device Info");
                }
            }
            else if (!strcmp(securityMode, "wpa"))
            {
                WF_ASSERT(g_wifi_deviceInfo.deviceType != MRF24WN_MODULE,   \
                        "Incorrect Wi-Fi Device Info");
                g_wifi_redirectionConfig.securityMode =                     \
                        WF_SECURITY_WPA_AUTO_WITH_PASS_PHRASE;
            }
            else
            {
                // Security mode no good. :-(
                SYS_CONSOLE_MESSAGE("\r\nInvalid redirection security mode\r\n\r\n");
                return HTTP_APP_ConfigFailure(connHandle, httpDataBuff);
            }
        }
        else if (!strcmp((const char *)httpDataBuff, (const char *)"key"))
        {
            // Read new key material.
            if (!Helper_WIFI_SecuritySelect(                                \
                        &g_wifi_redirectionConfig,                          \
                        (const char *)(httpDataBuff + 6)))
                return HTTP_APP_ConfigFailure(connHandle, httpDataBuff);
        }
    }

    /* Check if WPA hasn't been selected with Ad hoc, if it has we choke! */
    if ((g_wifi_redirectionConfig.networkType == WF_NETWORK_TYPE_ADHOC) && (
        (g_wifi_redirectionConfig.securityMode == WF_SECURITY_WPA_WITH_PASS_PHRASE) ||
        (g_wifi_redirectionConfig.securityMode == WF_SECURITY_WPA2_WITH_PASS_PHRASE) ||
        (g_wifi_redirectionConfig.securityMode == WF_SECURITY_WPA_AUTO_WITH_PASS_PHRASE)))
        return HTTP_APP_ConfigFailure(connHandle, httpDataBuff);

    /*
     * All parsing complete!  If we have got to here all data has been validated and
     * We can handle what is necessary to start the reconfigure process of the Wi-Fi device.
     */

    /* Copy Wi-Fi cfg data to be committed to NVM. */
    s_httpapp_get_param.cfg.config = &g_wifi_cfg;
    iwpriv_get(CONFIG_GET, &s_httpapp_get_param);
    strcpy((char *)g_wifi_cfg.ssid, (const char *)g_wifi_redirectionConfig.ssid);
    g_wifi_cfg.ssidLen = strlen((const char *)g_wifi_redirectionConfig.ssid);
    /* Going to set security type. */
    g_wifi_cfg.securityMode = g_wifi_redirectionConfig.securityMode;
    /* Going to save the key, if required. */
    if (g_wifi_redirectionConfig.securityMode != WF_SECURITY_OPEN)
        Helper_WIFI_KeySave(&g_wifi_redirectionConfig, &g_wifi_cfg);
    /* Going to save the network type. */
    g_wifi_cfg.networkType = g_wifi_redirectionConfig.networkType;
    s_httpapp_set_param.cfg.config = &g_wifi_cfg;
    iwpriv_set(CONFIG_SET, &s_httpapp_set_param);

    strcpy((char *)httpDataBuff, (const char *)"/reconnect.htm");
    TCPIP_HTTP_CurrentConnectionStatusSet(connHandle, HTTP_REDIRECT);

    /* Set 1s delay before redirection, goal is to display the redirection web page. */
    uint16_t redirection_delay = SYS_TMR_TickCounterFrequencyGet() * HTTP_APP_REDIRECTION_DELAY_TIME;
    SYS_TMR_CallbackSingle(redirection_delay, 0, HTTP_APP_RedirectionFlagSet);

    return HTTP_IO_DONE;
}
#endif // #if defined(HTTP_APP_USE_WIFI)

#endif // #if defined(TCPIP_HTTP_USE_POST)

/****************************************************************************
  Section:
    Authorization Handlers
 ****************************************************************************/

/****************************************************************************
  Function:
    uint8_t TCPIP_HTTP_FileAuthenticate(HTTP_CONN_HANDLE connHandle, uint8_t *cFile)

  Internal:
    See documentation in the TCP/IP Stack APIs or http.h for details.
 ****************************************************************************/
#if defined(TCPIP_HTTP_USE_AUTHENTICATION)
uint8_t TCPIP_HTTP_FileAuthenticate(HTTP_CONN_HANDLE connHandle, uint8_t *cFile)
{
    // If the filename begins with the folder "protect", then require auth.
    if(memcmp(cFile, "protect", 7) == 0)
        return 0x00;        // Authentication will be needed later.

    // If the filename begins with the folder "snmp", then require auth.
    if(memcmp(cFile, "snmp", 4) == 0)
        return 0x00;        // Authentication will be needed later.

    #if defined(HTTP_MPFS_UPLOAD_REQUIRES_AUTH)
    if(memcmp(cFile, "mpfsupload", 10) == 0)
        return 0x00;
    #endif
    // You can match additional strings here to password protect other files.
    // You could switch this and exclude files from authentication.
    // You could also always return 0x00 to require auth for all files.
    // You can return different values (0x00 to 0x79) to track "realms" for below.

    return 0x80; // No authentication required.
}
#endif

/****************************************************************************
  Function:
    uint8_t TCPIP_HTTP_UserAuthenticate(uint8_t *cUser, uint8_t *cPass)

  Internal:
    See documentation in the TCP/IP Stack APIs or http.h for details.
 ****************************************************************************/
#if defined(TCPIP_HTTP_USE_AUTHENTICATION)
uint8_t TCPIP_HTTP_UserAuthenticate(                                        \
                        HTTP_CONN_HANDLE connHandle,                        \
                        uint8_t *cUser,                                     \
                        uint8_t *cPass)
{
    if(strcmp((char *)cUser,(const char *)"admin") == 0
        && strcmp((char *)cPass, (const char *)"microchip") == 0)
        return 0x80;        // We accept this combination

    // You can add additional user/pass combos here.
    // If you return specific "realm" values above, you can base this
    //   decision on what specific file or folder is being accessed.
    // You could return different values (0x80 to 0xff) to indicate
    //   various users or groups, and base future processing decisions
    //   in TCPIP_HTTP_GetExecute/Post or HTTPPrint callbacks on this value.

    return 0x00;            // Provided user/pass is invalid
}
#endif

/****************************************************************************
  Section:
    Dynamic Variable Callback Functions
 ****************************************************************************/

/****************************************************************************
  Function:
    void TCPIP_HTTP_Print_varname(void)

  Internal:
    See documentation in the TCP/IP Stack APIs or http.h for details.
 ****************************************************************************/
void TCPIP_HTTP_Print_hellomsg(HTTP_CONN_HANDLE connHandle)
{
    const uint8_t *ptr;
    TCP_SOCKET sktHTTP = TCPIP_HTTP_CurrentConnectionSocketGet(connHandle);

    ptr = TCPIP_HTTP_ArgGet(                                                \
                    TCPIP_HTTP_CurrentConnectionDataBufferGet(connHandle),  \
                    (const uint8_t *)"name");
    // We omit checking for space because this is the only data being written
    if(ptr != NULL)
    {
        TCPIP_TCP_StringPut(sktHTTP, (const uint8_t *)"Hello, ");
        TCPIP_TCP_StringPut(sktHTTP, ptr);
    }
}

void TCPIP_HTTP_Print_builddate(HTTP_CONN_HANDLE connHandle)
{
    TCP_SOCKET sktHTTP;
    sktHTTP = TCPIP_HTTP_CurrentConnectionSocketGet(connHandle);

    TCPIP_HTTP_CurrentConnectionCallbackPosSet(connHandle, 0x01);
    if(TCPIP_TCP_PutIsReady(sktHTTP) < strlen((const char *)__DATE__" "__TIME__))
        return;

    TCPIP_HTTP_CurrentConnectionCallbackPosSet(connHandle, 0x00);
    TCPIP_TCP_StringPut(sktHTTP, (const void *)__DATE__" "__TIME__);
}

void TCPIP_HTTP_Print_version(HTTP_CONN_HANDLE connHandle)
{
    TCPIP_TCP_StringPut(                                                    \
                        TCPIP_HTTP_CurrentConnectionSocketGet(connHandle),  \
                        (const void *)TCPIP_STACK_VERSION_STR);
}

void TCPIP_HTTP_Print_drive(HTTP_CONN_HANDLE connHandle)
{
    TCPIP_TCP_StringPut(                                                    \
                        TCPIP_HTTP_CurrentConnectionSocketGet(connHandle),  \
                        (const void *)SYS_FS_DRIVE);
}

void TCPIP_HTTP_Print_fstype(HTTP_CONN_HANDLE connHandle)
{
    TCPIP_TCP_StringPut(TCPIP_HTTP_CurrentConnectionSocketGet(connHandle),  \
                        (const void *)SYS_FS_MPFS_STRING);
}

void TCPIP_HTTP_Print_cookiename(HTTP_CONN_HANDLE connHandle)
{
    const uint8_t *ptr;
    TCP_SOCKET sktHTTP = TCPIP_HTTP_CurrentConnectionSocketGet(connHandle);

    ptr = TCPIP_HTTP_ArgGet(                                                \
                    TCPIP_HTTP_CurrentConnectionDataBufferGet(connHandle),  \
                    (const uint8_t *)"name");
    if(ptr)
        TCPIP_TCP_StringPut(sktHTTP, ptr);
    else
        TCPIP_TCP_StringPut(sktHTTP, (const uint8_t *)"not set");
}

void TCPIP_HTTP_Print_cookiefav(HTTP_CONN_HANDLE connHandle)
{
    const uint8_t *ptr;
    TCP_SOCKET sktHTTP = TCPIP_HTTP_CurrentConnectionSocketGet(connHandle);

    ptr = TCPIP_HTTP_ArgGet(                                                \
                    TCPIP_HTTP_CurrentConnectionDataBufferGet(connHandle),  \
                    (const uint8_t *)"fav");
    if(ptr)
        TCPIP_TCP_StringPut(sktHTTP, ptr);
    else
        TCPIP_TCP_StringPut(sktHTTP, (const uint8_t *)"not set");
}

void TCPIP_HTTP_Print_btn(HTTP_CONN_HANDLE connHandle, uint16_t num)
{
    const uint8_t HTML_UP_ARROW[] = "up";
    const uint8_t HTML_DOWN_ARROW[] = "dn";

    // Determine which button
    switch(num)
    {
        case 0:
            //num = APP_SWITCH_1StateGet();
            break;
        case 1:
            //num = APP_SWITCH_2StateGet();
            break;
        case 2:
            //num = APP_SWITCH_3StateGet();
            break;
        default:
            num = 0;
    }

    // Print the output
    TCPIP_TCP_StringPut(TCPIP_HTTP_CurrentConnectionSocketGet(connHandle),  \
                        (num ? HTML_UP_ARROW : HTML_DOWN_ARROW));
}

void TCPIP_HTTP_Print_led(HTTP_CONN_HANDLE connHandle, uint16_t num)
{
    // Determine which LED
    switch(num)
    {
        case 0:
            //num = BSP_LEDStateGet(APP_LED_1);
            break;
        case 1:
            //num = BSP_LEDStateGet(APP_LED_2);
            break;
        case 2:
            //num = BSP_LEDStateGet(APP_LED_3);
            break;
        default:
            num = 0;
    }

    // Print the output
    TCPIP_TCP_Put(      TCPIP_HTTP_CurrentConnectionSocketGet(connHandle),  \
                        (num ? '1' : '0'));
}

void TCPIP_HTTP_Print_ledSelected(                                          \
                        HTTP_CONN_HANDLE connHandle,                        \
                        uint16_t num,                                       \
                        uint16_t state)
{
    // Determine which LED to check
    switch(num)
    {
        case 0:
            //num = BSP_LEDStateGet(APP_LED_1);
            break;
        case 1:
            //num = BSP_LEDStateGet(APP_LED_2);
            break;
        case 2:
            //num = BSP_LEDStateGet(APP_LED_3);
            break;
        default:
            num = 0;
    }

    // Print output if true and ON or if false and OFF
    if((state && num) || (!state && !num))
        TCPIP_TCP_StringPut(                                                \
                        TCPIP_HTTP_CurrentConnectionSocketGet(connHandle),  \
                        (const uint8_t *)"SELECTED");
}

void TCPIP_HTTP_Print_pot(HTTP_CONN_HANDLE connHandle)
{
    uint8_t AN0String[8];
    uint16_t ADval;

    ADval = (uint16_t)SYS_RANDOM_PseudoGet();

    uitoa(ADval, (uint8_t *)AN0String);

    TCPIP_TCP_StringPut(                                                    \
                        TCPIP_HTTP_CurrentConnectionSocketGet(connHandle),  \
                        AN0String);
}

void TCPIP_HTTP_Print_status_ok(HTTP_CONN_HANDLE connHandle)
{
    TCP_SOCKET sktHTTP = TCPIP_HTTP_CurrentConnectionSocketGet(connHandle);

    if(lastSuccess)
        TCPIP_TCP_StringPut(sktHTTP, (const uint8_t *)"block");
    else
        TCPIP_TCP_StringPut(sktHTTP, (const uint8_t *)"none");
    lastSuccess = false;
}

void TCPIP_HTTP_Print_status_fail(HTTP_CONN_HANDLE connHandle)
{
    TCP_SOCKET sktHTTP = TCPIP_HTTP_CurrentConnectionSocketGet(connHandle);

    if(lastFailure)
        TCPIP_TCP_StringPut(sktHTTP, (const uint8_t *)"block");
    else
        TCPIP_TCP_StringPut(sktHTTP, (const uint8_t *)"none");
    lastFailure = false;
}

void TCPIP_HTTP_Print_uploadedmd5(HTTP_CONN_HANDLE connHandle)
{
    uint8_t i;
    uint8_t *httpDataBuff;
    TCP_SOCKET sktHTTP = TCPIP_HTTP_CurrentConnectionSocketGet(connHandle);

    // Set a flag to indicate not finished
    TCPIP_HTTP_CurrentConnectionCallbackPosSet(connHandle, 1);

    // Make sure there's enough output space
    if(TCPIP_TCP_PutIsReady(sktHTTP) < 32u + 37u + 5u)
        return;

    // Check for flag set in HTTPPostMD5
#if defined(HTTP_APP_USE_MD5)
    if(TCPIP_HTTP_CurrentConnectionPostSmGet(connHandle) != SM_MD5_POST_COMPLETE)
#endif
    {// No file uploaded, so just return
        TCPIP_TCP_StringPut(sktHTTP, (const uint8_t *)"<b>Upload a File</b>");
        TCPIP_HTTP_CurrentConnectionCallbackPosSet(connHandle, 0);
        return;
    }

    TCPIP_TCP_StringPut(sktHTTP,                                            \
                    (const uint8_t *)"<b>Uploaded File's MD5 was:</b><br />");
    httpDataBuff = TCPIP_HTTP_CurrentConnectionDataBufferGet(connHandle);

    // Write a byte of the md5 sum at a time
    for(i = 0; i < 16u; ++i)
    {
        TCPIP_TCP_Put(sktHTTP, btohexa_high(httpDataBuff[i]));
        TCPIP_TCP_Put(sktHTTP, btohexa_low(httpDataBuff[i]));
        if((i & 0x03) == 3u)
            TCPIP_TCP_Put(sktHTTP, ' ');
    }

    TCPIP_HTTP_CurrentConnectionCallbackPosSet(connHandle, 0x00);
}

void TCPIP_HTTP_Print_config_hostname(HTTP_CONN_HANDLE connHandle)
{
    TCP_SOCKET sktHTTP = TCPIP_HTTP_CurrentConnectionSocketGet(connHandle);

    TCPIP_TCP_StringPut(sktHTTP,                                            \
            (uint8_t *)TCPIP_STACK_NetBIOSName(TCPIP_TCP_SocketNetGet(sktHTTP)));
}

void TCPIP_HTTP_Print_config_dhcpchecked(HTTP_CONN_HANDLE connHandle)
{
    TCP_SOCKET sktHTTP = TCPIP_HTTP_CurrentConnectionSocketGet(connHandle);

    if(TCPIP_DHCP_IsEnabled(TCPIP_TCP_SocketNetGet(sktHTTP)))
    {
        TCPIP_TCP_StringPut(sktHTTP, (const uint8_t *)"checked");
    }
}

void TCPIP_HTTP_Print_config_ip(HTTP_CONN_HANDLE connHandle)
{
    IPV4_ADDR ipAddress;
    TCP_SOCKET sktHTTP = TCPIP_HTTP_CurrentConnectionSocketGet(connHandle);
    TCPIP_NET_HANDLE netH = TCPIP_TCP_SocketNetGet(sktHTTP);

    ipAddress.Val = TCPIP_STACK_NetAddress(netH);
    if (TCPIP_Helper_IPAddressToString(                                     \
                &ipAddress,                                                 \
                (char *)s_buf_ipv4addr, HTTP_APP_IPV4_ADDRESS_BUFFER_SIZE))
    {
        TCPIP_TCP_StringPut(sktHTTP, s_buf_ipv4addr);
    }
}

void TCPIP_HTTP_Print_config_gw(HTTP_CONN_HANDLE connHandle) // gateway
{
    IPV4_ADDR gwAddress;
    TCP_SOCKET sktHTTP = TCPIP_HTTP_CurrentConnectionSocketGet(connHandle);
    TCPIP_NET_HANDLE netH = TCPIP_TCP_SocketNetGet(sktHTTP);

    gwAddress.Val = TCPIP_STACK_NetAddressGateway(netH);
    if (TCPIP_Helper_IPAddressToString(                                     \
                        &gwAddress,                                         \
                        (char *)s_buf_ipv4addr,                             \
                        HTTP_APP_IPV4_ADDRESS_BUFFER_SIZE))
    {
        TCPIP_TCP_StringPut(sktHTTP, s_buf_ipv4addr);
    }
}

void TCPIP_HTTP_Print_config_subnet(HTTP_CONN_HANDLE connHandle)
{
    IPV4_ADDR ipMask;
    TCP_SOCKET sktHTTP = TCPIP_HTTP_CurrentConnectionSocketGet(connHandle);
    TCPIP_NET_HANDLE netH = TCPIP_TCP_SocketNetGet(sktHTTP);

    ipMask.Val = TCPIP_STACK_NetMask(netH);
    if (TCPIP_Helper_IPAddressToString(                                     \
                        &ipMask,                                            \
                        (char *)s_buf_ipv4addr,                             \
                        HTTP_APP_IPV4_ADDRESS_BUFFER_SIZE))
    {
        TCPIP_TCP_StringPut(sktHTTP, s_buf_ipv4addr);
    }
}

void TCPIP_HTTP_Print_config_dns1(HTTP_CONN_HANDLE connHandle)
{
    IPV4_ADDR priDnsAddr;
    TCP_SOCKET sktHTTP = TCPIP_HTTP_CurrentConnectionSocketGet(connHandle);
    TCPIP_NET_HANDLE netH = TCPIP_TCP_SocketNetGet(sktHTTP);

    priDnsAddr.Val = TCPIP_STACK_NetAddressDnsPrimary(netH);
    if (TCPIP_Helper_IPAddressToString(                                     \
                        &priDnsAddr,                                        \
                        (char *)s_buf_ipv4addr,                             \
                        HTTP_APP_IPV4_ADDRESS_BUFFER_SIZE))
    {
        TCPIP_TCP_StringPut(sktHTTP, s_buf_ipv4addr);
    }
}

void TCPIP_HTTP_Print_config_dns2(HTTP_CONN_HANDLE connHandle)
{
    IPV4_ADDR secondDnsAddr;
    TCP_SOCKET sktHTTP = TCPIP_HTTP_CurrentConnectionSocketGet(connHandle);
    TCPIP_NET_HANDLE netH = TCPIP_TCP_SocketNetGet(sktHTTP);

    secondDnsAddr.Val = TCPIP_STACK_NetAddressDnsSecond(netH);
    if (TCPIP_Helper_IPAddressToString(                                     \
                        &secondDnsAddr,                                     \
                        (char *)s_buf_ipv4addr,                             \
                        HTTP_APP_IPV4_ADDRESS_BUFFER_SIZE))
    {
        TCPIP_TCP_StringPut(sktHTTP, s_buf_ipv4addr);
    }
}

void TCPIP_HTTP_Print_config_mac(HTTP_CONN_HANDLE connHandle)
{
    uint8_t i;
    TCP_SOCKET sktHTTP;
    TCPIP_NET_HANDLE hNet;
    const uint8_t *pMacAdd;

    sktHTTP = TCPIP_HTTP_CurrentConnectionSocketGet(connHandle);

    if(TCPIP_TCP_PutIsReady(sktHTTP) < 18u)
    {   // need 17 bytes to write a MAC
        TCPIP_HTTP_CurrentConnectionCallbackPosSet(connHandle, 0x01);
        return;
    }

    hNet = TCPIP_TCP_SocketNetGet(sktHTTP);
    pMacAdd = TCPIP_STACK_NetAddressMac(hNet);
    // Write each byte
    for(i = 0; i < 6u; ++i)
    {
        if(i)
            TCPIP_TCP_Put(sktHTTP, ':');
        TCPIP_TCP_Put(sktHTTP, btohexa_high(pMacAdd[i]));
        TCPIP_TCP_Put(sktHTTP, btohexa_low(pMacAdd[i]));
    }

    // Indicate that we're done
    TCPIP_HTTP_CurrentConnectionCallbackPosSet(connHandle, 0x00);
    return;
}

void TCPIP_HTTP_Print_ddns_user(HTTP_CONN_HANDLE connHandle)
{
    #if defined(TCPIP_STACK_USE_DYNAMICDNS_CLIENT)
    uint32_t callbackPos;

    if(DDNSClient.ROMPointers.Username || !DDNSClient.Username.szRAM)
        return;

    callbackPos = TCPIP_HTTP_CurrentConnectionCallbackPosGet(connHandle);
    if(callbackPos == 0x00u)
        callbackPos = (uint32_t)DDNSClient.Username.szRAM;
    callbackPos = (uint32_t)TCPIP_TCP_StringPut(                            \
                        TCPIP_HTTP_CurrentConnectionSocketGet(connHandle),  \
                        (uint8_t *)callbackPos);
    if(*(uint8_t *)callbackPos == '\0')
        callbackPos = 0x00;
    TCPIP_HTTP_CurrentConnectionCallbackPosSet(connHandle, callbackPos);
    #endif
}

void TCPIP_HTTP_Print_ddns_pass(HTTP_CONN_HANDLE connHandle)
{
    #if defined(TCPIP_STACK_USE_DYNAMICDNS_CLIENT)
    uint32_t callbackPos;

    if(DDNSClient.ROMPointers.Password || !DDNSClient.Password.szRAM)
        return;

    callbackPos = TCPIP_HTTP_CurrentConnectionCallbackPosGet(connHandle);

    if(callbackPos == 0x00u)
        callbackPos = (uint32_t)DDNSClient.Password.szRAM;
    callbackPos = (uint32_t)TCPIP_TCP_StringPut(                            \
                        TCPIP_HTTP_CurrentConnectionSocketGet(connHandle),  \
                        (uint8_t *)callbackPos);
    if(*(uint8_t *)callbackPos == '\0')
        callbackPos = 0x00;
    TCPIP_HTTP_CurrentConnectionCallbackPosSet(connHandle, callbackPos);
    #endif
}

void TCPIP_HTTP_Print_ddns_host(HTTP_CONN_HANDLE connHandle)
{
    #if defined(TCPIP_STACK_USE_DYNAMICDNS_CLIENT)
    uint32_t callbackPos;

    if(DDNSClient.ROMPointers.Host || !DDNSClient.Host.szRAM)
        return;
    callbackPos = TCPIP_HTTP_CurrentConnectionCallbackPosGet(connHandle);
    if(callbackPos == 0x00u)
        callbackPos = (uint32_t)DDNSClient.Host.szRAM;
    callbackPos = (uint32_t)TCPIP_TCP_StringPut(                            \
                        TCPIP_HTTP_CurrentConnectionSocketGet(connHandle),  \
                        (uint8_t *)callbackPos);
    if(*(uint8_t *)callbackPos == '\0')
        callbackPos = 0x00;
    TCPIP_HTTP_CurrentConnectionCallbackPosSet(connHandle, callbackPos);
    #endif
}

void TCPIP_HTTP_Print_ddns_service(HTTP_CONN_HANDLE connHandle, uint16_t i)
{
    #if defined(TCPIP_STACK_USE_DYNAMICDNS_CLIENT)
    if(!DDNSClient.ROMPointers.UpdateServer || !DDNSClient.UpdateServer.szROM)
        return;
    if((const char *)DDNSClient.UpdateServer.szROM == ddnsServiceHosts[i])
        TCPIP_TCP_StringPut(                                                \
                        TCPIP_HTTP_CurrentConnectionSocketGet(connHandle),  \
                        (const uint8_t *)"selected");
    #endif
}

void TCPIP_HTTP_Print_ddns_status(HTTP_CONN_HANDLE connHandle)
{
    TCP_SOCKET sktHTTP = TCPIP_HTTP_CurrentConnectionSocketGet(connHandle);

    #if defined(TCPIP_STACK_USE_DYNAMICDNS_CLIENT)
    DDNS_STATUS s;
    s = TCPIP_DDNS_LastStatusGet();
    if(s == DDNS_STATUS_GOOD || s == DDNS_STATUS_UNCHANGED || s == DDNS_STATUS_NOCHG)
        TCPIP_TCP_StringPut(sktHTTP, (const uint8_t *)"ok");
    else if(s == DDNS_STATUS_UNKNOWN)
        TCPIP_TCP_StringPut(sktHTTP, (const uint8_t *)"unk");
    else
        TCPIP_TCP_StringPut(sktHTTP, (const uint8_t *)"fail");
    #else
    TCPIP_TCP_StringPut(sktHTTP, (const uint8_t *)"fail");
    #endif
}

void TCPIP_HTTP_Print_ddns_status_msg(HTTP_CONN_HANDLE connHandle)
{
    TCP_SOCKET sktHTTP = TCPIP_HTTP_CurrentConnectionSocketGet(connHandle);

    if(TCPIP_TCP_PutIsReady(sktHTTP) < 75u)
    {
        TCPIP_HTTP_CurrentConnectionCallbackPosSet(connHandle, 0x01);
        return;
    }

    #if defined(TCPIP_STACK_USE_DYNAMICDNS_CLIENT)
    switch(TCPIP_DDNS_LastStatusGet())
    {
        case DDNS_STATUS_GOOD:
        case DDNS_STATUS_NOCHG:
            TCPIP_TCP_StringPut(                                            \
                        sktHTTP,                                            \
                        (const uint8_t *)"The last update was successful.");
            break;
        case DDNS_STATUS_UNCHANGED:
            TCPIP_TCP_StringPut(                                            \
            sktHTTP,                                                        \
            (const uint8_t *)"The IP has not changed since the last update.");
            break;
        case DDNS_STATUS_UPDATE_ERROR:
        case DDNS_STATUS_CHECKIP_ERROR:
            TCPIP_TCP_StringPut(                                            \
                    sktHTTP,                                                \
                    (const uint8_t *)"Could not communicate with DDNS server.");
            break;
        case DDNS_STATUS_INVALID:
            TCPIP_TCP_StringPut(                                            \
                    sktHTTP,                                                \
                    (const uint8_t *)"The current configuration is not valid.");
            break;
        case DDNS_STATUS_UNKNOWN:
            TCPIP_TCP_StringPut(                                            \
                    sktHTTP,                                                \
                    (const uint8_t *)"The Dynamic DNS client is pending an update.");
            break;
        default:
            TCPIP_TCP_StringPut(                                            \
                    sktHTTP,                                                \
                    (const uint8_t *)"An error occurred during the update.<br />The DDNS Client is suspended.");
            break;
    }
    #else
    TCPIP_TCP_StringPut(                                                    \
                    sktHTTP,                                                \
                    (const uint8_t *)"The Dynamic DNS Client is not enabled.");
    #endif

    TCPIP_HTTP_CurrentConnectionCallbackPosSet(connHandle, 0);
}

void TCPIP_HTTP_Print_reboot(HTTP_CONN_HANDLE connHandle)
{
    // This is not so much a print function, but causes the interface to restart
    // when the configuration is changed.  If called via an AJAX call, this
    // will gracefully restart the interface and bring it back online immediately
    if(httpNetData.currNet != 0)
    {   // valid data
        httpNetData.netConfig.interface = httpNetData.ifName;
        httpNetData.netConfig.hostName = httpNetData.nbnsName;
        httpNetData.netConfig.macAddr = httpNetData.ifMacAddr;
        httpNetData.netConfig.ipAddr = httpNetData.ipAddr;
        httpNetData.netConfig.ipMask = httpNetData.ipMask;
        httpNetData.netConfig.gateway = httpNetData.gwIP;
        httpNetData.netConfig.priDNS = httpNetData.dns1IP;
        httpNetData.netConfig.secondDNS = httpNetData.dns2IP;
        httpNetData.netConfig.powerMode = TCPIP_STACK_IF_POWER_FULL;
        // httpNetData.netConfig.startFlags should be already set;
        httpNetData.netConfig.pMacObject =                                  \
                        TCPIP_STACK_MACObjectGet(httpNetData.currNet);

        TCPIP_STACK_NetDown(httpNetData.currNet);
        TCPIP_STACK_NetUp(httpNetData.currNet, &httpNetData.netConfig);
    }
}

void TCPIP_HTTP_Print_rebootaddr(HTTP_CONN_HANDLE connHandle)
{   // This is the expected address of the board upon rebooting
    TCPIP_TCP_StringPut(                                                    \
                        TCPIP_HTTP_CurrentConnectionSocketGet(connHandle),  \
                        TCPIP_HTTP_CurrentConnectionDataBufferGet(connHandle));
}

void TCPIP_HTTP_Print_smtps_en(HTTP_CONN_HANDLE connHandle)
{
    TCP_SOCKET sktHTTP = TCPIP_HTTP_CurrentConnectionSocketGet(connHandle);

    TCPIP_TCP_StringPut(sktHTTP, (const uint8_t *)"none");
}

void TCPIP_HTTP_Print_snmp_en(HTTP_CONN_HANDLE connHandle)
{
    TCP_SOCKET sktHTTP = TCPIP_HTTP_CurrentConnectionSocketGet(connHandle);

    #if defined(TCPIP_STACK_USE_SNMP_SERVER)
        TCPIP_TCP_StringPut(sktHTTP, (const uint8_t *)"none");
    #else
        TCPIP_TCP_StringPut(sktHTTP, (const uint8_t *)"block");
    #endif
}

// SNMP Read communities configuration page
void TCPIP_HTTP_Print_read_comm(HTTP_CONN_HANDLE connHandle, uint16_t num)
{
#if defined(TCPIP_STACK_USE_SNMP_SERVER)
    uint8_t dest[TCPIP_SNMP_COMMUNITY_MAX_LEN + 1];
    TCP_SOCKET sktHTTP = TCPIP_HTTP_CurrentConnectionSocketGet(connHandle);

    // Ensure no one tries to read illegal memory addresses by specifying
    // illegal num values.
    if(num >= TCPIP_SNMP_MAX_COMMUNITY_SUPPORT)
        return;
    memset(dest,0,sizeof(dest));
    if(TCPIP_SNMP_ReadCommunityGet(num,TCPIP_SNMP_COMMUNITY_MAX_LEN,dest) != true)
        return;
    // Send proper string
    TCPIP_TCP_StringPut(sktHTTP,dest);
#endif
}

// SNMP Write communities configuration page
void TCPIP_HTTP_Print_write_comm(HTTP_CONN_HANDLE connHandle, uint16_t num)
{
#if defined(TCPIP_STACK_USE_SNMP_SERVER)
    uint8_t dest[TCPIP_SNMP_COMMUNITY_MAX_LEN + 1];
    TCP_SOCKET sktHTTP = TCPIP_HTTP_CurrentConnectionSocketGet(connHandle);

    // Ensure no one tries to read illegal memory addresses by specifying
    // illegal num values.
    if(num >= TCPIP_SNMP_MAX_COMMUNITY_SUPPORT)
        return;
    memset(dest,0,sizeof(dest));
    // Send proper string
    if(TCPIP_SNMP_WriteCommunityGet(num,TCPIP_SNMP_COMMUNITY_MAX_LEN,dest) != true)
        return;
    TCPIP_TCP_StringPut(sktHTTP,dest);
#endif
}

void TCPIP_HTTP_Print_fwver(HTTP_CONN_HANDLE connHandle)
{
    static bool firstTime = true;
    static uint8_t fwverString[8];
    TCP_SOCKET sktHTTP = TCPIP_HTTP_CurrentConnectionSocketGet(connHandle);

    if (firstTime)
    {
        WF_DEVICE_INFO deviceInfo;

        firstTime = false;
        s_httpapp_get_param.devInfo.info = &deviceInfo;
        iwpriv_get(DEVICEINFO_GET, &s_httpapp_get_param);
        sprintf(        (char *)fwverString,                                \
                        "%02x%02x",                                         \
                        deviceInfo.romVersion,                              \
                        deviceInfo.patchVersion);
    }
    TCPIP_TCP_StringPut(sktHTTP, fwverString);
}

void TCPIP_HTTP_Print_ssid(HTTP_CONN_HANDLE connHandle)
{
    static uint8_t ssidString[33];
    TCP_SOCKET sktHTTP = TCPIP_HTTP_CurrentConnectionSocketGet(connHandle);

    // We don't need to check the connection state as the only way this function
    // is called is from the web server.  If the web server is requesting this,
    // then you can infer that we should be connected to the network.
    s_httpapp_get_param.ssid.ssid = ssidString;
    iwpriv_get(SSID_GET, &s_httpapp_get_param);
    TCPIP_TCP_StringPut(sktHTTP, ssidString);
}

void TCPIP_HTTP_Print_scan(HTTP_CONN_HANDLE connHandle)
{
    uint8_t scanInProgressString[4];
    TCP_SOCKET sktHTTP = TCPIP_HTTP_CurrentConnectionSocketGet(connHandle);

    iwpriv_get(SCANSTATUS_GET, &s_httpapp_get_param);
    if (s_httpapp_get_param.scan.scanStatus == IWPRIV_SCAN_IN_PROGRESS)
        uitoa((uint16_t)true, scanInProgressString);
    else
        uitoa((uint16_t)false, scanInProgressString);
    TCPIP_TCP_StringPut(sktHTTP, scanInProgressString);
}

void TCPIP_HTTP_Print_bssCount(HTTP_CONN_HANDLE connHandle)
{
    uint8_t bssCountString[4];
    TCP_SOCKET sktHTTP = TCPIP_HTTP_CurrentConnectionSocketGet(connHandle);

    if (Helper_WIFI_IsScanNeeded()) {
        iwpriv_get(SCANRESULTS_COUNT_GET, &s_httpapp_get_param);
        uitoa(s_httpapp_get_param.scan.numberOfResults, bssCountString);
    } else {
        uitoa(g_wifi_scanContext.numberOfResults, bssCountString);
    }
    TCPIP_TCP_StringPut(sktHTTP, bssCountString);
}

void TCPIP_HTTP_Print_valid(HTTP_CONN_HANDLE connHandle)
{
    uint8_t s_scanResultIsValidString[4];
    TCP_SOCKET sktHTTP = TCPIP_HTTP_CurrentConnectionSocketGet(connHandle);

    uitoa((uint8_t)s_scanResultIsValid, s_scanResultIsValidString);
    TCPIP_TCP_StringPut(sktHTTP, s_scanResultIsValidString);
}

void TCPIP_HTTP_Print_name(HTTP_CONN_HANDLE connHandle)
{
    TCP_SOCKET sktHTTP = TCPIP_HTTP_CurrentConnectionSocketGet(connHandle);

    if (s_scanResultIsValid)
    {
        TCPIP_TCP_StringPut(sktHTTP, sp_scanResult->ssid);
    }
    else
    {
        TCPIP_TCP_StringPut(sktHTTP, (const uint8_t *)"0");
    }
}

void TCPIP_HTTP_Print_wlan(HTTP_CONN_HANDLE connHandle)
{
    uint8_t bssTypeString[4];
    TCP_SOCKET sktHTTP = TCPIP_HTTP_CurrentConnectionSocketGet(connHandle);

    if (s_scanResultIsValid)
    {
        uitoa(sp_scanResult->bssType, bssTypeString);
        TCPIP_TCP_StringPut(sktHTTP, bssTypeString);
    }
    else
    {
        TCPIP_TCP_StringPut(sktHTTP, (const uint8_t *)"0");
    }
}

void TCPIP_HTTP_Print_privacy(HTTP_CONN_HANDLE connHandle)
{
    TCP_SOCKET sktHTTP = TCPIP_HTTP_CurrentConnectionSocketGet(connHandle);

    if (s_scanResultIsValid)
    {
        uint8_t secString[4];
        uint8_t security = (sp_scanResult->apConfig & 0xd0) >> 4;
        uitoa(security, secString);
        TCPIP_TCP_StringPut(sktHTTP, secString);
    }
    else
    {
        TCPIP_TCP_StringPut(sktHTTP, (const uint8_t *)"0");
    }
}

void TCPIP_HTTP_Print_strength(HTTP_CONN_HANDLE connHandle)
{
    uint8_t rssi;
    uint8_t strVal;
    uint8_t strString[4];
    TCP_SOCKET sktHTTP = TCPIP_HTTP_CurrentConnectionSocketGet(connHandle);

    if (s_scanResultIsValid)
    {
        if (g_wifi_deviceInfo.deviceType == MRF24WN_MODULE)
            rssi = sp_scanResult->rssi;
        else
            rssi = sp_scanResult->rssi + 125;

        if (rssi < 61)
            strVal = 1;
        else if (rssi < 81)
            strVal = 2;
        else if (rssi < 101)
            strVal = 3;
        else
            strVal = 4;

        uitoa(strVal, strString);
        TCPIP_TCP_StringPut(sktHTTP, strString);
    }
    else
    {
        TCPIP_TCP_StringPut(sktHTTP, (const uint8_t *)"0");
    }
}

void TCPIP_HTTP_Print_prevSSID(HTTP_CONN_HANDLE connHandle)
{
    TCP_SOCKET sktHTTP = TCPIP_HTTP_CurrentConnectionSocketGet(connHandle);

    TCPIP_TCP_StringPut(sktHTTP, (uint8_t *)g_wifi_redirectionConfig.prevSSID);
}

void TCPIP_HTTP_Print_nextSSID(HTTP_CONN_HANDLE connHandle)
{
    TCP_SOCKET sktHTTP = TCPIP_HTTP_CurrentConnectionSocketGet(connHandle);

    TCPIP_TCP_StringPut(sktHTTP, (uint8_t *)g_wifi_redirectionConfig.ssid);
}

void TCPIP_HTTP_Print_prevWLAN(HTTP_CONN_HANDLE connHandle)
{
    TCP_SOCKET sktHTTP = TCPIP_HTTP_CurrentConnectionSocketGet(connHandle);

    if (g_wifi_redirectionConfig.prevNetworkType ==                         \
                        WF_NETWORK_TYPE_INFRASTRUCTURE)
    {
        TCPIP_TCP_StringPut(sktHTTP, (const uint8_t *)"Infrastructure (BSS)");
    }
    else if (g_wifi_redirectionConfig.prevNetworkType ==                    \
                        WF_NETWORK_TYPE_ADHOC)
    {
        TCPIP_TCP_StringPut(sktHTTP, (const uint8_t *)"Ad hoc (IBSS)");
    }
    else if (g_wifi_redirectionConfig.prevNetworkType ==                    \
                        WF_NETWORK_TYPE_SOFT_AP)
    {
        TCPIP_TCP_StringPut(sktHTTP, (const uint8_t *)"SoftAP (BSS)");
    }
    else
        TCPIP_TCP_StringPut(sktHTTP, (const uint8_t *)"Unknown");
}

void TCPIP_HTTP_Print_currWLAN(HTTP_CONN_HANDLE connHandle)
{
    TCP_SOCKET sktHTTP = TCPIP_HTTP_CurrentConnectionSocketGet(connHandle);

    s_httpapp_get_param.cfg.config = &g_wifi_cfg;
    iwpriv_get(CONFIG_GET, &s_httpapp_get_param);
    if (g_wifi_cfg.networkType == WF_NETWORK_TYPE_INFRASTRUCTURE)
        TCPIP_TCP_StringPut(sktHTTP, (const uint8_t *)"Infrastructure (BSS)");
    else if (g_wifi_cfg.networkType == WF_NETWORK_TYPE_ADHOC)
        TCPIP_TCP_StringPut(sktHTTP, (const uint8_t *)"Ad hoc (IBSS)");
    else if (g_wifi_cfg.networkType == WF_NETWORK_TYPE_SOFT_AP)
        TCPIP_TCP_StringPut(sktHTTP, (const uint8_t *)"SoftAP (BSS)");
    else if (g_wifi_cfg.networkType == WF_NETWORK_TYPE_P2P)
        TCPIP_TCP_StringPut(sktHTTP, (const uint8_t *)"Wi-Fi Direct (P2P)");
    else
        TCPIP_TCP_StringPut(sktHTTP, (const uint8_t *)"Unknown");
}

void TCPIP_HTTP_Print_nextWLAN(HTTP_CONN_HANDLE connHandle)
{
    TCP_SOCKET sktHTTP = TCPIP_HTTP_CurrentConnectionSocketGet(connHandle);

    if (g_wifi_redirectionConfig.networkType == WF_NETWORK_TYPE_INFRASTRUCTURE)
        TCPIP_TCP_StringPut(sktHTTP, (const uint8_t *)"Infrastructure (BSS)");
    else if (g_wifi_redirectionConfig.networkType == WF_NETWORK_TYPE_ADHOC)
        TCPIP_TCP_StringPut(sktHTTP, (const uint8_t *)"Ad hoc (IBSS)");
    else if (g_wifi_redirectionConfig.networkType == WF_NETWORK_TYPE_SOFT_AP)
        TCPIP_TCP_StringPut(sktHTTP, (const uint8_t *)"SoftAP (BSS)");
    else
        TCPIP_TCP_StringPut(sktHTTP, (const uint8_t *)"Unknown");
}

#endif // #if defined(TCPIP_STACK_USE_HTTP_SERVER)

