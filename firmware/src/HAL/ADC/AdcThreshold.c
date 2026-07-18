/**
 * @file AdcThreshold.c
 * @brief ADCHS digital-comparator threshold alarms (#670, epic #664).
 *
 * Hand-rolled ADCCMPCONx/ADCCMPENx/ADCCMPx setup (no Harmony comparator API
 * exists). Six comparator units; one is allocated per monitored channel. See
 * AdcThreshold.h for the mode->condition map and the raw-code contract.
 *
 * AN-coverage limit: ADCCMPENx is a single 32-bit register, so a comparator can
 * only be assigned an AN input < 32. A handful of NQ1 channels scan on AN>=32
 * (via ADCCSS2) and are therefore rejected with a clear message. (Whether the
 * silicon can compare AN>=32 through some other path is a FRM question; the safe,
 * always-correct subset is AN<32, which is what we expose.)
 */
#define LOG_LVL    LOG_LEVEL_ADC
#define LOG_MODULE LOG_MODULE_ADC
#include "AdcThreshold.h"
#include "device.h"
#include "../ADC.h"
#include "../../state/board/BoardConfig.h"
#include "../../state/board/AInConfig.h"
#include "../../state/runtime/BoardRuntimeConfig.h"
#include "../../services/streaming.h"
#include "../../Util/Logger.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

/* ADCCMPCONx condition bits (identical layout across CMPCON1..6). */
#define CMP_IELOLO   _ADCCMPCON1_IELOLO_MASK   /* result <  DCMPLO */
#define CMP_IEHILO   _ADCCMPCON1_IEHILO_MASK   /* DCMPLO <= result < DCMPHI */
#define CMP_IEHIHI   _ADCCMPCON1_IEHIHI_MASK   /* result >= DCMPHI */
#define CMP_ENDCMP   _ADCCMPCON1_ENDCMP_MASK   /* enable the comparator */

#define AN_CMP_MAX   31u   /* ADCCMPENx is 32-bit: only AN0..31 are selectable */

typedef struct {
    volatile uint32_t* con;   /* ADCCMPCONx */
    volatile uint32_t* en;    /* ADCCMPENx  */
    volatile uint32_t* cmp;   /* ADCCMPx (DCMPHI:DCMPLO) */
    uint8_t  ifsBit;          /* IFS1/IEC1 bit for ADCDCxIF/IE */
} CmpRegs_t;

/* Vector 46..51 -> IFS1/IEC1 bits 14..19 (verified device header). */
static const CmpRegs_t gRegs[ADC_THRESHOLD_UNITS] = {
    { &ADCCMPCON1, &ADCCMPEN1, &ADCCMP1, 14u },
    { &ADCCMPCON2, &ADCCMPEN2, &ADCCMP2, 15u },
    { &ADCCMPCON3, &ADCCMPEN3, &ADCCMP3, 16u },
    { &ADCCMPCON4, &ADCCMPEN4, &ADCCMP4, 17u },
    { &ADCCMPCON5, &ADCCMPEN5, &ADCCMP5, 18u },
    { &ADCCMPCON6, &ADCCMPEN6, &ADCCMP6, 19u },
};

typedef struct {
    bool     inUse;
    uint8_t  chId;                 /* user channel id being watched */
    uint8_t  an;                   /* AN input (< 32) */
    bool     isT1;                 /* dedicated (continuous) vs T2 (scanned) */
    AdcThresholdMode mode;
    uint16_t lo, hi;
    volatile uint32_t tripCount;   /* ISR-incremented (ISR is the sole writer) */
    volatile bool     latched;     /* ISR-set; cleared by AdcThreshold_Clear */
} ThreshUnit;

static ThreshUnit gUnits[ADC_THRESHOLD_UNITS];

static SemaphoreHandle_t gMutex;
static StaticSemaphore_t gMutexBuf;

