/*
 * DoubleFmt.h — shortest decimal rendering of a double that reads back
 * EXACTLY, for libscpi's SCPIDEFINE_doubleToStr (#1156).
 *
 * THE DEFECT. libscpi formatted every double with "%.15lg". A double carries
 * up to 17 significant digits, so a coefficient needing 16 or 17 could not be
 * read back: `CONFigure:ADC:CHANCALM?` answered a ROUNDED value, the error
 * queue stayed clean, and nothing said so — which makes any read-modify-write
 * of calibration silently lossy. Worse for a client that verifies its own
 * restore: it compares rounded against rounded, so the check passes while the
 * board has drifted. That verification cannot fail on this class of value.
 *
 * WHY NOT JUST "%.17lg". It would fix the round trip and make every ordinary
 * value ugly for every getter that formats a double — 0.1 would answer
 * 0.10000000000000001. So this picks the NARROWEST width that reproduces the
 * value: 15 first, so output stays BYTE-IDENTICAL to the old format wherever
 * 15 digits already sufficed, then 16, then 17.
 *
 * ⭐ THE WIDTH IS CHOSEN BY PARSING THE RESULT BACK, not by a rule about the
 * value. That is the design, not an implementation detail: the check that the
 * text is faithful IS the thing that selects the text, so the two cannot
 * disagree. A rule-based widening ("use 17 when the value looks computed")
 * would be a guess, and it would fail silently in exactly the cases #1156 is
 * about. The verification co-occurs with the thing it guards.
 *
 * ⚠ WHAT THIS CANNOT PROMISE. An exact round trip at every magnitude. #1144
 * measured this toolchain's own conversions as unfaithful at the extremes
 * ("%.17g" of 1e300 gives 9.999999999999998e+299). Where NO width reproduces
 * the value, the widest that fits is returned and
 * `daqifi_double_round_trips()` reports that it did not verify, so a test can
 * STATE which values this platform carries rather than assume all of them do.
 *
 * ⛔ AND AT DBL_MAX THAT FALLBACK IS NOT STRICTLY AN IMPROVEMENT — measured
 * on an NQ1, same board, same getter, both images:
 *
 *     old ("%.15lg")  -> "0e+309"                  parses to 0.0
 *     new (this file) -> "1.797693134862316e+308"  parses to INFINITY
 *
 * The toolchain cannot render DBL_MAX to 17 significant digits at all, so no
 * width round-trips and the 16-digit result is returned — and that rendering
 * rounds UP past the representable maximum, so a client parsing it gets inf
 * for a finite stored value. An earlier revision of this comment claimed the
 * fallback was "never worse than the old behaviour"; 0.0 -> inf does not
 * support that, so the claim is withdrawn rather than softened. Both answers
 * are wrong; inf is at least detectable by an isfinite() check where 0.0
 * looks plausible — the same argument #1199 made for null over a clamped
 * INT32_MAX — but that is a consolation, not a promise.
 *
 * The defect there is the toolchain's conversion, not the digit count, and it
 * is #1144's surface. #1156 is the width, and the width is fixed.
 *
 * ⚠ AND IT NEVER EMITS A TRUNCATED NUMERAL. A numeral cut off by a short
 * buffer is a CORRUPT value, strictly worse than a rounded one — 1.2345e+10
 * truncated to "1.2345e+1" is wrong by a factor of 10^9, not by an epsilon.
 * So a width is only eligible if its rendering FITS; if NO width fits, the
 * empty string and 0 are returned. That is a deliberate difference from the
 * old `snprintf`, which wrote a TRUNCATED numeral and reported `len-1`: an
 * empty reply is recognisably absent, a truncated number is not. It is only
 * reachable through `SCPI_NumberToStr`, which has no caller in
 * `firmware/src`; `SCPI_ResultDouble`'s own buffer is 32 bytes, and the
 * widest rendering is 24, so it always fits there.
 *
 * Header-only and dependency-free, for the same reason as FixedPointFmt.h and
 * SampleEmit.h beside it: the behaviour at the 15/16/17-digit boundaries, at
 * the non-finite values, and in a short buffer can then be checked
 * exhaustively on a host with no stubs. It is reached from the vendored
 * libscpi config.h by an include RELATIVE TO THAT FILE -- the project-rooted
 * `"Util/DoubleFmt.h"` form resolves for the firmware build but NOT for the
 * host tests, which compile libscpi with only their own stub directories on
 * the include path.
 */
