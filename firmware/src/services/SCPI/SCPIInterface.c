#define LOG_LVL LOG_LEVEL_SCPI

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
#include "HAL/Power/PowerApi.h"
#include "services/DaqifiPB/DaqifiOutMessage.pb.h"
#include "services/DaqifiPB/NanoPB_Encoder.h"
//#include "Util/StringFormatters.h"
#include "Util/Logger.h"
#include "state/data/BoardData.h"
#include "state/board/BoardConfig.h"
#include "services/daqifi_settings.h"
#include "state/runtime/BoardRuntimeConfig.h"
#include "state/board/AInConfig.h"  // For MAX_AIN_PUBLIC_CHANNELS
#include "peripheral/gpio/plib_gpio.h"
#include "HAL/BQ24297/BQ24297.h"
#include "HAL/ADC.h"
#include "HAL/DIO.h"
#include "SCPIADC.h"
#include "SCPIDAC.h" 
#include "SCPIDIO.h"
#include "SCPILAN.h"
#include "services/wifi_services/wifi_manager.h"
#include "SCPIStorageSD.h"
#include "../sd_card_services/sd_card_manager.h"
#include "../streaming.h"
#include "../csv_encoder.h"
#include "../JSON_Encoder.h"
#include "../../HAL/TimerApi/TimerApi.h"
#include "../UsbCdc/UsbCdc.h"

//
#define UNUSED(x) (void)(x)
//
#define SCPI_IDN1 "DAQiFi"
// SCPI_IDN2 (model) is constructed dynamically from BoardConfig.BoardVariant
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
 * Helper function to detect which interface initiated a SCPI command
 * Used for single-interface streaming to prevent bandwidth overload
 */
static StreamingInterface SCPI_GetInterface(scpi_t* context) {
    UsbCdcData_t * pRunTimeUsbSettings = UsbCdc_GetSettings();
    wifi_tcp_server_context_t * pRunTimeServerData = wifi_manager_GetTcpServerContext();

    if (&pRunTimeUsbSettings->scpiContext == context) {
        return StreamingInterface_USB;
    } else if (&pRunTimeServerData->client.scpiContext == context) {
        return StreamingInterface_WiFi;
    }

    // Default to USB if interface cannot be determined
    // Multi-interface streaming (All) is available but not used by default
    return StreamingInterface_USB;
}

/**
 * Triggers a board reset 
 */