static SemaphoreHandle_t thr_Mutex(void) {
    if (gMutex == NULL) {
        taskENTER_CRITICAL();
        if (gMutex == NULL) { gMutex = xSemaphoreCreateMutexStatic(&gMutexBuf); }
        taskEXIT_CRITICAL();
    }
    return gMutex;
}

/* ---- interrupt enable/flag/priority (kept in-module; plib_evic untouched) ---- */

static void thr_IntSetPriority(uint8_t u) {
    /* Priority 3 (<= configMAX_SYSCALL_INTERRUPT_PRIORITY = 4), matching EOS.
     * DC1/2 in IPC11, DC3/4/5/6 in IPC12; sub-priority left 0. */
    switch (u) {
        case 0: IPC11bits.ADCDC1IP = 3; IPC11bits.ADCDC1IS = 0; break;
        case 1: IPC11bits.ADCDC2IP = 3; IPC11bits.ADCDC2IS = 0; break;
        case 2: IPC12bits.ADCDC3IP = 3; IPC12bits.ADCDC3IS = 0; break;
        case 3: IPC12bits.ADCDC4IP = 3; IPC12bits.ADCDC4IS = 0; break;
        case 4: IPC12bits.ADCDC5IP = 3; IPC12bits.ADCDC5IS = 0; break;
        case 5: IPC12bits.ADCDC6IP = 3; IPC12bits.ADCDC6IS = 0; break;
        default: break;
    }
}
static inline void thr_IntEnable(uint8_t u)  { IEC1SET = (1u << gRegs[u].ifsBit); }
static inline void thr_IntDisable(uint8_t u) { IEC1CLR = (1u << gRegs[u].ifsBit); }
static inline void thr_IntClearFlag(uint8_t u){ IFS1CLR = (1u << gRegs[u].ifsBit); }

/* ---- channel resolution + capability gate ---- */

/* Resolve a user channel id to its board index + AN + type, enforcing the
 * MC12b-only / public-only / AN<32 constraints. err set on any rejection. */
static bool thr_Resolve(uint8_t chId, size_t* idx, uint8_t* an, bool* isT1,
                        const char** err) {
    const tBoardConfig* pCfg =
            (const tBoardConfig*)BoardConfig_Get(BOARDCONFIG_ALL_CONFIG, 0);
    size_t i = ADC_FindChannelIndex(chId);
    if (i >= pCfg->AInChannels.Size) {
        if (err) { *err = "threshold: unknown ADC channel"; }
        return false;
    }
    const AInChannel* ch = &pCfg->AInChannels.Data[i];
    if (ch->Type != AIn_MC12bADC) {
        if (err) { *err = "threshold: not supported on this board's ADC (MC12b only)"; }
        return false;
    }
    if (ch->Config.MC12b.IsPublic != 1) {
        if (err) { *err = "threshold: not a public user channel"; }
        return false;
    }
    uint32_t anv = ch->Config.MC12b.ChannelId;
    if (anv > AN_CMP_MAX) {
        if (err) { *err = "threshold: channel's ADC input (AN>=32) is not comparator-reachable"; }
        return false;
    }
    if (idx)  { *idx = i; }
    if (an)   { *an = (uint8_t)anv; }
    if (isT1) { *isT1 = (ch->Config.MC12b.ChannelType == 1); }
    return true;
}

/* find the unit watching chId, or -1 */
static int thr_UnitForCh(uint8_t chId) {
    for (int u = 0; u < (int)ADC_THRESHOLD_UNITS; u++) {
        if (gUnits[u].inUse && gUnits[u].chId == chId) { return u; }
    }
    return -1;
}

static uint32_t thr_ModeBits(AdcThresholdMode m) {
    switch (m) {
        case ADC_THRESH_BELOW:   return CMP_IELOLO;
        case ADC_THRESH_ABOVE:   return CMP_IEHIHI;
        case ADC_THRESH_INSIDE:  return CMP_IEHILO;
        case ADC_THRESH_OUTSIDE: return CMP_IELOLO | CMP_IEHIHI;
        default:                 return 0u;
    }
}

