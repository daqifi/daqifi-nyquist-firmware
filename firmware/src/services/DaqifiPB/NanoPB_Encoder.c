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
#define min(x,y) ((x) <= (y) ? (x) : (y))
#endif // min

#ifndef max
#define max(x,y) ((x) >= (y) ? (x) : (y))
#endif // max
//! Buffer size used for streaming purposes

//! Maximum batched AIN values: 16 channels × 50 queued sample sets
#define MAX_BATCH_VALUES (MAX_AIN_PUBLIC_CHANNELS * 50)

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
                len += sizeof (out->device_status);
                break;

            case DaqifiOutMessage_pwr_status_tag:
                len += sizeof (out->pwr_status);
                break;

            case DaqifiOutMessage_batt_status_tag:
                len += sizeof (out->batt_status);
                break;

            case DaqifiOutMessage_temp_status_tag:
                len += sizeof (out->temp_status);
                break;

            case DaqifiOutMessage_timestamp_freq_tag:
                len += sizeof (out->timestamp_freq);
                break;

            case DaqifiOutMessage_analog_in_port_num_tag:
                len += sizeof (out->analog_in_port_num);
                break;

            case DaqifiOutMessage_analog_in_port_num_priv_tag:
                len += sizeof (out->analog_in_port_num_priv);
                break;

            case DaqifiOutMessage_analog_in_port_type_tag:
                break;

            case DaqifiOutMessage_analog_in_port_av_rse_tag:
                len += sizeof (out->analog_in_port_av_rse);
                break;

            case DaqifiOutMessage_analog_in_port_rse_tag:
                len += sizeof (out->analog_in_port_rse);
                break;

            case DaqifiOutMessage_analog_in_port_enabled_tag:
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
                len += sizeof (out->analog_in_res);
                break;

            case DaqifiOutMessage_analog_in_res_priv_tag:
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
                len += sizeof (out->digital_port_num);
                break;

            case DaqifiOutMessage_digital_port_dir_tag:
                len += sizeof (out->digital_port_dir);
                break;

            case DaqifiOutMessage_analog_out_res_tag:
                len += sizeof (out->analog_out_res);
                break;

            case DaqifiOutMessage_ip_addr_tag:
                len += sizeof (out->ip_addr);
                break;

            case DaqifiOutMessage_net_mask_tag:
                len += sizeof (out->net_mask);
                break;

            case DaqifiOutMessage_gateway_tag:
                len += sizeof (out->gateway);
                break;

            case DaqifiOutMessage_primary_dns_tag:
                len += sizeof (out->primary_dns);
                break;

            case DaqifiOutMessage_secondary_dns_tag:
                len += sizeof (out->secondary_dns.bytes);
                break;

            case DaqifiOutMessage_mac_addr_tag:
                len += sizeof (out->mac_addr);
                break;

            case DaqifiOutMessage_ip_addr_v6_tag:
                len += sizeof (out->ip_addr_v6);
                break;

            case DaqifiOutMessage_sub_pre_length_v6_tag:
                len += sizeof (out->sub_pre_length_v6);
                break;

            case DaqifiOutMessage_gateway_v6_tag:
                len += sizeof (out->gateway_v6);
                break;

            case DaqifiOutMessage_primary_dns_v6_tag:
                len += sizeof (out->primary_dns_v6);
                break;

            case DaqifiOutMessage_secondary_dns_v6_tag:
                len += sizeof (out->secondary_dns_v6);
                break;

            case DaqifiOutMessage_eui_64_tag:
                len += sizeof (out->eui_64);
                break;

            case DaqifiOutMessage_host_name_tag:
                len += sizeof (out->host_name);
                break;

            case DaqifiOutMessage_device_port_tag:
                len += sizeof (out->device_port);
                break;

            case DaqifiOutMessage_friendly_device_name_tag:
                len += sizeof (out->friendly_device_name);
                break;

            case DaqifiOutMessage_ssid_tag:
                len += sizeof (out->ssid);
                break;

            case DaqifiOutMessage_wifi_security_mode_tag:
                len += sizeof (out->wifi_security_mode);
                break;

            case DaqifiOutMessage_wifi_inf_mode_tag:
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
                len += sizeof (out->device_pn);
                break;

            case DaqifiOutMessage_device_hw_rev_tag:
                len += sizeof (out->device_hw_rev);
                break;

            case DaqifiOutMessage_device_fw_rev_tag:
                len += sizeof (out->device_fw_rev);
                break;

            case DaqifiOutMessage_device_sn_tag:
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
                        if (message.analog_in_data_count > maxDataIndex)
                            break;  // Array full
                        // Use sequential packing
                        message.analog_in_data[message.analog_in_data_count] = data.Value;
                        message.analog_in_data_count++;
                    }

                     AInSampleList_FreeToPool(pPublicSampleList);  // Use pool instead of vPortFree

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
                DIOSample DIOdata;

                if (DIOSampleList_PopFront(&state->DIOSamples, &DIOdata)) {
                    memcpy(
                            message.digital_data.bytes,
                            &DIOdata.Values,
                            sizeof (message.digital_data.bytes));
                    message.digital_data.size =
                            sizeof (message.digital_data.bytes);
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
                message.device_status = flag;
            }
                break;
            case DaqifiOutMessage_pwr_status_tag:
                message.pwr_status = state->PowerData.powerState;
                break;
            case DaqifiOutMessage_batt_status_tag:
                message.batt_status = state->PowerData.chargePct;
                break;
            case DaqifiOutMessage_temp_status_tag:
                //TODO:  message.temp_status;
                break;
            case DaqifiOutMessage_timestamp_freq_tag:
                // timestamp timer running frequency
                message.timestamp_freq = TimerApi_FrequencyGet(
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
                message.analog_in_port_num = 0;

                if (pBoardConfig == NULL || pBoardConfig->AInChannels.Size == 0 || pBoardConfig->AInModules.Size == 0) {
                    break;
                }

                for (uint32_t x = 0; x < pBoardConfig->AInChannels.Size; x++) {
                    if (AInChannel_IsPublic(&pBoardConfig->AInChannels.Data[x])) {
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
                message.analog_in_port_num_priv = 0;

                if (pBoardConfig == NULL || pBoardConfig->AInChannels.Size == 0 || pBoardConfig->AInModules.Size == 0) {
                    break;
                }

                for (uint32_t x = 0; x < pBoardConfig->AInChannels.Size; x++) {
                    if (!AInChannel_IsPublic(&pBoardConfig->AInChannels.Data[x])) {
                        message.analog_in_port_num_priv++;
                    }
                }

                break;
            }
            case DaqifiOutMessage_analog_in_port_type_tag:
                break;
            case DaqifiOutMessage_analog_in_port_av_rse_tag:
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
                 * indicating the possible ranges a module can operate within (e.g., 0-5V, +/-10V).
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
                    if (AInChannel_IsPublic(&pBoardConfig->AInChannels.Data[x]) && chan < max_range_count) {
                        // Get range from appropriate module
                        if (pBoardConfig->AInChannels.Data[x].Type == AIn_MC12bADC) {
                            message.analog_in_port_range[chan++] = pRuntimeAInModules->Data[AIn_MC12bADC].Range;
                        } else if (pBoardConfig->AInChannels.Data[x].Type == AIn_AD7609) {
                            message.analog_in_port_range[chan++] = pRuntimeAInModules->Data[AIn_AD7609].Range;
                        }
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
                    if (!AInChannel_IsPublic(&pBoardConfig->AInChannels.Data[x]) && chan < max_range_count) {
                        // Get range from appropriate module
                        if (pBoardConfig->AInChannels.Data[x].Type == AIn_MC12bADC) {
                            message.analog_in_port_range_priv[chan++] = pRuntimeAInModules->Data[AIn_MC12bADC].Range;
                        } else if (pBoardConfig->AInChannels.Data[x].Type == AIn_AD7609) {
                            message.analog_in_port_range_priv[chan++] = pRuntimeAInModules->Data[AIn_AD7609].Range;
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
                    // InternalScale is MC12b-specific, only encode for public MC12b channels
                    if (pBoardConfig->AInChannels.Data[x].Type == AIn_MC12bADC &&
                            AInChannel_IsPublic(&pBoardConfig->AInChannels.Data[x])) {

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
                    // InternalScale is MC12b-specific, only encode for private MC12b channels
                    if (pBoardConfig->AInChannels.Data[x].Type == AIn_MC12bADC &&
                            !AInChannel_IsPublic(&pBoardConfig->AInChannels.Data[x])) {

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
                    // CalM applies to all public channels regardless of type
                    if (AInChannel_IsPublic(&pBoardConfig->AInChannels.Data[x])) {
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
                    // CalB applies to all public channels regardless of type
                    if (AInChannel_IsPublic(&pBoardConfig->AInChannels.Data[x])) {
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
                    // CalM applies to all private channels regardless of type
                    if (!AInChannel_IsPublic(&pBoardConfig->AInChannels.Data[x])) {
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
                    // CalB applies to all private channels regardless of type
                    if (!AInChannel_IsPublic(&pBoardConfig->AInChannels.Data[x])) {
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

                switch (pBoardConfig->BoardVariant) {
                    case 1:
                        // No analog output on board variant 1
                        break;
                    case 2:
                        //message.analog_out_res = pBoardConfig->AInModules.Data[1].Config.AD7173.Resolution;
                        break;
                    case 3:
                        //message.analog_out_res = pBoardConfig->AInModules.Data[1].Config.AD7609.Resolution;
                        break;
                    default:
                        break;
                }

                break;
            }

            case DaqifiOutMessage_ip_addr_tag:
            {
                wifi_manager_settings_t* wifiSettings = &state->wifiSettings;
                memcpy(message.ip_addr.bytes, wifiSettings->ipAddr.v, 4);
                message.ip_addr.size = 4;
                break;
            }
            case DaqifiOutMessage_net_mask_tag:
            {
                wifi_manager_settings_t* wifiSettings = &state->wifiSettings;
                memcpy(message.net_mask.bytes, wifiSettings->ipMask.v, 4);
                message.net_mask.size = 4;
                break;
            }
            case DaqifiOutMessage_gateway_tag:
            {
                wifi_manager_settings_t* wifiSettings = &state->wifiSettings;
                memcpy(message.gateway.bytes, wifiSettings->gateway.v, 4);
                message.gateway.size = 4;
                break;
            }
            case DaqifiOutMessage_primary_dns_tag:
            {
                break;
            }
            case DaqifiOutMessage_secondary_dns_tag:
            {
                break;
            }
            case DaqifiOutMessage_mac_addr_tag:
            {
                wifi_manager_settings_t* wifiSettings = &state->wifiSettings;
                memcpy(message.mac_addr.bytes, wifiSettings->macAddr.addr, 6);
                message.mac_addr.size = 6;

                break;
            }
            case DaqifiOutMessage_ip_addr_v6_tag:
            {
                break;
            }
            case DaqifiOutMessage_sub_pre_length_v6_tag:
                break;
            case DaqifiOutMessage_gateway_v6_tag:
                break;
            case DaqifiOutMessage_primary_dns_v6_tag:
                break;
            case DaqifiOutMessage_secondary_dns_v6_tag:
                break;
            case DaqifiOutMessage_eui_64_tag:
                break;

            case DaqifiOutMessage_host_name_tag:
            {
                wifi_manager_settings_t* wifiSettings = &state->wifiSettings;
                size_t len = min(strlen(wifiSettings->hostName), WIFI_MANAGER_DNS_CLIENT_MAX_HOSTNAME_LEN);
                memcpy(message.host_name, wifiSettings->hostName, len);
                message.host_name[len] = '\0';
                break;
            }
            case DaqifiOutMessage_device_port_tag:
            {
                wifi_manager_settings_t* wifiSettings = &state->wifiSettings;
                message.device_port = wifiSettings->tcpPort;

                break;
            }
            case DaqifiOutMessage_friendly_device_name_tag:
                break;
            case DaqifiOutMessage_ssid_tag:
            {
                wifi_manager_settings_t* wifiSettings = &state->wifiSettings;
                size_t len = min(strlen(wifiSettings->ssid), WDRV_WINC_MAX_SSID_LEN);
                memcpy(message.ssid, wifiSettings->ssid, len);
                message.ssid[len] = '\0';

                break;
            }
            case DaqifiOutMessage_wifi_security_mode_tag:
            {
                wifi_manager_settings_t* wifiSettings = &state->wifiSettings;
                message.wifi_security_mode = wifiSettings->securityMode;

                break;
            }
            case DaqifiOutMessage_wifi_inf_mode_tag:
            {
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
                snprintf(message.device_pn, 6, "Nq%d", pBoardConfig->BoardVariant);
                break;
            }
            case DaqifiOutMessage_device_hw_rev_tag:
                memcpy(&message.device_hw_rev,
                        pBoardConfig->boardHardwareRev,
                        strlen(pBoardConfig->boardHardwareRev));
                break;
            case DaqifiOutMessage_device_fw_rev_tag:
                memcpy(&message.device_fw_rev,
                        pBoardConfig->boardFirmwareRev,
                        strlen(pBoardConfig->boardFirmwareRev));
                break;
            case DaqifiOutMessage_device_sn_tag:
                message.device_sn = pBoardConfig->boardSerialNumber;
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

/* =========================================================================
 * Fast-path streaming protobuf encoder
 *
 * Writes wire-format bytes directly for the 2-4 streaming fields,
 * bypassing nanopb's 65-field descriptor iteration (248us -> ~10-30us).
 * Uses nanopb's low-level primitives for correct varint/tag encoding.
 * ========================================================================= */

/**
 * @brief Encode streaming fields to a pb_ostream_t.
 *
 * Works with both sizing streams (NULL callback) and real buffer streams.
 * Two-pass pattern: call once with sizing stream to get size, then with
 * real stream to write bytes.
 */
static bool encode_streaming_fields(pb_ostream_t *stream,
        uint32_t timestamp,
        const int32_t* ainData, size_t ainCount,
        const uint8_t* dioData, size_t dioSize,
        const uint8_t* dioDir, size_t dioDirSize) {

    /* Field 1: msg_time_stamp (uint32, varint) */
    if (timestamp != 0) {
        if (!pb_encode_tag(stream, PB_WT_VARINT, DaqifiOutMessage_msg_time_stamp_tag))
            return false;
        if (!pb_encode_varint(stream, (uint32_t)timestamp))
            return false;
    }

    /* Field 2: analog_in_data (packed repeated sint32) */
    if (ainCount > 0) {
        /* Calculate packed payload size using a sizing sub-stream */
        pb_ostream_t sizestream = {0};
        for (size_t i = 0; i < ainCount; i++) {
            if (!pb_encode_svarint(&sizestream, (int32_t)ainData[i]))
                return false;
        }

        /* Write tag + length prefix + packed zigzag values */
        if (!pb_encode_tag(stream, PB_WT_STRING, DaqifiOutMessage_analog_in_data_tag))
            return false;
        if (!pb_encode_varint(stream, (uint32_t)sizestream.bytes_written))
            return false;
        for (size_t i = 0; i < ainCount; i++) {
            if (!pb_encode_svarint(stream, (int32_t)ainData[i]))
                return false;
        }
    }

    /* Field 5: digital_data (bytes) */
    if (dioData != NULL && dioSize > 0) {
        if (!pb_encode_tag(stream, PB_WT_STRING, DaqifiOutMessage_digital_data_tag))
            return false;
        if (!pb_encode_string(stream, dioData, dioSize))
            return false;
    }

    /* Field 37: digital_port_dir (bytes) */
    if (dioDir != NULL && dioDirSize > 0) {
        if (!pb_encode_tag(stream, PB_WT_STRING, DaqifiOutMessage_digital_port_dir_tag))
            return false;
        if (!pb_encode_string(stream, dioDir, dioDirSize))
            return false;
    }

    return true;
}

/**
 * @brief Encode a single length-delimited streaming message.
 *
 * Two-pass: sizing pass to get inner message length, then encode pass
 * to write varint length prefix + message bytes.
 *
 * @return Bytes written to pBuffer, or 0 on error.
 */
static size_t encode_streaming_msg_delimited(
        uint8_t* pBuffer, size_t buffSize,
        uint32_t timestamp,
        const int32_t* ainData, size_t ainCount,
        const uint8_t* dioData, size_t dioSize,
        const uint8_t* dioDir, size_t dioDirSize) {

    /* Sizing pass */
    pb_ostream_t sizestream = {0};
    if (!encode_streaming_fields(&sizestream, timestamp,
            ainData, ainCount, dioData, dioSize, dioDir, dioDirSize)) {
        return 0;
    }

    /* Encoding pass: varint length prefix + message */
    pb_ostream_t stream = pb_ostream_from_buffer(pBuffer, buffSize);
    if (!pb_encode_varint(&stream, (uint32_t)sizestream.bytes_written))
        return 0;
    if (!encode_streaming_fields(&stream, timestamp,
            ainData, ainCount, dioData, dioSize, dioDir, dioDirSize))
        return 0;

    return stream.bytes_written;
}

size_t Nanopb_EncodeStreamingFast(tBoardData* state,
        const NanopbFlagsArray* fields,
        uint8_t* pBuffer, size_t buffSize) {

    if (pBuffer == NULL || buffSize == 0) {
        return 0;
    }

    /* Parse flags to determine which streaming fields are present */
    bool hasAIN = false, hasDIO = false;
    for (size_t i = 0; i < fields->Size; i++) {
        switch (fields->Data[i]) {
            case DaqifiOutMessage_analog_in_data_tag: hasAIN = true; break;
            case DaqifiOutMessage_digital_data_tag:   hasDIO = true; break;
        }
    }

    /* Prepare DIO data upfront */
    const tBoardConfig* pBoardConfig = BoardConfig_Get(BOARDCONFIG_ALL_CONFIG, 0);
    DIORuntimeArray* pRuntimeDIOChannels = BoardRunTimeConfig_Get(BOARDRUNTIMECONFIG_DIO_CHANNELS);

    uint8_t dioValues[2] = {0};
    size_t dioSize = 0;
    uint8_t dioDir[2] = {0};
    size_t dioDirSize = 0;

    if (hasDIO) {
        DIOSample DIOdata;
        if (DIOSampleList_PopFront(&state->DIOSamples, &DIOdata)) {
            memcpy(dioValues, &DIOdata.Values, sizeof(dioValues));
            dioSize = sizeof(dioValues);
        }

        /* Build port direction bitmap */
        uint32_t dirData = 0;
        for (uint32_t x = 0; x < pBoardConfig->DIOChannels.Size; x++) {
            dirData |= (pRuntimeDIOChannels->Data[x].IsInput << x);
        }
        for (size_t y = 0; y < sizeof(dioDir); y++) {
            dioDir[y] = (uint8_t)(dirData >> (y * 8));
        }
        dioDirSize = sizeof(dioDir);
    }

    uint32_t bufferOffset = 0;
    uint32_t lastTimestamp = state->StreamTrigStamp;

    /* Batch all queued AIN samples into one message.
     * Collect all values from all queued sample sets into a single
     * analog_in_data packed array. This reduces per-message framing
     * overhead and produces fewer, larger USB writes.
     * Static: 3200 bytes — too large for streaming task stack (5568 bytes). */
    if (hasAIN) {
        /* Static buffer — safe because only called from streaming_Task (single caller) */
        static int32_t batchValues[MAX_BATCH_VALUES];
        size_t batchCount = 0;

        uint32_t queueSize = AInSampleList_Size();
        AInPublicSampleList_t *pPublicSampleList;

        while (queueSize > 0) {
            if (!AInSampleList_PopFront(&pPublicSampleList)) break;
            if (pPublicSampleList == NULL) break;
            queueSize--;

            for (int i = 0; i < MAX_AIN_PUBLIC_CHANNELS; i++) {
                if (!pPublicSampleList->isSampleValid[i])
                    continue;
                if (batchCount >= MAX_BATCH_VALUES)
                    break;
                batchValues[batchCount++] = pPublicSampleList->sampleElement[i].Value;
                lastTimestamp = pPublicSampleList->sampleElement[i].Timestamp;
            }

            AInSampleList_FreeToPool(pPublicSampleList);

            if (batchCount >= MAX_BATCH_VALUES)
                break;
        }

        if (batchCount > 0) {
            size_t written = encode_streaming_msg_delimited(
                pBuffer + bufferOffset, buffSize - bufferOffset,
                lastTimestamp, batchValues, batchCount,
                dioSize > 0 ? dioValues : NULL, dioSize,
                dioDirSize > 0 ? dioDir : NULL, dioDirSize);

            if (written == 0) return 0;
            bufferOffset += written;
        }
    } else if (dioSize > 0) {
        /* DIO-only message (no AIN data available) */
        size_t written = encode_streaming_msg_delimited(
            pBuffer + bufferOffset, buffSize - bufferOffset,
            lastTimestamp, NULL, 0,
            dioValues, dioSize, dioDir, dioDirSize);

        if (written > 0) {
            bufferOffset += written;
        }
    }

    return bufferOffset;
}
