/*! @file AD7609.c
 *
 * @brief AD7609 8-Channel, 18-bit ADC Driver
 *
 * This file implements the hardware abstraction layer for the Analog Devices AD7609
 * simultaneous sampling, 18-bit ADC. The driver supports serial data interface mode
 * with manual chip select control via Harmony DRV_SPI.
 *
 * Key Features:
 * - 8 differential input channels
 * - 18-bit resolution with 2's complement output
 * - Configurable ±5V or ±10V input range per module
 * - SPI interface at 10 MHz
 * - Manual conversion triggering via CONVST pin
 * - Busy signal monitoring for conversion complete
 */

#include "AD7609.h"
#include "configuration.h"
#include "definitions.h"
#include "driver/spi/drv_spi.h"
#include "peripheral/coretimer/plib_coretimer.h"
#include "state/board/BoardConfig.h"
#include "state/runtime/BoardRuntimeConfig.h"
#include "HAL/ADC.h"
#include "Util/Logger.h"
#include "system/cache/sys_cache.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

// Simple delay function using core timer (wrap-around safe, overflow-safe)
static void AD7609_Delay_ms(uint32_t milliseconds) {
    uint32_t startCount = CORETIMER_CounterGet();
    // Use 64-bit arithmetic to prevent overflow for large millisecond values
    uint32_t ticks = (uint32_t)(((uint64_t)milliseconds * CORETIMER_FrequencyGet()) / 1000U);

    // Use modular arithmetic to handle 32-bit timer wrap-around correctly
    // Subtraction of unsigned values wraps correctly
    while ((uint32_t)(CORETIMER_CounterGet() - startCount) < ticks) {
        // Wait for elapsed ticks
    }
}

#define UNUSED(x) (void)(x)

//! SPI timeout in iterations (approximately 100k iterations = ~10ms at 200MHz)
#define AD7609_SPI_TIMEOUT 100000

/**
 * @brief Extract 18-bit value from big-endian bitstream
 *
 * Optimized for AD7609: No bounds checking needed. Buffer is 19 bytes (18 data + 1 padding)
 * to allow 4-byte window loads for all channels (0-7) without bounds checking overhead.
 * Valid bit positions: 0, 18, 36, 54, 72, 90, 108, 126 (hwChannel validated in caller).
 *
 * @param buf Pointer to 19-byte buffer (18 data + 1 padding)
 * @param bitPos Bit position in stream (0-126 for channels 0-7)
 * @return Extracted 18-bit value
 */
static uint32_t AD7609_Extract18Bit(const uint8_t* buf, uint16_t bitPos) {
    // Calculate byte index and bit offset
    size_t byteIndex = bitPos >> 3;  // bitPos / 8
    uint8_t bitInByte = (uint8_t)(bitPos & 0x7U);  // bitPos % 8

    // Load 32-bit window from buffer (big-endian, MSB first)
    // Channel 7 (bit 126) loads buf[15..18] - all within 19-byte buffer
    uint32_t window = ((uint32_t)buf[byteIndex]     << 24) |
                      ((uint32_t)buf[byteIndex + 1U] << 16) |
                      ((uint32_t)buf[byteIndex + 2U] << 8)  |
                      ((uint32_t)buf[byteIndex + 3U]);

    // Shift to align 18-bit field at top of window, then shift down and mask
    return ((window << bitInByte) >> (32 - 18)) & 0x3FFFFU;
}

//! BSY pin settling timeout
//! Datasheet: BSY deasserts typically <10us after conversion/read complete
//! Timeout: 10x datasheet spec = 100us for safety margin
#define AD7609_BSY_SETTLE_TIMEOUT_US 100

//! Pointer to the module configuration data structure to be set in initialization
static const AD7609ModuleConfig* pModuleConfigAD7609;
//! Pointer to the module configuration data structure in runtime
static AInModuleRuntimeConfig* pModuleRuntimeConfigAD7609 __attribute__((unused));

// DRV_SPI handle for AD7609 communication
static DRV_HANDLE spi_handle = DRV_HANDLE_INVALID;

