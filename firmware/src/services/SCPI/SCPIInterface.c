#include "SCPIInterface.h"


//// General
#include <stdlib.h>
#include <string.h>
//
//// Harmony
//#include "system_config.h"
//#include "system_definitions.h"
//
//// 3rd Party
//#include "HAL/NVM/DaqifiSettings.h"
//#include "HAL/Power/PowerApi.h"
#include "services/DaqifiPB/DaqifiOutMessage.pb.h"
#include "services/DaqifiPB/NanoPB_Encoder.h"
//#include "Util/StringFormatters.h"
#include "Util/Logger.h"
#include "state/data/BoardData.h"
#include "state/board/BoardConfig.h"
#include "state/runtime/BoardRuntimeConfig.h"
#include "HAL/DIO.h"
#include "SCPIADC.h"
#include "SCPIDIO.h"
#include "SCPILAN.h"
#include "SCPIStorageSD.h"
#include "../streaming.h"
#include "../../HAL/TimerApi/TimerApi.h"

//
#define UNUSED(x) (void)(x)
//
#define SCPI_IDN1 "DAQiFi"
#define SCPI_IDN2 "Nq1"
#define SCPI_IDN3 NULL
#define SCPI_IDN4 "01-02"


// Declare force bootloader RAM flag location
volatile uint32_t force_bootloader_flag __attribute__((persistent, coherent, address(FORCE_BOOTLOADER_FLAG_ADDR)));

const NanopbFlagsArray fields_all = {
    .Size = 65,
    .Data =
    {
        DaqifiOutMessage_msg_time_stamp_tag,
        DaqifiOutMessage_analog_in_data_tag,
        DaqifiOutMessage_analog_in_data_float_tag,
        DaqifiOutMessage_analog_in_data_ts_tag,
        DaqifiOutMessage_digital_data_tag,
        DaqifiOutMessage_digital_data_ts_tag,
        DaqifiOutMessage_analog_out_data_tag,
        DaqifiOutMessage_device_status_tag,
        DaqifiOutMessage_pwr_status_tag,
        DaqifiOutMessage_batt_status_tag,
        DaqifiOutMessage_temp_status_tag,
        DaqifiOutMessage_timestamp_freq_tag,
        DaqifiOutMessage_analog_in_port_num_tag,
        DaqifiOutMessage_analog_in_port_num_priv_tag,
        DaqifiOutMessage_analog_in_port_type_tag,
        DaqifiOutMessage_analog_in_port_av_rse_tag,
        DaqifiOutMessage_analog_in_port_rse_tag,
        DaqifiOutMessage_analog_in_port_enabled_tag,
        DaqifiOutMessage_analog_in_port_av_range_tag,
        DaqifiOutMessage_analog_in_port_av_range_priv_tag,
        DaqifiOutMessage_analog_in_port_range_tag,
        DaqifiOutMessage_analog_in_port_range_priv_tag,
        DaqifiOutMessage_analog_in_res_tag,
        DaqifiOutMessage_analog_in_res_priv_tag,
        DaqifiOutMessage_analog_in_int_scale_m_tag,
        DaqifiOutMessage_analog_in_int_scale_m_priv_tag,
        DaqifiOutMessage_analog_in_cal_m_tag,
        DaqifiOutMessage_analog_in_cal_b_tag,
        DaqifiOutMessage_analog_in_cal_m_priv_tag,
        DaqifiOutMessage_analog_in_cal_b_priv_tag,
        DaqifiOutMessage_digital_port_num_tag,
        DaqifiOutMessage_digital_port_type_tag,
        DaqifiOutMessage_digital_port_dir_tag,
        DaqifiOutMessage_analog_out_port_num_tag,
        DaqifiOutMessage_analog_out_port_type_tag,
        DaqifiOutMessage_analog_out_res_tag,
        DaqifiOutMessage_analog_out_port_av_range_tag,
        DaqifiOutMessage_analog_out_port_range_tag,
        DaqifiOutMessage_ip_addr_tag,
        DaqifiOutMessage_net_mask_tag,
        DaqifiOutMessage_gateway_tag,
        DaqifiOutMessage_primary_dns_tag,
        DaqifiOutMessage_secondary_dns_tag,
        DaqifiOutMessage_mac_addr_tag,
        DaqifiOutMessage_ip_addr_v6_tag,
        DaqifiOutMessage_sub_pre_length_v6_tag,
        DaqifiOutMessage_gateway_v6_tag,
        DaqifiOutMessage_primary_dns_v6_tag,
        DaqifiOutMessage_secondary_dns_v6_tag,
        DaqifiOutMessage_eui_64_tag,
        DaqifiOutMessage_host_name_tag,
        DaqifiOutMessage_device_port_tag,
        DaqifiOutMessage_friendly_device_name_tag,
        DaqifiOutMessage_ssid_tag,
        DaqifiOutMessage_ssid_strength_tag,
        DaqifiOutMessage_wifi_security_mode_tag,
        DaqifiOutMessage_wifi_inf_mode_tag,
        DaqifiOutMessage_av_ssid_tag,
        DaqifiOutMessage_av_ssid_strength_tag,
        DaqifiOutMessage_av_wifi_security_mode_tag,
        DaqifiOutMessage_av_wifi_inf_mode_tag,
        DaqifiOutMessage_device_pn_tag,
        DaqifiOutMessage_device_hw_rev_tag,
        DaqifiOutMessage_device_fw_rev_tag,
        DaqifiOutMessage_device_sn_tag,
    }
};

