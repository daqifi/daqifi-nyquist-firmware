/*! @file NanoPB_Encoder.c 
 * 
 * This file implements the functions to manage the NanoPB encoder
 */

#include "libraries/nanopb/pb_encode.h"
#include "libraries/nanopb/pb_decode.h"


#include "state/data/BoardData.h"
#include "Util/Logger.h"

#include "DaqifiOutMessage.pb.h"
//#include "state/board/BoardConfig.h"
#include "encoder.h"
#include "NanoPB_Encoder.h"
#include "services/daqifi_settings.h"
#ifndef min
    #define min(x,y) x <= y ? x : y
#endif // min

#ifndef max
    #define max(x,y) x >= y ? x : y
#endif // min
//! Buffer size used for streaming purposes
#define NANOPB_ENCODER_BUFFER_SIZE                      (ENCODER_BUFFER_SIZE>1350?1350:ENCODER_BUFFER_SIZE)
//#if ENCODER_BUFFER_SIZE >1300
//#define NANOPB_ENCODER_BUFFER_SIZE                    1300
//#else
//#define NANOPB_ENCODER_BUFFER_SIZE                    ENCODER_BUFFER_SIZE
//#endif

/*  TODO: Verify this length calculation is accurate.  
**  
**  NOTE: Perhaps the most official way to calculate length would be something like:
**  Person myperson = ...;
**  pb_ostream_t sizestream = {0};
**  pb_encode(&sizestream, Person_fields, &myperson);
**  printf("Encoded size is %d\n", sizestream.bytes_written);
**  per https://jpa.kapsi.fi/nanopb/docs/concepts.html
**
**  However, it would be more expensive.
*/

