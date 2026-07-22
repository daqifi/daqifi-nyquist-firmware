/**
 * @file UserEdge.c
 * @brief Edge events (INT1..4) + hardware pulse-count totalizers (Timer8/9) on the
 *        DIO terminal (#667, epic #664). Hand-rolled — no Harmony driver exists for
 *        either external-interrupt edge events or an external-clock totalizer.
 *
 * Register semantics are device-header / FRM verified (DS60001108 INT, DS60001105
 * TMR, plib_gpio's shipping CN handler as the interrupt-idiom reference):
 *   - INTx: single INTxR PPS input select, INTCON.INTxEP edge polarity (1 = rising,
 *     0 = falling). Both-edge is emulated by flipping INTxEP in the ISR.
 *   - Timer8/9: TxCON.TCS=1 selects the external TxCK pin as the clock, TCKPS=0
 *     counts every edge, PR=0xFFFF so a rollover interrupt fires each 65536 counts
 *     to extend the 16-bit hardware count to 48 bits. Timer8/9 are held in PMD at
 *     boot (PMD4<T8MD,T9MD>) and MUST be PMD-ungated before their SFRs respond.
 *   - Timestamps come from TMR6 read directly — the streaming timebase
 *     (TSTimerIndex = TMR_INDEX_6), so an edge and an ADC sample carry the same
 *     clock and correlate by construction. TMR6 only runs while streaming, so a
 *     timestamp is meaningful during a stream; the edge COUNT is the idle signal.
 *
 * Concurrency: the four INT ISRs and the two rollover ISRs all run at priority 3.
 * Equal-priority interrupts do not preempt one another on this MIPS core, so the
 * shared event FIFO and the per-source counters have a single writer at any instant
 * among the ISRs. The task side reads 64-bit counters and pops the FIFO under
 * taskENTER_CRITICAL, which raises IPL above 3 and blocks those ISRs, making the
 * multi-word reads coherent (per the project atomicity rules). Arm/disarm run under
 * a mutex so USB (pri 7) and WiFi (pri 2) SCPI cannot race the PPS/ownership setup.
 */
#define LOG_LVL     LOG_LEVEL_ERROR
#define LOG_MODULE  LOG_MODULE_GENERAL

#include "UserEdge.h"
#include "configuration.h"
#include "definitions.h"
#include "HAL/DIO.h"
#include "services/streaming.h"
#include "peripheral/coretimer/plib_coretimer.h"
#include "Util/Logger.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

#define USER_EDGE_INT_UNITS   4u
#define USER_EDGE_CTR_UNITS   2u
/* Recent-events correlation window (drop-oldest). Kept small because the device
 * is at the RAM edge; the per-pin COUNT and the hardware totalizer are the
 * lossless primary data, the FIFO is a bounded window of the most recent edges. */
#define USER_EDGE_FIFO_LEN    16u
#define USER_EDGE_STORM_MAX   32u   /* edges per 1 ms window before auto-mute */

/* Per-pin PPS input-select code (shared by INTxR and TxCKR — a pin's input code is
 * fixed within its PPS group). Cross-checked vs the #666 IC pin map. */
typedef struct { uint8_t dio; uint8_t code; } EdgePinCode_t;

/* INT unit -> reachable {DIO, code}. One active pin per unit (shared-unit group). */
static const EdgePinCode_t gInt1Pins[] = { { 0u, 0u }, { 11u, 12u } };               /* group 4 */
static const EdgePinCode_t gInt2Pins[] = { { 3u, 10u }, { 12u, 6u } };               /* group 3 */
static const EdgePinCode_t gInt3Pins[] = { { 5u, 4u }, { 7u, 12u }, { 15u, 10u } };  /* group 1 */
static const EdgePinCode_t gInt4Pins[] = { { 2u, 0u }, { 4u, 4u }, { 6u, 12u }, { 14u, 6u } }; /* group 2 */
/* Totalizer T8CK/T9CK -> reachable {DIO, code} (same pins/codes as INT2/INT1). */
static const EdgePinCode_t gCtr8Pins[] = { { 3u, 10u }, { 12u, 6u } };               /* group 3 */
static const EdgePinCode_t gCtr9Pins[] = { { 0u, 0u }, { 11u, 12u } };               /* group 4 */

