/**
 * @file UserIC.c
 * @brief Input-capture frequency/period/pulse-width/duty on the DIO terminal
 *        (#666, epic #664). Hand-rolled ICxCON (no Harmony IC plib exists).
 *
 * Register semantics are FRM/DS-verified (DS60001320H §17 + device header):
 * SINGLE 32-bit ICxCON per unit (this MZ EF part has no ICxCON1/ICxCON2, no
 * SYNCSEL). ICM=0b011 captures every rising edge; ICM=0b110 + FEDGE=1 captures
 * every edge. Timebase is Timer2 (ICTMR=1, C32=0 -> 16-bit) because Timer3 is
 * owned by DIO PWM; a software epoch bumped by the TMR2 rollover ISR extends the
 * 16-bit counter to 48 bits for low-frequency reach.
 *
 * Measurements are serialized (one active unit at a time under gMutex), so any
 * reachable IC unit is free while we hold the lock. Pulse/duty parity is derived
 * from the pin LEVEL sampled per edge in the capture ISR (robust — does not rely
 * on the FEDGE first-edge guarantee, which is bench-unverified).
 *
 * BENCH-VALIDATION PENDING (#666): needs a DIO0(PWM)->DIO5(IC) jumper self-test.
 * Two spec corners are UNVERIFIED until then: (a) the 16-bit epoch near-wrap
 * reconciliation, (b) FEDGE behavior (mitigated by the per-edge level anchor).
 */
#define LOG_LVL    LOG_LEVEL_GENERAL
#define LOG_MODULE LOG_MODULE_GENERAL
#include "UserIC.h"
#include "device.h"
#include "../DIO.h"
#include "../TimerApi/TimerApi.h"
#include "../../Util/Logger.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

/* ICxCON bits (single 32-bit register; DS60001320H §17). */
#define IC_CON_ON      (1u << 15)
#define IC_CON_FEDGE   (1u << 9)   /* first-edge select (ICM=110 only) */
#define IC_CON_ICTMR   (1u << 7)   /* 1 = Timer2 (with ICACLK=0) */
#define IC_CON_ICOV    (1u << 4)   /* RO: FIFO overflow */
#define IC_CON_ICBNE   (1u << 3)   /* RO: buffer not empty */
#define IC_ICM_RISING  0b011u      /* every rising edge (period/frequency) */
#define IC_ICM_BOTH    0b110u      /* every edge, FEDGE-first (pulse/duty) */

#define IC_CAP_MAX     16u         /* captured edges per measurement (RAM-bounded;
                                    * 15 periods is ample for averaging, and the
                                    * device is at the RAM edge) */

typedef struct {
    volatile uint32_t* con;     /* ICxCON  */
    volatile uint32_t* buf;     /* ICxBUF  (read pops FIFO) */
    volatile uint32_t* rpr;     /* ICxR    (PPS input select) */
    volatile uint32_t* ifsClr;  /* IFS0CLR / IFS1CLR */
    volatile uint32_t* iecSet;  /* IEC0SET / IEC1SET */
    volatile uint32_t* iecClr;  /* IEC0CLR / IEC1CLR */
    uint32_t flagMask;          /* capture-interrupt flag bit */
} IcRegs_t;

/* Capture flags: IC1-6 in IFS0<6,11,16,21,26,30>, IC7-9 in IFS1<2,6,10>. */
static const IcRegs_t gIc[USER_IC_UNITS] = {
    { &IC1CON, &IC1BUF, &IC1R, &IFS0CLR, &IEC0SET, &IEC0CLR, (1u << 6)  },
    { &IC2CON, &IC2BUF, &IC2R, &IFS0CLR, &IEC0SET, &IEC0CLR, (1u << 11) },
    { &IC3CON, &IC3BUF, &IC3R, &IFS0CLR, &IEC0SET, &IEC0CLR, (1u << 16) },
    { &IC4CON, &IC4BUF, &IC4R, &IFS0CLR, &IEC0SET, &IEC0CLR, (1u << 21) },
    { &IC5CON, &IC5BUF, &IC5R, &IFS0CLR, &IEC0SET, &IEC0CLR, (1u << 26) },
    { &IC6CON, &IC6BUF, &IC6R, &IFS0CLR, &IEC0SET, &IEC0CLR, (1u << 30) },
    { &IC7CON, &IC7BUF, &IC7R, &IFS1CLR, &IEC1SET, &IEC1CLR, (1u << 2)  },
    { &IC8CON, &IC8BUF, &IC8R, &IFS1CLR, &IEC1SET, &IEC1CLR, (1u << 6)  },
    { &IC9CON, &IC9BUF, &IC9R, &IFS1CLR, &IEC1SET, &IEC1CLR, (1u << 10) },
};

