/*! @file JSON_Encoder.c 
 * 
 * This file implements the functions to manage the JSON encoder
 */

#include "../services/DaqifiPB/DaqifiOutMessage.pb.h"
#include "state/data/BoardData.h"
#include "Util/StringFormatters.h"
#include "encoder.h"
#include "JSON_Encoder.h"
#include "../HAL/ADC.h"

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

size_t Json_Encode(tBoardData* state,
        NanopbFlagsArray* fields,
        uint8_t* pBuffer, size_t buffSize) {
    int tmpLen = 0;
    char* charBuffer = (char*) pBuffer;
    size_t startIndex = snprintf(charBuffer, buffSize, "{\n");
    size_t initialOffsetIndex = startIndex;
    size_t i = 0;
    bool encodeDIO = false;
    bool encodeADC = false;

    if (pBuffer == NULL) {
        return 0; // Return 0 if buffer is NULL
    }

    if (pBuffer == NULL) {
        return 0;
    }


    for (i = 0; i < fields->Size; ++i) {
        if (buffSize - startIndex < 3) {
            break;
        }

        switch (fields->Data[i]) {
            case DaqifiOutMessage_msg_time_stamp_tag:
                startIndex += snprintf(charBuffer + startIndex,
                        buffSize - startIndex,
                        "\"timestamp\":%u,\n",
                        state->StreamTrigStamp);
                break;
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
                    startIndex += snprintf(charBuffer + startIndex,
                            buffSize - startIndex,
                            "\"ip\":\"%s\",\n",
                            tmp);
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
                    startIndex += snprintf(charBuffer + startIndex,
                            buffSize - startIndex,
                            "\"mac\":\"%s\",\n",
                            tmp);
                }
                break;
            }
            case DaqifiOutMessage_ssid_tag:
            {
                wifi_manager_settings_t* wifiSettings = &state->wifiSettings;
                tmpLen = min(strlen(wifiSettings->ssid), WDRV_WINC_MAX_SSID_LEN);
                if (tmpLen > 0) {
                    startIndex += snprintf(charBuffer + startIndex,
                            buffSize - startIndex,
                            "\"ssid\":\"%s\",\n",
                            wifiSettings->ssid);
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
                startIndex += snprintf(charBuffer + startIndex,
                        buffSize - startIndex,
                        "\"port\":%u,\n",
                        wifiSettings->tcpPort);
                break;
            }
            case DaqifiOutMessage_wifi_security_mode_tag:
            {
                wifi_manager_settings_t* wifiSettings = &state->wifiSettings;
                startIndex += snprintf(charBuffer + startIndex,
                        buffSize - startIndex,
                        "\"sec\":%u,\n",
                        wifiSettings->securityMode);
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
        startIndex += snprintf(charBuffer + startIndex,
                buffSize - startIndex,
                "\"dio\":[");

        while (((buffSize - startIndex) >= 65) && (!DIOSampleList_IsEmpty(&state->DIOSamples))) {
            DIOSample data;
            DIOSampleList_PopFront(&state->DIOSamples, &data);
            startIndex += snprintf(charBuffer + startIndex,
                    buffSize - startIndex,
                    "{\"time\":%u, \"mask\":%u, \"data\":%u},",
                    state->StreamTrigStamp - data.Timestamp,
                    data.Mask,
                    data.Values);
        }

        // Remove trailing comma and add closing bracket for dio array
        if (charBuffer[startIndex - 1] == ',') {
            startIndex -= 1; // Remove trailing comma
        }
        startIndex += snprintf(charBuffer + startIndex,
                buffSize - startIndex,
                "],\n");
        initialOffsetIndex=startIndex; // so that analog data can be appended
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
                    startIndex += snprintf(charBuffer + startIndex,
                            buffSize - startIndex,
                            "\"timestamp\":%u,\n",
                            data.Timestamp);
                    startIndex += snprintf(charBuffer + startIndex,
                            buffSize - startIndex,
                            "\"adc\":[\n");
                    timestampAdded = true;
                }

               
                double voltage = ADC_ConvertToVoltage(&data) * 1000; // Convert to millivolts
                startIndex += snprintf(charBuffer + startIndex,
                        buffSize - startIndex,
                        "{\"ch\":%u, \"data\":%u},\n",
                        data.Channel,
                        (int) voltage);
            }

            free(pPublicSampleList);
            if(startIndex == initialOffsetIndex) //no adc data added
                break;
            // Remove trailing comma and close adc array
            if (charBuffer[startIndex - 2] == ',') {
                startIndex -= 2; // Remove trailing comma
            }
            startIndex += snprintf(charBuffer + startIndex,
                    buffSize - startIndex,
                    "\n],\n");
        }
    }

    // Close the JSON object
    if (charBuffer[startIndex - 2] == ',') {
        startIndex -= 2; // Remove trailing comma
    }
    startIndex += snprintf(charBuffer + startIndex,
            buffSize - startIndex,
            "\n}\n");

    charBuffer[startIndex] = '\0'; // Null-terminate the JSON string
    return startIndex; // Return the number of bytes written
}