typedef struct {
    volatile uint32_t* rpr;     /* INTxR PPS input select */
    uint32_t epMask;            /* INTCON.INTxEP (edge polarity) */
    uint32_t ieMask;            /* IEC0.INTxIE */
    uint32_t ifMask;            /* IFS0.INTxIF */
    const EdgePinCode_t* pins;
    uint8_t  nPins;
} EdgeIntUnit_t;

static const EdgeIntUnit_t gInt[USER_EDGE_INT_UNITS] = {
    { &INT1R, _INTCON_INT1EP_MASK, _IEC0_INT1IE_MASK, _IFS0_INT1IF_MASK, gInt1Pins, 2u },
    { &INT2R, _INTCON_INT2EP_MASK, _IEC0_INT2IE_MASK, _IFS0_INT2IF_MASK, gInt2Pins, 2u },
    { &INT3R, _INTCON_INT3EP_MASK, _IEC0_INT3IE_MASK, _IFS0_INT3IF_MASK, gInt3Pins, 3u },
    { &INT4R, _INTCON_INT4EP_MASK, _IEC0_INT4IE_MASK, _IFS0_INT4IF_MASK, gInt4Pins, 4u },
};

typedef struct {
    volatile uint32_t* con;     /* TxCON  */
    volatile uint32_t* tmr;     /* TMRx   (16-bit hardware count) */
    volatile uint32_t* pr;      /* PRx    */
    volatile uint32_t* rpr;     /* TxCKR PPS input select */
    uint32_t pmdMask;           /* PMD4<TxMD> */
    uint32_t ieMask;            /* IEC1.TxIE */
    uint32_t ifMask;            /* IFS1.TxIF */
    const EdgePinCode_t* pins;
    uint8_t  nPins;
} EdgeCtrUnit_t;

/* TxCON bit layout is identical across all Type-B timers, so the _T8CON_* masks
 * apply to T9CON too. */
static const EdgeCtrUnit_t gCtr[USER_EDGE_CTR_UNITS] = {
    { &T8CON, &TMR8, &PR8, &T8CKR, _PMD4_T8MD_MASK, _IEC1_T8IE_MASK, _IFS1_T8IF_MASK, gCtr8Pins, 2u },
    { &T9CON, &TMR9, &PR9, &T9CKR, _PMD4_T9MD_MASK, _IEC1_T9IE_MASK, _IFS1_T9IF_MASK, gCtr9Pins, 2u },
};

/* enabled/stormed/dio/mode are written by the arming task and read by the INT
 * ISR, so they are volatile (cross-context visibility + ordering vs the volatile
 * IEC0SET that enables the interrupt — a non-volatile store could legally sink
 * past it and let an early edge see enabled==false). */
typedef struct {
    volatile bool     enabled;
    volatile bool     stormed;     /* auto-muted by the storm guard */
    volatile uint8_t  dio;         /* active pin (shared-unit mismatch guard) */
    volatile uint8_t  mode;        /* UserEdgeMode_t */
    volatile uint64_t count;       /* edge count (ISR is sole writer) */
    volatile uint32_t winStart;    /* storm-window start (core-timer count) */
    volatile uint16_t winEdges;    /* edges in the current window */
} EdgeIntState_t;

typedef struct {
    bool              enabled;
    uint8_t           dio;
    volatile uint32_t high;        /* rollover epochs (each = 65536 counts) */
} EdgeCtrState_t;

static EdgeIntState_t gIntState[USER_EDGE_INT_UNITS];
static EdgeCtrState_t gCtrState[USER_EDGE_CTR_UNITS];

/* Shared timestamped event FIFO (drop-oldest). ISR pushes, task pops. One ring
 * slot is reserved to distinguish full from empty, so up to LEN-1 (15) events
 * are pending; overflow drop-oldest is counted in gFifoDropped and surfaced via
 * DIO:EVENt:NEXT?. */
typedef struct { uint8_t dio; uint8_t edge; uint32_t ts; } EdgeEvent_t;
static volatile EdgeEvent_t gFifo[USER_EDGE_FIFO_LEN];
static volatile uint8_t     gFifoHead;    /* next push slot */
static volatile uint8_t     gFifoTail;    /* next pop slot  */
static volatile uint32_t    gFifoDropped;

