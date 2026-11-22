#include "nvm.h"
#include <string.h>
#include <xc.h>            /* #1206: _CP0_GET_COUNT() -- the wait deadline's clock */
#include "clock_config.h"  /* #1206: DAQIFI_CORE_TIMER_HZ */
/* #1153: recursive mutex -- see nvm_MutexInit()/nvm_Lock()/nvm_Unlock()
 * doc comments in nvm.h for why recursive and why an explicit init. */
#include "FreeRTOS.h"
#include "semphr.h"

#define UNUSED(x) (void)(x)

volatile static bool gNvmOperationDone=false;

/* #1153: static allocation, matching this codebase's existing recursive-
 * mutex precedent (wifi_manager.c's gProcessStateMutex /
 * gProcessStateMutexBuf) rather than a heap-backed
 * xSemaphoreCreateRecursiveMutex(), so the mutex cannot fail to allocate
 * at runtime. */
static StaticSemaphore_t gNvmMutexBuf;
static SemaphoreHandle_t gNvmMutex = NULL;

static void NvmEventHandler(uintptr_t context)
{
    gNvmOperationDone = true;
}

void nvm_MutexInit(void)
{
    /* Idempotent: a second call must not replace a handle other code may
     * already be holding (see the header doc comment). */
    if (gNvmMutex == NULL) {
        gNvmMutex = xSemaphoreCreateRecursiveMutexStatic(&gNvmMutexBuf);
    }
}

void nvm_Lock(void)
{
    /* ⚠ No NULL-guard / lazy-create here on purpose. A missing
     * nvm_MutexInit() call is a boot-sequencing bug, and the header doc
     * comment on nvm_MutexInit() explains why that should fail loudly
     * (hang/crash at the first NVM access) rather than silently construct
     * a per-caller mutex instance that coordinates with nobody. */
    xSemaphoreTakeRecursive(gNvmMutex, portMAX_DELAY);
}

void nvm_Unlock(void)
{
    xSemaphoreGiveRecursive(gNvmMutex);
}

/* ==========================================================================
 * #1206: every wait in this file is BOUNDED.
 *
 * Before #1206 each of nvm_ErasePage / nvm_WriteWordtoAddr /
 * nvm_WriteRowtoAddr spun on three conditions with no way out but success --
 * DMA quiescing, the NVM controller going idle, and the completion callback
 * -- with DMA SUSPENDED device-wide across all three. A completion that never
 * arrived (e.g. a caller inside a critical section, which masks the
 * priority-1 FLASH_CONTROL interrupt that sets gNvmOperationDone --
 * plib_evic.c's IPC41 line) hung the caller forever AND left every DMA-backed
 * data path stalled, at boot included (app_SystemInit's settings save).
 *
 * THE CLOCK: the CP0 core timer (_CP0_GET_COUNT), not FreeRTOS ticks.
 *   - It advances with interrupts masked and with the scheduler suspended or
 *     not yet started; xTaskGetTickCount() does neither, so a tick-based
 *     deadline would never expire in exactly the critical-section case above.
 *   - It runs at SYSCLK/2 (DAQIFI_CORE_TIMER_HZ, clock_config.h). It is
 *     started by SYS_TIME_Initialize -> CORETIMER_Start (plib_coretimer.c)
 *     inside SYS_Initialize, before any task exists, and nothing else in the
 *     application writes CP0 Count afterwards.
 *   - Elapsed time is computed as unsigned (now - start), which is correct
 *     across the 32-bit wrap as long as the budget is far below 2^32 cycles
 *     (~34 s at 126 MHz); the static assert below enforces < 2^31.
 *
 * THE BUDGET: 250 ms per wait. The slowest operation here is a page erase:
 * DS60001320G Table 37-12 D142 TPGE = 128,453 FRC cycles typical (no max is
 * published) = 16.1 ms at the 8 MHz FRC, and the FRC itself is specified
 * +/-10% over -40..+125 C (Table 37-20 F20) -> ~17.8 ms. A row program is
 * D136 TRW = 66,813 FRC cycles (~8.4 ms), a word program D138 TWW = 383 FRC
 * cycles (~48 us), and a DMA suspend completes within the in-flight bus
 * transfer. 250 ms is therefore >14x the slowest legitimate wait, while still
 * bounding a stuck operation to a fraction of a second. The budget is
 * computed for the BUILT SYSCLK; if the silicon runs slower (e.g. a clock
 * fail-safe fallback to FRC) the core timer slows with it and the wall-clock
 * budget only grows, and 252 MHz is the chip's rated maximum, so it cannot
 * meaningfully shrink.
 *
 * WHAT A TIMEOUT RETURNS: false, through the caller's ordinary failure path.
 * DMA is restored and the recursive mutex released on that path exactly as on
 * success -- each entry point below has ONE exit, after its single
 * nvm_OpEnd() and nvm_Unlock().
 * ========================================================================== */
