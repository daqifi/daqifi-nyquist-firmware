/*! @file JSON_Encoder.c
 *
 * This file implements the functions to manage the JSON encoder
 */

#include "../services/DaqifiPB/DaqifiOutMessage.pb.h"
#include "state/data/BoardData.h"
#include "state/board/BoardConfig.h"
#include "state/runtime/BoardRuntimeConfig.h"
#include "Util/StringFormatters.h"
#include "Util/Logger.h"
#include "encoder.h"
#include "JSON_Encoder.h"
#include "../HAL/ADC.h"
#include "../HAL/TimerApi/TimerApi.h"

#ifndef min
#define min(x,y) x <= y ? x : y
#endif // min

#ifndef max
#define max(x,y) x >= y ? x : y
#endif // min

//! Size of temporal buffer used for JSON encoding purposes
#define TMP_MAX_LEN                                 64
//! Temporal buffer used for JSON encoding purposes
static char tmp[ TMP_MAX_LEN ];

// Track whether JSON header has been sent (reset when streaming stops)
static bool jsonHeaderSent = false;

/**
 * @brief Reset JSON encoder state (call when streaming stops)
 */
void json_ResetEncoder(void) {
    jsonHeaderSent = false;
}

/**
 * @brief Generate JSON metadata header with device info (sent once per session)
 *
 * Outputs a JSON object containing device metadata similar to CSV header:
 * {"meta":{"dev":"Nyquist NQ1","sn":"0123456789ABCDEF","tick_hz":1000000}}
 *
 * @param out      Output buffer
 * @param buffSize Size of output buffer
 * @return Bytes written (0 on failure)
 */
static size_t generateJsonHeader(char *out, size_t buffSize) {
    if (!out || buffSize < 128) {  // Ensure enough room for metadata
        return 0;
    }

    const tBoardConfig* boardConfig = (const tBoardConfig*)BoardConfig_Get(BOARDCONFIG_ALL_CONFIG, 0);
    if (!boardConfig) {
        LOG_E("[JSON] Board config is NULL, cannot generate header");
        return 0;
    }

    uint8_t variant = 0;
    uint64_t serialNum = 0;
    void* pVar = BoardConfig_Get(BOARDCONFIG_VARIANT, 0);
    void* pSer = BoardConfig_Get(BOARDCONFIG_SERIAL_NUMBER, 0);
    if (pVar) variant = *(uint8_t*)pVar;
    if (pSer) serialNum = *(uint64_t*)pSer;

    // Get timestamp tick rate with validation
    uint32_t tickRate = 0;
    uint8_t tsIdx = boardConfig->StreamingConfig.TSTimerIndex;
    tickRate = TimerApi_FrequencyGet(tsIdx);
    if (tickRate == 0) {
        // Fallback to safe default if timer not initialized
        // Note: This should not happen in production, indicates config issue
        tickRate = 1000000u;  // 1 MHz default
    }

    // Generate compact JSON metadata object
    int written = snprintf(out, buffSize,
        "{\"meta\":{\"dev\":\"%s %u\",\"sn\":\"%016llX\",\"tick_hz\":%u}}\n",
        DAQIFI_PRODUCT_NAME,
        variant,
        (unsigned long long)serialNum,
        tickRate);

    if (written < 0 || written >= (int)buffSize) {
        return 0;  // Not enough space
    }

    return (size_t)written;
}