static uint32_t gStormWindowTicks = 126000u;   /* 1 ms @ 126 MHz core timer */

static SemaphoreHandle_t gMutex;
static StaticSemaphore_t gMutexBuf;

static SemaphoreHandle_t edge_Mutex(void) {
    if (gMutex == NULL) {
        taskENTER_CRITICAL();
        if (gMutex == NULL) { gMutex = xSemaphoreCreateMutexStatic(&gMutexBuf); }
        taskEXIT_CRITICAL();
    }
    return gMutex;
}

/* ------------------------------------------------------------------ */
/* Lookups */

static int edge_IntUnitForDio(uint8_t dio) {
    for (uint8_t u = 0; u < USER_EDGE_INT_UNITS; u++) {
        for (uint8_t i = 0; i < gInt[u].nPins; i++) {
            if (gInt[u].pins[i].dio == dio) { return (int)u; }
        }
    }
    return -1;
}

static int edge_CtrUnitForDio(uint8_t dio) {
    for (uint8_t u = 0; u < USER_EDGE_CTR_UNITS; u++) {
        for (uint8_t i = 0; i < gCtr[u].nPins; i++) {
            if (gCtr[u].pins[i].dio == dio) { return (int)u; }
        }
    }
    return -1;
}

static uint8_t edge_LookupCode(const EdgePinCode_t* pins, uint8_t n, uint8_t dio) {
    for (uint8_t i = 0; i < n; i++) {
        if (pins[i].dio == dio) { return pins[i].code; }
    }
    return 0u;   /* not reached: callers resolve the unit (and thus the pin) first */
}

/* True if an edge-event INT unit is currently armed on @p dio. */
static bool edge_EventActiveOnPin(uint8_t dio) {
    int u = edge_IntUnitForDio(dio);
    return (u >= 0) && gIntState[u].enabled && (gIntState[u].dio == dio);
}

/* True if a totalizer is currently armed on @p dio. */
static bool edge_CounterActiveOnPin(uint8_t dio) {
    int u = edge_CtrUnitForDio(dio);
    return (u >= 0) && gCtrState[u].enabled && (gCtrState[u].dio == dio);
}

/* ------------------------------------------------------------------ */
/* PPS / PMD register plumbing (plib_nvm SYSKEY idiom, interrupts off) */

static void edge_CfgconUnlockedWrite(uint32_t val) {
    SYSKEY = 0x00000000U;
    SYSKEY = 0xAA996655U;
    SYSKEY = 0x556699AAU;
    CFGCON = val;
    SYSKEY = 0x33333333U;
}

static void edge_SetPps(volatile uint32_t* rpr, uint8_t code) {
    uint32_t st = __builtin_disable_interrupts();
    edge_CfgconUnlockedWrite(CFGCON & ~(uint32_t)_CFGCON_IOLOCK_MASK);
    *rpr = (uint32_t)code;
    edge_CfgconUnlockedWrite(CFGCON | (uint32_t)_CFGCON_IOLOCK_MASK);
    __builtin_mtc0(12, 0, st);
}

/* Ungate (enable) / re-gate a totalizer timer's PMD4 bit. A PMD-gated timer
 * ignores SFR writes and reads 0, so this MUST precede any TxCON/TMRx write. */
static void edge_CtrSetPmd(uint8_t u, bool enable) {
    uint32_t st = __builtin_disable_interrupts();
    edge_CfgconUnlockedWrite(CFGCON & ~(uint32_t)_CFGCON_PMDLOCK_MASK);
    if (enable) { PMD4CLR = gCtr[u].pmdMask; } else { PMD4SET = gCtr[u].pmdMask; }
    edge_CfgconUnlockedWrite(CFGCON | (uint32_t)_CFGCON_PMDLOCK_MASK);
    __builtin_mtc0(12, 0, st);
    for (volatile int d = 0; enable && d < 8; d++) { /* let the module clock settle */ }
}

static bool edge_Streaming(void) {
    return Streaming_IsActiveOnNonWifiInterface() || Streaming_IsActiveOnWifiInterface();
}