// AD7609 BSY interrupt handling
// Volatile ensures ISR sees updates from task context without caching
static volatile TaskHandle_t gAD7609_TaskHandle = NULL;

// SPI mutex to serialize access to shared SPI bus and DMA buffers
static SemaphoreHandle_t gAD7609_SpiMutex = NULL;

// DMA-coherent SPI buffers (must be global for cache coherency)
// RX buffer is 19 bytes (18 data + 1 padding) to allow window-loading for channel 7
// without bounds checking overhead. AD7609 sends 144 bits (18 bytes) of data.
static __attribute__((coherent)) uint8_t gAD7609_txBuffer[18];
static __attribute__((coherent)) uint8_t gAD7609_rxBuffer[19];

// Accessor function for task handle (used by tasks.c)
volatile void* AD7609_GetTaskHandle(void) {
    return &gAD7609_TaskHandle;
}

// AD7609 BSY pin interrupt callback (called from GPIO ISR)
void AD7609_BSY_InterruptCallback(GPIO_PIN pin, uintptr_t context)
{
    UNUSED(context);

    if (pModuleConfigAD7609 == NULL || pin != pModuleConfigAD7609->BSY_Pin || gAD7609_TaskHandle == NULL) {
        return;
    }

    // Disable further BSY interrupts until next sample trigger
    // (interrupt status already cleared by GPIO PLIB handler before callback)
    GPIO_PinIntDisable(pModuleConfigAD7609->BSY_Pin);

    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    vTaskNotifyGiveFromISR(gAD7609_TaskHandle, &xHigherPriorityTaskWoken);
    portEND_SWITCHING_ISR(xHigherPriorityTaskWoken);
}

// AD7609 deferred interrupt task (handles SPI read after BSY interrupt)
void AD7609_DeferredInterruptTask(void) {
    // Use 100ms timeout to prevent deadlock if interrupt is missed or spurious
    const TickType_t xBlockTime = pdMS_TO_TICKS(100);

    while (1) {
        // Wait for BSY pin interrupt to signal conversion complete
        // ulTaskNotifyTake() returns notification count and clears it atomically
        uint32_t notificationValue = ulTaskNotifyTake(pdFALSE, xBlockTime);

        if (notificationValue > 0) {
            // Received valid notification from ISR
            // Call ADC layer to handle data acquisition and storage
            // This separates hardware driver from data management
            ADC_HandleAD7609Interrupt();

            if (pModuleConfigAD7609 != NULL) {
                // BSY should be idle-high by now (SPI read complete)
                // If still low, indicates hardware fault - log error but don't block
                if (!GPIO_PinRead(pModuleConfigAD7609->BSY_Pin)) {
                    LOG_E("AD7609: BSY pin stuck low after SPI read - possible hardware fault");
                }
                // Re-enable interrupt for next conversion
                GPIO_PinIntEnable(pModuleConfigAD7609->BSY_Pin, GPIO_INTERRUPT_ON_FALLING_EDGE);
            }
        } else {
            // Timeout occurred - no notification received within 100ms
            // Timeout recovery: re-enable interrupt for next attempt
            if (pModuleConfigAD7609 != NULL) {
                GPIO_PinIntEnable(pModuleConfigAD7609->BSY_Pin, GPIO_INTERRUPT_ON_FALLING_EDGE);
            }
        }
    }
} 

/*!
 *  Reset the module - Updated for Harmony 3 GPIO API
 */
static void AD7609_Reset( void )
{
    AD7609_Delay_ms(100); // Delay reset for 100ms to ensure power on is completed properly 
    
    // Proper AD7609 reset sequence (from working commit):
    // 1. Set CS high (inactive) 
    GPIO_PinWrite(pModuleConfigAD7609->CS_Pin, true);
    
    // 2. Set RST high (active reset)
    GPIO_PinWrite(pModuleConfigAD7609->RST_Pin, true);
    AD7609_Delay_ms(1); // Hold reset for 1ms
    
    // 3. Set RST low (release reset)
    GPIO_PinWrite(pModuleConfigAD7609->RST_Pin, false);
    
    // 4. Wait for chip to initialize (datasheet: >500ns)
    AD7609_Delay_ms(1); // 10ms for safety
}