/* IC-reachable DIO pins: RPn input code (-> ICxR), PORT register + bit for the
 * per-edge level read, and the bitmask of IC units that reach the pin (bit u =
 * IC(u+1)). Groups: g1{5,7,15}=IC3,IC7; g2{2,4,6,14}=IC4,IC8; g3{3,12}=IC2,IC5,
 * IC9; g4{0,11}=IC1,IC6. (DIO->RPn cross-checked vs DS Table 12-2 + UserUart.) */
typedef struct {
    uint8_t  dio;
    uint8_t  rpnCode;
    volatile uint32_t* portReg;
    uint8_t  portBit;
    uint16_t unitMask;
} IcPin_t;

#define M_G1 ((1u << 2) | (1u << 6))              /* IC3, IC7 */
#define M_G2 ((1u << 3) | (1u << 7))              /* IC4, IC8 */
#define M_G3 ((1u << 1) | (1u << 4) | (1u << 8))  /* IC2, IC5, IC9 */
#define M_G4 ((1u << 0) | (1u << 5))              /* IC1, IC6 */

static const IcPin_t gPins[] = {
    {  0,  0u, &PORTD, 1u,  M_G4 },   /* RPD1  */
    {  2,  0u, &PORTD, 3u,  M_G2 },   /* RPD3  */
    {  3, 10u, &PORTD, 12u, M_G3 },   /* RPD12 */
    {  4,  4u, &PORTF, 0u,  M_G2 },   /* RPF0  */
    {  5,  4u, &PORTF, 1u,  M_G1 },   /* RPF1  */
    {  6, 12u, &PORTG, 0u,  M_G2 },   /* RPG0  */
    {  7, 12u, &PORTG, 1u,  M_G1 },   /* RPG1  */
    { 11, 12u, &PORTC, 2u,  M_G4 },   /* RPC2  */
    { 12,  6u, &PORTE, 3u,  M_G3 },   /* RPE3  */
    { 14,  6u, &PORTE, 5u,  M_G2 },   /* RPE5  */
    { 15, 10u, &PORTC, 1u,  M_G1 },   /* RPC1  */
};
#define IC_PIN_COUNT (sizeof(gPins) / sizeof(gPins[0]))

/* Active-measurement context (one at a time under gMutex). The capture ISR is
 * the sole writer of count/ts/lvl/overflow while active; the task reads them
 * after teardown. */
static struct {
    volatile bool     active;
    uint8_t           unit;
    volatile uint32_t* portReg;
    uint8_t           portBit;
    volatile uint32_t epoch;
    volatile uint64_t ts[IC_CAP_MAX];
    volatile uint8_t  lvl[IC_CAP_MAX];
    volatile uint16_t count;
    volatile bool     overflow;
} gM;

static SemaphoreHandle_t gMutex;
static StaticSemaphore_t gMutexBuf;

static SemaphoreHandle_t ic_Mutex(void) {
    if (gMutex == NULL) {
        taskENTER_CRITICAL();
        if (gMutex == NULL) { gMutex = xSemaphoreCreateMutexStatic(&gMutexBuf); }
        taskEXIT_CRITICAL();
    }
    return gMutex;
}

static const IcPin_t* ic_PinForDio(uint8_t dio) {
    for (uint32_t i = 0; i < IC_PIN_COUNT; i++) {
        if (gPins[i].dio == dio) { return &gPins[i]; }
    }
    return NULL;
}