const NanopbFlagsArray fields_info = {
    .Size = 59,
    .Data =
    {
        DaqifiOutMessage_msg_time_stamp_tag,
        DaqifiOutMessage_device_status_tag,
        DaqifiOutMessage_pwr_status_tag,
        DaqifiOutMessage_batt_status_tag,
        DaqifiOutMessage_temp_status_tag,
        DaqifiOutMessage_timestamp_freq_tag,
        DaqifiOutMessage_analog_in_port_num_tag,
        DaqifiOutMessage_analog_in_port_num_priv_tag,
        DaqifiOutMessage_analog_in_port_type_tag,
        DaqifiOutMessage_analog_in_port_av_rse_tag,
        DaqifiOutMessage_analog_in_port_rse_tag,
        DaqifiOutMessage_analog_in_port_enabled_tag,
        DaqifiOutMessage_analog_in_port_av_range_tag,
        DaqifiOutMessage_analog_in_port_av_range_priv_tag,
        DaqifiOutMessage_analog_in_port_range_tag,
        DaqifiOutMessage_analog_in_port_range_priv_tag,
        DaqifiOutMessage_analog_in_res_tag,
        DaqifiOutMessage_analog_in_res_priv_tag,
        DaqifiOutMessage_analog_in_int_scale_m_tag,
        DaqifiOutMessage_analog_in_int_scale_m_priv_tag,
        DaqifiOutMessage_analog_in_cal_m_tag,
        DaqifiOutMessage_analog_in_cal_b_tag,
        DaqifiOutMessage_analog_in_cal_m_priv_tag,
        DaqifiOutMessage_analog_in_cal_b_priv_tag,
        DaqifiOutMessage_digital_port_num_tag,
        DaqifiOutMessage_digital_port_type_tag,
        DaqifiOutMessage_digital_port_dir_tag,
        DaqifiOutMessage_analog_out_port_num_tag,
        DaqifiOutMessage_analog_out_port_type_tag,
        DaqifiOutMessage_analog_out_res_tag,
        DaqifiOutMessage_analog_out_port_av_range_tag,
        DaqifiOutMessage_analog_out_port_range_tag,
        DaqifiOutMessage_ip_addr_tag,
        DaqifiOutMessage_net_mask_tag,
        DaqifiOutMessage_gateway_tag,
        DaqifiOutMessage_primary_dns_tag,
        DaqifiOutMessage_secondary_dns_tag,
        DaqifiOutMessage_mac_addr_tag,
        DaqifiOutMessage_ip_addr_v6_tag,
        DaqifiOutMessage_sub_pre_length_v6_tag,
        DaqifiOutMessage_gateway_v6_tag,
        DaqifiOutMessage_primary_dns_v6_tag,
        DaqifiOutMessage_secondary_dns_v6_tag,
        DaqifiOutMessage_eui_64_tag,
        DaqifiOutMessage_host_name_tag,
        DaqifiOutMessage_device_port_tag,
        DaqifiOutMessage_friendly_device_name_tag,
        DaqifiOutMessage_ssid_tag,
        DaqifiOutMessage_ssid_strength_tag,
        DaqifiOutMessage_wifi_security_mode_tag,
        DaqifiOutMessage_wifi_inf_mode_tag,
        DaqifiOutMessage_av_ssid_tag,
        DaqifiOutMessage_av_ssid_strength_tag,
        DaqifiOutMessage_av_wifi_security_mode_tag,
        DaqifiOutMessage_av_wifi_inf_mode_tag,
        DaqifiOutMessage_device_pn_tag,
        DaqifiOutMessage_device_hw_rev_tag,
        DaqifiOutMessage_device_fw_rev_tag,
        DaqifiOutMessage_device_sn_tag,
    }
};

const NanopbFlagsArray fields_discovery = {
    .Size = 37,
    .Data =
    {
        DaqifiOutMessage_msg_time_stamp_tag,
        DaqifiOutMessage_device_status_tag,
        DaqifiOutMessage_pwr_status_tag,
        DaqifiOutMessage_batt_status_tag,
        DaqifiOutMessage_temp_status_tag,
        DaqifiOutMessage_analog_in_port_num_tag,
        DaqifiOutMessage_analog_in_port_num_priv_tag,
        DaqifiOutMessage_analog_in_port_type_tag,
        DaqifiOutMessage_analog_in_port_av_range_tag,
        DaqifiOutMessage_analog_in_port_av_range_priv_tag,
        DaqifiOutMessage_analog_in_res_tag,
        DaqifiOutMessage_analog_in_res_priv_tag,
        DaqifiOutMessage_digital_port_num_tag,
        DaqifiOutMessage_digital_port_type_tag,
        DaqifiOutMessage_analog_out_port_num_tag,
        DaqifiOutMessage_analog_out_port_type_tag,
        DaqifiOutMessage_analog_out_res_tag,
        DaqifiOutMessage_analog_out_port_av_range_tag,
        DaqifiOutMessage_ip_addr_tag,
        DaqifiOutMessage_net_mask_tag,
        DaqifiOutMessage_gateway_tag,
        DaqifiOutMessage_primary_dns_tag,
        DaqifiOutMessage_secondary_dns_tag,
        DaqifiOutMessage_mac_addr_tag,
        DaqifiOutMessage_ip_addr_v6_tag,
        DaqifiOutMessage_sub_pre_length_v6_tag,
        DaqifiOutMessage_gateway_v6_tag,
        DaqifiOutMessage_primary_dns_v6_tag,
        DaqifiOutMessage_secondary_dns_v6_tag,
        DaqifiOutMessage_eui_64_tag,
        DaqifiOutMessage_host_name_tag,
        DaqifiOutMessage_device_port_tag,
        DaqifiOutMessage_friendly_device_name_tag,
        DaqifiOutMessage_device_pn_tag,
        DaqifiOutMessage_device_hw_rev_tag,
        DaqifiOutMessage_device_fw_rev_tag,
        DaqifiOutMessage_device_sn_tag,
    }
};

