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
#include "HAL/WaitLoop.h"
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

/* Fast-spin bound before falling back to a yielding wait (#913's twin,
 * #1057). A tight register poll covers the legitimate case with no context
 * switch, mirroring UserSpi.c's spi_WaitStat -- see that function's comment
 * for the full derivation of why 8000 register reads is the right cutover
 * point for a command/response SPI HAL. DAC7718's own worst-case byte is far
 * faster than the case that sizing was chosen for (see
 * DAC7718_SPI_BYTE_TIMEOUT_MS below), so this spin catches every legitimate
 * transfer without ever reaching the yielding path below; reusing the same
 * constant avoids a second unreviewed magic number. */
#define DAC7718_SPI_FAST_SPIN_COUNT 8000u

/* Per-WAIT wall-clock budget in ms, consulted only after the fast spin above
 * misses (#913's twin, #1057) -- "per-wait", not "per-byte": each byte pays
 * TWO of these (TX-buffer-empty, then RX-buffer-not-empty), and each 3-byte
 * frame pays a third for the trailing shift-register-empty wait, so a single
 * DAC7718_ReadWriteReg call's worst-case fault hold is 7x this figure
 * (140 ms) for a plain write (RW=0) and 14x (280 ms) once the RW=1 readback
 * doubles the frame. Still comfortably inside SCPIDAC.c's outer
 * SCPIDAC_COMMAND_LOCK_TIMEOUT_MS (2000 ms), including the 8-channel
 * all-voltage loop -- see the PR description for the exact margin -- but
 * that margin is a function of THIS constant and would need re-checking
 * before raising it.
 *
 * SPI2's baud is fixed, not user-configurable like UserSpi's: SPI2BRG = 5
 * (plib_spi2_master.c:85) off PBCLK2 (84 MHz -- Clock Tree table, CLAUDE.md),
 * so SPI_CLK = 84e6 / (2*(5+1)) = 7 MHz [V, formula per CLAUDE.md's SPI4 BRG
 * derivation -- same SPIx peripheral family]. (The 100 MHz legacy PBCLK2
 * branch that predates #487, still present in clock_config.h, would give
 * 8.33 MHz -- FASTER -- so 84 MHz/7 MHz is the binding, slower worst case
 * across both, same reasoning UserSpi.c's own sizing comment uses.) SPI2BRG
 * is not literally unwritable -- `SPI2_TransferSetup` (plib_spi2_master.c:148)
 * can rewrite it, and a live `DRV_SPI` instance for index 2 exists
 * (initialization.c) -- it is simply never CALLED: nothing in this tree opens
 * `DRV_SPI_INDEX_2`, only 0 and 1 have clients. This constant-BRG premise
 * therefore holds on the absence of a caller, not the absence of a writer; a
 * future SPI2 client changing the baud at runtime would invalidate it.
 *
 * A SPI slave cannot stretch or otherwise gate SCK the way an I2C slave can
 * clock-stretch SCL -- the master (this driver) generates every edge
 * regardless of what DAC7718 does with MISO -- so the worst legitimate byte
 * is a fixed 8 bits / 7 MHz ~= 1.14 us in BOTH directions (this applies to
 * the RW=1 readback path too, which clocks data OUT of the DAC -- DAC7718 is
 * not receive-only). 20 ms is ~17,500x that per-wait margin -- expiry here
 * means SPI2 is not responding at all (peripheral off, PMD-gated, or a
 * wiring fault), never a slow transfer. Numerically equal to UserSpi.c's
 * USER_SPI_BYTE_TIMEOUT_MS for an unrelated reason: that one is sized against
 * a configurable LOW baud floor, this one against this driver's fixed HIGH
 * baud with enormous margin either way; kept equal only because both are
 * "obviously enough" and one fewer number needs explaining. */
#define DAC7718_SPI_BYTE_TIMEOUT_MS 20u

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

