/* ************************************************************************** */
/** Descriptive File Name

  @Company
    Company Name

  @File Name
    filename.c

  @Summary
    Brief description of the file.

  @Description
    Describe the purpose of this file.
 */
/* ************************************************************************** */

#include "TimerApi.h"


void TimerApi_Initialize(uint8_t index) {
    switch (index) {
        case 2:
            TMR2_Initialize();
            break;
        case 3:
            TMR3_Initialize();
            break;
        case 4:
            TMR4_Initialize();
            break;
        case 6:
            TMR6_Initialize();
            break;
        default:
            break;
    }
}

void TimerApi_Start(uint8_t index) {
    switch (index) {
        case 2:
            TMR2_Start();
            break;
        case 3:
            TMR3_Start();
            break;
        case 4:
            TMR4_Start();
            break;
        case 6:
            TMR6_Start();
            break;
        default:
            break;
    }
}

void TimerApi_Stop(uint8_t index) {
    switch (index) {
        case 2:
            TMR2_Stop();
            break;
        case 3:
            TMR3_Stop();
            break;
        case 4:
            TMR4_Stop();
            break;
        case 6:
            TMR6_Stop();
            break;
        default:
            break;
    }
}

void TimerApi_PeriodSet(uint8_t index, uint32_t period) {
    switch (index) {
        case 2:
            TMR2_PeriodSet(period);
            break;
        case 3:
            TMR3_PeriodSet(period);
            break;
        case 4:
            TMR4_PeriodSet(period);
            break;
        case 6:
            TMR6_PeriodSet(period);
            break;
        default:
            break;
    }
}

uint32_t TimerApi_PeriodGet(uint8_t index) {
    uint32_t ret = 0;
    switch (index) {
        case 2:
            ret = TMR2_PeriodGet();
            break;
        case 3:
            ret = TMR3_PeriodGet();
            break;
        case 4:
            ret = TMR4_PeriodGet();
            break;
        case 6:
            ret = TMR6_PeriodGet();
            break;
        default:
            break;
    }
    return ret;
}

uint32_t TimerApi_CounterGet(uint8_t index) {
    uint32_t ret = 0;
    switch (index) {
        case 2:
            ret = TMR2_CounterGet();
            break;
        case 3:
            ret = TMR3_CounterGet();
            break;
        case 4:
            ret = TMR4_CounterGet();
            break;
        case 6:
            ret = TMR6_CounterGet();
            break;
        default:
            break;
    }
    return ret;
}

void TimerApi_PreScalerSet(uint8_t index, timerApi_presScale_t preScale) {
    switch (index) {
        case 2:
            T2CONbits.TCKPS = preScale;
            break;
        case 3:
            T3CONbits.TCKPS = preScale;
            break;
        case 4:
            T4CONbits.TCKPS = preScale;
            break;
        case 6:
            T6CONbits.TCKPS = preScale;
            break;
        default:
            break;
    }
}

uint16_t TimerApi_PreScalerGet(uint8_t index) {    
    uint8_t temp_prescaler = 0;
    uint16_t preScaler = 0;
    switch (index) {
        case 2:
            temp_prescaler = T2CONbits.TCKPS;
            break;
        case 3:
            temp_prescaler = T3CONbits.TCKPS;
            break;
        case 4:
            temp_prescaler = T4CONbits.TCKPS;
            break;
        case 6:
            temp_prescaler = T6CONbits.TCKPS;
            break;
        default:
            break;
    }
    if (temp_prescaler == 7u) {
        temp_prescaler++;
    }
    preScaler = (uint16_t) (0x01u << temp_prescaler);
    return preScaler;
}

/* #716: derive the real clock rather than trusting the compile-time constant.
 *
 * Bit encodings are from the PIC32MZ EF datasheet DS60001320G Register 8-3
 * (SPLLCON) and Register 8-9 (PBxDIV) — quoted here because getting any of
 * them wrong silently scales every streamed rate:
 *
 *   SPLLCON PLLIDIV<2:0>  bits 10:8   000 = /1 ... 111 = /8      -> value + 1
 *   SPLLCON PLLMULT<6:0>  bits 22:16  0000000 = x1 ... x128      -> value + 1
 *   SPLLCON PLLODIV<2:0>  bits 26:24  001 = /2, 010 = /4, 011 = /8,
 *                                     100 = /16, 101 = /32       -> 1 << value
 *                                     (000 and 11x are Reserved)
 *   SPLLCON PLLICLK       bit 7       0 = POSC, 1 = FRC
 *   OSCCON  COSC<2:0>     bits 14:12  001 = SPLL
 *   PBxDIV  PBDIV<6:0>    bits 6:0    0000001 = /2, 0000010 = /3 -> value + 1
 *
 * Uses the SFR bitfield accessors rather than raw register maths (CLAUDE.md
 * peripheral-access preference); there is no Harmony PLIB that reports a
 * derived clock frequency on this part.
 *
 * Not cached: file statics on this part can land in .bss sections outside
 * [_bss_begin,_bss_end] and so are NOT reliably zero-initialised across MCLR,
 * which makes a lazily-filled cache-valid flag a hazard for no gain here — the
 * callers are SCPI/config paths, never the per-sample path.
 */