/* Re-assert INTx / TMR8-9-rollover interrupt priority (3, <= FreeRTOS syscall 4;
 * subpriority 0). The identical write in UserEdge_Initialize runs in the pri-1
 * boot task and does NOT persist on this silicon: the IPCn store reads back 0,
 * leaving the vector at priority 0, which never fires — the exact #702
 * input-capture root cause (edges reach the peripheral and the FIFO/hardware
 * counter fills, but the deferring ISR is never entered → EVENt count stuck at
 * 0, COUNter's 64-bit rollover extension dead). The same store from a live SCPI
 * task sticks, so call these at ARM time, right before the matching IECxSET.
 * Mirrors UserIC ic_SetPriority (the proven #702 fix). */
static void edge_SetIntPriority(uint8_t u) {
    switch (u) {
        case 0: IPC2bits.INT1IP = 3; IPC2bits.INT1IS = 0; break;
        case 1: IPC3bits.INT2IP = 3; IPC3bits.INT2IS = 0; break;
        case 2: IPC4bits.INT3IP = 3; IPC4bits.INT3IS = 0; break;
        case 3: IPC5bits.INT4IP = 3; IPC5bits.INT4IS = 0; break;
        default: break;
    }
}

static void edge_SetCtrPriority(uint8_t u) {
    switch (u) {
        case 0: IPC9bits.T8IP  = 3; IPC9bits.T8IS  = 0; break;
        case 1: IPC10bits.T9IP = 3; IPC10bits.T9IS = 0; break;
        default: break;
    }
}

/* ------------------------------------------------------------------ */
/* FIFO (single ISR writer at a time; task pops under a critical section) */

static void edge_FifoPush(uint8_t dio, uint32_t ts, uint8_t edge) {
    uint8_t nh = (uint8_t)((gFifoHead + 1u) % USER_EDGE_FIFO_LEN);
    if (nh == gFifoTail) {   /* full -> drop the oldest */
        gFifoTail = (uint8_t)((gFifoTail + 1u) % USER_EDGE_FIFO_LEN);
        gFifoDropped++;
    }
    gFifo[gFifoHead].dio  = dio;
    gFifo[gFifoHead].ts   = ts;
    gFifo[gFifoHead].edge = edge;
    gFifoHead = nh;
}

/* ------------------------------------------------------------------ */
/* Public API */

void UserEdge_Initialize(void) {
    /* Park INT1..4: disabled, flags cleared, priority 3 (<= FreeRTOS syscall 4),
     * subpriority 0. The interrupt-controller SFRs are not PMD-gated. */
    IEC0CLR = _IEC0_INT1IE_MASK | _IEC0_INT2IE_MASK | _IEC0_INT3IE_MASK | _IEC0_INT4IE_MASK;
    IFS0CLR = _IFS0_INT1IF_MASK | _IFS0_INT2IF_MASK | _IFS0_INT3IF_MASK | _IFS0_INT4IF_MASK;
    /* Defensive parking only — this boot-task write does not persist (see
     * edge_SetIntPriority); the arm path re-asserts and is authoritative. */
    for (uint8_t u = 0; u < USER_EDGE_INT_UNITS; u++) { edge_SetIntPriority(u); }

    /* Park Timer8/9 rollover interrupts (the timers stay PMD-gated until armed). */
    IEC1CLR = _IEC1_T8IE_MASK | _IEC1_T9IE_MASK;
    IFS1CLR = _IFS1_T8IF_MASK | _IFS1_T9IF_MASK;
    for (uint8_t u = 0; u < USER_EDGE_CTR_UNITS; u++) { edge_SetCtrPriority(u); }

    gFifoHead = 0u; gFifoTail = 0u; gFifoDropped = 0u;
    for (uint8_t u = 0; u < USER_EDGE_INT_UNITS; u++) {
        gIntState[u].enabled = false; gIntState[u].stormed = false;
        gIntState[u].dio = 0u; gIntState[u].mode = 0u; gIntState[u].count = 0u;
        gIntState[u].winStart = 0u; gIntState[u].winEdges = 0u;
    }
    for (uint8_t u = 0; u < USER_EDGE_CTR_UNITS; u++) {
        gCtrState[u].enabled = false; gCtrState[u].dio = 0u; gCtrState[u].high = 0u;
    }

    uint32_t f = CORETIMER_FrequencyGet();
    gStormWindowTicks = (f >= 1000u) ? (f / 1000u) : 126000u;   /* 1 ms window */

    /* Eager static create (mirrors UserIC #409 note): a non-zeroed-BSS reset must
     * not leave gMutex pointing at a stale taken semaphore. */
    gMutex = xSemaphoreCreateMutexStatic(&gMutexBuf);
}