/* Wait for a SPI2STAT bit to reach @p want (true = wait for set, false =
 * wait for clear), YIELDING so a stuck SPI2 peripheral does not busy-spin at
 * the caller's (SCPI command) task priority for the whole budget (#913's
 * twin, #1057).
 *
 * THE LOOP ITSELF LIVES IN HAL/WaitLoop.h (#1108, finishing what #1056
 * started), one definition shared with spi_WaitStat, uart_WaitSta and
 * i2c_WaitMif rather than a fourth copy of the same shape. #913's ordering
 * guarantee -- the status is read before the budget is ever consulted, and
 * ONCE MORE, freshly, at expiry, so a bit that set while this task was
 * preempted still counts as success -- is stated and EXERCISED there:
 * tests/host/test_1056_wait_loop.c compiles that header for real, which is
 * what retires the grep guard + modelled copy (test_1057_dac7718_wait_stat.c)
 * that stood in for it here. What stays below is what is specific to SPI2:
 * the status read, the tick budget, and the spin bound.
 *
 * WHY THE FAST SPIN IS NO LONGER HOISTED OUT OF THE RETRY LOOP. #1057 shipped
 * a variant shape -- one fast spin at entry, then a retry loop doing a SINGLE
 * status read per 1 ms wake -- answering a Qodo /improve finding that
 * re-spinning 8000 times per wake buys no extra detection for a
 * level-sensitive bit. Folding onto the shared loop gives that up
 * [V: WaitLoop.h's WaitLoop_SpinThenYield spins inside its outer `for (;;)`,
 * as it already did for the three drivers it serves]. The trade [I -- a
 * judgement call, recorded so the next reader need not re-derive it]: the
 * hoist is invisible on every successful transfer, because a legitimate byte
 * is ~1.14 us and returns inside the FIRST spin without ever reaching a retry
 * (see DAC7718_SPI_BYTE_TIMEOUT_MS above), so it only ever applied once SPI2
 * was genuinely not responding -- a path bounded by the same 20 ms budget and
 * ending in the same logged error + UINT32_MAX either way. What it saved
 * there was duty cycle, not latency: ~8000 register reads (~400 us, from
 * UserSpi.c's own explicitly-estimated sizing of 8000 as about one 20 kHz
 * byte) per 1 ms pass instead of one. Buying that back needs a second entry
 * point in a header three other drivers depend on -- a second wait shape with
 * exactly one caller, covered by test_1056_wait_loop.c's mutation suite only
 * if that suite grows a whole second set of cases -- which is the drift #1056
 * existed to remove. One shape wins. If the re-spin duty is worth reclaiming,
 * hoist it ONCE in WaitLoop.h for all four callers, with its own mutation
 * coverage; do not keep a private copy here.
 *
 * The one shape this collapses that the other three drivers don't have:
 * DAC7718's "shift register empty" wait was previously expressed via the
 * PLIB helper SPI2_IsTransmitterBusy(), which is
 * `(SPI2STAT & _SPI2STAT_SRMT_MASK) == 0` (plib_spi2_master.c:167) --
 * i.e. NOT busy is the SRMT bit SET. [V, PIC32 Family Reference Manual
 * Section 23 "Serial Peripheral Interface (SPI)", DS61106G, Register 23-3
 * (SPIxSTAT), bit 7: "SRMT: Shift Register Empty bit (valid only when
 * ENHBUF = 1) -- 1 = When SPI module shift register is empty, 0 = When SPI
 * module shift register is not empty." SRMT's ENHBUF precondition is met
 * here: SPI2_Initialize sets ENHBUF=1 (plib_spi2_master.c:99).] Testing
 * that mask/want pair here directly, rather than calling the PLIB function
 * and inverting the sense, lets all three of this driver's wait shapes
 * (TX-buffer-empty, RX-buffer-not-empty, shift-register-empty) route
 * through this one helper instead of two different waiting idioms -- the
 * design choice #1057 made over routing this one condition through
 * SPI2_IsTransmitterBusy() and keeping two wait idioms (Qodo /agentic_review,
 * declined with rationale on the PR). */
typedef struct {
    uint32_t   mask;
    bool       want;
    TickType_t start;
    TickType_t timeoutTicks;
} Dac7718WaitCtx_t;

static bool dac7718_WaitBitMet(void* ctx) {
    const Dac7718WaitCtx_t* w = (const Dac7718WaitCtx_t*)ctx;
    return ((SPI2STAT & w->mask) != 0u) == w->want;
}

static bool dac7718_WaitBudgetSpent(void* ctx) {
    const Dac7718WaitCtx_t* w = (const Dac7718WaitCtx_t*)ctx;
    /* Rollover-safe: unsigned (now - start) is the true elapsed count even
     * across a tick-counter wrap, unlike an absolute-deadline compare. */
    return (TickType_t)(xTaskGetTickCount() - w->start) >= w->timeoutTicks;
}

static void dac7718_WaitYield(void* ctx) {
    (void)ctx;
    vTaskDelay(1);
}