static scpi_result_t SCPI_Reset(scpi_t * context) {
    // Send notification that reset is occurring
    // This gives connected clients a chance to know what happened
    const char* msg = "System reset initiated\r\n";
    context->interface->write(context, msg, strlen(msg));
    
    // Ensure the message is sent before reset
    if (context->interface && context->interface->flush) {
        context->interface->flush(context);
    }
    
    // Allow time for message transmission and any pending operations
    vTaskDelay(100 / portTICK_PERIOD_MS);
    
    // Perform the software reset
    RCON_SoftwareReset();
    
    // If we get here, the reset didn't work
    SCPI_ErrorPush(context, SCPI_ERROR_SYSTEM_ERROR);
    return SCPI_RES_ERR;
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
 * SCPI Callback: Returns system information in human-readable text format
 * @return SCPI_RES_OK on success
 */
static scpi_result_t SCPI_SysInfoTextGet(scpi_t * context) {
    char buffer[256];
    const tBoardConfig* pBoardConfig = BoardConfig_Get(BOARDCONFIG_ALL_CONFIG, 0);
    tBoardData * pBoardData = BoardData_Get(BOARDDATA_ALL_DATA, 0);
    wifi_manager_settings_t * pWifiSettings = BoardData_Get(BOARDDATA_WIFI_SETTINGS, 0);
    AInRuntimeArray * pAInConfig = BoardRunTimeConfig_Get(BOARDRUNTIMECONFIG_AIN_CHANNELS);
    DIORuntimeArray * pDIOConfig = BoardRunTimeConfig_Get(BOARDRUNTIMECONFIG_DIO_CHANNELS);

    // Check for NULL pointers to prevent crashes
    if (!pBoardData || !pBoardConfig) {
        const char* err = !pBoardData ? "ERROR: BoardData not available\r\n"
                                      : "ERROR: BoardConfig not available\r\n";
        context->interface->write(context, err, strlen(err));
        return SCPI_RES_ERR;
    }

    // Header with device identification
    snprintf(buffer, sizeof(buffer), "=== DAQiFi Nyquist%d | HW:%s FW:%s ===\r\n",
        pBoardConfig->BoardVariant, pBoardConfig->boardHardwareRev, pBoardConfig->boardFirmwareRev);
    context->interface->write(context, buffer, strlen(buffer));
    
    // Network Section
    const char* netHeader = "[Network]\r\n";
    context->interface->write(context, netHeader, strlen(netHeader));
    
    // WiFi status - check actual driver state
    wifi_status_t wifiStatus = wifi_manager_GetWiFiStatus();
    
    if ((wifiStatus == WIFI_STATUS_CONNECTED || wifiStatus == WIFI_STATUS_DISCONNECTED) && pWifiSettings) {
        // WiFi is enabled - show configuration regardless of connection status
        char ipStr[16];
        snprintf(ipStr, sizeof(ipStr), "%d.%d.%d.%d", 
            (uint8_t)(pWifiSettings->ipAddr.Val & 0xFF),
            (uint8_t)((pWifiSettings->ipAddr.Val >> 8) & 0xFF),
            (uint8_t)((pWifiSettings->ipAddr.Val >> 16) & 0xFF),
            (uint8_t)((pWifiSettings->ipAddr.Val >> 24) & 0xFF));
        
        snprintf(buffer, sizeof(buffer), "  2.4GHz: On | Mode: %s | SSID: %s\r\n", 
            pWifiSettings->networkMode == WIFI_MANAGER_NETWORK_MODE_AP ? "AP" : "STA",
            pWifiSettings->ssid);
        context->interface->write(context, buffer, strlen(buffer));
        
        snprintf(buffer, sizeof(buffer), "  IP: %s | Port: %d | Security: %s\r\n", 
            ipStr, pWifiSettings->tcpPort,
            pWifiSettings->securityMode == WIFI_MANAGER_SECURITY_MODE_OPEN ? "Open" : "WPA");
        context->interface->write(context, buffer, strlen(buffer));
    } else {
        const char* wifiOff = "  2.4GHz: Off\r\n";
        context->interface->write(context, wifiOff, strlen(wifiOff));
    }
    
    // Connectivity Section
    const char* connHeader = "[Connectivity]\r\n";
    context->interface->write(context, connHeader, strlen(connHeader));
    bool hasUSBPower = (pBoardData->PowerData.externalPowerSource == USB_100MA_EXT_POWER || 
                        pBoardData->PowerData.externalPowerSource == USB_500MA_EXT_POWER);
    snprintf(buffer, sizeof(buffer), "  USB: %s | WiFi: %s | Ext power: %s\r\n",
        hasUSBPower ? "Connected" : "Disconnected",
        wifiStatus == WIFI_STATUS_CONNECTED ? "Connected" : 
        (wifiStatus == WIFI_STATUS_DISCONNECTED ? "Disconnected" : "Disabled"),
        pBoardData->PowerData.externalPowerSource != NO_EXT_POWER ? "Present" : "None");
    context->interface->write(context, buffer, strlen(buffer));
    
    // Power Section
    const char* powHeader = "[Power]\r\n";
    context->interface->write(context, powHeader, strlen(powHeader));
    const char* powerState = "Unknown";
    switch(pBoardData->PowerData.powerState) {
        case POWERED_UP: powerState = "Run"; break;
        case POWERED_UP_EXT_DOWN: powerState = "Partial"; break;
        case STANDBY: powerState = "Standby"; break;
        default: powerState = "Unknown"; break;
    }
    
    // Only show shutdown status if a shutdown is actually requested
    const char* shutdownStatus = "";
    if (pBoardData->PowerData.requestedPowerState == DO_POWER_DOWN) {
        shutdownStatus = pBoardData->PowerData.shutdownNotified ? " | Shutdown: Ready" : " | Shutdown: Pending";
    }
    
    snprintf(buffer, sizeof(buffer), "  State: %s (%d) | USB: %s%s\r\n", 
        powerState,
        pBoardData->PowerData.powerState,
        pBoardData->PowerData.USBSleep ? "Sleep" : "Active",
        shutdownStatus);
    context->interface->write(context, buffer, strlen(buffer));
    
    // Display battery info appropriately based on monitoring state
    if (pBoardData->PowerData.powerState == STANDBY) {
        // Battery monitoring inactive in STANDBY - but BQ24297 still reports charge status
        const char* chargeStatus = "Off";
        if (pBoardData->PowerData.BQ24297Data.status.otg) {
            chargeStatus = "OTG";
        } else if (pBoardData->PowerData.BQ24297Data.status.chgEn) {
            // Charge enabled, check actual status
            switch(pBoardData->PowerData.BQ24297Data.status.chgStat) {
                case 0: chargeStatus = "Off"; break;      // No charge
                case 1: chargeStatus = "Pre"; break;      // Precharge
                case 2: chargeStatus = "Fast"; break;     // Fast charge
                case 3: chargeStatus = "Done"; break;     // Charge complete
                default: chargeStatus = "?"; break;
            }
        }
        snprintf(buffer, sizeof(buffer), "  Battery: -- (--) [--] | Charging: %s\r\n", chargeStatus);
    } else {
        // Battery monitoring active - show actual values
        const char* chargeStatus = "Off";
        if (pBoardData->PowerData.BQ24297Data.status.otg) {
            chargeStatus = "OTG";
        } else if (pBoardData->PowerData.BQ24297Data.status.chgEn) {
            // Charge enabled, check actual status
            switch(pBoardData->PowerData.BQ24297Data.status.chgStat) {
                case 0: chargeStatus = "Off"; break;      // No charge
                case 1: chargeStatus = "Pre"; break;      // Precharge
                case 2: chargeStatus = "Fast"; break;     // Fast charge
                case 3: chargeStatus = "Done"; break;     // Charge complete
                default: chargeStatus = "?"; break;
            }
        }
        snprintf(buffer, sizeof(buffer), "  Battery: %.2fV (%d%%) %s | Charging: %s\r\n", 
            pBoardData->PowerData.battVoltage, 
            pBoardData->PowerData.chargePct,
            pBoardData->PowerData.battLow ? "[Low]" : "[Ok]",
            chargeStatus);
    }
    context->interface->write(context, buffer, strlen(buffer));
    
    // Status Section
    const char* statHeader = "[Status]\r\n";
    context->interface->write(context, statHeader, strlen(statHeader));
    
    // Channel status - separate user and internal ADCs by channel ID
    // User channels have IDs 0-15, internal monitoring channels have IDs >= 248
    int userAdcEnabled = 0, internalAdcEnabled = 0, dioInputs = 0;
    int userAdcTotal = 0, internalAdcTotal = 0;

    const tBoardConfig* pBoardConfigForAdc = BoardConfig_Get(BOARDCONFIG_ALL_CONFIG, 0);
    const AInArray* pBoardConfigAInChannels = pBoardConfigForAdc ? &pBoardConfigForAdc->AInChannels : NULL;

    if (pAInConfig && pBoardConfigAInChannels) {
        for (int i = 0; i < pAInConfig->Size; i++) {
            // Check channel ID from board config
            uint8_t channelId = pBoardConfigAInChannels->Data[i].DaqifiAdcChannelId;

            if (channelId >= ADC_CHANNEL_3_3V) {
                // Internal monitoring channel (ID >= 248)
                // Exclude temperature sensor (doesn't work per silicon errata)
                if (channelId != ADC_CHANNEL_TEMP) {
                    internalAdcTotal++;
                    if (pAInConfig->Data[i].IsEnabled) {
                        internalAdcEnabled++;
                    }
                }
            } else {
                // User ADC channel (ID 0-15 for NQ1, 0-7 for NQ3)
                userAdcTotal++;
                if (pAInConfig->Data[i].IsEnabled) {
                    userAdcEnabled++;
                }
            }
        }
    }
    
    if (pDIOConfig) {
        for (int i = 0; i < pDIOConfig->Size; i++) {
            if (pDIOConfig->Data[i].IsInput) dioInputs++;
        }
    }
    
    // Display separated ADC counts
    snprintf(buffer, sizeof(buffer), "  User ADC: %d/%d | Internal ADC: %d/%d | DIO: %d/%d inputs\r\n", 
        userAdcEnabled, userAdcTotal,
        internalAdcEnabled, internalAdcTotal,
        dioInputs, pDIOConfig ? pDIOConfig->Size : 0);
    context->interface->write(context, buffer, strlen(buffer));
    
    // Show which specific user ADC channels are enabled
    if (userAdcEnabled > 0 && pAInConfig && pBoardConfigAInChannels) {
        context->interface->write(context, "  Enabled user ch: ", 19);
        bool first = true;
        for (int i = 0; i < pAInConfig->Size; i++) {
            uint8_t channelId = pBoardConfigAInChannels->Data[i].DaqifiAdcChannelId;
            // Only show user channels (ID < 248, not internal monitoring)
            if (channelId < ADC_CHANNEL_3_3V && pAInConfig->Data[i].IsEnabled) {
                if (!first) context->interface->write(context, ",", 1);
                snprintf(buffer, sizeof(buffer), "%d", channelId);
                context->interface->write(context, buffer, strlen(buffer));
                first = false;
            }
        }
        context->interface->write(context, "\r\n", 2);
    }
    
    // DIO pin states
    if (pDIOConfig && pDIOConfig->Size > 0) {
        // Read current DIO states including both inputs and outputs
        DIOSample sample;
        uint32_t channelMask = 0xFFFF; // Read all 16 channels
        
        if (DIO_ReadSampleByMask(&sample, channelMask)) {
            // Debug: show raw value
            snprintf(buffer, sizeof(buffer), "  DIO raw: %u (0x%04X)\r\n", sample.Values, sample.Values);
            context->interface->write(context, buffer, strlen(buffer));
            
            context->interface->write(context, "  DIO state: ", 13);
            // Display the state of each pin
            for (int i = 0; i < pDIOConfig->Size && i < 16; i++) {
                if (i == 8) {
                    context->interface->write(context, " ", 1); // Space between bytes
                }
                context->interface->write(context, (sample.Values & (1 << i)) ? "1" : "0", 1);
            }
            context->interface->write(context, "\r\n", 2);
        }
    }
    
    // Get streaming configuration
    StreamingRuntimeConfig * pRunTimeStreamConfig = BoardRunTimeConfig_Get(
        BOARDRUNTIME_STREAMING_CONFIGURATION);
    
    // Streaming and sampling - cannot be active in STANDBY
    bool canStream = (pBoardData->PowerData.powerState != STANDBY);
    snprintf(buffer, sizeof(buffer), "  Streaming: %s\r\n",
        (canStream && pRunTimeStreamConfig && pRunTimeStreamConfig->IsEnabled) ? "Active" : 
        (!canStream ? "Disabled" : "Idle"));
    context->interface->write(context, buffer, strlen(buffer));
    
    // Battery Diagnostics Section
    const char* battDiagHeader = "\r\n[Battery Diagnostics]\r\n";
    context->interface->write(context, battDiagHeader, strlen(battDiagHeader));
    
    // Battery voltage and charge from ADC
    if (pBoardData->PowerData.powerState == STANDBY) {
        // Battery monitoring inactive in STANDBY
        const char* adcInactive = "  ADC: -- | --\r\n";
        context->interface->write(context, adcInactive, strlen(adcInactive));
    } else {
        snprintf(buffer, sizeof(buffer), "  ADC: %d%% | %.2fV\r\n",
            pBoardData->PowerData.chargePct,
            pBoardData->PowerData.battVoltage);
        context->interface->write(context, buffer, strlen(buffer));
    }
    
    // BQ24297 status - get fresh data
    tBQ24297Data * pBQ24297Data = &pBoardData->PowerData.BQ24297Data;
    if (pBQ24297Data->initComplete) {
        BQ24297_UpdateStatus();
        
        // Battery detection and charging
        const char* chgStatStr[] = {"Not Charging", "Pre-charge", "Fast Charge", "Charge Done"};
        snprintf(buffer, sizeof(buffer), "  BQ24297: Battery %s | Charging: %s\r\n",
            pBQ24297Data->status.batPresent ? "Present" : "Not Present",
            (pBQ24297Data->status.chgStat < 4) ? chgStatStr[pBQ24297Data->status.chgStat] : "Unknown");
        context->interface->write(context, buffer, strlen(buffer));
        
        // Power conditions with clear explanations
        snprintf(buffer, sizeof(buffer), "  vsysStat: %d (Battery >3.0V: %s) | pgStat: %d (Ext power: %s)\r\n",
            pBQ24297Data->status.vsysStat,
            pBQ24297Data->status.vsysStat ? "No" : "Yes",
            pBQ24297Data->status.pgStat,
            pBQ24297Data->status.pgStat ? "Yes" : "No");
        context->interface->write(context, buffer, strlen(buffer));
        
        // NTC and current limit
        const char* ntcStr[] = {"Ok", "Hot", "Cold (Battery disconnected?)", "Hot/Cold"};
        const char* iLimStr[] = {"100mA", "150mA", "500mA", "900mA", "1A", "1.5A", "2A", "3A"};
        
        // Read OTG GPIO pin state for debugging
        bool otgGpioState = BATT_MAN_OTG_Get();
        
        snprintf(buffer, sizeof(buffer), "  NTC: %s | Current limit: %s | OTG: %s (GPIO: %s)\r\n",
            (pBQ24297Data->status.ntcFault < 4) ? ntcStr[pBQ24297Data->status.ntcFault] : "Fault",
            (pBQ24297Data->status.inLim < 8) ? iLimStr[pBQ24297Data->status.inLim] : "Unknown",
            pBQ24297Data->status.otg ? "On" : "Off",
            otgGpioState ? "High" : "Low");
        context->interface->write(context, buffer, strlen(buffer));
        
        // Read REG01 and REG07 for detailed status
        uint8_t reg01 = BQ24297_Read_I2C(0x01);
        uint8_t reg07 = BQ24297_Read_I2C(0x07);
        
        // BATFET status (REG07 bit 5: 0=enabled, 1=disabled)
        bool batfetEnabled = !(reg07 & 0x20);
        
        // REG01 breakdown: [7:6]=watchdog, [5]=OTG, [4]=CHG_CONFIG, [3:1]=SYS_MIN, [0]=reserved
        snprintf(buffer, sizeof(buffer), "  BATFET: %s | REG01: 0x%02X (OTG=%d, CHG=%d) | REG07: 0x%02X\r\n",
            batfetEnabled ? "Enabled" : "Disabled!",
            reg01,
            (reg01 >> 5) & 1,  // OTG bit
            (reg01 >> 4) & 1,  // Charge enable bit
            reg07);
        context->interface->write(context, buffer, strlen(buffer));
        
        // Power-up readiness - the key diagnostic info
        bool canPowerUp = (!pBQ24297Data->status.vsysStat || pBQ24297Data->status.pgStat);
        snprintf(buffer, sizeof(buffer), "  >>> Power-up ready: %s %s\r\n",
            canPowerUp ? "Yes" : "No",
            canPowerUp ? "" : "(Battery <3.0V and no external power)");
        context->interface->write(context, buffer, strlen(buffer));
    } else {
        context->interface->write(context, "  BQ24297: Not initialized\r\n", 28);
    }

    // Voltage Rail Monitoring Section - only when powered up
    if (pBoardData->PowerData.powerState != STANDBY) {
        const char* voltHeader = "\r\n[Voltage Rails]\r\n";
        context->interface->write(context, voltHeader, strlen(voltHeader));

        // Read latest ADC samples for internal monitoring channels
        // Use ADC_ConvertToVoltage for proper conversion based on channel type and config
        // NOTE: PIC32MZ internal temperature sensor (ADC_CHANNEL_TEMP) is not functional
        //       due to silicon errata. Temperature monitoring is not available on this device.
        AInRuntimeArray* pAInRuntimeConfig = BoardRunTimeConfig_Get(BOARDRUNTIMECONFIG_AIN_CHANNELS);

        if (pAInRuntimeConfig && pBoardConfigAInChannels) {
            double volt3_3 = 0, volt5 = 0, volt10 = 0, voltSys = 0, vBatt = 0, v2_5Ref = 0, v5Ref = 0;
            bool found3_3 = false, found5 = false, found10 = false, foundSys = false;
            bool foundBatt = false, found2_5Ref = false, found5Ref = false;

            // Get the actual count of available samples (not runtime config size)
            size_t* pAInLatestSize = BoardData_Get(BOARDDATA_AIN_LATEST_SIZE, 0);
            size_t sampleCount = pAInLatestSize ? *pAInLatestSize : 0;

            // Iterate through available samples and use channel ID for identification
            for (size_t i = 0; i < sampleCount; i++) {
                AInSample* sample = BoardData_Get(BOARDDATA_AIN_LATEST, i);
                if (!sample || !ADC_IsDataValid(sample)) continue;

                // Convert raw ADC value to voltage using ADC layer function
                double voltage = ADC_ConvertToVoltage(sample);
                uint8_t sampleChannelId = sample->Channel;

                // Store voltage for the appropriate rail
                if (sampleChannelId == ADC_CHANNEL_3_3V) {
                    volt3_3 = voltage;
                    found3_3 = true;
                } else if (sampleChannelId == ADC_CHANNEL_5V) {
                    volt5 = voltage;
                    found5 = true;
                } else if (sampleChannelId == ADC_CHANNEL_10V) {
                    volt10 = voltage;
                    found10 = true;
                } else if (sampleChannelId == ADC_CHANNEL_VSYS) {
                    voltSys = voltage;
                    foundSys = true;
                } else if (sampleChannelId == ADC_CHANNEL_VBATT) {
                    vBatt = voltage;
                    foundBatt = true;
                } else if (sampleChannelId == ADC_CHANNEL_2_5VREF) {
                    v2_5Ref = voltage;
                    found2_5Ref = true;
                } else if (sampleChannelId == ADC_CHANNEL_5VREF) {
                    v5Ref = voltage;
                    found5Ref = true;
                }
                // ADC_CHANNEL_TEMP intentionally not processed - PIC32MZ temperature sensor
                // does not function per silicon errata
            }

            // Display voltage rails with proper string formatting
            // Initialize strings to empty to ensure well-defined behavior
            char str3_3[20] = {0}, str5[20] = {0}, str10[20] = {0};
            char strSys[20] = {0}, strBatt[20] = {0}, str2_5Ref[20] = {0}, str5Ref[20] = {0};

            if (found3_3) {
                snprintf(str3_3, sizeof(str3_3), "%.2fV", volt3_3);
            } else {
                strcpy(str3_3, "--");
            }

            if (found5) {
                snprintf(str5, sizeof(str5), "%.2fV", volt5);
            } else {
                strcpy(str5, "--");
            }

            if (found10) {
                snprintf(str10, sizeof(str10), "%.2fV", volt10);
            } else {
                strcpy(str10, "--");
            }

            if (foundSys) {
                snprintf(strSys, sizeof(strSys), "%.2fV", voltSys);
            } else {
                strcpy(strSys, "--");
            }

            if (foundBatt) {
                snprintf(strBatt, sizeof(strBatt), "%.2fV", vBatt);
            } else {
                strcpy(strBatt, "--");
            }

            if (found2_5Ref) {
                snprintf(str2_5Ref, sizeof(str2_5Ref), "%.2fV", v2_5Ref);
            } else {
                strcpy(str2_5Ref, "--");
            }

            if (found5Ref) {
                snprintf(str5Ref, sizeof(str5Ref), "%.2fV", v5Ref);
            } else {
                strcpy(str5Ref, "--");
            }

            // Display power rails
            snprintf(buffer, sizeof(buffer), "  +3.3V: %s | +5V: %s | +10V: %s\r\n",
                str3_3, str5, str10);
            context->interface->write(context, buffer, strlen(buffer));

            snprintf(buffer, sizeof(buffer), "  VSYS: %s | VBATT: %s\r\n",
                strSys, strBatt);
            context->interface->write(context, buffer, strlen(buffer));

            // Display reference voltages
            snprintf(buffer, sizeof(buffer), "  2.5V Ref: %s | 5V Ref: %s\r\n",
                str2_5Ref, str5Ref);
            context->interface->write(context, buffer, strlen(buffer));
        } else {
            context->interface->write(context, "  Voltage monitoring unavailable\r\n", 34);
        }
    }

    return SCPI_RES_OK;
}

/**
 * Gets the system log
 * @param context
 * @return 
 */
static scpi_result_t SCPI_SysLogGet(scpi_t * context) {
    char buffer[128];

    // Add a test log message to verify logging works  
    LOG_D("SCPI_SysLogGet called - log count: %d", LogMessageCount());
    
    size_t logSize = LogMessageCount();
    
    size_t i = 0;
    for (i = 0; i < logSize; ++i) {
        size_t messageSize = LogMessagePop((uint8_t*) buffer, 128);
        if (messageSize > 0) {
            // Write the message
            context->interface->write(context, buffer, messageSize);
            
            // Flush after each message to prevent buffer overflow
            // This ensures the USB buffer has time to process each message
            if (context->interface->flush) {
                context->interface->flush(context);
            }
            
            // Small delay to allow USB task to process the data
            // This prevents overwhelming the USB circular buffer
            // Reduced delay for faster log output
            // vTaskDelay(1);  // Commented out for maximum speed
        }
    }

    return SCPI_RES_OK;
}

/**
 * Tests the logging system by adding test messages
 * @param context
 * @return 
 */
static scpi_result_t SCPI_SysLogTest(scpi_t * context) {
    // Add test messages to the log
    LOG_D("Test log message 1");
    LOG_E("Test error message");
    LOG_I("Test info message");
    
    // Add some more to test the buffer limits
    for (int i = 0; i < 5; i++) {
        LOG_D("Test message %d", i);
    }
    
    context->interface->write(context, "Added test log messages\n", 24);
    
    return SCPI_RES_OK;
}

/**
 * Clears the log buffer
 * @param context
 * @return 
 */
static scpi_result_t SCPI_SysLogClear(scpi_t * context) {
    char buffer[128];
    
    // Pop all messages to clear the buffer
    while (LogMessageCount() > 0) {
        LogMessagePop((uint8_t*) buffer, 128);
    }
    
    context->interface->write(context, "Log cleared\n", 12);
    
    return SCPI_RES_OK;
}

/**
 * Gets the external power source type
 * @param context
 * @return 
 */
static scpi_result_t SCPI_PowerSourceGet(scpi_t * context) {
    tPowerData *pPowerData = BoardData_Get(
            BOARDDATA_POWER_DATA,
            0);
    SCPI_ResultInt32(context, (int) (pPowerData->externalPowerSource));
    return SCPI_RES_OK;
}

/**
 * Gets the battery level
 * Returns -1 if device is in standby (battery monitoring inactive)
 * @param context
 * @return 
 */
static scpi_result_t SCPI_BatteryLevelGet(scpi_t * context) {
    tPowerData *pPowerData = BoardData_Get(
            BOARDDATA_POWER_DATA,
            0);
    
    // Battery monitoring is only active when powered up
    if (pPowerData->powerState == STANDBY) {
        SCPI_ResultInt32(context, -1);
    } else {
        SCPI_ResultInt32(context, (int) (pPowerData->chargePct));
    }
    
    return SCPI_RES_OK;
}

/**
 * Gets the power state
 * Returns 0 for standby/off, 1 for any powered state
 * @param context
 * @return 
 */
static scpi_result_t SCPI_GetPowerState(scpi_t * context) {
    tPowerData *pPowerData = BoardData_Get(
            BOARDDATA_POWER_DATA,
            0);
    // Return actual power state enum value
    // 0 = STANDBY (CPU on but standby, powers off if disconnected)
    // 1 = POWERED_UP (fully powered)
    // 2 = POWERED_UP_EXT_DOWN (partial power, low battery mode)
    SCPI_ResultInt32(context, (int)pPowerData->powerState);
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
            BOARDDATA_POWER_DATA,
            0);

    if (!SCPI_ParamInt32(context, &param1, TRUE)) {
        return SCPI_RES_ERR;
    }

    // Validate input: accept 0 (standby), 1 (powered up), or 2 (powered up ext down)
    if (param1 < 0 || param1 > 2) {
        SCPI_ErrorPush(context, SCPI_ERROR_ILLEGAL_PARAMETER_VALUE);
        return SCPI_RES_ERR;
    }

    // Check if already in the requested state - no-op if so
    if ((param1 == 0 && pPowerData->powerState == STANDBY) ||
        (param1 == 1 && pPowerData->powerState == POWERED_UP) ||
        (param1 == 2 && pPowerData->powerState == POWERED_UP_EXT_DOWN)) {
        // Already in requested state, nothing to do
        return SCPI_RES_OK;
    }

    switch (param1) {
        case 0:  // STANDBY
            pPowerData->requestedPowerState = DO_POWER_DOWN;
            break;
        case 1:  // POWERED_UP
            pPowerData->requestedPowerState = DO_POWER_UP;
            break;
        case 2:  // POWERED_UP_EXT_DOWN (power system but not user power)
            pPowerData->requestedPowerState = DO_POWER_UP_EXT_DOWN;
            break;
    }
    
    BoardData_Set(
            BOARDDATA_POWER_DATA,
            0,
            pPowerData);

    return SCPI_RES_OK;
}