/**
 * Helper function to allow us to know which user interface the command originated from
 */
static microrl_t* SCPI_GetMicroRLClient(scpi_t* context) {
    UsbCdcData_t * pRunTimeUsbSettings = UsbCdc_GetSettings();

    wifi_tcp_server_context_t * pRunTimeServerData = wifi_manager_GetTcpServerContext();

    if (&pRunTimeUsbSettings->scpiContext == context) {
        return &pRunTimeUsbSettings->console;
    } else if (&pRunTimeServerData->client.scpiContext == context) {
        return &pRunTimeServerData->client.console;

    }
    return NULL;
}

/**
 * Triggers a board reset 
 */
static scpi_result_t SCPI_Reset(scpi_t * context) {
    UNUSED(context);

    // TODO: Send a shutdown message. The purpose of this message is to shut down timers
    // and anything else we don't want to leave running when the board goes offline.
    // We'll need something similar for the bootloader- in that case we wont be re-initializing
    // the board right away so we don't want to leave some things (like streaming data) active.
    vTaskDelay(100); // be sure any pending flash or other operations complete before the reset
    RCON_SoftwareReset();
    return SCPI_RES_ERR; // If we get here, the reset didn't work
}

/**
 * Placeholder for un-implemented SCPI commands
 * @param context The SCPI context
 * @return always SCPI_RES_ERROR
 */
static scpi_result_t SCPI_NotImplemented(scpi_t * context) {
    context->interface->write(context, "Not Implemented!", 16);
    return SCPI_RES_ERR;
}

/**
 * Prints a list of available commands
 * (Forward declared so it can reference scpi_commands)
 * @param context The SCPI context
 * @return SCPI_RES_OK
 */
scpi_result_t SCPI_Help(scpi_t* context);

/**
 * SCPI Callback: Clears the settings saved in memory, but does not overwrite the current in-memory values
 * @return SCPI_RES_OK on success SCPI_RES_ERR on error
 */

static scpi_result_t SCPI_SysInfoGet(scpi_t * context) {
    int param1;
    tBoardData * pBoardData = BoardData_Get(BOARDDATA_ALL_DATA, 0);

    if (!SCPI_ParamInt32(context, &param1, FALSE)) {
        param1 = 0;
    }

    uint8_t buffer[DaqifiOutMessage_size];
    size_t count = Nanopb_Encode(
            pBoardData,
            (const NanopbFlagsArray *) &fields_info,
            buffer, DaqifiOutMessage_size);
    if (count < 1) {
        return SCPI_RES_ERR;
    }
    context->interface->write(context, (char*) buffer, count);
    return SCPI_RES_OK;
}

/**
 * Gets the system log
 * @param context
 * @return 
 */
static scpi_result_t SCPI_SysLogGet(scpi_t * context) {
    char buffer[128];

    size_t logSize = LogMessageCount();
    size_t i = 0;
    for (i = 0; i < logSize; ++i) {
        size_t messageSize = LogMessagePop((uint8_t*) buffer, 128);
        if (messageSize > 0) {
            context->interface->write(context, buffer, messageSize);
        }
    }

    return SCPI_RES_OK;
}

/**
 * Gets the external power status
 * @param context
 * @return 
 */
static scpi_result_t SCPI_BatteryStatusGet(scpi_t * context) {
    tPowerData *pPowerData = BoardData_Get(
            BOARDATA_POWER_DATA,
            0);
    SCPI_ResultInt32(context, (int) (pPowerData->externalPowerSource));
    return SCPI_RES_OK;
}

/**
 * Gets the battery level
 * @param context
 * @return 
 */
static scpi_result_t SCPI_BatteryLevelGet(scpi_t * context) {
    tPowerData *pPowerData = BoardData_Get(
            BOARDATA_POWER_DATA,
            0);
    SCPI_ResultInt32(context, (int) (pPowerData->chargePct));
    return SCPI_RES_OK;
}

/**
 * GEts the power state
 * @param context
 * @return 
 */
static scpi_result_t SCPI_GetPowerState(scpi_t * context) {
    tPowerData *pPowerData = BoardData_Get(
            BOARDATA_POWER_DATA,
            0);
    SCPI_ResultInt32(context, (int) (pPowerData->powerState));
    return SCPI_RES_OK;
}

/**
 * Sets the power state
 * @param context
 * @return 
 */

static scpi_result_t SCPI_SetPowerState(scpi_t * context) {
    int param1;


    tPowerData * pPowerData = BoardData_Get(
            BOARDATA_POWER_DATA,
            0);

    if (!SCPI_ParamInt32(context, &param1, TRUE)) {
        return SCPI_RES_ERR;
    }

    if (param1 != 0) {
        pPowerData->requestedPowerState = DO_POWER_UP;
        BoardData_Set(
                BOARDATA_POWER_DATA,
                0,
                pPowerData);
    } else {
        pPowerData->requestedPowerState = DO_POWER_DOWN;
        BoardData_Set(
                BOARDATA_POWER_DATA,
                0,
                pPowerData);
    }

    return SCPI_RES_OK;
}

static scpi_result_t SCPI_ClearStreamStats(scpi_t * context) {
    //memset(commTest.stats,0, sizeof(commTest.stats));
    return SCPI_RES_OK;
}