/* SYSKEY-guarded CFGCON write to toggle IOLOCK for PPS (mirror of UserUart). */
static void ic_CfgconUnlockedWrite(uint32_t val) {
    SYSKEY = 0x00000000U;
    SYSKEY = 0xAA996655U;
    SYSKEY = 0x556699AAU;
    CFGCON = val;
    SYSKEY = 0x33333333U;
}

/* Route (code != 0) or unroute (code == 0) a pin to IC unit u's input. */
static void ic_SetPps(uint8_t u, uint8_t code) {
    uint32_t st = __builtin_disable_interrupts();
    ic_CfgconUnlockedWrite(CFGCON & ~(uint32_t)_CFGCON_IOLOCK_MASK);
    *(gIc[u].rpr) = (uint32_t)code;
    ic_CfgconUnlockedWrite(CFGCON | (uint32_t)_CFGCON_IOLOCK_MASK);
    __builtin_mtc0(12, 0, st);
}

/* TMR2 rollover callback (plib TIMER_2_InterruptHandler dispatches this). */
static void ic_RolloverCb(uint32_t status, uintptr_t ctx) {
    (void)status; (void)ctx;
    UserIC_IsrRollover();
}

/* ------------------------------------------------------------------ */
/* Public API */

void UserIC_Initialize(void) {
    /* Park every IC unit: off, capture IRQ disabled, flag cleared, priority 3
     * (<= FreeRTOS max-syscall 4). IPC fields per DS: IC1=IPC1<20:18>,
     * IC2=IPC2<28:26>, IC3=IPC4<4:2>, IC4=IPC5<12:10>, IC5=IPC6<20:18>,
     * IC6=IPC7<20:18>, IC7=IPC8<20:18>, IC8=IPC9<20:18>, IC9=IPC10<20:18>. */
    for (uint8_t u = 0; u < USER_IC_UNITS; u++) {
        *(gIc[u].con)    = 0u;
        *(gIc[u].iecClr) = gIc[u].flagMask;
        *(gIc[u].ifsClr) = gIc[u].flagMask;
    }
    IPC1bits.IC1IP  = 3; IPC1bits.IC1IS  = 0;
    IPC2bits.IC2IP  = 3; IPC2bits.IC2IS  = 0;
    IPC4bits.IC3IP  = 3; IPC4bits.IC3IS  = 0;
    IPC5bits.IC4IP  = 3; IPC5bits.IC4IS  = 0;
    IPC6bits.IC5IP  = 3; IPC6bits.IC5IS  = 0;
    IPC7bits.IC6IP  = 3; IPC7bits.IC6IS  = 0;
    IPC8bits.IC7IP  = 3; IPC8bits.IC7IS  = 0;
    IPC9bits.IC8IP  = 3; IPC9bits.IC8IS  = 0;
    IPC10bits.IC9IP = 3; IPC10bits.IC9IS = 0;
    gM.active = false;
}

void UserIC_IsrRollover(void) {
    gM.epoch++;   /* sole writer; TMR2 rollover interrupt */
}

void UserIC_IsrCapture(uint8_t unit) {
    if (unit >= USER_IC_UNITS) { return; }
    const IcRegs_t* r = &gIc[unit];
    if (!gM.active || unit != gM.unit) {
        /* stray/late interrupt for an inactive unit — drain + clear, don't wedge */
        while ((*r->con) & IC_CON_ICBNE) { (void)*(r->buf); }
        *(r->ifsClr) = r->flagMask;
        return;
    }
    while ((*r->con) & IC_CON_ICBNE) {
        uint32_t cap   = *(r->buf) & 0xFFFFu;   /* pops one FIFO entry */
        uint32_t epoch = gM.epoch;
        /* Near-wrap reconciliation (UNVERIFIED — bench-confirm): if a TMR2
         * rollover is pending (T2IF) and this capture is in the low half, the
         * wrap happened before we serviced it -> use epoch+1. */
        if ((IFS0 & _IFS0_T2IF_MASK) != 0u && cap < 0x8000u) { epoch++; }
        uint64_t ts = ((uint64_t)epoch << 16) | cap;
        uint8_t level = (uint8_t)((*gM.portReg >> gM.portBit) & 1u);
        uint16_t i = gM.count;
        if (i < IC_CAP_MAX) {
            gM.ts[i]  = ts;
            gM.lvl[i] = level;
            gM.count  = (uint16_t)(i + 1u);
        }
    }
    if ((*r->con) & IC_CON_ICOV) { gM.overflow = true; }
    *(r->ifsClr) = r->flagMask;
}