static int Nanopb_EncodeLength(const NanopbFlagsArray* fields)
{
    int i;
    int len = 0;
    DaqifiOutMessage* out;
    
    for (i=0; i<fields->Size; i++)
    {
        switch(fields->Data[i])
        {
            case DaqifiOutMessage_msg_time_stamp_tag:
                len += sizeof(out->has_msg_time_stamp);
                len += sizeof(out->msg_time_stamp);
                break;
                
            case DaqifiOutMessage_analog_in_data_tag:
                len += sizeof(out->analog_in_data_count);
                len += sizeof(out->analog_in_data);
                break;
                
            case DaqifiOutMessage_analog_in_data_float_tag:
                len += sizeof(out->analog_in_data_float_count);
                len += sizeof(out->analog_in_data_float);
                break;
                
            case DaqifiOutMessage_analog_in_data_ts_tag:
                len += sizeof(out->analog_in_data_ts_count);
                len += sizeof(out->analog_in_data_ts);
                break;
                
            case DaqifiOutMessage_digital_data_tag:
                len += sizeof(out->has_digital_data);
                len += sizeof(out->digital_data);
                break;
   
            case DaqifiOutMessage_digital_data_ts_tag:
                len += sizeof(out->digital_data_ts_count);
                len += sizeof(out->digital_data_ts);
                break;
            case DaqifiOutMessage_analog_out_data_tag:
                len += sizeof(out->analog_out_data_count);
                len += sizeof(out->analog_out_data);
                break;
                
            case DaqifiOutMessage_device_status_tag:
                len += sizeof(out->has_device_status);
                len += sizeof(out->device_status);
                break;
                
            case DaqifiOutMessage_pwr_status_tag:
                len += sizeof(out->has_pwr_status);
                len += sizeof(out->pwr_status);
                break;
                
            case DaqifiOutMessage_batt_status_tag:
                len += sizeof(out->has_batt_status);
                len += sizeof(out->batt_status);
                break;
                
            case DaqifiOutMessage_temp_status_tag:
                len += sizeof(out->has_temp_status);
                len += sizeof(out->temp_status);
                break;
                
            case DaqifiOutMessage_timestamp_freq_tag:
                len += sizeof(out->has_timestamp_freq);
                len += sizeof(out->timestamp_freq);
				break;
                
            case DaqifiOutMessage_analog_in_port_num_tag:
                len += sizeof(out->has_analog_in_port_num);
                len += sizeof(out->analog_in_port_num); 
            break;
            
            case DaqifiOutMessage_analog_in_port_num_priv_tag:
                len += sizeof(out->has_analog_in_port_num_priv);
                len += sizeof(out->analog_in_port_num_priv);
                break;
            
            case DaqifiOutMessage_analog_in_port_type_tag:
                len += sizeof(out->has_analog_in_port_type);
                break;
                
            case DaqifiOutMessage_analog_in_port_av_rse_tag:
                len += sizeof(out->has_analog_in_port_av_rse);
                len += sizeof(out->analog_in_port_av_rse);
                break;
                
            case DaqifiOutMessage_analog_in_port_rse_tag:
                len += sizeof(out->has_analog_in_port_rse);
                len += sizeof(out->analog_in_port_rse);
				break;

            case DaqifiOutMessage_analog_in_port_enabled_tag:             
                len += sizeof(out->has_analog_in_port_enabled);
                len += sizeof(out->analog_in_port_enabled);
                break;
                
            case DaqifiOutMessage_analog_in_port_av_range_tag:
                len += sizeof(out->analog_in_port_av_range);
                len += sizeof(out->analog_in_port_av_range_count);
                break;
       
            case DaqifiOutMessage_analog_in_port_av_range_priv_tag:
                len += sizeof(out->analog_in_port_av_range_priv_count);
                len += sizeof(out->analog_in_port_av_range_priv);
                break;
                
            case DaqifiOutMessage_analog_in_port_range_tag:
                len += sizeof(out->analog_in_port_range);
                len += sizeof(out->analog_in_port_range_count);
				break;
            
            case DaqifiOutMessage_analog_in_port_range_priv_tag:
                len += sizeof(out->analog_in_port_range_priv);
                len += sizeof(out->analog_in_port_range_priv_count);
				break;
            
            case DaqifiOutMessage_analog_in_res_tag:
                len += sizeof(out->has_analog_in_res);
                len += sizeof(out->analog_in_res);
				break;
                
            case DaqifiOutMessage_analog_in_res_priv_tag:
                len += sizeof(out->has_analog_in_res_priv);
                len += sizeof(out->analog_in_res_priv);
				break;
                
            case DaqifiOutMessage_analog_in_int_scale_m_tag:
                len += sizeof(out->analog_in_int_scale_m);
                len += sizeof(out->analog_in_int_scale_m_count);
				break;
  
            case DaqifiOutMessage_analog_in_int_scale_m_priv_tag:     
                len += sizeof(out->analog_in_int_scale_m_priv);
                len += sizeof(out->analog_in_int_scale_m_priv_count);
				break;
  
            case DaqifiOutMessage_analog_in_cal_m_tag:  
                len += sizeof(out->analog_in_cal_m);
                len += sizeof(out->analog_in_cal_m_count);
				break;
 
            case DaqifiOutMessage_analog_in_cal_b_tag:        
                len += sizeof(out->analog_in_cal_b);
                len += sizeof(out->analog_in_cal_b_count);
				break;
 
            case DaqifiOutMessage_analog_in_cal_m_priv_tag:
                len += sizeof(out->analog_in_cal_m_priv);
                len += sizeof(out->analog_in_cal_m_priv_count);
				break;
               
            case DaqifiOutMessage_analog_in_cal_b_priv_tag:
                len += sizeof(out->analog_in_cal_b_priv_count);
                len += sizeof(out->analog_in_cal_b_priv);
				break;
                           
            case DaqifiOutMessage_digital_port_num_tag:
                len += sizeof(out->has_digital_port_num);
                len += sizeof(out->digital_port_num);
				break;
                
            case DaqifiOutMessage_digital_port_dir_tag:
                len += sizeof(out->has_digital_port_dir);
                len += sizeof(out->digital_port_dir);
				break;

            case DaqifiOutMessage_analog_out_res_tag:
                len += sizeof(out->has_analog_out_res);
                len += sizeof(out->analog_out_res);
				break;                   
                
            case DaqifiOutMessage_ip_addr_tag:
                len += sizeof(out->has_ip_addr);
                len += sizeof(out->ip_addr);
                break;
            
            case DaqifiOutMessage_net_mask_tag:
                len += sizeof(out->has_net_mask);
                len += sizeof(out->net_mask);
                break;

            case DaqifiOutMessage_gateway_tag:
                len += sizeof(out->has_gateway);
                len += sizeof(out->gateway);
				break;
    
            case DaqifiOutMessage_primary_dns_tag:
                len += sizeof(out->has_primary_dns);
                len += sizeof(out->primary_dns);
				break;
            
            case DaqifiOutMessage_secondary_dns_tag:
                len += sizeof(out->has_secondary_dns);
                len += sizeof(out->secondary_dns.bytes);
				break;
            
            case DaqifiOutMessage_mac_addr_tag:
                len += sizeof(out->has_mac_addr);
                len += sizeof(out->mac_addr);    
                break;
             
            case DaqifiOutMessage_ip_addr_v6_tag:
                len += sizeof(out->has_ip_addr_v6);
                len += sizeof(out->ip_addr_v6);
                break;
            
            case DaqifiOutMessage_sub_pre_length_v6_tag:
                len += sizeof(out->has_sub_pre_length_v6);
                len += sizeof(out->sub_pre_length_v6);
				break;
                
            case DaqifiOutMessage_gateway_v6_tag:
                len += sizeof(out->has_gateway_v6);
                len += sizeof(out->gateway_v6);
				break;
                
            case DaqifiOutMessage_primary_dns_v6_tag:
                len += sizeof(out->has_primary_dns_v6);
				len += sizeof(out->primary_dns_v6);
                break;
                
            case DaqifiOutMessage_secondary_dns_v6_tag:
                len += sizeof(out->has_secondary_dns_v6);
                len += sizeof(out->secondary_dns_v6);
				break;
                
            case DaqifiOutMessage_eui_64_tag:
                len += sizeof(out->has_eui_64);
                len += sizeof(out->eui_64);
				break;
            
            case DaqifiOutMessage_host_name_tag:
                len += sizeof(out->has_host_name);
                len += sizeof(out->host_name);
                break;
            
            case DaqifiOutMessage_device_port_tag:
                len += sizeof(out->has_device_port);
                len += sizeof(out->device_port);
                break;
            
            case DaqifiOutMessage_friendly_device_name_tag:
                len += sizeof(out->has_friendly_device_name);
                len += sizeof(out->friendly_device_name);
                break;
                
            case DaqifiOutMessage_ssid_tag:
                len += sizeof(out->has_ssid);
                len += sizeof(out->ssid);
                break;
            
            case DaqifiOutMessage_wifi_security_mode_tag:
                len += sizeof(out->has_wifi_security_mode);
                len += sizeof(out->wifi_security_mode);
                break;
  
            case DaqifiOutMessage_wifi_inf_mode_tag:            
                len += sizeof(out->has_wifi_inf_mode);
                len += sizeof(out->wifi_inf_mode);
                break;
          
            case DaqifiOutMessage_av_ssid_tag:
                len += sizeof(out->av_ssid_count);
                len += sizeof(out->av_ssid);
                break;
         
            case DaqifiOutMessage_av_ssid_strength_tag:
                len += sizeof(out->av_ssid_strength_count);
                len += sizeof(out->av_ssid_strength);
                break;
                                  
            case DaqifiOutMessage_av_wifi_security_mode_tag:
                len += sizeof(out->av_wifi_security_mode_count);
                len += sizeof(out->av_wifi_security_mode);
                break;

            case DaqifiOutMessage_av_wifi_inf_mode_tag:
                len += sizeof(out->av_wifi_inf_mode_count);
                len += sizeof(out->av_wifi_inf_mode);     
                break;
  
            case DaqifiOutMessage_device_pn_tag:
                len += sizeof(out->has_device_pn);
                len += sizeof(out->device_pn);
                break;
           
            case DaqifiOutMessage_device_hw_rev_tag:
                len += sizeof(out->has_device_hw_rev);
                len += sizeof(out->device_hw_rev);
				break;
                
            case DaqifiOutMessage_device_fw_rev_tag:
                len += sizeof(out->has_device_fw_rev);
                len += sizeof(out->device_fw_rev);
                break;
                
            case DaqifiOutMessage_device_sn_tag:
                len += sizeof(out->has_device_sn);
                len += sizeof(out->device_sn);
                break;
                
            default:
                // Skip unknown fields
                break;
        }
    }
    
    return len;
}

