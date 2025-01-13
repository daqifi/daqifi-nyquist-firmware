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
#include "HAL/TimerApi/TimerApi.h"
#include "state/board/BoardConfig.h"
#include "HAL/DIO.h"
#ifndef min
#define min(x,y) x <= y ? x : y
#endif // min

#ifndef max
#define max(x,y) x >= y ? x : y
#endif // min
//! Buffer size used for streaming purposes

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

static int Nanopb_EncodeLength(const NanopbFlagsArray* fields) {
    int i;
    int len = 0;
    DaqifiOutMessage* out;

    for (i = 0; i < fields->Size; i++) {
        switch (fields->Data[i]) {
            case DaqifiOutMessage_msg_time_stamp_tag:
                len += sizeof (out->has_msg_time_stamp);
                len += sizeof (out->msg_time_stamp);
                break;

            case DaqifiOutMessage_analog_in_data_tag:
                len += sizeof (out->analog_in_data_count);
                len += sizeof (out->analog_in_data);
                break;

            case DaqifiOutMessage_analog_in_data_float_tag:
                len += sizeof (out->analog_in_data_float_count);
                len += sizeof (out->analog_in_data_float);
                break;

            case DaqifiOutMessage_analog_in_data_ts_tag:
                len += sizeof (out->analog_in_data_ts_count);
                len += sizeof (out->analog_in_data_ts);
                break;

            case DaqifiOutMessage_digital_data_tag:
                len += sizeof (out->has_digital_data);
                len += sizeof (out->digital_data);
                break;

            case DaqifiOutMessage_digital_data_ts_tag:
                len += sizeof (out->digital_data_ts_count);
                len += sizeof (out->digital_data_ts);
                break;
            case DaqifiOutMessage_analog_out_data_tag:
                len += sizeof (out->analog_out_data_count);
                len += sizeof (out->analog_out_data);
                break;

            case DaqifiOutMessage_device_status_tag:
                len += sizeof (out->has_device_status);
                len += sizeof (out->device_status);
                break;

            case DaqifiOutMessage_pwr_status_tag:
                len += sizeof (out->has_pwr_status);
                len += sizeof (out->pwr_status);
                break;

            case DaqifiOutMessage_batt_status_tag:
                len += sizeof (out->has_batt_status);
                len += sizeof (out->batt_status);
                break;

            case DaqifiOutMessage_temp_status_tag:
                len += sizeof (out->has_temp_status);
                len += sizeof (out->temp_status);
                break;

            case DaqifiOutMessage_timestamp_freq_tag:
                len += sizeof (out->has_timestamp_freq);
                len += sizeof (out->timestamp_freq);
                break;

            case DaqifiOutMessage_analog_in_port_num_tag:
                len += sizeof (out->has_analog_in_port_num);
                len += sizeof (out->analog_in_port_num);
                break;

            case DaqifiOutMessage_analog_in_port_num_priv_tag:
                len += sizeof (out->has_analog_in_port_num_priv);
                len += sizeof (out->analog_in_port_num_priv);
                break;

            case DaqifiOutMessage_analog_in_port_type_tag:
                len += sizeof (out->has_analog_in_port_type);
                break;

            case DaqifiOutMessage_analog_in_port_av_rse_tag:
                len += sizeof (out->has_analog_in_port_av_rse);
                len += sizeof (out->analog_in_port_av_rse);
                break;

            case DaqifiOutMessage_analog_in_port_rse_tag:
                len += sizeof (out->has_analog_in_port_rse);
                len += sizeof (out->analog_in_port_rse);
                break;

            case DaqifiOutMessage_analog_in_port_enabled_tag:
                len += sizeof (out->has_analog_in_port_enabled);
                len += sizeof (out->analog_in_port_enabled);
                break;

            case DaqifiOutMessage_analog_in_port_av_range_tag:
                len += sizeof (out->analog_in_port_av_range);
                len += sizeof (out->analog_in_port_av_range_count);
                break;

            case DaqifiOutMessage_analog_in_port_av_range_priv_tag:
                len += sizeof (out->analog_in_port_av_range_priv_count);
                len += sizeof (out->analog_in_port_av_range_priv);
                break;

            case DaqifiOutMessage_analog_in_port_range_tag:
                len += sizeof (out->analog_in_port_range);
                len += sizeof (out->analog_in_port_range_count);
                break;

            case DaqifiOutMessage_analog_in_port_range_priv_tag:
                len += sizeof (out->analog_in_port_range_priv);
                len += sizeof (out->analog_in_port_range_priv_count);
                break;

            case DaqifiOutMessage_analog_in_res_tag:
                len += sizeof (out->has_analog_in_res);
                len += sizeof (out->analog_in_res);
                break;

            case DaqifiOutMessage_analog_in_res_priv_tag:
                len += sizeof (out->has_analog_in_res_priv);
                len += sizeof (out->analog_in_res_priv);
                break;

            case DaqifiOutMessage_analog_in_int_scale_m_tag:
                len += sizeof (out->analog_in_int_scale_m);
                len += sizeof (out->analog_in_int_scale_m_count);
                break;

            case DaqifiOutMessage_analog_in_int_scale_m_priv_tag:
                len += sizeof (out->analog_in_int_scale_m_priv);
                len += sizeof (out->analog_in_int_scale_m_priv_count);
                break;

            case DaqifiOutMessage_analog_in_cal_m_tag:
                len += sizeof (out->analog_in_cal_m);
                len += sizeof (out->analog_in_cal_m_count);
                break;

            case DaqifiOutMessage_analog_in_cal_b_tag:
                len += sizeof (out->analog_in_cal_b);
                len += sizeof (out->analog_in_cal_b_count);
                break;

            case DaqifiOutMessage_analog_in_cal_m_priv_tag:
                len += sizeof (out->analog_in_cal_m_priv);
                len += sizeof (out->analog_in_cal_m_priv_count);
                break;

            case DaqifiOutMessage_analog_in_cal_b_priv_tag:
                len += sizeof (out->analog_in_cal_b_priv_count);
                len += sizeof (out->analog_in_cal_b_priv);
                break;

            case DaqifiOutMessage_digital_port_num_tag:
                len += sizeof (out->has_digital_port_num);
                len += sizeof (out->digital_port_num);
                break;

            case DaqifiOutMessage_digital_port_dir_tag:
                len += sizeof (out->has_digital_port_dir);
                len += sizeof (out->digital_port_dir);
                break;

            case DaqifiOutMessage_analog_out_res_tag:
                len += sizeof (out->has_analog_out_res);
                len += sizeof (out->analog_out_res);
                break;

            case DaqifiOutMessage_ip_addr_tag:
                len += sizeof (out->has_ip_addr);
                len += sizeof (out->ip_addr);
                break;

            case DaqifiOutMessage_net_mask_tag:
                len += sizeof (out->has_net_mask);
                len += sizeof (out->net_mask);
                break;

            case DaqifiOutMessage_gateway_tag:
                len += sizeof (out->has_gateway);
                len += sizeof (out->gateway);
                break;

            case DaqifiOutMessage_primary_dns_tag:
                len += sizeof (out->has_primary_dns);
                len += sizeof (out->primary_dns);
                break;

            case DaqifiOutMessage_secondary_dns_tag:
                len += sizeof (out->has_secondary_dns);
                len += sizeof (out->secondary_dns.bytes);
                break;

            case DaqifiOutMessage_mac_addr_tag:
                len += sizeof (out->has_mac_addr);
                len += sizeof (out->mac_addr);
                break;

            case DaqifiOutMessage_ip_addr_v6_tag:
                len += sizeof (out->has_ip_addr_v6);
                len += sizeof (out->ip_addr_v6);
                break;

            case DaqifiOutMessage_sub_pre_length_v6_tag:
                len += sizeof (out->has_sub_pre_length_v6);
                len += sizeof (out->sub_pre_length_v6);
                break;

            case DaqifiOutMessage_gateway_v6_tag:
                len += sizeof (out->has_gateway_v6);
                len += sizeof (out->gateway_v6);
                break;

            case DaqifiOutMessage_primary_dns_v6_tag:
                len += sizeof (out->has_primary_dns_v6);
                len += sizeof (out->primary_dns_v6);
                break;

            case DaqifiOutMessage_secondary_dns_v6_tag:
                len += sizeof (out->has_secondary_dns_v6);
                len += sizeof (out->secondary_dns_v6);
                break;

            case DaqifiOutMessage_eui_64_tag:
                len += sizeof (out->has_eui_64);
                len += sizeof (out->eui_64);
                break;

            case DaqifiOutMessage_host_name_tag:
                len += sizeof (out->has_host_name);
                len += sizeof (out->host_name);
                break;

            case DaqifiOutMessage_device_port_tag:
                len += sizeof (out->has_device_port);
                len += sizeof (out->device_port);
                break;

            case DaqifiOutMessage_friendly_device_name_tag:
                len += sizeof (out->has_friendly_device_name);
                len += sizeof (out->friendly_device_name);
                break;

            case DaqifiOutMessage_ssid_tag:
                len += sizeof (out->has_ssid);
                len += sizeof (out->ssid);
                break;

            case DaqifiOutMessage_wifi_security_mode_tag:
                len += sizeof (out->has_wifi_security_mode);
                len += sizeof (out->wifi_security_mode);
                break;

            case DaqifiOutMessage_wifi_inf_mode_tag:
                len += sizeof (out->has_wifi_inf_mode);
                len += sizeof (out->wifi_inf_mode);
                break;

            case DaqifiOutMessage_av_ssid_tag:
                len += sizeof (out->av_ssid_count);
                len += sizeof (out->av_ssid);
                break;

            case DaqifiOutMessage_av_ssid_strength_tag:
                len += sizeof (out->av_ssid_strength_count);
                len += sizeof (out->av_ssid_strength);
                break;

            case DaqifiOutMessage_av_wifi_security_mode_tag:
                len += sizeof (out->av_wifi_security_mode_count);
                len += sizeof (out->av_wifi_security_mode);
                break;

            case DaqifiOutMessage_av_wifi_inf_mode_tag:
                len += sizeof (out->av_wifi_inf_mode_count);
                len += sizeof (out->av_wifi_inf_mode);
                break;

            case DaqifiOutMessage_device_pn_tag:
                len += sizeof (out->has_device_pn);
                len += sizeof (out->device_pn);
                break;

            case DaqifiOutMessage_device_hw_rev_tag:
                len += sizeof (out->has_device_hw_rev);
                len += sizeof (out->device_hw_rev);
                break;

            case DaqifiOutMessage_device_fw_rev_tag:
                len += sizeof (out->has_device_fw_rev);
                len += sizeof (out->device_fw_rev);
                break;

            case DaqifiOutMessage_device_sn_tag:
                len += sizeof (out->has_device_sn);
                len += sizeof (out->device_sn);
                break;

            default:
                // Skip unknown fields
                break;
        }
    }

    return len;
}

