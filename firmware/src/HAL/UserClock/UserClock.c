/**
 * @file UserClock.c
 * @brief REFCLKO programmable clock outputs on the DIO terminal (#668, epic #664).
 *
 * Hand-rolled REFOxCON/REFOxTRIM setup (no-MCC; REFO is unused elsewhere in the
 * tree). Three units serve three PPS pin-groups; a DIO pin maps to exactly one.
 * Source is POSC (24 MHz) only -- SYSCLK/SPLL (252 MHz) are illegal per erratum #1,
 * and we never offer them, so the erratum cannot be tripped. See UserClock.h.
 */
#include "device.h"
#include "UserClock.h"
#include "../DIO.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

#define D(n)   ((uint16_t)(1u << (n)))

/* REFOxCON/REFOxTRIM field encodings (identical layout across REFO1/3/4; take
 * positions/masks from the device header rather than literals so the field
 * layout stays owned by the DFP, not this file). */
#define CLK_ROSEL_POSC   2u                          /* ROSEL value: POSC (24 MHz) */
#define CLK_ROSEL_POS    _REFO1CON_ROSEL_POSITION    /* ROSEL field position (0) */
#define CLK_RODIV_POS    _REFO1CON_RODIV_POSITION    /* RODIV<14:0> (16) */
#define CLK_ROTRIM_POS   _REFO1TRIM_ROTRIM_POSITION  /* ROTRIM<8:0> in REFOxTRIM (23) */
#define CLK_ROTRIM_MASK  _REFO1TRIM_ROTRIM_MASK      /* ROTRIM is REFOxTRIM's only field */
#define CLK_OE_MASK      _REFO1CON_OE_MASK           /* 0x1000 */
#define CLK_ON_MASK      _REFO1CON_ON_MASK           /* 0x8000 */
#define CLK_RODIV_MAX    32767u                      /* 15-bit */

typedef struct {
    volatile uint32_t* con;      /* REFOxCON    */
    volatile uint32_t* conSet;   /* REFOxCONSET */
    volatile uint32_t* conClr;   /* REFOxCONCLR */
    volatile uint32_t* trim;     /* REFOxTRIM   */
    uint8_t  ppsval;             /* RPnR output code */
    uint16_t pins;               /* DIO channels this unit can drive */
} ClockUnit_t;

/* idx 0 = REFCLKO4 (g1), 1 = REFCLKO1 (g2), 2 = REFCLKO3 (g3). */
static const ClockUnit_t gUnits[3] = {
    { (volatile uint32_t*)&REFO4CON, (volatile uint32_t*)&REFO4CONSET,
      (volatile uint32_t*)&REFO4CONCLR, (volatile uint32_t*)&REFO4TRIM, 13u,
      D(5) | D(7) | D(15) },
    { (volatile uint32_t*)&REFO1CON, (volatile uint32_t*)&REFO1CONSET,
      (volatile uint32_t*)&REFO1CONCLR, (volatile uint32_t*)&REFO1TRIM, 15u,
      D(2) | D(4) | D(6) | D(14) },
    { (volatile uint32_t*)&REFO3CON, (volatile uint32_t*)&REFO3CONSET,
      (volatile uint32_t*)&REFO3CONCLR, (volatile uint32_t*)&REFO3TRIM, 15u,
      D(3) | D(12) },
};

typedef struct {
    bool     configured;
    uint8_t  dio;
    uint32_t rodiv;
    uint32_t rotrim;
    uint32_t actualHz;
    bool     enabled;
} ClockState_t;

static ClockState_t gState[3];

static SemaphoreHandle_t gClockMutex;
static StaticSemaphore_t gClockMutexBuf;

static SemaphoreHandle_t clock_Mutex(void) {
    if (gClockMutex == NULL) {
        taskENTER_CRITICAL();
        if (gClockMutex == NULL) {   /* re-check: another task may have won the race */
            gClockMutex = xSemaphoreCreateMutexStatic(&gClockMutexBuf);
        }
        taskEXIT_CRITICAL();
    }
    return gClockMutex;
}

