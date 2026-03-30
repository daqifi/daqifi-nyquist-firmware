/*! @file NanoPB_Encoder.c
 *
 * This file implements the functions to manage the NanoPB encoder
 */

#include "libraries/nanopb/pb_encode.h"

#include "state/data/BoardData.h"
#include "Util/Logger.h"

#include "DaqifiOutMessage.pb.h"
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
/* Worst-case encoded size for one streaming PB message (length-delimited).
 * Computed at compile time from proto constants. Used as a pre-check before
 * popping samples from the queue to avoid consuming data that can't encode.
 *
 * varint worst case: 5 bytes for uint32, 5 bytes for sint32 (zigzag)
 * tag sizes: field 1-15 = 1 byte, field 16-2047 = 2 bytes
 */
#define PB_VARINT32_MAX     5U  /* max bytes for a 32-bit varint */
#define PB_TAG1_SIZE        1U  /* tag byte for fields 1-15 */
#define PB_TAG2_SIZE        2U  /* tag bytes for fields 16-2047 */

/* Derive sizes from generated struct to stay in sync with .proto/.options */
#define PB_DIO_DATA_MAX  (sizeof(((DaqifiOutMessage*)0)->digital_data.bytes))
#define PB_DIO_DIR_MAX   (sizeof(((DaqifiOutMessage*)0)->digital_port_dir.bytes))
#define PB_AIN_MAX_COUNT (sizeof(((DaqifiOutMessage*)0)->analog_in_data) / \
                          sizeof(((DaqifiOutMessage*)0)->analog_in_data[0]))

#define STREAMING_MSG_MAX_SIZE (                                              \
    PB_VARINT32_MAX +                            /* length-delimited prefix */\
    PB_TAG1_SIZE + PB_VARINT32_MAX +             /* field 1: uint32 ts */     \
    PB_TAG1_SIZE + PB_VARINT32_MAX +             /* field 2: packed length */ \
    (PB_AIN_MAX_COUNT * PB_VARINT32_MAX) +       /* field 2: sint32 values */\
    PB_TAG1_SIZE + 1 + PB_DIO_DATA_MAX +         /* field 5: digital_data */ \
    PB_TAG2_SIZE + 1 + PB_DIO_DIR_MAX            /* field 37: port_dir */    \
)

/**
 * @brief Estimate buffer size needed for encoding selected fields.
 *
 * Returns a conservative upper bound by summing the C struct sizes of
 * the selected fields. Used by Nanopb_Encode() (metadata path) to
 * pre-check buffer capacity before encoding. Overestimates are safe;
 * the actual encoded protobuf will always be smaller.
 */