scpi_result_t SCPI_GetStreamStats(scpi_t * context) {
    //    SCPI_ResultInt32(context, commTest.stats[0]);
    //    SCPI_ResultInt32(context, commTest.stats[1]);
    //    SCPI_ResultInt32(context, commTest.stats[2]);
    //    SCPI_ResultInt32(context, commTest.stats[3]);

    return SCPI_RES_OK;
}

static scpi_result_t SCPI_StartStreaming(scpi_t * context) {
    int32_t freq;

    StreamingRuntimeConfig * pRunTimeStreamConfig = BoardRunTimeConfig_Get(
            BOARDRUNTIME_STREAMING_CONFIGURATION);
    const tBoardConfig * pBoardConfig = BoardConfig_Get(
            BOARDCONFIG_ALL_CONFIG, 0);
    volatile AInRuntimeArray * pRuntimeAInChannels = BoardRunTimeConfig_Get(BOARDRUNTIMECONFIG_AIN_CHANNELS);
    volatile AInArray *pBoardConfigADC = BoardConfig_Get(BOARDCONFIG_AIN_CHANNELS, 0);

    // timer running frequency
    uint32_t clkFreq = TimerApi_FrequencyGet(pBoardConfig->StreamingConfig.TimerIndex);

    //int i;
    uint16_t activeType1ChannelCount = 0;
    int i;
    for (i = 0; i < pBoardConfigADC->Size; i++) {
        if (pRuntimeAInChannels->Data[i].IsEnabled == 1) {
            if (pBoardConfigADC->Data[i].Config.MC12b.ChannelType == 1) {
                activeType1ChannelCount++;
            }
        }
    }

    if (SCPI_ParamInt32(context, &freq, FALSE)) {
        if (freq >= 1 && freq <= 15000)///TODO: Test higher throughput
        {
            /**
             * The maximum aggregate trigger frequency for all active Type 1 ADC channels is 15,000 Hz.
             * For example, if two Type 1 channels are active, each can trigger at a maximum frequency of 7,500 Hz (15,000 / 2).
             * 
             * The maximum triggering frequency of non type 1 channel is 1000 hz, 
             * which is obtained by dividing Frequency with ChannelScanFreqDiv. 
             * Non-Type 1 channels are setup for channel scanning
             * 
             */
            if (activeType1ChannelCount > 0 && (freq * activeType1ChannelCount) > 15000) {
                freq = 15000 / activeType1ChannelCount;
            }
            pRunTimeStreamConfig->ClockPeriod = clkFreq / freq;
            pRunTimeStreamConfig->Frequency = freq;
            pRunTimeStreamConfig->TSClockPeriod = 0xFFFFFFFF;
            if (freq > 1000) {
                pRunTimeStreamConfig->ChannelScanFreqDiv = freq / 1000;
            } else {
                pRunTimeStreamConfig->ChannelScanFreqDiv = 1;
            }
        } else {
            return SCPI_RES_ERR;
        }
    } else {
        //No freq given just stream with the current value
    }

    Streaming_UpdateState();
    pRunTimeStreamConfig->IsEnabled = true;
    return SCPI_RES_OK;
}

static scpi_result_t SCPI_StopStreaming(scpi_t * context) {
    StreamingRuntimeConfig * pRunTimeStreamConfig = BoardRunTimeConfig_Get(
            BOARDRUNTIME_STREAMING_CONFIGURATION);

    pRunTimeStreamConfig->IsEnabled = false;

    Streaming_UpdateState();

    return SCPI_RES_OK;
}

static scpi_result_t SCPI_IsStreaming(scpi_t * context) {
    StreamingRuntimeConfig * pRunTimeStreamConfig = BoardRunTimeConfig_Get(
            BOARDRUNTIME_STREAMING_CONFIGURATION);

    SCPI_ResultInt32(context, (int) pRunTimeStreamConfig->IsEnabled);
    return SCPI_RES_OK;
}

static scpi_result_t SCPI_SetStreamFormat(scpi_t * context) {
    int param1;

    StreamingRuntimeConfig * pRunTimeStreamConfig = BoardRunTimeConfig_Get(
            BOARDRUNTIME_STREAMING_CONFIGURATION);

    if (!SCPI_ParamInt32(context, &param1, TRUE)) {
        return SCPI_RES_ERR;
    }

    if (param1 == Streaming_ProtoBuffer) {
        pRunTimeStreamConfig->Encoding = Streaming_ProtoBuffer;
    } else if (param1 == Streaming_Json) {
        pRunTimeStreamConfig->Encoding = Streaming_Json;
    }

    return SCPI_RES_OK;
}

static scpi_result_t SCPI_GetStreamFormat(scpi_t * context) {
    StreamingRuntimeConfig * pRunTimeStreamConfig = BoardRunTimeConfig_Get(
            BOARDRUNTIME_STREAMING_CONFIGURATION);

    SCPI_ResultInt32(context, (int) pRunTimeStreamConfig->Encoding);
    return SCPI_RES_OK;
}

static scpi_result_t SCPI_GetEcho(scpi_t * context) {
    microrl_t* console;
    console = SCPI_GetMicroRLClient(context);
    SCPI_ResultInt32(context, (int) console->echoOn);
    return SCPI_RES_OK;
}