/* DIO channel -> its RPnR output-select register (same map SPI/UART use). */
static volatile uint32_t* clock_RprAddr(uint8_t dio) {
    switch (dio) {
        case 2:  return &RPD3R;
        case 3:  return &RPD12R;
        case 4:  return &RPF0R;
        case 5:  return &RPF1R;
        case 6:  return &RPG0R;
        case 7:  return &RPG1R;
        case 12: return &RPE3R;
        case 14: return &RPE5R;
        case 15: return &RPC1R;
        default: return NULL;
    }
}

/* -1 if the pin has no REFCLKO unit, else the gUnits index. */
static int clock_UnitForDio(uint8_t dio) {
    if (dio > 15u) { return -1; }
    uint16_t bit = D(dio);
    for (int i = 0; i < 3; ++i) {
        if ((gUnits[i].pins & bit) != 0u) { return i; }
    }
    return -1;
}

/* Fout = Fsrc/(2*(RODIV + ROTRIM/512)); RODIV=0 => Fsrc passthrough. */
static void clock_ComputeDiv(uint32_t hz, uint32_t* rodiv, uint32_t* rotrim, uint32_t* actual) {
    if (hz >= USER_CLOCK_SRC_HZ) {
        *rodiv = 0u; *rotrim = 0u; *actual = USER_CLOCK_SRC_HZ;   /* passthrough */
        return;
    }
    /* N = 512*RODIV + ROTRIM = round(256 * Fsrc / hz). 256*Fsrc overflows 32-bit
     * (6.144e9) -> 64-bit. */
    uint64_t N = (((uint64_t)USER_CLOCK_SRC_HZ * 256u) + (hz / 2u)) / hz;
    uint64_t nMax = (uint64_t)512u * CLK_RODIV_MAX;   /* RODIV=32767, ROTRIM=0 */
    if (N > nMax) { N = nMax; }
    if (N < 512u) { N = 512u; }                       /* RODIV >= 1 */
    *rodiv  = (uint32_t)(N / 512u);
    *rotrim = (uint32_t)(N % 512u);
    *actual = (uint32_t)(((uint64_t)USER_CLOCK_SRC_HZ * 256u) / N);
}

/* SYSKEY-guarded CFGCON write to toggle IOLOCK for PPS (plib_nvm idiom). */
static void clock_CfgconUnlockedWrite(uint32_t val) {
    uint32_t st = __builtin_disable_interrupts();
    SYSKEY = 0x00000000U;
    SYSKEY = 0xAA996655U;
    SYSKEY = 0x556699AAU;
    CFGCON = val;
    SYSKEY = 0x33333333U;
    __builtin_mtc0(12, 0, st);
}

static void clock_SetPps(uint8_t dio, uint8_t ppsval) {
    volatile uint32_t* rpr = clock_RprAddr(dio);
    if (rpr == NULL) { return; }
    uint32_t st = __builtin_disable_interrupts();
    clock_CfgconUnlockedWrite(CFGCON & ~(uint32_t)_CFGCON_IOLOCK_MASK);
    *rpr = (uint32_t)ppsval;
    clock_CfgconUnlockedWrite(CFGCON | (uint32_t)_CFGCON_IOLOCK_MASK);
    __builtin_mtc0(12, 0, st);
}

/* @return true iff REFOx ON latched (a not-clocked/gated module reads back 0). */
static bool clock_StartUnit(const ClockUnit_t* u, uint32_t rodiv, uint32_t rotrim) {
    *(u->con)  = ((uint32_t)CLK_ROSEL_POSC << CLK_ROSEL_POS) /* ROSEL = POSC (24 MHz) */
               | (rodiv << CLK_RODIV_POS)            /* RODIV */
               | CLK_OE_MASK;                        /* OE on; ON=0 for now */
    *(u->trim) = (rotrim << CLK_ROTRIM_POS) & CLK_ROTRIM_MASK; /* ROTRIM (sole field) */
    *(u->conSet) = CLK_ON_MASK;                      /* turn the reference on */
    return (*(u->con) & CLK_ON_MASK) != 0u;
}

/* ------------------------------------------------------------------ */
/* Public API */