static int Nanopb_EncodeLength(const NanopbFlagsArray* fields) {
    int i;
    int len = 0;
    DaqifiOutMessage* out = NULL;  // sizeof only — never dereferenced

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
 * =========================================================================
 *
 * WHY THIS EXISTS:
 * ----------------
 * DaqifiOutMessage has 65 fields, but streaming only uses 4 of them
 * (timestamp, analog data, digital data, digital port direction).
 * The standard nanopb pb_encode_delimited() must iterate ALL 65 field
 * descriptors twice (sizing pass + encoding pass) even when only 4 have
 * data. This took 248us per encode on PIC32MZ@200MHz.
 *
 * This fast encoder writes protobuf wire-format bytes directly for just
 * the streaming fields, reducing encode time to ~10-20us (~15x speedup).
 *
 * WHY NOT A SEPARATE PROTO MESSAGE:
 * ---------------------------------
 * An alternative would be defining a small `DaqifiStreamMessage` with
 * only the 4 streaming fields. nanopb would iterate 4 descriptors instead
 * of 65, taking ~30-50us. However:
 *   - Still requires zero-initializing and populating a struct (~100 bytes)
 *   - Still has per-field function pointer dispatch overhead
 *   - Adds a second message type for clients to handle
 *   - The streaming fields are stable (timestamp + ADC + DIO), so the
 *     maintenance cost of direct encoding is low
 * The fast encoder avoids all struct overhead by writing directly from
 * the sample queue values to the output buffer.
 *
 * WIRE FORMAT PRODUCED:
 * ---------------------
 * Each call produces one or more length-delimited protobuf messages,
 * each compatible with DaqifiOutMessage decoders:
 *
 *   [varint: inner_message_length]     <- length-delimited framing
 *   [0x08] [varint: timestamp]         <- field 1: msg_time_stamp (uint32)
 *   [0x12] [varint: packed_len]        <- field 2: analog_in_data tag + length
 *     [zigzag varint] [zigzag varint]... <- packed sint32 channel values
 *   [0x2A] [varint: len] [bytes]       <- field 5: digital_data (optional)
 *   [0xAA 0x02] [varint: len] [bytes]  <- field 37: digital_port_dir (optional)
 *
 * Fields use the same tag numbers as DaqifiOutMessage, so any client
 * decoding DaqifiOutMessage will correctly parse these messages. Fields
 * not present (e.g., device_status, network info) are simply absent,
 * which protobuf handles natively.
 *
 * ENCODING APPROACH:
 * ------------------
 * Uses nanopb's public low-level encoding API:
 *   - pb_encode_tag()    : write field tag (field number + wire type)
 *   - pb_encode_varint() : write unsigned variable-length integer
 *   - pb_encode_svarint(): write signed zigzag-encoded varint
 *   - pb_encode_string() : write length-prefixed byte array
 *   - PB_OSTREAM_SIZING  : null stream that counts bytes without writing
 *
 * The two-pass pattern (size then encode) matches how nanopb implements
 * pb_encode_delimited() internally, but only touches 2-4 fields instead
 * of 65.
 *
 * WHEN TO MODIFY THIS CODE:
 * -------------------------
 * If the streaming message format changes (new fields added to the
 * streaming path, field types changed, or tag numbers reassigned),
 * update encode_streaming_fields() to match. The field tags are
 * defined in DaqifiOutMessage.pb.h as DaqifiOutMessage_*_tag macros.
 *
 * For metadata/config messages (device info, network config, etc.),
 * continue using Nanopb_Encode() which handles all 65 fields via the
 * standard nanopb API.
 *
 * THREAD SAFETY:
 * --------------
 * Nanopb_EncodeStreamingFast() is called only from streaming_Task
 * (priority 2). It pops samples from the lock-free sample queue and
 * DIO queue. No shared mutable state beyond the queues themselves.
 *
 * PERFORMANCE (PIC32MZ@200MHz, measured):
 * ----------------------------------------
 * Old (nanopb 0.3.9, 65-field iteration): 248us per encode
 * New (direct wire encoding):              ~10-20us per encode
 * Throughput ceilings (USB, NQ1):
 *   1ch:  5kHz -> 18kHz  (3.6x improvement)
 *   16ch: 3kHz -> 5kHz   (1.7x improvement)
 * ========================================================================= */

/**
 * @brief Encode streaming fields to a pb_ostream_t.
 *
 * Writes the protobuf wire format for streaming-specific fields only.
 * Works with both sizing streams (PB_OSTREAM_SIZING) and real buffer
 * streams (pb_ostream_from_buffer). Call once with a sizing stream to
 * get the encoded size, then again with a real stream to write bytes.
 *
 * @param stream    nanopb output stream (sizing or buffer-backed)
 * @param timestamp Sample set timestamp (ISR trigger time from hardware timer)
 * @param ainData   Array of raw ADC values (sint32, zigzag-encoded on wire)
 * @param ainCount  Number of values in ainData (= number of enabled channels)
 * @param dioData   Digital I/O sample bytes (2 bytes, LSB=ch0), or NULL
 * @param dioSize   Size of dioData (0 if no DIO data)
 * @param dioDir    Digital port direction bitmap bytes, or NULL
 * @param dioDirSize Size of dioDir (0 if no direction data)
 * @return true on success, false if stream write failed
 */
static bool encode_streaming_fields(pb_ostream_t *stream,
        uint32_t timestamp,
        const int32_t* ainData, size_t ainCount,
        const uint8_t* dioData, size_t dioSize,
        const uint8_t* dioDir, size_t dioDirSize) {

    /* Field 1: msg_time_stamp (uint32, wire type 0 = varint)
     * Proto3: zero-valued fields are omitted (not encoded on wire). */
    if (timestamp != 0) {
        if (!pb_encode_tag(stream, PB_WT_VARINT, DaqifiOutMessage_msg_time_stamp_tag))
            return false;
        if (!pb_encode_varint(stream, (uint32_t)timestamp))
            return false;
    }

    /* Field 2: analog_in_data (packed repeated sint32, wire type 2 = length-delimited)
     *
     * Proto3 packs repeated scalar fields by default. The packed format is:
     *   [tag] [varint: total_payload_bytes] [zigzag_val1] [zigzag_val2] ...
     *
     * Each sint32 value is zigzag-encoded: (n << 1) ^ (n >> 31), then
     * varint-encoded. This makes small negative values compact (e.g., -1 = 1 byte).
     *
     * We need the packed payload size BEFORE writing it (for the length prefix),
     * so we use a sizing sub-stream to calculate it first. */
    if (ainCount > 0) {
        /* Sub-sizing pass: count bytes for all zigzag varints */
        pb_ostream_t sizestream = PB_OSTREAM_SIZING;
        for (size_t i = 0; i < ainCount; i++) {
            if (!pb_encode_svarint(&sizestream, (int32_t)ainData[i]))
                return false;
        }

        /* Write: [tag 2, wire type 2] [payload length] [zigzag values...] */
        if (!pb_encode_tag(stream, PB_WT_STRING, DaqifiOutMessage_analog_in_data_tag))
            return false;
        if (!pb_encode_varint(stream, (uint32_t)sizestream.bytes_written))
            return false;
        for (size_t i = 0; i < ainCount; i++) {
            if (!pb_encode_svarint(stream, (int32_t)ainData[i]))
                return false;
        }
    }

    /* Field 5: digital_data (bytes, wire type 2 = length-delimited)
     * 2-byte bitmap: bit N = digital channel N value (LSB = ch0). */
    if (dioData != NULL && dioSize > 0) {
        if (!pb_encode_tag(stream, PB_WT_STRING, DaqifiOutMessage_digital_data_tag))
            return false;
        if (!pb_encode_string(stream, dioData, dioSize))
            return false;
    }

    /* Field 37: digital_port_dir (bytes, wire type 2 = length-delimited)
     * 2-byte bitmap: bit N = 1 if channel N is input, 0 if output.
     * Tag 37 requires 2 bytes on wire: (37 << 3 | 2) = 298 = 0xAA 0x02. */
    if (dioDir != NULL && dioDirSize > 0) {
        if (!pb_encode_tag(stream, PB_WT_STRING, DaqifiOutMessage_digital_port_dir_tag))
            return false;
        if (!pb_encode_string(stream, dioDir, dioDirSize))
            return false;
    }

    return true;
}

/**
 * @brief Encode a single length-delimited streaming message to a buffer.
 *
 * Produces one protobuf message with a varint length prefix, suitable for
 * concatenation in a stream (the standard protobuf delimited format used
 * by all DAQiFi clients).
 *
 * Uses two passes:
 *   1. Sizing pass (PB_OSTREAM_SIZING) — counts inner message bytes
 *   2. Encoding pass — writes [varint length] [message bytes] to buffer
 *
 * @param pBuffer   Output buffer
 * @param buffSize  Available space in output buffer
 * @param timestamp Sample set timestamp
 * @param ainData   ADC channel values array, or NULL if no AIN data
 * @param ainCount  Number of AIN values (0 if no AIN data)
 * @param dioData   Digital I/O sample bytes, or NULL
 * @param dioSize   Size of dioData
 * @param dioDir    Digital port direction bytes, or NULL
 * @param dioDirSize Size of dioDir
 * @return Bytes written to pBuffer, or 0 on error
 */
static size_t encode_streaming_msg_delimited(
        uint8_t* pBuffer, size_t buffSize,
        uint32_t timestamp,
        const int32_t* ainData, size_t ainCount,
        const uint8_t* dioData, size_t dioSize,
        const uint8_t* dioDir, size_t dioDirSize) {

    /* Pass 1: calculate inner message size without writing */
    pb_ostream_t sizestream = PB_OSTREAM_SIZING;
    if (!encode_streaming_fields(&sizestream, timestamp,
            ainData, ainCount, dioData, dioSize, dioDir, dioDirSize)) {
        return 0;
    }

    /* Pass 2: write [varint length prefix] [message bytes] to buffer */
    pb_ostream_t stream = pb_ostream_from_buffer(pBuffer, buffSize);
    if (!pb_encode_varint(&stream, (uint32_t)sizestream.bytes_written))
        return 0;
    if (!encode_streaming_fields(&stream, timestamp,
            ainData, ainCount, dioData, dioSize, dioDir, dioDirSize))
        return 0;

    return stream.bytes_written;
}

/**
 * @brief Fast-path protobuf encoder for streaming data.
 *
 * Encodes one length-delimited PB message per queued sample set, each with
 * its own timestamp. This preserves per-ISR-trigger timing fidelity — the
 * client receives one message per acquisition with the exact hardware
 * timestamp from the streaming timer.
 *
 * Data flow:
 *   1. Pop all queued AInPublicSampleList_t entries from the sample queue
 *   2. For each sample set: extract enabled channel values + timestamp
 *   3. Encode as a length-delimited DaqifiOutMessage (fast wire-format path)
 *   4. Append to output buffer (may contain multiple delimited messages)
 *   5. DIO data (if available) is included in the first AIN message
 *
 * Called from: streaming_Task (priority 2) in the encode loop.
 * Replaces: Nanopb_Encode() for the streaming hot path only.
 *
 * @param state     Board data (provides DIO sample queue, stream trigger stamp)
 * @param fields    NanopbFlagsArray indicating which field tags to encode
 *                  (typically: msg_time_stamp, analog_in_data, digital_data,
 *                  digital_port_dir — set up in streaming.c)
 * @param pBuffer   Output buffer for encoded protobuf messages
 * @param buffSize  Available space in output buffer
 * @return Total bytes written (may contain multiple delimited messages), or 0 on error
 */
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

        /* Build port direction bitmap (1=input per channel) */
        uint32_t dirData = 0;
        for (uint32_t x = 0; x < pBoardConfig->DIOChannels.Size && x < 32; x++) {
            dirData |= ((uint32_t)pRuntimeDIOChannels->Data[x].IsInput << x);
        }
        for (size_t y = 0; y < sizeof(dioDir); y++) {
            dioDir[y] = (uint8_t)(dirData >> (y * 8));
        }
        dioDirSize = sizeof(dioDir);
    }

    uint32_t bufferOffset = 0;

    /* Encode one PB message per sample set (ISR trigger).
     * Each sample set has its own timestamp that must be preserved.
     * DIO data is included in the first AIN message if available,
     * or in a standalone message if no AIN data is present. */
    if (hasAIN) {
        uint32_t queueSize = AInSampleList_Size();
        AInPublicSampleList_t *pPublicSampleList;
        bool dioIncluded = false;

        while (queueSize > 0) {
            /* Check buffer space BEFORE consuming a sample from the queue.
             * If we pop first and then can't encode, the sample is lost. */
            if (buffSize - bufferOffset < STREAMING_MSG_MAX_SIZE) {
                LOG_I("[PB] Buffer full at %u/%u bytes, %u samples deferred",
                      (unsigned)bufferOffset, (unsigned)buffSize, (unsigned)queueSize);
                break;  /* Leave remaining samples queued for next call */
            }

            if (!AInSampleList_PopFront(&pPublicSampleList)) break;
            if (pPublicSampleList == NULL) break;
            queueSize--;

            /* Collect valid channel values for this sample set */
            int32_t values[MAX_AIN_PUBLIC_CHANNELS];
            size_t count = 0;
            uint32_t timestamp = 0;

            for (int i = 0; i < MAX_AIN_PUBLIC_CHANNELS; i++) {
                if (!pPublicSampleList->isSampleValid[i])
                    continue;
                if (count >= MAX_AIN_PUBLIC_CHANNELS)
                    break;
                values[count++] = pPublicSampleList->sampleElement[i].Value;
                timestamp = pPublicSampleList->sampleElement[i].Timestamp;
            }

            AInSampleList_FreeToPool(pPublicSampleList);

            if (count > 0) {
                /* Include DIO in the first AIN message only */
                const uint8_t* dioV = (!dioIncluded && dioSize > 0) ? dioValues : NULL;
                size_t dioS = (!dioIncluded && dioSize > 0) ? dioSize : 0;
                const uint8_t* dioD = (!dioIncluded && dioDirSize > 0) ? dioDir : NULL;
                size_t dioDS = (!dioIncluded && dioDirSize > 0) ? dioDirSize : 0;
                if (dioS > 0) dioIncluded = true;

                size_t written = encode_streaming_msg_delimited(
                    pBuffer + bufferOffset, buffSize - bufferOffset,
                    timestamp, values, count,
                    dioV, dioS, dioD, dioDS);

                if (written == 0) {
                    LOG_E("[PB] Encode failed: buf=%u off=%u max=%u ch=%u",
                          (unsigned)buffSize, (unsigned)bufferOffset,
                          (unsigned)STREAMING_MSG_MAX_SIZE, (unsigned)count);
                    return bufferOffset > 0 ? bufferOffset : 0;
                }
                bufferOffset += written;
            }
        }
    }

    /* DIO-only message if no AIN data consumed the DIO */
    if (!hasAIN && dioSize > 0) {
        size_t written = encode_streaming_msg_delimited(
            pBuffer + bufferOffset, buffSize - bufferOffset,
            state->StreamTrigStamp, NULL, 0,
            dioValues, dioSize, dioDir, dioDirSize);

        if (written > 0) {
            bufferOffset += written;
        }
    }

    return bufferOffset;
}