/**
 * SCPI Callback: Get auto external power switching state
 * Command: SYST:POW:AUTO:EXT?
 * Returns: 1 if enabled, 0 if disabled
 */
static scpi_result_t SCPI_GetAutoExtPower(scpi_t * context) {
    tPowerData * pPowerData = BoardData_Get(
            BOARDDATA_POWER_DATA,
            0);
    
    SCPI_ResultInt32(context, pPowerData->autoExtPowerEnabled ? 1 : 0);
    return SCPI_RES_OK;
}

/**
 * SCPI Callback: Set auto external power switching state
 * Command: SYST:POW:AUTO:EXT 0|1
 * 0 = Disable automatic external power switching (manual control only)
 * 1 = Enable automatic external power switching based on battery level
 */
static scpi_result_t SCPI_SetAutoExtPower(scpi_t * context) {
    int param1;
    
    tPowerData * pPowerData = BoardData_Get(
            BOARDDATA_POWER_DATA,
            0);
    
    if (!SCPI_ParamInt32(context, &param1, TRUE)) {
        return SCPI_RES_ERR;
    }
    
    // Validate input: accept 0 or 1
    if (param1 < 0 || param1 > 1) {
        SCPI_ErrorPush(context, SCPI_ERROR_ILLEGAL_PARAMETER_VALUE);
        return SCPI_RES_ERR;
    }
    
    pPowerData->autoExtPowerEnabled = (param1 != 0);
    LOG_D("Auto external power switching %s", 
          pPowerData->autoExtPowerEnabled ? "enabled" : "disabled");
    
    BoardData_Set(
            BOARDDATA_POWER_DATA,
            0,
            pPowerData);
    
    return SCPI_RES_OK;
}

