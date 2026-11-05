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
 * the value, the widest that fits is returned — the most information
 * available, and never worse than the old behaviour.
 * `daqifi_double_round_trips()` exposes that honestly, so a test can state
 * which values this platform can and cannot carry rather than assume.
 *
 * ⚠ AND IT NEVER EMITS A TRUNCATED NUMERAL. A numeral cut off by a short
 * buffer is a CORRUPT value, strictly worse than a rounded one — 1.2345e+10
 * truncated to "1.2345e+1" is wrong by a factor of 10^9, not by an epsilon.
 * So a width is only eligible if its rendering FITS, and if nothing fits, the
 * narrowest attempt (which is what the old code would have written) is kept.
 *
 * Header-only and dependency-free, for the same reason as FixedPointFmt.h and
 * SampleEmit.h beside it: the behaviour at the 15/16/17-digit boundaries, at
 * the non-finite values, and in a short buffer can then be checked
 * exhaustively on a host with no stubs. It is reached from the vendored
 * libscpi config.h, whose quoted include falls through to the project's
 * `../src` include path.
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
            /* Does not fit the caller's buffer. A wider one never will, so
             * stop and keep whatever narrower rendering already landed. */
            break;
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