/* Configure an INT unit's edge polarity + reset its counters. IEC left DISABLED —
 * the caller enables it last, after gIntState[u].enabled is set, so no edge is
 * delivered against half-initialized state. */
static void edge_ConfigInt(uint8_t u, uint8_t dio, uint8_t mode) {
    const EdgeIntUnit_t* r = &gInt[u];
    IEC0CLR = r->ieMask;                                        /* this unit's ISR can't run now */
    if (mode == USER_EDGE_FALLING) {
        INTCONCLR = r->epMask;                                  /* falling */
    } else if (mode == USER_EDGE_BOTH) {
        /* Both-edge is emulated by flipping INTxEP per edge in the ISR, so the
         * FIRST polarity must match the NEXT expected transition or that edge is
         * missed (INTx is edge-triggered; a pin held HIGH at arm then driven LOW
         * presents a falling edge while armed for rising -> no interrupt). Arm
         * from the current level: HIGH -> expect falling next; LOW -> rising.
         * (Audit #705.) Default to rising if the level can't be read. */
        bool high = false;
        (void)DIO_ReadChannelLevel(dio, &high);
        if (high) { INTCONCLR = r->epMask; } else { INTCONSET = r->epMask; }
    } else {
        INTCONSET = r->epMask;                                  /* rising */
    }
    /* Reset the shared state under a critical section: the 64-bit count store must
     * be atomic against a concurrent cross-task getter (this unit's ISR is already
     * IEC-disabled, but UserEdge_EventCount runs on another task). */
    uint32_t now = CORETIMER_CounterGet();
    taskENTER_CRITICAL();
    gIntState[u].mode = mode;
    gIntState[u].count = 0u;
    gIntState[u].stormed = false;
    gIntState[u].winStart = now;
    gIntState[u].winEdges = 0u;
    taskEXIT_CRITICAL();
    IFS0CLR = r->ifMask;
}

bool UserEdge_EventEnable(uint8_t dio, uint8_t mode, const char** err) {
    if (mode > (uint8_t)USER_EDGE_BOTH) {
        if (err) { *err = "DIO:EVENt: mode must be 0=off,1=rising,2=falling,3=both"; }
        return false;
    }
    int u = edge_IntUnitForDio(dio);
    if (u < 0) {
        if (err) { *err = "DIO:EVENt: pin is not edge-event reachable (use 0/2/3/4/5/6/7/11/12/14/15)"; }
        return false;
    }
    const EdgeIntUnit_t* r = &gInt[u];
    uint8_t code = edge_LookupCode(r->pins, r->nPins, dio);
    xSemaphoreTake(edge_Mutex(), portMAX_DELAY);
    bool ok = true;
    if (edge_Streaming()) {
        /* Re-checked under the mutex. Best-effort: stream start does not take this
         * mutex, but a stream only READS TMR6 and never touches INT1-4/T8-9 or these
         * pins, so a start racing an in-flight arm is benign. */
        xSemaphoreGive(gMutex);
        if (err) { *err = "DIO:EVENt: cannot arm/disarm while streaming"; }
        return false;
    }
    if (mode != (uint8_t)USER_EDGE_OFF) {
        if (gIntState[u].enabled && gIntState[u].dio != dio) {
            /* shared-unit: a different pin of this INT group is active (#9 guard) */
            if (err) { *err = "DIO:EVENt: this INT unit is busy on another pin of its group"; }
            ok = false;
        } else {
            bool fresh = !gIntState[u].enabled;
            if (fresh && edge_CounterActiveOnPin(dio)) {
                /* Events and totalizers share DIO_OWNER_IC, so a same-owner claim
                 * would succeed idempotently — cross-check the sibling family here
                 * (the #9 shared-pin guard extended ACROSS families). */
                if (err) { *err = "DIO:EVENt: pin is an active totalizer (disable DIO:COUNter first)"; }
                ok = false;
            } else if (fresh && !DIO_ClaimChannel(dio, DIO_OWNER_IC)) {
                if (err) { *err = "DIO:EVENt: pin is owned by another peripheral"; }
                ok = false;
            } else {
                if (fresh) {
                    DIO_SetChannelPeripheralInput(dio);   /* buffer high-Z; read the terminal */
                    edge_SetPps(r->rpr, code);            /* route the pin to INTx */
                }
                edge_ConfigInt((uint8_t)u, dio, mode);
                gIntState[u].dio = dio;
                gIntState[u].enabled = true;
                edge_SetIntPriority((uint8_t)u);          /* boot IPC write doesn't persist (#702) */
                IEC0SET = r->ieMask;                      /* enable last */
            }
        }
    } else {   /* disarm */
        if (gIntState[u].enabled) {
            if (gIntState[u].dio != dio) {
                if (err) { *err = "DIO:EVENt: DIO is not the active event pin for its INT unit"; }
                ok = false;
            } else {
                IEC0CLR = r->ieMask;
                IFS0CLR = r->ifMask;
                edge_SetPps(r->rpr, 0u);
                gIntState[u].enabled = false;
                gIntState[u].stormed = false;
                DIO_ReleaseChannel(dio, DIO_OWNER_IC);
                DIO_RestoreChannel(dio);
            }
        }
        /* disarming an inactive pin is a no-op success */
    }
    xSemaphoreGive(gMutex);
    return ok;
}