// OTG control functions are disabled - see command table for explanation
#if 0
/**
 * SCPI Callback: Control OTG mode
 * Command: SYST:POW:OTG 0|1
 * 0 = Disable OTG
 * 1 = Enable OTG
 * 
 * NOTE: This command is currently disabled because:
 * - When external power is present, OTG is automatically disabled every second for safety
 * - Manual OTG control only works on battery power
 * - The power management system (Power_Tasks) overrides manual settings
 * To enable manual control, would need to modify BQ24297_SetPowerMode() behavior
 */
static scpi_result_t SCPI_SetOTGMode(scpi_t * context) {
    int param;
    
    if (!SCPI_ParamInt32(context, &param, TRUE)) {
        return SCPI_RES_ERR;
    }
    
    if (param != 0 && param != 1) {
        SCPI_ErrorPush(context, SCPI_ERROR_ILLEGAL_PARAMETER_VALUE);
        return SCPI_RES_ERR;
    }
    
    if (param == 1) {
        LOG_D("SCPI: Manually enabling OTG mode");
        BQ24297_EnableOTG();
    } else {
        LOG_D("SCPI: Manually disabling OTG mode");
        BQ24297_DisableOTG(true);
    }
    
    return SCPI_RES_OK;
}

/**
 * SCPI Callback: Get OTG status
 * Query: SYST:POW:OTG?
 * Returns: 0 (disabled) or 1 (enabled)
 */