/* Program comparator unit u for (an, mode, lo, hi) and arm its interrupt. */
static void thr_ApplyUnit(uint8_t u, uint8_t an, AdcThresholdMode mode,
                          uint16_t lo, uint16_t hi) {
    const CmpRegs_t* r = &gRegs[u];
    *(r->con) = 0u;                 /* disable while reconfiguring */
    thr_IntDisable(u);
    thr_IntClearFlag(u);
    *(r->cmp) = ((uint32_t)hi << 16) | (uint32_t)lo;   /* DCMPHI:DCMPLO */
    *(r->en)  = (1u << an);                              /* watch this AN input */
    *(r->con) = thr_ModeBits(mode) | CMP_ENDCMP;        /* condition + enable */
    thr_IntClearFlag(u);
    thr_IntEnable(u);
}

/* Fully release comparator unit u. */
static void thr_ReleaseUnit(uint8_t u) {
    thr_IntDisable(u);
    *(gRegs[u].con) = 0u;
    *(gRegs[u].en)  = 0u;
    thr_IntClearFlag(u);
    gUnits[u].inUse     = false;
    gUnits[u].mode      = ADC_THRESH_OFF;
    gUnits[u].tripCount = 0u;
    gUnits[u].latched   = false;
}

/* ------------------------------------------------------------------ */
/* Public API */

void AdcThreshold_Initialize(void) {
    for (uint8_t u = 0; u < ADC_THRESHOLD_UNITS; u++) {
        thr_IntDisable(u);
        thr_IntClearFlag(u);
        thr_IntSetPriority(u);
        *(gRegs[u].con) = 0u;
        *(gRegs[u].en)  = 0u;
        gUnits[u] = (ThreshUnit){0};
    }
}

bool AdcThreshold_Configure(uint8_t chId, AdcThresholdMode mode,
                            uint16_t lo, uint16_t hi, const char** err) {
    if (mode > ADC_THRESH_OUTSIDE) {
        if (err) { *err = "threshold: mode out of range (0..4)"; }
        return false;
    }
    if (mode != ADC_THRESH_OFF) {
        if (lo > ADC_THRESHOLD_MAX_CODE || hi > ADC_THRESHOLD_MAX_CODE || lo > hi) {
            if (err) { *err = "threshold: need 0 <= lo <= hi <= 4095 (raw codes)"; }
            return false;
        }
    }
    size_t idx = 0; uint8_t an = 0; bool isT1 = false;
    if (mode != ADC_THRESH_OFF) {
        if (!thr_Resolve(chId, &idx, &an, &isT1, err)) { return false; }
    }
    (void)idx;   /* resolved for validation; the unit is keyed by chId */

    xSemaphoreTake(thr_Mutex(), portMAX_DELAY);
    bool ok = true;
    int existing = thr_UnitForCh(chId);

    if (mode == ADC_THRESH_OFF) {
        if (existing >= 0) { thr_ReleaseUnit((uint8_t)existing); }
        /* OFF on a channel with no threshold is a no-op success. */
    } else if (existing >= 0) {
        /* reconfigure in place (keep the unit, reset counters via ApplyUnit) */
        gUnits[existing].mode = mode;
        gUnits[existing].an   = an;
        gUnits[existing].isT1 = isT1;
        gUnits[existing].lo   = lo;
        gUnits[existing].hi   = hi;
        gUnits[existing].tripCount = 0u;
        gUnits[existing].latched   = false;
        thr_ApplyUnit((uint8_t)existing, an, mode, lo, hi);
    } else {
        int u = -1;
        for (int k = 0; k < (int)ADC_THRESHOLD_UNITS; k++) {
            if (!gUnits[k].inUse) { u = k; break; }
        }
        if (u < 0) {
            if (err) { *err = "threshold: all 6 comparator units in use"; }
            ok = false;
        } else {
            gUnits[u] = (ThreshUnit){ .inUse = true, .chId = chId, .an = an,
                                      .isT1 = isT1, .mode = mode, .lo = lo, .hi = hi };
            thr_ApplyUnit((uint8_t)u, an, mode, lo, hi);
        }
    }
    xSemaphoreGive(thr_Mutex());
    return ok;
}