bool AD7609_InitHardware(const AD7609ModuleConfig* pBoardConfigInit)
{
    // Initialize AD7609 hardware after power-up

    pModuleConfigAD7609 = pBoardConfigInit;

    // Create SPI mutex for serializing shared SPI bus access
    if (gAD7609_SpiMutex == NULL) {
        gAD7609_SpiMutex = xSemaphoreCreateMutex();
        if (gAD7609_SpiMutex == NULL) {
            LOG_E("AD7609_InitHardware: Failed to create SPI mutex");
            return false;
        }
    }

    // Open DRV_SPI driver for SPI6 (DRV_SPI_INDEX_1 maps to SPI6)
    spi_handle = DRV_SPI_Open(DRV_SPI_INDEX_1, DRV_IO_INTENT_READWRITE);
    if (spi_handle == DRV_HANDLE_INVALID) {
        LOG_E("AD7609_InitHardware: Failed to open SPI driver");
        return false;
    }

    // Initialize GPIO pins for AD7609 control after reset
    GPIO_PinWrite(pModuleConfigAD7609->CS_Pin, true);      // CS high (inactive)
    // Per datasheet: Range pin HIGH=±20V, LOW=±10V
    // But hardware may be wired differently - use inverted logic to match SCPI implementation
    GPIO_PinWrite(pModuleConfigAD7609->Range_Pin, !pModuleConfigAD7609->Range10V);  // Range setting
    GPIO_PinWrite(pModuleConfigAD7609->OS0_Pin, pModuleConfigAD7609->OSMode & 0b01); // OS0 setting
    GPIO_PinWrite(pModuleConfigAD7609->OS1_Pin, (pModuleConfigAD7609->OSMode & 0b10) >> 1); // OS1 setting
    GPIO_PinWrite(pModuleConfigAD7609->CONVST_Pin, true);  // CONVST high (idle state per datasheet)
    GPIO_PinWrite(pModuleConfigAD7609->STBY_Pin, true);    // STBY high (normal operation, active low)
    GPIO_PinWrite(pModuleConfigAD7609->RST_Pin, false);    // RST low (idle, not in reset)
    
    // Override MCC GPIO configuration for SPI bus management
    // Ensure proper pin directions and states for AD7609 operation
    GPIO_PinOutputEnable(pModuleConfigAD7609->CS_Pin);      // CS as output
    GPIO_PinOutputEnable(pModuleConfigAD7609->Range_Pin);   // Range as output
    GPIO_PinOutputEnable(pModuleConfigAD7609->OS0_Pin);   // OS0 as output
    GPIO_PinOutputEnable(pModuleConfigAD7609->OS1_Pin);   // OS1 as output
    GPIO_PinOutputEnable(pModuleConfigAD7609->CONVST_Pin);  // CONVST as output 
    GPIO_PinOutputEnable(pModuleConfigAD7609->STBY_Pin);    // STBY as output
    GPIO_PinOutputEnable(pModuleConfigAD7609->RST_Pin);    // RST as output
    
    GPIO_PinInputEnable(pModuleConfigAD7609->BSY_Pin);      // BSY as input
    
    // If RC15 is another SPI device CS, ensure it's inactive
    GPIO_PinOutputEnable(GPIO_PIN_RC15);
    GPIO_PinWrite(GPIO_PIN_RC15, true); // Keep inactive
    
    // Reset the AD7609
    AD7609_Reset();

    // Register BSY pin interrupt callback (falling edge on conversion complete)
    // Note: Task creation is handled in config/default/tasks.c
    // AD7609 BSY goes LOW (falling edge) when conversion is complete
    GPIO_PinInterruptCallbackRegister(pModuleConfigAD7609->BSY_Pin, AD7609_BSY_InterruptCallback, 0);

    // Wait for BSY to reach idle-high after reset before enabling interrupt
    // Datasheet: BSY deasserts typically <10us after reset completes
    // Timeout: 10x spec = 100us, Tick: continuous polling
    if (!GPIO_PinRead(pModuleConfigAD7609->BSY_Pin)) {
        const uint32_t coreTimerFreq = CORETIMER_FrequencyGet();
        const uint32_t timeoutTicks = (coreTimerFreq * AD7609_BSY_SETTLE_TIMEOUT_US) / 1000000;
        uint32_t startCount = CORETIMER_CounterGet();

        while (!GPIO_PinRead(pModuleConfigAD7609->BSY_Pin) &&
               ((uint32_t)(CORETIMER_CounterGet() - startCount) < timeoutTicks)) {
            // Continuous polling (wrap-safe arithmetic)
        }

        // If still low after timeout, log hardware fault
        if (!GPIO_PinRead(pModuleConfigAD7609->BSY_Pin)) {
            LOG_E("AD7609_InitHardware: BSY pin stuck low after reset - possible hardware fault");
        }
    }

    // Enable falling-edge interrupt for conversion complete detection
    GPIO_PinIntEnable(pModuleConfigAD7609->BSY_Pin, GPIO_INTERRUPT_ON_FALLING_EDGE);

    return true;
}

