#define LOG_LVL LOG_LEVEL_SCPI
#define LOG_MODULE LOG_MODULE_SCPI

#include "SCPIInterface.h"
#include "semphr.h"  // #347: mutex for SysInfoGet static buffer


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
#include "HAL/DioProbe.h"
#include "SCPIADC.h"
#include "SCPIDAC.h" 
#include "SCPIDIO.h"
#include "SCPILAN.h"
#include "services/wifi_services/wifi_manager.h"
#include "SCPIStorageSD.h"
#include "../sd_card_services/sd_card_manager.h"

/* SD write metrics accessed via sd_card_manager API */
#include "../streaming.h"
#include "../Capabilities.h"
#include "Util/StreamingBufferPool.h"
#include "state/data/AInSample.h"
#include "../csv_encoder.h"
#include "../JSON_Encoder.h"
#include "../../HAL/TimerApi/TimerApi.h"
#include "../UsbCdc/UsbCdc.h"
#include "config/default/driver/usb/usbhs/src/plib_usbhs_header.h"
#include "Util/CoherentPool.h"
#include "state/data/AInSample.h"  // For AInSampleList_PoolCapacity
#include "services/wifi_services/wifi_tcp_server.h"  // For WIFI_CIRCULAR_BUFF_SIZE
#include "services/wifi_services/iperf2/iperf2.h"   // #377 iperf2 control
#include "config/default/driver/winc/include/dev/wdrv_winc_spi.h"  // For WDRV_WINC_SPI_SetBuffer/WaitIdle
#include "config/default/WincIdleGate.h"  // For SYST:WINC:GATE? debug accessor
#ifndef DAQIFI_WINC_SPI_PATCHED
#error "wdrv_winc_spi.h was overwritten by Harmony/MCC! Re-apply DAQiFi patches. See wiki: Harmony-Driver-Patches"
#endif

//
// SCPI STATus:OPERation condition register bit assignments (IEEE 488.2 Sec 11.6.1)
#define OPER_MEASURING   (1 << 4)   // Bit 4: streaming/measuring active
#define OPER_SD_LOGGING  (1 << 10)  // Bit 10: SD card logging active

// SCPI STATus:QUEStionable condition register bit assignments
// These must match the QUES_BIT_* defines in streaming.c
#define QUES_DATA_LOSS      (1 << 4)   // Bit 4: windowed sample loss >= 5%
#define QUES_USB_OVERFLOW   (1 << 8)   // Bit 8: USB buffer overflow
#define QUES_WIFI_OVERFLOW  (1 << 9)   // Bit 9: WiFi buffer overflow
#define QUES_SD_OVERFLOW    (1 << 10)  // Bit 10: SD write failure
#define QUES_ENCODER_FAIL   (1 << 11)  // Bit 11: Encoder failure
#define QUES_TRANSPORT_DOWN (1 << 12)  // Bit 12: all configured transports down >grace (#397 auto-stop)
#define QUES_ALL_BITS       (QUES_DATA_LOSS | QUES_USB_OVERFLOW | QUES_WIFI_OVERFLOW | \
                             QUES_SD_OVERFLOW | QUES_ENCODER_FAIL | QUES_TRANSPORT_DOWN)

#define UNUSED(x) (void)(x)
//
#define SCPI_IDN1 "DAQiFi"
// SCPI_IDN2 (model) is constructed dynamically from BoardConfig.BoardVariant
// SCPI_IDN3 (serial number) is constructed dynamically from BoardConfig.boardSerialNumber (#436)
#define SCPI_IDN4 "01-02"

// File-scope IDN strings: built once by SCPI_InitIdentification() pre-scheduler
// and then read-only.  libscpi stores these as raw pointers in every per-
// transport SCPI context, so the storage must outlive every context.  Module-
// scope avoids the data race that function-static buffers would have when
// CreateSCPIContext() is called concurrently from USB and WiFi tasks (#441
// Qodo finding) — the writes happen before any SCPI task spawns.
static char gIdnModel[8]   = "Nq?";  // Filled from BoardConfig.BoardVariant
static char gIdnSerial[17] = "0";    // 16 hex digits of uint64 + null

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
 * Set or clear OPERC bits on both USB and WiFi SCPI contexts.
 * Device state (streaming, SD logging) is global, so both contexts
 * must reflect the same condition register values.
 */
static void SCPI_SetOperBits(scpi_reg_val_t bits) {
    UsbCdcData_t* usb = UsbCdc_GetSettings();
    if (usb) SCPI_RegSetBits(&usb->scpiContext, SCPI_REG_OPERC, bits);
    wifi_tcp_server_context_t* wifi = wifi_manager_GetTcpServerContext();
    if (wifi) SCPI_RegSetBits(&wifi->client.scpiContext, SCPI_REG_OPERC, bits);
}

/** Clear OPERC bits on both USB and WiFi SCPI contexts. */
static void SCPI_ClearOperBits(scpi_reg_val_t bits) {
    UsbCdcData_t* usb = UsbCdc_GetSettings();
    if (usb) SCPI_RegClearBits(&usb->scpiContext, SCPI_REG_OPERC, bits);
    wifi_tcp_server_context_t* wifi = wifi_manager_GetTcpServerContext();
    if (wifi) SCPI_RegClearBits(&wifi->client.scpiContext, SCPI_REG_OPERC, bits);
}

/**
 * Sync QUESC bits from the streaming engine to both SCPI contexts.
 * Called from SCPI_StopStreaming (to clear) and can be called
 * periodically or on-demand to refresh from streaming state.
 *
 * Uses a single SCPI_RegSet (read-modify-write) instead of separate
 * clear+set to avoid a transient 0-state that would latch a spurious
 * 1→0 event in the QUES event register.
 */