static bool dac7718_WaitStat(uint32_t mask, bool want,
                             TickType_t start, TickType_t timeoutTicks)
{
    Dac7718WaitCtx_t w = { mask, want, start, timeoutTicks };
    /* Named directly, never through a variable, so the optimiser's
     * inline -> constant-propagate -> devirtualise chain applies and the
     * spin keeps costing one register read per iteration (WaitLoop.h's
     * "COST" paragraph). */
    return WaitLoop_SpinThenYield(dac7718_WaitBitMet, dac7718_WaitBudgetSpent,
                                  dac7718_WaitYield, &w,
                                  DAC7718_SPI_FAST_SPIN_COUNT);
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
        if (!dac7718_WaitStat(_SPI2STAT_SPITBE_MASK, true, xTaskGetTickCount(),
                               pdMS_TO_TICKS(DAC7718_SPI_BYTE_TIMEOUT_MS))) {
            LOG_E("DAC7718_ReadWriteReg: TX buffer timeout on byte %u", x);
            rdData = UINT32_MAX;
            goto cleanup;
        }
        SPI2BUF = (uint8_t)((Com & 0x00FF0000UL) >> 16);
        Com <<= 8;

        if (!dac7718_WaitStat(_SPI2STAT_SPIRBE_MASK, false, xTaskGetTickCount(),
                               pdMS_TO_TICKS(DAC7718_SPI_BYTE_TIMEOUT_MS))) {
            LOG_E("DAC7718_ReadWriteReg: RX buffer timeout on byte %u", x);
            rdData = UINT32_MAX;
            goto cleanup;
        }
        (void)SPI2BUF; // clear
    }

    // Wait for shift register to empty (transmission complete)
    if (!dac7718_WaitStat(_SPI2STAT_SRMT_MASK, true, xTaskGetTickCount(),
                           pdMS_TO_TICKS(DAC7718_SPI_BYTE_TIMEOUT_MS))) {
        LOG_E("DAC7718_ReadWriteReg: Shift register timeout");
        rdData = UINT32_MAX;
        goto cleanup;
    }
    // De-assert CS after first transaction
    GPIO_PinWrite(config->CS_Pin, true);
    csAsserted = false;

    // Readback if requested
    if (RW == 1U) {
        // Inter-frame delay: DAC7718 requires minimum CS high time between transactions.
        // Datasheet (SBAS361A, t8 "CS high time") specifies a minimum of 10/19/28 ns at
        // IOVDD = 5/3/1.8 V. The CS rising edge latches the read command,
        // and the DAC loads the readback data during this CS high period.
        // Cannot eliminate CS toggle - it's required by the protocol to delimit frames.
        // DAC7718_Delay_us(1) below waits ~1us -- the function's granularity is
        // whole microseconds, and it converts them to core-timer ticks at the
        // configured frequency -- far above that minimum at every IOVDD.
        DAC7718_Delay_us(1);  // ~1us actual (minimum resolution); datasheet t8 min is <=28ns

        Com = 0b000000001000000110100000U; // NOP to clock out data

        spi_txData[0] = (uint8_t)((Com >> 16) & 0xFFU);
        spi_txData[1] = (uint8_t)((Com >> 8)  & 0xFFU);
        spi_txData[2] = (uint8_t)( Com        & 0xFFU);

        // Assert CS for readback transaction
        GPIO_PinWrite(config->CS_Pin, false);
        csAsserted = true;

        for (uint8_t i = 0U; i < DAC7718_TRANSFER_BYTES; i++) {
            if (!dac7718_WaitStat(_SPI2STAT_SPITBE_MASK, true, xTaskGetTickCount(),
                                   pdMS_TO_TICKS(DAC7718_SPI_BYTE_TIMEOUT_MS))) {
                LOG_E("DAC7718_ReadWriteReg: NOP TX timeout on byte %u", i);
                rdData = UINT32_MAX;
                goto cleanup;
            }
            SPI2BUF = spi_txData[i];

            if (!dac7718_WaitStat(_SPI2STAT_SPIRBE_MASK, false, xTaskGetTickCount(),
                                   pdMS_TO_TICKS(DAC7718_SPI_BYTE_TIMEOUT_MS))) {
                LOG_E("DAC7718_ReadWriteReg: NOP RX timeout on byte %u", i);
                rdData = UINT32_MAX;
                goto cleanup;
            }
            spi_rxData[i] = (uint8_t)SPI2BUF;
        }

        // Wait for shift register to empty (readback transmission complete)
        if (!dac7718_WaitStat(_SPI2STAT_SRMT_MASK, true, xTaskGetTickCount(),
                               pdMS_TO_TICKS(DAC7718_SPI_BYTE_TIMEOUT_MS))) {
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