bool AdcThreshold_Query(uint8_t chId, AdcThresholdMode* mode,
                        uint16_t* lo, uint16_t* hi,
                        uint32_t* tripCount, bool* latched) {
    xSemaphoreTake(thr_Mutex(), portMAX_DELAY);
    int u = thr_UnitForCh(chId);
    bool found = (u >= 0);
    if (mode)      { *mode      = found ? gUnits[u].mode : ADC_THRESH_OFF; }
    if (lo)        { *lo        = found ? gUnits[u].lo : 0u; }
    if (hi)        { *hi        = found ? gUnits[u].hi : 0u; }
    if (tripCount) { *tripCount = found ? gUnits[u].tripCount : 0u; }
    if (latched)   { *latched   = found ? gUnits[u].latched : false; }
    xSemaphoreGive(thr_Mutex());
    return found;
}

void AdcThreshold_Clear(uint8_t chId) {
    xSemaphoreTake(thr_Mutex(), portMAX_DELAY);
    for (uint8_t u = 0; u < ADC_THRESHOLD_UNITS; u++) {
        if (!gUnits[u].inUse) { continue; }
        if (chId != ADC_THRESHOLD_ALL_CH && gUnits[u].chId != chId) { continue; }
        /* Disable the interrupt across the clear so a concurrent trip can't
         * immediately re-latch/re-count what we just zeroed. */
        thr_IntDisable(u);
        gUnits[u].tripCount = 0u;
        gUnits[u].latched   = false;
        thr_IntClearFlag(u);
        thr_IntEnable(u);
    }
    xSemaphoreGive(thr_Mutex());
}

bool AdcThreshold_AnyLatched(void) {
    /* Lock-free read: each latched flag is a bool written only by its ISR; a
     * stale miss is corrected on the next SyncQuesBits. */
    for (uint8_t u = 0; u < ADC_THRESHOLD_UNITS; u++) {
        if (gUnits[u].inUse && gUnits[u].latched) { return true; }
    }
    return false;
}

void AdcThreshold_RevalidateForStream(void) {
    /* A Type 2 threshold whose channel is not in the session scan (ADCCSS) can
     * never fire. Disable it and inform. Reads the applied ADCCSS1/2 live. */
    xSemaphoreTake(thr_Mutex(), portMAX_DELAY);
    for (uint8_t u = 0; u < ADC_THRESHOLD_UNITS; u++) {
        if (!gUnits[u].inUse || gUnits[u].isT1) { continue; }
        uint8_t an = gUnits[u].an;   /* < 32 by construction */
        bool scanned = ((ADCCSS1 >> an) & 1u) != 0u;
        if (!scanned) {
            LOG_E("ADC threshold on ch %u disabled: channel not in the streaming scan",
                  (unsigned)gUnits[u].chId);
            thr_ReleaseUnit(u);
        }
    }
    xSemaphoreGive(thr_Mutex());
}

void AdcThreshold_IsrTrip(uint8_t unit) {
    if (unit >= ADC_THRESHOLD_UNITS) { return; }
    thr_IntClearFlag(unit);
    (void)*(gRegs[unit].con);          /* read clears the DCMPED event */
    gUnits[unit].tripCount++;          /* ISR is the sole writer of this field */
    gUnits[unit].latched = true;
    LOG_E_ONCE(LOG_ONCE_ADC_THRESHOLD_TRIP, "ADC analog threshold tripped");
}
