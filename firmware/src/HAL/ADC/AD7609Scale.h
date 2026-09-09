/* ==========================================================================
 * AD7609Scale.h — the AD7609 code space and its raw-code -> volts arithmetic
 *
 * Split out of AD7609.h for #889, which made AD7609_ConvertToVoltage actually
 * apply the per-channel user calibration it had been discarding. The bar for
 * that change is that it stay bit-identical on an uncalibrated board (the
 * shipped defaults are CalM = 1, CalB = 0) across the whole signed 18-bit
 * code space — a claim that is only worth making if it can be checked
 * exhaustively, and there is no NQ3 on the bench to check it on.
 *
 * Header-only and dependency-free ON PURPOSE, the same way
 * Util/FixedPointFmt.h is (#250). AD7609.h pulls in state/board/AInConfig.h,
 * which opens with Harmony's configuration.h / definitions.h and drags in the
 * whole driver tree; a host test cannot include it without faking so much of
 * that graph that the function under test stops resembling the one that
 * ships. Keeping the pure arithmetic here means tests/host/test_ad7609_scale.c
 * compiles the EXACT text XC32 compiles into the firmware, with no stubs.
 *
 * SCOPE: pure arithmetic only — no hardware access, no logging, no board or
 * runtime config lookups. Anything that needs those belongs in AD7609.c.
 * AD7609.h includes this header, so every existing consumer of the constants
 * below still sees them unchanged.
 * ========================================================================== */
#ifndef AD7609SCALE_H
#define AD7609SCALE_H

#include <stdint.h>

/* AD7609 code space: 18-bit, two's complement (datasheet Table 7).
 * Range is -131072 .. +131071; AD7609_MAX_VALUE is the max POSITIVE code. */
#define AD7609_MAX_VALUE         0x1FFFF // 18-bit max value (131071)
#define AD7609_SIGN_BIT          0x20000 // Bit 17 (sign bit for 2's complement)
#define AD7609_SIGN_EXTEND       0xFFFC0000U // Sign extension mask for negative values

/*!
 * Converts a raw 18-bit two's-complement code to a signed 32-bit value.
 *
 * The mask is defensive: callers should only pass 18-bit values, but if upper
 * bits are set (e.g. from a wider register read, or from a value that was
 * already sign-extended once and stored in a 32-bit field) they would corrupt
 * the sign logic.
 *
 * @param[in] rawValue Raw code; only bits [17:0] are considered
 * @return The code as a signed integer in [-131072, +131071]
 */
static inline int32_t AD7609_DecodeSignedCode(uint32_t rawValue)
{
    /* 0x3FFFF = bits [17:0] = AD7609_MAX_VALUE | AD7609_SIGN_BIT */
    int32_t signedValue = (int32_t)(rawValue & (AD7609_MAX_VALUE | AD7609_SIGN_BIT));

    /* If bit 17 is set the value is negative and needs sign extension. */
    if (signedValue & AD7609_SIGN_BIT) {
        signedValue |= AD7609_SIGN_EXTEND;  // Sign extend from 18 bits to 32 bits
    }

    return signedValue;
}

/*!
 * Applies the module range and the channel's user calibration to a decoded
 * code.
 *
 * Deliberately the same shape as MC12b_ConvertToVoltage (MC12bADC.c): the
 * gain MULTIPLIES the scaled value and the offset is added LAST, i.e.
 * volts = scaled * calM + calB, never (scaled + calB) * calM. Both converters
 * are fed from the same AInRuntimeConfig.CalM / CalB pair by the same SCPI
 * commands (CONFigure:ADC:chanCALM / chanCALB), so a client that calibrates
 * an NQ1 and an NQ3 with one procedure has to get the same answer from both.
 *
 * With the shipped defaults (calM = 1.0, calB = 0.0) the result is bit-
 * identical to the uncalibrated expression: x * 1.0 is exact for every finite
 * x, and the only value that could make `+ 0.0` observable is -0.0, which this
 * expression never produces (code 0 scales to +0.0).
 *
 * @param[in] signedCode Decoded code, from AD7609_DecodeSignedCode
 * @param[in] fullScale  Module range in volts (the +/- full-scale magnitude)
 * @param[in] calM       Channel gain calibration (1.0 = uncalibrated)
 * @param[in] calB       Channel offset calibration in volts (0.0 = uncalibrated)
 * @return The calibrated voltage
 */
static inline double AD7609_ScaleToVolts(int32_t signedCode,
                                         double  fullScale,
                                         double  calM,
                                         double  calB)
{
    return ((double)signedCode / (double)AD7609_MAX_VALUE) * fullScale * calM
           + calB;
}

#endif /* AD7609SCALE_H */