uint8_t UserEdge_EventMode(uint8_t dio) {
    int u = edge_IntUnitForDio(dio);
    if (u < 0) { return 0u; }
    uint8_t m = 0u;
    /* gMutex serializes vs a concurrent arm/disarm (another SCPI task) so the read
     * can't observe a half-applied transition; the critical section keeps the read
     * coherent vs the INT ISR. */
    xSemaphoreTake(edge_Mutex(), portMAX_DELAY);
    taskENTER_CRITICAL();
    if (gIntState[u].enabled && gIntState[u].dio == dio) { m = gIntState[u].mode; }
    taskEXIT_CRITICAL();
    xSemaphoreGive(gMutex);
    return m;
}

uint64_t UserEdge_EventCount(uint8_t dio) {
    int u = edge_IntUnitForDio(dio);
    if (u < 0) { return 0u; }
    uint64_t c = 0u;
    xSemaphoreTake(edge_Mutex(), portMAX_DELAY);
    taskENTER_CRITICAL();
    if (gIntState[u].enabled && gIntState[u].dio == dio) { c = gIntState[u].count; }
    taskEXIT_CRITICAL();
    xSemaphoreGive(gMutex);
    return c;
}

bool UserEdge_EventNext(uint8_t* dio, uint32_t* ts, uint8_t* edge, uint32_t* dropped) {
    bool got = false;
    taskENTER_CRITICAL();
    if (dropped != NULL) { *dropped = gFifoDropped; }   /* cumulative drop-oldest losses */
    if (gFifoHead != gFifoTail) {
        uint8_t t = gFifoTail;
        if (dio != NULL)  { *dio = gFifo[t].dio; }
        if (ts != NULL)   { *ts = gFifo[t].ts; }
        if (edge != NULL) { *edge = gFifo[t].edge; }
        gFifoTail = (uint8_t)((t + 1u) % USER_EDGE_FIFO_LEN);
        got = true;
    }
    taskEXIT_CRITICAL();
    return got;
}

