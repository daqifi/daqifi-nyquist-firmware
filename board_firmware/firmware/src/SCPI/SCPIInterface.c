#include "SCPIInterface.h"

// General
#include <stdlib.h>
#include <string.h>

// Harmony
#include "system_config.h"
#include "system_definitions.h"

// 3rd Party
#include "scpi/scpi.h"

#include "HAL/NVM/DaqifiSettings.h"
#include "HAL/Power/PowerApi.h"
#include "nanopb/DaqifiOutMessage.pb.h"
#include "nanopb/Encoder.h"
#include "Util/StringFormatters.h"
#include "Util/Logger.h"
#include "state/data/BoardData.h"
#include "state/board/BoardConfig.h"
#include "state/runtime/BoardRuntimeConfig.h"
#include "HAL/DIO.h"
#include "SCPI/SCPIADC.h"
#include "SCPI/SCPIDIO.h"
#include "SCPI/SCPILAN.h"
#include "streaming.h"

#define UNUSED(x) (void)(x)

#define SCPI_IDN1 "DAQiFi"
#define SCPI_IDN2 "Nq1"
#define SCPI_IDN3 NULL
#define SCPI_IDN4 "01-02"

// These are now defined in the gcc configuration
//#define FORCE_BOOTLOADER_FLAG_ADDR 0x8007FFE0 
//#define FORCE_BOOTLOADER_FLAG_VALUE 0x04CEB007

// Declare force bootloader RAM flag location
volatile uint32_t force_bootloader_flag __attribute__((persistent, coherent, address(FORCE_BOOTLOADER_FLAG_ADDR)));