static void SCPI_SyncQuesBits(void) {
    uint32_t bits = Streaming_GetQuesBits();
    UsbCdcData_t* usb = UsbCdc_GetSettings();
    wifi_tcp_server_context_t* wifi = wifi_manager_GetTcpServerContext();
    // Replace streaming-related QUES bits in a single write to avoid
    // spurious event register transitions from a clear+set sequence.
    if (usb) {
        scpi_reg_val_t val = SCPI_RegGet(&usb->scpiContext, SCPI_REG_QUESC);
        val = (val & ~QUES_ALL_BITS) | bits;
        SCPI_RegSet(&usb->scpiContext, SCPI_REG_QUESC, val);
    }
    if (wifi) {
        scpi_reg_val_t val = SCPI_RegGet(&wifi->client.scpiContext, SCPI_REG_QUESC);
        val = (val & ~QUES_ALL_BITS) | bits;
        SCPI_RegSet(&wifi->client.scpiContext, SCPI_REG_QUESC, val);
    }
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
    
    // Reset the WINC chip via GPIO toggle BEFORE the PIC32 soft reset.
    // RCON_SoftwareReset() only resets the PIC32 — the WINC keeps running
    // with stale state and ends up out-of-sync with the freshly-booted
    // driver (#383). Firing DEINIT drives WDRV_WINC_Deinitialize +
    // wifi_manager_FixWincResetState (CHIP_EN/RESET_N GPIO toggle).
    //
    // Surface failure: if the event queue is uninitialized or saturated,
    // we'll proceed with the PIC32 reset even though the WINC won't have
    // been reset.  Better than wedging — the user's reboot still
    // happens, and the LOG_E gives them a breadcrumb.
    if (!wifi_manager_Deinit()) {
        LOG_E("SYST:REboot: wifi_manager_Deinit failed, WINC may not "
              "be reset before PIC32 reboot");
    }

    // Actively pump wifi_manager_ProcessStateNoTcpRx() during the 500 ms
    // settle — not just sleep.  Reason: TCP-SCPI dispatch runs on
    // app_WifiTask (post-#353), the same task that drains the WiFi event
    // queue.  A passive vTaskDelay there blocks the queue and DEINIT
    // never gets processed before RCON_SoftwareReset() reboots the chip.
    //
    // NoTcpRx variant skips the deferred TCP-rx drain so we don't
    // re-enter SCPI_Input() on the same scpiContext when *RST itself
    // arrived over TCP.
    {
        TickType_t start = xTaskGetTickCount();
        while ((xTaskGetTickCount() - start) < pdMS_TO_TICKS(500)) {
            wifi_manager_ProcessStateNoTcpRx();
            vTaskDelay(pdMS_TO_TICKS(5));
        }
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

// #347: Shared SCPI response scratch buffer. Any SCPI callback needing
// ≥256 B of scratch should use this instead of a stack local — keeps
// WifiTask's 1024-word stack safe on the TCP path (see SCPI_ResponseBuf_Take
// docs and issue #347 for the underlying stack-overflow story).
// Statically allocated mutex (configSUPPORT_STATIC_ALLOCATION=1) so there's
// no heap-allocation failure path.
_Static_assert(SCPI_RESPONSE_BUF_SIZE >= DaqifiOutMessage_size,
               "SCPI_RESPONSE_BUF_SIZE must hold the largest SCPI response");
/* Aligned to 8 bytes so callers can safely cast the pointer to any
 * scratch struct (e.g. DaqifiSettings, which contains uint32_t /
 * uint64_t fields).  PIC32MZ MIPS32 traps unaligned word accesses. */
static uint8_t gScpiRespBuf[SCPI_RESPONSE_BUF_SIZE] __attribute__((aligned(8)));
static StaticSemaphore_t gScpiRespMutexStorage;
static SemaphoreHandle_t gScpiRespMutex = NULL;

void SCPI_ResponseBuf_Init(void) {
    // Idempotent: if SCPI_ResponseBuf_Init is called more than once (e.g.
    // directly from app boot AND implicitly from the first CreateSCPIContext),
    // the second call is a no-op.
    //
    // The check-and-create pair is guarded by a critical section. The
    // intended caller is single-threaded (app_SystemInit runs pre-scheduler,
    // then CreateSCPIContext runs during serial boot-time transport init)
    // and taskENTER_CRITICAL is a no-op before the scheduler starts, so this
    // is cost-free in practice. The guard catches any future misuse where
    // SCPI_ResponseBuf_Init is invoked concurrently.
    taskENTER_CRITICAL();
    if (gScpiRespMutex == NULL) {
        gScpiRespMutex = xSemaphoreCreateMutexStatic(&gScpiRespMutexStorage);
    }
    taskEXIT_CRITICAL();
}

void SCPI_InitIdentification(void) {
    // #441 Qodo finding: CreateSCPIContext is called from both
    // UsbCdc_Initialize (USB task) and wifi_tcp_server_Initialize (WiFi
    // task) — concurrent snprintf() into shared gIdn* buffers would race
    // and libscpi stores the raw pointers, so corruption would persist in
    // the *IDN? response.  We avoid that race by populating the strings
    // sequentially in app_SystemInit, AFTER InitBoardConfig sets the
    // silicon serial AND BEFORE any of those transport tasks are created
    // (their xTaskCreate calls happen later in the same app_SystemInit
    // body).  The scheduler is technically running by this point — we're
    // inside the priority-1 APP_FREERTOS_Tasks boot task — but no higher-
    // priority task has reached CreateSCPIContext yet because none have
    // been created.
    const tBoardConfig* pBoardConfig = BoardConfig_Get(BOARDCONFIG_ALL_CONFIG, 0);
    if (pBoardConfig != NULL) {
        snprintf(gIdnModel, sizeof(gIdnModel), "Nq%d", pBoardConfig->BoardVariant);
        // SCPI 1999.0 §10.14.1 requires a real per-unit identifier.  Use the
        // silicon serial (DEVSN1:DEVSN0) the rest of the firmware already
        // exposes via SYST:SN? and the streaming CSV metadata header.
        snprintf(gIdnSerial, sizeof(gIdnSerial), "%016llX",
                 (unsigned long long)pBoardConfig->boardSerialNumber);
    }
    // pBoardConfig == NULL leaves the file-scope defaults ("Nq?", "0") in
    // place — same fallback as before, but now no concurrent-write hazard.
}

uint8_t* SCPI_ResponseBuf_Take(void) {
    // gScpiRespMutex is NULL only if SCPI_ResponseBuf_Init() was not called
    // before any SCPI dispatch — that would be a programming error. Defensive
    // NULL return keeps the device alive (caller returns SCPI_RES_ERR) rather
    // than dereferencing a NULL handle.
    if (gScpiRespMutex == NULL) {
        return NULL;
    }
    if (xSemaphoreTake(gScpiRespMutex, portMAX_DELAY) != pdTRUE) {
        return NULL;
    }
    return gScpiRespBuf;
}

void SCPI_ResponseBuf_Give(void) {
    if (gScpiRespMutex != NULL) {
        xSemaphoreGive(gScpiRespMutex);
    }
}

static scpi_result_t SCPI_SysInfoGet(scpi_t * context) {
    int param1;
    tBoardData * pBoardData = BoardData_Get(BOARDDATA_ALL_DATA, 0);

    if (!SCPI_ParamInt32(context, &param1, FALSE)) {
        param1 = 0;
    }

    uint8_t* buf = SCPI_ResponseBuf_Take();
    if (buf == NULL) {
        return SCPI_RES_ERR;
    }

    size_t count = Nanopb_Encode(
            pBoardData,
            (const NanopbFlagsArray *) &fields_info,
            buf, DaqifiOutMessage_size);
    scpi_result_t result = SCPI_RES_OK;
    if (count < 1) {
        result = SCPI_RES_ERR;
    } else {
        context->interface->write(context, (char*) buf, count);
    }

    SCPI_ResponseBuf_Give();
    return result;
}


/**
 * SCPI Callback: Returns system information in human-readable text format
 * @return SCPI_RES_OK on success
 */
static scpi_result_t SCPI_SysInfoTextGet(scpi_t * context) {
    const tBoardConfig* pBoardConfig = BoardConfig_Get(BOARDCONFIG_ALL_CONFIG, 0);
    tBoardData * pBoardData = BoardData_Get(BOARDDATA_ALL_DATA, 0);
    wifi_manager_settings_t * pWifiSettings = BoardData_Get(BOARDDATA_WIFI_SETTINGS, 0);
    AInRuntimeArray * pAInConfig = BoardRunTimeConfig_Get(BOARDRUNTIMECONFIG_AIN_CHANNELS);
    DIORuntimeArray * pDIOConfig = BoardRunTimeConfig_Get(BOARDRUNTIMECONFIG_DIO_CHANNELS);

    // Check for NULL pointers to prevent crashes (before taking the shared
    // buffer so we don't need to release it on this early return).
    if (!pBoardData || !pBoardConfig) {
        const char* err = !pBoardData ? "ERROR: BoardData not available\r\n"
                                      : "ERROR: BoardConfig not available\r\n";
        context->interface->write(context, err, strlen(err));
        return SCPI_RES_ERR;
    }

    // #347: use shared SCPI response scratch buffer (was stack-local char[256]).
    char* buffer = (char*)SCPI_ResponseBuf_Take();
    if (buffer == NULL) {
        return SCPI_RES_ERR;
    }

    // Header with device identification
    snprintf(buffer, SCPI_RESPONSE_BUF_SIZE, "=== DAQiFi Nyquist%d | HW:%s FW:%s ===\r\n",
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
        
        snprintf(buffer, SCPI_RESPONSE_BUF_SIZE, "  2.4GHz: On | Mode: %s | SSID: %s\r\n", 
            pWifiSettings->networkMode == WIFI_MANAGER_NETWORK_MODE_AP ? "AP" : "STA",
            pWifiSettings->ssid);
        context->interface->write(context, buffer, strlen(buffer));
        
        snprintf(buffer, SCPI_RESPONSE_BUF_SIZE, "  IP: %s | Port: %d | Security: %s\r\n", 
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
    bool vbusDetected = UsbCdc_IsVbusDetected();
    /* Read VBUS level directly from USB hardware register */
    USBHS_VBUS_LEVEL vbusLevel = PLIB_USBHS_VBUSLevelGet(USBHS_ID_0);
    const char* vbusLevelStr = "Unknown";
    switch (vbusLevel) {
        case USBHS_VBUS_SESSION_END: vbusLevelStr = "None"; break;
        case USBHS_VBUS_BELOW_AVALID: vbusLevelStr = "Low"; break;
        case USBHS_VBUS_BELOW_VBUSVALID: vbusLevelStr = "Medium"; break;
        case USBHS_VBUS_VALID: vbusLevelStr = "Valid"; break;
        default: break;
    }
    snprintf(buffer, SCPI_RESPONSE_BUF_SIZE, "  USB: %s | WiFi: %s | Ext power: %s | VBUS: %s (HW: %s)\r\n",
        hasUSBPower ? "Connected" : "Disconnected",
        wifiStatus == WIFI_STATUS_CONNECTED ? "Connected" :
        (wifiStatus == WIFI_STATUS_DISCONNECTED ? "Disconnected" : "Disabled"),
        pBoardData->PowerData.externalPowerSource != NO_EXT_POWER ? "Present" : "None",
        vbusDetected ? "Yes" : "No",
        vbusLevelStr);
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
    
    snprintf(buffer, SCPI_RESPONSE_BUF_SIZE, "  State: %s (%d) | USB: %s%s\r\n", 
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
        snprintf(buffer, SCPI_RESPONSE_BUF_SIZE, "  Battery: -- (--) [--] | Charging: %s\r\n", chargeStatus);
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
        snprintf(buffer, SCPI_RESPONSE_BUF_SIZE, "  Battery: %.2fV (%d%%) %s | Charging: %s\r\n", 
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
    snprintf(buffer, SCPI_RESPONSE_BUF_SIZE, "  User ADC: %d/%d | Internal ADC: %d/%d | DIO: %d/%d inputs\r\n", 
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
                snprintf(buffer, SCPI_RESPONSE_BUF_SIZE, "%d", channelId);
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
            snprintf(buffer, SCPI_RESPONSE_BUF_SIZE, "  DIO raw: %u (0x%04X)\r\n", sample.Values, sample.Values);
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
    snprintf(buffer, SCPI_RESPONSE_BUF_SIZE, "  Streaming: %s\r\n",
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
        snprintf(buffer, SCPI_RESPONSE_BUF_SIZE, "  ADC: %d%% | %.2fV\r\n",
            pBoardData->PowerData.chargePct,
            pBoardData->PowerData.battVoltage);
        context->interface->write(context, buffer, strlen(buffer));
    }
    
    // BQ24297 status - get fresh data
    tBQ24297Data * pBQ24297Data = &pBoardData->PowerData.BQ24297Data;
    if (pBQ24297Data->initComplete) {
        // UpdateBatteryStatus (not bare UpdateStatus) so batPresent is
        // recomputed live, not the boot-time value (stale-global audit).
        BQ24297_UpdateBatteryStatus();
        
        // Battery detection and charging
        const char* chgStatStr[] = {"Not Charging", "Pre-charge", "Fast Charge", "Charge Done"};
        snprintf(buffer, SCPI_RESPONSE_BUF_SIZE, "  BQ24297: Battery %s | Charging: %s\r\n",
            pBQ24297Data->status.batPresent ? "Present" : "Not Present",
            (pBQ24297Data->status.chgStat < 4) ? chgStatStr[pBQ24297Data->status.chgStat] : "Unknown");
        context->interface->write(context, buffer, strlen(buffer));
        
        // Power conditions with clear explanations
        snprintf(buffer, SCPI_RESPONSE_BUF_SIZE, "  vsysStat: %d (Battery >3.0V: %s) | pgStat: %d (Ext power: %s)\r\n",
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
        
        snprintf(buffer, SCPI_RESPONSE_BUF_SIZE, "  NTC: %s | Current limit: %s | OTG: %s (GPIO: %s)\r\n",
            (pBQ24297Data->status.ntcFault < 4) ? ntcStr[pBQ24297Data->status.ntcFault] : "Fault",
            (pBQ24297Data->status.inLim < 8) ? iLimStr[pBQ24297Data->status.inLim] : "Unknown",
            pBQ24297Data->status.otg ? "On" : "Off",
            otgGpioState ? "High" : "Low");
        context->interface->write(context, buffer, strlen(buffer));
        
        // Read REG01 and REG07 for detailed status
        uint8_t reg01 = 0, reg07 = 0;
        bool reg01Ok = BQ24297_Read_I2C(0x01, &reg01);
        bool reg07Ok = BQ24297_Read_I2C(0x07, &reg07);

        // BATFET status (REG07 bit 5: 0=enabled, 1=disabled)
        bool batfetEnabled = reg07Ok && !(reg07 & 0x20);

        // REG01 breakdown: [7:6]=watchdog, [5]=OTG, [4]=CHG_CONFIG, [3:1]=SYS_MIN, [0]=reserved
        if (reg01Ok && reg07Ok) {
            snprintf(buffer, SCPI_RESPONSE_BUF_SIZE, "  BATFET: %s | REG01: 0x%02X (OTG=%d, CHG=%d) | REG07: 0x%02X\r\n",
                batfetEnabled ? "Enabled" : "Disabled!",
                reg01,
                (reg01 >> 5) & 1,  // OTG bit
                (reg01 >> 4) & 1,  // Charge enable bit
                reg07);
        } else {
            snprintf(buffer, SCPI_RESPONSE_BUF_SIZE, "  BATFET: %s | REG01: %s | REG07: %s\r\n",
                reg07Ok ? (batfetEnabled ? "Enabled" : "Disabled!") : "ERR",
                reg01Ok ? "OK" : "ERR",
                reg07Ok ? "OK" : "ERR");
        }
        context->interface->write(context, buffer, strlen(buffer));
        
        // Power-up readiness - the key diagnostic info
        bool canPowerUp = (!pBQ24297Data->status.vsysStat || pBQ24297Data->status.pgStat);
        snprintf(buffer, SCPI_RESPONSE_BUF_SIZE, "  >>> Power-up ready: %s %s\r\n",
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

            // Iterate through available samples.  Use ConvertToVoltageByIndex
            // (passing the loop index `i` directly) instead of ConvertToVoltage
            // (which does a linear channel-ID lookup internally) — avoids
            // O(N²) work in the rail-monitoring path.
            // We do NOT gate on sample timestamp here: monitoring channels are
            // populated by ADC_Tasks polling (HAL/ADC.c:320), which doesn't
            // touch BOARDDATA_STREAMING_TIMESTAMP.  Pre-#379, the streaming
            // timer ran at boot and incidentally updated that timestamp; post-
            // #379 (which correctly only starts the timer when streaming is
            // enabled), monitoring samples have Timestamp=0 between boot and
            // first stream start.  The sample VALUES are valid regardless —
            // ADC reads via ADC_HandleAD7609Interrupt / MC12bADC_EosInterrupt
            // populate Value independently of any timer.  See #460.
            for (size_t i = 0; i < sampleCount; i++) {
                AInSample* sample = BoardData_Get(BOARDDATA_AIN_LATEST, i);
                if (!sample) continue;

                // Convert raw ADC value to voltage using ADC layer function
                double voltage = ADC_ConvertToVoltageByIndex(i, sample->Value);
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
            snprintf(buffer, SCPI_RESPONSE_BUF_SIZE, "  +3.3V: %s | +5V: %s | +10V: %s\r\n",
                str3_3, str5, str10);
            context->interface->write(context, buffer, strlen(buffer));

            snprintf(buffer, SCPI_RESPONSE_BUF_SIZE, "  VSYS: %s | VBATT: %s\r\n",
                strSys, strBatt);
            context->interface->write(context, buffer, strlen(buffer));

            // Display reference voltages
            snprintf(buffer, SCPI_RESPONSE_BUF_SIZE, "  2.5V Ref: %s | 5V Ref: %s\r\n",
                str2_5Ref, str5Ref);
            context->interface->write(context, buffer, strlen(buffer));

            // Stale data indicator: all monitoring channels are scanned
            // together by MODULE7, so a single age applies to all rails.
            uint32_t lastDiagTick = ADC_GetLastDiagScanTick();
            if (lastDiagTick > 0) {
                uint32_t ageTicks = xTaskGetTickCount() - lastDiagTick;
                uint32_t ageSec = ageTicks / configTICK_RATE_HZ;
                // Show stale indicator if data is older than 2 seconds
                if (ageSec >= 2) {
                    StreamingRuntimeConfig *pStrmCfg = BoardRunTimeConfig_Get(
                            BOARDRUNTIME_STREAMING_CONFIGURATION);
                    bool diagOff = pStrmCfg->Running && !pStrmCfg->OnboardDiagEnabled;
                    snprintf(buffer, SCPI_RESPONSE_BUF_SIZE,
                             "  * Stale: last update %lus ago%s\r\n",
                             (unsigned long)ageSec,
                             diagOff ? " (diag scanning disabled)" : "");
                    context->interface->write(context, buffer, strlen(buffer));
                }
            }
        } else {
            context->interface->write(context, "  Voltage monitoring unavailable\r\n", 34);
        }
    }

    SCPI_ResponseBuf_Give();
    return SCPI_RES_OK;
}

/**
 * Gets the system log
 * @param context
 * @return 
 */
static scpi_result_t SCPI_SysLogGet(scpi_t * context) {
    LogMessageDump(context);
    return SCPI_RES_OK;
}

/**
 * Tests the logging system by adding test messages
 * @param context
 * @return 
 */
static scpi_result_t SCPI_SysLogTest(scpi_t * context) {
    // Add test messages to the log (task context)
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

    LogMessageClear();
    context->interface->write(context, "Log cleared\n", 12);
    return SCPI_RES_OK;
}

/**
 * Sets the runtime log level for a module.
 * Usage: SYST:LOG:LEVel <module_name>,<level>
 *   module_name: POWER, WIFI, SD, USB, SCPI, ADC, DAC, STREAM, ENCODER, GENERAL
 *   level: 0=NONE, 1=ERROR, 2=INFO, 3=DEBUG
 * Example: SYST:LOG:LEV STREAM,2
 */
static scpi_result_t SCPI_SysLogLevelSet(scpi_t * context) {
    const char* moduleName;
    size_t moduleLen;
    int32_t level;

    if (!SCPI_ParamCharacters(context, &moduleName, &moduleLen, TRUE)) {
        return SCPI_RES_ERR;
    }
    if (!SCPI_ParamInt32(context, &level, TRUE)) {
        return SCPI_RES_ERR;
    }

    LogModule_t module;
    if (!Logger_FindModule(moduleName, moduleLen, &module)) {
        SCPI_ErrorPush(context, SCPI_ERROR_ILLEGAL_PARAMETER_VALUE);
        return SCPI_RES_ERR;
    }

    if (level < LOG_LEVEL_NONE || level > LOG_LEVEL_DEBUG) {
        SCPI_ErrorPush(context, SCPI_ERROR_DATA_OUT_OF_RANGE);
        return SCPI_RES_ERR;
    }

    uint8_t ceiling = Logger_GetCeiling(module);
    Logger_SetLevel(module, (uint8_t)level);
    uint8_t actual = Logger_GetLevel(module);

    char buf[80];
    int len = snprintf(buf, sizeof(buf), "%s: %d (ceiling %d)\r\n",
                       Logger_GetModuleName(module), actual, ceiling);
    if (len > 0) {
        context->interface->write(context, buf, ((size_t)len < sizeof(buf) - 1) ? (size_t)len : sizeof(buf) - 1);
    }

    return SCPI_RES_OK;
}

/**
 * Queries the runtime log level for a module.
 * Usage: SYST:LOG:LEVel? <module_name>
 *   If no parameter given, dumps all modules.
 */
static scpi_result_t SCPI_SysLogLevelGet(scpi_t * context) {
    const char* moduleName;
    size_t moduleLen;

    if (SCPI_ParamCharacters(context, &moduleName, &moduleLen, FALSE) && moduleLen > 0) {
        LogModule_t module;
        if (!Logger_FindModule(moduleName, moduleLen, &module)) {
            SCPI_ErrorPush(context, SCPI_ERROR_ILLEGAL_PARAMETER_VALUE);
            return SCPI_RES_ERR;
        }
        SCPI_ResultInt32(context, Logger_GetLevel(module));
    } else {
        /* No parameter — dump all modules */
        char buf[48];
        for (int i = 0; i < LOG_MODULE_COUNT; i++) {
            int len = snprintf(buf, sizeof(buf), "%s: %d (ceiling %d)\r\n",
                               Logger_GetModuleName((LogModule_t)i),
                               Logger_GetLevel((LogModule_t)i),
                               Logger_GetCeiling((LogModule_t)i));
            if (len > 0) {
                context->interface->write(context, buf, ((size_t)len < sizeof(buf) - 1) ? (size_t)len : sizeof(buf) - 1);
            }
        }
    }
    return SCPI_RES_OK;
}

/**
 * Sets all modules to the same runtime log level.
 * Usage: SYST:LOG:LEVel:ALL <level>
 *   level: 0=NONE, 1=ERROR, 2=INFO, 3=DEBUG
 */
static scpi_result_t SCPI_SysLogLevelAllSet(scpi_t * context) {
    int32_t level;

    if (!SCPI_ParamInt32(context, &level, TRUE)) {
        return SCPI_RES_ERR;
    }
    if (level < LOG_LEVEL_NONE || level > LOG_LEVEL_DEBUG) {
        SCPI_ErrorPush(context, SCPI_ERROR_DATA_OUT_OF_RANGE);
        return SCPI_RES_ERR;
    }

    Logger_SetAllLevels((uint8_t)level);

    /* Echo result showing actual levels (may differ due to ceilings) */
    char buf[48];
    for (int i = 0; i < LOG_MODULE_COUNT; i++) {
        int len = snprintf(buf, sizeof(buf), "%s: %d\r\n",
                           Logger_GetModuleName((LogModule_t)i),
                           Logger_GetLevel((LogModule_t)i));
        if (len > 0) {
            context->interface->write(context, buf, ((size_t)len < sizeof(buf) - 1) ? (size_t)len : sizeof(buf) - 1);
        }
    }
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

    // #400: when transitioning from POWERED_UP* → STANDBY, clean up WiFi
    // BEFORE PowerAndUITask runs Power_Down (which kills the WINC's 3.3V
    // rail). Without this, app_WifiTask sees the power-state change only
    // AFTER rails drop, calls wifi_manager_Deinit on a chip that no longer
    // has power, gets stuck in WDRV_WINC_Status==SYS_STATUS_BUSY forever,
    // and every subsequent POW:STAT 1 cycle never recovers WiFi (all
    // SYST:COMM:LAN:* queries return -200 "WiFi not ready" indefinitely).
    //
    // Mirrors SCPI_Reset's pattern (above): wifi_manager_Deinit() just
    // queues an event; we must pump wifi_manager_ProcessState() actively
    // because if this SCPI command arrived over TCP it's running on
    // app_WifiTask itself (post-#353) — sleeping there would deadlock
    // the queue. 500 ms is enough for healthy WDRV_WINC_Deinitialize
    // (~50 ms) plus margin for a busy HIF queue.
    if (param1 == 0 &&
        (pPowerData->powerState == POWERED_UP ||
         pPowerData->powerState == POWERED_UP_EXT_DOWN)) {
        if (!wifi_manager_Deinit()) {
            LOG_E("SYST:POW:STAT 0: wifi_manager_Deinit failed; "
                  "WINC may wedge if rails drop before chip is idle");
        }
        // NoTcpRx variant: skips the deferred TCP-rx drain so we don't
        // re-enter SCPI_Input() on the same scpiContext when this SCPI
        // command itself arrived over TCP.  Mirrors SCPI_Reset.
        TickType_t start = xTaskGetTickCount();
        while ((xTaskGetTickCount() - start) < pdMS_TO_TICKS(500)) {
            wifi_manager_ProcessStateNoTcpRx();
            vTaskDelay(pdMS_TO_TICKS(5));
        }
    }

    switch (param1) {
        case 0:  // STANDBY
            pPowerData->requestedPowerState = DO_POWER_DOWN;
            // #454: latch the per-VBUS-session auto-promote suppression
            // so the Standby handler doesn't immediately re-promote.
            // The latch already clears when VBUS goes away.
            pPowerData->autoPromotedThisVbusSession = true;
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

/**
 * #454 SCPI: SYSTem:POWer:AUTOOn 0|1
 * When enabled, the device auto-transitions STANDBY → POWERED_UP
 * whenever VBUS is detected (at boot or on cable insertion mid-session).
 * Runtime-only; use :SAVE to persist to NVM.
 */
static scpi_result_t SCPI_SetAutoPowerOnUsb(scpi_t * context) {
    int param1;
    if (!SCPI_ParamInt32(context, &param1, TRUE)) {
        return SCPI_RES_ERR;
    }
    if (param1 < 0 || param1 > 1) {
        SCPI_ErrorPush(context, SCPI_ERROR_ILLEGAL_PARAMETER_VALUE);
        return SCPI_RES_ERR;
    }
    tPowerData *pPowerData = BoardData_Get(BOARDDATA_POWER_DATA, 0);
    pPowerData->autoPowerOnUsb = (param1 != 0);
    /* Re-arm the per-session latch so toggling 0→1 with USB already
     * plugged in triggers the auto-promote on the next Power_Tasks
     * call instead of waiting for the next VBUS rising-edge. */
    pPowerData->autoPromotedThisVbusSession = false;
    BoardData_Set(BOARDDATA_POWER_DATA, 0, pPowerData);
    return SCPI_RES_OK;
}

static scpi_result_t SCPI_GetAutoPowerOnUsb(scpi_t * context) {
    tPowerData *pPowerData = BoardData_Get(BOARDDATA_POWER_DATA, 0);
    SCPI_ResultInt32(context, pPowerData->autoPowerOnUsb ? 1 : 0);
    return SCPI_RES_OK;
}

static scpi_result_t SCPI_SaveAutoPowerOnUsb(scpi_t * context) {
    /* Use shared SCPI response buffer instead of stack-local —
     * DaqifiSettings is ~500 bytes (union includes WiFi settings),
     * and WiFi-task stack peak is ~780 words / 3120 bytes already.
     * Pattern matches the SCPI buffer-discipline rule (#347).
     */
    DaqifiSettings *pSettings = (DaqifiSettings *)SCPI_ResponseBuf_Take();
    if (pSettings == NULL) {
        SCPI_ErrorPush(context, SCPI_ERROR_SYSTEM_ERROR);
        return SCPI_RES_ERR;
    }
    memset(pSettings, 0, sizeof(*pSettings));
    /* Load existing NVM to preserve other TopLevelSettings fields.
     * Set autoPowerOnUsb from PowerData runtime explicitly (don't rely
     * on SaveToNvm's auto-capture — that path would also affect
     * unrelated saves like CONF:VOLT:SAVE).
     */
    if (!daqifi_settings_LoadFromNvm(DaqifiSettings_TopLevelSettings, pSettings)) {
        daqifi_settings_LoadFactoryDeafult(DaqifiSettings_TopLevelSettings, pSettings);
    }
    pSettings->type = DaqifiSettings_TopLevelSettings;
    tPowerData *pPwr = BoardData_Get(BOARDDATA_POWER_DATA, 0);
    if (pPwr != NULL) {
        pSettings->settings.topLevelSettings.autoPowerOnUsb = pPwr->autoPowerOnUsb;
    }
    bool ok = daqifi_settings_SaveToNvm(pSettings);
    SCPI_ResponseBuf_Give();
    return ok ? SCPI_RES_OK : SCPI_RES_ERR;
}

static scpi_result_t SCPI_LoadAutoPowerOnUsb(scpi_t * context) {
    DaqifiSettings *pSettings = (DaqifiSettings *)SCPI_ResponseBuf_Take();
    if (pSettings == NULL) {
        SCPI_ErrorPush(context, SCPI_ERROR_SYSTEM_ERROR);
        return SCPI_RES_ERR;
    }
    memset(pSettings, 0, sizeof(*pSettings));
    if (!daqifi_settings_LoadFromNvm(DaqifiSettings_TopLevelSettings, pSettings)) {
        SCPI_ResponseBuf_Give();
        return SCPI_RES_ERR;
    }
    tPowerData *pPowerData = BoardData_Get(BOARDDATA_POWER_DATA, 0);
    pPowerData->autoPowerOnUsb = pSettings->settings.topLevelSettings.autoPowerOnUsb;
    pPowerData->autoPromotedThisVbusSession = false;  // re-arm
    BoardData_Set(BOARDDATA_POWER_DATA, 0, pPowerData);
    SCPI_ResponseBuf_Give();
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

/**
 * SCPI Callback: Dump all BQ24297 registers for debugging
 * Command: SYSTem:POWer:BQ:REGisters?
 * Returns: All 11 registers (REG00-REG0A) in hex format with decoded fields
 */
static scpi_result_t SCPI_GetBQRegisters(scpi_t * context) {
    // Read all registers including REG0A, tracking I2C errors
    // Skip REG09 in the loop — it's a latched register that needs double-read protocol
    uint8_t regs[11] = {0};
    bool regOk[11] = {false};
    int errCount = 0;
    for (int i = 0; i < 11; i++) {
        if (i == 9) { regOk[i] = false; continue; }  // REG09 handled separately below
        regOk[i] = BQ24297_Read_I2C(i, &regs[i]);
        if (!regOk[i]) errCount++;
    }

    // Read REG09 through centralized reader (accumulates faults properly)
    uint8_t reg09Latched = 0, reg09Current = 0;
    if (!BQ24297_ReadFaultReg(&reg09Latched, &reg09Current)) {
        regOk[9] = false;
        errCount++;
    } else {
        regOk[9] = true;
        regs[9] = reg09Latched;  // Use latched for the "regs" display
    }

    // If all reads failed, I2C bus is down
    if (errCount == 11) {
        scpi_printf(context, "I2C error: all register reads failed (0xFF)\r\n");
        SCPI_ErrorPush(context, SCPI_ERROR_SYSTEM_ERROR);
        return SCPI_RES_ERR;
    }

    // REG00: Input Source Control
    if (!regOk[0]) {
        scpi_printf(context, "REG00=ERR (I2C read failed)\r\n");
    } else {
        scpi_printf(context, "REG00=0x%02X HIZ=%d VINDPM=%d ILIM=%d\r\n",
            regs[0], (regs[0] >> 7) & 0x01, (regs[0] >> 3) & 0x0F, regs[0] & 0x07);
    }

    // REG01: Power-On Config
    if (!regOk[1]) {
        scpi_printf(context, "REG01=ERR (I2C read failed)\r\n");
    } else {
        scpi_printf(context, "REG01=0x%02X OTG=%d CHG=%d SYS_MIN=%d\r\n",
            regs[1], (regs[1] >> 5) & 0x01, (regs[1] >> 4) & 0x01, (regs[1] >> 1) & 0x07);
    }

    // REG02: Charge Current Control
    if (!regOk[2]) {
        scpi_printf(context, "REG02=ERR (I2C read failed)\r\n");
    } else {
        scpi_printf(context, "REG02=0x%02X ICHG=%d BCOLD=%d\r\n",
            regs[2], (regs[2] >> 2) & 0x3F, regs[2] & 0x01);
    }

    // REG03: Pre-Charge/Termination Current
    if (!regOk[3]) {
        scpi_printf(context, "REG03=ERR (I2C read failed)\r\n");
    } else {
        scpi_printf(context, "REG03=0x%02X IPRECHG=%d ITERM=%d\r\n",
            regs[3], (regs[3] >> 4) & 0x0F, regs[3] & 0x0F);
    }

    // REG04: Charge Voltage Control
    if (!regOk[4]) {
        scpi_printf(context, "REG04=ERR (I2C read failed)\r\n");
    } else {
        scpi_printf(context, "REG04=0x%02X VREG=%d BATLOWV=%d VRECHG=%d\r\n",
            regs[4], (regs[4] >> 2) & 0x3F, (regs[4] >> 1) & 0x01, regs[4] & 0x01);
    }

    // REG05: Charge Termination/Timer Control
    if (!regOk[5]) {
        scpi_printf(context, "REG05=ERR (I2C read failed)\r\n");
    } else {
        scpi_printf(context, "REG05=0x%02X EN_TERM=%d WDOG=%d EN_TIMER=%d\r\n",
            regs[5], (regs[5] >> 7) & 0x01, (regs[5] >> 4) & 0x03, (regs[5] >> 3) & 0x01);
    }

    // REG06: Boost Voltage/Thermal Regulation
    if (!regOk[6]) {
        scpi_printf(context, "REG06=ERR (I2C read failed)\r\n");
    } else {
        scpi_printf(context, "REG06=0x%02X BOOSTV=%d BHOT=%d TREG=%d\r\n",
            regs[6], (regs[6] >> 4) & 0x0F, (regs[6] >> 2) & 0x03, regs[6] & 0x03);
    }

    // REG07: Misc Operation Control
    if (!regOk[7]) {
        scpi_printf(context, "REG07=ERR (I2C read failed)\r\n");
    } else {
        scpi_printf(context, "REG07=0x%02X DPDM=%d TMR2X=%d BATFET=%d INT=%d\r\n",
            regs[7], (regs[7] >> 7) & 0x01, (regs[7] >> 6) & 0x01,
            (regs[7] >> 5) & 0x01, regs[7] & 0x03);
    }

    // REG08: System Status (key register for DPDM result)
    if (!regOk[8]) {
        scpi_printf(context, "REG08=ERR (I2C read failed)\r\n");
    } else {
        const char* vbusStr[] = {"Unknown", "USB_SDP", "Adapter", "OTG"};
        const char* chgStr[] = {"NotChg", "PreChg", "FastChg", "Done"};
        scpi_printf(context, "REG08=0x%02X VBUS=%s CHG=%s DPM=%d PG=%d THERM=%d VSYS=%d\r\n",
            regs[8], vbusStr[(regs[8] >> 6) & 0x03], chgStr[(regs[8] >> 4) & 0x03],
            (regs[8] >> 3) & 0x01, (regs[8] >> 2) & 0x01,
            (regs[8] >> 1) & 0x01, regs[8] & 0x01);
    }

    // REG09: Fault Status — values from centralized reader (faults accumulated)
    if (!regOk[9]) {
        scpi_printf(context, "REG09=ERR (I2C read failed)\r\n");
    } else {
        scpi_printf(context, "REG09=0x%02X (latched) WDOG_FLT=%d OTG_FLT=%d CHG_FLT=%d BAT_FLT=%d NTC=%d\r\n",
            reg09Latched, (reg09Latched >> 7) & 0x01, (reg09Latched >> 6) & 0x01,
            (reg09Latched >> 4) & 0x03, (reg09Latched >> 3) & 0x01, reg09Latched & 0x03);
        scpi_printf(context, "REG09=0x%02X (current) WDOG_FLT=%d OTG_FLT=%d CHG_FLT=%d BAT_FLT=%d NTC=%d\r\n",
            reg09Current, (reg09Current >> 7) & 0x01, (reg09Current >> 6) & 0x01,
            (reg09Current >> 4) & 0x03, (reg09Current >> 3) & 0x01, reg09Current & 0x03);
    }

    // REG0A: Vendor/Part/Revision (read-only)
    // PN[2:0] bits [7:5]: 001=BQ24296, 011=BQ24297
    // Rev[2:0] bits [2:0]: device revision
    if (!regOk[10]) {
        scpi_printf(context, "REG0A=ERR (I2C read failed)\r\n");
    } else {
        uint8_t pn = (regs[10] >> 5) & 0x07;
        uint8_t rev = regs[10] & 0x07;
        const char* pnStr = (pn == 3) ? "BQ24297" :
                             (pn == 1) ? "BQ24296" : "UNKNOWN";
        scpi_printf(context, "REG0A=0x%02X PN=%d (%s) REV=%d%s\r\n",
            regs[10], pn, pnStr, rev,
            (pn != 3) ? " WARNING: expected BQ24297 (PN=3)" : "");
    }

    if (errCount > 0) {
        scpi_printf(context, "WARNING: %d register(s) returned I2C error\r\n", errCount);
    }

    return SCPI_RES_OK;
}

/**
 * SCPI Callback: Set BQ24297 ILIM (input current limit)
 * Command: SYSTem:POWer:BQ:ILIM <value>
 * Values: 0=100mA, 1=150mA, 2=500mA, 3=900mA, 4=1A, 5=1.5A, 6=2A, 7=3A
 */
static scpi_result_t SCPI_SetBQILim(scpi_t * context) {
    int32_t ilim;

    if (!SCPI_ParamInt32(context, &ilim, TRUE)) {
        return SCPI_RES_ERR;
    }

    if (ilim < 0 || ilim > ILim_3000) {
        SCPI_ErrorPush(context, SCPI_ERROR_ILLEGAL_PARAMETER_VALUE);
        return SCPI_RES_ERR;
    }

    bool success = BQ24297_SetIINLIM((uint8_t)ilim);
    if (!success) {
        LOG_E("SCPI_SetBQILim: I2C write failed for ILIM=%d", (int)ilim);
        SCPI_ErrorPush(context, SCPI_ERROR_SYSTEM_ERROR);
        return SCPI_RES_ERR;
    }

    // Verify by reading back
    uint8_t readback;
    if (!BQ24297_Read_I2C(0x00, &readback)) {
        LOG_E("SCPI_SetBQILim: readback failed for REG00");
        SCPI_ErrorPush(context, SCPI_ERROR_SYSTEM_ERROR);
        return SCPI_RES_ERR;
    }

    uint8_t actual = readback & 0x07;
    uint8_t hiz = (readback >> 7) & 0x01;
    if (actual != (uint8_t)ilim || hiz != 0) {
        LOG_E("SCPI_SetBQILim: verify failed: wrote ILIM=%d, read ILIM=%d HIZ=%d (REG00=0x%02X)",
                 (int)ilim, actual, hiz, readback);
        SCPI_ErrorPush(context, SCPI_ERROR_SYSTEM_ERROR);
        return SCPI_RES_ERR;
    }

    // Prevent IINLIM state machine from overriding the manual setting
    tPowerData* pPower = (tPowerData*)BoardData_Get(BOARDDATA_POWER_DATA, 0);
    if (pPower == NULL) {
        LOG_E("SCPI_SetBQILim: power data not available to lock IINLIM state");
        SCPI_ErrorPush(context, SCPI_ERROR_SYSTEM_ERROR);
        return SCPI_RES_ERR;
    }
    pPower->BQ24297Data.iinlimState = IINLIM_STATE_SETTLED;

    scpi_printf(context, "ILIM=%d HIZ=%d Readback=0x%02X OK\r\n", actual, hiz, readback);
    return SCPI_RES_OK;
}

/**
 * SCPI Callback: Force DPDM detection
 * Command: SYSTem:POWer:BQ:DPDM
 * Triggers BQ24297 to re-run D+/D- detection.
 *
 * This is a diagnostic-only command. The detection result (VBUS type from
 * REG08) is printed to the user and then discarded — it does NOT update
 * the BQ24297 status struct or reset the IINLIM state machine. Therefore
 * no automatic current switching occurs based on the result. To change
 * IINLIM after forced DPDM, use SYST:POW:BQ:ILIM manually.
 *
 * WARNING: Forcing DPDM while connected via USB will disrupt communication.
 * DPDM re-detection temporarily resets IINLIM (potentially to 100mA),
 * which can cause VBUS sag and USB disconnect on the host side.
 * A physical cable replug is required to recover USB and restart the
 * IINLIM state machine. Only use when debugging wall charger behavior.
 */
static scpi_result_t SCPI_ForceDPDM(scpi_t * context) {
    // Read REG07
    uint8_t reg7;
    if (!BQ24297_Read_I2C(0x07, &reg7)) {
        scpi_printf(context, "I2C error: cannot read REG07\r\n");
        SCPI_ErrorPush(context, SCPI_ERROR_SYSTEM_ERROR);
        return SCPI_RES_ERR;
    }

    // Set DPDM_EN bit (bit 7) to force detection
    if (!BQ24297_Write_I2C(0x07, reg7 | 0x80)) {
        scpi_printf(context, "I2C error: cannot write REG07\r\n");
        SCPI_ErrorPush(context, SCPI_ERROR_SYSTEM_ERROR);
        return SCPI_RES_ERR;
    }

    // Wait for detection to complete (poll bit 7)
    int timeout = 20;  // 2 seconds max
    uint8_t status = 0;
    bool i2cOk = true;
    do {
        vTaskDelay(pdMS_TO_TICKS(100));
        if (!BQ24297_Read_I2C(0x07, &status)) { i2cOk = false; break; }
        timeout--;
    } while ((status & 0x80) && timeout > 0);

    if (!i2cOk) {
        scpi_printf(context, "I2C error during DPDM polling\r\n");
        SCPI_ErrorPush(context, SCPI_ERROR_SYSTEM_ERROR);
        return SCPI_RES_ERR;
    }

    if (timeout <= 0 && (status & 0x80)) {
        scpi_printf(context, "DPDM timeout\r\n");
        SCPI_ErrorPush(context, SCPI_ERROR_SYSTEM_ERROR);
        return SCPI_RES_ERR;
    }

    // Read result from REG08
    uint8_t reg8;
    const char* vbusStr[] = {"Unknown", "USB_SDP", "Adapter", "OTG"};

    if (!BQ24297_Read_I2C(0x08, &reg8)) {
        scpi_printf(context, "DPDM complete but REG08 read failed\r\n");
        SCPI_ErrorPush(context, SCPI_ERROR_SYSTEM_ERROR);
        return SCPI_RES_ERR;
    }

    uint8_t vbusStat = (reg8 >> 6) & 0x03;
    scpi_printf(context, "DPDM complete: VBUS=%s (%d), REG08=0x%02X\r\n",
             vbusStr[vbusStat], vbusStat, reg8);
    return SCPI_RES_OK;
}

/**
 * SCPI Callback: Clear accumulated BQ24297 faults
 * Command: SYSTem:POWer:BQ:FAULT:CLEar
 */
static scpi_result_t SCPI_ClearBQFaults(scpi_t * context) {
    (void)context;
    BQ24297_ClearAccumulatedFaults();
    return SCPI_RES_OK;
}

/**
 * SCPI Callback: Comprehensive BQ24297 diagnostics report
 * Command: SYSTem:POWer:BQ:DIAGnostics?
 * Returns: Multi-section human-readable diagnostic dump
 */
static scpi_result_t SCPI_GetBQDiagnostics(scpi_t * context) {
    tPowerData* pPower = (tPowerData*)BoardData_Get(
            BOARDDATA_POWER_DATA,
            0);
    if (pPower == NULL) {
        LOG_E("SCPI_GetBQDiagnostics: power data not available");
        scpi_printf(context, "Power data not available\r\n");
        SCPI_ErrorPush(context, SCPI_ERROR_SYSTEM_ERROR);
        return SCPI_RES_ERR;
    }
    tBQ24297Data* pBQ = &pPower->BQ24297Data;

    // Refresh cached status from hardware. Use UpdateBatteryStatus (not the
    // bare UpdateStatus) so batPresent/chargeAllowed are recomputed from live
    // NTC+voltage — they are otherwise only computed once at boot, so this
    // diagnostic would print a stale battery-presence after a runtime
    // insert/remove (stale-global audit). The ChargeEnable re-enforce inside
    // only fires when chargeAllowed actually changes, so steady-state polling
    // has no side effect.
    BQ24297_UpdateBatteryStatus();

    // --- [Battery] ---
    scpi_printf(context, "[Battery]\r\n");
    scpi_printf(context, "  Voltage: %.2fV | Charge: %d%% | Low: %s\r\n",
        pPower->battVoltage, pPower->chargePct,
        pPower->battLow ? "Yes" : "No");
    scpi_printf(context, "  Present: %s | Charge allowed: %s\r\n",
        pBQ->status.batPresent ? "Yes" : "No",
        pBQ->chargeAllowed ? "Yes" : "No");

    // --- [BQ24297 Registers] ---
    scpi_printf(context, "[BQ24297 Registers]\r\n");

    // Read raw registers for fields not in status struct
    uint8_t reg00, reg01, reg07, reg08;
    bool reg00Ok = BQ24297_Read_I2C(0x00, &reg00);
    bool reg01Ok = BQ24297_Read_I2C(0x01, &reg01);
    bool reg07Ok = BQ24297_Read_I2C(0x07, &reg07);
    bool reg08Ok = BQ24297_Read_I2C(0x08, &reg08);

    // Read REG09 through centralized reader (accumulates faults properly)
    uint8_t reg09Latched = 0, reg09Current = 0;
    bool reg09Ok = BQ24297_ReadFaultReg(&reg09Latched, &reg09Current);
    uint8_t reg09 = reg09Ok ? reg09Current : 0xFF;

    const char* iinlimStr[] = {"100mA","150mA","500mA","900mA","1A","1.5A","2A","3A"};
    if (!reg00Ok) {
        scpi_printf(context, "  REG00=ERR (I2C read failed)\r\n");
    } else {
        uint8_t vindpm = (reg00 >> 3) & 0x0F;
        uint8_t iinlim = reg00 & 0x07;
        scpi_printf(context,
            "  REG00=0x%02X (Input):  HIZ=%d | VINDPM=%dmV (%d) | IINLIM=%s (%d)\r\n",
            reg00, (reg00 >> 7) & 1, 3880 + vindpm * 80, vindpm,
            iinlimStr[iinlim], iinlim);
    }

    if (!reg01Ok) {
        scpi_printf(context, "  REG01=ERR (I2C read failed)\r\n");
    } else {
        uint8_t sysMin = (reg01 >> 1) & 0x07;
        scpi_printf(context,
            "  REG01=0x%02X (Config): OTG=%d | CHG=%d | SYS_MIN=%dmV (%d)\r\n",
            reg01, (reg01 >> 5) & 1, (reg01 >> 4) & 1, 3000 + sysMin * 100, sysMin);
    }

    if (!reg07Ok) {
        scpi_printf(context, "  REG07=ERR (I2C read failed)\r\n");
    } else {
        scpi_printf(context,
            "  REG07=0x%02X (Misc):   DPDM=%d | BATFET=%s (%d)\r\n",
            reg07, (reg07 >> 7) & 1,
            (reg07 & 0x20) ? "Disabled" : "Enabled", (reg07 >> 5) & 1);
    }

    const char* vbusStr[] = {"Unknown","USB_SDP","Adapter","OTG"};
    const char* chgStr[] = {"Not charging","Pre-charge","Fast charge","Charge done"};
    if (!reg08Ok) {
        scpi_printf(context, "  REG08=ERR (I2C read failed)\r\n");
    } else {
        uint8_t vbusStat = (reg08 >> 6) & 0x03;
        uint8_t chgStat = (reg08 >> 4) & 0x03;
        scpi_printf(context,
            "  REG08=0x%02X (Status): VBUS=%s (%d) | Charge=%s (%d) | PG=%d | DPM=%d | THERM=%d | VSYS=%d\r\n",
            reg08, vbusStr[vbusStat], vbusStat, chgStr[chgStat], chgStat,
            (reg08 >> 2) & 1, (reg08 >> 3) & 1, (reg08 >> 1) & 1, reg08 & 1);
    }

    const char* chgFaultStr[] = {"Normal","Input fault","Thermal","Timer"};
    const char* ntcFaultStr[] = {"Ok","Hot","Cold","Hot/Cold"};
    if (!reg09Ok) {
        scpi_printf(context, "  REG09=ERR (I2C read failed)\r\n");
    } else {
        uint8_t chgFault = (reg09 >> 4) & 0x03;
        uint8_t ntcFault = reg09 & 0x03;
        scpi_printf(context,
            "  REG09=0x%02X (Faults): Watchdog=%d | OTG=%d | CHG=%s (%d) | BAT=%d | NTC=%s (%d)\r\n",
            reg09, (reg09 >> 7) & 1, (reg09 >> 6) & 1,
            chgFaultStr[chgFault], chgFault, (reg09 >> 3) & 1,
            (ntcFault < 4) ? ntcFaultStr[ntcFault] : "Fault", ntcFault);
    }

    // --- [Accumulated Faults] ---
    {
        const char* chgFaultAccStr[] = {"Normal","Input fault","Thermal","Timer"};
        const char* ntcFaultAccStr[] = {"Ok","Hot","Cold","Hot/Cold"};
        uint8_t chgA = (uint8_t)pBQ->status.chgFaultAccum;
        uint8_t ntcA = (uint8_t)pBQ->status.ntcFaultAccum;
        scpi_printf(context, "[Accumulated Faults]\r\n");
        scpi_printf(context, "  Watchdog=%d | OTG=%d | CHG=%s (%d) | BAT=%d | NTC=%s (%d)\r\n",
            pBQ->status.watchdog_faultAccum ? 1 : 0,
            pBQ->status.otg_faultAccum ? 1 : 0,
            (chgA < 4) ? chgFaultAccStr[chgA] : "Unknown", chgA,
            pBQ->status.bat_faultAccum ? 1 : 0,
            (ntcA < 4) ? ntcFaultAccStr[ntcA] : "Unknown", ntcA);
    }

    // --- [GPIO] ---
    scpi_printf(context, "[GPIO]\r\n");
    scpi_printf(context, "  STAT (RH11): %d (%s) [%s]\r\n",
        (int)BATT_MAN_STAT_Get(),
        BATT_MAN_STAT_Get() ? "Not charging" : "Charging",
        (TRISH >> 11) & 1 ? "Input" : "Output");
    scpi_printf(context, "  OTG  (RK5):  %d (%s) [%s]\r\n",
        (int)BATT_MAN_OTG_Get(),
        BATT_MAN_OTG_Get() ? "HIGH" : "LOW",
        (TRISK >> 5) & 1 ? "Input" : "Output");
    scpi_printf(context, "  INT  (RA4):  %d [%s]\r\n",
        (int)BATT_MAN_INT_Get(),
        (TRISA >> 4) & 1 ? "Input" : "Output");

    // --- [IINLIM State Machine] ---
    scpi_printf(context, "[IINLIM State Machine]\r\n");
    const char* iinlimStateStr[] = {"IDLE","WAIT_DPDM","WAIT_USB","SETTLED"};
    uint8_t stateIdx = (uint8_t)pBQ->iinlimState;
    scpi_printf(context, "  State: %s (%d) | Last VBUS: %s\r\n",
        stateIdx < 4 ? iinlimStateStr[stateIdx] : "UNKNOWN", stateIdx,
        pBQ->iinlimLastVbus ? "Yes" : "No");

    // --- [Power] ---
    scpi_printf(context, "[Power]\r\n");
    const char* powerStateStr[] = {"STANDBY","POWERED_UP","POWERED_UP_EXT_DOWN"};
    const char* extPowerStr[] = {"NONE","UNKNOWN","CHARGER_1A","CHARGER_2A","USB_100MA","USB_500MA"};
    uint8_t ps = (uint8_t)pPower->powerState;
    uint8_t ep = (uint8_t)pPower->externalPowerSource;
    scpi_printf(context, "  State: %s (%d) | Ext power: %s (%d)\r\n",
        ps < 3 ? powerStateStr[ps] : "UNKNOWN", ps,
        ep < 6 ? extPowerStr[ep] : "UNKNOWN", ep);

    USBHS_VBUS_LEVEL vbusLevel = PLIB_USBHS_VBUSLevelGet(USBHS_ID_0);
    const char* vbusLevelName;
    switch (vbusLevel) {
        case USBHS_VBUS_SESSION_END:      vbusLevelName = "SessionEnd"; break;
        case USBHS_VBUS_BELOW_AVALID:     vbusLevelName = "BelowAValid"; break;
        case USBHS_VBUS_BELOW_VBUSVALID:  vbusLevelName = "BelowVBUSValid"; break;
        case USBHS_VBUS_VALID:            vbusLevelName = "Valid"; break;
        default:                          vbusLevelName = "Unknown"; break;
    }
    scpi_printf(context, "  VBUS: %s | Level: %s (0x%02X) | USB configured: %s\r\n",
        UsbCdc_IsVbusDetected() ? "Yes" : "No",
        vbusLevelName,
        (unsigned)vbusLevel,
        UsbCdc_IsConfigured() ? "Yes" : "No");

    return SCPI_RES_OK;
}

static scpi_result_t SCPI_SetTestPattern(scpi_t * context) {
    int32_t pattern;
    if (!SCPI_ParamInt32(context, &pattern, TRUE)) {
        return SCPI_RES_ERR;
    }
    if (pattern < 0 || pattern > 6) {
        SCPI_ErrorPush(context, SCPI_ERROR_ILLEGAL_PARAMETER_VALUE);
        return SCPI_RES_ERR;
    }
    Streaming_SetTestPattern((uint32_t)pattern);
    return SCPI_RES_OK;
}

static scpi_result_t SCPI_GetTestPattern(scpi_t * context) {
    SCPI_ResultInt32(context, (int32_t)Streaming_GetTestPattern());
    return SCPI_RES_OK;
}

static scpi_result_t SCPI_SetBenchmarkMode(scpi_t * context) {
    int32_t val;
    if (!SCPI_ParamInt32(context, &val, TRUE)) return SCPI_RES_ERR;
    if (val < 0 || val > BENCHMARK_PIPELINE) {
        SCPI_ErrorPush(context, SCPI_ERROR_DATA_OUT_OF_RANGE);
        return SCPI_RES_ERR;
    }
    // Block changes while streaming
    StreamingRuntimeConfig* pStreamCfg = BoardRunTimeConfig_Get(
            BOARDRUNTIME_STREAMING_CONFIGURATION);
    if (pStreamCfg->IsEnabled && pStreamCfg->Running) {
        SCPI_ExecutionError(context, "SYST:STR:BENCH: cannot change while streaming");
        return SCPI_RES_ERR;
    }
    Streaming_SetBenchmarkMode((uint32_t)val);
    return SCPI_RES_OK;
}

static scpi_result_t SCPI_GetBenchmarkMode(scpi_t * context) {
    SCPI_ResultInt32(context, (int32_t)Streaming_GetBenchmarkMode());
    return SCPI_RES_OK;
}

/**
 * Self-contained throughput benchmark.
 * Syntax: SYST:STR:THRoughput <freq>,<duration_sec>
 *
 * Uses current interface (SYST:STR:INT), format (SYST:STR:FOR), and
 * enabled channels. Enables benchmark mode (uncapped freq) and test
 * pattern 2 (midscale) automatically. Streams for <duration_sec>,
 * stops, and returns all stats in the response.
 *
 * Example: SYST:STR:THRoughput 5000,10
 *   → streams at 5kHz for 10s, returns results
 */
// Shared streaming-buffer setup (partition + DMA pool + sample pool).  Defined
// near SCPI_MemAutoBalance; forward-declared here for the throughput bench, the
// WiFi finder, and SCPI_StartStreaming, which all route through it.
static bool PrepareStreamingBuffers(uint32_t poolCount, size_t sampleElemSize);

// #520/Qodo: PrepareStreamingBuffers() quiesces SD by forcing WRITE->NONE so
// f_write can't be mid-DMA during the coherent-pool reset (see
// SCPI_QuiesceAndResetCoherentPool).  SCPI_StartStreaming re-enables SD after
// (it may be streaming TO the card), but the WiFi finder and THR benchmark do
// NOT target SD — so they must save the user's prior SD mode before the prepare
// and restore it after, or running either command would silently disable SD
// logging.  SaveSdMode() returns -1 when SD settings are unavailable (no-op
// restore).
static int SaveSdMode(void) {
    sd_card_manager_settings_t* pSd =
            BoardRunTimeConfig_Get(BOARDRUNTIME_SD_CARD_SETTINGS);
    return (pSd != NULL) ? (int)pSd->mode : -1;
}
static void RestoreSdMode(int savedMode) {
    if (savedMode < 0) return;
    sd_card_manager_settings_t* pSd =
            BoardRunTimeConfig_Get(BOARDRUNTIME_SD_CARD_SETTINGS);
    if (pSd != NULL && pSd->mode != (sd_card_manager_mode_t)savedMode) {
        pSd->mode = (sd_card_manager_mode_t)savedMode;
        sd_card_manager_UpdateSettings(pSd);
    }
}

static scpi_result_t SCPI_RunThroughputBench(scpi_t * context) {
    int32_t freq, duration;
    if (!SCPI_ParamInt32(context, &freq, TRUE)) return SCPI_RES_ERR;
    if (!SCPI_ParamInt32(context, &duration, TRUE)) return SCPI_RES_ERR;

    if (freq < 1 || freq > 100000 || duration < 1 || duration > 60) {
        SCPI_ErrorPush(context, SCPI_ERROR_DATA_OUT_OF_RANGE);
        return SCPI_RES_ERR;
    }

    StreamingRuntimeConfig* pStreamCfg = BoardRunTimeConfig_Get(
            BOARDRUNTIME_STREAMING_CONFIGURATION);
    if (pStreamCfg->Running) {
        SCPI_ExecutionError(context, "SYST:STR:THR: streaming already active");
        return SCPI_RES_ERR;
    }

    // Check power state
    const tPowerData *pPowerState = BoardData_Get(BOARDDATA_POWER_DATA, 0);
    if (pPowerState == NULL ||
        (pPowerState->powerState != POWERED_UP && pPowerState->powerState != POWERED_UP_EXT_DOWN)) {
        LOG_E("Throughput benchmark rejected: device must be powered up");
        SCPI_ErrorPush(context, SCPI_ERROR_EXECUTION_ERROR);
        return SCPI_RES_ERR;
    }

    // Save previous state to restore after benchmark
    uint32_t savedBenchmark = Streaming_GetBenchmarkMode();
    uint32_t savedPattern = Streaming_GetTestPattern();

    // Enable benchmark mode + test pattern
    Streaming_SetBenchmarkMode(BENCHMARK_NOCAP);
    Streaming_SetTestPattern(2);  // midscale

    // Clear stats
    Streaming_ClearStats();
    AInSampleList_PoolResetMaxUsed();

    // Save SD mode: PrepareStreamingBuffers quiesces SD (WRITE->NONE) and THR
    // doesn't stream to SD, so restore it on every exit below (Qodo #521).
    int savedSdMode = SaveSdMode();

    // Establish the buffer partition + sample pool that the cfg-poke start
    // below does NOT do on its own (this was a latent bug — THR relied on a
    // prior STR:START having partitioned; standalone it streamed 0 bytes, the
    // same #520 failure mode).  Shared helper, same path as StartStreaming.
    {
        volatile AInRuntimeArray* pRtAin =
            BoardRunTimeConfig_Get(BOARDRUNTIMECONFIG_AIN_CHANNELS);
        const tBoardConfig* pBcfg = BoardConfig_Get(BOARDCONFIG_ALL_CONFIG, 0);
        Streaming_BuildChannelMapping(pBcfg, (const AInRuntimeArray*)pRtAin);
        const AInChannelMapping* chMap = Streaming_GetChannelMapping();
        uint8_t ec = (chMap != NULL && chMap->count > 0) ? chMap->count : 1;
        MemoryConfig* mcfg = BoardRunTimeConfig_Get(BOARDRUNTIME_MEMORY_CONFIG);
        if (!PrepareStreamingBuffers(mcfg->samplePoolCount,
                                     AInSampleList_ElementSize(ec))) {
            Streaming_SetBenchmarkMode(savedBenchmark);
            Streaming_SetTestPattern(savedPattern);
            RestoreSdMode(savedSdMode);
            SCPI_ExecutionError(context, "SYST:STR:THR: buffer prepare failed");
            return SCPI_RES_ERR;
        }
    }

    // Set frequency and start
    const tBoardConfig* pBoardConfig = BoardConfig_Get(BOARDCONFIG_ALL_CONFIG, 0);
    uint32_t clkFreq = TimerApi_FrequencyGet(pBoardConfig->StreamingConfig.TimerIndex);
    // PIC32MZ type-B timer counts 0..PR inclusive (PR+1 cycles per match).
    uint32_t periodCycles = (clkFreq + freq - 1) / freq;
    if (periodCycles < 2) periodCycles = 2;
    pStreamCfg->ClockPeriod = periodCycles - 1;
    pStreamCfg->Frequency = freq;
    pStreamCfg->IsEnabled = true;
    Streaming_UpdateState();

    // Verify streaming actually started
    if (!pStreamCfg->Running) {
        LOG_E("Throughput benchmark: streaming failed to start");
        pStreamCfg->IsEnabled = false;
        Streaming_SetBenchmarkMode(savedBenchmark);
        Streaming_SetTestPattern(savedPattern);
        RestoreSdMode(savedSdMode);
        SCPI_ErrorPush(context, SCPI_ERROR_EXECUTION_ERROR);
        return SCPI_RES_ERR;
    }

    // Wait for duration
    vTaskDelay(pdMS_TO_TICKS(duration * 1000));

    // Stop
    pStreamCfg->IsEnabled = false;
    Streaming_UpdateState();

    // Restore previous state
    Streaming_SetBenchmarkMode(savedBenchmark);
    Streaming_SetTestPattern(savedPattern);
    RestoreSdMode(savedSdMode);

    // Collect and report results
    StreamingStats s;
    Streaming_GetStats(&s);

    uint32_t durationMs = duration * 1000;
    uint32_t samplesPerSec = (durationMs > 0) ?
        (uint32_t)((s.totalSamplesStreamed * 1000ULL) / durationMs) : 0;
    uint32_t bytesPerSec = (durationMs > 0) ?
        (uint32_t)((s.totalBytesStreamed * 1000ULL) / durationMs) : 0;

    scpi_printf(context, "Freq=%d\r\n", (int)freq);
    scpi_printf(context, "Duration=%d\r\n", (int)duration);
    scpi_printf(context, "TotalSamples=%llu\r\n", (unsigned long long)s.totalSamplesStreamed);
    scpi_printf(context, "TotalBytes=%llu\r\n", (unsigned long long)s.totalBytesStreamed);
    scpi_printf(context, "SamplesPerSec=%u\r\n", (unsigned)samplesPerSec);
    scpi_printf(context, "BytesPerSec=%u\r\n", (unsigned)bytesPerSec);
    scpi_printf(context, "QueueDropped=%u\r\n", (unsigned)s.queueDroppedSamples);
    // #499: split sub-counters — QueueDropped is their sum (kept for back-compat).
    //   PoolExhausted = sample pool depth too shallow for the rate.
    //   QueueOverflow = streaming_Task drain too slow (queue full while pool has slots).
    scpi_printf(context, "PoolExhausted=%u\r\n", (unsigned)s.poolExhaustedSamples);
    scpi_printf(context, "QueueOverflow=%u\r\n", (unsigned)s.queueOverflowSamples);
    scpi_printf(context, "UsbDropped=%u\r\n", (unsigned)s.usbDroppedBytes);
    scpi_printf(context, "WifiDropped=%u\r\n", (unsigned)s.wifiDroppedBytes);
    scpi_printf(context, "SdDropped=%u\r\n", (unsigned)s.sdDroppedBytes);
    scpi_printf(context, "EncoderFail=%u\r\n", (unsigned)s.encoderFailures);

    uint64_t totalAttempted = s.totalSamplesStreamed + s.queueDroppedSamples;
    uint32_t lossPct = totalAttempted > 0
        ? (uint32_t)((s.queueDroppedSamples * 100ULL) / totalAttempted) : 0;
    scpi_printf(context, "LossPercent=%u\r\n", (unsigned)lossPct);
    scpi_printf(context, "PoolMaxUsed=%u\r\n", (unsigned)AInSampleList_PoolMaxUsed());

    return SCPI_RES_OK;
}

// =====================================================================
// #520: device-side WiFi throughput finder (fast AIMD link-rate probe)
// =====================================================================
//
// SYSTem:STReam:WIFI:FINd? [startHz][,maxHz]
//   -> <recommendedHz>,<measuredKBps>,<reason>
//
// Climbs the streaming rate from a conservative start and locks the ceiling
// only when the buffer that actually fills is essentially FULL (high-water mark
// >= FIND_BUF_FULL_PCT of capacity), giving the buffer full room to absorb WiFi
// jitter before calling a ceiling.
//
// WHICH buffer fills (EMPIRICAL, #520 HW 2026-05-31): originally we hypothesized
// the SAMPLE POOL (last buffer to fill if backpressure propagates upstream).
// Hardware first refuted that — the pool sat at ~1% while the WiFi ring pegged
// full and dropped 1.17 MB — because the encoder used an all-or-nothing
// no-retry write that DROPPED at the ring instead of back-pressuring upstream,
// wasting the pool's absorption capacity.  That was fixed (#520 backpressure):
// solo WiFi/USB now route through Streaming_WriteWithRetry, so the encoder HOLDS
// samples while the ring is full and the pool fills as designed — the SINGLE
// drop point is now pool exhaustion (verified: at over-rate SamplePoolMaxUsed
// hit capacity, drops = PoolExhausted, WifiDropped ~0).  So the pool hypothesis
// is now CORRECT and we gauge the sample-pool high-water mark.  Note the WiFi
// ring level is NOT a ceiling signal post-fix: it sits near-full at the link
// rate (the encoder keeps it full, the WINC drains it) — that's normal, not
// saturation.  A jitter swell the ring + lower pool absorb never drives the
// pool high-water near full.  Actual loss (QueueDropped / WiFi / window) is a
// hard secondary trip.
//
// Reported rate = highest rate whose pool stayed below full, minus a margin
// (FIND_BACKOFF).  Structure mirrors SCPI_RunThroughputBench.
#define FIND_DEFAULT_START_HZ   1000u   // conservative baseline
#define FIND_DEFAULT_MAX_HZ     20000u  // backstop; further clamped to the ADC cap
#define FIND_SETTLE_MS          500u    // prime + let the pipeline reach steady state
#define FIND_DWELL_MS           3000u   // observation window — long enough for the
                                        // buffer to actually fill at a near-ceiling
                                        // rate (slow fill needs room to reveal itself)
#define FIND_OCC_SAMPLES        6u      // ring-occupancy samples across the dwell
                                        // (high-water = max over these)
#define FIND_BUF_FULL_PCT       95u     // buffer high-water >= this % of capacity
                                        // => buffer full => ceiling (user 2026-05-31)
#define FIND_INTERSTEP_MS       300u    // settle between rate changes (WINC is touchy)
#define FIND_BACKOFF_NUM        9u      // recommended = lastGood * 9/10 (-10% margin)
#define FIND_BACKOFF_DEN        10u
#define FIND_RESOLUTION_HZ      500u    // binary-refine stops when bracket <= this

// One measurement cycle for SCPI_WifiFindRate: start streaming at `freq`, dwell,
// observe the sample-pool high-water mark (the last-to-fill buffer — see header),
// tear down cleanly, and return whether the buffers SATURATED at this rate.
// Outputs the measured wire KB/s.  *outStartFailed is set if the stream never
// went Running (caller aborts).  Factored (#520, 2026-05-31) so the coarse AIMD
// climb and the binary-search refinement share one code path.  wRingCap is used
// only for the diagnostic log line.
static bool FindMeasureStep(StreamingRuntimeConfig* cfg, uint32_t clkFreq,
                            uint32_t wRingCap, uint32_t freq,
                            uint32_t* outKBps, bool* outStartFailed) {
    uint32_t periodCycles = (clkFreq + freq - 1) / freq;
    if (periodCycles < 2) periodCycles = 2;

    Streaming_ClearStats();
    cfg->ClockPeriod = periodCycles - 1;
    cfg->Frequency = freq;
    cfg->IsEnabled = true;
    Streaming_UpdateState();
    if (!cfg->Running) { *outStartFailed = true; *outKBps = 0; return true; }

    // Prime to steady state, THEN zero the pool high-water mark so the dwell's
    // peak reflects this rate (not the priming transient).  Observe across the
    // dwell; the pool's running-max captures the peak whenever it occurs.
    vTaskDelay(pdMS_TO_TICKS(FIND_SETTLE_MS));
    AInSampleList_PoolResetMaxUsed();
    uint32_t occMax = 0;   // WiFi-ring high-water mark across the dwell
    for (uint32_t i = 0; i < FIND_OCC_SAMPLES; i++) {
        vTaskDelay(pdMS_TO_TICKS(FIND_DWELL_MS / FIND_OCC_SAMPLES));
        uint32_t occ = wifi_tcp_server_GetCircularBufferAvailable();
        if (occ > occMax) occMax = occ;
    }
    uint32_t poolPeak = AInSampleList_PoolMaxUsed();
    uint32_t poolCap = (uint32_t)AInSampleList_PoolCapacity();

    StreamingStats s;
    Streaming_GetStats(&s);

    // Clean teardown BEFORE evaluating / changing rate (WINC dislikes mid-
    // stream transitions — #425/#467/#517).
    cfg->IsEnabled = false;
    Streaming_UpdateState();

    // Primary trip: the SAMPLE POOL high-water reached ~full (>= FIND_BUF_FULL_PCT
    // of capacity).  After the #520 backpressure fix (streaming.c, solo WiFi/USB
    // now route through Streaming_WriteWithRetry) the encoder HOLDS samples while
    // the WiFi ring is full, so the ring sits near-full at the link rate as
    // normal operation — NOT a ceiling — and the POOL is the buffer that backs
    // up only when inflow exceeds drain.  So the pool high-water is the true
    // "can't keep up" signal; ring level is no longer a ceiling indicator (it's
    // full whenever the encoder is backpressuring).  A jitter swell the ring +
    // lower pool absorb never drives the pool high-water near full.
    bool poolFull = (poolCap > 0) &&
                    ((uint64_t)poolPeak * 100ULL >= (uint64_t)poolCap * FIND_BUF_FULL_PCT);
    // Hard secondary: actual loss — the pool overflowed (PoolExhausted /
    // QueueOverflow = QueueDropped, the single drop point post-fix) or a
    // transport dropped.  Unambiguously over the ceiling.
    bool lossy = (s.queueDroppedSamples > 0) ||
                 (s.wifiDroppedBytesSteady > 0) || (s.windowLossPercent > 0);
    bool tripped = poolFull || lossy;

    uint32_t dwellMs = FIND_SETTLE_MS + FIND_DWELL_MS;
    *outKBps = (dwellMs > 0)
        ? (uint32_t)((s.totalBytesStreamed * 1000ULL) / dwellMs / 1024ULL) : 0;

    // #520 tuning instrumentation (SCPI INFO), retrievable via SYST:LOG?.
    // ringHW=peak/cap is the trip gauge; pool is logged to confirm it stays low.
    LOG_I("WIFI:FIND %u Hz: samp=%u bytes=%u ringHW=%u/%u pool=%u/%u qd=%u wst=%u wlp=%u -> %s",
          (unsigned)freq, (unsigned)s.totalSamplesStreamed,
          (unsigned)s.totalBytesStreamed,
          (unsigned)occMax, (unsigned)wRingCap,
          (unsigned)poolPeak, (unsigned)poolCap,
          (unsigned)s.queueDroppedSamples,
          (unsigned)s.wifiDroppedBytesSteady, (unsigned)s.windowLossPercent,
          (!tripped ? "OK" : (lossy ? "LOSSY" : "BUF_FULL")));

    return tripped;
}

static scpi_result_t SCPI_WifiFindRate(scpi_t * context) {
    // Optional params: startHz, maxHz (0/absent => defaults).
    int32_t startArg = 0, maxArg = 0;
    SCPI_ParamInt32(context, &startArg, FALSE);
    SCPI_ParamInt32(context, &maxArg, FALSE);

    StreamingRuntimeConfig* cfg = BoardRunTimeConfig_Get(
            BOARDRUNTIME_STREAMING_CONFIGURATION);
    if (cfg->Running) {
        SCPI_ExecutionError(context, "SYST:STR:WIFI:FIND: streaming already active");
        return SCPI_RES_ERR;
    }

    const tPowerData* pPower = BoardData_Get(BOARDDATA_POWER_DATA, 0);
    if (pPower == NULL ||
        (pPower->powerState != POWERED_UP && pPower->powerState != POWERED_UP_EXT_DOWN)) {
        SCPI_ExecutionError(context, "SYST:STR:WIFI:FIND: device must be powered up");
        return SCPI_RES_ERR;
    }

    if (cfg->ActiveInterface != StreamingInterface_WiFi) {
        SCPI_ExecutionError(context,
            "SYST:STR:WIFI:FIND: requires ActiveInterface=WiFi (SYST:STR:INT 1)");
        return SCPI_RES_ERR;
    }

    uint16_t type1Count = 0, totalEnabled = 0;
    bool hasAd7609 = false;
    Streaming_CountActiveChannels(&type1Count, &totalEnabled, &hasAd7609);
    if (totalEnabled == 0) {
        SCPI_ExecutionError(context, "SYST:STR:WIFI:FIND: no ADC channels enabled");
        return SCPI_RES_ERR;
    }

    // Cap the climb at the ADC-safe rate: the recommendation must be usable in
    // normal (non-benchmark) mode, where Streaming_ComputeMaxFreq applies.
    uint32_t adcCap = Streaming_ComputeMaxFreq(type1Count, totalEnabled);
    uint32_t hardMax = (maxArg > 0) ? (uint32_t)maxArg : FIND_DEFAULT_MAX_HZ;
    if (hardMax > adcCap) hardMax = adcCap;

    uint32_t freq = (startArg > 0) ? (uint32_t)startArg : FIND_DEFAULT_START_HZ;
    if (freq < 1) freq = 1;
    if (freq > hardMax) freq = hardMax;

    // Save state to restore on exit (mirrors SCPI_RunThroughputBench).
    uint32_t savedBenchmark = Streaming_GetBenchmarkMode();
    uint32_t savedPattern = Streaming_GetTestPattern();
    Streaming_SetBenchmarkMode(BENCHMARK_NOCAP);   // bypass cap to probe the link
    Streaming_SetTestPattern(3);                   // fullscale: worst-case PB size

    const tBoardConfig* pBoardConfig = BoardConfig_Get(BOARDCONFIG_ALL_CONFIG, 0);
    uint32_t clkFreq = TimerApi_FrequencyGet(pBoardConfig->StreamingConfig.TimerIndex);

    // Save SD mode: PrepareStreamingBuffers quiesces SD (WRITE->NONE) and the
    // finder doesn't stream to SD, so restore it on every exit (Qodo #521).
    int savedSdMode = SaveSdMode();

    // #520: establish the WiFi buffer partition + sample-pool ourselves — the
    // cfg-poke start below (like SCPI_RunThroughputBench) does NOT, so without
    // this the pipeline has no buffers and the encoder produces 0 bytes.
    // Build the channel mapping first (sizes the sample-pool element), then
    // auto-balance-partition with the actual channel count.
    {
        volatile AInRuntimeArray* pRtAin =
            BoardRunTimeConfig_Get(BOARDRUNTIMECONFIG_AIN_CHANNELS);
        Streaming_BuildChannelMapping(pBoardConfig, (const AInRuntimeArray*)pRtAin);
        const AInChannelMapping* chMap = Streaming_GetChannelMapping();
        uint8_t ec = (chMap != NULL && chMap->count > 0) ? chMap->count : 1;
        MemoryConfig* mcfg = BoardRunTimeConfig_Get(BOARDRUNTIME_MEMORY_CONFIG);
        if (!PrepareStreamingBuffers(mcfg->samplePoolCount,
                                     AInSampleList_ElementSize(ec))) {
            Streaming_SetBenchmarkMode(savedBenchmark);
            Streaming_SetTestPattern(savedPattern);
            RestoreSdMode(savedSdMode);
            SCPI_ExecutionError(context, "SYST:STR:WIFI:FIND: buffer prepare failed");
            return SCPI_RES_ERR;
        }
    }

    uint32_t lastGoodHz = 0;
    uint32_t lastGoodKBps = 0;
    bool startFailed = false;
    bool saturated = false;
    bool confirmTrip = false;   // debounce: require 2 consecutive trips to lock

    // WiFi ring capacity — informational only (logged alongside the pool gauge).
    // The trip is on the sample-pool high-water mark (see FindMeasureStep header),
    // not the ring, so the ring size doesn't gate the result.
    uint8_t* wRingBuf = NULL; uint32_t wRingCap = 0;
    StreamingBufferPool_GetWifi(&wRingBuf, &wRingCap);

    // Coarse AIMD climb: geometric step up until a step trips twice in a row.
    while (freq <= hardMax) {
        uint32_t kbps = 0;
        bool sf = false;
        bool sat = FindMeasureStep(cfg, clkFreq, wRingCap, freq, &kbps, &sf);
        if (sf) { startFailed = true; break; }

        if (!sat) {
            confirmTrip = false;   // clean step clears any pending debounce
            lastGoodHz = freq;
            lastGoodKBps = kbps;
            // Additive-ish increase: bigger steps low, finer near the top so a
            // single overshoot can't slam the WINC.
            uint32_t stepUp = (freq < hardMax / 2) ? (freq / 4 + 500) : 500;
            freq += stepUp;
            vTaskDelay(pdMS_TO_TICKS(FIND_INTERSTEP_MS));
        } else if (!confirmTrip) {
            // Debounce: WiFi saturation is bimodal — a single transient ring
            // spike at a low step shouldn't collapse the result.  Re-test the
            // SAME freq once; only lock saturation if it trips again (#520 HW
            // tuning 2026-05-31: 1/5 runs collapsed to 900 Hz on a transient).
            confirmTrip = true;
            LOG_I("WIFI:FIND %u Hz: trip — re-testing (debounce)", (unsigned)freq);
            vTaskDelay(pdMS_TO_TICKS(FIND_INTERSTEP_MS));
            // freq unchanged → loop re-measures this step
        } else {
            saturated = true;   // tripped twice in a row → real saturation
            break;              // freq holds the confirmed-saturated rate
        }
    }

    // Binary-search refinement: the coarse climb's geometric step can skip a
    // wide band (HW 2026-05-31: 5×T1 jumped 3858 -> 5322, leaving the real
    // ~5 kHz ceiling unprobed and the recommendation needlessly conservative).
    // When the climb confirmed saturation, bisect the (lastGoodHz, freq)
    // bracket — known-clean vs known-saturated — down to FIND_RESOLUTION_HZ to
    // pin the real ceiling.  A transient false-trip here only narrows toward
    // lastGoodHz (conservative, safe), so no debounce is needed in this phase.
    if (saturated && lastGoodHz > 0 && freq > lastGoodHz + FIND_RESOLUTION_HZ) {
        uint32_t lo = lastGoodHz;   // highest confirmed-clean
        uint32_t hi = freq;         // confirmed-saturated
        while (hi - lo > FIND_RESOLUTION_HZ) {
            uint32_t mid = lo + (hi - lo) / 2;
            uint32_t kbps = 0;
            bool sf = false;
            bool sat = FindMeasureStep(cfg, clkFreq, wRingCap, mid, &kbps, &sf);
            if (sf) break;          // unexpected start failure — keep coarse lastGood
            if (sat) {
                hi = mid;
            } else {
                lo = mid;
                lastGoodHz = mid;
                lastGoodKBps = kbps;
            }
            vTaskDelay(pdMS_TO_TICKS(FIND_INTERSTEP_MS));
        }
    }

    // Restore prior streaming config + SD mode.
    Streaming_SetBenchmarkMode(savedBenchmark);
    Streaming_SetTestPattern(savedPattern);
    RestoreSdMode(savedSdMode);

    const char* reason;
    if (startFailed)            reason = "START_FAIL";
    else if (lastGoodHz == 0)   reason = "NO_LINK";       // nothing sustainable, even the start freq
    else if (saturated)         reason = "LINK_SATURATED";
    else if (lastGoodHz >= adcCap) reason = "HIT_ADC_CAP";
    else                        reason = "HIT_MAX";

    uint32_t recommendedHz = (lastGoodHz > 0)
        ? (uint32_t)((uint64_t)lastGoodHz * FIND_BACKOFF_NUM / FIND_BACKOFF_DEN) : 0;

    scpi_printf(context, "%u,%u,%s\r\n",
                (unsigned)recommendedHz, (unsigned)lastGoodKBps, reason);
    return SCPI_RES_OK;
}

// =====================================================================
// #377 iperf2 SCPI handlers
// =====================================================================

static bool Iperf2_RefuseIfStreaming(scpi_t * context) {
    StreamingRuntimeConfig* pStreamCfg = BoardRunTimeConfig_Get(
            BOARDRUNTIME_STREAMING_CONFIGURATION);
    if (pStreamCfg->IsEnabled && pStreamCfg->Running) {
        SCPI_ExecutionError(context, "SYST:WIFI:IPERF: stop streaming first");
        return true;
    }
    return false;
}

// Optional [port=5001] arg
static scpi_result_t SCPI_Iperf2_TcpServer(scpi_t * context) {
    int32_t port = IPERF2_DEFAULT_PORT;
    if (!SCPI_ParamInt32(context, &port, FALSE)) port = IPERF2_DEFAULT_PORT;
    if (port <= 0 || port > 65535) {
        SCPI_ErrorPush(context, SCPI_ERROR_DATA_OUT_OF_RANGE);
        return SCPI_RES_ERR;
    }
    if (Iperf2_RefuseIfStreaming(context)) return SCPI_RES_ERR;
    if (!Iperf2_StartTcpServer((uint16_t)port)) {
        SCPI_ErrorPush(context, SCPI_ERROR_EXECUTION_ERROR);
        return SCPI_RES_ERR;
    }
    return SCPI_RES_OK;
}

static scpi_result_t SCPI_Iperf2_UdpServer(scpi_t * context) {
    int32_t port = IPERF2_DEFAULT_PORT;
    if (!SCPI_ParamInt32(context, &port, FALSE)) port = IPERF2_DEFAULT_PORT;
    if (port <= 0 || port > 65535) {
        SCPI_ErrorPush(context, SCPI_ERROR_DATA_OUT_OF_RANGE);
        return SCPI_RES_ERR;
    }
    if (Iperf2_RefuseIfStreaming(context)) return SCPI_RES_ERR;
    if (!Iperf2_StartUdpServer((uint16_t)port)) {
        SCPI_ErrorPush(context, SCPI_ERROR_EXECUTION_ERROR);
        return SCPI_RES_ERR;
    }
    return SCPI_RES_OK;
}

// Common <ip>,[port=5001],[dur_s=10] parser
static bool Iperf2_ParseClientArgs(scpi_t * context, char* ipBuf,
                                   size_t ipBufLen, int32_t* port,
                                   int32_t* duration_sec) {
    size_t ipLen = 0;
    *port = IPERF2_DEFAULT_PORT;
    *duration_sec = 10;
    if (!SCPI_ParamCopyText(context, ipBuf, ipBufLen, &ipLen, TRUE)) {
        SCPI_ErrorPush(context, SCPI_ERROR_MISSING_PARAMETER);
        return false;
    }
    if (!SCPI_ParamInt32(context, port, FALSE)) *port = IPERF2_DEFAULT_PORT;
    if (!SCPI_ParamInt32(context, duration_sec, FALSE)) *duration_sec = 10;
    if (*port <= 0 || *port > 65535 || *duration_sec < 1 ||
        *duration_sec > 3600) {
        SCPI_ErrorPush(context, SCPI_ERROR_DATA_OUT_OF_RANGE);
        return false;
    }
    return true;
}

static scpi_result_t SCPI_Iperf2_TcpClient(scpi_t * context) {
    char ipBuf[32];
    int32_t port, duration_sec;
    if (!Iperf2_ParseClientArgs(context, ipBuf, sizeof(ipBuf), &port,
                                 &duration_sec)) return SCPI_RES_ERR;
    if (Iperf2_RefuseIfStreaming(context)) return SCPI_RES_ERR;
    if (!Iperf2_StartTcpClient(ipBuf, (uint16_t)port,
                               (uint32_t)duration_sec)) {
        SCPI_ErrorPush(context, SCPI_ERROR_EXECUTION_ERROR);
        return SCPI_RES_ERR;
    }
    return SCPI_RES_OK;
}

static scpi_result_t SCPI_Iperf2_UdpClient(scpi_t * context) {
    char ipBuf[32];
    int32_t port, duration_sec;
    if (!Iperf2_ParseClientArgs(context, ipBuf, sizeof(ipBuf), &port,
                                 &duration_sec)) return SCPI_RES_ERR;
    if (Iperf2_RefuseIfStreaming(context)) return SCPI_RES_ERR;
    if (!Iperf2_StartUdpClient(ipBuf, (uint16_t)port,
                               (uint32_t)duration_sec)) {
        SCPI_ErrorPush(context, SCPI_ERROR_EXECUTION_ERROR);
        return SCPI_RES_ERR;
    }
    return SCPI_RES_OK;
}

// SYST:WIFI:IPERF:TXBLast <port>,<duration_s>
// Listen+accept TX-blast for #399 — repeated-session-safe TX throughput.
// Device listens on port; on accept, sends 1400-byte chunks for duration.
// PC connects with any TCP client (iperf -c, nc, etc.) and measures bytes
// received as device's TX throughput.
static scpi_result_t SCPI_Iperf2_TxBlast(scpi_t * context) {
    int32_t port, duration_sec;
    if (!SCPI_ParamInt32(context, &port, TRUE)) return SCPI_RES_ERR;
    if (!SCPI_ParamInt32(context, &duration_sec, TRUE)) return SCPI_RES_ERR;
    if (port <= 0 || port > 65535 || duration_sec <= 0) {
        SCPI_ErrorPush(context, SCPI_ERROR_DATA_OUT_OF_RANGE);
        return SCPI_RES_ERR;
    }
    if (Iperf2_RefuseIfStreaming(context)) return SCPI_RES_ERR;
    if (!Iperf2_StartTxBlast((uint16_t)port, (uint32_t)duration_sec)) {
        SCPI_ErrorPush(context, SCPI_ERROR_EXECUTION_ERROR);
        return SCPI_RES_ERR;
    }
    return SCPI_RES_OK;
}

static scpi_result_t SCPI_Iperf2_Stop(scpi_t * context) {
    Iperf2_Stop();
    return SCPI_RES_OK;
}

static scpi_result_t SCPI_Iperf2_Stats(scpi_t * context) {
    Iperf2_Stats s;
    Iperf2_GetStats(&s);
    scpi_printf(context, "Mode=%d\r\n", (int)s.mode);
    scpi_printf(context, "Active=%d\r\n", (int)(s.active ? 1 : 0));
    scpi_printf(context, "Completed=%d\r\n", (int)(s.completed ? 1 : 0));
    scpi_printf(context, "Bytes=%llu\r\n", (unsigned long long)s.bytes_transferred);
    scpi_printf(context, "DurationMs=%u\r\n", (unsigned)s.duration_ms);
    scpi_printf(context, "KBps=%u\r\n", (unsigned)s.kbps);
    scpi_printf(context, "UdpTotalPkt=%u\r\n", (unsigned)s.udp_total_pkt);
    scpi_printf(context, "UdpLostPkt=%u\r\n", (unsigned)s.udp_lost_pkt);
    scpi_printf(context, "UdpOutOfOrder=%u\r\n", (unsigned)s.udp_outoforder);
    return SCPI_RES_OK;
}

// #399 workaround — limit MAX_PENDING_TX (= WINC HIF queue depth in flight).
// 0 restores compile-time default (4), 1-4 throttle TX rate.
static scpi_result_t SCPI_Iperf2_SetMaxPending(scpi_t * context) {
    int32_t n;
    if (!SCPI_ParamInt32(context, &n, TRUE)) return SCPI_RES_ERR;
    if (n < 0 || n > 4) {
        SCPI_ErrorPush(context, SCPI_ERROR_ILLEGAL_PARAMETER_VALUE);
        return SCPI_RES_ERR;
    }
    Iperf2_SetMaxPending((uint8_t)n);
    return SCPI_RES_OK;
}

static scpi_result_t SCPI_Iperf2_GetMaxPending(scpi_t * context) {
    SCPI_ResultInt32(context, (int32_t)Iperf2_GetMaxPending());
    return SCPI_RES_OK;
}

// #399 workaround toggle — auto-HRESet WiFi after every iperf2 session.
static scpi_result_t SCPI_Iperf2_SetAutoReset(scpi_t * context) {
    int32_t v;
    if (!SCPI_ParamInt32(context, &v, TRUE)) return SCPI_RES_ERR;
    Iperf2_SetAutoReset(v != 0);
    return SCPI_RES_OK;
}

static scpi_result_t SCPI_Iperf2_GetAutoReset(scpi_t * context) {
    SCPI_ResultInt32(context, Iperf2_GetAutoReset() ? 1 : 0);
    return SCPI_RES_OK;
}

// #399 diagnostic — visibility into WINC HIF state across iperf2 sessions.
// Shows current/last gCtx state plus a free-socket probe (only when IDLE).
static scpi_result_t SCPI_Iperf2_Diag(scpi_t * context) {
    Iperf2_Diag d;
    Iperf2_GetDiag(&d);
    scpi_printf(context, "Mode=%d\r\n", (int)d.mode);
    scpi_printf(context, "DataSock=%d\r\n", (int)d.data_sock);
    scpi_printf(context, "ListenSock=%d\r\n", (int)d.listen_sock);
    scpi_printf(context, "PendingTx=%u\r\n", (unsigned)d.pending_tx);
    scpi_printf(context, "AbortPending=%d\r\n", (int)(d.abort_pending ? 1 : 0));
    scpi_printf(context, "BytesConfirmed=%llu\r\n",
                (unsigned long long)d.bytes_confirmed);
    scpi_printf(context, "LastSendRc=%d\r\n", (int)d.last_send_rc);
    scpi_printf(context, "SendErrCount=%u\r\n", (unsigned)d.send_err_count);
    scpi_printf(context, "WincState=%u\r\n", (unsigned)d.winc_state);
    if (d.free_tcp_sockets == 0xFF) {
        scpi_printf(context, "FreeTcpSockets=skipped\r\n");
        scpi_printf(context, "FreeUdpSockets=skipped\r\n");
    } else {
        scpi_printf(context, "FreeTcpSockets=%u\r\n",
                    (unsigned)d.free_tcp_sockets);
        scpi_printf(context, "FreeUdpSockets=%u\r\n",
                    (unsigned)d.free_udp_sockets);
    }
    return SCPI_RES_OK;
}

static scpi_result_t SCPI_ClearStreamStats(scpi_t * context) {
    Streaming_ClearStats();
    // Reset WiFi TCP send tracking counters atomically
    wifi_tcp_server_context_t* pTcp = wifi_manager_GetTcpServerContext();
    if (pTcp != NULL) {
        taskENTER_CRITICAL();
        pTcp->client.wifiTcpBytesSent = 0;
        pTcp->client.wifiTcpBytesConfirmed = 0;
        pTcp->client.wifiTcpSendErrors = 0;
        pTcp->client.wifiTcpPartialSends = 0;
        pTcp->client.wifiPartialBytesMissing = 0;
        pTcp->client.wifiWriteBufferRejectedCalls = 0;
        pTcp->client.wifiWriteBufferRejectedBytes = 0;
        for (uint8_t i = 0; i < WIFI_TCP_MAX_IN_FLIGHT; i++) {
            pTcp->client.inflightSizes[i] = 0;
        }
        pTcp->client.inflightHead = 0;
        pTcp->client.inflightTail = 0;
        taskEXIT_CRITICAL();
    }
    // Reset SD write metrics
    sd_card_manager_ResetWriteMetrics();
    AInSampleList_PoolResetMaxUsed();
    SCPI_SyncQuesBits();
    return SCPI_RES_OK;
}

static scpi_result_t SCPI_SetLossThreshold(scpi_t * context) {
    int32_t pct;
    if (!SCPI_ParamInt32(context, &pct, TRUE)) {
        return SCPI_RES_ERR;
    }
    if (pct < 1 || pct > 100) {
        SCPI_ErrorPush(context, SCPI_ERROR_ILLEGAL_PARAMETER_VALUE);
        return SCPI_RES_ERR;
    }
    Streaming_SetLossThreshold((uint32_t)pct);
    return SCPI_RES_OK;
}

static scpi_result_t SCPI_GetLossThreshold(scpi_t * context) {
    SCPI_ResultInt32(context, (int32_t)Streaming_GetLossThreshold());
    return SCPI_RES_OK;
}

static scpi_result_t SCPI_SetFlowWindow(scpi_t * context) {
    int32_t size;
    if (!SCPI_ParamInt32(context, &size, TRUE)) {
        return SCPI_RES_ERR;
    }
    if (size < 0 || (size > 0 && size < 20) || size > 10000) {
        SCPI_ErrorPush(context, SCPI_ERROR_ILLEGAL_PARAMETER_VALUE);
        return SCPI_RES_ERR;
    }
    Streaming_SetFlowWindowOverride((uint32_t)size);
    return SCPI_RES_OK;
}

static scpi_result_t SCPI_GetFlowWindow(scpi_t * context) {
    SCPI_ResultInt32(context, (int32_t)Streaming_GetFlowWindowOverride());
    return SCPI_RES_OK;
}

/**
 * #397 SYST:STReam:CONSumer:GRACe <sec> — grace window in seconds for the
 * self-heal transport check.  Streaming auto-stops only when every
 * configured transport (USB/WiFi/SD per ActiveInterface) has been
 * unhealthy for longer than this window.  Range 5..300, default 60.
 * Runtime-only.
 */
static scpi_result_t SCPI_SetTransportGrace(scpi_t * context) {
    int32_t sec;
    if (!SCPI_ParamInt32(context, &sec, TRUE)) {
        return SCPI_RES_ERR;
    }
    if (sec < 5 || sec > 300) {
        SCPI_ErrorPush(context, SCPI_ERROR_ILLEGAL_PARAMETER_VALUE);
        return SCPI_RES_ERR;
    }
    Streaming_SetTransportGraceSec((uint32_t)sec);
    return SCPI_RES_OK;
}

static scpi_result_t SCPI_GetTransportGrace(scpi_t * context) {
    SCPI_ResultInt32(context, (int32_t)Streaming_GetTransportGraceSec());
    return SCPI_RES_OK;
}

/**
 * SYSTem:STReam:LOSS:GRACe <sec> — #450
 * Sets the startup-drop grace window.  Drops within `sec` seconds of
 * Streaming_Start are counted only in the existing *DroppedBytes totals;
 * drops after also accumulate in the new *DroppedBytesSteady counters.
 * Range 0..60, default 3.  Runtime-only.
 *
 * Takes effect IMMEDIATELY: the grace check reads gLossGraceSec at every
 * drop site, so mid-session changes shift the classification of
 * subsequent drops.  This matches the LOSS:THREshold pattern, where
 * tuning the threshold during a live measurement is the intended use
 * case.  If a snapshot-at-Start contract is wanted, callers should set
 * GRACe before calling SYST:STR:START.
 */
static scpi_result_t SCPI_SetLossGrace(scpi_t * context) {
    int32_t sec;
    if (!SCPI_ParamInt32(context, &sec, TRUE)) {
        return SCPI_RES_ERR;
    }
    if (sec < 0 || sec > 60) {
        SCPI_ErrorPush(context, SCPI_ERROR_ILLEGAL_PARAMETER_VALUE);
        return SCPI_RES_ERR;
    }
    if (!Streaming_SetLossGraceSec((uint32_t)sec)) {
        SCPI_ErrorPush(context, SCPI_ERROR_EXECUTION_ERROR);
        return SCPI_RES_ERR;
    }
    return SCPI_RES_OK;
}

static scpi_result_t SCPI_GetLossGrace(scpi_t * context) {
    SCPI_ResultInt32(context, (int32_t)Streaming_GetLossGraceSec());
    return SCPI_RES_OK;
}

/**
 * STATus:QUEStionable:CONDition? wrapper that syncs streaming health
 * bits from the streaming engine before reading the register.
 */
static scpi_result_t SCPI_QuesConditionQ(scpi_t * context) {
    SCPI_SyncQuesBits();
    return SCPI_StatusQuestionableConditionQ(context);
}

/**
 * STATus:QUEStionable[:EVENt]? wrapper that syncs streaming health
 * bits before reading (and clearing) the event register.
 */
static scpi_result_t SCPI_QuesEventQ(scpi_t * context) {
    SCPI_SyncQuesBits();
    return SCPI_StatusQuestionableEventQ(context);
}

scpi_result_t SCPI_GetStreamStats(scpi_t * context) {
    StreamingStats s;
    Streaming_GetStats(&s);
    scpi_printf(context, "TotalSamplesStreamed=%llu\r\n", (unsigned long long)s.totalSamplesStreamed);
    scpi_printf(context, "TotalBytesStreamed=%llu\r\n", (unsigned long long)s.totalBytesStreamed);
    scpi_printf(context, "QueueDroppedSamples=%u\r\n", (unsigned)s.queueDroppedSamples);
    // #499: split sub-counters — QueueDroppedSamples is their sum (kept for back-compat).
    //   PoolExhaustedSamples = sample pool depth too shallow for the rate.
    //   QueueOverflowSamples = streaming_Task drain too slow (queue full while pool has slots).
    // #483: Steady = post-grace subset of QueueDroppedSamples (no per-sub-counter Steady).
    scpi_printf(context, "QueueDroppedSamplesSteady=%u\r\n", (unsigned)s.queueDroppedSamplesSteady);
    scpi_printf(context, "PoolExhaustedSamples=%u\r\n", (unsigned)s.poolExhaustedSamples);
    scpi_printf(context, "QueueOverflowSamples=%u\r\n", (unsigned)s.queueOverflowSamples);
    scpi_printf(context, "UsbDroppedBytes=%u\r\n", (unsigned)s.usbDroppedBytes);
    scpi_printf(context, "UsbDroppedBytesSteady=%u\r\n", (unsigned)s.usbDroppedBytesSteady);
    scpi_printf(context, "WifiDroppedBytes=%u\r\n", (unsigned)s.wifiDroppedBytes);
    scpi_printf(context, "WifiDroppedBytesSteady=%u\r\n", (unsigned)s.wifiDroppedBytesSteady);
    {
        uint64_t bytesSent = 0, bytesConfirmed = 0;
        uint32_t sendErrors = 0, partialSends = 0, partialMissing = 0;
        uint32_t rejectedCalls = 0, rejectedBytes = 0;
        uint32_t cirbufProduced = 0, cirbufConsumed = 0, cirbufBufSize = 0;
        wifi_tcp_server_context_t* pTcp = wifi_manager_GetTcpServerContext();
        if (pTcp != NULL) {
            // Atomic snapshot of 64-bit counters (not atomic on 32-bit PIC32MZ)
            taskENTER_CRITICAL();
            bytesSent = pTcp->client.wifiTcpBytesSent;
            bytesConfirmed = pTcp->client.wifiTcpBytesConfirmed;
            sendErrors = pTcp->client.wifiTcpSendErrors;
            partialSends = pTcp->client.wifiTcpPartialSends;
            partialMissing = pTcp->client.wifiPartialBytesMissing;
            rejectedCalls = pTcp->client.wifiWriteBufferRejectedCalls;
            rejectedBytes = pTcp->client.wifiWriteBufferRejectedBytes;
            cirbufProduced = pTcp->client.wCirbuf.producedBytes;
            cirbufConsumed = pTcp->client.wCirbuf.consumedBytes;
            cirbufBufSize  = pTcp->client.wCirbuf.buf_size;
            taskEXIT_CRITICAL();
        }
        // Always print for consistent response schema
        scpi_printf(context, "WifiTcpBytesSent=%llu\r\n", (unsigned long long)bytesSent);
        scpi_printf(context, "WifiTcpBytesConfirmed=%llu\r\n", (unsigned long long)bytesConfirmed);
        scpi_printf(context, "WifiTcpSendErrors=%u\r\n", (unsigned)sendErrors);
        scpi_printf(context, "WifiTcpPartialSends=%u\r\n", (unsigned)partialSends);
        // #367 diag: cumulative byte shortfall across all partial sends
        scpi_printf(context, "WifiPartialBytesMissing=%u\r\n", (unsigned)partialMissing);
        // #371 diag: WriteBuffer-side rejection counters (should match wifiDroppedBytes)
        scpi_printf(context, "WifiWriteBufferRejectedCalls=%u\r\n", (unsigned)rejectedCalls);
        scpi_printf(context, "WifiWriteBufferRejectedBytes=%u\r\n", (unsigned)rejectedBytes);
        // #371 diag: raw circular-buffer SPSC counters — invariant: produced >= consumed,
        // and (produced - consumed) <= buf_size.  If violated, NumBytesFree underflows.
        scpi_printf(context, "WifiCirbufProduced=%u\r\n", (unsigned)cirbufProduced);
        scpi_printf(context, "WifiCirbufConsumed=%u\r\n", (unsigned)cirbufConsumed);
        scpi_printf(context, "WifiCirbufBufSize=%u\r\n", (unsigned)cirbufBufSize);
        // #475 step 3: how long the TCP listen socket has been continuously
        // open.  0 = currently closed.  Long values combined with the
        // silent-failure log lines from #477 give operators a way to spot
        // stuck listen sockets.  See #475 for the eventual periodic-recycle
        // watchdog this enables.
        scpi_printf(context, "WifiListenUptimeSec=%u\r\n",
                    (unsigned)wifi_manager_GetListenSocketUptimeSec());
    }
    scpi_printf(context, "SdDroppedBytes=%u\r\n", (unsigned)s.sdDroppedBytes);
    scpi_printf(context, "SdDroppedBytesSteady=%u\r\n", (unsigned)s.sdDroppedBytesSteady);
    {
        sd_card_write_metrics_t sdm;
        sd_card_manager_GetWriteMetricsSnapshot(&sdm);
        scpi_printf(context, "SdWriteCalls=%u\r\n", (unsigned)sdm.writeCallCount);
        scpi_printf(context, "SdWriteSectors=%u\r\n", (unsigned)sdm.writeSectorCount);
        scpi_printf(context, "SdBytesWritten=%llu\r\n", (unsigned long long)sdm.writeBytesTotal);
        scpi_printf(context, "SdWriteErrors=%u\r\n", (unsigned)sdm.writeErrors);
        scpi_printf(context, "SdWriteMaxLatencyMs=%u\r\n", (unsigned)sdm.writeMaxLatencyMs);
        scpi_printf(context, "SdWriteAlignedCopies=%u\r\n", (unsigned)sdm.writeAlignedCopies);
    }
    scpi_printf(context, "EncoderFailures=%u\r\n", (unsigned)s.encoderFailures);
    scpi_printf(context, "EncoderFailuresSteady=%u\r\n", (unsigned)s.encoderFailuresSteady);
    scpi_printf(context, "EncoderDroppedSamples=%u\r\n", (unsigned)s.encoderDroppedSamples);
    scpi_printf(context, "EncoderDroppedSamplesSteady=%u\r\n", (unsigned)s.encoderDroppedSamplesSteady);
    scpi_printf(context, "DioDroppedSamples=%u\r\n", (unsigned)s.dioDroppedSamples);
    scpi_printf(context, "DioDroppedSamplesSteady=%u\r\n", (unsigned)s.dioDroppedSamplesSteady);
    scpi_printf(context, "EosOverruns=%u\r\n", (unsigned)s.eosOverruns);
    // Timer ISR tracking (#265): actual ISR entry count this session (64-bit
    // so it never wraps in practice). Compare against (TotalSamplesStreamed
    // + QueueDroppedSamples) to verify every timer event is accounted for,
    // and against (freq × duration) to see whether the timer is firing at
    // the requested rate or rate-limited.
    scpi_printf(context, "TimerISRCalls=%llu\r\n", (unsigned long long)s.timerISRCalls);
    // #367 diag: bytes sitting in WiFi circular buffer at session end (Stop)
    scpi_printf(context, "CircularBufferEndBytes=%u\r\n", (unsigned)s.circularBufferEndBytes);
    // #499 diag: sample-pool peak utilization this session.  Compare with
    // SamplePoolCount (MEM:FREE?) — if Used == Count, the pool was saturated
    // and PoolExhaustedSamples > 0 makes sense.  If Used << Count but
    // QueueOverflowSamples > 0, the queue is the bottleneck (streaming_Task
    // drain too slow), not the pool.
    scpi_printf(context, "SamplePoolMaxUsed=%u\r\n",
                (unsigned)AInSampleList_PoolMaxUsed());
#if PB_PROFILE_COUNTERS
    // #388 PB streaming profile counters (compile-time gated).  Raw cycle
    // counts at SYSCLK/2 = 100 MHz on PIC32MZ — divide by 1e8 for seconds,
    // or by 100 for microseconds.  Compare ratios across rows to identify
    // the dominant bottleneck (encoder vs writebuf-copy vs dma-copy vs
    // dma-idle).
    scpi_printf(context, "PbEncodeCycles=%llu\r\n", (unsigned long long)s.pbEncodeCycles);
    scpi_printf(context, "PbEncodeMaxCycles=%u\r\n", (unsigned)s.pbEncodeMaxCycles);
    scpi_printf(context, "PbEncodeBytesOut=%llu\r\n", (unsigned long long)s.pbEncodeBytesOut);
    scpi_printf(context, "UsbWriteBufCycles=%llu\r\n", (unsigned long long)s.usbWriteBufCycles);
    scpi_printf(context, "UsbDmaCopyCycles=%llu\r\n", (unsigned long long)s.usbDmaCopyCycles);
    scpi_printf(context, "UsbDmaPendingCycles=%llu\r\n", (unsigned long long)s.usbDmaPendingCycles);
    scpi_printf(context, "UsbDmaIdleCount=%u\r\n", (unsigned)s.usbDmaIdleCount);
#endif

    // Compute sample loss percentage (64-bit intermediate to avoid overflow)
    uint64_t totalSampleAttempts = s.totalSamplesStreamed + s.queueDroppedSamples;
    uint32_t sampleLoss = totalSampleAttempts > 0
        ? (uint32_t)((s.queueDroppedSamples * 100ULL) / totalSampleAttempts) : 0;
    scpi_printf(context, "SampleLossPercent=%u\r\n", (unsigned)sampleLoss);

    // Compute byte loss percentage (combined across all outputs).
    // Can exceed 100% when multiple outputs drop simultaneously.
    uint64_t totalDroppedBytes = s.usbDroppedBytes + s.wifiDroppedBytes + s.sdDroppedBytes;
    uint32_t byteLoss = s.totalBytesStreamed > 0
        ? (uint32_t)((totalDroppedBytes * 100ULL) / s.totalBytesStreamed) : 0;
    scpi_printf(context, "ByteLossPercent=%u\r\n", (unsigned)byteLoss);
    scpi_printf(context, "WindowLossPercent=%u\r\n", (unsigned)s.windowLossPercent);

    // Sync QUES bits from streaming engine to SCPI registers on each stats query
    SCPI_SyncQuesBits();

    return SCPI_RES_OK;
}

/**
 * Quiesce all DMA consumers and reset the coherent pool.
 * Closes SD file (filesystem safety), waits for WiFi SPI idle.
 * USB is assumed already waited by caller.
 * @return true if successful, false if a timeout occurred
 */
static bool SCPI_QuiesceAndResetCoherentPool(void) {
    // SD: close write file so f_write can't be mid-DMA during reset.
    // Only interrupt WRITE mode (DMA consumer). Leave READ/LIST/DELETE alone.
    {
        sd_card_manager_settings_t* pSd = BoardRunTimeConfig_Get(BOARDRUNTIME_SD_CARD_SETTINGS);
        if (pSd && pSd->mode == SD_CARD_MANAGER_MODE_WRITE) {
            pSd->mode = SD_CARD_MANAGER_MODE_NONE;
            sd_card_manager_UpdateSettings(pSd);
        }
        int sdWait = 0;
        while (!sd_card_manager_IsIdle() && sdWait < 500) {
            vTaskDelay(pdMS_TO_TICKS(10));
            sdWait++;
        }
        if (sdWait >= 500) {
            LOG_E("SD idle timeout before DMA resize (%d ms)", sdWait * 10);
            return false;
        }
    }
    // WiFi: wait for any in-flight SPI DMA to complete.
    if (!WDRV_WINC_SPI_WaitIdle(1000)) {
        LOG_E("WiFi SPI idle timeout before DMA resize");
        return false;
    }
    CoherentPool_Reset();
    return true;
}

static scpi_result_t SCPI_StartStreaming(scpi_t * context) {
    int32_t freq;

    StreamingRuntimeConfig * pRunTimeStreamConfig = BoardRunTimeConfig_Get(
            BOARDRUNTIME_STREAMING_CONFIGURATION);
    const tBoardConfig * pBoardConfig = BoardConfig_Get(
            BOARDCONFIG_ALL_CONFIG, 0);
    // Check power state first - streaming requires powered-up state
    const tPowerData *pPowerState = BoardData_Get(BOARDDATA_POWER_DATA, 0);
    if (pPowerState == NULL) {
        LOG_E("Streaming command rejected: Power data unavailable");
        SCPI_ErrorPush(context, SCPI_ERROR_SYSTEM_ERROR);
        return SCPI_RES_ERR;
    }
    if (pPowerState->powerState != POWERED_UP && pPowerState->powerState != POWERED_UP_EXT_DOWN) {
        LOG_E("Streaming command rejected: Device must be powered up (SYST:POW:STAT 1)");
        SCPI_ErrorPush(context, SCPI_ERROR_EXECUTION_ERROR);
        return SCPI_RES_ERR;
    }

    volatile AInRuntimeArray * pRuntimeAInChannels = BoardRunTimeConfig_Get(BOARDRUNTIMECONFIG_AIN_CHANNELS);
    volatile AInArray *pBoardConfigADC = BoardConfig_Get(BOARDCONFIG_AIN_CHANNELS, 0);

    // Validate ADC runtime/config pointers
    if (pRuntimeAInChannels == NULL || pBoardConfigADC == NULL) {
        LOG_E("Streaming command rejected: ADC runtime/config unavailable");
        SCPI_ErrorPush(context, SCPI_ERROR_SYSTEM_ERROR);
        return SCPI_RES_ERR;
    }

    // Defensive bound check to avoid mismatched arrays
    if (pBoardConfigADC->Size > pRuntimeAInChannels->Size) {
        LOG_E("Streaming command rejected: ADC config/runtime size mismatch (%u > %u)",
              pBoardConfigADC->Size, pRuntimeAInChannels->Size);
        SCPI_ErrorPush(context, SCPI_ERROR_SYSTEM_ERROR);
        return SCPI_RES_ERR;
    }

    // timer running frequency
    uint32_t clkFreq = TimerApi_FrequencyGet(pBoardConfig->StreamingConfig.TimerIndex);

    uint16_t activeType1ChannelCount = 0;
    uint16_t totalEnabledPublicChannels = 0;
    bool hasActiveAD7609Channels = false;
    Streaming_CountActiveChannels(&activeType1ChannelCount,
                                  &totalEnabledPublicChannels,
                                  &hasActiveAD7609Channels);
    bool hasEnabledChannels = (totalEnabledPublicChannels > 0);

    // Build channel mapping for compact sample pool (#177).
    // Must happen before pool partitioning (needs channel count for element sizing).
    Streaming_BuildChannelMapping(pBoardConfig, (const AInRuntimeArray*)pRuntimeAInChannels);

    // Check if DIO is globally enabled
    bool *pDIOGlobalEnable = BoardRunTimeConfig_Get(BOARDRUNTIMECONFIG_DIO_GLOBAL_ENABLE);
    if (pDIOGlobalEnable && *pDIOGlobalEnable) {
        hasEnabledChannels = true;
    }

    if (!hasEnabledChannels) {
        LOG_E("Streaming command rejected: No ADC or DIO channels enabled");
        SCPI_ErrorPush(context, SCPI_ERROR_SETTINGS_CONFLICT);
        return SCPI_RES_ERR;
    }

    bool freqProvided = SCPI_ParamInt32(context, &freq, FALSE);
    if (freqProvided && freq == 0) {
        // Frequency = 0 is the explicit disable form.  Always allowed,
        // including under heap pressure (the user may be trying to
        // stop streaming as a recovery action).
        pRunTimeStreamConfig->IsEnabled = false;
        Streaming_UpdateState();
        UsbCdc_FlushWriteBuffer();
        return SCPI_RES_OK;
    }

    // #475 step 4 — heap-floor enforcement applied to every new-start
    // path (freq > 0 explicit, or no-argument re-start with current
    // freq).  Rationale: in #475 the symptom (TCP 9760 unreachable)
    // appeared after heap had drifted down to ~7 KB across hours of
    // streaming; WINC SDK accept() + wifi_manager event paths allocate
    // from this heap, and starting a new session when already pinched
    // accelerates the slide.
    //
    // Floor is 2500 B (lowered from 10 KB 2026-05-31 — see
    // MIN_HEAP_FREE_FOR_STREAM_START_BYTES in SCPIInterface.h for the
    // rationale + tradeoff).  Boot-idle HeapFree is ~13 KB per CLAUDE.md,
    // so the guard now only bites under severe accumulated pressure (the
    // #490 per-session leak), not on ordinary post-boot starts.
    //
    // Placed AFTER the freq==0 disable branch, but BEFORE the freq-
    // parameter branch — covers both the explicit-freq and no-argument
    // start forms.  Disable path remains always-allowed above.
    //
    // Scope-gated to WiFi-relevant interfaces: pure USB or SD streams
    // don't allocate from the FreeRTOS heap during operation (their
    // DMA + circular buffers live in the static StreamingBufferPool +
    // CoherentPool, not heap), so the #475 failure mode doesn't apply
    // to them.  ActiveInterface is checked here; the interface may not
    // yet be auto-detected at this point in the flow, but if the user
    // explicitly set WiFi via SYST:STR:INTerface (or it's already the
    // default from a prior session), we gate.  The auto-detect later
    // in this function may pick WiFi anyway — false negatives here are
    // acceptable since the floor is purely defensive.
    StreamingInterface currentInterfaceAtCheck = pRunTimeStreamConfig->ActiveInterface;
    if (currentInterfaceAtCheck == StreamingInterface_WiFi) {
        size_t heapFreeAtStart = xPortGetFreeHeapSize();
        if (heapFreeAtStart < MIN_HEAP_FREE_FOR_STREAM_START_BYTES) {
            LOG_E("WiFi streaming start rejected: free heap %u < floor %u (#475 — bounce LAN:APPLY or reboot)",
                  (unsigned)heapFreeAtStart,
                  (unsigned)MIN_HEAP_FREE_FOR_STREAM_START_BYTES);
            SCPI_ErrorPush(context, SCPI_ERROR_EXECUTION_ERROR);
            return SCPI_RES_ERR;
        }
    }

    if (freqProvided) {
        // NQ3 Smart Frequency Management:
        // When external AD7609 channels are active WITHOUT any Type 1 MC12bADC channels,
        // force internal monitoring to 1Hz (since only monitoring channels would be streaming)
        if (hasActiveAD7609Channels && activeType1ChannelCount == 0 && freq > 1) {
            freq = 1; // Override to 1Hz for internal monitoring when external ADC active
        }

        // Three-constraint frequency capping (validated via benchmark testing)
        // See https://github.com/daqifi/daqifi-nyquist-firmware/issues/215
        // Bypass cap in benchmark mode to measure pure interface throughput.
        if (Streaming_GetBenchmarkMode() == BENCHMARK_OFF) {
            // #232: reject up-front when any Type 2 (muxed) MC12b channel is
            // enabled and the requested freq would silently cap the muxed
            // scan rate.  The firmware always sets
            //   ChannelScanFreqDiv = freq / 1000 (when freq > 1000)
            // so T2 channels sample at most 1 kHz regardless of timer rate.
            // Pre-#232 we silently throttled — user saw "5 kHz streaming"
            // but T2 data was 1 kHz.  Now we surface that as a SCPI error
            // so the client can either reduce freq, disable T2 channels,
            // or opt-in via SYST:STR:BENCHmark (which bypasses the check
            // since benchmarks intentionally measure timer-rate throughput).
            if (freq > (int32_t)STREAMING_MUXED_CAP_HZ) {
                bool hasMuxed = false;
                volatile AInRuntimeArray* rt = (volatile AInRuntimeArray*)pRuntimeAInChannels;
                volatile AInArray* cfg = (volatile AInArray*)pBoardConfigADC;
                size_t cnt = (cfg->Size < rt->Size) ? cfg->Size : rt->Size;
                for (size_t i = 0; i < cnt; i++) {
                    if (rt->Data[i].IsEnabled != 1) continue;
                    if (cfg->Data[i].Type != AIn_MC12bADC) continue;
                    if (!cfg->Data[i].Config.MC12b.IsPublic) continue;
                    if (cfg->Data[i].Config.MC12b.ChannelType == MC12B_CHANNEL_TYPE_MUXED) {
                        hasMuxed = true;
                        break;
                    }
                }
                if (hasMuxed) {
                    LOG_I("Streaming rejected: T2 channels max %u Hz (requested %d Hz)",
                          (unsigned)STREAMING_MUXED_CAP_HZ, (int)freq);
                    /* Stringify STREAMING_MUXED_CAP_HZ into the user-facing
                     * error so the message stays in sync if the constant
                     * ever changes (#449 Qodo r2 finding). */
                    #define MUXED_CAP_STR_HELPER(x) #x
                    #define MUXED_CAP_STR(x) MUXED_CAP_STR_HELPER(x)
                    static const char muxedErrMsg[] =
                        "T2 (muxed) channels cap at " MUXED_CAP_STR(STREAMING_MUXED_CAP_HZ)
                        " Hz; reduce freq, disable T2 channels, or use "
                        "SYST:STR:BENCHmark to bypass";
                    #undef MUXED_CAP_STR
                    #undef MUXED_CAP_STR_HELPER
                    SCPI_ErrorPushEx(context, SCPI_ERROR_SETTINGS_CONFLICT,
                                     (char *)muxedErrMsg, sizeof(muxedErrMsg) - 1);
                    return SCPI_RES_ERR;
                }
            }

            uint32_t maxFreq = Streaming_ComputeMaxFreq(
                activeType1ChannelCount, totalEnabledPublicChannels);
            if (freq > (int32_t)maxFreq) {
                LOG_I("Frequency capped: %d Hz -> %u Hz (%u ch, %u type1)",
                      (int)freq, (unsigned)maxFreq,
                      (unsigned)totalEnabledPublicChannels,
                      (unsigned)activeType1ChannelCount);
                freq = (int32_t)maxFreq;
            }
        } else {
            // Benchmark: no frequency cap. Timer hardware limit is
            // PBCLK3/2 = 50MHz. Practical limit is when ISR overhead
            // exceeds the timer period. Let the user push until it breaks.
            if (freq > 100000) freq = 100000;  // Sanity cap at 100kHz
            LOG_I("Benchmark mode: uncapped freq=%d Hz (%u ch)",
                  (int)freq, (unsigned)totalEnabledPublicChannels);
        }

        int32_t freqLimit = (Streaming_GetBenchmarkMode() != BENCHMARK_OFF) ? 100000 : (int32_t)STREAMING_ISR_MAX_HZ;
        if (freq >= 1 && freq <= freqLimit)
        {

            // Note: Internal monitoring channels have fixed 1Hz in NQ3 runtime config

            // PIC32MZ type-B timer counts 0..PR inclusive (PR+1 cycles per match).
            // Compute the period in timer cycles, then subtract 1 for the PR value.
            // Ceiling division ensures actual freq <= requested freq (no overspeed).
            uint32_t periodCycles = (clkFreq + freq - 1) / freq;
            if (periodCycles < 2) periodCycles = 2;  // PR must be >= 1
            pRunTimeStreamConfig->ClockPeriod = periodCycles - 1;
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

    /* Interface_All is USB+SD (WiFi excluded by SPI bus conflict). Any
     * interface other than WiFi-only can drive SD logging when the SD
     * card is enabled and a filename is set. */
    sd_card_manager_settings_t* pSDCardSettings =
        BoardRunTimeConfig_Get(BOARDRUNTIME_SD_CARD_SETTINGS);
    bool sdLoggingRequested = (pRunTimeStreamConfig->ActiveInterface != StreamingInterface_WiFi) &&
                              pSDCardSettings != NULL && pSDCardSettings->enable &&
                              pSDCardSettings->file[0] != '\0';

    if (pRunTimeStreamConfig->ActiveInterface == StreamingInterface_WiFi &&
        pSDCardSettings != NULL && pSDCardSettings->enable &&
        pSDCardSettings->mode == SD_CARD_MANAGER_MODE_WRITE) {
        LOG_E("Cannot start WiFi streaming while SD logging is active (SPI bus conflict)");
        SCPI_ErrorPush(context, SCPI_ERROR_EXECUTION_ERROR);
        return SCPI_RES_ERR;
    }

    // If SD logging is requested, set mode to WRITE now (deferred from LOGging command)
    if (sdLoggingRequested) {
        // Check if SD card is busy with another operation (DELETE, FORMAT, etc.)
        if (sd_card_manager_IsBusy()) {
            LOG_E("Cannot start SD logging - SD card busy with another operation");
            SCPI_ErrorPush(context, SCPI_ERROR_EXECUTION_ERROR);
            return SCPI_RES_ERR;
        }
        // #498 / #503: pre-start disk-full gate consolidated INTO the
        // SD task's WRITE init.  A single UpdateSettings(WRITE) call now
        // does the mount + (optional) disk-full check + file open in one
        // pass.  Previously this site set mode=GET_SPACE, waited, then
        // set mode=WRITE, which triggered two DEINIT/MOUNT cycles per
        // STR:START.  Now: one cycle.
        //
        // Behavior contract preserved:
        //   - If minFreeBytes == 0, the SD task skips the check and
        //     proceeds straight to file open (same as legacy zero-gate).
        //   - If minFreeBytes > 0 and free space is below the floor, the
        //     SD task transitions to ERROR with startupDiskFull=true.
        //     IsWriteReady never returns true; the loop below times out;
        //     we then check StartupDiskFull() to surface the friendly
        //     "out of space" message instead of generic "WRITE timeout."
        //   - If minFreeBytes > 0 but the free-space query itself fails
        //     (transient mount glitch), the SD task logs and falls
        //     through to file open — caller gets the existing
        //     SCPI_ERROR_EXECUTION_ERROR if the open then fails.
        // Synchronously clear the disk-full flag BEFORE arming the
        // new WRITE request — otherwise a stale `true` from a previous
        // disk-full rejection causes the early-exit poll below to bail
        // instantly before the SD task has had a chance to clear the
        // flag itself in CURRENT_DRIVE.  Without this pre-clear, every
        // STR:START after a single disk-full rejection would fail
        // nondeterministically with a misleading "out of space" message
        // even when free space is fine on the current attempt.
        // (Qodo /agentic_review pass-1 finding on PR #508: "Stale
        // disk-full short-circuits start".)
        sd_card_manager_ClearStartupDiskFull();

        pSDCardSettings->mode = SD_CARD_MANAGER_MODE_WRITE;
        sd_card_manager_UpdateSettings(pSDCardSettings);

        // Wait for SD file to be open before starting streaming.
        // Without this, early samples are dropped while SD mounts/opens.
        //
        // Early-exit when the SD task signals startupDiskFull — without
        // it, a disk-full rejection costs the caller a full 5 s
        // (500 × 10 ms) wait before we read the flag below.  The SD
        // task knows the answer within milliseconds of CHECK_DISK_FULL
        // running; polling that flag in the loop gets the friendly
        // -200 back to the operator promptly.  The pre-clear above
        // guarantees this flag reflects ONLY the current request's
        // outcome.  (Qodo follow-up to #503, "Exit early on disk-full".)
        int readyWait = 0;
        while (!sd_card_manager_IsWriteReady() && readyWait < 500) {
            if (sd_card_manager_StartupDiskFull()) {
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(10));
            readyWait++;
        }
        if (!sd_card_manager_IsWriteReady()) {
            // #503: distinguish disk-full rejection from a generic open
            // failure.  The SD task sets startupDiskFull=true and routes
            // to ERROR when the free-space pre-check fails the floor;
            // surface that as the precise log line operators expect.
            if (sd_card_manager_StartupDiskFull()) {
                uint64_t freeBytes = 0, totalBytes = 0;
                bool haveSpace = sd_card_manager_GetSpaceInfo(&freeBytes, &totalBytes);
                /* Snapshot the 64-bit floor under critical section per
                 * CLAUDE.md atomicity rules — pairs with the setter's
                 * critical-section write in SCPI_StorageSDMinFreeSet. */
                uint64_t floor;
                taskENTER_CRITICAL();
                floor = pSDCardSettings->minFreeBytes;
                taskEXIT_CRITICAL();
                /* CHECK_DISK_FULL caches spaceResult before the
                 * rejection branch, so haveSpace SHOULD be true in this
                 * path.  The fallback exists for defense-in-depth: if a
                 * future code change clears spaceResultValid between
                 * the SD task's reject and the SCPI read, log "unknown"
                 * instead of formatting a misleading 0 B value. */
                if (haveSpace) {
                    LOG_E("[SD] STR:START refused: %llu B free < %llu B floor",
                          (unsigned long long)freeBytes,
                          (unsigned long long)floor);
                } else {
                    LOG_E("[SD] STR:START refused: disk full (space unknown), floor=%llu B",
                          (unsigned long long)floor);
                }
            } else {
                LOG_E("SD file not ready after %d ms", readyWait * 10);
            }
            pSDCardSettings->mode = SD_CARD_MANAGER_MODE_NONE;
            sd_card_manager_UpdateSettings(pSDCardSettings);
            SCPI_ErrorPush(context, SCPI_ERROR_EXECUTION_ERROR);
            return SCPI_RES_ERR;
        }
    }

    // Reset per-session counters (matches Streaming_ClearStats lifecycle)
    sd_card_manager_ResetWriteMetrics();
    {
        wifi_tcp_server_context_t* pTcp = wifi_manager_GetTcpServerContext();
        if (pTcp != NULL) {
            taskENTER_CRITICAL();
            pTcp->client.wifiTcpBytesSent = 0;
            pTcp->client.wifiTcpBytesConfirmed = 0;
            pTcp->client.wifiTcpSendErrors = 0;
            pTcp->client.wifiTcpPartialSends = 0;
            pTcp->client.wifiPartialBytesMissing = 0;
            for (uint8_t i = 0; i < WIFI_TCP_MAX_IN_FLIGHT; i++) {
                pTcp->client.inflightSizes[i] = 0;
            }
            pTcp->client.inflightHead = 0;
            pTcp->client.inflightTail = 0;
            taskEXIT_CRITICAL();
        }
    }

    // Stop streaming before re-partitioning. If called from WiFi task
    // (priority 2, same as streaming task), the streaming task could
    // round-robin mid-swap and use partially-swapped buffer pointers.
    // Stopping first ensures no task is using the old buffers.
    if (pRunTimeStreamConfig->IsEnabled && pRunTimeStreamConfig->Running) {
        pRunTimeStreamConfig->IsEnabled = false;
        Streaming_UpdateState();  // Stop timer + streaming task
    }

    // Partition the streaming buffer pool (auto or static per MemoryConfig) +
    // DMA coherent pool + sample pool, via the shared helper — same path used
    // by SYST:STR:THR, the WiFi finder, and SYST:MEM:AUTO (issue #229).  The
    // helper internally does the USB-DMA + task-quiescence waits (abort on
    // timeout, #486) before the destructive re-partition.
    {
        const AInChannelMapping* chMapping = Streaming_GetChannelMapping();
        uint8_t enabledChannels = (chMapping->count > 0) ? chMapping->count : 1;
        MemoryConfig* mc = BoardRunTimeConfig_Get(BOARDRUNTIME_MEMORY_CONFIG);
        if (!PrepareStreamingBuffers(mc->samplePoolCount,
                                     AInSampleList_ElementSize(enabledChannels))) {
            SCPI_ExecutionError(context, "STR:START: buffer partition failed (USB DMA / tasks not quiescent, or pool error)");
            return SCPI_RES_ERR;
        }

        // Re-enable SD if it was closed for DMA quiesce
        if (sdLoggingRequested) {
            pSDCardSettings->mode = SD_CARD_MANAGER_MODE_WRITE;
            sd_card_manager_UpdateSettings(pSDCardSettings);
            int readyWait = 0;
            while (!sd_card_manager_IsWriteReady() && readyWait < 500) {
                vTaskDelay(pdMS_TO_TICKS(10));
                readyWait++;
            }
            if (!sd_card_manager_IsWriteReady()) {
                LOG_E("SD file not ready after DMA resize (%d ms)", readyWait * 10);
            }
        }
    }

    pRunTimeStreamConfig->IsEnabled = true;
    Streaming_UpdateState();

    // Update STATus:OPERation condition register
    SCPI_SetOperBits(OPER_MEASURING);
    if (sdLoggingRequested) {
        SCPI_SetOperBits(OPER_SD_LOGGING);
    }

    return SCPI_RES_OK;
}

static scpi_result_t SCPI_StopStreaming(scpi_t * context) {
    StreamingRuntimeConfig * pRunTimeStreamConfig = BoardRunTimeConfig_Get(
            BOARDRUNTIME_STREAMING_CONFIGURATION);

    if (pRunTimeStreamConfig) {
        pRunTimeStreamConfig->IsEnabled = false;
    }

    Streaming_UpdateState();

    // Flush any remaining USB CDC data so the host receives all streamed bytes.
    // Without this, data sitting in the circular buffer (waiting for DMA) may
    // not reach the host until the next SCPI command triggers a USB write.
    UsbCdc_FlushWriteBuffer();

    // Close SD card file if logging was enabled
    sd_card_manager_settings_t* pSDCardRuntimeConfig = BoardRunTimeConfig_Get(BOARDRUNTIME_SD_CARD_SETTINGS);
    if (pSDCardRuntimeConfig != NULL &&
        pSDCardRuntimeConfig->enable && pSDCardRuntimeConfig->mode == SD_CARD_MANAGER_MODE_WRITE) {
        // Set mode to NONE and update to trigger file close (DEINIT → UNMOUNT → close)
        pSDCardRuntimeConfig->mode = SD_CARD_MANAGER_MODE_NONE;
        sd_card_manager_UpdateSettings(pSDCardRuntimeConfig);
        // Wait for SD card manager to drain buffer, close file, and go idle
        {
            int idleWait = 0;
            while (!sd_card_manager_IsIdle() && idleWait < 500) {
                vTaskDelay(pdMS_TO_TICKS(10));
                idleWait++;
            }
            if (idleWait >= 500) {
                LOG_E("[SD] StopStreaming: SD idle timeout after 5s");
            }
        }
    }

    // Reset encoder state so next session gets fresh headers
    csv_ResetEncoder();
    json_ResetEncoder();
    Streaming_ResetSdPbMetadata();

    // Clear STATus:OPERation condition register
    SCPI_ClearOperBits(OPER_MEASURING | OPER_SD_LOGGING);

    // Sync STATus:QUEStionable condition register (streaming health bits cleared in Streaming_Stop)
    SCPI_SyncQuesBits();

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

static scpi_result_t SCPI_SetDataPrecision(scpi_t * context) {
    int32_t param1;
    StreamingRuntimeConfig * pRunTimeStreamConfig = BoardRunTimeConfig_Get(
            BOARDRUNTIME_STREAMING_CONFIGURATION);
    if (!SCPI_ParamInt32(context, &param1, TRUE)) {
        return SCPI_RES_ERR;
    }
    if (param1 < 0 || param1 > 10) {
        SCPI_ErrorPush(context, SCPI_ERROR_DATA_OUT_OF_RANGE);
        return SCPI_RES_ERR;
    }
    pRunTimeStreamConfig->VoltagePrecision = (uint8_t)param1;
    return SCPI_RES_OK;
}

static scpi_result_t SCPI_GetDataPrecision(scpi_t * context) {
    StreamingRuntimeConfig * pRunTimeStreamConfig = BoardRunTimeConfig_Get(
            BOARDRUNTIME_STREAMING_CONFIGURATION);
    SCPI_ResultInt32(context, (int)pRunTimeStreamConfig->VoltagePrecision);
    return SCPI_RES_OK;
}

static scpi_result_t SCPI_SaveDataPrecision(scpi_t * context) {
    DaqifiSettings settings;
    memset(&settings, 0, sizeof(DaqifiSettings));
    // Load existing NVM to preserve other fields (e.g. calVals)
    if (!daqifi_settings_LoadFromNvm(DaqifiSettings_TopLevelSettings, &settings)) {
        daqifi_settings_LoadFactoryDeafult(DaqifiSettings_TopLevelSettings, &settings);
    }
    settings.type = DaqifiSettings_TopLevelSettings;
    // SaveToNvm captures current precision from runtime config automatically
    if (!daqifi_settings_SaveToNvm(&settings)) {
        return SCPI_RES_ERR;
    }
    return SCPI_RES_OK;
}

static scpi_result_t SCPI_LoadDataPrecision(scpi_t * context) {
    DaqifiSettings settings;
    memset(&settings, 0, sizeof(DaqifiSettings));
    if (!daqifi_settings_LoadFromNvm(DaqifiSettings_TopLevelSettings, &settings)) {
        return SCPI_RES_ERR;
    }
    uint8_t savedPrec = settings.settings.topLevelSettings.voltagePrecision;
    if (savedPrec <= 10) {
        StreamingRuntimeConfig *pStreamCfg = BoardRunTimeConfig_Get(
                BOARDRUNTIME_STREAMING_CONFIGURATION);
        if (pStreamCfg != NULL) {
            pStreamCfg->VoltagePrecision = savedPrec;
        }
    }
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

    // Check if SD card is enabled when trying to use SD or USB+SD interfaces
    if (param1 == StreamingInterface_SD || param1 == StreamingInterface_UsbAndSd) {
        if (!pSDCardSettings->enable) {
            LOG_E("Cannot set SD/USB+SD interface - SD card not enabled. Use SYSTem:STORage:SD:ENAble 1");
            SCPI_ErrorPush(context, SCPI_ERROR_EXECUTION_ERROR);
            return SCPI_RES_ERR;
        }
    }

    /* WiFi+SD SPI conflict: only a risk when switching explicitly to
     * WiFi while SD is actively writing. Interface_All is USB+SD now
     * (no WiFi), so no check needed for All. */
    if (param1 == StreamingInterface_WiFi) {
        if (pSDCardSettings->enable && pSDCardSettings->mode == SD_CARD_MANAGER_MODE_WRITE) {
            LOG_E("Cannot switch to WiFi while SD streaming is active (SPI bus conflict). Stop streaming first.");
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

    if (usbSettings->cmdHistoryCount == 0) {
        SCPI_ResultCharacters(context, "No command history", 18);
        return SCPI_RES_OK;
    }

    // #347: shared SCPI response scratch buffer (was stack-local char[256]).
    char* buffer = (char*)SCPI_ResponseBuf_Take();
    if (buffer == NULL) {
        return SCPI_RES_ERR;
    }

    // Calculate starting position in circular buffer
    int startIdx = (usbSettings->cmdHistoryHead - usbSettings->cmdHistoryCount + SCPI_CMD_HISTORY_SIZE) % SCPI_CMD_HISTORY_SIZE;

    // Send header — guard against snprintf encoding errors (< 0) and
    // clamp to buffer size if the output would otherwise be truncated.
    int len = snprintf(buffer, SCPI_RESPONSE_BUF_SIZE, "Last %d commands:\r\n", usbSettings->cmdHistoryCount);
    if (len > 0) {
        size_t wlen = ((size_t)len < SCPI_RESPONSE_BUF_SIZE)
                      ? (size_t)len : (SCPI_RESPONSE_BUF_SIZE - 1);
        context->interface->write(context, buffer, wlen);
    }

    // Send command history
    for (int i = 0; i < usbSettings->cmdHistoryCount; i++) {
        int idx = (startIdx + i) % SCPI_CMD_HISTORY_SIZE;
        len = snprintf(buffer, SCPI_RESPONSE_BUF_SIZE, "%d: %s\r\n",
                      usbSettings->cmdHistoryCount - i,
                      usbSettings->cmdHistory[idx]);
        if (len > 0) {
            size_t wlen = ((size_t)len < SCPI_RESPONSE_BUF_SIZE)
                          ? (size_t)len : (SCPI_RESPONSE_BUF_SIZE - 1);
            context->interface->write(context, buffer, wlen);
        }
    }

    SCPI_ResponseBuf_Give();
    return SCPI_RES_OK;
}

// =============================================================================
// Dynamic Memory Configuration SCPI Callbacks
//
// All setters reject changes while streaming is active (SCPI_ERROR_EXECUTION_ERROR).
// Settings take effect at next StartStreamData. Runtime-only (not NVM-persisted,
// reset on reboot to safe defaults).
//
// Bounds:
//   SD buffer:     4096 - 65536 bytes, must be multiple of 512 (sector alignment)
//   WiFi buffer:   1400 - 65536 bytes (min = SOCKET_BUFFER_MAX_LENGTH)
//   USB buffer:    4096 - 65536 bytes (min = USBCDC_WBUFFER_SIZE)
//   Sample pool:   0 (auto = DEFAULT_AIN_SAMPLE_COUNT), or 100 - 2000
// =============================================================================

/**
 * @brief Guard: reject memory config changes while streaming is active.
 * @return true if streaming is active (caller should return SCPI_RES_ERR)
 */
static bool SCPI_MemRejectIfStreaming(scpi_t * context) {
    StreamingRuntimeConfig* sc = BoardRunTimeConfig_Get(BOARDRUNTIME_STREAMING_CONFIGURATION);
    if (sc->IsEnabled && sc->Running) {
        LOG_E("Memory config rejected: streaming is active");
        SCPI_ErrorPush(context, SCPI_ERROR_EXECUTION_ERROR);
        return true;
    }
    return false;
}

static scpi_result_t SCPI_SetMemSdBuf(scpi_t * context) {
    if (SCPI_MemRejectIfStreaming(context)) return SCPI_RES_ERR;
    int32_t val;
    if (!SCPI_ParamInt32(context, &val, TRUE)) return SCPI_RES_ERR;
    // Must be 4096-65536 and a multiple of 512 (SD sector alignment)
    if (val < 4096 || val > 65536 || (val % 512) != 0) {
        SCPI_ErrorPush(context, SCPI_ERROR_DATA_OUT_OF_RANGE);
        return SCPI_RES_ERR;
    }
    MemoryConfig* mc = BoardRunTimeConfig_Get(BOARDRUNTIME_MEMORY_CONFIG);
    mc->sdCircularBufSize = (uint32_t)val;
    return SCPI_RES_OK;
}

static scpi_result_t SCPI_GetMemSdBuf(scpi_t * context) {
    // #494: return the ACTIVE partition size from StreamingBufferPool.
    // Uses the size-only accessor to avoid pointer-arithmetic UB if a
    // SCPI query overlaps a concurrent SBP_Partition() (PR #495 Qodo
    // pass 2 bug 1).  Pool is initialized + partitioned at boot in
    // app_freertos.c:495, so a zero return indicates a partition
    // failure path, not a "before SYST:STR:START" state — surface it
    // honestly rather than masking with MemoryConfig defaults (Qodo
    // pass 2 bug 2).
    SCPI_ResultInt32(context, (int32_t)StreamingBufferPool_SdCircularSize());
    return SCPI_RES_OK;
}

static scpi_result_t SCPI_SetMemWifiBuf(scpi_t * context) {
    if (SCPI_MemRejectIfStreaming(context)) return SCPI_RES_ERR;
    int32_t val;
    if (!SCPI_ParamInt32(context, &val, TRUE)) return SCPI_RES_ERR;
    // Min = SOCKET_BUFFER_MAX_LENGTH (1400), max = 65536
    if (val < 1400 || val > 65536) {
        SCPI_ErrorPush(context, SCPI_ERROR_DATA_OUT_OF_RANGE);
        return SCPI_RES_ERR;
    }
    MemoryConfig* mc = BoardRunTimeConfig_Get(BOARDRUNTIME_MEMORY_CONFIG);
    mc->wifiCircularBufSize = (uint32_t)val;
    return SCPI_RES_OK;
}

static scpi_result_t SCPI_GetMemWifiBuf(scpi_t * context) {
    // #494: see SCPI_GetMemSdBuf.
    SCPI_ResultInt32(context, (int32_t)StreamingBufferPool_WifiSize());
    return SCPI_RES_OK;
}

static scpi_result_t SCPI_SetMemUsbBuf(scpi_t * context) {
    if (SCPI_MemRejectIfStreaming(context)) return SCPI_RES_ERR;
    int32_t val;
    if (!SCPI_ParamInt32(context, &val, TRUE)) return SCPI_RES_ERR;
    // Min = USBCDC_WBUFFER_SIZE (4096), max = 65536
    if (val < 4096 || val > 65536) {
        SCPI_ErrorPush(context, SCPI_ERROR_DATA_OUT_OF_RANGE);
        return SCPI_RES_ERR;
    }
    MemoryConfig* mc = BoardRunTimeConfig_Get(BOARDRUNTIME_MEMORY_CONFIG);
    mc->usbCircularBufSize = (uint32_t)val;
    return SCPI_RES_OK;
}

static scpi_result_t SCPI_GetMemUsbBuf(scpi_t * context) {
    // #494: see SCPI_GetMemSdBuf.
    SCPI_ResultInt32(context, (int32_t)StreamingBufferPool_UsbSize());
    return SCPI_RES_OK;
}

static scpi_result_t SCPI_SetMemSamplePool(scpi_t * context) {
    if (SCPI_MemRejectIfStreaming(context)) return SCPI_RES_ERR;
    int32_t val;
    if (!SCPI_ParamInt32(context, &val, TRUE)) return SCPI_RES_ERR;
    // 0 = auto (DEFAULT_AIN_SAMPLE_COUNT), 100-2000 = explicit
    if (val != 0 && (val < 100 || val > (int32_t)MAX_AIN_SAMPLE_COUNT)) {
        SCPI_ErrorPush(context, SCPI_ERROR_DATA_OUT_OF_RANGE);
        return SCPI_RES_ERR;
    }
    MemoryConfig* mc = BoardRunTimeConfig_Get(BOARDRUNTIME_MEMORY_CONFIG);
    mc->samplePoolCount = (uint32_t)val;
    return SCPI_RES_OK;
}

static scpi_result_t SCPI_GetMemSamplePool(scpi_t * context) {
    MemoryConfig* mc = BoardRunTimeConfig_Get(BOARDRUNTIME_MEMORY_CONFIG);
    uint32_t effective = mc->samplePoolCount ? mc->samplePoolCount : DEFAULT_AIN_SAMPLE_COUNT;
    SCPI_ResultInt32(context, (int32_t)effective);
    return SCPI_RES_OK;
}

static scpi_result_t SCPI_SetMemEncoderBuf(scpi_t * context) {
    if (SCPI_MemRejectIfStreaming(context)) return SCPI_RES_ERR;
    int32_t val;
    if (!SCPI_ParamInt32(context, &val, TRUE)) return SCPI_RES_ERR;
    // 0 = auto (ENCODER_BUFFER_DEFAULT), 1024-65536 = explicit
    if (val != 0 && (val < (int32_t)ENCODER_BUFFER_MIN || val > 65536)) {
        SCPI_ErrorPush(context, SCPI_ERROR_DATA_OUT_OF_RANGE);
        return SCPI_RES_ERR;
    }
    MemoryConfig* mc = BoardRunTimeConfig_Get(BOARDRUNTIME_MEMORY_CONFIG);
    mc->encoderBufSize = (uint32_t)val;
    return SCPI_RES_OK;
}

static scpi_result_t SCPI_GetMemEncoderBuf(scpi_t * context) {
    // #494: see SCPI_GetMemSdBuf.
    SCPI_ResultInt32(context, (int32_t)StreamingBufferPool_EncoderSize());
    return SCPI_RES_OK;
}

static scpi_result_t SCPI_GetMemFree(scpi_t * context) {
    size_t heapFree = xPortGetFreeHeapSize();
    size_t heapMinEver = xPortGetMinimumEverFreeHeapSize();
    size_t heapTotal = configTOTAL_HEAP_SIZE;
    uint32_t poolTotal = CoherentPool_TotalSize();
    uint32_t poolFree = CoherentPool_FreeBytes();
    size_t samplePoolCap = AInSampleList_PoolCapacity();

    scpi_printf(context, "HeapTotal=%u\r\n", (unsigned)heapTotal);
    scpi_printf(context, "HeapFree=%u\r\n", (unsigned)heapFree);
    scpi_printf(context, "HeapUsed=%u\r\n", (unsigned)(heapTotal - heapFree));
    scpi_printf(context, "HeapMinEverFree=%u\r\n", (unsigned)heapMinEver);
    scpi_printf(context, "CoherentPoolTotal=%u\r\n", (unsigned)poolTotal);
    scpi_printf(context, "CoherentPoolFree=%u\r\n", (unsigned)poolFree);
    scpi_printf(context, "SdCircularSize=%u\r\n",
                (unsigned)StreamingBufferPool_SdCircularSize());
    size_t elemSize = AInSampleList_PoolElementSize();
    scpi_printf(context, "SamplePoolCount=%u\r\n", (unsigned)samplePoolCap);
    scpi_printf(context, "SampleElementBytes=%u\r\n", (unsigned)elemSize);
    scpi_printf(context, "SamplePoolBytes=%u\r\n",
                (unsigned)(samplePoolCap * elemSize));
    scpi_printf(context, "SampleNextFreeBytes=%u\r\n",
                (unsigned)(samplePoolCap * sizeof(int16_t)));
    scpi_printf(context, "SampleQueueBytes=%u\r\n",
                (unsigned)(samplePoolCap * sizeof(void*) + 80));
    scpi_printf(context, "SamplePoolInUse=%u\r\n",
                (unsigned)AInSampleList_PoolInUse());
    scpi_printf(context, "SamplePoolMaxUsed=%u\r\n",
                (unsigned)AInSampleList_PoolMaxUsed());
    return SCPI_RES_OK;
}

// #520: shared streaming-buffer prepare — auto-balance partition of the
// unified pool + DMA coherent-pool re-alloc + sample-pool init.  Mirrors the
// setup SCPI_StartStreaming/SCPI_MemAutoBalance do; factored out so the WiFi
// throughput finder can establish a valid partition standalone.  Without this,
// the finder's (and SCPI_RunThroughputBench's) cfg-poke start path leaves the
// pipeline with no WiFi/encoder buffers, so the encoder produces 0 bytes
// (root cause found on HW 2026-05-31).  poolCount/sampleElemSize: 0/0 = generic
// 16ch default; or the real values after Streaming_BuildChannelMapping.
// Returns false on partition/quiesce failure (caller pushes the SCPI error).
// NOTE: duplicates SCPI_MemAutoBalance's body for now — DRY once verified.
static bool PrepareStreamingBuffers(uint32_t poolCount, size_t sampleElemSize) {
    uint32_t usbSize, wifiSize, sdCircSize;
    uint32_t sdDmaSize, usbDmaSize, wifiDmaSize, encSize;

    // Mirror SCPI_StartStreaming's auto-vs-explicit decision so the finder
    // measures with the SAME buffer layout the user's real stream will use:
    // if any MemoryConfig field is set (SYST:MEM:* static config), honor those
    // sizes; only auto-balance when fully in auto mode.  (SCPI_MemAutoBalance
    // forces auto by zeroing mc first — this helper does not, so it respects
    // a user's static buffers.)
    MemoryConfig* mc = BoardRunTimeConfig_Get(BOARDRUNTIME_MEMORY_CONFIG);
    bool isAutoMode = (mc->sdCircularBufSize == 0 &&
                       mc->wifiCircularBufSize == 0 &&
                       mc->usbCircularBufSize == 0 &&
                       mc->encoderBufSize == 0 &&
                       mc->samplePoolCount == 0);
    if (isAutoMode) {
        Streaming_ComputeAutoBuffers(&usbSize, &wifiSize, &sdCircSize,
                                     &sdDmaSize, &usbDmaSize, &wifiDmaSize, &encSize);
    } else {
        usbSize   = mc->usbCircularBufSize ? mc->usbCircularBufSize : USBCDC_CIRCULAR_BUFF_SIZE;
        wifiSize  = mc->wifiCircularBufSize ? mc->wifiCircularBufSize : WIFI_CIRCULAR_BUFF_SIZE;
        sdCircSize = mc->sdCircularBufSize ? mc->sdCircularBufSize : SD_CARD_MANAGER_DEFAULT_CIRCULAR_SIZE;
        sdDmaSize  = SD_CARD_MANAGER_CONF_WBUFFER_SIZE;
        usbDmaSize = USBCDC_DMA_WBUFFER_MAX;
        wifiDmaSize = WIFI_DMA_MAX;
        encSize    = mc->encoderBufSize ? mc->encoderBufSize : ENCODER_BUFFER_DEFAULT;
    }

    // Wait for any in-flight USB DMA write before swapping buffers; ABORT on
    // timeout rather than proceeding (#486) — swapping the write buffer while
    // a DMA transfer is live would race the SetWriteBuffer pointer.
    UsbCdcData_t* pUsb = UsbCdc_GetSettings();
    TickType_t t = xTaskGetTickCount();
    while (pUsb->writeTransferHandle != USB_DEVICE_CDC_TRANSFER_HANDLE_INVALID) {
        if ((xTaskGetTickCount() - t) > pdMS_TO_TICKS(1000)) return false;
        vTaskDelay(1);
    }

    // Wait for streaming_Task (pri 6) + deferred ISR task (pri 9) to exit their
    // resource-touching regions before re-partitioning, so the buffer-pointer
    // swap below can't be torn-read by a task mid-iteration (#486).  ABORT on
    // timeout.  No-op for idle callers (tasks already quiescent); load-bearing
    // when StartStreaming re-partitions a just-stopped active stream.
    {
        TickType_t qStart = xTaskGetTickCount();
        while (!Streaming_TasksAreQuiescent()) {
            if ((xTaskGetTickCount() - qStart) > pdMS_TO_TICKS(100)) return false;
            vTaskDelay(1);
        }
    }

    StreamingBufferPool_Partition(usbSize, wifiSize, encSize,
                                  sdCircSize, poolCount, sampleElemSize);

    uint8_t *usbBuf, *wifiBuf, *encBuf, *sdCircBuf;
    uint32_t usbLen, wifiLen, encLen, sdCircLen;
    StreamingBufferPool_GetUsb(&usbBuf, &usbLen);
    StreamingBufferPool_GetWifi(&wifiBuf, &wifiLen);
    StreamingBufferPool_GetEncoder(&encBuf, &encLen);
    StreamingBufferPool_GetSdCircular(&sdCircBuf, &sdCircLen);
    if (usbBuf == NULL || wifiBuf == NULL || encBuf == NULL || sdCircBuf == NULL) {
        LOG_E("PrepareStreamingBuffers: partition failed USB=%u WiFi=%u enc=%u sd=%u",
              (unsigned)usbLen, (unsigned)wifiLen, (unsigned)encLen, (unsigned)sdCircLen);
        return false;
    }
    UsbCdc_SetWriteBuffer(usbBuf, usbLen);
    wifi_tcp_server_SetWriteBuffer(wifiBuf, wifiLen);
    Streaming_SetEncoderBuffer(encBuf, encLen);
    sd_card_manager_SetCircularBuffer(sdCircBuf, sdCircLen);

    sdDmaSize &= ~(511U);
    if (sdDmaSize < 512) sdDmaSize = 512;
    sdDmaSize &= ~(COHERENT_POOL_ALIGNMENT - 1);
    usbDmaSize &= ~(COHERENT_POOL_ALIGNMENT - 1);
    wifiDmaSize &= ~(COHERENT_POOL_ALIGNMENT - 1);
    uint32_t totalDma = sdDmaSize + usbDmaSize + wifiDmaSize + 3 * COHERENT_POOL_ALIGNMENT;
    if (totalDma > CoherentPool_TotalSize()) {
        sdDmaSize = SD_CARD_MANAGER_MIN_WBUFFER_SIZE;
        usbDmaSize = USBCDC_DMA_WBUFFER_MIN;
        wifiDmaSize = WIFI_DMA_MIN;
    }
    if (!SCPI_QuiesceAndResetCoherentPool()) {
        return false;
    }
    uint8_t* sdDmaBuf = CoherentPool_Alloc("SD_write", sdDmaSize);
    if (sdDmaBuf == NULL) { LOG_E("CoherentPool alloc failed: SD_write (%u)", (unsigned)sdDmaSize); }
    else { sd_card_manager_SetWriteBuffer(sdDmaBuf, sdDmaSize); }
    uint8_t* usbDmaBuf = CoherentPool_Alloc("USB_write", usbDmaSize);
    if (usbDmaBuf == NULL) { LOG_E("CoherentPool alloc failed: USB_write (%u)", (unsigned)usbDmaSize); }
    else { UsbCdc_SetDmaWriteBuffer(usbDmaBuf, usbDmaSize); }
    uint8_t* wifiDmaBuf = CoherentPool_Alloc("WiFi_SPI", wifiDmaSize);
    if (wifiDmaBuf == NULL) { LOG_E("CoherentPool alloc failed: WiFi_SPI (%u)", (unsigned)wifiDmaSize); }
    else { WDRV_WINC_SPI_SetBuffer(wifiDmaBuf, wifiDmaSize); }

    void* sPoolMem; int16_t* sFreeMem; uint32_t sCount; size_t sElemSz;
    StreamingBufferPool_GetSamplePool(&sPoolMem, &sFreeMem, &sCount, &sElemSz);
    AInSampleList_InitializeExternal(sPoolMem, sFreeMem, sCount, sElemSz);
    return true;
}

static scpi_result_t SCPI_MemAutoBalance(scpi_t * context) {
    if (SCPI_MemRejectIfStreaming(context)) return SCPI_RES_ERR;
    MemoryConfig* mc = BoardRunTimeConfig_Get(BOARDRUNTIME_MEMORY_CONFIG);

    // Force auto mode by zeroing mc BEFORE partitioning, so the shared helper
    // computes auto sizes regardless of any prior static config — SYST:MEM:AUTO
    // is the "balance it for me" command — and leaves mc at zero so subsequent
    // StartStreamData stays in auto mode.  poolCount/elemSize 0/0 = generic
    // 16ch default (actual channel count isn't known until StartStreamData).
    memset(mc, 0, sizeof(MemoryConfig));
    if (!PrepareStreamingBuffers(0, 0)) {
        SCPI_ExecutionError(context, "SYST:MEM:AUTO: buffer partition failed");
        return SCPI_RES_ERR;
    }
    // SCPI compliance: commands (no ?) don't return data. Use SYST:MEM:FREE?.
    return SCPI_RES_OK;
}

static scpi_result_t SCPI_MemReset(scpi_t * context) {
    if (SCPI_MemRejectIfStreaming(context)) return SCPI_RES_ERR;
    MemoryConfig* mc = BoardRunTimeConfig_Get(BOARDRUNTIME_MEMORY_CONFIG);
    memset(mc, 0, sizeof(MemoryConfig));
    return SCPI_RES_OK;
}

// =============================================================================
// Stack Profiling SCPI Callback
// =============================================================================

static scpi_result_t SCPI_GetStackStats(scpi_t * context) {
    // uxTaskGetSystemState requires configUSE_TRACE_FACILITY = 1
    UBaseType_t taskCount = uxTaskGetNumberOfTasks();
    TaskStatus_t* taskStatusArray = (TaskStatus_t*)pvPortMalloc((taskCount + 2) * sizeof(TaskStatus_t));
    if (taskStatusArray == NULL) {
        LOG_E("SYST:MEM:STACk? malloc failed for %u tasks", (unsigned)taskCount);
        SCPI_ErrorPush(context, SCPI_ERROR_SYSTEM_ERROR);
        return SCPI_RES_ERR;
    }

    UBaseType_t actualCount = uxTaskGetSystemState(taskStatusArray, taskCount, NULL);

    for (UBaseType_t i = 0; i < actualCount; i++) {
        // usStackHighWaterMark = minimum free stack WORDS ever (on PIC32MZ, 1 word = 4 bytes)
        UBaseType_t hwm = taskStatusArray[i].usStackHighWaterMark;

        scpi_printf(context, "%s: prio=%u, hwm=%u words (%u bytes free)\r\n",
                    taskStatusArray[i].pcTaskName,
                    (unsigned)taskStatusArray[i].uxCurrentPriority,
                    (unsigned)hwm,
                    (unsigned)(hwm * sizeof(StackType_t)));
    }

    // Also report ISR stack (not tracked by FreeRTOS — would need manual measurement)
    scpi_printf(context, "ISR_Stack: configured=8192 bytes (not profiled)\r\n");

    vPortFree(taskStatusArray);
    return SCPI_RES_OK;
}

// =============================================================================
// DIO Debug Probe SCPI Callbacks
// =============================================================================

static bool dioprobe_parse_mode(const char* str, size_t len, DioProbeMode_t* out) {
    if (len >= 3 && (str[0] == 'O' || str[0] == 'o') &&
                    (str[1] == 'F' || str[1] == 'f') &&
                    (str[2] == 'F' || str[2] == 'f')) {
        *out = DIO_PROBE_MODE_OFF;
        return true;
    }
    if (len >= 3 && (str[0] == 'T' || str[0] == 't') &&
                    (str[1] == 'O' || str[1] == 'o') &&
                    (str[2] == 'G' || str[2] == 'g')) {
        *out = DIO_PROBE_MODE_TOGGLE;
        return true;
    }
    if (len >= 3 && (str[0] == 'P' || str[0] == 'p') &&
                    (str[1] == 'U' || str[1] == 'u') &&
                    (str[2] == 'L' || str[2] == 'l')) {
        *out = DIO_PROBE_MODE_PULSE;
        return true;
    }
    return false;
}

static const char* dioprobe_mode_name(uint8_t mode) {
    switch (mode) {
        case DIO_PROBE_MODE_TOGGLE: return "TOGGLE";
        case DIO_PROBE_MODE_PULSE:  return "PULSE";
        default:                    return "OFF";
    }
}

static scpi_result_t SCPI_DioProbeModeSet(scpi_t * context) {
    int32_t probeId;
    if (!SCPI_ParamInt32(context, &probeId, TRUE)) return SCPI_RES_ERR;
    if (probeId < 0 || probeId >= DIO_PROBE_STANDARD_COUNT) {
        SCPI_ErrorPush(context, SCPI_ERROR_ILLEGAL_PARAMETER_VALUE);
        return SCPI_RES_ERR;
    }
    const char* modeStr;
    size_t modeLen;
    if (!SCPI_ParamCharacters(context, &modeStr, &modeLen, TRUE)) return SCPI_RES_ERR;
    DioProbeMode_t mode;
    if (!dioprobe_parse_mode(modeStr, modeLen, &mode)) {
        SCPI_ErrorPush(context, SCPI_ERROR_ILLEGAL_PARAMETER_VALUE);
        return SCPI_RES_ERR;
    }
    if (!DioProbe_Assign((uint8_t)probeId, mode)) {
        SCPI_ExecutionError(context, "SYST:DIOP:MODE: probe assign failed (channel owned?)");
        return SCPI_RES_ERR;
    }
    return SCPI_RES_OK;
}

static scpi_result_t SCPI_DioProbeModeGet(scpi_t * context) {
    int32_t probeId;
    if (!SCPI_ParamInt32(context, &probeId, TRUE)) return SCPI_RES_ERR;
    if (probeId < 0 || probeId >= DIO_PROBE_SLOTS) {
        SCPI_ErrorPush(context, SCPI_ERROR_ILLEGAL_PARAMETER_VALUE);
        return SCPI_RES_ERR;
    }
    DioProbeSlot_t s;
    DioProbe_GetSlot((uint8_t)probeId, &s);
    SCPI_ResultText(context, dioprobe_mode_name(s.mode));
    return SCPI_RES_OK;
}

static scpi_result_t SCPI_DioProbeClear(scpi_t * context) {
    int32_t probeId;
    if (!SCPI_ParamInt32(context, &probeId, TRUE)) return SCPI_RES_ERR;
    if (probeId < 0 || probeId >= DIO_PROBE_STANDARD_COUNT) {
        SCPI_ErrorPush(context, SCPI_ERROR_ILLEGAL_PARAMETER_VALUE);
        return SCPI_RES_ERR;
    }
    DioProbe_Clear((uint8_t)probeId);
    return SCPI_RES_OK;
}

static scpi_result_t SCPI_DioProbeClearAll(scpi_t * context) {
    (void)context;
    DioProbe_ClearAll();
    return SCPI_RES_OK;
}

static scpi_result_t SCPI_DioProbePipeline(scpi_t * context) {
    const char* modeStr;
    size_t modeLen;
    if (!SCPI_ParamCharacters(context, &modeStr, &modeLen, TRUE)) return SCPI_RES_ERR;
    DioProbeMode_t mode;
    if (!dioprobe_parse_mode(modeStr, modeLen, &mode)) {
        SCPI_ErrorPush(context, SCPI_ERROR_ILLEGAL_PARAMETER_VALUE);
        return SCPI_RES_ERR;
    }
    DioProbe_SetPipeline(mode);
    return SCPI_RES_OK;
}

static scpi_result_t SCPI_DioProbeMap(scpi_t * context) {
    int32_t probeId, channel;
    if (!SCPI_ParamInt32(context, &probeId, TRUE)) return SCPI_RES_ERR;
    if (probeId < 0 || probeId >= DIO_PROBE_SLOTS) {
        SCPI_ErrorPush(context, SCPI_ERROR_ILLEGAL_PARAMETER_VALUE);
        return SCPI_RES_ERR;
    }
    if (!SCPI_ParamInt32(context, &channel, TRUE)) return SCPI_RES_ERR;
    if (channel < 0 || channel > DIO_PROBE_MAX_DIO_CHANNEL) {
        SCPI_ErrorPush(context, SCPI_ERROR_ILLEGAL_PARAMETER_VALUE);
        return SCPI_RES_ERR;
    }
    const char* modeStr;
    size_t modeLen;
    if (!SCPI_ParamCharacters(context, &modeStr, &modeLen, TRUE)) return SCPI_RES_ERR;
    DioProbeMode_t mode;
    if (!dioprobe_parse_mode(modeStr, modeLen, &mode)) {
        SCPI_ErrorPush(context, SCPI_ERROR_ILLEGAL_PARAMETER_VALUE);
        return SCPI_RES_ERR;
    }
    if (!DioProbe_AssignToChannel((uint8_t)probeId, (uint8_t)channel, mode)) {
        SCPI_ExecutionError(context, "SYST:DIOP:ROUT: assign failed (PWM active or channel owned?)");
        return SCPI_RES_ERR;
    }
    return SCPI_RES_OK;
}

static scpi_result_t SCPI_DioProbeList(scpi_t * context) {
    for (uint8_t i = 0; i < DIO_PROBE_SLOTS; ++i) {
        DioProbeSlot_t s;
        DioProbe_GetSlot(i, &s);
        const char* compile = (i >= DIO_PROBE_ADHOC_FIRST) ? "yes" : "no";
        unsigned ch = (s.channel == 0xFF) ? 0xFFu : s.channel;
        scpi_printf(context, "probe %u: channel=%u mode=%s compile_time=%s\r\n",
                    (unsigned)i, ch, dioprobe_mode_name(s.mode), compile);
    }
    return SCPI_RES_OK;
}

static scpi_result_t SCPI_WincGateQ(scpi_t * context) {
    int status = 0;
    bool streaming_non_wifi = false;
    bool tcp_client = false;
    uint32_t delay_ms = 0;
    WincIdleGate_GetDebugState(&status, &streaming_non_wifi, &tcp_client, &delay_ms);
    const char* name;
    switch ((SYS_STATUS)status) {
        case SYS_STATUS_UNINITIALIZED: name = "UNINITIALIZED"; break;
        case SYS_STATUS_BUSY:          name = "BUSY";          break;
        case SYS_STATUS_READY:         name = "READY";         break;
        case SYS_STATUS_ERROR:         name = "ERROR";         break;
        default:                       name = "UNKNOWN";       break;
    }
    scpi_printf(context, "Status=%s\r\n", name);
    scpi_printf(context, "StatusValue=%d\r\n", status);
    scpi_printf(context, "StreamingNonWifi=%u\r\n", (unsigned)streaming_non_wifi);
    scpi_printf(context, "TcpClient=%u\r\n", (unsigned)tcp_client);
    scpi_printf(context, "ComputedDelayMs=%u\r\n", (unsigned)delay_ms);
    return SCPI_RES_OK;
}

/*
 * CONFigure:CAPabilities:APIVersion?
 * Returns the capability schema version byte so clients can dispatch
 * to the right parser before issuing any other capability query. See
 * Capabilities.h (DAQIFI_CAPABILITIES_VERSION) and issue #327.
 */
static scpi_result_t SCPI_CapabilitiesApiVersionGet(scpi_t * context) {
    SCPI_ResultUInt32(context, (uint32_t)DAQIFI_CAPABILITIES_VERSION);
    return SCPI_RES_OK;
}

/* -------- CONFigure:CAPabilities:JSON? emitter helpers -----------
 * Each helper emits one channel object with the schema shape
 *   { "id", "kind", "signal_type", ... }
 * Kept file-scope (not in Capabilities.c) because they're coupled
 * to the libscpi transport via scpi_printf. */

/* Design principle (see #327): emit client-actionable facts only.
 * No IC names, no module internals, no formula constants — just
 * "what can the client DO with this channel/pin/transport?" An
 * "extensions":{} escape hatch on each object reserves space for
 * vendor-specific or future fields that older clients MUST ignore.
 *
 * Chunked to stay under the 192-byte scpi_printf buffer per call. */

static void EmitAinChannelJson(scpi_t* context,
                               const AInChannel* ch,
                               const AInRuntimeConfig* rt,
                               double moduleRangeSpan) {
    uint8_t id = ch->DaqifiAdcChannelId;
    bool    isTemperature     = false;
    bool    allowDifferential = false;
    bool    simultaneous      = false;   /* true = dedicated ADC, zero inter-channel skew */
    uint8_t resolutionBits    = 12;

    if (ch->Type == AIn_MC12bADC) {
        isTemperature     = ch->Config.MC12b.IsTemperatureSensor;
        allowDifferential = ch->Config.MC12b.AllowDifferential;
        simultaneous      = (ch->Config.MC12b.ChannelType == 1);
        resolutionBits    = 12;
    } else if (ch->Type == AIn_AD7609) {
        simultaneous   = true;   /* AD7609 converts all 8 channels together */
        resolutionBits = 18;
    }

    /* Terminal range depends on ADC type:
     *   - MC12b (NQ1): single-ended unipolar input, 0..span
     *   - AD7609 (NQ2/NQ3): true-differential bipolar input, ±(span/2)
     * Clients that need exact raw→volts use calibration.slope +
     * calibration.intercept regardless. */
    double rangeMin, rangeMax;
    if (ch->Type == AIn_MC12bADC) {
        rangeMin = 0.0;
        rangeMax = moduleRangeSpan;
    } else {
        rangeMin = -moduleRangeSpan / 2.0;
        rangeMax =  moduleRangeSpan / 2.0;
    }

    scpi_printf(context,
        "{\"id\":%u,\"kind\":\"analog-input\","
        "\"signal_type\":\"%s\",\"unit\":\"%s\","
        "\"resolution_bits\":%u,",
        (unsigned)id,
        isTemperature ? "temperature" : "voltage",
        isTemperature ? "Cel"         : "V",
        (unsigned)resolutionBits);

    scpi_printf(context,
        "\"simultaneous\":%s,\"differential\":%s,"
        "\"ranges\":[{\"min\":%.3f,\"max\":%.3f}],",
        simultaneous      ? "true" : "false",
        allowDifferential ? "true" : "false",
        rangeMin, rangeMax);

    scpi_printf(context,
        "\"calibration\":{\"model\":\"linear\","
        "\"user_override_supported\":true,"
        "\"slope\":%.6f,\"intercept\":%.6f},"
        "\"extensions\":{}}",
        rt->CalM, rt->CalB);
}

static void EmitAoutChannelJson(scpi_t* context,
                                const AOutChannel* ch,
                                const CapabilitiesAoutSummary* ao) {
    scpi_printf(context,
        "{\"id\":%u,\"kind\":\"analog-output\","
        "\"signal_type\":\"voltage\",\"unit\":\"V\","
        "\"resolution_bits\":%u,",
        (unsigned)ch->DaqifiDacChannelId,
        (unsigned)ao->resolutionBits);

    scpi_printf(context,
        "\"ranges\":[{\"min\":%.3f,\"max\":%.3f}],"
        "\"calibration\":{\"model\":\"linear\","
        "\"user_override_supported\":true},"
        "\"extensions\":{}}",
        ao->moduleMinVoltage, ao->moduleMaxVoltage);
}

static void EmitDioChannelJson(scpi_t* context,
                               const DIOConfig* dio,
                               uint8_t channelIndex) {
    /* features{} only contains keys for features the pin actually
     * supports. Absent key = unavailable. Forward-compat:
     *   - new feature kinds (spi, uart, i2c, counter, trigger, …)
     *     just add a new key here and clients that know the key
     *     pick it up; older clients ignore it.
     *   - richer settings for existing features (e.g. PWM dead-time)
     *     go under the feature's own object.
     * "extensions":{} is the catch-all for vendor-specific keys. */
    scpi_printf(context,
        "{\"id\":%u,\"kind\":\"digital-io\","
        "\"features\":{\"input\":true,\"output\":true%s",
        (unsigned)channelIndex,
        dio->IsPwmCapable ? "," : "},\"extensions\":{}}");

    if (dio->IsPwmCapable) {
        /* PWM freq/res reflect OCMP HAL support
         * (HAL/DIO.c:DIO_PWMFrequencySet, 16-bit OCMP timer). */
        scpi_printf(context,
            "\"pwm\":{\"min_freq_hz\":1,\"max_freq_hz\":50000,"
            "\"resolution_bits\":16}},\"extensions\":{}}");
    }
}

/*
 * CONFigure:CAPabilities:JSON?
 * Emits the unified V1 capability schema (see #327 and
 * Capabilities.h) for the current board. Includes identity,
 * per-channel capabilities, streaming, storage, power, transports,
 * and triggers blocks — enough that a client can render its UI
 * from the single query without hard-coding board variants.
 *
 * The schema version byte is DAQIFI_CAPABILITIES_VERSION; bump it
 * in Capabilities.h on breaking changes (see history comment
 * there for the evolution rules).
 *
 * Emission uses scpi_printf in chunks — each call writes directly
 * to the libscpi transport via interface->write. The 192-byte
 * per-call buffer is never exceeded because the helpers above
 * split each channel object across multiple calls.
 *
 * INVARIANT: boardFirmwareRev and boardHardwareRev come from
 * FIRMWARE_REVISION / HARDWARE_REVISION macros in version.h and
 * are version strings like "3.4.6b1" — no characters that need
 * JSON escaping (no `"`, `\`, or control chars). If a future
 * revision scheme introduces such characters, add a small JSON
 * string escaper before embedding these fields.
 */
static scpi_result_t SCPI_CapabilitiesJsonGet(scpi_t * context) {
    const tBoardConfig* cfg = BoardConfig_Get(BOARDCONFIG_ALL_CONFIG, 0);
    const tBoardRuntimeConfig* rt =
        (const tBoardRuntimeConfig*)BoardRunTimeConfig_Get(
            BOARDRUNTIMECONFIG_ALL_CONFIG);

    CapabilitiesAinSummary        ai;
    CapabilitiesAoutSummary       ao;
    CapabilitiesDioSummary        di;
    CapabilitiesStreamingSummary  st;
    CapabilitiesStorageSummary    stor;
    CapabilitiesPowerSummary      pw;
    CapabilitiesTransportsSummary tr;
    Capabilities_GetAinSummary(&ai);
    Capabilities_GetAoutSummary(&ao);
    Capabilities_GetDioSummary(&di);
    Capabilities_GetStreamingSummary(&st);
    Capabilities_GetStorageSummary(&stor);
    Capabilities_GetPowerSummary(&pw);
    Capabilities_GetTransportsSummary(&tr);

    const char* variantName =
        (cfg->BoardVariant == 1) ? "NQ1" :
        (cfg->BoardVariant == 2) ? "NQ2" :
        (cfg->BoardVariant == 3) ? "NQ3" : "UNK";

    /* Each scpi_printf call is capped at 192 bytes by the helper's
     * internal buffer (see SCPIInterface.h). Chunks below are kept
     * well under that so there's headroom for longer firmware_rev
     * strings, larger variant codes, etc. */

    /* ---- Schema header + extensions escape hatch ----
     * "extensions":{} is a reserved object at every level clients
     * parse. Older clients MUST ignore unknown keys inside it; new
     * keys can be added without bumping schema_version. Breaking
     * changes (renames, removals, retypes) still require a
     * schema_version bump. */
    scpi_printf(context,
        "{\"schema_version\":%u,"
        "\"schema_uri\":\"https://daqifi.com/schemas/capability/v1\","
        "\"extensions\":{},",
        (unsigned)DAQIFI_CAPABILITIES_VERSION);

    /* ---- identity ----
     * Vendor/model/variant strings identify the product. USB VID/PID
     * live here (not transports) because they're device-identity
     * information the host uses for enumeration / driver matching,
     * not a configurable transport setting. */
    scpi_printf(context,
        "\"identity\":{\"vendor\":\"DAQiFi\",\"model\":\"Nyquist\","
        "\"variant\":\"%s\",",
        variantName);

    scpi_printf(context,
        "\"serial\":\"%llX\","
        "\"firmware_rev\":\"%s\",\"hardware_rev\":\"%s\",",
        (unsigned long long)cfg->boardSerialNumber,
        cfg->boardFirmwareRev,
        cfg->boardHardwareRev);

    scpi_printf(context,
        "\"usb\":{\"vid\":%u,\"pid\":%u,\"class\":\"CDC\"}},",
        (unsigned)0x04D8,   /* Microchip VID (matches usb_device_init_data) */
        (unsigned)0xF794);  /* DAQiFi Nyquist PID */

    /* ---- channels[] array (flat, kind-grouped) ----
     * Order: all analog-input first (sorted by DaqifiAdcChannelId
     * via board-config array order), then analog-output (by
     * DaqifiDacChannelId), then digital-io (by index). See
     * plan doc for the grouping contract. */
    scpi_printf(context, "\"channels\":[");
    bool firstEntry = true;

    /* AIn — public only. Bound the loop by the smaller of the
     * two arrays so a runtime-config shape mismatch can't walk
     * past the end of rt->AInChannels.Data[]. In practice both
     * arrays have MAX_AIN_CHANNEL (48) capacity and matching Size,
     * but defending the read is cheap. */
    uint32_t ainLoopCount = (cfg->AInChannels.Size < rt->AInChannels.Size)
        ? cfg->AInChannels.Size : rt->AInChannels.Size;
    for (uint32_t i = 0; i < ainLoopCount; i++) {
        const AInChannel* ch = &cfg->AInChannels.Data[i];
        bool isPublic =
            (ch->Type == AIn_MC12bADC && ch->Config.MC12b.IsPublic) ||
            (ch->Type == AIn_AD7609   && ch->Config.AD7609.IsPublic);
        if (!isPublic) continue;

        /* Module range feeds per-channel range emission. MC12b
         * channels belong to the single MC12b module (index 0 by
         * board-config convention); AD7609 is a separate module. */
        uint32_t modIdx = 0;
        for (uint32_t m = 0; m < cfg->AInModules.Size; m++) {
            if ((cfg->AInModules.Data[m].Type == ch->Type)) {
                modIdx = m;
                break;
            }
        }
        double moduleRange = (modIdx < rt->AInModules.Size)
            ? rt->AInModules.Data[modIdx].Range : 0.0;

        if (!firstEntry) scpi_printf(context, ",");
        firstEntry = false;

        const AInRuntimeConfig* rc = &rt->AInChannels.Data[i];
        EmitAinChannelJson(context, ch, rc, moduleRange);
    }

    /* AOut — always emitted, just empty on boards without a DAC */
    for (uint32_t i = 0; i < cfg->AOutChannels.Size; i++) {
        if (!firstEntry) scpi_printf(context, ",");
        firstEntry = false;
        EmitAoutChannelJson(context, &cfg->AOutChannels.Data[i], &ao);
    }

    /* DIO — emitted in board-config array order, which equals
     * user-facing channel number by the DIOConfig invariant
     * documented in Capabilities.c. */
    for (uint32_t i = 0; i < cfg->DIOChannels.Size; i++) {
        if (!firstEntry) scpi_printf(context, ",");
        firstEntry = false;
        EmitDioChannelJson(context, &cfg->DIOChannels.Data[i], (uint8_t)i);
    }

    scpi_printf(context, "],");

    /* ---- streaming ----
     * Client-actionable facts: what encodings, which transports,
     * what sample rates, and — importantly — a predictive rate_model
     * so a client UI can preview "if the user enables this set of
     * channels, the max rate will be X Hz" without round-tripping
     * for every checkbox change. */
    scpi_printf(context,
        "\"streaming\":{\"encodings\":[\"pb\",\"csv\",\"json\"],"
        "\"transports\":[");
    {
        bool first = true;
        if (tr.usbSupported)  { scpi_printf(context, "\"usb\"");                     first = false; }
        if (tr.wifiSupported) { scpi_printf(context, first ? "\"wifi\"" : ",\"wifi\""); first = false; }
        if (stor.sdSupported) { scpi_printf(context, first ? "\"sd\""   : ",\"sd\"");                 }
    }

    /* Three rate tiers, each a different contract (see issue #344):
     *
     * - sample_rate_range_hz.max: the absolute ISR ceiling — hardware
     *   envelope, never achievable under real load.
     *
     * - conservative_envelope_hz: rate guaranteed zero-drop regardless
     *   of how the client configures channels / interface / encoder /
     *   DIO / OBDiag. Measured via the worst-case test in #344. A
     *   client that always picks this rate is never surprised.
     *
     * - current_max_rate_hz: device's authoritative cap for the
     *   channel set enabled right now. Today only reflects channel
     *   count/type; future work (#344 Phase 3) extends it to apply
     *   interface / encoder / DIO caps too.
     *
     * - rate_model: client-side formula for optimistic previews
     *   across hypothetical channel groupings. Channel-count only —
     *   real max for a committed configuration may be lower. Count
     *   channels[] entries with kind=="analog-input" and
     *   simultaneous==true to get simultaneous_count; count all
     *   enabled kind=="analog-input" to get total_count. DIO / AOut
     *   do not factor (DIO is amortized in per_tick_overhead; AOut
     *   is not streamed).
     *
     * rate_validation documents over-ask handling. Today: silent_cap
     * (firmware lowers to current_max_rate_hz, logs LOG_I). Clients
     * needing explicit feedback MUST pre-validate against
     * current_max_rate_hz before SYSTem:StartStreamData. */
    scpi_printf(context,
        "],\"sample_rate_range_hz\":{\"min\":1,\"max\":%u},"
        "\"conservative_envelope_hz\":%u,"
        "\"current_max_rate_hz\":%u,",
        (unsigned)st.isrMaxHz,
        (unsigned)cfg->CapabilitiesFlags.streamingConservativeEnvelopeHz,
        (unsigned)st.maxFreqHz);

    scpi_printf(context,
        "\"rate_model\":{\"formula\":"
        "\"min(absolute_max_hz,"
        " type1_aggregate_max_hz/simultaneous_count,"
        " per_tick_budget_hz/(per_tick_overhead+total_count))\",");

    scpi_printf(context,
        "\"absolute_max_hz\":%u,\"type1_aggregate_max_hz\":%u,"
        "\"per_tick_budget_hz\":%u,\"per_tick_overhead\":%u},",
        (unsigned)st.isrMaxHz,
        (unsigned)st.type1AggMaxHz,
        (unsigned)st.tickBudget,
        (unsigned)st.tickOverhead);

    scpi_printf(context, "\"rate_validation\":\"silent_cap\",");

    scpi_printf(context,
        "\"buffer_ranges_bytes\":{"
        "\"usb\":{\"min\":2048,\"max\":65536,\"default\":16384},"
        "\"wifi\":{\"min\":1400,\"max\":65536,\"default\":14000},");

    scpi_printf(context,
        "\"sd\":{\"min\":4096,\"max\":65536,\"default\":32768},"
        "\"encoder\":{\"min\":1024,\"max\":65536,\"default\":8192},"
        "\"sample_pool\":{\"min\":100,\"max\":2000,\"default\":1100}},");

    scpi_printf(context,
        "\"test_patterns\":[0,1,2,3,4,5,6],\"extensions\":{}},");

    /* ---- storage ----
     * Client cares about: can I write files, what filesystems do
     * you support, and what's the max file size I can expect
     * before the auto-split kicks in. */
    scpi_printf(context,
        "\"storage\":{\"sd_supported\":%s,"
        "\"filesystems\":[\"FAT32\"],"
        "\"max_file_size_bytes\":%llu,"
        "\"extensions\":{}},",
        stor.sdSupported ? "true" : "false",
        (unsigned long long)SD_CARD_MANAGER_FAT32_SAFE_MAX_FILE_SIZE);

    /* ---- power ----
     * Client cares about: where does this device take power from,
     * does it have a battery I should monitor, can it source 5V
     * (OTG) to peripherals. Charger IC, IIN-limit rungs, and
     * internal power-state enum values are firmware/datasheet
     * facts, not client-actionable — dropped. */
    scpi_printf(context, "\"power\":{\"sources\":[");
    {
        bool first = true;
        if (tr.usbSupported)           { scpi_printf(context, "\"usb\"");                               first = false; }
        if (pw.externalPowerSupported) { scpi_printf(context, first ? "\"external\"" : ",\"external\""); first = false; }
        if (pw.batteryPresent)         { scpi_printf(context, first ? "\"battery\""  : ",\"battery\"");                }
    }
    scpi_printf(context,
        "],\"battery_present\":%s,"
        "\"external_power_supported\":%s,"
        "\"otg_output_supported\":%s,"
        "\"extensions\":{}},",
        pw.batteryPresent ? "true" : "false",
        pw.externalPowerSupported ? "true" : "false",
        pw.otgSupported ? "true" : "false");

    /* ---- transports ----
     * Client-actionable facts per channel: what bands / security
     * modes am I restricted to, what port do I connect my command
     * stream on, what UDP port do I send discovery broadcasts to.
     * Internal chipset names and USB class (in identity.usb) are
     * not relevant here. */
    scpi_printf(context,
        "\"transports\":{"
        "\"usb\":{\"supported\":%s,\"extensions\":{}},",
        tr.usbSupported ? "true" : "false");

    scpi_printf(context,
        "\"wifi\":{\"supported\":%s,"
        "\"bands\":[\"2.4GHz\"],"
        "\"modes\":[\"sta\",\"ap\"],",
        tr.wifiSupported ? "true" : "false");

    scpi_printf(context,
        "\"security\":[\"open\",\"wpa\",\"wpa2\"],"
        "\"tcp_command_port\":%u,"
        "\"udp_announce_port\":%u,"
        "\"extensions\":{}},",
        (unsigned)DEFAULT_TCP_PORT,
        (unsigned)WIFI_MANAGER_UDP_LISTEN_PORT);

    scpi_printf(context,
        "\"ethernet\":{\"supported\":%s,\"extensions\":{}},"
        "\"serial_debug\":{\"supported\":%s,\"baud\":921600,"
        "\"extensions\":{}}},",
        tr.ethernetSupported ? "true" : "false",
        tr.serialDebugSupported ? "true" : "false");

    /* ---- triggers (stub for V1; no HW trigger feature yet) ----
     * When HW triggers ship, hardware_inputs[] fills with objects
     * describing each input (pin, supported edges, debounce). */
    scpi_printf(context,
        "\"triggers\":{\"hardware_inputs\":[],\"software\":true,"
        "\"extensions\":{}}}"
        "\r\n");

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
    // *RST → SCPI_Reset directly. libscpi's SCPI_CoreRst dispatches to
    // interface->reset, which is NULL on both USB CDC and TCP server
    // scpi_interface_t structs — going through CoreRst would no-op.
    {.pattern = "*RST", .callback = SCPI_Reset,},
    {.pattern = "*SRE", .callback = SCPI_CoreSre,},
    {.pattern = "*SRE?", .callback = SCPI_CoreSreQ,},
    {.pattern = "*STB?", .callback = SCPI_CoreStbQ,},
    {.pattern = "*TST?", .callback = SCPI_CoreTstQ,},
    {.pattern = "*WAI", .callback = SCPI_CoreWai,},
    {.pattern = "SYSTem:ERRor?", .callback = SCPI_SystemErrorNextQ,},
    {.pattern = "SYSTem:ERRor:NEXT?", .callback = SCPI_SystemErrorNextQ,},
    {.pattern = "SYSTem:ERRor:COUNt?", .callback = SCPI_SystemErrorCountQ,},
    {.pattern = "SYSTem:VERSion?", .callback = SCPI_SystemVersionQ,},
    {.pattern = "STATus:QUEStionable?", .callback = SCPI_QuesEventQ,},
    {.pattern = "STATus:QUEStionable:EVENt?", .callback = SCPI_QuesEventQ,},
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
    {.pattern = "SYSTem:LOG:LEVel", .callback = SCPI_SysLogLevelSet,},
    {.pattern = "SYSTem:LOG:LEVel?", .callback = SCPI_SysLogLevelGet,},
    {.pattern = "SYSTem:LOG:LEVel:ALL", .callback = SCPI_SysLogLevelAllSet,},
    {.pattern = "SYSTem:LOG:CMDHistory?", .callback = SCPI_GetCommandHistory,},
    {.pattern = "SYSTem:ECHO", .callback = SCPI_SetEcho,},
    {.pattern = "SYSTem:ECHO?", .callback = SCPI_GetEcho,},
    //    {.pattern = "SYSTem:NVMRead?", .callback = SCPI_NVMRead, },  
    //    {.pattern = "SYSTem:NVMWrite", .callback = SCPI_NVMWrite, }, 
    //    {.pattern = "SYSTem:NVMErasePage", .callback = SCPI_NVMErasePage, },
    {.pattern = "SYSTem:FORceBoot", .callback = SCPI_ForceBootloader,},
    // USB transparent mode — new namespace form is canonical; legacy alias kept for back-compat (#311 round 3).
    {.pattern = "SYSTem:USB:TRANSparent:MODE", .callback = SCPI_UsbSetTransparentMode},
    {.pattern = "SYSTem:USB:SetTransparentMode", .callback = SCPI_UsbSetTransparentMode},
    {.pattern = "SYSTem:SERialNUMber?", .callback = SCPI_GetSerialNumber,},

    // Operation status registers (library-provided, returns 0 until firmware sets condition bits)
    // Per SCPI standard, bare "STATus:OPERation?" defaults to the Event register (clears on read).
    // See libscpi test_parser.c: pattern "STATus:OPERation[:EVENt]?" -> SCPI_StatusOperationEventQ
    {.pattern = "STATus:OPERation?", .callback = SCPI_StatusOperationEventQ,},
    {.pattern = "STATus:OPERation:EVENt?", .callback = SCPI_StatusOperationEventQ,},
    {.pattern = "STATus:OPERation:CONDition?", .callback = SCPI_StatusOperationConditionQ,},
    {.pattern = "STATus:OPERation:ENABle", .callback = SCPI_StatusOperationEnable,},
    {.pattern = "STATus:OPERation:ENABle?", .callback = SCPI_StatusOperationEnableQ,},
    // Questionable status condition (completes the set already registered above)
    {.pattern = "STATus:QUEStionable:CONDition?", .callback = SCPI_QuesConditionQ,},
    //    {.pattern = "SYSTem:COMMunication:TCPIP:CONTROL?", .callback = SCPI_NotImplemented, },

    // Power
    {.pattern = "SYSTem:POWer:SOURce?", .callback = SCPI_PowerSourceGet,},
    {.pattern = "SYSTem:BAT:LEVel?", .callback = SCPI_BatteryLevelGet,},
    {.pattern = "SYSTem:POWer:STATe?", .callback = SCPI_GetPowerState,},
    {.pattern = "SYSTem:POWer:STATe", .callback = SCPI_SetPowerState,},
    {.pattern = "SYSTem:POWer:AUTO:EXTernal?", .callback = SCPI_GetAutoExtPower,},
    {.pattern = "SYSTem:POWer:AUTO:EXTernal", .callback = SCPI_SetAutoExtPower,},
    {.pattern = "SYSTem:POWer:AUTOOn", .callback = SCPI_SetAutoPowerOnUsb,},      // #454
    {.pattern = "SYSTem:POWer:AUTOOn?", .callback = SCPI_GetAutoPowerOnUsb,},
    {.pattern = "SYSTem:POWer:AUTOOn:SAVE", .callback = SCPI_SaveAutoPowerOnUsb,},
    {.pattern = "SYSTem:POWer:AUTOOn:LOAD", .callback = SCPI_LoadAutoPowerOnUsb,},
    {.pattern = "SYSTem:POWer:BQ:REGisters?", .callback = SCPI_GetBQRegisters,},
    {.pattern = "SYSTem:POWer:BQ:ILIM", .callback = SCPI_SetBQILim,},
    {.pattern = "SYSTem:POWer:BQ:DPDM", .callback = SCPI_ForceDPDM,},
    {.pattern = "SYSTem:POWer:BQ:DIAGnostics?", .callback = SCPI_GetBQDiagnostics,},
    {.pattern = "SYSTem:POWer:BQ:FAULT:CLEar", .callback = SCPI_ClearBQFaults,},
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
    {.pattern = "SYSTem:COMMunicate:LAN:CONFigure:ADDRess?", .callback = SCPI_LANConfAddrGet,},
    {.pattern = "SYSTem:COMMunicate:LAN:MASK?", .callback = SCPI_LANMaskGet,},
    {.pattern = "SYSTem:COMMunicate:LAN:MASK", .callback = SCPI_LANMaskSet,},
    {.pattern = "SYSTem:COMMunicate:LAN:CONFigure:MASK?", .callback = SCPI_LANConfMaskGet,},
    {.pattern = "SYSTem:COMMunicate:LAN:GATEway?", .callback = SCPI_LANGatewayGet,},
    {.pattern = "SYSTem:COMMunicate:LAN:GATEway", .callback = SCPI_LANGatewaySet,},
    {.pattern = "SYSTem:COMMunicate:LAN:CONFigure:GATEway?", .callback = SCPI_LANConfGatewayGet,},
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
    {.pattern = "SYSTem:COMMunicate:LAN:HRESet", .callback = SCPI_LANHardReset,},   // hard reset WINC (#383 recovery)
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
    /* Capability framework — JSON? is the canonical source of truth.
     * APIVersion? is a fast pre-parse compat probe. See
     * Capabilities.h for the schema and evolution rules. */
    {.pattern = "CONFigure:CAPabilities:APIVersion?", .callback = SCPI_CapabilitiesApiVersionGet,},
    {.pattern = "CONFigure:CAPabilities:JSON?", .callback = SCPI_CapabilitiesJsonGet,},
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
    {.pattern = "CONFigure:ADC:OBDiag", .callback = SCPI_ADCOnboardDiagSet,},
    {.pattern = "CONFigure:ADC:OBDiag?", .callback = SCPI_ADCOnboardDiagGet,},
    {.pattern = "CONFigure:ADC:SAMC:DEDicated", .callback = SCPI_ADCSamcDedicatedSet,},
    {.pattern = "CONFigure:ADC:SAMC:DEDicated?", .callback = SCPI_ADCSamcDedicatedGet,},
    {.pattern = "CONFigure:ADC:SAMC:SHARed", .callback = SCPI_ADCSamcSharedSet,},
    {.pattern = "CONFigure:ADC:SAMC:SHARed?", .callback = SCPI_ADCSamcSharedGet,},
    //
    // Voltage output precision
    {.pattern = "CONFigure:VOLTage:PRECision", .callback = SCPI_SetDataPrecision,},
    {.pattern = "CONFigure:VOLTage:PRECision?", .callback = SCPI_GetDataPrecision,},
    {.pattern = "CONFigure:VOLTage:SAVE", .callback = SCPI_SaveDataPrecision,},
    {.pattern = "CONFigure:VOLTage:LOAD", .callback = SCPI_LoadDataPrecision,},
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
    // Streaming control — new SYST:STR:* namespace forms are canonical; legacy
    // SYSTem:Start/Stop/StreamData aliases kept for back-compat with existing
    // client libraries and user scripts (#311 round 3).
    {.pattern = "SYSTem:STReam:START", .callback = SCPI_StartStreaming,},
    {.pattern = "SYSTem:STReam:STOP", .callback = SCPI_StopStreaming,},
    {.pattern = "SYSTem:STReam:DATA?", .callback = SCPI_IsStreaming,},
    {.pattern = "SYSTem:StartStreamData", .callback = SCPI_StartStreaming,},
    {.pattern = "SYSTem:StopStreamData", .callback = SCPI_StopStreaming,},
    {.pattern = "SYSTem:StreamData?", .callback = SCPI_IsStreaming,},
    {.pattern = "SYSTem:STReam:FORmat", .callback = SCPI_SetStreamFormat,}, // 0 = pb = default, 1 = text (json)
    {.pattern = "SYSTem:STReam:FORmat?", .callback = SCPI_GetStreamFormat,},
    {.pattern = "SYSTem:STReam:INTerface", .callback = SCPI_SetStreamInterface,}, // 0=USB, 1=WiFi, 2=SD, 3=USB+SD
    {.pattern = "SYSTem:STReam:INTerface?", .callback = SCPI_GetStreamInterface,},
    {.pattern = "SYSTem:STReam:STATS?", .callback = SCPI_GetStreamStats,},
    {.pattern = "SYSTem:STReam:STATS:CLEar", .callback = SCPI_ClearStreamStats,},
    {.pattern = "SYSTem:STReam:LOSS:THREshold", .callback = SCPI_SetLossThreshold,},
    {.pattern = "SYSTem:STReam:LOSS:THREshold?", .callback = SCPI_GetLossThreshold,},
    {.pattern = "SYSTem:STReam:LOSS:WINDow", .callback = SCPI_SetFlowWindow,},
    {.pattern = "SYSTem:STReam:LOSS:WINDow?", .callback = SCPI_GetFlowWindow,},
    {.pattern = "SYSTem:STReam:CONSumer:GRACe", .callback = SCPI_SetTransportGrace,},
    {.pattern = "SYSTem:STReam:CONSumer:GRACe?", .callback = SCPI_GetTransportGrace,},
    {.pattern = "SYSTem:STReam:LOSS:GRACe", .callback = SCPI_SetLossGrace,},  // #450
    {.pattern = "SYSTem:STReam:LOSS:GRACe?", .callback = SCPI_GetLossGrace,},
    {.pattern = "SYSTem:STReam:TEST:PATtern", .callback = SCPI_SetTestPattern,}, // 0=off, 1=counter, 2=midscale, 3=fullscale, 4=walking, 5=triangle, 6=sine
    {.pattern = "SYSTem:STReam:TEST:PATtern?", .callback = SCPI_GetTestPattern,},
    {.pattern = "SYSTem:STReam:BENCHmark", .callback = SCPI_SetBenchmarkMode,}, // 0=normal, 1=nocap, 2=pipeline (skip ADC)
    {.pattern = "SYSTem:STReam:BENCHmark?", .callback = SCPI_GetBenchmarkMode,},
    {.pattern = "SYSTem:STReam:THRoughput", .callback = SCPI_RunThroughputBench,}, // <freq>,<duration_sec> — self-contained benchmark
    {.pattern = "SYSTem:STReam:WIFI:FINd?", .callback = SCPI_WifiFindRate,}, // #520 [startHz][,maxHz] -> recommendedHz,KBps,reason
    // #377 iperf2 wire-rate benchmarks (TCP + UDP).  Refuse if streaming.
    {.pattern = "SYSTem:WIFI:IPERF:TCPServer", .callback = SCPI_Iperf2_TcpServer,}, // [port=5001]
    {.pattern = "SYSTem:WIFI:IPERF:TXBLast", .callback = SCPI_Iperf2_TxBlast,}, // <port>,<duration_s>  #399 workaround
    {.pattern = "SYSTem:WIFI:IPERF:UDPServer", .callback = SCPI_Iperf2_UdpServer,}, // [port=5001]
    {.pattern = "SYSTem:WIFI:IPERF:TCPClient", .callback = SCPI_Iperf2_TcpClient,}, // <ip>,[port=5001],[dur_s=10]
    {.pattern = "SYSTem:WIFI:IPERF:UDPClient", .callback = SCPI_Iperf2_UdpClient,}, // <ip>,[port=5001],[dur_s=10]
    {.pattern = "SYSTem:WIFI:IPERF:STOP", .callback = SCPI_Iperf2_Stop,},
    {.pattern = "SYSTem:WIFI:IPERF:STATs?", .callback = SCPI_Iperf2_Stats,},
    {.pattern = "SYSTem:WIFI:IPERF:DIAGnostics?", .callback = SCPI_Iperf2_Diag,}, // #399
    {.pattern = "SYSTem:WIFI:IPERF:MAXPending", .callback = SCPI_Iperf2_SetMaxPending,}, // #399 throttle (0=default, 1-4)
    {.pattern = "SYSTem:WIFI:IPERF:MAXPending?", .callback = SCPI_Iperf2_GetMaxPending,},
    {.pattern = "SYSTem:WIFI:IPERF:AUTOReset", .callback = SCPI_Iperf2_SetAutoReset,}, // #399 auto-HRESet on stop
    {.pattern = "SYSTem:WIFI:IPERF:AUTOReset?", .callback = SCPI_Iperf2_GetAutoReset,},
    // Dynamic memory configuration
    {.pattern = "SYSTem:MEMory:SD:BUFfer", .callback = SCPI_SetMemSdBuf,},
    {.pattern = "SYSTem:MEMory:SD:BUFfer?", .callback = SCPI_GetMemSdBuf,},
    {.pattern = "SYSTem:MEMory:WIFI:BUFfer", .callback = SCPI_SetMemWifiBuf,},
    {.pattern = "SYSTem:MEMory:WIFI:BUFfer?", .callback = SCPI_GetMemWifiBuf,},
    {.pattern = "SYSTem:MEMory:USB:BUFfer", .callback = SCPI_SetMemUsbBuf,},
    {.pattern = "SYSTem:MEMory:USB:BUFfer?", .callback = SCPI_GetMemUsbBuf,},
    {.pattern = "SYSTem:MEMory:SAMPle:POOL", .callback = SCPI_SetMemSamplePool,},
    {.pattern = "SYSTem:MEMory:SAMPle:POOL?", .callback = SCPI_GetMemSamplePool,},
    {.pattern = "SYSTem:MEMory:ENCoder:BUFfer", .callback = SCPI_SetMemEncoderBuf,},
    {.pattern = "SYSTem:MEMory:ENCoder:BUFfer?", .callback = SCPI_GetMemEncoderBuf,},
    {.pattern = "SYSTem:MEMory:FREE?", .callback = SCPI_GetMemFree,},
    {.pattern = "SYSTem:MEMory:AUTO", .callback = SCPI_MemAutoBalance,},
    {.pattern = "SYSTem:MEMory:RESet", .callback = SCPI_MemReset,},
    {.pattern = "SYSTem:MEMory:STACk?", .callback = SCPI_GetStackStats,},
    //
    {.pattern = "SYSTem:STORage:SD:FILE", .callback = SCPI_StorageSDLoggingSet,},
    {.pattern = "SYSTem:STORage:SD:GET", .callback = SCPI_StorageSDGetData},
    {.pattern = "SYSTem:STORage:SD:LISt?", .callback = SCPI_StorageSDListDir},
    {.pattern = "SYSTem:STORage:SD:ENAble", .callback = SCPI_StorageSDEnableSet},
    {.pattern = "SYSTem:STORage:SD:ENAble?", .callback = SCPI_StorageSDEnableGet},
    {.pattern = "SYSTem:STORage:SD:DELete", .callback = SCPI_StorageSDDelete},
    {.pattern = "SYSTem:STORage:SD:FORmat", .callback = SCPI_StorageSDFormat},
    {.pattern = "SYSTem:STORage:SD:FORmat?", .callback = SCPI_StorageSDFormatQuery},
    {.pattern = "SYSTem:STORage:SD:BENCHmark", .callback = SCPI_StorageSDBenchmark},
    {.pattern = "SYSTem:STORage:SD:BENCHmark?", .callback = SCPI_StorageSDBenchmarkQuery},
    {.pattern = "SYSTem:STORage:SD:MAXSize", .callback = SCPI_StorageSDMaxSizeSet},
    {.pattern = "SYSTem:STORage:SD:MAXSize?", .callback = SCPI_StorageSDMaxSizeGet},
    {.pattern = "SYSTem:STORage:SD:MINFree", .callback = SCPI_StorageSDMinFreeSet},   // #498
    {.pattern = "SYSTem:STORage:SD:MINFree?", .callback = SCPI_StorageSDMinFreeGet},  // #498
    {.pattern = "SYSTem:STORage:SD:SPACe?", .callback = SCPI_StorageSDSpaceGet},
    {.pattern = "SYSTem:STORage:SD:ABORt", .callback = SCPI_StorageSDAbort},
    {.pattern = "SYSTem:STORage:SD:INFO?", .callback = SCPI_StorageSDInfo},
    // DIO debug probe framework
    {.pattern = "SYSTem:DIOProbe:MODE", .callback = SCPI_DioProbeModeSet,},
    {.pattern = "SYSTem:DIOProbe:MODE?", .callback = SCPI_DioProbeModeGet,},
    {.pattern = "SYSTem:DIOProbe:ROUTe", .callback = SCPI_DioProbeMap,},
    {.pattern = "SYSTem:DIOProbe:CLEar", .callback = SCPI_DioProbeClear,},
    {.pattern = "SYSTem:DIOProbe:CLEar:ALL", .callback = SCPI_DioProbeClearAll,},
    {.pattern = "SYSTem:DIOProbe:PIPELine", .callback = SCPI_DioProbePipeline,},
    {.pattern = "SYSTem:DIOProbe:LIST?", .callback = SCPI_DioProbeList,},
    {.pattern = "SYSTem:WINC:GATE?", .callback = SCPI_WincGateQ,},
    // FreeRTOS
    //{.pattern = "SYSTem:OS:Stats?",           .callback = SCPI_GetFreeRtosStats,},
    // Testing
    {.pattern = "BENCHmark?", .callback = SCPI_NotImplemented,},
    {.pattern = NULL, .callback = SCPI_NotImplemented,},
};

#define SCPI_INPUT_BUFFER_LENGTH 512  // Match USB CDC max packet size to prevent silent truncation
#define SCPI_ERROR_QUEUE_SIZE 17
char scpi_input_buffer[SCPI_INPUT_BUFFER_LENGTH];
scpi_error_t scpi_error_queue_data[SCPI_ERROR_QUEUE_SIZE];

// Append formatted text to `buffer` at offset `count`, flushing via
// `context->interface->write` when the next chunk would overflow. Returns
// the updated count. snprintf negative returns (encoding errors) are
// treated as empty append — safer than storing -1 into size_t.
static size_t scpi_help_append(scpi_t* context, char* buffer, size_t count,
                               const char* pattern) {
    size_t cmdSize = strlen(pattern) + 5;  // "  " + pattern + "\r\n"
    if (count + cmdSize >= SCPI_RESPONSE_BUF_SIZE) {
        buffer[count] = '\0';
        context->interface->write(context, buffer, count);
        count = 0;
    }
    int n = snprintf(buffer + count, SCPI_RESPONSE_BUF_SIZE - count,
                     "  %s\r\n", pattern);
    if (n > 0) {
        size_t added = (size_t)n < (SCPI_RESPONSE_BUF_SIZE - count)
                       ? (size_t)n
                       : (SCPI_RESPONSE_BUF_SIZE - count - 1);
        count += added;
    }
    return count;
}

scpi_result_t SCPI_Help(scpi_t* context) {
    // #347: shared SCPI response scratch buffer (was stack-local char[512]).
    char* buffer = (char*)SCPI_ResponseBuf_Take();
    if (buffer == NULL) {
        return SCPI_RES_ERR;
    }
    size_t numCommands = sizeof (scpi_commands) / sizeof (scpi_command_t);
    size_t i = 0;

    int hdr = snprintf(buffer, SCPI_RESPONSE_BUF_SIZE,
                       "%s", "\r\nImplemented:\r\n");
    size_t count = (hdr > 0) ? (size_t)hdr : 0;
    for (i = 0; i < numCommands; ++i) {
        if (scpi_commands[i].callback != SCPI_NotImplemented &&
                scpi_commands[i].pattern != NULL) {
            count = scpi_help_append(context, buffer, count,
                                     scpi_commands[i].pattern);
        }
    }

    if (count > 0) {
        context->interface->write(context, buffer, count);
    }

    hdr = snprintf(buffer, SCPI_RESPONSE_BUF_SIZE,
                   "%s", "\r\nNot Implemented:\r\n");
    count = (hdr > 0) ? (size_t)hdr : 0;
    for (i = 0; i < numCommands; ++i) {
        if (scpi_commands[i].callback == SCPI_NotImplemented &&
                scpi_commands[i].pattern != NULL) {
            count = scpi_help_append(context, buffer, count,
                                     scpi_commands[i].pattern);
        }
    }

    if (count > 0) {
        context->interface->write(context, buffer, count);
    }

    SCPI_ResponseBuf_Give();
    return SCPI_RES_OK;
}

#define SCPI_WRITE_MAX_RETRIES      200
#define SCPI_WRITE_RETRY_DELAY_MS   5

size_t SCPI_WriteWithRetry(ScpiTransportWriteFn writeFn,
                           const char* data, size_t len) {
    size_t written = 0;
    int retries = SCPI_WRITE_MAX_RETRIES;
    while (written < len && retries > 0) {
        size_t n = writeFn(data + written, len - written);
        written += n;
        if (written >= len) break;
        vTaskDelay(pdMS_TO_TICKS(SCPI_WRITE_RETRY_DELAY_MS));
        retries--;
    }
    return written;
}

scpi_t CreateSCPIContext(scpi_interface_t* interface, void* user_context) {
    // Defense in depth: SCPI_ResponseBuf_Init() is supposed to have been
    // called during app boot before any transport creates its SCPI context.
    // Call it again here — it's idempotent — so the shared response-buffer
    // mutex is guaranteed to exist before any callback could dispatch.
    SCPI_ResponseBuf_Init();

    // Create a context
    scpi_t daqifiScpiContext;
    // Init context.  gIdnModel and gIdnSerial are populated once pre-scheduler
    // by SCPI_InitIdentification() so concurrent CreateSCPIContext() calls
    // from USB and WiFi tasks just read the same finished strings.
    SCPI_Init(&daqifiScpiContext,
            scpi_commands,
            interface,
            scpi_units_def,
            SCPI_IDN1, gIdnModel, gIdnSerial, SCPI_IDN4,
            scpi_input_buffer, SCPI_INPUT_BUFFER_LENGTH,
            scpi_error_queue_data, SCPI_ERROR_QUEUE_SIZE);

    // Return it to the app
    return daqifiScpiContext;
}