bool UserClock_Configure(uint8_t dio, uint32_t hz, uint32_t* actualHz, const char** err) {
    int idx = clock_UnitForDio(dio);
    if (idx < 0) {
        if (err) { *err = "clock: pin has no REFCLKO (use DIO 2/3/4/5/6/7/12/14/15)"; }
        return false;
    }
    if (hz < USER_CLOCK_MIN_HZ || hz > USER_CLOCK_MAX_HZ) {
        if (err) { *err = "clock: frequency out of range (366 Hz .. 24 MHz; below 366 Hz use PWM)"; }
        return false;
    }
    uint32_t rodiv, rotrim, actual;
    clock_ComputeDiv(hz, &rodiv, &rotrim, &actual);
    xSemaphoreTake(clock_Mutex(), portMAX_DELAY);
    /* One REFCLKO unit serves a whole pin-group, so reconfiguring it while it is
     * live (possibly on another pin of the group) would desync the recorded pin
     * from the claimed/clocked one. Reject; the client disables first. */
    if (gState[idx].enabled) {
        xSemaphoreGive(clock_Mutex());
        if (err) { *err = "clock: disable before reconfiguring (DIO:CLOCk:ENAble <dio>,0)"; }
        return false;
    }
    gState[idx].configured = true;
    gState[idx].dio        = dio;
    gState[idx].rodiv      = rodiv;
    gState[idx].rotrim     = rotrim;
    gState[idx].actualHz   = actual;
    xSemaphoreGive(clock_Mutex());
    if (actualHz != NULL) { *actualHz = actual; }
    return true;
}

bool UserClock_Enable(uint8_t dio, bool on, const char** err) {
    int idx = clock_UnitForDio(dio);
    if (idx < 0) {
        if (err) { *err = "clock: pin has no REFCLKO"; }
        return false;
    }
    xSemaphoreTake(clock_Mutex(), portMAX_DELAY);
    bool ok = true;
    const ClockUnit_t* u = &gUnits[idx];
    if (on) {
        if (!gState[idx].configured || gState[idx].dio != dio) {
            if (err) { *err = "clock: no config for this pin (DIO:CLOCk:CONFig first)"; }
            ok = false;
        } else if (gState[idx].enabled) {
            /* idempotent */
        } else if (!DIO_ClaimChannel(dio, DIO_OWNER_CLOCK)) {
            if (err) { *err = "clock: pin owned by another peripheral"; }
            ok = false;
        } else {
            if (!clock_StartUnit(u, gState[idx].rodiv, gState[idx].rotrim)) {
                *(u->conClr) = CLK_ON_MASK;
                DIO_ReleaseChannel(dio, DIO_OWNER_CLOCK);
                if (err) { *err = "clock: REFCLKO failed to start (source/PMD)"; }
                ok = false;
            } else {
                clock_SetPps(dio, u->ppsval);           /* route REFCLKO to the pin */
                DIO_SetChannelPeripheralOutput(dio);    /* buffer on last -> clean */
                gState[idx].enabled = true;
            }
        }
    } else if (gState[idx].enabled) {
        /* One unit serves a whole pin-group; tear down ONLY the pin that is
         * actually clocking. Disabling a different pin of the same unit must not
         * stop that unit's reference or release/restore the wrong (unclaimed)
         * pin -- reject it (symmetric with the enable path's dio-mismatch guard). */
        if (gState[idx].dio != dio) {
            if (err) { *err = "clock: DIO is not the active clock pin for its unit"; }
            ok = false;
        } else {
            clock_SetPps(dio, 0u);                       /* unroute PPS */
            *(u->conClr) = CLK_ON_MASK;                  /* stop the reference */
            DIO_ReleaseChannel(dio, DIO_OWNER_CLOCK);    /* release BEFORE restore */
            DIO_RestoreChannel(dio);
            gState[idx].enabled = false;
        }
    }
    xSemaphoreGive(clock_Mutex());
    return ok;
}

uint32_t UserClock_GetActualHz(uint8_t dio) {
    int idx = clock_UnitForDio(dio);
    if (idx < 0) { return 0u; }
    xSemaphoreTake(clock_Mutex(), portMAX_DELAY);
    uint32_t hz = (gState[idx].enabled && gState[idx].dio == dio) ? gState[idx].actualHz : 0u;
    xSemaphoreGive(clock_Mutex());
    return hz;
}