static scpi_result_t SCPI_SetEcho(scpi_t * context) {
    int param1;
    microrl_t* console;
    if (!SCPI_ParamInt32(context, &param1, TRUE)) {
        return SCPI_RES_ERR;
    }
    if (param1<-1 || param1 > 1) {
        return SCPI_RES_ERR;
    }
    console = SCPI_GetMicroRLClient(context);
    if (console == NULL)
        return SCPI_RES_ERR;
    microrl_set_echo(console, param1);
    return SCPI_RES_OK;
}
//
//static scpi_result_t SCPI_NVMRead(scpi_t * context)
//{
//    int param1;
//    if (!SCPI_ParamInt32(context, &param1, TRUE))
//    {
//        return SCPI_RES_ERR;
//    }
//    SCPI_ResultUInt32Base(context, ReadfromAddr((uint32_t)param1), 16);
//    return SCPI_RES_OK;
//}
//
//static scpi_result_t SCPI_NVMWrite(scpi_t * context)
//{
//    uint32_t param1, param2;
//    bool status = false;
//    if (!SCPI_ParamUInt32(context, &param1, TRUE))
//    {
//        return SCPI_RES_ERR;
//    }
//    if (!SCPI_ParamUInt32(context, &param2, TRUE))
//    {
//        return SCPI_RES_ERR;
//    }
//    status = WriteWordtoAddr(param1, param2);
//    if (status)
//    {
//        return SCPI_RES_OK;
//    }else
//    {
//        return SCPI_RES_ERR;
//    }
//}
//
//static scpi_result_t SCPI_NVMErasePage(scpi_t * context)
//{
//    uint32_t param1;
//    bool status = false;
//    if (!SCPI_ParamUInt32(context, &param1, TRUE))
//    {
//        return SCPI_RES_ERR;
//    }
//    
//    status = ErasePage(param1);
//    if (status)
//    {
//        return SCPI_RES_OK;
//    }else
//    {
//        return SCPI_RES_ERR;
//    }
//}
//
//

scpi_result_t SCPI_ForceBootloader(scpi_t * context) {
    force_bootloader_flag = FORCE_BOOTLOADER_FLAG_VALUE; // magic force boot value!

    vTaskDelay(10); // Be sure all operations are finished

    RCON_SoftwareReset();
    return SCPI_RES_ERR; // If we get here, the reset didn't work
}

scpi_result_t SCPI_UsbSetTransparentMode(scpi_t * context) {
    int param1;
    if (!SCPI_ParamInt32(context, &param1, TRUE)) {
        return SCPI_RES_ERR;
    }
    if (param1 == 0) {
        UsbCdc_SetTransparentMode(false);
    } else {
        UsbCdc_SetTransparentMode(true);
    }
    return SCPI_RES_OK;
}

scpi_result_t SCPI_GetSerialNumber(scpi_t * context) {
    tBoardConfig * pBoardConfig = BoardConfig_Get(
            BOARDCONFIG_ALL_CONFIG,
            0);

    SCPI_ResultUInt64Base(context, pBoardConfig->boardSerialNumber, 16);
    return SCPI_RES_OK;
}

scpi_result_t SCPI_Force5v5PowerStateSet(scpi_t * context) {
    tBoardRuntimeConfig * pBoardRuntimeConfig = BoardRunTimeConfig_Get(
            BOARDRUNTIMECONFIG_ALL_CONFIG);

    tPowerData * pPowerData = BoardData_Get(
            BOARDATA_POWER_DATA,
            0);

    uint32_t param1;

    if (!SCPI_ParamUInt32(context, &param1, TRUE)) {
        return SCPI_RES_ERR;
    }
    if (pPowerData->powerState != POWERED_UP) {
        return SCPI_RES_ERR;
    }
    if (param1)
        pBoardRuntimeConfig->PowerWriteVars.EN_5_10V_Val = 1;
    else
        pBoardRuntimeConfig->PowerWriteVars.EN_5_10V_Val = 0;
    Power_Write();
    return SCPI_RES_OK;
}

//scpi_result_t SCPI_GetFreeRtosStats(scpi_t * context)
//{
//    char* pcWriteBuffer;
//    int len;
//    
//    pcWriteBuffer = pvPortMalloc(1000);
//    
//    if(pcWriteBuffer!=NULL){
//        // generate run-time stats string into the buffer
//        vTaskGetRunTimeStats(pcWriteBuffer);
//        
//        len = strlen(pcWriteBuffer);
//        if (len > 0){
//            context->interface->write(context, pcWriteBuffer,len);
//        }
//        
//        vPortFree(pcWriteBuffer);
//    }
//    return SCPI_RES_OK;
//}