uint32_t TimerApi_PeripheralClockHz(void) {
    uint32_t sysclkHz;

    if (OSCCONbits.COSC == 0x1u) {          /* running from the System PLL */
        const uint32_t inHz = (SPLLCONbits.PLLICLK != 0u)
                              ? (uint32_t)DAQIFI_FRC_HZ
                              : (uint32_t)DAQIFI_POSC_HZ;
        const uint32_t idiv = (uint32_t)SPLLCONbits.PLLIDIV + 1u;
        const uint32_t mult = (uint32_t)SPLLCONbits.PLLMULT + 1u;
        const uint32_t odivField = (uint32_t)SPLLCONbits.PLLODIV;
        /* 000 is Reserved; treat it as the /2 POR default rather than shifting
         * by 0 and reporting double the real clock. */
        const uint32_t odiv = (odivField == 0u) ? 2u : (1u << odivField);

        /* 64-bit intermediate: 24 MHz x 128 overflows uint32. Multiply before
         * dividing so a non-integral input divide cannot truncate. */
        sysclkHz = (uint32_t)(((uint64_t)inHz * mult) / idiv / odiv);
    } else {
        /* Not on the PLL (FRC/POSC/SOSC directly). Nothing in this firmware
         * switches away from SPLL, so this is unreachable in practice; fall
         * back to the built-for value rather than reporting nonsense. */
        sysclkHz = (uint32_t)DAQIFI_SYSCLK_HZ;
    }

    /* Timers live on PBCLK3. */
    return sysclkHz / ((uint32_t)PB3DIVbits.PBDIV + 1u);
}

bool TimerApi_ClockMatchesBuild(void) {
    return (TimerApi_PeripheralClockHz() == (uint32_t)TIMER_CLOCK_FRQ_BUILT);
}

uint32_t TimerApi_FrequencyGet(uint8_t index) {
    uint32_t ret = 0;
    /* #716: the ACTUAL peripheral clock, not the compile-time constant. Every
     * streaming-rate computation funnels through this function (stream START,
     * channel enable, Streaming_Init's boot derivation, CONF:CAP:JSON?), so
     * making it honest here fixes the rate, the reported timebase and the caps
     * together. The prescaler was already read from the register. */
    const uint32_t clkHz = TimerApi_PeripheralClockHz();
    switch (index) {
        case 2:
            ret=clkHz/TimerApi_PreScalerGet(2);
            break;
        case 3:
            ret=clkHz/TimerApi_PreScalerGet(3);
            break;
        case 4:
            ret=clkHz/TimerApi_PreScalerGet(4);
            break;
        case 6:
             ret=clkHz/TimerApi_PreScalerGet(6);
            break;
        default:
            break;
    }
    return ret;
}

void TimerApi_InterruptEnable(uint8_t index) {
    switch (index) {
        case 2:
            TMR2_InterruptEnable();
            break;
        case 3:
            TMR3_InterruptEnable();
            break;
        case 4:
            TMR4_InterruptEnable();
            break;
        case 6:
            TMR6_InterruptEnable();
            break;
        default:
            break;
    }
}

void TimerApi_InterruptDisable(uint8_t index) {
    // Also clear the pending IFS flag — Harmony's TMRx_InterruptDisable
    // only clears IEC (enable bit), so a pending IF could still trigger
    // a spurious ISR the moment IEC is re-enabled later (#458 Qodo).
    // For 32-bit pairs T4+T5 and T6+T7 the upper-half timer holds the
    // interrupt (T5IF / T7IF), so case 4 uses T5IF and case 6 uses T7IF.
    switch (index) {
        case 2:
            TMR2_InterruptDisable();
            IFS0CLR = _IFS0_T2IF_MASK;
            break;
        case 3:
            TMR3_InterruptDisable();
            IFS0CLR = _IFS0_T3IF_MASK;
            break;
        case 4:
            TMR4_InterruptDisable();
            IFS0CLR = _IFS0_T5IF_MASK;
            break;
        case 6:
            TMR6_InterruptDisable();
            IFS1CLR = _IFS1_T7IF_MASK;
            break;
        default:
            break;
    }
}

void TimerApi_CallbackRegister(uint8_t index, TMR_CALLBACK callback_fn, uintptr_t context) {
    switch (index) {
        case 2:
            TMR2_CallbackRegister(callback_fn, context);
            break;
        case 3:
            TMR3_CallbackRegister(callback_fn, context);
            break;
        case 4:
            TMR4_CallbackRegister(callback_fn, context);
            break;
        case 6:
            TMR6_CallbackRegister(callback_fn, context);
            break;
        default:
            break;
    }
}