bool AD7609_WriteModuleState(bool isPowered)
{
    UNUSED(isPowered);

    if (pModuleConfigAD7609 == NULL) {
        return false;
    }

    return true;
}

bool AD7609_WriteStateSingle(
        AInModuleRuntimeConfig* moduleRuntimeConfig,
        const AD7609ChannelConfig* channelConfig,
        AInRuntimeConfig* channelRuntimeConfig)
{
    UNUSED(moduleRuntimeConfig);
    UNUSED(channelConfig);
    UNUSED(channelRuntimeConfig);

    return true;
}

bool AD7609_WriteStateAll(
                        const AInArray* channelConfig,
                        AInRuntimeArray* channelRuntimeConfig)
{
    UNUSED(channelConfig);
    UNUSED(channelRuntimeConfig);

    if (pModuleConfigAD7609 == NULL) {
        LOG_E("AD7609_WriteStateAll: AD7609 not initialized");
        return false;
    }

    return true;
}

bool AD7609_ReadSamples(AInSampleArray* samples,
                        const AInArray* channelConfigList,
                        AInRuntimeArray* channelRuntimeConfigList,
                        uint32_t triggerTimeStamp)
{
    if (pModuleConfigAD7609 == NULL || spi_handle == DRV_HANDLE_INVALID) {
        return false;
    }

    // Check if conversion is ready by reading BSY pin (BSY low = ready)
    if (GPIO_PinRead(pModuleConfigAD7609->BSY_Pin)) {
        return false; // Chip not ready
    }

    // Acquire SPI lock before using shared SPI and buffers
    if (xSemaphoreTake(gAD7609_SpiMutex, pdMS_TO_TICKS(10)) != pdTRUE) {
        LOG_E("AD7609_ReadSamples: Failed to acquire SPI mutex");
        return false;
    }
    bool success = false;

    // Configure SPI mode for AD7609 before transfer
    // AD7609 requires SPI Mode 2: CPOL=1 (idle high), CPHA=0 (sample on first edge)
    // Harmony enum: LEADING_EDGE = sample on first edge = CPHA=0, maps to CKE=1 for Mode 2
    // This ensures correct configuration even if SPI bus is shared with other devices
    DRV_SPI_TRANSFER_SETUP spiSetup;
    spiSetup.baudRateInHz = 10000000;  // 10 MHz (AD7609 supports up to 24 MHz)
    spiSetup.clockPolarity = DRV_SPI_CLOCK_POLARITY_IDLE_HIGH;     // CPOL=1
    spiSetup.clockPhase = DRV_SPI_CLOCK_PHASE_VALID_LEADING_EDGE;  // CPHA=0 (first edge, CKE=1)
    spiSetup.dataBits = DRV_SPI_DATA_BITS_8;
    spiSetup.chipSelect = SYS_PORT_PIN_NONE;  // Manual CS control
    spiSetup.csPolarity = DRV_SPI_CS_POLARITY_ACTIVE_LOW;

    if (!DRV_SPI_TransferSetup(spi_handle, &spiSetup)) {
        LOG_E("AD7609_ReadSamples: Failed to configure SPI mode");
        xSemaphoreGive(gAD7609_SpiMutex);
        return false;
    }

    // Clear DMA buffers (global, coherent for DMA access)
    memset(gAD7609_txBuffer, 0, sizeof(gAD7609_txBuffer));
    memset(gAD7609_rxBuffer, 0, sizeof(gAD7609_rxBuffer));

    // Ensure TX buffer is written back to memory before DMA reads it
    SYS_CACHE_CleanDCache_by_Addr((void*)gAD7609_txBuffer, sizeof(gAD7609_txBuffer));
    // Invalidate RX buffer so CPU reads post-DMA contents (not stale cache)
    SYS_CACHE_InvalidateDCache_by_Addr((void*)gAD7609_rxBuffer, sizeof(gAD7609_rxBuffer));

    // Assert CS (active low)
    GPIO_PinWrite(pModuleConfigAD7609->CS_Pin, false);

    // Use DRV_SPI async API (driver is in async mode for WiFi compatibility)
    DRV_SPI_TRANSFER_HANDLE transferHandle = DRV_SPI_TRANSFER_HANDLE_INVALID;
    DRV_SPI_WriteReadTransferAdd(spi_handle, gAD7609_txBuffer, 18, gAD7609_rxBuffer, 18, &transferHandle);

    if (transferHandle == DRV_SPI_TRANSFER_HANDLE_INVALID) {
        LOG_E("AD7609_ReadSamples: Failed to queue SPI transfer");
        goto read_cleanup;
    }

    // Poll for completion with timeout protection
    DRV_SPI_TRANSFER_EVENT event;
    uint32_t timeout = AD7609_SPI_TIMEOUT;
    do {
        event = DRV_SPI_TransferStatusGet(transferHandle);
        if (event != DRV_SPI_TRANSFER_EVENT_PENDING) {
            break;
        }
    } while (--timeout > 0);

    if (timeout == 0 || event != DRV_SPI_TRANSFER_EVENT_COMPLETE) {
        LOG_E("AD7609_ReadSamples: SPI transfer %s", (timeout == 0) ? "timeout" : "failed");
        goto read_cleanup;
    }

    // Invalidate RX buffer cache again to ensure CPU reads DMA-transferred data
    // This guarantees we're not reading stale cached values
    SYS_CACHE_InvalidateDCache_by_Addr((void*)gAD7609_rxBuffer, sizeof(gAD7609_rxBuffer));

    success = true;
    // Fall through to cleanup

read_cleanup:
    // Deassert CS (inactive high) on all paths
    GPIO_PinWrite(pModuleConfigAD7609->CS_Pin, true);
    xSemaphoreGive(gAD7609_SpiMutex);

    if (!success) {
        return false;
    }

    // AD7609 sends data in serial mode: 18 bits per channel, 8 channels sequentially
    // Total: 144 bits (18 bytes) in continuous stream, MSB first per channel

    // Treat samples->Size as capacity (maximum samples buffer can hold)
    size_t capacity = samples->Size;
    size_t sampleCount = 0;

    for (size_t i = 0; i < channelConfigList->Size; i++) {
        if (channelConfigList->Data[i].Type == AIn_AD7609 &&
            channelRuntimeConfigList->Data[i].IsEnabled) {

            // Get the hardware channel number from config
            uint8_t hwChannel = channelConfigList->Data[i].Config.AD7609.ChannelNumber;

            // Validate hardware channel is in range
            if (hwChannel >= AD7609_NUM_CHANNELS) {
                continue; // Skip invalid channels
            }

            // Calculate bit position in the bitstream
            uint16_t bitPosition = hwChannel * AD7609_BITS_PER_CHANNEL;

            // Extract 18-bit value (no bounds checking needed - buffer is fixed size)
            uint32_t tmpData = AD7609_Extract18Bit(gAD7609_rxBuffer, bitPosition);

            // Convert from 18-bit 2's complement to signed 32-bit
            if (tmpData & AD7609_SIGN_BIT) {
                tmpData |= AD7609_SIGN_EXTEND;  // Sign extend
            }

            // Create sample entry with DAQiFi channel ID (check capacity, not current size)
            if (sampleCount < capacity) {
                samples->Data[sampleCount].Channel = channelConfigList->Data[i].DaqifiAdcChannelId;
                samples->Data[sampleCount].Value = (int32_t)tmpData;
                samples->Data[sampleCount].Timestamp = triggerTimeStamp;
                sampleCount++;
            } else {
                LOG_E("AD7609_ExtractSamples: Sample buffer full (capacity=%u)", capacity);
                break;  // Stop processing if buffer is full
            }
        }
    }

    // Update Size to reflect actual number of samples written
    samples->Size = sampleCount;
    
    return (sampleCount > 0); // Return true if we got any samples
}