bool UserEdge_CounterEnable(uint8_t dio, bool on, const char** err) {
    int u = edge_CtrUnitForDio(dio);
    if (u < 0) {
        if (err) { *err = "DIO:COUNter: pin is not totalizer-reachable (use 0/3/11/12)"; }
        return false;
    }
    const EdgeCtrUnit_t* c = &gCtr[u];
    uint8_t code = edge_LookupCode(c->pins, c->nPins, dio);
    xSemaphoreTake(edge_Mutex(), portMAX_DELAY);
    bool ok = true;
    if (edge_Streaming()) {
        /* Re-checked under the mutex (best-effort — see UserEdge_EventEnable). */
        xSemaphoreGive(gMutex);
        if (err) { *err = "DIO:COUNter: cannot arm/disarm while streaming"; }
        return false;
    }
    if (on) {
        if (gCtrState[u].enabled && gCtrState[u].dio != dio) {
            if (err) { *err = "DIO:COUNter: this timer is busy on the other group pin"; }
            ok = false;
        } else if (gCtrState[u].enabled) {
            /* idempotent — already counting this pin */
        } else if (edge_EventActiveOnPin(dio)) {
            /* cross-family guard: the pin is a live edge-event source (shared
             * DIO_OWNER_IC — see UserEdge_EventEnable). */
            if (err) { *err = "DIO:COUNter: pin is an active event source (disable DIO:EVENt first)"; }
            ok = false;
        } else if (!DIO_ClaimChannel(dio, DIO_OWNER_IC)) {
            if (err) { *err = "DIO:COUNter: pin is owned by another peripheral"; }
            ok = false;
        } else {
            DIO_SetChannelPeripheralInput(dio);
            edge_SetPps(c->rpr, code);            /* route the pin to TxCK */
            edge_CtrSetPmd((uint8_t)u, true);     /* power the timer before its SFRs */
            gCtrState[u].high = 0u;
            *(c->con) = 0u;
            *(c->tmr) = 0u;
            *(c->pr)  = 0xFFFFu;                  /* rollover interrupt each 65536 */
            IFS1CLR = c->ifMask;
            edge_SetCtrPriority((uint8_t)u);      /* boot IPC write doesn't persist (#702) */
            IEC1SET = c->ieMask;
            *(c->con) = _T8CON_TCS_MASK;          /* external clock on TxCK, 1:1, 16-bit */
            *(c->con) |= _T8CON_ON_MASK;
            if ((*(c->con) & _T8CON_ON_MASK) == 0u) {
                /* liveness: a still-PMD-gated timer can't latch ON — fail cleanly */
                *(c->con) = 0u;
                IEC1CLR = c->ieMask;
                edge_CtrSetPmd((uint8_t)u, false);
                edge_SetPps(c->rpr, 0u);
                DIO_ReleaseChannel(dio, DIO_OWNER_IC);
                DIO_RestoreChannel(dio);
                if (err) { *err = "DIO:COUNter: timer failed to power up (PMD)"; }
                ok = false;
            } else {
                gCtrState[u].dio = dio;
                gCtrState[u].enabled = true;
            }
        }
    } else {   /* disable */
        if (gCtrState[u].enabled) {
            if (gCtrState[u].dio != dio) {
                if (err) { *err = "DIO:COUNter: DIO is not the active totalizer pin for its timer"; }
                ok = false;
            } else {
                *(c->con) = 0u;
                IEC1CLR = c->ieMask;
                IFS1CLR = c->ifMask;
                edge_CtrSetPmd((uint8_t)u, false);
                edge_SetPps(c->rpr, 0u);
                gCtrState[u].enabled = false;
                DIO_ReleaseChannel(dio, DIO_OWNER_IC);
                DIO_RestoreChannel(dio);
            }
        }
    }
    xSemaphoreGive(gMutex);
    return ok;
}

bool UserEdge_CounterEnabled(uint8_t dio) {
    int u = edge_CtrUnitForDio(dio);
    if (u < 0) { return false; }
    /* gMutex vs a concurrent arm/disarm — a getter must not read a half-applied
     * transition (e.g. enabled still true after the timer was PMD-gated). */
    xSemaphoreTake(edge_Mutex(), portMAX_DELAY);
    bool on = gCtrState[u].enabled && (gCtrState[u].dio == dio);
    xSemaphoreGive(gMutex);
    return on;
}

uint64_t UserEdge_CounterGet(uint8_t dio) {
    int u = edge_CtrUnitForDio(dio);
    if (u < 0) { return 0u; }
    uint64_t v = 0u;
    xSemaphoreTake(edge_Mutex(), portMAX_DELAY);
    taskENTER_CRITICAL();
    if (gCtrState[u].enabled && gCtrState[u].dio == dio) {
        uint32_t hi = gCtrState[u].high;
        uint16_t lo = (uint16_t)(*(gCtr[u].tmr));
        /* rollover pending but its ISR blocked by this critical section: a low read
         * with the flag set belongs to the next epoch (near-wrap reconcile). */
        if ((IFS1 & gCtr[u].ifMask) != 0u && lo < 0x8000u) { hi++; }
        v = ((uint64_t)hi << 16) | lo;
    }
    taskEXIT_CRITICAL();
    xSemaphoreGive(gMutex);
    return v;
}