size_t Json_Encode(tBoardData* state,
        NanopbFlagsArray* fields,
        uint8_t* pBuffer, size_t buffSize) {
    int tmpLen = 0;
    char* charBuffer = (char*) pBuffer;
    size_t startIndex = 0;
    size_t initialOffsetIndex = 0;
    size_t i = 0;
    bool encodeDIO = false;
    bool encodeADC = false;

    if (pBuffer == NULL) {
        return 0; // Return 0 if buffer is NULL
    }

    if (buffSize < 10) {
        return 0;  // Minimum buffer size check
    }

    // Generate metadata header on first call (once per streaming session)
    if (!jsonHeaderSent) {
        size_t headerLen = generateJsonHeader(charBuffer, buffSize);
        if (headerLen == 0) {
            // Not enough space for header
            if (buffSize > 0) {
                charBuffer[0] = '\0';
            }
            return 0;
        }
        startIndex = headerLen;  // Advance offset only, don't modify pointers
        initialOffsetIndex = startIndex;
        jsonHeaderSent = true;
    }

    // Start JSON sample object (write at current offset)
    int objWritten = snprintf(charBuffer + startIndex, buffSize - startIndex, "{\n");
    if (objWritten < 0 || objWritten >= (int)(buffSize - startIndex)) {
        // Could not start a new JSON object; signal failure for this frame
        if (buffSize > 0) {
            // Ensure termination without corrupting previous header
            size_t term = startIndex < buffSize ? startIndex : (buffSize - 1);
            charBuffer[term] = '\0';
        }
        return 0;  // Return 0 on failure (not partial header length)
    }
    startIndex += objWritten;
    initialOffsetIndex = startIndex;


    for (i = 0; i < fields->Size; ++i) {
        if (buffSize - startIndex < 3) {
            break;
        }

        switch (fields->Data[i]) {
            case DaqifiOutMessage_msg_time_stamp_tag:
            {
                int written = snprintf(charBuffer + startIndex,
                        buffSize - startIndex,
                        "\"ts\":%u,\n",
                        state->StreamTrigStamp);
                if (written < 0 || written >= (int)(buffSize - startIndex)) {
                    // Avoid out-of-bounds write when buffer is full
                    if (startIndex < buffSize) {
                        charBuffer[startIndex] = '\0';
                    }
                    return startIndex;  // Return early, not break
                }
                startIndex += written;
                break;
            }
            case DaqifiOutMessage_analog_in_data_tag:
                encodeADC = true;
                break;
            case DaqifiOutMessage_digital_data_tag:
                encodeDIO = true;
                break;
            case DaqifiOutMessage_device_status_tag:
                //TODO: message.device_status;
                break;
            case DaqifiOutMessage_batt_status_tag:
                //TODO: message.bat_level;
                break;
            case DaqifiOutMessage_pwr_status_tag:
                //TODO:  message.pwr_status;
                break;
            case DaqifiOutMessage_temp_status_tag:
                //TODO:  message.temp_status;
                break;
            case DaqifiOutMessage_analog_out_data_tag:
                //TODO:  message.analog_out_data[8];
                break;
            case DaqifiOutMessage_ip_addr_tag:
            {
                wifi_manager_settings_t* wifiSettings = &state->wifiSettings;
                inet_ntop(AF_INET, &wifiSettings->ipAddr.Val, tmp, TMP_MAX_LEN);
                tmpLen = strlen(tmp);
                if (tmpLen > 0) {
                    int written = snprintf(charBuffer + startIndex,
                            buffSize - startIndex,
                            "\"ip\":\"%s\",\n",
                            tmp);
                    if (written > 0 && written < (int)(buffSize - startIndex)) {
                        startIndex += written;
                    }
                }
                break;
            }
            case DaqifiOutMessage_host_name_tag:
            {
                //                WifiSettings* wifiSettings =                                
                //                        &state->wifiSettings;
                //                tmpLen = min(                                               
                //                        strlen(wifiSettings->hostName),                     
                //                        WIFI_MANAGER_DNS_CLIENT_MAX_HOSTNAME_LEN);
                //                
                //                if (tmpLen > 0)
                //                {
                //                    startIndex += snprintf(                                 
                //                        charBuffer + startIndex,                            
                //                        JSON_ENCODER_BUFFER_SIZE - startIndex,              
                //                        " \"host\"=\"%s\",\n\r",                            
                //                        wifiSettings->hostName);
                //                }

                break;
            }
            case DaqifiOutMessage_mac_addr_tag:
            {
                wifi_manager_settings_t* wifiSettings = &state->wifiSettings;
                tmpLen = MacAddr_ToString(wifiSettings->macAddr.addr, tmp, TMP_MAX_LEN);
                if (tmpLen > 0) {
                    int written = snprintf(charBuffer + startIndex,
                            buffSize - startIndex,
                            "\"mac\":\"%s\",\n",
                            tmp);
                    if (written > 0 && written < (int)(buffSize - startIndex)) {
                        startIndex += written;
                    }
                }
                break;
            }
            case DaqifiOutMessage_ssid_tag:
            {
                wifi_manager_settings_t* wifiSettings = &state->wifiSettings;
                tmpLen = min(strlen(wifiSettings->ssid), WDRV_WINC_MAX_SSID_LEN);
                if (tmpLen > 0) {
                    int written = snprintf(charBuffer + startIndex,
                            buffSize - startIndex,
                            "\"ssid\":\"%s\",\n",
                            wifiSettings->ssid);
                    if (written > 0 && written < (int)(buffSize - startIndex)) {
                        startIndex += written;
                    }
                }
                break;
            }
            case DaqifiOutMessage_digital_port_dir_tag:
                //TODO:  message.digital_port_dir;
                break;
            case DaqifiOutMessage_analog_in_port_rse_tag:
                //TODO:  message.analog_in_port_rse;
                break;
            case DaqifiOutMessage_analog_in_port_enabled_tag:
                //TODO:  message.analog_in_port_enabled;
                break;
            case DaqifiOutMessage_analog_in_port_range_tag:
                //TODO:  message.analog_in_port_range;
                break;
            case DaqifiOutMessage_analog_in_res_tag:
                //TODO:  message.analog_in_res;
                break;
            case DaqifiOutMessage_analog_out_res_tag:
                //TODO:  message.analog_out_res;
                break;
            case DaqifiOutMessage_device_pn_tag:
            {
                //TODO:  message.device_pn[32];
                break;
            }
            case DaqifiOutMessage_device_port_tag:
            {
                wifi_manager_settings_t* wifiSettings = &state->wifiSettings;
                int written = snprintf(charBuffer + startIndex,
                        buffSize - startIndex,
                        "\"port\":%u,\n",
                        wifiSettings->tcpPort);
                if (written > 0 && written < (int)(buffSize - startIndex)) {
                    startIndex += written;
                }
                break;
            }
            case DaqifiOutMessage_wifi_security_mode_tag:
            {
                wifi_manager_settings_t* wifiSettings = &state->wifiSettings;
                int written = snprintf(charBuffer + startIndex,
                        buffSize - startIndex,
                        "\"sec\":%u,\n",
                        wifiSettings->securityMode);
                if (written > 0 && written < (int)(buffSize - startIndex)) {
                    startIndex += written;
                }
                break;
            }
            case DaqifiOutMessage_friendly_device_name_tag:
                //TODO:  message.friendly_device_name[32];
                break;
            default:
                // Skip unknown fields
                break;
        }
    }

    // Encode DIO if needed
    if (encodeDIO) {
        size_t diStart = startIndex;

        int written = snprintf(charBuffer + startIndex,
                buffSize - startIndex,
                "\"di\":[");
        if (written < 0 || written >= (int)(buffSize - startIndex)) return startIndex;
        startIndex += written;

        size_t diElementsStart = startIndex;

        while (((buffSize - startIndex) >= 65) && (!DIOSampleList_IsEmpty(&state->DIOSamples))) {
            DIOSample data;
            // Peek first to avoid data loss if write fails
            if (!DIOSampleList_PeekFront(&state->DIOSamples, &data)) break;

            int elemWritten = snprintf(charBuffer + startIndex,
                    buffSize - startIndex,
                    "{\"ts\":%u, \"mask\":%u, \"val\":%u},",
                    state->StreamTrigStamp - data.Timestamp,
                    data.Mask,
                    data.Values);
            if (elemWritten < 0 || elemWritten >= (int)(buffSize - startIndex)) {
                break;  // Keep sample for next attempt
            }

            // Write succeeded - commit by removing sample from queue
            startIndex += elemWritten;
            DIOSampleList_PopFront(&state->DIOSamples, &data);
        }

        if (startIndex == diElementsStart) {
            // No elements were written; roll back emission of "di":[
            startIndex = diStart;
        } else {
            // Remove trailing comma and close array
            if (startIndex > 0 && charBuffer[startIndex - 1] == ',') {
                startIndex -= 1;
            }
            int closeWritten = snprintf(charBuffer + startIndex,
                    buffSize - startIndex,
                    "],\n");
            if (closeWritten < 0 || closeWritten >= (int)(buffSize - startIndex)) return startIndex;
            startIndex += closeWritten;
        }

        initialOffsetIndex = startIndex; // so that analog data can be appended
    }

    // Encode ADC if needed
    if (encodeADC) {
        startIndex = initialOffsetIndex; // Remove the initial timestamp added

        uint32_t qSize = AInSampleList_Size();
        AInPublicSampleList_t *pPublicSampleList;
        while (((buffSize - startIndex) >= 65) && (qSize > 0)) {
            AInSample data;
            if (!AInSampleList_PopFront(&pPublicSampleList)) {
                break;
            }
            if (pPublicSampleList == NULL)
                break;
            qSize--;
            bool timestampAdded = false;
            for (int i = 0; i < MAX_AIN_PUBLIC_CHANNELS; i++) {
                if (!pPublicSampleList->isSampleValid[i])
                    continue;

                 data = pPublicSampleList->sampleElement[i];
                if (!timestampAdded) {
                    int written = snprintf(charBuffer + startIndex,
                            buffSize - startIndex,
                            "\"ts\":%u,\n",
                            data.Timestamp);
                    if (written < 0 || written >= (int)(buffSize - startIndex)) break;
                    startIndex += written;

                    written = snprintf(charBuffer + startIndex,
                            buffSize - startIndex,
                            "\"ai\":[\n");
                    if (written < 0 || written >= (int)(buffSize - startIndex)) break;
                    startIndex += written;
                    timestampAdded = true;
                }


                double voltage = ADC_ConvertToVoltage(&data) * 1000; // Convert to millivolts
                int written = snprintf(charBuffer + startIndex,
                        buffSize - startIndex,
                        "{\"ch\":%u, \"val\":%u},\n",
                        data.Channel,
                        (int) voltage);
                if (written < 0 || written >= (int)(buffSize - startIndex)) break;
                startIndex += written;
            }

            AInSampleList_FreeToPool(pPublicSampleList);  // Use pool instead of vPortFree
            if(startIndex == initialOffsetIndex) //no adc data added
                break;
            // Remove trailing comma and close adc array
            if (startIndex >= 2 && charBuffer[startIndex - 2] == ',') {
                startIndex -= 2; // Remove trailing comma
            }
            int written = snprintf(charBuffer + startIndex,
                    buffSize - startIndex,
                    "\n],\n");
            if (written < 0 || written >= (int)(buffSize - startIndex)) break;
            startIndex += written;
        }
    }

    // Close the JSON object
    if (startIndex >= 2 && charBuffer[startIndex - 2] == ',') {
        startIndex -= 2; // Remove trailing comma
    }
    int written = snprintf(charBuffer + startIndex,
            buffSize - startIndex,
            "\n}\n");
    if (written > 0 && written < (int)(buffSize - startIndex)) {
        startIndex += written;
    }

    // Ensure safe null-termination without exceeding buffer
    if (buffSize > 0) {
        if (startIndex >= buffSize) {
            startIndex = buffSize - 1;
        }
        charBuffer[startIndex] = '\0';
    }
    return startIndex; // Return the number of bytes written
}
