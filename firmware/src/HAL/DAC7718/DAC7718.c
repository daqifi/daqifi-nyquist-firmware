/*
 * @file   DAC7718.c
 * @brief This file manages the DAC7718 module
 *
 */
#define LOG_LVL LOG_LEVEL_DAC
#define LOG_MODULE LOG_MODULE_DAC

#include "DAC7718.h"
#include "peripheral/gpio/plib_gpio.h"
#include "peripheral/spi/spi_master/plib_spi2_master.h"
#include "peripheral/coretimer/plib_coretimer.h"
#include "Util/Logger.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

// Simple delay function using core timer (wrap-around safe, overflow-safe)
static void DAC7718_Delay_us(uint32_t microseconds) {
    // Cache frequency on first call to avoid repeated function calls
    static uint32_t freq = 0U;
    if (freq == 0U) {
        freq = CORETIMER_FrequencyGet();
    }

    uint32_t startCount = CORETIMER_CounterGet();
    // Use 64-bit arithmetic to prevent overflow for large microsecond values
    uint32_t ticks = (uint32_t)(((uint64_t)microseconds * freq) / 1000000U);

    // Use modular arithmetic to handle 32-bit timer wrap-around correctly
    while ((uint32_t)(CORETIMER_CounterGet() - startCount) < ticks) {
        // Wait for elapsed ticks
    }
}

//! Max number of configuration to DAC7718 module
#define MAX_DAC7718_CONFIG 1

//! SPI timeout in iterations (approximately 100k iterations = ~10ms at 200MHz)
#define DAC7718_SPI_TIMEOUT 100000

//! Buffer with DAC7718 configurations
static tDAC7718Config m_DAC7718Config[MAX_DAC7718_CONFIG];
//! Number of configuration
static uint8_t m_DAC7718ConfigCount;

//! Static SPI buffers to avoid stack/cache issues
static uint8_t spi_txData[3] __attribute__((coherent, aligned(4)));
static uint8_t spi_rxData[3] __attribute__((coherent, aligned(4)));

//! Mutex to protect DAC7718 initialization and SPI access
static SemaphoreHandle_t gDAC7718_Mutex = NULL;