bool AD7609_TriggerConversion(const AD7609ModuleConfig* moduleConfig)
{
    UNUSED(moduleConfig);

    if (pModuleConfigAD7609 == NULL) {
        LOG_E("AD7609_TriggerConversion: pModuleConfigAD7609 is NULL");
        return false;
    }

    // Check if the AD7609 is busy
    bool busy = GPIO_PinRead(pModuleConfigAD7609->BSY_Pin);
    if (busy) {
        return false; // Skip conversion if still busy
    }

    // Ensure CONVST is at idle HIGH before pulsing (defensive check)
    GPIO_PinWrite(pModuleConfigAD7609->CONVST_Pin, true);

    // Generate deterministic CONVST LOW pulse using core timer
    // AD7609 datasheet requires LOW-going pulse: idle HIGH → pulse LOW → return HIGH
    // Minimum pulse width: 25ns, using 200ns for safety margin
    const uint32_t coreTimerFreq = CORETIMER_FrequencyGet();
    const uint32_t ticksForPulse = (coreTimerFreq + 4999999U) / 5000000U; // 200ns = 1/(5MHz)

    uint32_t startCount = CORETIMER_CounterGet();
    GPIO_PinWrite(pModuleConfigAD7609->CONVST_Pin, false);  // Pulse LOW to trigger conversion

    // Wait for pulse duration using wrap-safe arithmetic
    while ((uint32_t)(CORETIMER_CounterGet() - startCount) < ticksForPulse) {
        // Busy-wait for 200ns pulse width
    }

    GPIO_PinWrite(pModuleConfigAD7609->CONVST_Pin, true);   // Return to idle HIGH
    return true;
}