/* Run one capture session: claim pin + a reachable unit, route PPS, arm the
 * given ICM mode, collect edges into out[], release. Returns false with *err on
 * no-signal/overflow/unreachable. out/outLvl sized >= IC_CAP_MAX. */
static bool ic_Run(uint8_t dio, uint32_t icm, bool fedge, uint16_t minEdges,
                   uint32_t gateMs, uint32_t hardMs,
                   uint64_t* out, uint8_t* outLvl, uint16_t* outN,
                   const char** err) {
    const IcPin_t* p = ic_PinForDio(dio);
    if (p == NULL) {
        if (err) { *err = "IC: DIO pin is not input-capture reachable"; }
        return false;
    }
    xSemaphoreTake(ic_Mutex(), portMAX_DELAY);

    /* serialized -> any reachable unit is free */
    int unit = -1;
    for (uint8_t u = 0; u < USER_IC_UNITS; u++) {
        if ((p->unitMask & (1u << u)) != 0u) { unit = (int)u; break; }
    }
    if (unit < 0) {   /* unreachable — defensive; ic_PinForDio already filters */
        xSemaphoreGive(gMutex);
        if (err) { *err = "IC: no capture unit reaches this pin"; }
        return false;
    }
    if (!DIO_ClaimChannel(dio, DIO_OWNER_IC)) {
        xSemaphoreGive(gMutex);
        if (err) { *err = "IC: DIO pin is claimed by another peripheral"; }
        return false;
    }
    DIO_SetChannelPeripheralInput(dio);
    ic_SetPps((uint8_t)unit, p->rpnCode);

    gM.unit = (uint8_t)unit;
    gM.portReg = p->portReg;
    gM.portBit = p->portBit;
    gM.epoch = 0u;
    gM.count = 0u;
    gM.overflow = false;
    gM.active = true;

    /* Timer2: free-running 16-bit @ PBCLK3 1:1, rollover IRQ bumps the epoch. */
    TimerApi_Stop(2);
    TimerApi_PreScalerSet(2, TMR_PRESCALE_VALUE_1);
    TimerApi_PeriodSet(2, 0xFFFFu);
    TimerApi_CallbackRegister(2, ic_RolloverCb, 0);
    IFS0CLR = _IFS0_T2IF_MASK;
    TimerApi_InterruptEnable(2);
    TimerApi_Start(2);

    /* Arm the IC unit: reset (ON=0), drain stale FIFO, set mode, enable IRQ, ON. */
    const IcRegs_t* r = &gIc[unit];
    *(r->con) = 0u;
    while ((*r->con) & IC_CON_ICBNE) { (void)*(r->buf); }
    *(r->ifsClr) = r->flagMask;
    *(r->con) = IC_CON_ICTMR | (fedge ? IC_CON_FEDGE : 0u) | icm;   /* ON still 0 */
    *(r->iecSet) = r->flagMask;
    *(r->con) |= IC_CON_ON;

    /* Collect: stop when the buffer fills, or minEdges captured and the gate has
     * elapsed, or the hard timeout expires (no/too-slow signal). */
    TickType_t start = xTaskGetTickCount();
    for (;;) {
        uint16_t c = gM.count;
        TickType_t el = xTaskGetTickCount() - start;
        if (c >= IC_CAP_MAX) { break; }
        if (c >= minEdges && el >= pdMS_TO_TICKS(gateMs)) { break; }
        if (el >= pdMS_TO_TICKS(hardMs)) { break; }
        vTaskDelay(pdMS_TO_TICKS(2));
    }

    /* Teardown + snapshot under lock. */
    *(r->con) = 0u;
    *(r->iecClr) = r->flagMask;
    *(r->ifsClr) = r->flagMask;
    TimerApi_Stop(2);
    TimerApi_InterruptDisable(2);
    ic_SetPps((uint8_t)unit, 0u);
    gM.active = false;

    uint16_t n = gM.count;
    if (n > IC_CAP_MAX) { n = IC_CAP_MAX; }
    for (uint16_t i = 0; i < n; i++) { out[i] = gM.ts[i]; outLvl[i] = gM.lvl[i]; }
    bool ov = gM.overflow;

    DIO_ReleaseChannel(dio, DIO_OWNER_IC);
    xSemaphoreGive(gMutex);

    *outN = n;
    if (n < minEdges) {
        if (err) {
            *err = ov ? "IC: FIFO overflow — signal too fast for this range"
                      : "IC: no (or too-slow) signal within the measurement timeout";
        }
        return false;
    }
    return true;
}