// Helper to acquire mutex. #980: the mutex is created once in
// DAC7718_InitGlobal() (single-threaded, pre-scheduler) -- see the comment
// there for why the lazy "create on first use" this replaced was itself a
// second, unsynchronized TOCTOU race (two tasks could each see NULL and each
// create their OWN mutex, so neither ever excluded the other from SPI2).
// Fail closed if it is absent rather than manufacture one here.
static bool DAC7718_Lock(void)
{
    if (gDAC7718_Mutex == NULL) {
        LOG_E("DAC7718_Lock: mutex not created");
        return false;
    }

    if (xSemaphoreTake(gDAC7718_Mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        LOG_E("DAC7718_Lock: Failed to acquire mutex");
        return false;
    }

    return true;
}

// Helper to release mutex and cleanup CS pin state.
// #980 item 2: `lockHeld` must be true only when THIS call's own
// DAC7718_Lock() succeeded -- callers that jump here after a validation
// failure (before ever calling Lock) pass lockHeld=false so a mutex this call
// never took is never given. Giving an unowned mutex trips FreeRTOS's
// ownership assertion if another task currently holds it.
static void DAC7718_Unlock(tDAC7718Config* config, bool csAsserted, bool lockHeld)
{
    // De-assert CS if still asserted
    if (csAsserted && (config != NULL)) {
        GPIO_PinWrite(config->CS_Pin, true);
    }

    // Release mutex -- only what this call actually took.
    if (lockHeld && (gDAC7718_Mutex != NULL)) {
        xSemaphoreGive(gDAC7718_Mutex);
    }
}

/*!
* Resets the DAC7718.  Must be called after DAC7718_Init
* @param id Driver instance ID
*/
// static void DAC7718_Reset(uint8_t id);  // Not used - CLR tied to RST 

// SPI2 is configured by MCC - no separate configuration needed

void DAC7718_InitGlobal( void )
{
    memset(m_DAC7718Config, 0, MAX_DAC7718_CONFIG * sizeof(tDAC7718Config));
    m_DAC7718ConfigCount = 0;

    // #980: create the SPI/init mutex HERE rather than lazily in
    // DAC7718_Lock()/DAC7718_Init(). This runs from app_SystemInit(), which
    // APP_FREERTOS_Tasks() completes before app_TasksCreate() starts
    // app_USBDeviceTask / app_WifiTask -- a single context, so this create
    // cannot race. The lazy "if (gDAC7718_Mutex == NULL) create" it replaces
    // was itself check-then-act: two SCPI tasks could both observe NULL and
    // each create their OWN mutex, leaving SPI2 with no mutual exclusion at
    // all (plus a leaked semaphore) instead of the shared lock every caller
    // assumes exists.
    if (gDAC7718_Mutex == NULL) {
        gDAC7718_Mutex = xSemaphoreCreateMutex();
        if (gDAC7718_Mutex == NULL) {
            LOG_E("DAC7718_InitGlobal: Failed to create mutex; DAC unavailable");
        }
    }
}

uint8_t DAC7718_NewConfig(const tDAC7718Config *newDAC7718Config)
{
    // #64 audit, twin of DAC7718_GetConfig below: this used to take
    // id = m_DAC7718ConfigCount and memcpy into m_DAC7718Config[id]
    // unconditionally, so a second call wrote a whole tDAC7718Config past the
    // end of the one-element array. The `dacInstanceId == 0xFF` test at the
    // sole call site (SCPIDAC.c DAC_EnsureHardwareInitialized) was therefore
    // permanently dead, because nothing here could produce 0xFF. Bound-check
    // before the write, return that sentinel when the table is full, and only
    // advance the counter once the entry is actually stored -- so a rejected
    // call leaves no state behind and m_DAC7718ConfigCount keeps meaning
    // "number of valid entries", which is the invariant this check reads.
    //
    // Round-2 follow-up (concurrency): the bound check, the slot choice, the
    // copy and the increment are ONE atomic step. The command table is shared
    // by both SCPI transports -- app_USBDeviceTask (pri 7) and app_WifiTask
    // (pri 2) each run SCPI_Input() on their own context
    // (SCPIInterface.c:271-275) over the same scpi_commands[]
    // (SCPIInterface.c:8142, DAC rows 8382-8394) -- so two first-time DAC
    // commands can be inside SCPIDAC.c DAC_EnsureHardwareInitialized at the
    // same time and both reach here. Unguarded,
    // both read count==0, both take slot 0, and the counter lands at 2, which
    // breaks the invariant DAC7718_GetConfig below now depends on ("NewConfig
    // caps the counter at MAX, so id < count implies id < MAX") and would let
    // an id of 1 index past this one-element array -- re-opening the very #64
    // OOB this PR exists to close. Advancing the counter only after the copy
    // also means a reader on the other task sees either "no entry" or a fully
    // copied one, never a half-written config.
    //
    // Same idiom and rationale as SCPIDIO.c's cross-interface guard: the
    // region is a comparison, a fixed-size memcpy (~50 B) and one increment --
    // no loops, no I/O, no blocking call -- so the section is sub-microsecond.
    // LOG_E stays OUTSIDE it (it formats and takes a mutex).
    uint8_t id;

    taskENTER_CRITICAL();
    if (m_DAC7718ConfigCount >= MAX_DAC7718_CONFIG) {
        taskEXIT_CRITICAL();
        LOG_E("DAC7718_NewConfig: config table full (max %u)",
              (unsigned)MAX_DAC7718_CONFIG);
        return 0xFFU;   // sentinel: no id allocated
    }

    id = m_DAC7718ConfigCount;
    memcpy(&m_DAC7718Config[id], newDAC7718Config, sizeof(tDAC7718Config));
    ++m_DAC7718ConfigCount;
    taskEXIT_CRITICAL();

    return id;
}

tDAC7718Config* DAC7718_GetConfig(uint8_t id)
{
    // #64 audit: this used to return &m_DAC7718Config[id] unconditionally,
    // so an out-of-range id read/wrote past the one-element array while the
    // `config == NULL` tests at both call sites (DAC7718_Init,
    // DAC7718_ReadWriteReg) stayed permanently dead. Bound-check here once so
    // those existing checks become real instead of touching every call site.
    //
    // Bound against the allocation COUNT, not MAX_DAC7718_CONFIG (Qodo
    // /improve on #64): strictly stronger, since NewConfig above caps the
    // counter at MAX, so id < m_DAC7718ConfigCount implies id < MAX and the
    // index is in range. Identical for every reachable caller today -- the
    // only id that reaches here is SCPIDAC.c's dacInstanceId, which is 0xFF
    // until NewConfig succeeds and 0 only once the counter is already 1 --
    // but this also rejects slot 0 before DAC7718_InitGlobal has run, and
    // fails safe to NULL under concurrent USB/WiFi SCPI entry into
    // DAC_EnsureHardwareInitialized rather than returning a mid-memcpy config.
    if (id >= m_DAC7718ConfigCount) {
        return NULL;
    }
    return &m_DAC7718Config[id];
}

bool DAC7718_Init(uint8_t id, uint8_t range)
{
    tDAC7718Config* config = DAC7718_GetConfig(id);

    if (config == NULL) {
        LOG_E("DAC7718_Init: invalid config id=%u", id);
        return false;   // #980 item 1: honest failure, not a silent success
    }

    // #980: the mutex is created once in DAC7718_InitGlobal(), not lazily
    // here -- see that function's comment for why the lazy create this
    // replaced was itself an unsynchronized race. Fail closed (rather than
    // create one now) if it is somehow still absent.
    if (gDAC7718_Mutex == NULL) {
        LOG_E("DAC7718_Init: mutex not created");
        return false;
    }

    // SPI2 is already initialized by MCC

	// #980: serialize the GPIO reset pulse against any in-flight
	// DAC7718_ReadWriteReg SPI transaction. This pulse used to run outside any
	// lock; that was tolerable while it could only ever happen once, at boot,
	// before any command could reach DAC7718_ReadWriteReg. #980 item 3 makes
	// this function re-entrant (a power cycle can trigger a full re-init at
	// any time), so an in-flight SPI frame from a live command can now overlap
	// this reset -- taking the lock here closes that. Released before the
	// register write below, which takes this SAME non-recursive mutex itself
	// (recursion would need item 4's separate mutex-recursion change).
	if (!DAC7718_Lock()) {
	    return false;
	}

	// Configure GPIO pins as outputs before setting values (prevents glitches)
    GPIO_PinOutputEnable(config->CS_Pin);
    GPIO_PinOutputEnable(config->RST_Pin);

	// Initialize GPIO pins - CS high (inactive), RST high for normal operation
    GPIO_PinWrite(config->CS_Pin, true);
    GPIO_PinWrite(config->RST_Pin, true);

	// Hardware reset sequence: RST low pulse, then high
	// DAC7718 datasheet requires minimum 100ns reset pulse
	GPIO_PinWrite(config->RST_Pin, false);  // Assert reset (active low)
	DAC7718_Delay_us(1);  // 1us delay (10x minimum spec, guaranteed safe)
	GPIO_PinWrite(config->RST_Pin, true);   // De-assert reset (return to idle high)

	DAC7718_Unlock(config, false, true);  // csAsserted=false (CS untouched here), lockHeld=true

	// Configure DAC gain based on range parameter
	// Range 0: 0-5V  (GAIN-A=0, GAIN-B=0 for 2x gain)
	// Range 1: 0-10V (GAIN-A=1, GAIN-B=1 for 4x gain)
	// Currently fixed to 10V range, but infrastructure available for future use
	(void)range;  // Parameter reserved for future use
	uint16_t configReg = 0b100000011000;  // GAIN-A=1, GAIN-B=1 for 4x gain (10V)

	// Note: DAC7718_ReadWriteReg handles mutex locking internally
	uint32_t result = DAC7718_ReadWriteReg(id, 0, 0, configReg);
	if (result == UINT32_MAX) {
	    LOG_E("DAC7718_Init: Failed to write configuration register");
	    return false;   // #980 item 1
	}

	// Update latch to apply configuration, and report whether it worked
	return DAC7718_UpdateLatch(id);
}

uint32_t DAC7718_ReadWriteReg(uint8_t id, uint8_t RW, uint8_t Reg, uint16_t Data)
{
    uint32_t Com;
    uint32_t rdData = 0;
    uint8_t x;
    bool csAsserted = false;
    // #980 item 2: tracks whether THIS call's own DAC7718_Lock() succeeded.
    // The three validation `goto cleanup` sites below run before Lock() is
    // ever attempted, so without this Unlock() would give a mutex this call
    // never took -- see DAC7718_Unlock()'s comment.
    bool lockHeld = false;
    tDAC7718Config* config = NULL;

    // Validate inputs
    if (RW > 1U) {
        rdData = UINT32_MAX;
        goto cleanup;
    }
    if (Reg > DAC7718_MAX_REGISTER) {
        rdData = UINT32_MAX;
        goto cleanup;
    }

    config = DAC7718_GetConfig(id);
    if (config == NULL) {
        LOG_E("DAC7718_ReadWriteReg: invalid config id=%u", id);
        rdData = UINT32_MAX;
        goto cleanup;
    }

    // Acquire mutex to serialize SPI writes
    if (!DAC7718_Lock()) {
        rdData = UINT32_MAX;
        goto cleanup;
    }
    lockHeld = true;   // set ONLY after a successful take (#980 item 2)

    // Assert CS (active low)
    GPIO_PinWrite(config->CS_Pin, false);
    csAsserted = true;

    // Build 24-bit command
    Com  = (uint32_t)(RW & 0x1U);
    Com <<= 7;
    Com |=  (uint32_t)(Reg & 0x1FU);
    Com <<= 12;
    Com |=  (uint32_t)(Data & DAC7718_MAX_VALUE);
    Com <<= 4;

    // Transmit 24-bit command (MSB first) with timeout protection
    for (x = 0U; x < DAC7718_TRANSFER_BYTES; x++) {
        uint32_t timeout = DAC7718_SPI_TIMEOUT;
        while (((SPI2STAT & _SPI2STAT_SPITBE_MASK) == 0U) && (--timeout > 0U)) { }
        if (timeout == 0U) {
            LOG_E("DAC7718_ReadWriteReg: TX buffer timeout on byte %u", x);
            rdData = UINT32_MAX;
            goto cleanup;
        }
        SPI2BUF = (uint8_t)((Com & 0x00FF0000UL) >> 16);
        Com <<= 8;

        timeout = DAC7718_SPI_TIMEOUT;
        while (((SPI2STAT & _SPI2STAT_SPIRBE_MASK) != 0U) && (--timeout > 0U)) { }
        if (timeout == 0U) {
            LOG_E("DAC7718_ReadWriteReg: RX buffer timeout on byte %u", x);
            rdData = UINT32_MAX;
            goto cleanup;
        }
        (void)SPI2BUF; // clear
    }

    // Wait for shift register to empty (transmission complete)
    uint32_t timeout = DAC7718_SPI_TIMEOUT;
    while (SPI2_IsTransmitterBusy() && (--timeout > 0U)) { }
    if (timeout == 0U) {
        LOG_E("DAC7718_ReadWriteReg: Shift register timeout");
        rdData = UINT32_MAX;
        goto cleanup;
    }
    // De-assert CS after first transaction
    GPIO_PinWrite(config->CS_Pin, true);
    csAsserted = false;

    // Readback if requested
    if (RW == 1U) {
        // Inter-frame delay: DAC7718 requires minimum CS high time between transactions
        // Datasheet specifies minimum 50ns. The CS rising edge latches the read command,
        // and the DAC loads the readback data during this CS high period.
        // Cannot eliminate CS toggle - it's required by the protocol to delimit frames.
        // Using 100ns (2x minimum) provides adequate margin while minimizing overhead.
        // Note: At 200MHz core clock, 100ns = 20 ticks, well within timer resolution.
        DAC7718_Delay_us(1);  // ~1us actual (minimum resolution), datasheet requires 50ns

        Com = 0b000000001000000110100000U; // NOP to clock out data

        spi_txData[0] = (uint8_t)((Com >> 16) & 0xFFU);
        spi_txData[1] = (uint8_t)((Com >> 8)  & 0xFFU);
        spi_txData[2] = (uint8_t)( Com        & 0xFFU);

        // Assert CS for readback transaction
        GPIO_PinWrite(config->CS_Pin, false);
        csAsserted = true;

        for (uint8_t i = 0U; i < DAC7718_TRANSFER_BYTES; i++) {
            uint32_t to = DAC7718_SPI_TIMEOUT;
            while (((SPI2STAT & _SPI2STAT_SPITBE_MASK) == 0U) && (--to > 0U)) { }
            if (to == 0U) {
                LOG_E("DAC7718_ReadWriteReg: NOP TX timeout on byte %u", i);
                rdData = UINT32_MAX;
                goto cleanup;
            }
            SPI2BUF = spi_txData[i];

            to = DAC7718_SPI_TIMEOUT;
            while (((SPI2STAT & _SPI2STAT_SPIRBE_MASK) != 0U) && (--to > 0U)) { }
            if (to == 0U) {
                LOG_E("DAC7718_ReadWriteReg: NOP RX timeout on byte %u", i);
                rdData = UINT32_MAX;
                goto cleanup;
            }
            spi_rxData[i] = (uint8_t)SPI2BUF;
        }

        // Wait for shift register to empty (readback transmission complete)
        timeout = DAC7718_SPI_TIMEOUT;
        while (SPI2_IsTransmitterBusy() && (--timeout > 0U)) { }
        if (timeout == 0U) {
            LOG_E("DAC7718_ReadWriteReg: NOP shift register timeout");
            rdData = UINT32_MAX;
            goto cleanup;
        }
        // De-assert CS after readback transaction
        GPIO_PinWrite(config->CS_Pin, true);
        csAsserted = false;

        // Reconstruct received data
        rdData = (spi_rxData[0] << 16) | (spi_rxData[1] << 8) | spi_rxData[2];
    }

    // Only keep data bits (12-bit data is in bits 15:4)
    rdData = (rdData & (DAC7718_MAX_VALUE << DAC7718_READBACK_SHIFT)) >> DAC7718_READBACK_SHIFT;

cleanup:
    DAC7718_Unlock(config, csAsserted, lockHeld);
    return rdData;
}

bool DAC7718_UpdateLatch(uint8_t id)
{
	// Validate ID
	if (id >= MAX_DAC7718_CONFIG) {
		LOG_E("DAC7718_UpdateLatch: invalid id=%u", id);
		return false;   // #980 item 1
	}

	// Write to configuration register with LD bit set to update all DAC outputs
	// Note: DAC7718_ReadWriteReg handles mutex locking internally
	uint32_t result = DAC7718_ReadWriteReg(id, 0, 0, 0b110000011000);

	if (result == UINT32_MAX) {
		LOG_E("DAC7718_UpdateLatch: Failed to update latch");
		return false;
	}
	return true;
}

// SPI2 configuration is handled by MCC-generated initialization