bool UserEdge_CounterClear(uint8_t dio) {
    int u = edge_CtrUnitForDio(dio);
    if (u < 0) { return false; }
    bool ok = false;
    xSemaphoreTake(edge_Mutex(), portMAX_DELAY);
    taskENTER_CRITICAL();
    if (gCtrState[u].enabled && gCtrState[u].dio == dio) {
        *(gCtr[u].tmr) = 0u;
        gCtrState[u].high = 0u;
        IFS1CLR = gCtr[u].ifMask;   /* drop a pending rollover so it isn't re-counted */
        ok = true;
    }
    taskEXIT_CRITICAL();
    xSemaphoreGive(gMutex);
    return ok;
}

/* ------------------------------------------------------------------ */
/* ISR bodies */

void UserEdge_IsrEvent(uint8_t unit) {
    if (unit >= USER_EDGE_INT_UNITS) { return; }
    const EdgeIntUnit_t* r = &gInt[unit];
    if (!gIntState[unit].enabled || gIntState[unit].stormed) {
        IFS0CLR = r->ifMask;   /* stray/masked — acknowledge and bail */
        return;
    }
    uint8_t mode = gIntState[unit].mode;
    /* Single-edge modes: acknowledge EARLY. INTxIF is not auto-cleared, and an edge
     * arriving while it is still set merges into the set flag and is lost. Clearing
     * first makes a mid-body edge re-pend for a fresh ISR entry (no undercount). BOTH
     * mode must clear LATE (after the EP flip, to swallow the flip's synthetic edge),
     * so it acknowledges in its own branch below. */
    if (mode != (uint8_t)USER_EDGE_BOTH) {
        IFS0CLR = r->ifMask;
    }
    uint32_t ts = TMR6;        /* streaming timebase (TSTimerIndex = TMR_INDEX_6) */
    uint8_t edge;
    if (mode == (uint8_t)USER_EDGE_RISING)       { edge = 1u; }
    else if (mode == (uint8_t)USER_EDGE_FALLING) { edge = 0u; }
    else { edge = ((INTCON & r->epMask) != 0u) ? 1u : 0u; }   /* both: the armed polarity */

    edge_FifoPush(gIntState[unit].dio, ts, edge);
    gIntState[unit].count++;   /* ISRs serialized by equal priority -> single writer */

    if (mode == (uint8_t)USER_EDGE_BOTH) {
        /* Retarget the opposite edge: disable -> flip EP -> read-back (retire the
         * INTCON write through the bus so the flip settles before the ack) -> clear
         * the flip's synthetic edge -> enable. */
        IEC0CLR = r->ieMask;
        INTCONINV = r->epMask;
        (void)INTCON;
        IFS0CLR = r->ifMask;
        IEC0SET = r->ieMask;
    }

    /* Storm guard: mute a pin that floods the ISR so it can't starve the RTOS. */
    uint32_t now = CORETIMER_CounterGet();
    if ((uint32_t)(now - gIntState[unit].winStart) >= gStormWindowTicks) {
        gIntState[unit].winStart = now;
        gIntState[unit].winEdges = 0u;
    }
    gIntState[unit].winEdges++;
    if (gIntState[unit].winEdges > USER_EDGE_STORM_MAX) {
        IEC0CLR = r->ieMask;          /* stop the flood; pin stays claimed and the count
                                       * is preserved until a re-arm resets it to resume */
        gIntState[unit].stormed = true;
        LOG_E("DIO event: edge storm — pin auto-muted, re-arm to resume");
    }
}

void UserEdge_IsrCounterRollover(uint8_t unit) {
    if (unit >= USER_EDGE_CTR_UNITS) { return; }
    gCtrState[unit].high++;           /* sole writer (rollover interrupt) */
    IFS1CLR = gCtr[unit].ifMask;
}