#define NVM_WAIT_BUDGET_MS   250UL
#define NVM_WAIT_BUDGET_CP0  ((uint32_t)((DAQIFI_CORE_TIMER_HZ / 1000UL) * NVM_WAIT_BUDGET_MS))
_Static_assert(((DAQIFI_CORE_TIMER_HZ / 1000UL) * NVM_WAIT_BUDGET_MS) < 0x80000000UL,
               "NVM wait budget must stay far below the CP0 Count wrap");

typedef bool (*nvm_WaitCondFn)(void);

/* Spin until cond() holds or the budget is spent. The condition is read once
 * more, FRESHLY, at expiry: the caller can be preempted (or the CPU stalled
 * on a flash fetch during the erase itself) for longer than the budget, and
 * an operation that completed during that gap must not be reported as a
 * timeout. Same ordering rule as WaitLoop_SpinThenYield (HAL/WaitLoop.h) --
 * not that helper itself, because these waits must NOT yield: DMA is
 * suspended device-wide for their whole duration and a vTaskDelay() here
 * would stretch that suspension to a scheduler tick or more. */
static bool nvm_WaitUntil(nvm_WaitCondFn cond)
{
    const uint32_t start = _CP0_GET_COUNT();

    while (!cond()) {
        if ((uint32_t)(_CP0_GET_COUNT() - start) >= NVM_WAIT_BUDGET_CP0) {
            return cond();
        }
    }
    return true;
}

static bool nvm_DmaQuiet(void)
{
    return DMACONbits.DMABUSY == 0u;
}

static bool nvm_ControllerIdle(void)
{
    return NVM_IsBusy() == false;
}

/* Completion = the callback has fired AND the controller says WR is clear.
 * WR is set synchronously by NVM_StartOperationAtAddress (plib_nvm.c) before
 * the start call returns and is hardware-cleared on completion
 * (DS60001320G Register 5-1 NVMCON bit 15: "0 = Flash operation is complete
 * or inactive"), so requiring it here means a STALE gNvmOperationDone -- set
 * late by the completion interrupt of an earlier operation whose wait timed
 * out -- cannot end this operation's wait early. nvm_OpBegin() also clears
 * the flag before each start; this covers a stale interrupt that was still
 * pending at that moment and lands after the clear. */
static bool nvm_OperationComplete(void)
{
    return gNvmOperationDone && (NVM_IsBusy() == false);
}

/* Suspend DMA (remembering whether the caller already had it suspended),
 * register the completion callback, and wait for the controller to be idle.
 * Writes *dmaWasSuspended on EVERY path, so nvm_OpEnd() can always restore.
 * Returns false if a wait expired; the caller must then skip the operation
 * but still call nvm_OpEnd(). */