/* Averaged rising-edge span in timer counts (ts[n-1]-ts[0]) and edge count. */
bool UserIC_MeasureFrequency(uint8_t dio, uint32_t gate_ms, double* hz,
                             const char** err) {
    uint64_t ts[IC_CAP_MAX]; uint8_t lv[IC_CAP_MAX]; uint16_t n = 0;
    uint32_t gate = (gate_ms == 0u) ? 100u : gate_ms;
    uint32_t hard = (3u * gate > 5000u) ? (3u * gate) : 5000u;
    if (!ic_Run(dio, IC_ICM_RISING, false, 2u, gate, hard, ts, lv, &n, err)) {
        return false;
    }
    double span = (double)(ts[n - 1] - ts[0]);          /* timer counts */
    if (span <= 0.0) { if (err) { *err = "IC: degenerate capture span"; } return false; }
    *hz = (double)(n - 1) * (double)USER_IC_TIMER_HZ / span;
    return true;
}

bool UserIC_MeasurePeriod(uint8_t dio, double* us, const char** err) {
    uint64_t ts[IC_CAP_MAX]; uint8_t lv[IC_CAP_MAX]; uint16_t n = 0;
    if (!ic_Run(dio, IC_ICM_RISING, false, 2u, 100u, 5000u, ts, lv, &n, err)) {
        return false;
    }
    double span = (double)(ts[n - 1] - ts[0]);
    if (span <= 0.0) { if (err) { *err = "IC: degenerate capture span"; } return false; }
    double periodCounts = span / (double)(n - 1);
    *us = periodCounts * 1.0e6 / (double)USER_IC_TIMER_HZ;
    return true;
}

/* Both-edge capture; interval [k,k+1] carries level lvl[k] (post-edge-k level).
 * A high interval follows a rising edge (lvl==1). */
bool UserIC_MeasurePulseWidth(uint8_t dio, uint8_t polarity, double* us,
                              const char** err) {
    uint64_t ts[IC_CAP_MAX]; uint8_t lv[IC_CAP_MAX]; uint16_t n = 0;
    if (!ic_Run(dio, IC_ICM_BOTH, true, 3u, 100u, 5000u, ts, lv, &n, err)) {
        return false;
    }
    uint8_t want = (polarity != 0u) ? 1u : 0u;
    double sum = 0.0; uint32_t cnt = 0u;
    for (uint16_t k = 0; k + 1u < n; k++) {
        if (lv[k] == want) { sum += (double)(ts[k + 1] - ts[k]); cnt++; }
    }
    if (cnt == 0u) {
        if (err) { *err = "IC: no matching-polarity pulse captured"; }
        return false;
    }
    *us = (sum / (double)cnt) * 1.0e6 / (double)USER_IC_TIMER_HZ;
    return true;
}

bool UserIC_MeasureDuty(uint8_t dio, double* percent, const char** err) {
    uint64_t ts[IC_CAP_MAX]; uint8_t lv[IC_CAP_MAX]; uint16_t n = 0;
    if (!ic_Run(dio, IC_ICM_BOTH, true, 3u, 100u, 5000u, ts, lv, &n, err)) {
        return false;
    }
    double high = 0.0, total = 0.0;
    for (uint16_t k = 0; k + 1u < n; k++) {
        double interval = (double)(ts[k + 1] - ts[k]);
        total += interval;
        if (lv[k] == 1u) { high += interval; }
    }
    if (total <= 0.0) { if (err) { *err = "IC: degenerate capture span"; } return false; }
    *percent = 100.0 * high / total;
    return true;
}