double AD7609_ConvertToVoltage(
                        const AInRuntimeConfig* runtimeConfig,
                        const AInSample* sample)
{
    UNUSED(runtimeConfig);

    if (sample == NULL) {
        LOG_E("AD7609_ConvertToVoltage: NULL sample");
        return 0.0;
    }

    // Get runtime range from module configuration using BoardRunTimeConfig_Get
    AInModRuntimeArray* pRuntimeModules = BoardRunTimeConfig_Get(BOARDRUNTIMECONFIG_AIN_MODULES);
    double fullScaleVoltage = 10.0; // Default ±10V range

    if (pRuntimeModules != NULL && pRuntimeModules->Size > AIn_AD7609) {
        // AD7609 module index is AIn_AD7609 (1)
        fullScaleVoltage = pRuntimeModules->Data[AIn_AD7609].Range;
    }

    // AD7609 is 18-bit, 2's complement
    const int32_t maxCode = (AD7609_MAX_VALUE >> 1); // Half scale for signed 18-bit (131071)

    // Convert raw ADC value to signed integer
    int32_t rawValue = (int32_t)sample->Value;

    // Handle 18-bit 2's complement conversion by checking sign bit
    // If bit 17 is set, the value is negative and needs sign extension
    if (rawValue & AD7609_SIGN_BIT) {
        rawValue |= AD7609_SIGN_EXTEND;  // Sign extend from 18 bits to 32 bits
    }

    // Convert to voltage: raw / maxCode * fullScale
    double voltage = ((double)rawValue / (double)maxCode) * fullScaleVoltage;
    return voltage;
}