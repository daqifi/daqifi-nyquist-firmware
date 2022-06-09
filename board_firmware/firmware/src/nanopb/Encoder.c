#include "pb_encode.h"
#include "pb_decode.h"

#include "Encoder.h"
#include "state/data/BoardData.h"
#include "Util/Logger.h"

#include "DaqifiOutMessage.pb.h"
#include "state/board/BoardConfig.h"

size_t Nanopb_Encode(tBoardData* state, const NanopbFlagsArray* fields, uint8_t* buffer, size_t bufferLen)
{
    // If we cannot encode a whole message, bail out
    if (bufferLen < DaqifiOutMessage_size) return 0;

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
                message.analog_in_data_count = 0;
             
                AInSample aInData;
                //AInSampleList_PeekFront(&state->AInSamples, &aInData);
                uint32_t firstTimestamp = aInData.Timestamp;
                uint32_t index = 0;
                // As long as the time stamp matches the first of the list and array has data, pull those samples
                //while (aInData.Timestamp == firstTimestamp && !AInSampleList_IsEmpty(&state->AInSamples))
                while (!AInSampleList_IsEmpty(&state->AInSamples))
                {
                    AInSampleList_PopFront(&state->AInSamples, &aInData);
                    message.analog_in_data[index++] = aInData.Value;
                    message.analog_in_data_count++;
                    // Check next value to be evaluated in the while test
                    //AInSampleList_PeekFront(&state->AInSamples, &aInData);
                    // Added to catch error when forcing data through without generating real data
					//if (message.analog_in_data_count++ > 16)
                    //{
                    //    message.analog_in_data_count = 16;
                    //}
                }
                
                break;
            case DaqifiOutMessage_analog_in_data_float_tag:
                message.analog_in_data_float_count = 0;
                break;
            case DaqifiOutMessage_analog_in_data_ts_tag:
                message.analog_in_data_ts_count = 0;
                break;
            case DaqifiOutMessage_digital_data_tag:
            {
                message.has_digital_data = false;
                DIOSample DIOdata;
                if(DIOSampleList_PopFront(&state->DIOSamples, &DIOdata))
                {
                    memcpy(message.digital_data.bytes, &DIOdata.Values, sizeof(message.digital_data.bytes));
                    message.digital_data.size = sizeof(message.digital_data.bytes);
                    message.has_digital_data = true; 
                }
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
                message.has_pwr_status = true;
                message.pwr_status = state->PowerData.powerState;
                break;
            case DaqifiOutMessage_batt_status_tag:
                message.has_batt_status = true;
                message.batt_status = state->PowerData.chargePct;
                break;
            case DaqifiOutMessage_temp_status_tag:
                message.has_temp_status = false;
                //TODO:  message.temp_status;
                break;
            case DaqifiOutMessage_timestamp_freq_tag:
                message.has_timestamp_freq = true;
                message.timestamp_freq = DRV_TMR_CounterFrequencyGet(g_BoardRuntimeConfig.StreamingConfig.TSTimerHandle);  // timestamp timer running frequency
				break;
            case DaqifiOutMessage_analog_in_port_num_tag:
            {   
                uint32_t x = 0;
                message.has_analog_in_port_num = true;
                message.analog_in_port_num = 0;
                for (x = 0; x < g_BoardConfig.AInChannels.Size; x++)
                {
                    if(g_BoardConfig.AInModules.Data[g_BoardConfig.AInChannels.Data[x].DataModule].Type == AIn_MC12bADC)    //  If channel belongs to the AIn_MC12bADC
                    {
                        if(g_BoardConfig.AInChannels.Data[x].Config.MC12b.IsPublic) message.analog_in_port_num ++;          //  and it is marked as public, count as public channel
                    }else
                    {
                        message.analog_in_port_num ++;     //  All other modules' channels are automatically public
                    }
                }            
				break;
            }
            case DaqifiOutMessage_analog_in_port_num_priv_tag:
            {   
                uint32_t x = 0;
                message.has_analog_in_port_num_priv = true;
                message.analog_in_port_num_priv = 0;
                for (x = 0; x < g_BoardConfig.AInChannels.Size; x++)
                {
                    if(g_BoardConfig.AInModules.Data[g_BoardConfig.AInChannels.Data[x].DataModule].Type == AIn_MC12bADC)    //  If channel belongs to the AIn_MC12bADC (the only type to have priv ch)
                    {
                        if(!g_BoardConfig.AInChannels.Data[x].Config.MC12b.IsPublic) message.analog_in_port_num_priv ++;         //  and it is marked as private, count as private channel
                    }
                }  
            }
            case DaqifiOutMessage_analog_in_port_type_tag:
                message.has_analog_in_port_type = false;
                break;
            case DaqifiOutMessage_analog_in_port_av_rse_tag:
                message.has_analog_in_port_av_rse = false;
                break;
            case DaqifiOutMessage_analog_in_port_rse_tag:
            {
                uint32_t x = 0;
                uint32_t data = 0;
                message.has_analog_in_port_rse = true;
                for (x = 0; x < g_BoardConfig.AInChannels.Size; x++)
                {
                    data |= (g_BoardRuntimeConfig.AInChannels.Data[x].IsDifferential << x);
                }               
                int2PBByteArray(data, (pb_bytes_array_t*)&message.analog_in_port_rse, sizeof(message.analog_in_port_rse.bytes));
				break;
            }
            case DaqifiOutMessage_analog_in_port_enabled_tag:
            {
                uint32_t x = 0;
                uint32_t data = 0;                
                message.has_analog_in_port_enabled = true;
                for (x = 0; x < g_BoardConfig.AInChannels.Size; x++)
                {
                    data |= (g_BoardRuntimeConfig.AInChannels.Data[x].IsEnabled << x);
                }     
                int2PBByteArray(data, (pb_bytes_array_t*)&message.analog_in_port_enabled, sizeof(message.analog_in_port_enabled.bytes));
				break;
            }
            case DaqifiOutMessage_analog_in_port_av_range_tag:
            {
                // TODO: Make this work for cases besides Nq1
                message.analog_in_port_av_range[0] = g_BoardRuntimeConfig.AInModules.Data[0].Range; //  Get available ranges for that module
                message.analog_in_port_av_range_count = 1;
                break;
            }

            case DaqifiOutMessage_analog_in_port_av_range_priv_tag:
                message.analog_in_port_av_range_priv_count = 0;
                break;
            case DaqifiOutMessage_analog_in_port_range_tag:
            {
                uint32_t x = 0;  
                uint32_t chan = 0;
                
                //  Loop through public channels
                for (x = 0; x < g_BoardConfig.AInChannels.Size; x++)
                {
                    if(g_BoardConfig.AInModules.Data[g_BoardConfig.AInChannels.Data[x].DataModule].Type == AIn_MC12bADC)    //  If channel belongs to the AIn_MC12bADC
                    {
                        if(g_BoardConfig.AInChannels.Data[x].Config.MC12b.IsPublic)     //  and it is marked as public, count as public channel
                        {         
                            message.analog_in_port_range[chan] = g_BoardRuntimeConfig.AInModules.Data[g_BoardConfig.AInChannels.Data[x].DataModule].Range; //  Get range for that channel
                            chan ++; 

                        }
                    }else
                    {
                        //  All other modules' channels are automatically public
                        message.analog_in_port_range[chan] = g_BoardRuntimeConfig.AInModules.Data[g_BoardConfig.AInChannels.Data[x].DataModule].Range; //  Get range for that channel
                        chan ++;
                    }
                }            
                message.analog_in_port_range_count = chan;
				break;
            }
            case DaqifiOutMessage_analog_in_port_range_priv_tag:
            {
                uint32_t x = 0;  
                uint32_t chan = 0;
                
                //  Loop through private channels
                for (x = 0; x < g_BoardConfig.AInChannels.Size; ++x)
                {
                    if(g_BoardConfig.AInModules.Data[g_BoardConfig.AInChannels.Data[x].DataModule].Type == AIn_MC12bADC)    //  If channel belongs to the AIn_MC12bADC
                    {
                        if(!g_BoardConfig.AInChannels.Data[x].Config.MC12b.IsPublic)                                        //  and it is marked as private, count as private channel
                        {
                            message.analog_in_port_range_priv[chan] = g_BoardRuntimeConfig.AInModules.Data[g_BoardConfig.AInChannels.Data[x].DataModule].Range;     //  Get range for that channel
                            chan ++;                            
                        }
                    }
                }            
                message.analog_in_port_range_priv_count = chan;
				break;
            }
            case DaqifiOutMessage_analog_in_res_tag:
                message.has_analog_in_res = true;
                switch (g_BoardConfig.BoardVariant)
                {
                    case 1:
                        message.analog_in_res = g_BoardConfig.AInModules.Data[0].Config.MC12b.Resolution;
                        break;
                    case 2:
                        message.analog_in_res = g_BoardConfig.AInModules.Data[1].Config.AD7173.Resolution;
                        break;
                    case 3:
                        message.analog_in_res = g_BoardConfig.AInModules.Data[1].Config.AD7609.Resolution;
                        break;
                }
				break;
            case DaqifiOutMessage_analog_in_res_priv_tag:
                message.has_analog_in_res_priv = true;
                message.analog_in_res_priv = g_BoardConfig.AInModules.Data[0].Config.MC12b.Resolution;
				break;
            case DaqifiOutMessage_analog_in_int_scale_m_tag:
            {
                uint32_t x = 0;  
                uint32_t chan = 0;
                
                //  Loop through public channels
                for (x = 0; x < g_BoardConfig.AInChannels.Size; x++)
                {
                    if(g_BoardConfig.AInModules.Data[g_BoardConfig.AInChannels.Data[x].DataModule].Type == AIn_MC12bADC)    //  If channel belongs to the AIn_MC12bADC (only channels with internal scale)
                    {
                        if(g_BoardConfig.AInChannels.Data[x].Config.MC12b.IsPublic)     //  and it is marked as public, count as public channel
                        {         
                            message.analog_in_int_scale_m[chan] = g_BoardConfig.AInChannels.Data[x].Config.MC12b.InternalScale; //  Get internal scale m for that channel
                            chan ++; 
                        }
                    }
                }            
                message.analog_in_int_scale_m_count = chan;
				break;
            }
            case DaqifiOutMessage_analog_in_int_scale_m_priv_tag:
            {
                uint32_t x = 0;  
                uint32_t chan = 0;
                
                //  Loop through private channels
                for (x = 0; x < g_BoardConfig.AInChannels.Size; ++x)
                {
                    if(g_BoardConfig.AInModules.Data[g_BoardConfig.AInChannels.Data[x].DataModule].Type == AIn_MC12bADC)    //  If channel belongs to the AIn_MC12bADC (only channels with internal scale)
                    {
                        if(!g_BoardConfig.AInChannels.Data[x].Config.MC12b.IsPublic)                                        //  and it is marked as private, count as private channel
                        {
                            message.analog_in_int_scale_m_priv[chan] = g_BoardConfig.AInChannels.Data[x].Config.MC12b.InternalScale; //  Get internal scale m for that channel
                            chan ++;                            
                        }
                    }
                }            
                message.analog_in_int_scale_m_priv_count = chan;
				break;
            }                      
            case DaqifiOutMessage_analog_in_cal_m_tag:
            {
                uint32_t x = 0;  
                uint32_t chan = 0;
                
                //  Loop through public channels
                for (x = 0; x < g_BoardConfig.AInChannels.Size; x++)
                {
                    if(g_BoardConfig.AInModules.Data[g_BoardConfig.AInChannels.Data[x].DataModule].Type == AIn_MC12bADC)    //  If channel belongs to the AIn_MC12bADC
                    {
                        if(g_BoardConfig.AInChannels.Data[x].Config.MC12b.IsPublic)     //  and it is marked as public, count as public channel
                        {         
                            message.analog_in_cal_m[chan] = g_BoardRuntimeConfig.AInChannels.Data[x].CalM; //  Get cal m for that channel
                            chan ++; 

                        }
                    }else
                    {
                        //  All other modules' channels are automatically public
                        message.analog_in_cal_m[chan] = g_BoardRuntimeConfig.AInChannels.Data[x].CalM; //  Get cal m for that channel
                        chan ++;
                    }
                }            
                message.analog_in_cal_m_count = chan;
				break;
            }                          
            case DaqifiOutMessage_analog_in_cal_b_tag:
            {
                uint32_t x = 0;  
                uint32_t chan = 0;
                
                //  Loop through public channels
                for (x = 0; x < g_BoardConfig.AInChannels.Size; x++)
                {
                    if(g_BoardConfig.AInModules.Data[g_BoardConfig.AInChannels.Data[x].DataModule].Type == AIn_MC12bADC)    //  If channel belongs to the AIn_MC12bADC
                    {
                        if(g_BoardConfig.AInChannels.Data[x].Config.MC12b.IsPublic)     //  and it is marked as public, count as public channel
                        {         
                            message.analog_in_cal_b[chan] = g_BoardRuntimeConfig.AInChannels.Data[x].CalB; //  Get cal b for that channel
                            chan ++; 

                        }
                    }else
                    {
                        //  All other modules' channels are automatically public
                        message.analog_in_cal_b[chan] = g_BoardRuntimeConfig.AInChannels.Data[x].CalB; //  Get cal b for that channel
                        chan ++;
                    }
                }            
                message.analog_in_cal_b_count = chan;
				break;
            }                
            case DaqifiOutMessage_analog_in_cal_m_priv_tag:
            {
                uint32_t x = 0;  
                uint32_t chan = 0;
                
                //  Loop through private channels
                for (x = 0; x < g_BoardConfig.AInChannels.Size; ++x)
                {
                    if(g_BoardConfig.AInModules.Data[g_BoardConfig.AInChannels.Data[x].DataModule].Type == AIn_MC12bADC)    //  If channel belongs to the AIn_MC12bADC
                    {
                        if(!g_BoardConfig.AInChannels.Data[x].Config.MC12b.IsPublic)                                        //  and it is marked as private, count as private channel
                        {
                            message.analog_in_cal_m_priv[chan] = g_BoardRuntimeConfig.AInChannels.Data[x].CalM;             //  Get cal m for that channel
                            chan ++;                            
                        }
                    }
                }            
                message.analog_in_cal_m_priv_count = chan;
				break;
            }                
            case DaqifiOutMessage_analog_in_cal_b_priv_tag:
            {
                uint32_t x = 0;  
                uint32_t chan = 0;
                
                //  Loop through private channels
                for (x = 0; x < g_BoardConfig.AInChannels.Size; ++x)
                {
                    if(g_BoardConfig.AInModules.Data[g_BoardConfig.AInChannels.Data[x].DataModule].Type == AIn_MC12bADC)    //  If channel belongs to the AIn_MC12bADC
                    {
                        if(!g_BoardConfig.AInChannels.Data[x].Config.MC12b.IsPublic)                                        //  and it is marked as private, count as private channel
                        {
                            message.analog_in_cal_b_priv[chan] = g_BoardRuntimeConfig.AInChannels.Data[x].CalB;             //  Get cal b for that channel
                            chan ++;                            
                        }
                    }
                }            
                message.analog_in_cal_b_priv_count = chan;
				break;
            }                   
            case DaqifiOutMessage_digital_port_num_tag:
                message.has_digital_port_num = true;
                message.digital_port_num = g_BoardConfig.DIOChannels.Size;
				break;
            case DaqifiOutMessage_digital_port_dir_tag:
            {
                uint32_t x = 0;
                uint32_t data = 0;  
                
                message.has_digital_port_dir = true;
              
                for (x = 0; x < g_BoardConfig.DIOChannels.Size; x++)
                {
                    data |= (g_BoardRuntimeConfig.DIOChannels.Data[x].IsInput << x);
                }     
                int2PBByteArray(data, (pb_bytes_array_t*)&message.digital_port_dir, sizeof(message.digital_port_dir.bytes));
				break;
            }
            case DaqifiOutMessage_analog_out_res_tag:
                message.has_analog_out_res = false;
                switch (g_BoardConfig.BoardVariant)
                {
                    case 1:
                        // No AO on the Nq1
                        break;
                    case 2:
                        message.analog_out_res = g_BoardConfig.AInModules.Data[1].Config.AD7173.Resolution;
                        break;
                    case 3:
                        message.analog_out_res = g_BoardConfig.AInModules.Data[1].Config.AD7609.Resolution;
                        break;
                }
				break;                   
            case DaqifiOutMessage_ip_addr_tag:
            {
                message.has_ip_addr = true;
                
                WifiSettings* wifiSettings = &state->wifiSettings.settings.wifi;
                memcpy(message.ip_addr.bytes, wifiSettings->ipAddr.v4Add.v, 4);
                message.ip_addr.size = 4;
                break;
            }
            case DaqifiOutMessage_net_mask_tag:
            {
                message.has_net_mask = true;
                
                WifiSettings* wifiSettings = &state->wifiSettings.settings.wifi;
                memcpy(message.net_mask.bytes, wifiSettings->ipMask.v4Add.v, 4);
                message.net_mask.size = 4;
                break;
            }
            case DaqifiOutMessage_gateway_tag:
            {
                message.has_gateway = true;
                
                WifiSettings* wifiSettings = &state->wifiSettings.settings.wifi;
                memcpy(message.gateway.bytes, wifiSettings->gateway.v4Add.v, 4);
                message.gateway.size = 4;
				break;
            }
            case DaqifiOutMessage_primary_dns_tag:
            {
                message.has_primary_dns = true;
                
                WifiSettings* wifiSettings = &state->wifiSettings.settings.wifi;
                memcpy(message.primary_dns.bytes, wifiSettings->priDns.v4Add.v, 4);
                message.primary_dns.size = 4;
				break;
            }
            case DaqifiOutMessage_secondary_dns_tag:
            {
                message.has_secondary_dns = true;
                
                WifiSettings* wifiSettings = &state->wifiSettings.settings.wifi;
                memcpy(message.secondary_dns.bytes, wifiSettings->secDns.v4Add.v, 4);
                message.secondary_dns.size = 4;
				break;
            }
             case DaqifiOutMessage_mac_addr_tag:
            {
                message.has_mac_addr = true;
                               
                WifiSettings* wifiSettings = &state->wifiSettings.settings.wifi;
                memcpy(message.mac_addr.bytes, wifiSettings->macAddr.v, 6);
                message.mac_addr.size = 6;
                
                break;
            }
            case DaqifiOutMessage_ip_addr_v6_tag:
            {
                message.has_ip_addr_v6 = false;
           
                WifiSettings* wifiSettings = &state->wifiSettings.settings.wifi;
                if (wifiSettings->configFlags & TCPIP_NETWORK_CONFIG_IPV6_ADDRESS)
                {
                    message.has_ip_addr_v6 = true;        
                    memcpy(message.ip_addr.bytes, wifiSettings->ipAddr.v6Add.v, 16);
                    message.ip_addr_v6.size = 16;
                }
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
                message.has_host_name = true;
                
                WifiSettings* wifiSettings = &state->wifiSettings.settings.wifi;
                size_t len = min(strlen(wifiSettings->hostName), TCPIP_DNS_CLIENT_MAX_HOSTNAME_LEN);
                memcpy(message.host_name, wifiSettings->hostName, len);
                message.host_name[len] = '\0';

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
                size_t len = min(strlen(wifiSettings->ssid), WDRV_MAX_SSID_LENGTH);
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
                message.has_wifi_inf_mode = true;
                WifiSettings* wifiSettings = &state->wifiSettings.settings.wifi;
                message.wifi_inf_mode = wifiSettings->networkType;
                break;
            }      
            case DaqifiOutMessage_av_ssid_tag:
            {
                uint8_t index;
                WifiSettings* wifiSettings = &state->wifiSettings.settings.wifi;
                message.av_ssid_count = 0;
                for(index=0;index<wifiSettings->av_num;index++)
                {
                    size_t len = min(strlen(wifiSettings->av_ssid[index]), WDRV_MAX_SSID_LENGTH);
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
                uint8_t index;
                WifiSettings* wifiSettings = &state->wifiSettings.settings.wifi;
                message.av_wifi_inf_mode_count = 0;
                for(index=0;index<wifiSettings->av_num;index++)
                {
                    message.av_wifi_inf_mode[index] = wifiSettings->av_networkType[index];
                    message.av_wifi_inf_mode_count++;
                }
                break;
            }                 
            case DaqifiOutMessage_device_pn_tag:
            {
                message.has_device_pn = true;
                
                snprintf(message.device_pn, 4, "Nq%d", g_BoardConfig.BoardVariant);
                break;
            }
            case DaqifiOutMessage_device_hw_rev_tag:
                message.has_device_hw_rev = true;
                memcpy(&message.device_hw_rev, &g_BoardConfig.boardHardwareRev, 16);
				break;
            case DaqifiOutMessage_device_fw_rev_tag:
                message.has_device_fw_rev = true;
                memcpy(&message.device_fw_rev, &g_BoardConfig.boardFirmwareRev, 16);
                break;
            case DaqifiOutMessage_device_sn_tag:
                message.has_device_sn = true;
                message.device_sn = g_BoardConfig.boardSerialNumber;
                break;
            default:
                // Skip unknown fields
                break;
        }
    }

    pb_ostream_t stream = pb_ostream_from_buffer(buffer, bufferLen);
    
    bool status = pb_encode_delimited(&stream, DaqifiOutMessage_fields, &message);
    if (status)
    {
        return (stream.bytes_written);
    }
    else
    {
#ifndef PB_NO_ERRMSG
        LogMessage(stream.errmsg);
#else
        LogMessage("NonoPb encode error\n\r");
#endif
        return 0;
    }
}