#ifndef DAQIFI_UTIL_DOUBLEFMT_H
#define DAQIFI_UTIL_DOUBLEFMT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 15 is what libscpi used, and is the widest width guaranteed to survive a
 * double->text->double trip for values that need no more; 17 is the most a
 * double can ever need (DBL_DECIMAL_DIG). */
#define DAQIFI_DOUBLE_FMT_MIN_DIGITS 15
#define DAQIFI_DOUBLE_FMT_MAX_DIGITS 17

/* "-d.<16 digits>e-308" = 24 chars + NUL. libscpi's SCPI_ResultDouble gives
 * this a 32-byte buffer, so the widest rendering always fits there. */
#define DAQIFI_DOUBLE_FMT_MIN_BUFFER 25

/**
 * Render `v` into `s` using the narrowest of 15/16/17 significant digits that
 * reads back as exactly `v` and fits in `len`.
 *
 * @return strlen of what was written (0 if nothing could be).
 */
static inline size_t daqifi_double_to_str(double v, char *s, size_t len)
{
    int digits;
    size_t best = 0;                    /* length of the best rendering so far */

    if (s == NULL || len == 0) {
        return 0;
    }
    s[0] = '\0';

    for (digits = DAQIFI_DOUBLE_FMT_MIN_DIGITS;
         digits <= DAQIFI_DOUBLE_FMT_MAX_DIGITS;
         digits++) {
        char probe[DAQIFI_DOUBLE_FMT_MIN_BUFFER + 8];
        int n = snprintf(probe, sizeof probe, "%.*lg", digits, v);

        if (n < 0 || (size_t) n >= sizeof probe || (size_t) n >= len) {
            /* ⛔ This width does not fit, but a WIDER ONE STILL MIGHT, so
             * keep going rather than stopping here. `%g` length is NOT
             * monotonic in precision: it switches from `%e` to `%f` once the
             * precision exceeds the decimal exponent, and the `%f` form is
             * often SHORTER. The issue's own reproduction row is an example --
             * 9007199254740991.0 renders as "9.00719925474099e+15" (20 chars,
             * inexact) at 15 digits and "9007199254740991" (16 chars, EXACT)
             * at 16. An earlier revision did `break` here, and so returned
             * the EMPTY STRING for every buffer of 17..20 bytes while a
             * shorter exact rendering was available one width up. */
            continue;
        }
        memcpy(s, probe, (size_t) n + 1u);
        best = (size_t) n;

        /* strtod of "nan" does NOT equal the NaN it came from (no NaN equals
         * itself), so a non-finite value walks all three widths and keeps the
         * last. That is harmless: every width renders a non-finite
         * identically, so the output matches the old "%.15lg" exactly. The
         * SCPI layer's non-finite handling is #1149's, not this file's. */
        if (strtod(s, NULL) == v) {
            break;
        }
    }
    return best;
}

/**
 * Does `v` survive a text round trip through daqifi_double_to_str?
 *
 * Exists so a test can ASSERT which values this platform carries exactly
 * rather than assume all of them do — see the #1144 note in the header
 * comment. Not used by the SCPI path.
 */
static inline bool daqifi_double_round_trips(double v)
{
    char buf[DAQIFI_DOUBLE_FMT_MIN_BUFFER + 8];

    if (daqifi_double_to_str(v, buf, sizeof buf) == 0) {
        return false;
    }
    return strtod(buf, NULL) == v;
}

#endif /* DAQIFI_UTIL_DOUBLEFMT_H */