static scpi_result_t SCPI_GetOTGMode(scpi_t * context) {
    bool otg_enabled = BQ24297_IsOTGEnabled();
    SCPI_ResultInt32(context, otg_enabled ? 1 : 0);
    return SCPI_RES_OK;
}
#endif

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
    // Check power state first - streaming requires powered-up state
    const tPowerData *pPowerState = BoardData_Get(BOARDDATA_POWER_DATA, 0);
    if (pPowerState == NULL ||
        (pPowerState->powerState != POWERED_UP && pPowerState->powerState != POWERED_UP_EXT_DOWN)) {
        LOG_E("Streaming command rejected: Device must be powered up (SYST:POW:STAT 1)");
        SCPI_ErrorPush(context, SCPI_ERROR_HARDWARE_MISSING);
        return SCPI_RES_ERR;
    }

    volatile AInRuntimeArray * pRuntimeAInChannels = BoardRunTimeConfig_Get(BOARDRUNTIMECONFIG_AIN_CHANNELS);
    volatile AInArray *pBoardConfigADC = BoardConfig_Get(BOARDCONFIG_AIN_CHANNELS, 0);

    // timer running frequency
    uint32_t clkFreq = TimerApi_FrequencyGet(pBoardConfig->StreamingConfig.TimerIndex);

    //int i;
    uint16_t activeType1ChannelCount = 0;
    bool hasActiveAD7609Channels __attribute__((unused)) = false;
    int i;
    bool hasEnabledChannels = false;
    
    // Count active channels and detect AD7609 usage
    for (i = 0; i < pBoardConfigADC->Size; i++) {
        if (pRuntimeAInChannels->Data[i].IsEnabled == 1) {
            hasEnabledChannels = true;
            if (pBoardConfigADC->Data[i].Type == AIn_AD7609) {
                hasActiveAD7609Channels = true;
            } else if (pBoardConfigADC->Data[i].Type == AIn_MC12bADC && 
                       pBoardConfigADC->Data[i].Config.MC12b.ChannelType == 1) {
                activeType1ChannelCount++;
            }
        }
    }
    
    if (!hasEnabledChannels) {
        LOG_E("Streaming command rejected: No channels enabled");
        SCPI_ErrorPush(context, SCPI_ERROR_SETTINGS_CONFLICT);
        return SCPI_RES_ERR;
    }

    if (SCPI_ParamInt32(context, &freq, FALSE)) {
        // Frequency = 0 is valid and means "disable streaming"
        // (In future may support per-channel frequency where 0 disables that channel)
        if (freq == 0) {
            pRunTimeStreamConfig->IsEnabled = false;
            Streaming_UpdateState();
            return SCPI_RES_OK;
        }

        // NQ3 Smart Frequency Management:
        // When external AD7609 channels are active WITHOUT any Type 1 MC12bADC channels,
        // force internal monitoring to 1Hz (since only monitoring channels would be streaming)
        if (hasActiveAD7609Channels && activeType1ChannelCount == 0 && freq > 1) {
            freq = 1; // Override to 1Hz for internal monitoring when external ADC active
        }

        // Maximum frequency limited to 15kHz pending optimization
        // See https://github.com/daqifi/daqifi-nyquist-firmware/issues/58
        if (freq >= 1 && freq <= 15000)
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
            if (activeType1ChannelCount > 0) {
                // Avoid overflow: compare without multiplying freq * activeType1ChannelCount
                // Instead of: (freq * activeType1ChannelCount) > 15000
                // Use: freq > (15000 / activeType1ChannelCount)
                if (freq > (15000 / activeType1ChannelCount)) {
                    freq = 15000 / activeType1ChannelCount;

                    // Prevent divide-by-zero: if too many channels active, return error
                    if (freq == 0) {
                        SCPI_ErrorPush(context, SCPI_ERROR_ILLEGAL_PARAMETER_VALUE);
                        return SCPI_RES_ERR;
                    }
                }
            }

            // Note: Internal monitoring channels have fixed 1Hz in NQ3 runtime config

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

    // Auto-detect interface only if user hasn't explicitly set it via SYSTem:STReam:INTerface
    // If current interface matches what auto-detection would choose, allow override
    // If different, user explicitly set it (e.g., SD or All) so preserve their choice
    StreamingInterface detectedInterface = SCPI_GetInterface(context);
    StreamingInterface currentInterface = pRunTimeStreamConfig->ActiveInterface;

    // Only auto-detect if current setting matches the detected interface
    // (meaning user hasn't changed it, or wants auto-detection)
    if (currentInterface == detectedInterface || currentInterface == StreamingInterface_USB) {
        pRunTimeStreamConfig->ActiveInterface = detectedInterface;
    }
    // Otherwise keep user's explicit setting (e.g., SD or All)

    // Check for WiFi+SD conflict (both use SPI bus)
    sd_card_manager_settings_t* pSDCardSettings =
        BoardRunTimeConfig_Get(BOARDRUNTIME_SD_CARD_SETTINGS);
    if ((pRunTimeStreamConfig->ActiveInterface == StreamingInterface_WiFi ||
         pRunTimeStreamConfig->ActiveInterface == StreamingInterface_All) &&
        pSDCardSettings != NULL &&
        pSDCardSettings->enable && pSDCardSettings->mode == SD_CARD_MANAGER_MODE_WRITE) {
        LOG_E("Cannot start WiFi streaming while SD logging is active (SPI bus conflict)");
        SCPI_ErrorPush(context, SCPI_ERROR_EXECUTION_ERROR);
        return SCPI_RES_ERR;
    }

    Streaming_UpdateState();
    pRunTimeStreamConfig->IsEnabled = true;
    return SCPI_RES_OK;
}