const NanopbFlagsArray fields_all = {
    .Size = 65,
    .Data={
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
    .Data={
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
    .Data={
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
static microrl_t* SCPI_GetMicroRLClient(scpi_t* context)
{
    
    if (&g_BoardRuntimeConfig.usbSettings.scpiContext == context)
        {
             return &g_BoardRuntimeConfig.usbSettings.console;
        }
    
    uint8_t i = 0;
    for (i=0; i<WIFI_MAX_CLIENT; ++i)
    {
        if (&g_BoardRuntimeConfig.serverData.clients[i].scpiContext == context)
        {
             return &g_BoardRuntimeConfig.serverData.clients[i].console;
        }
    }
    return NULL;
}

/**
 * Triggers a board reset 
 */
static scpi_result_t SCPI_Reset(scpi_t * context)
{
    UNUSED(context);
    
    // TODO: Send a shutdown message. The purpose of this message is to shut down timers
    // and anything else we don't want to leave running when the board goes offline.
    // We'll need something similar for the bootloader- in that case we wont be re-initializing
    // the board right away so we don't want to leave some things (like streaming data) active.
    vTaskDelay(100);    // be sure any pending flash or other operations complete before the reset
    SYS_RESET_SoftwareReset();
    return SCPI_RES_ERR; // If we get here, the reset didn't work
}

/**
 * Placeholder for un-implemented SCPI commands
 * @param context The SCPI context
 * @return always SCPI_RES_ERROR
 */
static scpi_result_t SCPI_NotImplemented(scpi_t * context)
{
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
static scpi_result_t SCPI_SysInfoGet(scpi_t * context)
{
    int param1;
    if (!SCPI_ParamInt32(context, &param1, FALSE))
    {
        param1=0;
    }
    
    uint8_t buffer[DaqifiOutMessage_size];
    size_t count = Nanopb_Encode(&g_BoardData, (const)&fields_info, buffer, DaqifiOutMessage_size);
    if (count < 1)
    {
        return SCPI_RES_ERR;
    }
    
    context->interface->write(context, (char*)buffer, count);
    return SCPI_RES_OK;
}

/**
 * Gets the system log
 * @param context
 * @return 
 */
static scpi_result_t SCPI_SysLogGet(scpi_t * context)
{
    char buffer[128];
     
    size_t logSize = LogMessageCount();
    size_t i = 0;
    for (i=0; i<logSize; ++i)
    {
        size_t messageSize = LogMessagePop((uint8_t*)buffer, 128);
        if (messageSize > 0)
        {
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
static scpi_result_t SCPI_BatteryStatusGet(scpi_t * context)
{
    SCPI_ResultInt32(context, (int)g_BoardData.PowerData.externalPowerSource);
    return SCPI_RES_OK;
}

/**
 * Gets the battery level
 * @param context
 * @return 
 */
static scpi_result_t SCPI_BatteryLevelGet(scpi_t * context)
{
    SCPI_ResultInt32(context, (int)g_BoardData.PowerData.chargePct);
    return SCPI_RES_OK;
}

/**
 * GEts the power state
 * @param context
 * @return 
 */
static scpi_result_t SCPI_GetPowerState(scpi_t * context)
{
    SCPI_ResultInt32(context, (int)g_BoardData.PowerData.powerState);
    return SCPI_RES_OK;
}

/**
 * Sets the power state
 * @param context
 * @return 
 */
static scpi_result_t SCPI_SetPowerState(scpi_t * context)
{
    int param1;
    if (!SCPI_ParamInt32(context, &param1, TRUE))
    {
        return SCPI_RES_ERR;
    }
    
    if (param1 != 0)
    {
        g_BoardData.PowerData.powerState = DO_POWER_UP;
    }
    else
    {
        g_BoardData.PowerData.powerState = DO_POWER_DOWN;
    }
    
    return SCPI_RES_OK;
}

static scpi_result_t SCPI_StartStreaming(scpi_t * context)
{
    int32_t freq;
    uint32_t clkFreq = DRV_TMR_CounterFrequencyGet(g_BoardRuntimeConfig.StreamingConfig.TimerHandle);  // timer running frequency
    
    if (SCPI_ParamInt32(context, &freq, FALSE))
    {
        if (freq >= 1 && freq <= 1000)
        {
            g_BoardRuntimeConfig.StreamingConfig.ClockDivider = clkFreq / freq; // calculate the divider needed
            g_BoardRuntimeConfig.StreamingConfig.TSClockDivider = 0xFFFFFFFF; // Set timer to maximum period
        }
        else
        {
            return SCPI_RES_ERR;
        }
    }
    else
    {
        //No freq given just stream with the current value
    }
    
    Streaming_UpdateState(&g_BoardConfig, &g_BoardRuntimeConfig);
    g_BoardRuntimeConfig.StreamingConfig.IsEnabled = true;
    return SCPI_RES_OK;
}

static scpi_result_t SCPI_StopStreaming(scpi_t * context)
{
    g_BoardRuntimeConfig.StreamingConfig.IsEnabled = false;
    
    Streaming_UpdateState(&g_BoardConfig, &g_BoardRuntimeConfig);
    
    return SCPI_RES_OK;
}

static scpi_result_t SCPI_IsStreaming(scpi_t * context)
{
    SCPI_ResultInt32(context, (int)g_BoardRuntimeConfig.StreamingConfig.IsEnabled);
    return SCPI_RES_OK;
}

static scpi_result_t SCPI_SetStreamFormat(scpi_t * context)
{
    int param1;
    if (!SCPI_ParamInt32(context, &param1, TRUE))
    {
        return SCPI_RES_ERR;
    }
    
    if (param1 == 0)
    {
        g_BoardRuntimeConfig.StreamingConfig.Encoding = Streaming_ProtoBuffer;
    }
    else
    {
        g_BoardRuntimeConfig.StreamingConfig.Encoding = Streaming_Json;
    }
    
    return SCPI_RES_OK;
}

static scpi_result_t SCPI_GetStreamFormat(scpi_t * context)
{
    SCPI_ResultInt32(context, (int)g_BoardRuntimeConfig.StreamingConfig.Encoding);
    return SCPI_RES_OK;
}

static scpi_result_t SCPI_GetEcho(scpi_t * context)
{
    microrl_t* console;
    console = SCPI_GetMicroRLClient(context);
    SCPI_ResultInt32(context, (int)console->echoOn);
    return SCPI_RES_OK;
}

static scpi_result_t SCPI_SetEcho(scpi_t * context)
{
    int param1;
    microrl_t* console;
    if (!SCPI_ParamInt32(context, &param1, TRUE))
    {
        return SCPI_RES_ERR;
    }
    if (param1<-1 || param1>1)
    {
        return SCPI_RES_ERR;
    }
    console = SCPI_GetMicroRLClient(context);
    microrl_set_echo(console, param1);
    return SCPI_RES_OK;
}

static scpi_result_t SCPI_NVMRead(scpi_t * context)
{
    int param1;
    if (!SCPI_ParamInt32(context, &param1, TRUE))
    {
        return SCPI_RES_ERR;
    }
    SCPI_ResultUInt32Base(context, ReadfromAddr((uint32_t)param1), 16);
    return SCPI_RES_OK;
}

static scpi_result_t SCPI_NVMWrite(scpi_t * context)
{
    uint32_t param1, param2;
    bool status = false;
    if (!SCPI_ParamUInt32(context, &param1, TRUE))
    {
        return SCPI_RES_ERR;
    }
    if (!SCPI_ParamUInt32(context, &param2, TRUE))
    {
        return SCPI_RES_ERR;
    }
    status = WriteWordtoAddr(param1, param2);
    if (status)
    {
        return SCPI_RES_OK;
    }else
    {
        return SCPI_RES_ERR;
    }
}

static scpi_result_t SCPI_NVMErasePage(scpi_t * context)
{
    uint32_t param1;
    bool status = false;
    if (!SCPI_ParamUInt32(context, &param1, TRUE))
    {
        return SCPI_RES_ERR;
    }
    
    status = ErasePage(param1);
    if (status)
    {
        return SCPI_RES_OK;
    }else
    {
        return SCPI_RES_ERR;
    }
}


scpi_result_t SCPI_ForceBootloader(scpi_t * context)
{
    force_bootloader_flag = FORCE_BOOTLOADER_FLAG_VALUE;   // magic force boot value!
    
    vTaskDelay(10);        // Be sure all operations are finished
    
    SYS_RESET_SoftwareReset();
    return SCPI_RES_ERR; // If we get here, the reset didn't work
}

scpi_result_t SCPI_GetSerialNumber(scpi_t * context)
{
    SCPI_ResultUInt64Base(context, g_BoardConfig.boardSerialNumber, 16);
    return SCPI_RES_OK;
}


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
    
    // System
    {.pattern = "SYSTem:REboot", .callback = SCPI_Reset, },
    {.pattern = "HELP", .callback = SCPI_Help, },
    {.pattern = "SYSTem:SYSInfoPB?", .callback = SCPI_SysInfoGet, },
    {.pattern = "SYSTem:LOG?", .callback = SCPI_SysLogGet, },
    {.pattern = "SYSTem:ECHO", .callback = SCPI_SetEcho, },
    {.pattern = "SYSTem:ECHO?", .callback = SCPI_GetEcho, },    
    {.pattern = "SYSTem:NVMRead?", .callback = SCPI_NVMRead, },  
    {.pattern = "SYSTem:NVMWrite", .callback = SCPI_NVMWrite, }, 
    {.pattern = "SYSTem:NVMErasePage", .callback = SCPI_NVMErasePage, },
    {.pattern = "SYSTem:FORceBoot", .callback = SCPI_ForceBootloader, },
    {.pattern = "SYSTem:SERialNUMber?", .callback = SCPI_GetSerialNumber, },
    
    // Intentionally(?) not implemented (stubbed out in original firmware))
    {.pattern = "STATus:OPERation?", .callback = SCPI_NotImplemented, },
    {.pattern = "STATus:OPERation:EVENt?", .callback = SCPI_NotImplemented, },
    {.pattern = "STATus:OPERation:CONDition?", .callback = SCPI_NotImplemented, },
    {.pattern = "STATus:OPERation:ENABle", .callback = SCPI_NotImplemented, },
    {.pattern = "STATus:OPERation:ENABle?", .callback = SCPI_NotImplemented, },
    {.pattern = "STATus:QUEStionable:CONDition?", .callback = SCPI_NotImplemented, },
    {.pattern = "SYSTem:COMMunication:TCPIP:CONTROL?", .callback = SCPI_NotImplemented, },
    
    // Power
    {.pattern = "SYSTem:BAT:STAT?", .callback = SCPI_BatteryStatusGet, },
    {.pattern = "SYSTem:BAT:LEVel?", .callback = SCPI_BatteryLevelGet, },
    {.pattern = "SYSTem:POWer:STATe?", .callback = SCPI_GetPowerState, },
    {.pattern = "SYSTem:POWer:STATe", .callback = SCPI_SetPowerState, },
    
    // DIO
    {.pattern = "DIO:PORt:DIRection", .callback = SCPI_GPIODirectionSet, },
    {.pattern = "DIO:PORt:DIRection?", .callback = SCPI_GPIODirectionGet, },
    {.pattern = "DIO:PORt:STATe", .callback = SCPI_GPIOStateSet, },
    {.pattern = "DIO:PORt:STATe?", .callback = SCPI_GPIOStateGet, },
    {.pattern = "DIO:PORt:ENAble", .callback = SCPI_GPIOEnableSet, },
    {.pattern = "DIO:PORt:ENAble?", .callback = SCPI_GPIOEnableGet, },
    
    // Wifi
    {.pattern = "SYSTem:COMMunicate:LAN:ENAbled?", .callback = SCPI_LANEnabledGet, },
    {.pattern = "SYSTem:COMMunicate:LAN:ENAbled", .callback = SCPI_LANEnabledSet, },
    {.pattern = "SYSTem:COMMunicate:LAN:NETType?", .callback = SCPI_LANNetTypeGet, },
    {.pattern = "SYSTem:COMMunicate:LAN:AvNETType?", .callback = SCPI_LANAVNetTypeGet, },
    {.pattern = "SYSTem:COMMunicate:LAN:NETType", .callback = SCPI_LANNetTypeSet, },
    {.pattern = "SYSTem:COMMunicate:LAN:IPV6?", .callback = SCPI_LANIpv6Get, },
    {.pattern = "SYSTem:COMMunicate:LAN:IPV6", .callback = SCPI_LANIpv6Set, },
    {.pattern = "SYSTem:COMMunicate:LAN:ADDRess?", .callback = SCPI_LANAddrGet, },
    {.pattern = "SYSTem:COMMunicate:LAN:ADDRess", .callback = SCPI_LANAddrSet, },
    {.pattern = "SYSTem:COMMunicate:LAN:MASK?", .callback = SCPI_LANMaskGet, },
    {.pattern = "SYSTem:COMMunicate:LAN:MASK", .callback = SCPI_LANMaskSet, },
    {.pattern = "SYSTem:COMMunicate:LAN:GATEway?", .callback = SCPI_LANGatewayGet, },
    {.pattern = "SYSTem:COMMunicate:LAN:GATEway", .callback = SCPI_LANGatewaySet, },
    {.pattern = "SYSTem:COMMunicate:LAN:DNS1?", .callback = SCPI_LANDns1Get, },
    {.pattern = "SYSTem:COMMunicate:LAN:DNS1", .callback = SCPI_LANDns1Set, },
    {.pattern = "SYSTem:COMMunicate:LAN:DNS2?", .callback = SCPI_LANDns2Get, },
    {.pattern = "SYSTem:COMMunicate:LAN:DNS2", .callback = SCPI_LANDns2Set, },
    {.pattern = "SYSTem:COMMunicate:LAN:MAC?", .callback = SCPI_LANMacGet, },
    {.pattern = "SYSTem:COMMunicate:LAN:MAC", .callback = SCPI_LANMacSet, },
    {.pattern = "SYSTem:COMMunicate:LAN:CONnected?", .callback = SCPI_NotImplemented, },
    {.pattern = "SYSTem:COMMunicate:LAN:HOST?", .callback = SCPI_LANHostnameGet, },
    {.pattern = "SYSTem:COMMunicate:LAN:HOST", .callback = SCPI_LANHostnameSet, },
    {.pattern = "SYSTem:COMMunicate:LAN:AvSSIDScan", .callback = SCPI_LANAVSsidScan, },
    {.pattern = "SYSTem:COMMunicate:LAN:AvSSID?", .callback = SCPI_LANAVSsidGet, },
    {.pattern = "SYSTem:COMMunicate:LAN:AvSSIDStr?", .callback = SCPI_LANAVSsidStrengthGet, },
    {.pattern = "SYSTem:COMMunicate:LAN:SSID?", .callback = SCPI_LANSsidGet, },
    {.pattern = "SYSTem:COMMunicate:LAN:SSIDStr?", .callback = SCPI_LANSsidStrengthGet, },
    {.pattern = "SYSTem:COMMunicate:LAN:SSID", .callback = SCPI_LANSsidSet, },
    {.pattern = "SYSTem:COMMunicate:LAN:SECurity?", .callback = SCPI_LANSecurityGet, },
    {.pattern = "SYSTem:COMMunicate:LAN:AvSECurity?", .callback = SCPI_LANAVSecurityGet, },
    {.pattern = "SYSTem:COMMunicate:LAN:SECurity", .callback = SCPI_LANSecuritySet, },
    {.pattern = "SYSTem:COMMunicate:LAN:PASs", .callback = SCPI_LANPasskeySet, }, // No get for security reasons use PASSCHECK instead
    {.pattern = "SYSTem:COMMunicate:LAN:PASSCHECK", .callback = SCPI_LANPasskeyCheck, },
    {.pattern = "SYSTem:COMMunicate:LAN:DISPlay", .callback = SCPI_NotImplemented, },
    {.pattern = "SYSTem:COMMunicate:LAN:APPLY", .callback = SCPI_LANSettingsApply, },
    {.pattern = "SYSTem:COMMunicate:LAN:LOAD", .callback = SCPI_LANSettingsLoad, },
    {.pattern = "SYSTem:COMMunicate:LAN:SAVE", .callback = SCPI_LANSettingsSave, },
    {.pattern = "SYSTem:COMMunicate:LAN:FACRESET", .callback = SCPI_LANSettingsFactoryLoad, },
    {.pattern = "SYSTem:COMMunicate:LAN:CLEAR", .callback = SCPI_LANSettingsClear, },
    
    // ADC
    {.pattern = "MEASure:VOLTage:DC?", .callback = SCPI_ADCVoltageGet, },
    {.pattern = "ENAble:VOLTage:DC", .callback = SCPI_ADCChanEnableSet, },
    {.pattern = "ENAble:VOLTage:DC?", .callback = SCPI_ADCChanEnableGet, },
    {.pattern = "SOURce:VOLTage:LEVel", .callback = SCPI_NotImplemented, },
    {.pattern = "CONFigure:ADC:SINGleend", .callback = SCPI_ADCChanSingleEndSet, },
    {.pattern = "CONFigure:ADC:SINGleend?", .callback = SCPI_ADCChanSingleEndGet, },
    {.pattern = "CONFigure:ADC:RANGe", .callback = SCPI_ADCChanRangeSet, },
    {.pattern = "CONFigure:ADC:RANGe?", .callback = SCPI_ADCChanRangeGet, },
    {.pattern = "CONFigure:ADC:CHANnel", .callback = SCPI_ADCChanEnableSet, },
    {.pattern = "CONFigure:ADC:CHANnel?", .callback = SCPI_ADCChanEnableGet, },
    {.pattern = "CONFigure:ADC:chanCALM", .callback = SCPI_ADCChanCalmSet, },
    {.pattern = "CONFigure:ADC:chanCALB", .callback = SCPI_ADCChanCalbSet, },
    {.pattern = "CONFigure:ADC:chanCALM?", .callback = SCPI_ADCChanCalmGet, },
    {.pattern = "CONFigure:ADC:chanCALB?", .callback = SCPI_ADCChanCalbGet, },
    {.pattern = "CONFigure:ADC:SAVEcal", .callback = SCPI_ADCCalSave, },
    {.pattern = "CONFigure:ADC:SAVEFcal", .callback = SCPI_ADCCalFSave, },
    {.pattern = "CONFigure:ADC:LOADcal", .callback = SCPI_ADCCalLoad, },
    {.pattern = "CONFigure:ADC:LOADFcal", .callback = SCPI_ADCCalFLoad, },
    {.pattern = "CONFigure:ADC:USECal", .callback = SCPI_ADCUseCalSet, },
    {.pattern = "CONFigure:ADC:USECal?", .callback = SCPI_ADCUseCalGet, },
    
    // SPI
    {.pattern = "OUTPut:SPI:WRIte", .callback = SCPI_NotImplemented, },
    
    // Streaming
    {.pattern = "SYSTem:StartStreamData", .callback = SCPI_StartStreaming, },
    {.pattern = "SYSTem:StopStreamData", .callback = SCPI_StopStreaming, },
    {.pattern = "SYSTem:StreamData?", .callback = SCPI_IsStreaming, },
    
    {.pattern = "SYSTem:STReam:FORmat", .callback = SCPI_SetStreamFormat, }, // 0 = pb = default, 1 = text (json)
    {.pattern = "SYSTem:STReam:FORmat?", .callback = SCPI_GetStreamFormat, },
    
    // Testing
    {.pattern = "BENCHmark?", .callback = SCPI_NotImplemented, },
    
    {.pattern = NULL, .callback = SCPI_NotImplemented, },
};

#define SCPI_INPUT_BUFFER_LENGTH 64
#define SCPI_ERROR_QUEUE_SIZE 17
char scpi_input_buffer[SCPI_INPUT_BUFFER_LENGTH];
scpi_error_t scpi_error_queue_data[SCPI_ERROR_QUEUE_SIZE];

scpi_result_t SCPI_Help(scpi_t* context)
{
    char buffer[512];
    size_t numCommands = sizeof(scpi_commands) / sizeof(scpi_command_t);
    size_t i = 0;
    
    size_t count = snprintf(buffer, 512, "%s", "\r\nImplemented:\r\n");
    for (i = 0; i< numCommands; ++i)
    {
        if (scpi_commands[i].callback != SCPI_NotImplemented &&
            scpi_commands[i].pattern != NULL)
        {
            size_t cmdSize = strlen(scpi_commands[i].pattern) + 5;
            if (count + cmdSize >= 512)
            {
                buffer[count] = '\0';
                context->interface->write(context, buffer, count);
                count = 0;
            }
            
            count += snprintf(buffer + count, 512 - count, "  %s\r\n", scpi_commands[i].pattern);
        }
    }
    
    if (count > 0)
    {
        context->interface->write(context, buffer, count);
    }
    
    count = snprintf(buffer, 512, "%s", "\r\nNot Implemented:\r\n");
    for (i = 0; i< numCommands; ++i)
    {
        if (scpi_commands[i].callback == SCPI_NotImplemented &&
            scpi_commands[i].pattern != NULL)
        {
            size_t cmdSize = strlen(scpi_commands[i].pattern) + 5;
            if (count + cmdSize >= 512)
            {
                buffer[count] = '\0';
                context->interface->write(context, buffer, count);
                count = 0;
            }
            
            count += snprintf(buffer + count, 512 - count, "  %s\r\n", scpi_commands[i].pattern);
        }
    }
    
    if (count > 0)
    {
        context->interface->write(context, buffer, count);
    }
    
    return SCPI_RES_OK;
}

scpi_t CreateSCPIContext(scpi_interface_t* interface, void* user_context)
{
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