static const scpi_command_t scpi_commands[] = {
    // Build into libscpi
    {.pattern = "*CLS", .callback = SCPI_CoreCls,},
    {.pattern = "*ESE", .callback = SCPI_CoreEse,},
    {.pattern = "*ESE?", .callback = SCPI_CoreEseQ,},
    {.pattern = "*ESR?", .callback = SCPI_CoreEsrQ,},
    {.pattern = "*IDN?", .callback = SCPI_CoreIdnQ,},
    {.pattern = "*OPC", .callback = SCPI_CoreOpc,},
    {.pattern = "*OPC?", .callback = SCPI_CoreOpcQ,},
    {.pattern = "*RST", .callback = SCPI_CoreRst,},
    {.pattern = "*SRE", .callback = SCPI_CoreSre,},
    {.pattern = "*SRE?", .callback = SCPI_CoreSreQ,},
    {.pattern = "*STB?", .callback = SCPI_CoreStbQ,},
    {.pattern = "*TST?", .callback = SCPI_CoreTstQ,},
    {.pattern = "*WAI", .callback = SCPI_CoreWai,},
    {.pattern = "SYSTem:ERRor?", .callback = SCPI_SystemErrorNextQ,},
    {.pattern = "SYSTem:ERRor:NEXT?", .callback = SCPI_SystemErrorNextQ,},
    {.pattern = "SYSTem:ERRor:COUNt?", .callback = SCPI_SystemErrorCountQ,},
    {.pattern = "SYSTem:VERSion?", .callback = SCPI_SystemVersionQ,},
    {.pattern = "STATus:QUEStionable?", .callback = SCPI_StatusQuestionableEventQ,},
    {.pattern = "STATus:QUEStionable:EVENt?", .callback = SCPI_StatusQuestionableEventQ,},
    {.pattern = "STATus:QUEStionable:ENABle", .callback = SCPI_StatusQuestionableEnable,},
    {.pattern = "STATus:QUEStionable:ENABle?", .callback = SCPI_StatusQuestionableEnableQ,},
    {.pattern = "STATus:PRESet", .callback = SCPI_StatusPreset,},

    //    // System
    {.pattern = "SYSTem:REboot", .callback = SCPI_Reset,},
    {.pattern = "HELP", .callback = SCPI_Help,},
    {.pattern = "SYSTem:SYSInfoPB?", .callback = SCPI_SysInfoGet,},
    {.pattern = "SYSTem:LOG?", .callback = SCPI_SysLogGet,},
    {.pattern = "SYSTem:ECHO", .callback = SCPI_SetEcho,},
    {.pattern = "SYSTem:ECHO?", .callback = SCPI_GetEcho,},
    //    {.pattern = "SYSTem:NVMRead?", .callback = SCPI_NVMRead, },  
    //    {.pattern = "SYSTem:NVMWrite", .callback = SCPI_NVMWrite, }, 
    //    {.pattern = "SYSTem:NVMErasePage", .callback = SCPI_NVMErasePage, },
    {.pattern = "SYSTem:FORceBoot", .callback = SCPI_ForceBootloader,},
    {.pattern = "SYSTem:USB:SetTransparentMode", .callback = SCPI_UsbSetTransparentMode},
    {.pattern = "SYSTem:SERialNUMber?", .callback = SCPI_GetSerialNumber,},

    //    // Intentionally(?) not implemented (stubbed out in original firmware))
    //    {.pattern = "STATus:OPERation?", .callback = SCPI_NotImplemented, },
    //    {.pattern = "STATus:OPERation:EVENt?", .callback = SCPI_NotImplemented, },
    //    {.pattern = "STATus:OPERation:CONDition?", .callback = SCPI_NotImplemented, },
    //    {.pattern = "STATus:OPERation:ENABle", .callback = SCPI_NotImplemented, },
    //    {.pattern = "STATus:OPERation:ENABle?", .callback = SCPI_NotImplemented, },
    //    {.pattern = "STATus:QUEStionable:CONDition?", .callback = SCPI_NotImplemented, },
    //    {.pattern = "SYSTem:COMMunication:TCPIP:CONTROL?", .callback = SCPI_NotImplemented, },

    // Power
    {.pattern = "SYSTem:BAT:STAT?", .callback = SCPI_BatteryStatusGet,},
    {.pattern = "SYSTem:BAT:LEVel?", .callback = SCPI_BatteryLevelGet,},
    {.pattern = "SYSTem:POWer:STATe?", .callback = SCPI_GetPowerState,},
    {.pattern = "SYSTem:POWer:STATe", .callback = SCPI_SetPowerState,},
    {.pattern = "SYSTem:FORce5V5POWer:STATe", .callback = SCPI_Force5v5PowerStateSet},

    // DIO
    {.pattern = "DIO:PORt:DIRection", .callback = SCPI_GPIODirectionSet,},
    {.pattern = "DIO:PORt:DIRection?", .callback = SCPI_GPIODirectionGet,},
    {.pattern = "DIO:PORt:STATe", .callback = SCPI_GPIOStateSet,},
    {.pattern = "DIO:PORt:STATe?", .callback = SCPI_GPIOStateGet,},
    {.pattern = "DIO:PORt:ENAble", .callback = SCPI_GPIOEnableSet,},
    {.pattern = "DIO:PORt:ENAble?", .callback = SCPI_GPIOEnableGet,},
    {.pattern = "PWM:CHannel:ENable", .callback = SCPI_PWMChannelEnableSet,},
    {.pattern = "PWM:CHannel:ENable?", .callback = SCPI_PWMChannelEnableGet,},
    {.pattern = "PWM:CHannel:FREQuency", .callback = SCPI_PWMChannelFrequencySet,},
    {.pattern = "PWM:CHannel:FREQuency?", .callback = SCPI_PWMChannelFrequencyGet,},
    {.pattern = "PWM:CHannel:DUTY", .callback = SCPI_PWMChannelDUTYSet,},
    {.pattern = "PWM:CHannel:DUTY?", .callback = SCPI_PWMChannelDUTYGet,},
    //    // Wifi
    {.pattern = "SYSTem:COMMunicate:LAN:ENAbled?", .callback = SCPI_LANEnabledGet,},
    {.pattern = "SYSTem:COMMunicate:LAN:ENAbled", .callback = SCPI_LANEnabledSet,},
    {.pattern = "SYSTem:COMMunicate:LAN:NETType?", .callback = SCPI_LANNetModeGet,},
    //    {.pattern = "SYSTem:COMMunicate:LAN:AvNETType?", .callback = SCPI_LANAVNetTypeGet, },
    {.pattern = "SYSTem:COMMunicate:LAN:NETType", .callback = SCPI_LANNetModeSet,},
    //    {.pattern = "SYSTem:COMMunicate:LAN:IPV6?", .callback = SCPI_LANIpv6Get, },
    //    {.pattern = "SYSTem:COMMunicate:LAN:IPV6", .callback = SCPI_LANIpv6Set, },
    {.pattern = "SYSTem:COMMunicate:LAN:ADDRess?", .callback = SCPI_LANAddrGet,},
    {.pattern = "SYSTem:COMMunicate:LAN:ADDRess", .callback = SCPI_LANAddrSet,},
    {.pattern = "SYSTem:COMMunicate:LAN:MASK?", .callback = SCPI_LANMaskGet,},
    {.pattern = "SYSTem:COMMunicate:LAN:MASK", .callback = SCPI_LANMaskSet,},
    {.pattern = "SYSTem:COMMunicate:LAN:GATEway?", .callback = SCPI_LANGatewayGet,},
    {.pattern = "SYSTem:COMMunicate:LAN:GATEway", .callback = SCPI_NotImplemented,},
    //    {.pattern = "SYSTem:COMMunicate:LAN:DNS1?", .callback = SCPI_LANDns1Get, },
    {.pattern = "SYSTem:COMMunicate:LAN:DNS1", .callback = SCPI_NotImplemented,},
    {.pattern = "SYSTem:COMMunicate:LAN:DNS2?", .callback = SCPI_NotImplemented,},
    {.pattern = "SYSTem:COMMunicate:LAN:DNS2", .callback = SCPI_NotImplemented,},
    {.pattern = "SYSTem:COMMunicate:LAN:MAC?", .callback = SCPI_LANMacGet,},
    {.pattern = "SYSTem:COMMunicate:LAN:MAC", .callback = SCPI_NotImplemented,},
    {.pattern = "SYSTem:COMMunicate:LAN:CONnected?", .callback = SCPI_NotImplemented,},
    {.pattern = "SYSTem:COMMunicate:LAN:HOST?", .callback = SCPI_LANHostnameGet,},
    {.pattern = "SYSTem:COMMunicate:LAN:HOST", .callback = SCPI_NotImplemented,},
    {.pattern = "SYSTem:COMMunicate:LAN:FWUpdate", .callback = SCPI_LANFwUpdate,},
    //    {.pattern = "SYSTem:COMMunicate:LAN:AvSSIDScan", .callback = SCPI_LANAVSsidScan, },
    //    {.pattern = "SYSTem:COMMunicate:LAN:AvSSID?", .callback = SCPI_LANAVSsidGet, },
    //    {.pattern = "SYSTem:COMMunicate:LAN:AvSSIDStr?", .callback = SCPI_LANAVSsidStrengthGet, },
    {.pattern = "SYSTem:COMMunicate:LAN:SSID?", .callback = SCPI_LANSsidGet,},
    {.pattern = "SYSTem:COMMunicate:LAN:SSIDStr?", .callback = SCPI_LANSsidStrengthGet,},
    {.pattern = "SYSTem:COMMunicate:LAN:SSID", .callback = SCPI_LANSsidSet,},
    {.pattern = "SYSTem:COMMunicate:LAN:SECurity?", .callback = SCPI_LANSecurityGet,},
    //    {.pattern = "SYSTem:COMMunicate:LAN:AvSECurity?", .callback = SCPI_LANAVSecurityGet, },
    {.pattern = "SYSTem:COMMunicate:LAN:SECurity", .callback = SCPI_LANSecuritySet,},
    {.pattern = "SYSTem:COMMunicate:LAN:PASs", .callback = SCPI_LANPasskeySet,}, // No get for security reasons use PASSCHECK instead
    {.pattern = "SYSTem:COMMunicate:LAN:PASSCHECK", .callback = SCPI_LANPasskeyGet,},
    {.pattern = "SYSTem:COMMunicate:LAN:DISPlay", .callback = SCPI_NotImplemented,},
    {.pattern = "SYSTem:COMMunicate:LAN:APPLY", .callback = SCPI_LANSettingsApply,},
    {.pattern = "SYSTem:COMMunicate:LAN:LOAD", .callback = SCPI_LANSettingsLoad,},
    {.pattern = "SYSTem:COMMunicate:LAN:SAVE", .callback = SCPI_LANSettingsSave,},
    {.pattern = "SYSTem:COMMunicate:LAN:FACRESET", .callback = SCPI_LANSettingsFactoryLoad,},
    {.pattern = "SYSTem:COMMunicate:LAN:CLEAR", .callback = SCPI_LANSettingsClear,},
    //
    {.pattern = "SYSTem:COMMunicate:LAN:GETChipInfo", .callback = SCPI_LANGetChipInfo,},
    // ADC
    {.pattern = "MEASure:VOLTage:DC?", .callback = SCPI_ADCVoltageGet,},
    {.pattern = "ENAble:VOLTage:DC", .callback = SCPI_ADCChanEnableSet,},
    {.pattern = "ENAble:VOLTage:DC?", .callback = SCPI_ADCChanEnableGet,},
    {.pattern = "SOURce:VOLTage:LEVel", .callback = SCPI_NotImplemented,},
    {.pattern = "CONFigure:ADC:SINGleend", .callback = SCPI_ADCChanSingleEndSet,},
    {.pattern = "CONFigure:ADC:SINGleend?", .callback = SCPI_ADCChanSingleEndGet,},
    {.pattern = "CONFigure:ADC:RANGe", .callback = SCPI_ADCChanRangeSet,},
    {.pattern = "CONFigure:ADC:RANGe?", .callback = SCPI_ADCChanRangeGet,},
    {.pattern = "CONFigure:ADC:CHANnel", .callback = SCPI_ADCChanEnableSet,},
    {.pattern = "CONFigure:ADC:CHANnel?", .callback = SCPI_ADCChanEnableGet,},
    {.pattern = "CONFigure:ADC:chanCALM", .callback = SCPI_ADCChanCalmSet,},
    {.pattern = "CONFigure:ADC:chanCALB", .callback = SCPI_ADCChanCalbSet,},
    {.pattern = "CONFigure:ADC:chanCALM?", .callback = SCPI_ADCChanCalmGet,},
    {.pattern = "CONFigure:ADC:chanCALB?", .callback = SCPI_ADCChanCalbGet,},
    {.pattern = "CONFigure:ADC:SAVEcal", .callback = SCPI_ADCCalSave,},
    {.pattern = "CONFigure:ADC:SAVEFcal", .callback = SCPI_ADCCalFSave,},
    {.pattern = "CONFigure:ADC:LOADcal", .callback = SCPI_ADCCalLoad,},
    {.pattern = "CONFigure:ADC:LOADFcal", .callback = SCPI_ADCCalFLoad,},
    {.pattern = "CONFigure:ADC:USECal", .callback = SCPI_ADCUseCalSet,},
    {.pattern = "CONFigure:ADC:USECal?", .callback = SCPI_ADCUseCalGet,},
    //    
    //    // SPI
    //    {.pattern = "OUTPut:SPI:WRIte", .callback = SCPI_NotImplemented, },
    //    
    // Streaming
    {.pattern = "SYSTem:StartStreamData", .callback = SCPI_StartStreaming,},
    {.pattern = "SYSTem:StopStreamData", .callback = SCPI_StopStreaming,},
    {.pattern = "SYSTem:StreamData?", .callback = SCPI_IsStreaming,},
    {.pattern = "SYSTem:STReam:FORmat", .callback = SCPI_SetStreamFormat,}, // 0 = pb = default, 1 = text (json)
    {.pattern = "SYSTem:STReam:FORmat?", .callback = SCPI_GetStreamFormat,},
    {.pattern = "SYSTem:STReam:Stats?", .callback = SCPI_GetStreamStats,},
    {.pattern = "SYSTem:STReam:ClearStats", .callback = SCPI_ClearStreamStats,},
    //
    {.pattern = "SYSTem:STORage:SD:LOGging", .callback = SCPI_StorageSDLoggingSet,},
    {.pattern = "SYSTem:STORage:SD:GET", .callback = SCPI_StorageSDGetData},
    {.pattern = "SYSTem:STORage:SD:LISt?", .callback = SCPI_StorageSDListDir},
    {.pattern = "SYSTem:STORage:SD:ENAble", .callback = SCPI_StorageSDEnableSet},
    // FreeRTOS
    //{.pattern = "SYSTem:OS:Stats?",           .callback = SCPI_GetFreeRtosStats,},
    // Testing
    {.pattern = "BENCHmark?", .callback = SCPI_NotImplemented,},
    {.pattern = NULL, .callback = SCPI_NotImplemented,},
};