uint8_array Nanopb_Decode(const uint8_t* buffer, const size_t bufferLen, const NanopbFlagsArray* fields, tBoardRuntimeConfig* state)
{
    DaqifiOutMessage message = DaqifiOutMessage_init_default;
    
    pb_istream_t stream = pb_istream_from_buffer(buffer, bufferLen);
    uint8_array result = {.Size=0};
    if (!pb_decode_delimited(&stream, DaqifiOutMessage_fields, &message))
    {
        return result;
    }
            
    size_t i=0;
    for (i=0; i<fields->Size; ++i)
    {
        bool processed = false;
        switch(fields->Data[i])
        {
            case DaqifiOutMessage_msg_time_stamp_tag:
                processed = true;
                // No action
                break;
            case DaqifiOutMessage_analog_in_data_tag:
                //message.analog_in_data_count > 0;
                //TODO: message.analog_in_data[8];
                break;
            case DaqifiOutMessage_digital_data_tag:
                //message.has_digital_data;
                //TODO: message.digital_data;
                break;
            case DaqifiOutMessage_device_status_tag:
                //message.has_device_status;
                // message.device_status;
                break;
            case DaqifiOutMessage_batt_status_tag:
                //message.has_bat_level;
                //TODO: message.bat_level;
                break;
            case DaqifiOutMessage_pwr_status_tag:
                //message.has_pwr_status
                //TODO:  message.pwr_status;
                break;
            case DaqifiOutMessage_temp_status_tag:
                //message.has_temp_status
                //TODO:  message.temp_status;
                break;
            case DaqifiOutMessage_analog_out_data_tag:
                //message.analog_out_data_count > 0
                //TODO:  message.analog_out_data[8];
                break;
            case DaqifiOutMessage_ip_addr_tag:
            {
                if (!message.has_ip_addr)
                {
                    break;
                }

                WifiSettings* wifiSettings = &state->wifiSettings.settings.wifi;
                if (message.ip_addr.size == 16)
                {
                    memcpy(wifiSettings->ipAddr.v6Add.v, message.ip_addr.bytes, 16);
                    wifiSettings->configFlags |= TCPIP_NETWORK_CONFIG_IPV6_ADDRESS; // Set the V6 flag
                    processed = true;
                }
                else if (message.ip_addr.size == 4)
                {
                    memcpy(wifiSettings->ipAddr.v4Add.v, message.ip_addr.bytes, 4);
                    wifiSettings->configFlags &= ~TCPIP_NETWORK_CONFIG_IPV6_ADDRESS; // Clear the V6 flag
                    message.ip_addr.size = 4;
                    processed = true;
                }

                break;
            }
            case DaqifiOutMessage_host_name_tag:
            {
                if (!message.has_host_name)
                {
                    break;
                }
                
                WifiSettings* wifiSettings = &state->wifiSettings.settings.wifi;
                size_t len = min(strlen(message.host_name), TCPIP_DNS_CLIENT_MAX_HOSTNAME_LEN);
                if (len > 0)
                {
                    memcpy(wifiSettings->hostName, message.host_name, len);
                    wifiSettings->hostName[len] = '\0';
                    
                    processed = true;
                }
                
                break;
            }
            case DaqifiOutMessage_mac_addr_tag:
            {
                if (!message.has_mac_addr || message.mac_addr.size < 6)
                {
                    break;
                }
                
                WifiSettings* wifiSettings = &state->wifiSettings.settings.wifi;
                memcpy(wifiSettings->macAddr.v, message.mac_addr.bytes, 6);
                processed = true;
                
                break;
            }
            case DaqifiOutMessage_ssid_tag:
            {
                if (!message.has_ssid)
                {
                    break;
                }
                
                WifiSettings* wifiSettings = &state->wifiSettings.settings.wifi;
                size_t len = min(strlen(message.ssid), WDRV_MAX_SSID_LENGTH);
                if (len > 0)
                {
                    memcpy(wifiSettings->ssid, message.ssid, len);
                    message.ssid[len] = '\0';
                    processed = true;
                }

                
                break;
            }
            case DaqifiOutMessage_digital_port_dir_tag:
                message.has_digital_port_dir = false;
                //TODO:  message.digital_port_dir;
                break;
            case DaqifiOutMessage_analog_in_port_rse_tag:
                message.has_analog_in_port_rse = false;
                //TODO:  message.analog_in_port_rse;
                break;
            case DaqifiOutMessage_analog_in_port_enabled_tag:
                message.has_analog_in_port_enabled = false;
                //TODO:  message.analog_in_port_enabled;
                break;
            case DaqifiOutMessage_analog_in_port_range_tag:
                message.analog_in_port_range_count = 0;
                //TODO:  message.analog_in_port_range;
                break;
            case DaqifiOutMessage_analog_in_res_tag:
                message.has_analog_in_res = false;
                //TODO:  message.analog_in_res;
                break;
            case DaqifiOutMessage_analog_out_res_tag:
                message.has_analog_out_res = false;
                //TODO:  message.analog_out_res;
                break;
            case DaqifiOutMessage_device_pn_tag:
            {
                message.has_device_pn = true;
                //TODO:  message.device_pn[32];
                break;
            }
            case DaqifiOutMessage_device_port_tag:
            {
                if (!message.has_device_port)
                {
                    break;
                }
                
                WifiSettings* wifiSettings = &state->wifiSettings.settings.wifi;
                wifiSettings->tcpPort = message.device_port;
                processed = true;
                
                break;
            }
            case DaqifiOutMessage_wifi_security_mode_tag:
            {
                if (!message.has_wifi_security_mode)
                {
                    break;
                }
                
                WifiSettings* wifiSettings = &state->wifiSettings.settings.wifi;
                wifiSettings->securityMode = message.wifi_security_mode;
                processed = true;
                
                break;
            }
            case DaqifiOutMessage_friendly_device_name_tag:
                message.has_friendly_device_name = false;
                //TODO:  message.friendly_device_name[32];
                break;
            default:
                // Skip unknown fields
                break;
        }
        
        if (processed)
        {
            result.Data[result.Size] = fields->Data[i];
            ++result.Size;
        }
    }
    
    return result;
}

void int2PBByteArray(const size_t integer, pb_bytes_array_t* byteArray, size_t maxArrayLen)
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