/**
 * @brief Encodes a DaqifiOutMessage into the provided buffer.
 * 
 * This helper function handles the encoding of the DaqifiOutMessage structure into
 * the provided buffer. It updates the buffer offset to track the position for
 * the next encoding operation.
 * 
 * @param message        Pointer to the DaqifiOutMessage to encode.
 * @param pBuffer        Pointer to the output buffer.
 * @param buffSize       Total size of the output buffer.
 * @param pBuffOffset    Pointer to the current buffer offset (will be updated).
 * @return true          If encoding was successful.
 * @return false         If encoding failed due to insufficient buffer space or other errors.
 */
static bool encode_message_to_buffer(DaqifiOutMessage* message, uint8_t* pBuffer, size_t buffSize, uint32_t* pBuffOffset) {
    pb_ostream_t stream = pb_ostream_from_buffer(pBuffer + *pBuffOffset, buffSize - *pBuffOffset);
    if (!pb_encode_delimited(&stream, DaqifiOutMessage_fields, message)) {
        return false;
    }
    *pBuffOffset += stream.bytes_written;
    return true;
}

size_t Nanopb_Encode(tBoardData* state,
        const NanopbFlagsArray* fields,
        uint8_t* pBuffer, size_t buffSize) {

    if (pBuffer == NULL || buffSize == 0) {
        return 0;
    }

    const tBoardConfig* pBoardConfig = BoardConfig_Get(BOARDCONFIG_ALL_CONFIG, 0);
    AInRuntimeArray* pRuntimeAInChannels = BoardRunTimeConfig_Get(BOARDRUNTIMECONFIG_AIN_CHANNELS);
    AInModRuntimeArray *pRuntimeAInModules = BoardRunTimeConfig_Get(BOARDRUNTIMECONFIG_AIN_MODULES);
    DIORuntimeArray* pRuntimeDIOChannels = BoardRunTimeConfig_Get(BOARDRUNTIMECONFIG_DIO_CHANNELS);

    DaqifiOutMessage message = DaqifiOutMessage_init_default;
    uint32_t bufferOffset = 0;
    size_t i = 0;
    if (buffSize < Nanopb_EncodeLength(fields)) {
        return 0;
    }
    if (pBuffer == NULL) {
        return 0;
    }


    for (i = 0; i < fields->Size; i++) {
        switch (fields->Data[i]) {
            case DaqifiOutMessage_msg_time_stamp_tag:
                message.has_msg_time_stamp = true;
                message.msg_time_stamp = state->StreamTrigStamp;
                break;
            case DaqifiOutMessage_analog_in_data_tag:
            {
                // Initialize the analog input data processing  

                uint32_t queueSize = AInSampleList_Size(); //takes approx 3us                
                size_t maxDataIndex = (sizeof (message.analog_in_data) / sizeof (message.analog_in_data[0])) - 1;
                AInSample data = {0};
                AInPublicSampleList_t *pPublicSampleList;
                while (queueSize > 0) {
                    // Retrieve the next sample from the queue

                    if (!AInSampleList_PopFront(&pPublicSampleList)) { //takes approx 4.2us
                        break;
                    }

                    if (pPublicSampleList == NULL)
                        break;

                    queueSize--;

                    for (int i = 0; i < MAX_AIN_PUBLIC_CHANNELS; i++) {
                        if (!pPublicSampleList->isSampleValid[i])
                            continue;
                        data = pPublicSampleList->sampleElement[i];
                        if (data.Channel > maxDataIndex)
                            continue;
                        message.analog_in_data[data.Channel] = data.Value;
                        message.analog_in_data_count++;
                    }

                    free(pPublicSampleList);

                    if (message.analog_in_data_count > 0) {
                        //time stamp of all the samples in a list should be same, so using anyone should be fine
                        message.msg_time_stamp = data.Timestamp;
                        if (!encode_message_to_buffer(&message, pBuffer, buffSize, &bufferOffset)) { //takes 248 us
                            return 0; // Return 0 if encoding fails
                        }

                        // Check if buffer has enough space for another message
                        if (bufferOffset + Nanopb_EncodeLength(fields) > buffSize) {
                            return bufferOffset; // Stop if the buffer is full
                        }

                    }
                    message.analog_in_data_count = 0;
                    message.has_msg_time_stamp = true;
                }

                break;
            }

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

                if (DIOSampleList_PopFront(&state->DIOSamples, &DIOdata)) {
                    memcpy(
                            message.digital_data.bytes,
                            &DIOdata.Values,
                            sizeof (message.digital_data.bytes));
                    message.digital_data.size =
                            sizeof (message.digital_data.bytes);
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
            {
                //01 : USB connected
                //02 : wifi connected
                //04 : TCP Client Connected
                volatile uint32_t flag = 0;
                if (UsbCdc_GetSettings()->isCdcHostConnected == 0) {//usb com not connected 
                    flag &= ~0x00000001;
                } else { //usb com connected
                    flag |= 0x00000001;
                }
                wifi_tcp_server_context_t *wifiTcpCntxt = wifi_manager_GetTcpServerContext();
                if (wifiTcpCntxt != NULL) {
                    if (wifiTcpCntxt->serverSocket == -1) {//server socket is -1 WiFi is not connected
                        flag &= ~0x00000002;
                    } else {//server socket is non negative if WiFi is not connected
                        flag |= 0x00000002;
                    }
                    if (wifiTcpCntxt->client.clientSocket == -1) { //no tcp client is connected
                        flag &= ~0x00000004;
                    } else { //tcp client connected
                        flag |= 0x00000004;
                    }
                }
                message.has_device_status = true;
                message.device_status = flag;
            }
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
                //message.has_temp_status = false;
                //TODO:  message.temp_status;
                break;
            case DaqifiOutMessage_timestamp_freq_tag:
                message.has_timestamp_freq = true;
                // timestamp timer running frequency
                message.timestamp_freq = TimerApi_CounterGet(
                        pBoardConfig->StreamingConfig.TSTimerIndex);
                break;
            case DaqifiOutMessage_analog_in_port_num_tag:
            {
                /**
                 * @brief Counts public analog input channels for the MC12bADC module.
                 *
                 * This tag counts the number of public analog input channels and stores 
                 * the result in `analog_in_port_num`. All channels from other module types 
                 * are counted as public.
                 */
                message.has_analog_in_port_num = true;
                message.analog_in_port_num = 0;

                if (pBoardConfig == NULL || pBoardConfig->AInChannels.Size == 0 || pBoardConfig->AInModules.Size == 0) {
                    break;
                }

                for (uint32_t x = 0; x < pBoardConfig->AInChannels.Size; x++) {
                    if (pBoardConfig->AInChannels.Data[x].Type == AIn_MC12bADC) {
                        if (pBoardConfig->AInChannels.Data[x].Config.MC12b.IsPublic) {
                            message.analog_in_port_num++;
                        }
                    } else {
                        message.analog_in_port_num++;
                    }
                }

                break;
            }

            case DaqifiOutMessage_analog_in_port_num_priv_tag:
            {
                /**
                 * @brief Counts private analog input channels for the MC12bADC module.
                 *
                 * This tag counts the number of private (non-public) analog input channels
                 * in the `MC12bADC` module and stores the result in `analog_in_port_num_priv`.
                 * Only channels marked as private (`IsPublic == false`) are counted. Channels 
                 * from other module types are ignored.
                 */
                message.has_analog_in_port_num_priv = true;
                message.analog_in_port_num_priv = 0;

                if (pBoardConfig == NULL || pBoardConfig->AInChannels.Size == 0 || pBoardConfig->AInModules.Size == 0) {
                    break;
                }

                for (uint32_t x = 0; x < pBoardConfig->AInChannels.Size; x++) {
                    if (pBoardConfig->AInChannels.Data[x].Type == AIn_MC12bADC) {
                        if (!pBoardConfig->AInChannels.Data[x].Config.MC12b.IsPublic) {
                            message.analog_in_port_num_priv++;
                        }
                    }
                }

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
                /**
                 * @brief Encodes the analog input channels as single-ended (RSE) or differential.
                 *
                 * Each bit in `analog_in_port_rse` represents whether the corresponding analog input
                 * channel is configured as differential or single-ended:
                 * - Bit 1 (set) = Differential
                 * - Bit 0 (clear) = Single-ended (RSE)
                 */
                uint32_t x = 0;
                uint32_t data = 0;
                message.has_analog_in_port_rse = true;
                for (x = 0; x < pBoardConfig->AInChannels.Size; x++) {
                    data |=
                            (pRuntimeAInChannels->Data[x].IsDifferential << x);
                }
                int2PBByteArray(
                        data,
                        (pb_bytes_array_t*) & message.analog_in_port_rse,
                        sizeof (message.analog_in_port_rse.bytes));
                break;
            }
            case DaqifiOutMessage_analog_in_port_enabled_tag:
            {
                /**
                 * @brief Encodes the enabled state of analog input channels.
                 *
                 * This tag sets a bit for each channel in `analog_in_port_enabled`, where
                 * each bit indicates whether the corresponding channel is enabled (1) or 
                 * disabled (0).
                 */
                uint32_t x = 0;
                uint32_t data = 0;
                message.has_analog_in_port_enabled = true;
                for (x = 0; x < pBoardConfig->AInChannels.Size; x++) {
                    data |=
                            (pRuntimeAInChannels->Data[x].IsEnabled << x);
                }
                int2PBByteArray(
                        data,
                        (pb_bytes_array_t*) & message.analog_in_port_enabled,
                        sizeof (message.analog_in_port_enabled.bytes));
                break;
            }
            case DaqifiOutMessage_analog_in_port_av_range_tag:
            {
                /**
                 * @brief Encodes the available range settings for analog input modules.
                 *
                 * This tag stores the supported voltage ranges for each analog input module,
                 * indicating the possible ranges a module can operate within (e.g., 0-5V, ±10V).
                 */
                message.analog_in_port_av_range[0] =
                        pRuntimeAInModules->Data[AIn_MC12bADC].Range;
                message.analog_in_port_av_range_count = 1;
                message.analog_in_port_range_count = 0;
                break;
            }

            case DaqifiOutMessage_analog_in_port_av_range_priv_tag:
                message.analog_in_port_av_range_priv_count = 0;
                break;
            case DaqifiOutMessage_analog_in_port_range_tag:
            {
                /**
                 * @brief Encodes the range values for public analog input channels.
                 *
                 * This tag stores the input voltage ranges for each public analog input channel in
                 * `analog_in_port_range`. Channels from the `AIn_MC12bADC` module are checked for 
                 * public status, while channels from other module types are automatically considered public.
                 */
                uint32_t chan = 0;
                const uint32_t max_range_count = sizeof (message.analog_in_port_range) / sizeof (message.analog_in_port_range[0]);

                for (uint32_t x = 0; x < pBoardConfig->AInChannels.Size; x++) {
                    if (pBoardConfig->AInChannels.Data[x].Type == AIn_MC12bADC &&
                            pBoardConfig->AInChannels.Data[x].Config.MC12b.IsPublic) {

                        if (chan < max_range_count) {
                            message.analog_in_port_range[chan++] = pRuntimeAInModules->Data[AIn_MC12bADC].Range;
                        }
                    } else if (chan < max_range_count) {
                        message.analog_in_port_range[chan++] = pRuntimeAInModules->Data[AIn_MC12bADC].Range;
                    }
                }

                message.analog_in_port_range_count = chan;
                break;

            }
            case DaqifiOutMessage_analog_in_port_range_priv_tag:
            {
                /**
                 * @brief Encodes the current range settings for private analog input channels.
                 *
                 * This tag stores the actual, active range configuration for each private analog input channel,
                 * indicating the voltage range currently being used by private channels.
                 */
                uint32_t chan = 0;
                const uint32_t max_range_count = sizeof (message.analog_in_port_range_priv) / sizeof (message.analog_in_port_range_priv[0]);

                for (uint32_t x = 0; x < pBoardConfig->AInChannels.Size; x++) {
                    if (pBoardConfig->AInChannels.Data[x].Type == AIn_MC12bADC &&
                            !pBoardConfig->AInChannels.Data[x].Config.MC12b.IsPublic) {
                        if (chan < max_range_count) {
                            message.analog_in_port_range_priv[chan++] = pRuntimeAInModules->Data[AIn_MC12bADC].Range;
                        }
                    }
                }

                message.analog_in_port_range_priv_count = chan;
                break;
            }
            case DaqifiOutMessage_analog_in_res_tag:
            {
                /**
                 * @brief Encodes the resolution of the analog input channels.
                 *
                 * This tag stores the resolution (in bits) of the analog input channels,
                 * based on the ADC configuration for the active board variant.
                 */
                message.has_analog_in_res = true;

                switch (pBoardConfig->BoardVariant) {
                    case 1:
                        message.analog_in_res = pBoardConfig->AInModules.Data[AIn_MC12bADC].Config.MC12b.Resolution;
                        break;
                    case 2:
                        //message.analog_in_res = pBoardConfig->AInModules.Data[1].Config.AD7173.Resolution;
                        break;
                    case 3:
                        //message.analog_in_res = pBoardConfig->AInModules.Data[1].Config.AD7609.Resolution;
                        break;
                    default:
                        message.has_analog_in_res = false;
                        break;
                }

                break;
            }
            case DaqifiOutMessage_analog_in_res_priv_tag:
            {
                /**
                 * @brief Encodes the resolution of private analog input channels.
                 *
                 * This tag stores the resolution (in bits) of private analog input channels,
                 * based on the ADC configuration for the MC12b module.
                 */
                message.has_analog_in_res_priv = true;

                message.analog_in_res_priv = pBoardConfig->AInModules.Data[AIn_MC12bADC].Config.MC12b.Resolution;

                break;
            }
            case DaqifiOutMessage_analog_in_int_scale_m_tag:
            {
                /**
                 * @brief Encodes the internal scale factor (m) for public analog input channels.
                 *
                 * This tag stores the internal scaling factor (m) for each public analog input channel
                 * in the MC12b module.
                 */
                message.analog_in_int_scale_m_count = 0;

                for (uint32_t x = 0; x < pBoardConfig->AInChannels.Size; x++) {
                    if (pBoardConfig->AInModules.Data[pBoardConfig->AInChannels.Data[x].Type].Type == AIn_MC12bADC &&
                            pBoardConfig->AInChannels.Data[x].Config.MC12b.IsPublic) {

                        if (message.analog_in_int_scale_m_count < sizeof (message.analog_in_int_scale_m) / sizeof (message.analog_in_int_scale_m[0])) {
                            message.analog_in_int_scale_m[message.analog_in_int_scale_m_count++] = pBoardConfig->AInChannels.Data[x].Config.MC12b.InternalScale;
                        }
                    }
                }

                break;
            }
            case DaqifiOutMessage_analog_in_int_scale_m_priv_tag:

            {
                /**
                 * @brief Encodes the internal scale factor (m) for private analog input channels.
                 *
                 * This tag stores the internal scaling factor (m) for each private analog input channel
                 * in the MC12b module.
                 */
                message.analog_in_int_scale_m_priv_count = 0;

                for (uint32_t x = 0; x < pBoardConfig->AInChannels.Size; x++) {
                    if (pBoardConfig->AInModules.Data[pBoardConfig->AInChannels.Data[x].Type].Type == AIn_MC12bADC &&
                            !pBoardConfig->AInChannels.Data[x].Config.MC12b.IsPublic) {

                        if (message.analog_in_int_scale_m_priv_count < sizeof (message.analog_in_int_scale_m_priv) / sizeof (message.analog_in_int_scale_m_priv[0])) {
                            message.analog_in_int_scale_m_priv[message.analog_in_int_scale_m_priv_count++] = pBoardConfig->AInChannels.Data[x].Config.MC12b.InternalScale;
                        }
                    }
                }

                break;
            }
            case DaqifiOutMessage_analog_in_cal_m_tag:

            {
                /**
                 * @brief Encodes the calibration factor (m) for public analog input channels.
                 *
                 * This tag stores the calibration factor (m) for each public analog input channel
                 * across all modules.
                 */
                message.analog_in_cal_m_count = 0;

                for (uint32_t x = 0; x < pBoardConfig->AInChannels.Size; x++) {
                    if (pBoardConfig->AInModules.Data[pBoardConfig->AInChannels.Data[x].Type].Type == AIn_MC12bADC &&
                            pBoardConfig->AInChannels.Data[x].Config.MC12b.IsPublic) {

                        if (message.analog_in_cal_m_count < sizeof (message.analog_in_cal_m) / sizeof (message.analog_in_cal_m[0])) {
                            message.analog_in_cal_m[message.analog_in_cal_m_count++] = pRuntimeAInChannels->Data[x].CalM;
                        }
                    } else {
                        if (message.analog_in_cal_m_count < sizeof (message.analog_in_cal_m) / sizeof (message.analog_in_cal_m[0])) {
                            message.analog_in_cal_m[message.analog_in_cal_m_count++] = pRuntimeAInChannels->Data[x].CalM;
                        }
                    }
                }

                break;
            }
            case DaqifiOutMessage_analog_in_cal_b_tag:
            {
                /**
                 * @brief Encodes the calibration factor (b) for public analog input channels.
                 *
                 * This tag stores the calibration factor (b) for each public analog input channel
                 * across all modules.
                 */
                message.analog_in_cal_b_count = 0;

                for (uint32_t x = 0; x < pBoardConfig->AInChannels.Size; x++) {
                    if (pBoardConfig->AInModules.Data[pBoardConfig->AInChannels.Data[x].Type].Type == AIn_MC12bADC &&
                            pBoardConfig->AInChannels.Data[x].Config.MC12b.IsPublic) {

                        if (message.analog_in_cal_b_count < sizeof (message.analog_in_cal_b) / sizeof (message.analog_in_cal_b[0])) {
                            message.analog_in_cal_b[message.analog_in_cal_b_count++] = pRuntimeAInChannels->Data[x].CalB;
                        }
                    } else {
                        if (message.analog_in_cal_b_count < sizeof (message.analog_in_cal_b) / sizeof (message.analog_in_cal_b[0])) {
                            message.analog_in_cal_b[message.analog_in_cal_b_count++] = pRuntimeAInChannels->Data[x].CalB;
                        }
                    }
                }

                break;
            }
            case DaqifiOutMessage_analog_in_cal_m_priv_tag:
            {
                /**
                 * @brief Encodes the calibration factor (m) for private analog input channels.
                 *
                 * This tag stores the calibration factor (m) for each private analog input channel
                 * in the MC12b module.
                 */
                message.analog_in_cal_m_priv_count = 0;

                for (uint32_t x = 0; x < pBoardConfig->AInChannels.Size; x++) {
                    if (pBoardConfig->AInModules.Data[pBoardConfig->AInChannels.Data[x].Type].Type == AIn_MC12bADC &&
                            !pBoardConfig->AInChannels.Data[x].Config.MC12b.IsPublic) {

                        if (message.analog_in_cal_m_priv_count < sizeof (message.analog_in_cal_m_priv) / sizeof (message.analog_in_cal_m_priv[0])) {
                            message.analog_in_cal_m_priv[message.analog_in_cal_m_priv_count++] = pRuntimeAInChannels->Data[x].CalM;
                        }
                    }
                }

                break;
            }
            case DaqifiOutMessage_analog_in_cal_b_priv_tag:
            {
                /**
                 * @brief Encodes the calibration factor (b) for private analog input channels.
                 *
                 * This tag stores the calibration factor (b) for each private analog input channel
                 * in the MC12b module.
                 */
                message.analog_in_cal_b_priv_count = 0;

                for (uint32_t x = 0; x < pBoardConfig->AInChannels.Size; x++) {
                    if (pBoardConfig->AInModules.Data[pBoardConfig->AInChannels.Data[x].Type].Type == AIn_MC12bADC &&
                            !pBoardConfig->AInChannels.Data[x].Config.MC12b.IsPublic) {

                        if (message.analog_in_cal_b_priv_count < sizeof (message.analog_in_cal_b_priv) / sizeof (message.analog_in_cal_b_priv[0])) {
                            message.analog_in_cal_b_priv[message.analog_in_cal_b_priv_count++] = pRuntimeAInChannels->Data[x].CalB;
                        }
                    }
                }

                break;
            }
            case DaqifiOutMessage_digital_port_num_tag:
            {
                /**
                 * @brief Encodes the number of digital ports.
                 *
                 * This tag stores the total number of digital input/output ports available on the device.
                 */
                message.has_digital_port_num = true;
                message.digital_port_num = pBoardConfig->DIOChannels.Size;

                break;
            }
            case DaqifiOutMessage_digital_port_dir_tag:
            {
                /**
                 * @brief Encodes the direction of digital ports.
                 *
                 * This tag stores the direction (input or output) for each digital port, where each bit
                 * represents the direction of a specific digital port.
                 */
                message.has_digital_port_dir = true;
                uint32_t data = 0;

                for (uint32_t x = 0; x < pBoardConfig->DIOChannels.Size; x++) {
                    data |= (pRuntimeDIOChannels->Data[x].IsInput << x);
                }

                int2PBByteArray(data, (pb_bytes_array_t*) & message.digital_port_dir, sizeof (message.digital_port_dir.bytes));

                break;
            }
            case DaqifiOutMessage_analog_out_res_tag:
            {
                /**
                 * @brief Encodes the resolution of analog output channels.
                 *
                 * This tag stores the resolution (in bits) for the analog output channels based
                 * on the DAC configuration for the active board variant.
                 */
                message.has_analog_out_res = true;

                switch (pBoardConfig->BoardVariant) {
                    case 1:
                        // No analog output on board variant 1
                        message.has_analog_out_res = false;
                        break;
                    case 2:
                        //message.analog_out_res = pBoardConfig->AInModules.Data[1].Config.AD7173.Resolution;
                        break;
                    case 3:
                        //message.analog_out_res = pBoardConfig->AInModules.Data[1].Config.AD7609.Resolution;
                        break;
                    default:
                        message.has_analog_out_res = false;
                        break;
                }

                break;
            }

            case DaqifiOutMessage_ip_addr_tag:
            {
                message.has_ip_addr = true;

                wifi_manager_settings_t* wifiSettings = &state->wifiSettings;
                memcpy(message.ip_addr.bytes, wifiSettings->ipAddr.v, 4);
                message.ip_addr.size = 4;
                break;
            }
            case DaqifiOutMessage_net_mask_tag:
            {
                message.has_net_mask = true;

                wifi_manager_settings_t* wifiSettings = &state->wifiSettings;
                memcpy(message.net_mask.bytes, wifiSettings->ipMask.v, 4);
                message.net_mask.size = 4;
                break;
            }
            case DaqifiOutMessage_gateway_tag:
            {
                message.has_gateway = true;

                wifi_manager_settings_t* wifiSettings = &state->wifiSettings;
                memcpy(message.gateway.bytes, wifiSettings->gateway.v, 4);
                message.gateway.size = 4;
                break;
            }
            case DaqifiOutMessage_primary_dns_tag:
            {
                message.has_primary_dns = false;

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

                wifi_manager_settings_t* wifiSettings = &state->wifiSettings;
                memcpy(message.mac_addr.bytes, wifiSettings->macAddr.addr, 6);
                message.mac_addr.size = 6;

                break;
            }
            case DaqifiOutMessage_ip_addr_v6_tag:
            {
                message.has_ip_addr_v6 = false;
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

                wifi_manager_settings_t* wifiSettings = &state->wifiSettings;
                size_t len = min(strlen(wifiSettings->hostName), WIFI_MANAGER_DNS_CLIENT_MAX_HOSTNAME_LEN);
                memcpy(message.host_name, wifiSettings->hostName, len);
                message.host_name[len] = '\0';
                break;
            }
            case DaqifiOutMessage_device_port_tag:
            {
                message.has_device_port = true;

                wifi_manager_settings_t* wifiSettings = &state->wifiSettings;
                message.device_port = wifiSettings->tcpPort;

                break;
            }
            case DaqifiOutMessage_friendly_device_name_tag:
                message.has_friendly_device_name = false;
                break;
            case DaqifiOutMessage_ssid_tag:
            {
                message.has_ssid = true;

                wifi_manager_settings_t* wifiSettings = &state->wifiSettings;
                size_t len = min(strlen(wifiSettings->ssid), WDRV_WINC_MAX_SSID_LEN);
                memcpy(message.ssid, wifiSettings->ssid, len);
                message.ssid[len] = '\0';

                break;
            }
            case DaqifiOutMessage_wifi_security_mode_tag:
            {
                message.has_wifi_security_mode = true;

                wifi_manager_settings_t* wifiSettings = &state->wifiSettings;
                message.wifi_security_mode = wifiSettings->securityMode;

                break;
            }
            case DaqifiOutMessage_wifi_inf_mode_tag:
            {
                message.has_wifi_inf_mode = true;
                wifi_manager_settings_t* wifiSettings = &state->wifiSettings;
                message.wifi_inf_mode = wifiSettings->networkMode;
                break;
            }
            case DaqifiOutMessage_av_ssid_tag:
            {
                //                uint8_t index;
                //                WifiSettings* wifiSettings = &state->wifiSettings;
                message.av_ssid_count = 0;
                //                for(index=0;index<wifiSettings->av_num;index++)
                //                {
                //                    size_t len = min(strlen(wifiSettings->av_ssid[index]), WDRV_WINC_MAX_SSID_LEN);
                //                    memcpy(message.av_ssid[index], wifiSettings->av_ssid[index], len);
                //                    message.av_ssid[index][len] = '\0';
                //                    message.av_ssid_count++;
                //                }
                break;
            }
            case DaqifiOutMessage_av_ssid_strength_tag:
            {
                //                uint8_t index;
                //                WifiSettings* wifiSettings = &state->wifiSettings;
                message.av_ssid_strength_count = 0;
                //                for(index=0;index<wifiSettings->av_num;index++)
                //                {
                //                    message.av_ssid_strength[index] = wifiSettings->av_ssid_str[index];
                //                    message.av_ssid_strength_count++;
                //                }
                break;
            }
            case DaqifiOutMessage_av_wifi_security_mode_tag:
            {
                //                uint8_t index;
                //                WifiSettings* wifiSettings = &state->wifiSettings;
                message.av_wifi_security_mode_count = 0;
                //                for(index=0;index<wifiSettings->av_num;index++)
                //                {
                //                    message.av_wifi_security_mode[index] = wifiSettings->av_securityMode[index];
                //                    message.av_wifi_security_mode_count++;
                //                }
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
                snprintf(message.device_pn, 6, "Nq%d", pBoardConfig->BoardVariant);
                break;
            }
            case DaqifiOutMessage_device_hw_rev_tag:
                message.has_device_hw_rev = true;
                memcpy(&message.device_hw_rev,
                        pBoardConfig->boardHardwareRev,
                        strlen(pBoardConfig->boardHardwareRev));
                break;
            case DaqifiOutMessage_device_fw_rev_tag:
                message.has_device_fw_rev = true;
                memcpy(&message.device_fw_rev,
                        pBoardConfig->boardFirmwareRev,
                        strlen(pBoardConfig->boardFirmwareRev));
                break;
            case DaqifiOutMessage_device_sn_tag:
                message.has_device_sn = true;
                message.device_sn = 0x7E28CC7C62010277; //TODO(Daqifi):Change this properly
                break;
            default:
                // Skip unknown fields
                break;
        }
    }
    if (encode_message_to_buffer(&message, pBuffer, buffSize, &bufferOffset)) {
        return bufferOffset;
    } else {
        return 0;
    }
}

void int2PBByteArray(const size_t integer,
        pb_bytes_array_t* byteArray,
        size_t maxArrayLen) {
    size_t y = 0;
    uint8_t dataByte = 0;
    byteArray->size = 0;
    for (y = 0; y < maxArrayLen; y++) {
        dataByte = (uint8_t) (integer >> y * 8);
        if (dataByte != 0) {
            byteArray->bytes[y] = dataByte;
        }
        byteArray->size++;
    }
}