size_t Nanopb_Encode(   tBoardData* state,                                  \
                        const NanopbFlagsArray* fields,                     \
                        uint8_t* ppBuffer)
{
//    tBoardConfig * pBoardConfig = BoardConfig_Get(                          
//                        BOARDCONFIG_ALL_CONFIG,                             
//                        0 );
//    StreamingRuntimeConfig * pRuntimeStreamConfig = BoardRunTimeConfig_Get( 
//                        BOARDRUNTIME_STREAMING_CONFIGURATION); 
//    AInRuntimeArray * pRuntimeAInChannels = BoardRunTimeConfig_Get(         
//                        BOARDRUNTIMECONFIG_AIN_CHANNELS);    
//    AInModRuntimeArray * pRuntimeAInModules = BoardRunTimeConfig_Get(       
//                        BOARDRUNTIMECONFIG_AIN_MODULES);      
//    DIORuntimeArray * pRuntimeDIOChannels = BoardRunTimeConfig_Get(         
//                        BOARDRUNTIMECONFIG_DIO_CHANNELS);     
    volatile uint32_t pBuffOffset=0; //used to pack multiple PB packets
    // If we cannot encode a whole message, bail out
    if (NANOPB_ENCODER_BUFFER_SIZE < Nanopb_EncodeLength(fields))
    {
        return 0;
    }
    if( ppBuffer == NULL ){
        return 0;
    }
    //*ppBuffer = Encoder_Get_Buffer();

    DaqifiOutMessage message = DaqifiOutMessage_init_default;
    size_t i=0;
    for (i=0; i<fields->Size; i++)
    {
        switch(fields->Data[i])
        {
            case DaqifiOutMessage_msg_time_stamp_tag:
                message.has_msg_time_stamp = true;
                message.msg_time_stamp = state->StreamTrigStamp;
                break;
            case DaqifiOutMessage_analog_in_data_tag:
//               
                break;
            case DaqifiOutMessage_analog_in_data_float_tag:
//                message.analog_in_data_float_count = 0;
                break;
            case DaqifiOutMessage_analog_in_data_ts_tag:
//                message.analog_in_data_ts_count = 0;
                break;
            case DaqifiOutMessage_digital_data_tag:
            {
//               
                break;
            }
            case DaqifiOutMessage_digital_data_ts_tag:
                message.digital_data_ts_count = 0;
                break;
            case DaqifiOutMessage_analog_out_data_tag:
                message.analog_out_data_count = 0;
                //TODO:  message.analog_out_data[8];
                break;
            case DaqifiOutMessage_device_status_tag:
                message.has_device_status = false;
                // message.device_status;
                break;
            case DaqifiOutMessage_pwr_status_tag:
//                message.has_pwr_status = true;
//                message.pwr_status = state->PowerData.powerState;
                break;
            case DaqifiOutMessage_batt_status_tag:
//                message.has_batt_status = true;
//                message.batt_status = state->PowerData.chargePct;
                break;
            case DaqifiOutMessage_temp_status_tag:
                message.has_temp_status = false;
                //TODO:  message.temp_status;
                break;
            case DaqifiOutMessage_timestamp_freq_tag:
//               
				break;
            case DaqifiOutMessage_analog_in_port_num_tag:
            {                
                      
				break;
            }
            case DaqifiOutMessage_analog_in_port_num_priv_tag:
            {   
                break;
            }
            case DaqifiOutMessage_analog_in_port_type_tag:
                message.has_analog_in_port_type = false;
                break;
            case DaqifiOutMessage_analog_in_port_av_rse_tag:
                message.has_analog_in_port_av_rse = false;
                break;
            case DaqifiOutMessage_analog_in_port_rse_tag:
            {

				break;
            }
            case DaqifiOutMessage_analog_in_port_enabled_tag:
            {
//             
				break;
            }
            case DaqifiOutMessage_analog_in_port_av_range_tag:
            {
//               
                break;
            }

            case DaqifiOutMessage_analog_in_port_av_range_priv_tag:
                message.analog_in_port_av_range_priv_count = 0;
                break;
            case DaqifiOutMessage_analog_in_port_range_tag:
            {
//               
				break;
            }
            case DaqifiOutMessage_analog_in_port_range_priv_tag:
            {
//              
				break;
            }
            case DaqifiOutMessage_analog_in_res_tag:
//               
				break;
            case DaqifiOutMessage_analog_in_res_priv_tag:
//                message.has_analog_in_res_priv = true;
//                message.analog_in_res_priv = pBoardConfig->AInModules.Data[0].Config.MC12b.Resolution;
				break;
            case DaqifiOutMessage_analog_in_int_scale_m_tag:
            {
//              
				break;
            }
            case DaqifiOutMessage_analog_in_int_scale_m_priv_tag:
            {
//               
				break;
            }                      
            case DaqifiOutMessage_analog_in_cal_m_tag:
            {
//              
				break;
            }                          
            case DaqifiOutMessage_analog_in_cal_b_tag:
            {
//             
				break;
            }                
            case DaqifiOutMessage_analog_in_cal_m_priv_tag:
            {
//               
				break;
            }                
            case DaqifiOutMessage_analog_in_cal_b_priv_tag:
            {
//              
				break;
            }                   
            case DaqifiOutMessage_digital_port_num_tag:
//                message.has_digital_port_num = true;
//                message.digital_port_num = pBoardConfig->DIOChannels.Size;
				break;
            case DaqifiOutMessage_digital_port_dir_tag:
            {
//               
				break;
            }
            case DaqifiOutMessage_analog_out_res_tag:
//                
				break;                   
            case DaqifiOutMessage_ip_addr_tag:
            {
                message.has_ip_addr = true;
                
                WifiSettings* wifiSettings = &state->wifiSettings.settings.wifi;
                memcpy(message.ip_addr.bytes, wifiSettings->ipAddr.v, 4);
                message.ip_addr.size = 4;
                break;
            }
            case DaqifiOutMessage_net_mask_tag:
            {
                message.has_net_mask = true;
                
                WifiSettings* wifiSettings = &state->wifiSettings.settings.wifi;
                memcpy(message.net_mask.bytes, wifiSettings->ipMask.v, 4);
                message.net_mask.size = 4;
                break;
            }
            case DaqifiOutMessage_gateway_tag:
            {
                message.has_gateway = true;
                
                WifiSettings* wifiSettings = &state->wifiSettings.settings.wifi;
                memcpy(message.gateway.bytes, wifiSettings->gateway.v, 4);
                message.gateway.size = 4;
				break;
            }
            case DaqifiOutMessage_primary_dns_tag:
            {
                message.has_primary_dns = true;
                
                WifiSettings* wifiSettings = &state->wifiSettings.settings.wifi;
                memcpy(message.primary_dns.bytes, wifiSettings->priDns.v, 4);
                message.primary_dns.size = 4;
				break;
            }
            case DaqifiOutMessage_secondary_dns_tag:
            {
                message.has_secondary_dns = false;
                
				break;
            }
             case DaqifiOutMessage_mac_addr_tag:
            {
                message.has_mac_addr = true;
                               
                WifiSettings* wifiSettings = &state->wifiSettings.settings.wifi;
                memcpy(message.mac_addr.bytes, wifiSettings->macAddr.addr, 6);
                message.mac_addr.size = 6;
                
                break;
            }
            case DaqifiOutMessage_ip_addr_v6_tag:
            {
                message.has_ip_addr_v6 = false;
           
//                WifiSettings* wifiSettings = &state->wifiSettings.settings.wifi;
//                if (wifiSettings->configFlags & TCPIP_NETWORK_CONFIG_IPV6_ADDRESS)
//                {
//                    message.has_ip_addr_v6 = true;        
//                    memcpy(message.ip_addr.bytes, wifiSettings->ipAddr.v6Add.v, 16);
//                    message.ip_addr_v6.size = 16;
//                }
                break;
            }
            case DaqifiOutMessage_sub_pre_length_v6_tag:
                message.has_sub_pre_length_v6 = false;
				break;
            case DaqifiOutMessage_gateway_v6_tag:
                message.has_gateway_v6 = false;
				break;
            case DaqifiOutMessage_primary_dns_v6_tag:
                message.has_primary_dns_v6 = false;
				break;
            case DaqifiOutMessage_secondary_dns_v6_tag:
                message.has_secondary_dns_v6 = false;
				break;
            case DaqifiOutMessage_eui_64_tag:
                message.has_eui_64 = false;
				break;
            
            case DaqifiOutMessage_host_name_tag:
            {
                //message.has_host_name = true;
                
//                WifiSettings* wifiSettings = &state->wifiSettings.settings.wifi;
//                size_t len = min(strlen(wifiSettings->hostName), TCPIP_DNS_CLIENT_MAX_HOSTNAME_LEN);
//                memcpy(message.host_name, wifiSettings->hostName, len);
//                message.host_name[len] = '\0';

                break;
            }
            case DaqifiOutMessage_device_port_tag:
            {
                message.has_device_port = true;
                
                WifiSettings* wifiSettings = &state->wifiSettings.settings.wifi;
                message.device_port = wifiSettings->tcpPort;
                
                break;
            }
            case DaqifiOutMessage_friendly_device_name_tag:
                message.has_friendly_device_name = false;
                
                //TODO:  message.friendly_device_name[32];
                break;
            case DaqifiOutMessage_ssid_tag:
            {
                message.has_ssid = true;
                
                WifiSettings* wifiSettings = &state->wifiSettings.settings.wifi;
                size_t len = min(strlen(wifiSettings->ssid), WDRV_WINC_MAX_SSID_LEN);
                memcpy(message.ssid, wifiSettings->ssid, len);
                message.ssid[len] = '\0';

                break;
            }
            case DaqifiOutMessage_wifi_security_mode_tag:
            {
                message.has_wifi_security_mode = true;
                
                WifiSettings* wifiSettings = &state->wifiSettings.settings.wifi;
                message.wifi_security_mode = wifiSettings->securityMode;
                
                break;
            }
            case DaqifiOutMessage_wifi_inf_mode_tag:
            {            
                message.has_wifi_inf_mode = false;
                //WifiSettings* wifiSettings = &state->wifiSettings.settings.wifi;
                //message.wifi_inf_mode = wifiSettings->networkType;
                break;
            }      
            case DaqifiOutMessage_av_ssid_tag:
            {
                uint8_t index;
                WifiSettings* wifiSettings = &state->wifiSettings.settings.wifi;
                message.av_ssid_count = 0;
                for(index=0;index<wifiSettings->av_num;index++)
                {
                    size_t len = min(strlen(wifiSettings->av_ssid[index]), WDRV_WINC_MAX_SSID_LEN);
                    memcpy(message.av_ssid[index], wifiSettings->av_ssid[index], len);
                    message.av_ssid[index][len] = '\0';
                    message.av_ssid_count++;
                }
                break;
            }            
            case DaqifiOutMessage_av_ssid_strength_tag:
            {
                uint8_t index;
                WifiSettings* wifiSettings = &state->wifiSettings.settings.wifi;
                message.av_ssid_strength_count = 0;
                for(index=0;index<wifiSettings->av_num;index++)
                {
                    message.av_ssid_strength[index] = wifiSettings->av_ssid_str[index];
                    message.av_ssid_strength_count++;
                }
                break;
            }                      
            case DaqifiOutMessage_av_wifi_security_mode_tag:
            {
                uint8_t index;
                WifiSettings* wifiSettings = &state->wifiSettings.settings.wifi;
                message.av_wifi_security_mode_count = 0;
                for(index=0;index<wifiSettings->av_num;index++)
                {
                    message.av_wifi_security_mode[index] = wifiSettings->av_securityMode[index];
                    message.av_wifi_security_mode_count++;
                }
                break;
            }         
            case DaqifiOutMessage_av_wifi_inf_mode_tag:
            {
                message.av_wifi_inf_mode_count = 0;
                
                break;
            }                 
            case DaqifiOutMessage_device_pn_tag:
            {
                message.has_device_pn = true;
                
                snprintf(message.device_pn, 4, "Nq%d", 1);
                break;
            }
            case DaqifiOutMessage_device_hw_rev_tag:
                message.has_device_hw_rev = true;
                memcpy(&message.device_hw_rev, "1.0.0", 5);
				break;
            case DaqifiOutMessage_device_fw_rev_tag:
                message.has_device_fw_rev = true;
                memcpy(&message.device_fw_rev, "1.1.0", 5);
                break;
            case DaqifiOutMessage_device_sn_tag:
                message.has_device_sn = true;
                message.device_sn = 200;
                break;
            default:
                // Skip unknown fields
                break;
        }
    }

    pb_ostream_t stream = pb_ostream_from_buffer(((pb_byte_t*)ppBuffer)+pBuffOffset,NANOPB_ENCODER_BUFFER_SIZE-pBuffOffset);
    
    bool status = pb_encode_delimited(                                      \
                        &stream,                                            \
                        DaqifiOutMessage_fields,                            \
                        &message);
    if (status)
    {
        pBuffOffset+=stream.bytes_written;
        return (pBuffOffset);
    }
    else
    {
#ifndef PB_NO_ERRMSG
        //LogMessage(stream.errmsg);
#else
        LogMessage("NonoPb encode error\n\r");
#endif
        return 0;
    }
}


void int2PBByteArray(   const size_t integer,                               \
                        pb_bytes_array_t* byteArray,                        \
                        size_t maxArrayLen)
{
    size_t y = 0;
    uint8_t dataByte = 0;
    byteArray->size = 0;
    for (y = 0; y < maxArrayLen; y++)
    {
        dataByte = (uint8_t) (integer >> y*8);
        if (dataByte != 0)
        {
            byteArray->bytes[y] = dataByte;
        }
        byteArray->size ++;
    }
}