static scpi_result_t SCPI_StopStreaming(scpi_t * context) {
    StreamingRuntimeConfig * pRunTimeStreamConfig = BoardRunTimeConfig_Get(
            BOARDRUNTIME_STREAMING_CONFIGURATION);

    if (pRunTimeStreamConfig) {
        pRunTimeStreamConfig->IsEnabled = false;
    }

    Streaming_UpdateState();

    // Close SD card file if logging was enabled
    sd_card_manager_settings_t* pSDCardRuntimeConfig = BoardRunTimeConfig_Get(BOARDRUNTIME_SD_CARD_SETTINGS);
    if (pSDCardRuntimeConfig != NULL &&
        pSDCardRuntimeConfig->enable && pSDCardRuntimeConfig->mode == SD_CARD_MANAGER_MODE_WRITE) {
        // Set mode to NONE and update to trigger file close (DEINIT → UNMOUNT → close)
        pSDCardRuntimeConfig->mode = SD_CARD_MANAGER_MODE_NONE;
        sd_card_manager_UpdateSettings(pSDCardRuntimeConfig);
        // Give SD card manager task time to close the file
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    // Reset encoder state so next session gets fresh headers
    csv_ResetEncoder();
    json_ResetEncoder();

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
    }else if(param1 == Streaming_Csv){
         pRunTimeStreamConfig->Encoding = Streaming_Csv;
    }else{
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

static scpi_result_t SCPI_SetStreamInterface(scpi_t * context) {
    int param1;
    StreamingRuntimeConfig * pRunTimeStreamConfig = BoardRunTimeConfig_Get(
            BOARDRUNTIME_STREAMING_CONFIGURATION);

    if (!SCPI_ParamInt32(context, &param1, TRUE)) {
        return SCPI_RES_ERR;
    }

    // Validate interface value: 0=USB, 1=WiFi, 2=SD, 3=All
    if (param1 < 0 || param1 > 3) {
        SCPI_ErrorPush(context, SCPI_ERROR_ILLEGAL_PARAMETER_VALUE);
        return SCPI_RES_ERR;
    }

    sd_card_manager_settings_t* pSDCardSettings =
        BoardRunTimeConfig_Get(BOARDRUNTIME_SD_CARD_SETTINGS);

    // Check if SD card is enabled when trying to use SD or All interfaces
    if (param1 == StreamingInterface_SD || param1 == StreamingInterface_All) {
        if (pSDCardSettings != NULL && !pSDCardSettings->enable) {
            LOG_E("Cannot set interface to SD - SD card not enabled. Use SYSTem:STORage:SD:ENAble 1");
            SCPI_ErrorPush(context, SCPI_ERROR_EXECUTION_ERROR);
            return SCPI_RES_ERR;
        }
    }

    // Check for WiFi+SD conflict (both use SPI bus)
    if (param1 == StreamingInterface_WiFi || param1 == StreamingInterface_All) {
        if (pSDCardSettings != NULL &&
            pSDCardSettings->enable && pSDCardSettings->mode == SD_CARD_MANAGER_MODE_WRITE) {
            LOG_E("Cannot stream to WiFi while SD logging is active (SPI bus conflict). Disable SD logging first.");
            SCPI_ErrorPush(context, SCPI_ERROR_EXECUTION_ERROR);
            return SCPI_RES_ERR;
        }
    }

    pRunTimeStreamConfig->ActiveInterface = (StreamingInterface) param1;
    return SCPI_RES_OK;
}

static scpi_result_t SCPI_GetStreamInterface(scpi_t * context) {
    StreamingRuntimeConfig * pRunTimeStreamConfig = BoardRunTimeConfig_Get(
            BOARDRUNTIME_STREAMING_CONFIGURATION);

    SCPI_ResultInt32(context, (int) pRunTimeStreamConfig->ActiveInterface);
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

    // Return in standard SCPI hexadecimal format with #H prefix
    SCPI_ResultUInt64Base(context, pBoardConfig->boardSerialNumber, 16);
    
    return SCPI_RES_OK;
}

scpi_result_t SCPI_Force5v5PowerStateSet(scpi_t * context) {
    tBoardRuntimeConfig * pBoardRuntimeConfig = BoardRunTimeConfig_Get(
            BOARDRUNTIMECONFIG_ALL_CONFIG);

    tPowerData * pPowerData = BoardData_Get(
            BOARDDATA_POWER_DATA,
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

static scpi_result_t SCPI_GetCommandHistory(scpi_t * context) {
    UsbCdcData_t* usbSettings = UsbCdc_GetSettings();
    char buffer[256];
    
    if (usbSettings->cmdHistoryCount == 0) {
        SCPI_ResultCharacters(context, "No command history", 18);
        return SCPI_RES_OK;
    }
    
    // Calculate starting position in circular buffer
    int startIdx = (usbSettings->cmdHistoryHead - usbSettings->cmdHistoryCount + SCPI_CMD_HISTORY_SIZE) % SCPI_CMD_HISTORY_SIZE;
    
    // Send header
    int len = snprintf(buffer, sizeof(buffer), "Last %d commands:\r\n", usbSettings->cmdHistoryCount);
    context->interface->write(context, buffer, len);
    
    // Send command history
    for (int i = 0; i < usbSettings->cmdHistoryCount; i++) {
        int idx = (startIdx + i) % SCPI_CMD_HISTORY_SIZE;
        len = snprintf(buffer, sizeof(buffer), "%d: %s\r\n", 
                      usbSettings->cmdHistoryCount - i, 
                      usbSettings->cmdHistory[idx]);
        context->interface->write(context, buffer, len);
    }
    
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

    //    // System
    {.pattern = "SYSTem:REboot", .callback = SCPI_Reset,},
    {.pattern = "HELP", .callback = SCPI_Help,},
    {.pattern = "SYSTem:SYSInfoPB?", .callback = SCPI_SysInfoGet,},
    {.pattern = "SYSTem:INFo?", .callback = SCPI_SysInfoTextGet,},
    {.pattern = "SYSTem:LOG?", .callback = SCPI_SysLogGet,},
    {.pattern = "SYSTem:LOG:TEST", .callback = SCPI_SysLogTest,},
    {.pattern = "SYSTem:LOG:CLEar", .callback = SCPI_SysLogClear,},
    {.pattern = "SYSTem:LOG:CMDHistory?", .callback = SCPI_GetCommandHistory,},
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
    {.pattern = "SYSTem:POWer:SOURce?", .callback = SCPI_PowerSourceGet,},
    {.pattern = "SYSTem:BAT:LEVel?", .callback = SCPI_BatteryLevelGet,},
    {.pattern = "SYSTem:POWer:STATe?", .callback = SCPI_GetPowerState,},
    {.pattern = "SYSTem:POWer:STATe", .callback = SCPI_SetPowerState,},
    {.pattern = "SYSTem:POWer:AUTO:EXTernal?", .callback = SCPI_GetAutoExtPower,},
    {.pattern = "SYSTem:POWer:AUTO:EXTernal", .callback = SCPI_SetAutoExtPower,},
    // OTG commands disabled - OTG mode is managed automatically by the power system
    // When external power is present, OTG is always disabled for safety
    // When on battery power, OTG is controlled by the board configuration
    // Manual control could be enabled in future if needed for testing
    // {.pattern = "SYSTem:POWer:OTG?", .callback = SCPI_GetOTGMode,},
    // {.pattern = "SYSTem:POWer:OTG", .callback = SCPI_SetOTGMode,},
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
    {.pattern = "SYSTem:COMMunicate:LAN:GATEway", .callback = SCPI_LANGatewaySet,},
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
    {.pattern = "SYSTem:COMMunicate:LAN:PASSCHECK?", .callback = SCPI_LANPasskeyGet,},
    {.pattern = "SYSTem:COMMunicate:LAN:DISPlay", .callback = SCPI_NotImplemented,},
    {.pattern = "SYSTem:COMMunicate:LAN:APPLY", .callback = SCPI_LANSettingsApply,},
    {.pattern = "SYSTem:COMMunicate:LAN:LOAD", .callback = SCPI_LANSettingsLoad,},
    {.pattern = "SYSTem:COMMunicate:LAN:SAVE", .callback = SCPI_LANSettingsSave,},
    {.pattern = "SYSTem:COMMunicate:LAN:FACRESET", .callback = SCPI_LANSettingsFactoryLoad,},
    //
    {.pattern = "SYSTem:COMMunicate:LAN:GETChipInfo?", .callback = SCPI_LANGetChipInfo,},
    // ADC
    {.pattern = "MEASure:VOLTage:DC?", .callback = SCPI_ADCVoltageGet,},
    {.pattern = "ENAble:VOLTage:DC", .callback = SCPI_ADCChanEnableSet,},
    {.pattern = "ENAble:VOLTage:DC?", .callback = SCPI_ADCChanEnableGet,},
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
    // DAC
    {.pattern = "SOURce:VOLTage:LEVel", .callback = SCPI_DACVoltageSet,},
    {.pattern = "SOURce:VOLTage:LEVel?", .callback = SCPI_DACVoltageGet,},
    {.pattern = "CONFigure:DAC:chanCALM", .callback = SCPI_DACChanCalmSet,},
    {.pattern = "CONFigure:DAC:chanCALB", .callback = SCPI_DACChanCalbSet,},
    {.pattern = "CONFigure:DAC:chanCALM?", .callback = SCPI_DACChanCalmGet,},
    {.pattern = "CONFigure:DAC:chanCALB?", .callback = SCPI_DACChanCalbGet,},
    {.pattern = "CONFigure:DAC:SAVEcal", .callback = SCPI_DACCalSave,},
    {.pattern = "CONFigure:DAC:SAVEFcal", .callback = SCPI_DACCalFSave,},
    {.pattern = "CONFigure:DAC:LOADcal", .callback = SCPI_DACCalLoad,},
    {.pattern = "CONFigure:DAC:LOADFcal", .callback = SCPI_DACCalFLoad,},
    {.pattern = "CONFigure:DAC:USECal", .callback = SCPI_DACUseCalSet,},
    {.pattern = "CONFigure:DAC:USECal?", .callback = SCPI_DACUseCalGet,},
    {.pattern = "CONFigure:DAC:UPDATE", .callback = SCPI_DACUpdate,},
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
    {.pattern = "SYSTem:STReam:INTerface", .callback = SCPI_SetStreamInterface,}, // 0=USB, 1=WiFi, 2=SD, 3=All
    {.pattern = "SYSTem:STReam:INTerface?", .callback = SCPI_GetStreamInterface,},
    {.pattern = "SYSTem:STReam:Stats?", .callback = SCPI_GetStreamStats,},
    {.pattern = "SYSTem:STReam:ClearStats", .callback = SCPI_ClearStreamStats,},
    //
    {.pattern = "SYSTem:STORage:SD:LOGging", .callback = SCPI_StorageSDLoggingSet,},
    {.pattern = "SYSTem:STORage:SD:GET", .callback = SCPI_StorageSDGetData},
    {.pattern = "SYSTem:STORage:SD:LISt?", .callback = SCPI_StorageSDListDir},
    {.pattern = "SYSTem:STORage:SD:ENAble", .callback = SCPI_StorageSDEnableSet},
    {.pattern = "SYSTem:STORage:SD:DELete", .callback = SCPI_StorageSDDelete},
    {.pattern = "SYSTem:STORage:SD:FORmat", .callback = SCPI_StorageSDFormat},
    {.pattern = "SYSTem:STORage:SD:BENCHmark", .callback = SCPI_StorageSDBenchmark},
    {.pattern = "SYSTem:STORage:SD:BENCHmark?", .callback = SCPI_StorageSDBenchmarkQuery},
    {.pattern = "SYSTem:STORage:SD:MAXSize", .callback = SCPI_StorageSDMaxSizeSet},
    {.pattern = "SYSTem:STORage:SD:MAXSize?", .callback = SCPI_StorageSDMaxSizeGet},
    // FreeRTOS
    //{.pattern = "SYSTem:OS:Stats?",           .callback = SCPI_GetFreeRtosStats,},
    // Testing
    {.pattern = "BENCHmark?", .callback = SCPI_NotImplemented,},
    {.pattern = NULL, .callback = SCPI_NotImplemented,},
};

#define SCPI_INPUT_BUFFER_LENGTH 128  // Increased from 64 to handle rapid command sequences
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
    // Construct model string from BoardConfig.BoardVariant (e.g., "Nq3")
    static char modelString[8] = {0};  // Initialize to prevent garbage data
    const tBoardConfig* pBoardConfig = BoardConfig_Get(BOARDCONFIG_ALL_CONFIG, 0);
    if (pBoardConfig) {
        snprintf(modelString, sizeof(modelString), "Nq%d", pBoardConfig->BoardVariant);
    } else {
        strncpy(modelString, "Nq?", sizeof(modelString));
        modelString[sizeof(modelString) - 1] = '\0';  // Ensure null termination
    }

    // Create a context
    scpi_t daqifiScpiContext;
    // Init context
    SCPI_Init(&daqifiScpiContext,
            scpi_commands,
            interface,
            scpi_units_def,
            SCPI_IDN1, modelString, SCPI_IDN3, SCPI_IDN4,
            scpi_input_buffer, SCPI_INPUT_BUFFER_LENGTH,
            scpi_error_queue_data, SCPI_ERROR_QUEUE_SIZE);

    // Return it to the app
    return daqifiScpiContext;
}
