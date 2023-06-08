/*! @file JSON_Encoder.c 
 * 
 * This file implements the functions to manage the JSON encoder
 */

#include "nanopb/DaqifiOutMessage.pb.h"
#include "state/data/BoardData.h"
#include "Util/StringFormatters.h"
#include "encoder.h"
#include "JSON_Encoder.h"

//! Buffer size used for streaming purposes
#define JSON_ENCODER_BUFFER_SIZE                    ENCODER_BUFFER_SIZE
//! Size of temporal buffer used for JSON encoding purposes
#define TMP_MAX_LEN                                 64
//! Temporal buffer used for JSON encoding purposes
static char tmp[ TMP_MAX_LEN ];

size_t Json_Encode(     tBoardData* state,                                  \
                        NanopbFlagsArray* fields,                           \
                        uint8_t** ppBuffer)
{
    int tmpLen = 0;
    char *buffer = (char *)Encoder_Get_Buffer();
    char* charBuffer = (char*)buffer;
    size_t startIndex = snprintf(                                           \
                        charBuffer,                                         \
                        JSON_ENCODER_BUFFER_SIZE,                           \
                        "\n\r{\n\r");
    size_t i=0;
    bool encodeDIO = false;
    bool encodeADC = false;    
    
    if( ppBuffer == NULL ){
        return 0;
    }
    else{
        *ppBuffer = (char *)buffer;
    }
    
    for (i=0; i<fields->Size; ++i)
    {
        if (JSON_ENCODER_BUFFER_SIZE - startIndex < 3)
        {
            break;
        }
        
        switch(fields->Data[i])
        {
            case DaqifiOutMessage_msg_time_stamp_tag:
                startIndex += snprintf(                                     \
                        charBuffer + startIndex,                            \
                        JSON_ENCODER_BUFFER_SIZE - startIndex,              \
                        " \"timestamp\"=%u,\n\r",                           \
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
                WifiSettings* wifiSettings =                                \
                        &state->wifiSettings.settings.wifi;
                tmpLen = IpAddr_ToString(                                   \
                        wifiSettings->configFlags &                         \
                        TCPIP_NETWORK_CONFIG_IPV6_ADDRESS,
                        &wifiSettings->ipAddr,                              \
                        tmp,                                                \
                        TMP_MAX_LEN);
                if (tmpLen > 0)
                {
                    startIndex += snprintf(                                 \
                        charBuffer + startIndex,                            \
                        JSON_ENCODER_BUFFER_SIZE - startIndex,              \
                        " \"ip\"=\"%s\",\n\r",                              \
                        tmp);
                }

                break;
            }
            case DaqifiOutMessage_host_name_tag:
            {
                WifiSettings* wifiSettings =                                \
                        &state->wifiSettings.settings.wifi;
                tmpLen = min(                                               \
                        strlen(wifiSettings->hostName),                     \
                        TCPIP_DNS_CLIENT_MAX_HOSTNAME_LEN);
                
                if (tmpLen > 0)
                {
                    startIndex += snprintf(                                 \
                        charBuffer + startIndex,                            \
                        JSON_ENCODER_BUFFER_SIZE - startIndex,              \
                        " \"host\"=\"%s\",\n\r",                            \
                        wifiSettings->hostName);
                }

                break;
            }
            case DaqifiOutMessage_mac_addr_tag:
            {
                WifiSettings* wifiSettings =                                \
                        &state->wifiSettings.settings.wifi;
                tmpLen = MacAddr_ToString(                                  \
                        &wifiSettings->macAddr,                             \
                        tmp,                                                \
                        TMP_MAX_LEN);
                
                if (tmpLen > 0)
                {
                    startIndex += snprintf(                                 \
                        charBuffer + startIndex,                            \
                        JSON_ENCODER_BUFFER_SIZE - startIndex,              \
                        " \"mac\"=\"%s\",\n\r",                             \
                        tmp);
                }
                
                break;
            }
            case DaqifiOutMessage_ssid_tag:
            {
                WifiSettings* wifiSettings =                                \
                        &state->wifiSettings.settings.wifi;
                tmpLen = min(                                               \
                        strlen(wifiSettings->ssid),                         \
                        TCPIP_DNS_CLIENT_MAX_HOSTNAME_LEN);
                
                if (tmpLen > 0)
                {
                    startIndex += snprintf(                                 \
                        charBuffer + startIndex,                            \
                        JSON_ENCODER_BUFFER_SIZE - startIndex,              \
                        " \"ssid\"=\"%s\",\n\r",                            \
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
                WifiSettings* wifiSettings =                                \
                        &state->wifiSettings.settings.wifi;
                startIndex += snprintf(                                     \
                        charBuffer + startIndex,                            \
                        JSON_ENCODER_BUFFER_SIZE - startIndex,              \
                        " \"port\"=\"%u\",\n\r",                            \
                        wifiSettings->tcpPort);
                
                break;
            }
            case DaqifiOutMessage_wifi_security_mode_tag:
            {
                WifiSettings* wifiSettings =                                \
                        &state->wifiSettings.settings.wifi;
                startIndex += snprintf(                                     \
                        charBuffer + startIndex,                            \
                        JSON_ENCODER_BUFFER_SIZE - startIndex,              \
                        " \"sec\"=\"%u\",\n\r",                             \
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
    
    if (encodeDIO)
    {
        startIndex += snprintf(                                             \
                        charBuffer + startIndex,                            \
                        JSON_ENCODER_BUFFER_SIZE - startIndex,              \
                        " \"dio\"=[\n\r");
        
        while (((JSON_ENCODER_BUFFER_SIZE - startIndex) >= 65) &&           \
               (!DIOSampleList_IsEmpty(&state->DIOSamples)))
        {
            DIOSample data;
            DIOSampleList_PopFront(&state->DIOSamples, &data);
            startIndex += snprintf(                                         \
                        charBuffer + startIndex,                            \
                        JSON_ENCODER_BUFFER_SIZE - startIndex,              \
                        "  {\"time\"=%u, \"mask\"=%u, \"data\"=%u},\n\r",   \
                        data.Timestamp-state->StreamTrigStamp,              \
                        data.Mask,                                          \
                        data.Values);
        }
        
        startIndex += snprintf(                                             \
                        charBuffer + startIndex,                            \
                        JSON_ENCODER_BUFFER_SIZE - startIndex,              \
                        " ],\n\r");
    }
    
    if (encodeADC)
    {
        startIndex += snprintf(                                             \
                        charBuffer + startIndex,                            \
                        JSON_ENCODER_BUFFER_SIZE - startIndex,              \
                        " \"adc\"=[\n\r");
        
        while (((JSON_ENCODER_BUFFER_SIZE - startIndex) >= 65) &&           \
               (!AInSampleList_IsEmpty(&state->AInSamples)))
        {
            AInSample data;
            AInSampleList_PopFront(&state->AInSamples, &data);
            startIndex += snprintf(                                         \
                        charBuffer + startIndex,                            \
                        JSON_ENCODER_BUFFER_SIZE - startIndex,              \
                        "  {\"time\"=%u, \"ch\"=%u, \"data\"=%u},\n\r",     \
                        data.Timestamp-state->StreamTrigStamp,              \
                        data.Channel,                                       \
                        data.Value);
        }
        
        startIndex += snprintf(                                             \
                        charBuffer + startIndex,                            \
                        JSON_ENCODER_BUFFER_SIZE - startIndex,              \
                        " ],\n\r");
    }
    
    startIndex += snprintf(                                                 \
                        charBuffer + startIndex,                            \
                        JSON_ENCODER_BUFFER_SIZE - startIndex,              \
                        "}");
    charBuffer[startIndex] = '\0';
    
    return startIndex;
}