static bool nvm_OpBegin(uint32_t *dmaWasSuspended)
{
    bool ready = true;

    *dmaWasSuspended = DMACONbits.SUSPEND;
    if (!*dmaWasSuspended)
    {
        DMACONSET = _DMACON_SUSPEND_MASK; // suspend
        ready = nvm_WaitUntil(nvm_DmaQuiet); // wait to be actually suspended
    }

    if (ready)
    {
        NVM_CallbackRegister(NvmEventHandler, (uintptr_t)NULL);
        ready = nvm_WaitUntil(nvm_ControllerIdle);
    }

    if (ready)
    {
        gNvmOperationDone = false;
    }
    return ready;
}

/* Wait for the operation just started to complete. */
static bool nvm_OpWaitDone(void)
{
    bool done = nvm_WaitUntil(nvm_OperationComplete);
    gNvmOperationDone = false;
    return done;
}

/* Restore DMA to the state nvm_OpBegin() found it in. */
static void nvm_OpEnd(uint32_t dmaWasSuspended)
{
    if (!dmaWasSuspended)
    {
        DMACONCLR = _DMACON_SUSPEND_MASK; // resume DMA activity
    }
}

bool nvm_ErasePage(uint32_t addr)
{
    uint32_t dmaStatus;           // storage for current DMA state
    uint32_t readWord = 0;
    bool opDone;
    bool result = false;

    nvm_Lock();

    opDone = nvm_OpBegin(&dmaStatus);
    if (opDone)
    {
        /* Erase the Page */
        NVM_PageErase(addr);
        opDone = nvm_OpWaitDone();
    }
    nvm_OpEnd(dmaStatus);

    if (opDone)
    {
        /* #1206: read the WHOLE word. NVM_Read's length is in BYTES (it is a
         * memcpy -- plib_nvm.c), so the previous NVM_Read(&w, 1, addr) filled
         * one byte of an uninitialised word and the comparison below read
         * three bytes of stack. That was invisible while both branches
         * returned true; it is not now that a mismatch reports failure. */
        NVM_Read(&readWord, sizeof(readWord), addr);
        /* #1206: report a failed erase -- e.g. a write-protected page, which
         * the controller refuses and leaves intact -- instead of returning
         * true on both branches, matching the readback verify in
         * nvm_WriteWordtoAddr / nvm_WriteRowtoAddr. */
        result = (readWord == 0xFFFFFFFFu);
    }

    nvm_Unlock();
    return result;
}

bool nvm_WriteWordtoAddr(uint32_t addr, uint32_t data)
{
    uint32_t dmaStatus;           // storage for current DMA state
    bool opDone;
    bool result = false;

    nvm_Lock();

    opDone = nvm_OpBegin(&dmaStatus);
    if (opDone)
    {
        NVM_WordWrite(data,addr);
        opDone = nvm_OpWaitDone();
    }
    nvm_OpEnd(dmaStatus);

    if (opDone)
    {
        // TODO: Trap error/write to error log on a readback mismatch
        result = !memcmp(&data, (uint32_t *) PA_TO_KVA1(addr), sizeof(data));
    }

    nvm_Unlock();
    return result;
}

uint32_t nvm_ReadfromAddr(uint32_t addr)
{
    uint32_t word;
    NVM_Read(&word,1,addr);
    return word;
}

bool nvm_WriteRowtoAddr(uint32_t addr, uint8_t rowdata[])
{
    uint32_t dmaStatus;           // storage for current DMA state
    bool opDone;
    bool result = false;

    nvm_Lock();

    opDone = nvm_OpBegin(&dmaStatus);
    if (opDone)
    {
        /* Write a row of data to address, using data array as the source */
        NVM_RowWrite((uint32_t *)rowdata,addr);
        opDone = nvm_OpWaitDone();
    }
    nvm_OpEnd(dmaStatus);

    if (opDone)
    {
        // TODO: Trap error/write to error log on a readback mismatch
        result = !memcmp(rowdata, (uint32_t *) PA_TO_KVA1(addr), NVM_FLASH_ROWSIZE);
    }

    nvm_Unlock();
    return result;
}