#define SCPI_INPUT_BUFFER_LENGTH 64
#define SCPI_ERROR_QUEUE_SIZE 17
char scpi_input_buffer[SCPI_INPUT_BUFFER_LENGTH];
scpi_error_t scpi_error_queue_data[SCPI_ERROR_QUEUE_SIZE];

scpi_result_t SCPI_Help(scpi_t* context) {
    char buffer[512];
    size_t numCommands = sizeof (scpi_commands) / sizeof (scpi_command_t);
    size_t i = 0;

    size_t count = snprintf(buffer, 512, "%s", "\r\nImplemented:\r\n");
    for (i = 0; i < numCommands; ++i) {
        if (scpi_commands[i].callback != SCPI_NotImplemented &&
                scpi_commands[i].pattern != NULL) {
            size_t cmdSize = strlen(scpi_commands[i].pattern) + 5;
            if (count + cmdSize >= 512) {
                buffer[count] = '\0';
                context->interface->write(context, buffer, count);
                count = 0;
            }

            count += snprintf(buffer + count, 512 - count, "  %s\r\n", scpi_commands[i].pattern);
        }
    }

    if (count > 0) {
        context->interface->write(context, buffer, count);
    }

    count = snprintf(buffer, 512, "%s", "\r\nNot Implemented:\r\n");
    for (i = 0; i < numCommands; ++i) {
        if (scpi_commands[i].callback == SCPI_NotImplemented &&
                scpi_commands[i].pattern != NULL) {
            size_t cmdSize = strlen(scpi_commands[i].pattern) + 5;
            if (count + cmdSize >= 512) {
                buffer[count] = '\0';
                context->interface->write(context, buffer, count);
                count = 0;
            }

            count += snprintf(buffer + count, 512 - count, "  %s\r\n", scpi_commands[i].pattern);
        }
    }

    if (count > 0) {
        context->interface->write(context, buffer, count);
    }

    return SCPI_RES_OK;
}

scpi_t CreateSCPIContext(scpi_interface_t* interface, void* user_context) {
    // Create a context
    scpi_t daqifiScpiContext;
    // Init context
    SCPI_Init(&daqifiScpiContext,
            scpi_commands,
            interface,
            scpi_units_def,
            SCPI_IDN1, SCPI_IDN2, SCPI_IDN3, SCPI_IDN4,
            scpi_input_buffer, SCPI_INPUT_BUFFER_LENGTH,
            scpi_error_queue_data, SCPI_ERROR_QUEUE_SIZE);

    // Return it to the app
    return daqifiScpiContext;
}
