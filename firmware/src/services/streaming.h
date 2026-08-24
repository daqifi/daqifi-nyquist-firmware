#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "../state/board/BoardConfig.h"
#include "../state/runtime/BoardRuntimeConfig.h"
#include "../state/data/BoardData.h"
#include "../state/data/AInSample.h"


#ifdef	__cplusplus
extern "C" {
#endif

//! Buffer size used for streaming purposes
#define STREAMING_BUFFER_SIZE               ENCODER_BUFFER_SIZE

// Streaming frequency limits (validated via benchmark testing, see issue #215)
//
// Three independent constraints limit the maximum streaming frequency:
//
// 1. ISR ceiling: hard limit on timer interrupt rate regardless of channel count.
//    Fixed per-invocation cost (context switch, task notify, pool alloc, queue push).
//    Benchmark: 1ch@11kHz PASS, 1ch@12kHz FAIL.
//
// 2. Type 1 aggregate: dedicated ADC modules convert simultaneously, but the
//    deferred task channel loop + encoder cost scales with Type 1 count.
//    Benchmark: 5ch@6kHz PASS (all Type 1), 5ch@6.2kHz FAIL.
//
// 3. Per-tick budget: every ISR tick iterates ALL enabled channels (sample copy,
//    test pattern, encode). More channels = more work per tick = lower max freq.
//    Benchmark: 16ch@3.5kHz PASS, 16ch@3.75kHz FAIL.
//
// Effective limit: min(ISR_MAX, TYPE1_AGG / type1Count, BUDGET / (OVERHEAD + total))
// WiFi SPI DMA staging buffer limits
#define WIFI_DMA_MAX  (32U * 1024U)  // 32KB max (benchmarked: fixes CSV 8ch/16ch drops)
#define WIFI_DMA_MIN  (2U * 1024U)   // 2KB min (enough for WINC1500 control plane)

// Benchmark mode levels (extensible — add new values for future modes)
#define BENCHMARK_OFF      0   // Normal: freq cap active, real ADC
#define BENCHMARK_NOCAP    1   // Bypass freq cap, real ADC timing
#define BENCHMARK_PIPELINE 2   // Bypass freq cap + skip ADC entirely. Sample
                               // values are generated directly without touching
                               // ADC hardware (uses current test pattern; if
                               // pattern=0 then values will be 0). Timestamp
                               // comes from the streaming timer tick (same
                               // source as normal-mode AInSample timestamps),
                               // so output remains comparable across modes.

// Frequency cap constants — fitted to characterization data (2026-04-13).
// ISR batching (#277) reduced Type 1 overhead; refit with updated ceilings.
// ISR_MAX raised 13000->16000 (#524): the per-interface TRANSPORT caps
// (Streaming_TransportMaxFreq) now govern the real ceilings; ISR_MAX is the
// ADC/ISR safety ceiling and must sit above the highest single-channel transport
// cap (USB PB 1ch = 22000 as of the 2026-07-05 252 MHz refit) so it does not
// override it. NQ1 note: the ADC additive model binds T1-only cells below this
// anyway; NQ2/NQ3's legacy formula keeps its own 16000 start via
// STREAMING_ISR_MAX_HZ_LEGACY below.
// #832: the highest CONFigure:VOLTage:PRECision the pure-T1 CSV refit was measured
// at, and therefore the highest it may be applied at. NQ1's shipped default is
// 4; 0..3 drive an encoder that is cheaper or equal (0 is int_to_str, 1..3 emit
// fewer characters), so the precision-4 basis is never-over for them. 5..10
// emit MORE per value and have no basis, so they keep the #563 law.
#define STREAMING_CSV_REFIT_MAX_PRECISION 4
#define STREAMING_ISR_MAX_HZ        22000
// NQ2/NQ3 legacy-formula start value and additive-clamp ceiling for paths
// characterized only at 16 kHz (pre-252 MHz basis). Do not raise without a
// per-variant review.
#define STREAMING_ISR_MAX_HZ_LEGACY 16000
#define STREAMING_TYPE1_AGG_MAX_HZ  55000
#define STREAMING_TICK_BUDGET       110000
#define STREAMING_TICK_OVERHEAD     6

// Per-interface, per-format wire/storage transport caps live in
// Streaming_TransportMaxFreq below (#524), which superseded the earlier
// WiFi-only budget term (#520/#522) and generalized it to all interfaces.

// Type 2 (shared MODULE7 mux) channels are scanned sequentially via the analog
// multiplexer. The former fixed 1 kHz throttle (ChannelScanFreqDiv = freq/1000)
// and its up-front reject (#232) were REMOVED in #107: real-ADC characterization
// (18-pass overnight matrix, daqifi-python-test-suite benchmarks/107_*) showed the
// mux scan never overruns up to >=40 kHz at any channel count. ChannelScanFreqDiv
// is now always 1 (T2 scans every tick = real full-rate data), and T2 is bounded
// by the same per-interface/format transport cap as T1 (Streaming_TransportMaxFreq).

/**
 * Compute maximum safe streaming frequency for a given channel configuration.
 * Uses three-constraint model validated against empirical benchmark data.
 *
 * @param type1Count            Number of enabled Type 1 (dedicated ADC) channels
 * @param totalEnabledChannels  Total number of enabled public ADC channels
 * @return Maximum safe frequency in Hz
 */
static inline uint32_t Streaming_ComputeMaxFreq(uint32_t type1Count, uint32_t totalEnabledChannels) {
    uint32_t maxFreq = STREAMING_ISR_MAX_HZ_LEGACY;

    // Type 1 aggregate constraint
    if (type1Count > 0) {
        uint32_t type1Max = STREAMING_TYPE1_AGG_MAX_HZ / type1Count;
        if (type1Max < maxFreq) maxFreq = type1Max;
    }

    // Per-tick budget constraint (all enabled channels add per-tick cost)
    if (totalEnabledChannels > 0) {
        uint32_t tickMax = STREAMING_TICK_BUDGET / (STREAMING_TICK_OVERHEAD + totalEnabledChannels);
        if (tickMax < maxFreq) maxFreq = tickMax;
    }

    if (maxFreq == 0) maxFreq = 1;
    return maxFreq;
}

/**
 * #557: NQ1 freeze-aware additive ADC/scan cap (Hz).
 *
 * period_ns = base + arm*(scan armed) + cT1*nT1 + cT2*nT2_user + cMon*nMon
 *
 * Originally fitted to the 2026-06-22 freeze-aware ceiling sweep (#563),
 * replacing the drop-blind tick-budget / type1Agg / MC12b_ScanMaxFreq terms
 * FOR NQ1 only. #596 (2026-07-06, 252 MHz three-night grid) re-fitted the
 * PB path as a PIECEWISE model split by scan class — pure-T1 (no scan),
 * armed OBDiag-off, and OBDiag-on (the last keeps the #563 coefficients,
 * 600 s at-cap revalidated) — because a single additive law under-fit the
 * grid (tightness 0.74). CSV keeps the #563 single law EXCEPT for the pure-T1
 * (no-scan) class, which #832 re-fitted at 252 MHz and at CONFigure:VOLTage:PRECision 4
 * -- the NQ1 shipped default, and a materially more expensive encoder path than
 * the precision-0 integer fast path every earlier CSV basis was measured on.
 * JSON shares the CSV branch but is EXCLUDED from that refit (see below).
 *   - PB: margin 0.88 keeps each class a safe never-over envelope.
 *   - CSV is byte/transport-bound (noisier) -> margin 0.80 here, AND the
 *     per-format transport term is still min()'d downstream (binds CSV lower).
 * NQ2/NQ3 (AD7609 — no MODULE7 scan, different timing) keep the legacy formula.
 * Margins verified per-config by the #557 at-cap revalidation. nMon = monitoring
 * channels in the scan (8 when OBDiag on, else 0); arm = any scanned channel.
 */
static inline uint32_t Streaming_AdcAdditiveCap_NQ1(uint32_t nT1, uint32_t nT2user,
                                                    uint32_t nMon, uint32_t isProtoBuf,
                                                    uint32_t isJson,
                                                    uint32_t voltagePrecision) {
    uint32_t armed = (nT2user > 0u || nMon > 0u) ? 1u : 0u;
    uint64_t period_ns, num;
    if (isProtoBuf) {
        /* #596 (2026-07-06, 252 MHz re-fit): three-night fine-grain grid
         * (600 s freeze-aware endurance, benchmarks/overnight_*additive_grid*)
         * showed a single additive law under-fits - the armed classes are
         * fitted separately (never-over the worst-night clean basis):
         *   armed=0 (pure T1, no scan):  43200 + 2300*nT1   (+21..26%)
         *   armed=1, OBDiag off:         58000 + 2160*nT1 + 10000*nT2 (+31..33%)
         *   OBDiag on (nMon>0):          UNCHANGED - every cell 600 s at-cap
         *                                validated 2026-07-06 (12/12 clean);
         *                                the grid's lower readings were
         *                                1000 Hz-sweep lower bounds.
         * cT1=2160 in the armed class is carried from the prior fit (grid
         * fits it to ~0; keeping it is strictly conservative for mixed
         * configs, which have no dedicated OBD=OFF basis cells). */
        if (nMon > 0u) {
            period_ns = 55300ULL + 20000ULL*armed + 2160ULL*nT1 + 13470ULL*nT2user + 2260ULL*nMon;
        } else if (armed != 0u) {
            period_ns = 58000ULL + 2160ULL*nT1 + 10000ULL*nT2user;
        } else {
            /* #714/#90 refit (2026-07-23): the #596 pure-T1 term (43200+2300*nT1)
             * over-caps USB PB at 252 MHz — at-cap silently drops. Freeze-aware
             * walk-down soaks (COM3 …E8A7, 100 s/step, atcap_20260723_013919.csv)
             * measured the true zero-loss ceilings: 1xT1 16900, 3xT1 15350,
             * 5xT1 14075 (vs the enforced 19340/17564/16087 — ~14% too high).
             * Re-fit ~7% under the measured ceilings (never-over): 1xT1 15799,
             * 3xT1 14263, 5xT1 12998. Binds USB PB pure-T1 only (SD is SdAdditive-
             * bound lower, WiFi/USB+SD transport-bound lower). */
            period_ns = 52700ULL + 3000ULL*nT1;
        }
        num = 880000000ULL;   /* 1e9 * 0.88 (PB safe envelope) */
    } else if (armed == 0u && isJson == 0u
               && voltagePrecision <= STREAMING_CSV_REFIT_MAX_PRECISION) {
        /* #832 (2026-08-23): pure-T1 CSV/CsvCompact, re-fitted at 252 MHz AND
         * at CONFigure:VOLTage:PRECision 4 -- the NQ1 SHIPPED DEFAULT.
         *
         * WHY PRECISION IS NAMED HERE. csv_encoder takes an integer fast path
         * (int_to_str) at precision 0 and formats a float per channel per
         * sample at 4. The first attempt at this refit was fitted to ceilings
         * measured on a bench board whose NVM held 0, and the device then
         * dropped 21,286 samples AT ITS OWN NEW CAP -- the #714/#715 failure
         * mode, reached through the instrument rather than through the fit.
         * An A/B at one rate measured precision 0 clean vs precision 4 losing
         * 10.8%, at byte rates within 1% of each other: encoder CPU, not
         * bandwidth. The basis below is measured with precision PINNED to 4
         * and recorded per row (test-suite #233), 600 s freeze-aware soaks
         * (ScanStaleDropped and T1ArdyMisses both count as loss), real ADC,
         * OBDiag off, USB, board 7E2898F46200E8A7.
         *
         * BASIS (600 s per step, board 7E2898F46200E8A7, fw 3.7.2 crc32
         * 62067B0C, atcap_20260823_0737/0809/0850 in the test suite):
         *   nT1=1  clean 16413   FAIL 17472 (T1ArdyMisses 845)
         *   nT1=3  clean 12285   FAIL 13230 (T1ArdyMisses 228)
         *   nT1=5  clean  9386   FAIL 10240 (T1ArdyMisses   5)
         * Every ceiling here is bounded by T1ArdyMisses -- the ARDY-gated
         * direct read missing a conversion (#541) -- NOT by a transport drop,
         * so this really is an ADC-side term and belongs in this model.
         *
         * ONLY nT1==1 MOVES: 10589 -> 15263 Hz (800e6/52412, i.e. 93.0% of the
         * measured 16413). n=3 and n=5 have real headroom (12285 against an
         * enforced 9450; 9386 against 8533) but n=2 and n=4 have no
         * measurement, and bracketing them by the nearest measured ceiling
         * ABOVE (ceilings fall monotonically with channel count) puts n=4's
         * never-over bound at 0.93*9386 = 8729 -- BELOW its current enforced
         * 8968. So any line covering n>=2 would have to REGRESS n=4 to stay
         * never-over, and raising it instead would be an unmeasured guess.
         * n=4 is not over-capped today (8968 <= 9386); there is simply no
         * basis to move it. A 2/4-channel grid is the follow-up that unlocks
         * the rest, the way #596's fine-grain grid did for PB.
         *
         * SINGLE-CHANNEL SPECIAL CASE, for the reason Streaming_TransportMaxFreq
         * gives for its own: "1-channel sits far above the multi-channel curve,
         * which a single A/(B+n) cannot hug". Forcing one straight line through
         * the 1-channel and multi-channel points spends nearly all of the
         * 1-channel headroom to reach the others.
         *
         * JSON IS EXCLUDED and keeps the #563 law. It shares this branch, and
         * at USB 1ch the additive is its BINDING term (10589, below its own
         * #529/#831 transport single of 11000) -- so raising this branch would
         * lift JSON's enforced cap on the strength of a CSV measurement, and
         * JSON has never been characterised at precision 4. Its own precision-4
         * basis is #529 follow-up work. CsvCompact IS included: it emits
         * strictly fewer bytes per row than CSV (#619), so a CSV-fitted cap is
         * never-over for it. */
        /* PRECISION-GATED. The basis is precision 4, and precision changes how
         * much work csv_encoder does per value, so the raise may only be
         * applied where the encoder is no more expensive than it was when
         * measured:
         *   0      integer fast path (int_to_str) -- strictly cheaper.
         *   1..4   same float path, FEWER OR EQUAL characters emitted.
         *   5..10  MORE characters per value, and NOT measured.
         * So >4 falls through to the #563 law, which is unchanged and already
         * safe at every precision. Without this gate a user who set precision
         * 7 or 10 on an NQ1 would get a cap fitted for a cheaper encoder --
         * an over-cap of exactly the kind this ticket exists to prevent, just
         * reached through a setting instead of through a fit. */
        if (nT1 <= 1u) {
            /* 52412 ns -> 800e6/52412 = 15263 Hz. The MINIMAL never-over
             * period is 52411 (the fit recipe prints that); both floor to the
             * same 15263 Hz, so the extra nanosecond is free conservatism and
             * the two numbers do not disagree. */
            period_ns = 52412ULL;
        } else {
            /* nT1 >= 2 keeps the #563 single law, unchanged. The 3xT1 and 5xT1
             * ceilings measured at precision 4 leave no room to raise n=2 and
             * n=4 under the never-over + no-regress constraints, and n=2/n=4
             * have no measurement of their own -- bracketing them by the
             * nearest measured ceiling ABOVE (ceilings fall monotonically with
             * channel count) puts the bound below their CURRENT enforced cap.
             * Raising them would be a guess, and this ticket exists because
             * the last guess here dropped data at its own cap. */
            period_ns = 71000ULL + 4550ULL*nT1;
        }
        num = 800000000ULL;   /* 1e9 * 0.80 (CSV; transport min'd downstream) */
    } else {
        period_ns = 71000ULL + 21300ULL*armed + 4550ULL*nT1 + 15190ULL*nT2user + 2680ULL*nMon;
        num = 800000000ULL;   /* 1e9 * 0.80 (CSV; transport min'd downstream) */
    }
    uint32_t hz = (uint32_t)(num / period_ns);   /* period_ns always >= base, no div-by-0 */
    if (hz > STREAMING_ISR_MAX_HZ) hz = STREAMING_ISR_MAX_HZ;
    return (hz == 0u) ? 1u : hz;
}

/**
 * #574: NQ1 SD-PB sustainable-rate cap (Hz). The SD writer task (pri 5) loses
 * CPU to the ADC scan's per-conversion data-ready + EOS ISRs, so a scan-armed
 * config sustains a LOWER zero-loss SD rate than a pure-T1 (no-scan) config.
 * The interface-agnostic ADC additive cap and the byte-rate transport cap both
 * MISS this (neither models SD-writer starvation), so several scan-armed
 * configs were enforced ABOVE their true SD ceiling and silently dropped
 * (SdDroppedBytes > 0) — issue #574.
 *
 *   period_ns = base + arm*armed + cT1*nT1 + cT2*nT2user
 *   hz        = 1e9 / period_ns ;   armed = (nT2user>0 || nMon>0)
 *
 * Fitted by LP (never-over) to the 2026-06-30 MULTI-TRIAL SD ceilings. (The
 * single-pass campaign was unreliable — transient SD-GC stalls produced
 * spurious leaks; 3xT1's single-pass "dip" below its neighbours was the giveaway
 * and re-tested clean.) Constraints: cap <= measured ceiling for the 6 genuine
 * over-cap configs (1xT2, 3xT2, 1xT1+OBDiag, 5xT1+OBDiag, 5T1+3T2, 5T1+5T2),
 * AND cap >= the existing enforced cap for the pure-T1 / published configs so
 * their existing terms still bind (1ch=9000, 5ch=7500, 10ch, 16ch unchanged).
 * cMon fit to 0 — monitoring load is captured by `armed`. CSV is byte-bound
 * (the transport term binds it below this), so this is PB-only. Applied for the
 * SD interface only; UsbAndSd is uncharacterized (separate follow-up).
 */
static inline uint32_t Streaming_SdAdditiveCap_NQ1(uint32_t nT1, uint32_t nT2user,
                                                   uint32_t nMon, uint32_t isProtoBuf) {
    if (isProtoBuf == 0u) {
        return STREAMING_ISR_MAX_HZ;   /* CSV is byte-bound: the transport term binds it */
    }
    uint32_t armed = (nT2user > 0u || nMon > 0u) ? 1u : 0u;
    uint64_t period_ns;
    if (armed != 0u) {
        /* Armed (scan running) cells UNCHANGED from the #574 fit — every armed
         * SD-PB cell (1xT2, mixed, OBDiag) at-cap-validated clean 2026-07-23.
         * 157007 = the old 93539 base + 63468 armed offset. */
        period_ns = 157007ULL + 7959ULL*(uint64_t)nT1 + 4615ULL*(uint64_t)nT2user;
    } else {
        /* #714 refit (2026-07-23): the whole pure-T1 (no-scan, OBDiag=off) branch
         * was over-high. The old 93539+7959*nT1 curve capped 1xT1 at 9852, which
         * at-cap silently dropped 2.57M bytes/100 s (measured ceiling 8600); the
         * higher-nT1 cells of that curve (2xT1 9137, 3xT1 8517, 4xT1 7979) sit in
         * the SAME over-high regime — the SD-PB transport term (99000/(4+n):
         * 2ch 16500 ... 5ch 11000) is well ABOVE them, so this additive binds and
         * the old values were enforced with no measured backing. The pure-T1 SD
         * Hz ceiling is roughly FLAT (SD-writer per-tick cost dominates, not bytes):
         * re-fit ~5-8% under the flat ceiling — 1xT1 7900, 2xT1 7795, 3xT1 7694,
         * 4xT1 7592, 5xT1 7499. BOTH endpoints at-cap-validated zero-loss (1xT1
         * @7900, 5xT1 @7499, atcap_20260723_025553.csv); 2/3/4xT1 are bracketed on
         * this monotonic curve. A precise per-channel multi-T1 SD ceiling sweep
         * (to reclaim any headroom) is a #714 follow-up — never-over holds now. */
        period_ns = 124890ULL + 1692ULL*(uint64_t)nT1;
    }
    uint32_t hz = (uint32_t)(1000000000ULL / period_ns);  /* period_ns >= base, no div-by-0 */
    if (hz > STREAMING_ISR_MAX_HZ) hz = STREAMING_ISR_MAX_HZ;
    return (hz == 0u) ? 1u : hz;
}

/**
 * Per-interface, per-format TRANSPORT wire-rate cap (Hz) — generalizes the
 * WiFi-only term (#520) to USB / SD / USB+SD (#524).  Fitted to the 3-run
 * real-ADC zero-loss characterization (matrix_524 run-1/2/3 + WiFi-v2,
 * conservative min across runs).  Form: single-channel special-cased + A/(B+n)
 * for n>=2 (the "F3" fit) — 1-channel (esp. CSV) sits far above the
 * multi-channel curve, which a single A/(B+n) cannot hug.  Every predicted cap
 * is <= the measured zero-loss ceiling at the tested channel counts (safe by
 * construction; tightness 86-100%).  This closes the prior format-blind hole
 * where high-channel CSV was capped well ABOVE its true ceiling (silent loss).
 * JSON is now SPLIT (#529). On USB/NQ1 it carries its OWN bench-measured
 * coefficients (single 11000, 32000/(2+n)) and skips the derate entirely —
 * `jsonFitted` marks that. Everywhere else — WiFi, SD, USB+SD, and every
 * interface on NQ2/NQ3 — JSON still uses the CSV coefficient family plus the
 * /2 placeholder, which remains uncharacterized there. NQ2/NQ3 are excluded
 * deliberately: their wider ADC codes cost more bytes/sample, so an NQ1-fitted
 * Hz cap would over-cap them.
 *
 * The #562 note still holds for the un-fitted paths: JSON does NOT inherit the
 * 252 MHz CSV transport raise (guarded by `isNQ1 && !json`), because raising it
 * on top of the /2 placeholder could over-cap JSON's uncharacterized byte cost.
 * On USB/NQ1 that guard is now moot — the measured branch is taken first.
 * Only meaningful for the ACTIVE interface; ComputeMaxFreqForConfig gates on it.
 *
 * @param interface      StreamingInterface (USB / WiFi / SD / UsbAndSd)
 * @param encoding       StreamingEncoding (PB vs CSV/JSON)
 * @param totalChannels  Total enabled public ADC channels
 * @param isNQ1          1 for NQ1 (12-bit MC12b) — use the 2026-07-05 252 MHz PB
 *                       transport refit (#595). 0 for NQ2/NQ3 (24-bit AD7173 /
 *                       18-bit AD7609), which keep the pre-#595 (200 MHz)
 *                       conservative PB caps: the #595 raise was characterized on
 *                       NQ1 only, and NQ2/NQ3 emit wider PB varints per sample, so
 *                       the same Hz pushes more transport bytes — the NQ1-raised Hz
 *                       cap would over-cap them and risk silent transport overflow.
 * @return transport-limited max frequency in Hz
 */
/* #619: both CSV encodings share the CSV encoder, the CSV header and the CSV
 * cap coefficients — they differ only in whether a row carries one timestamp or
 * N identical ones. Call sites must test the FAMILY, not the single value, or
 * compact CSV silently falls through to whatever branch follows. */
static inline bool Streaming_EncodingIsCsv(StreamingEncoding e)
{
    return (e == Streaming_Csv) || (e == Streaming_CsvCompact);
}

static inline uint32_t Streaming_TransportMaxFreq(StreamingInterface interface,
                                                  StreamingEncoding encoding,
                                                  uint32_t totalChannels,
                                                  uint32_t isNQ1) {
    if (totalChannels == 0) return STREAMING_ISR_MAX_HZ;
    /* Explicit encoding handling (Qodo): unknown encodings cap at 1 Hz so a
     * future/garbage value can never over-cap. */
    uint32_t pb = 0u, json = 0u;  /* init pb defensively (Qodo pass-7); every
                                   * non-default case still assigns it explicitly */
    /* #529: set when an interface supplies MEASURED JSON coefficients, so the
     * blanket x0.5 placeholder at the bottom is skipped for that interface
     * only. Interfaces still uncharacterised keep the derate. */
    uint32_t jsonFitted = 0u;
    switch (encoding) {
        case Streaming_ProtoBuffer: pb = 1u; break;
        case Streaming_Csv:         pb = 0u; break;
        /* #619: compact CSV emits strictly FEWER bytes per row than CSV, so the
         * CSV coefficients are a conservative (never-over) bound for it. Note
         * this case is required, not optional -- the `default` below caps an
         * unrecognised encoding at 1 Hz. */
        case Streaming_CsvCompact:  pb = 0u; break;
        case Streaming_Json:        pb = 0u; json = 1u; break;  /* CSV coefficients, derated below */
        default:                    return 1u;
    }
    uint32_t single, A, B;
    switch (interface) {
        case StreamingInterface_USB:
            /* PB refit 2026-07-05 @252 MHz (#487 harvest, two-night 600 s
             * worst-night basis, refit_252_transport.py in the test suite):
             * transport-bound cells measured 1ch=22000 / 5ch(T1)=20000 clean
             * both nights -> single 22000, A/(B+n)=120000/(1+n) through the
             * 5ch point (raise-only vs the old curve at every n; mid-n values
             * shelter under the ADC additive model and the ISR clamp). Note:
             * the additive models (fitted at 200 MHz) now bind most cells --
             * their 252 MHz re-fit is the follow-up that unlocks the rest. */
            /* #595 252 MHz USB-PB raise is NQ1-only; NQ2/NQ3 keep the pre-#595
             * (200 MHz) USB-PB curve — their wider ADC samples cost more
             * bytes/sample, so the NQ1-raised Hz cap would over-cap them. */
            if (pb) {
                if (isNQ1) { single = 22000u; A = 120000u; B =  1u; }
                else       { single = 15000u; A = 180000u; B = 10u; }
            }
            /* CSV transport refit 2026-07-21 @252 MHz (#562): NQ1 transport-bound
             * cells measured 1ch(T1)=20000 / 5ch(T1)=15000 600 s-endurance-clean,
             * board-validated on two units (…E8A7 + …0292 agree on the T1/CSV
             * ceilings). single 15000->20000, A/(B+n) 34000/(1+n)->90000/(1+n)
             * through the 5ch point (raise-only at every n). This retires the
             * stale 200 MHz CSV transport curve as a binder for multi-channel, so
             * the (conservative, 200 MHz-fitted) ADC additive term binds instead:
             * 5xT1 5666->8533, 10ch 3090->4188, 16ch 2000->2835 -- each well under
             * the measured NOCAP ceiling and at-cap-validated. NQ1 ONLY (NQ2/NQ3
             * wider ADC samples cost more bytes/sample -> the NQ1 Hz cap would
             * over-cap them). JSON is DECOUPLED (kept on the old CSV coeffs, then
             * /2 downstream): its 2-3x-CSV byte cost is uncharacterized, so the
             * ×0.5 placeholder must not inherit the raise. The CSV *additive*
             * refit (unlocks the additive-bound 1ch/T2 cells, e.g. 1xT1 10589 vs
             * measured 20000) needs a fine grid -> tracked #562/#529 follow-up. */
            else if (json && isNQ1) {
                /* #529 JSON transport fit, measured 2026-08-21 on NQ1/USB.
                 * NOCAP ceiling sweep, each ceiling then held 120 s with zero
                 * drops on every counter (1xT1 reproduced on two passes):
                 *   1ch (1xT1 OBD=OFF) 12000 Hz  563 KB/s
                 *   5ch (5xT1 OBD=OFF)  7000 Hz  951 KB/s
                 *  10ch (5T1+5T2)       3000 Hz  737 KB/s
                 *  16ch (5T1+11T2)      2000 Hz  755 KB/s
                 * single 11000 and 32000/(2+n) sit at 89% of measured at n=1,
                 * 10 and 16 -- inside the 86-100% tightness band the other
                 * transport fits use.
                 *
                 * n=5 lands at 65%, deliberately. No A/(B+n) can pass through
                 * both 7000@5 and 2000@16 with positive B (solving gives
                 * B=-0.6): 5xT1 arms no MODULE7 scan while the 10ch and 16ch
                 * configs do, so the measured curve is steeper than this form.
                 * Fitting UNDER the 5ch point is the safe side of that -- the
                 * 5ch headroom is real but unreachable through this curve,
                 * the same way CsvCompact's is.
                 *
                 * NOTE the single (n=1) value is currently DORMANT. The
                 * enforced cap is min(additive, transport), and for JSON the
                 * additive model runs its CSV-class branch (isProtoBuf=0,
                 * x0.80 envelope): 1xT1 gives 800e6/(71000+4550) = 10589 Hz,
                 * below this 11000, so the additive binds and the single never
                 * applies. Confirmed on the bench -- the device reports exactly
                 * 10589 for that config. It is set to the measured-safe
                 * transport value anyway (<= the 12000 ceiling) so it is
                 * correct if a JSON-specific ADDITIVE fit later lifts that
                 * bound; characterising the additive for JSON is the follow-up
                 * that would unlock the remaining 1-channel headroom.
                 *
                 * NQ2/NQ3 are NOT covered: their wider ADC codes cost more
                 * bytes/sample, so an NQ1-fitted Hz cap would over-cap them.
                 * They fall through to the CSV coefficients + the x0.5 below. */
                single = 11000u; A = 32000u; B = 2u;
                jsonFitted = 1u;
            }
            else {
                if (isNQ1 && !json) { single = 20000u; A = 90000u; B = 1u; }
                else                { single = 15000u; A = 34000u; B = 1u; }
            }
            break;
        case StreamingInterface_WiFi:
            /* Refit 2026-06-11 (take-5 walk-down soaks, T2-only,
             * atcap_20260611_045901.csv) — the first endurance basis with
             * the #537 fix in.  All earlier WiFi OBDiag=off soak bases
             * (2026-06-02 sweep, 2026-06-10 atcap) were measured with the
             * MODULE7 scan silently skipped (#537): frozen T2 data and no
             * EOS-task load, so the device sustained ~1.5x the honest
             * rate.  With the scan actually running, walk-down ceilings:
             *
             *   PB : 1ch 5175 · 3ch 4225 · 5ch 4000 · 8ch 3675 · 11ch 3400
             *        -> single 5175; A/(B+n) refit 210000->139000 (B=30):
             *        139000/(30+n) = 4212/3971/3658/3390 — under every
             *        measured cell within 1%.
             *   CSV: 1ch 4675 · 3ch 3050 · 5ch 2857 / 8ch 2000 / 11ch 1538
             *        AT CAP (zero-drop at the existing 20000/(2+n) curve).
             *        Keep the curve (validated at cap for n>=5 same-night);
             *        single 8000->4675; clamp low-n multi to the measured
             *        3-ch ceiling 3050 (the curve over-caps n=2..4, where
             *        higher Hz costs more per-tick overhead than the
             *        hyperbola predicts).
             *
             * Night-to-night link variation remains (the 06-10 basis held
             * its caps clean; tonight needed 0.66x) — static caps are
             * worst-observed-safe, runtime AIMD (#523) is the real fix. */
            /* PB single refit 2026-07-05 @252 MHz: 1ch cells 600 s-clean at
             * 8000-9000 both nights (worst-night basis); single 5175 -> 8000.
             * The multi-channel curve is UNCHANGED: its two-night basis had
             * soak-unproven (leaked-at-ceiling) points at 5/8/10 ch which the
             * never-over discipline penalizes below the current curve -- a
             * raise there needs a third night or the additive-model re-fit. */
            /* #595 raised only the PB single-channel term (5175->8000) at 252 MHz
             * on NQ1; NQ2/NQ3 keep 5175. The multi-channel curve was unchanged by
             * #595, so both variants share A/(B+n). */
            if (pb) { single = isNQ1 ? 8000u : 5175u; A = 139000u; B = 30u; }
            else    { single =  4675u; A =  20000u; B =  2u; }
            break;
        case StreamingInterface_SD:
            /* PB refit 2026-07-05 @252 MHz (see USB note): transport-bound
             * cells 1ch=13000 / 5ch(T1)=11000, 600 s-clean both nights ->
             * single 13000, A/(B+n)=99000/(4+n) (raise-only at every n).
             * The SD-additive model (#574, 200 MHz fit) now binds most SD
             * cells (e.g. 1xT1 at 9852, 5xT1 at 7500) -- its re-fit is the
             * follow-up. */
            /* #595 252 MHz SD-PB raise is NQ1-only; NQ2/NQ3 keep the pre-#595
             * (200 MHz) SD-PB curve. */
            if (pb) {
                if (isNQ1) { single = 13000u; A =  99000u; B =  4u; }
                else       { single =  9000u; A = 150000u; B = 15u; }
            }
            /* SD CSV A 42000->36000 (2026-07-09): the 8 h freeze-aware soak
             * dropped SD bytes at the 10ch cap (5T1+5T2 @ 1909 = 42000/22) in
             * 2/7 rounds -- byte-rate-limited (sdDrop, no wedge). sdDrop scales
             * with the high-channel byte-rate asymptote (~A*bytes/sample), so
             * lowering A pulls the many-channel ceiling under the SD write
             * limit (10ch 1909->1636, ~14% margin) while the clean low-channel
             * cells only gain headroom. Single-channel (7500) unaffected. */
            else    { single =  7500u; A =  36000u; B = 12u; }
            break;
        case StreamingInterface_UsbAndSd:
            if (pb) { single =  8000u; A =  66000u; B =  6u; }
            /* #719: the CSV single-channel cap of 8000 silently dropped SD data
             * at-cap (nightly soak: USB+SD CSV 1xT1 @8000 leaked 5/5 rounds;
             * walk-down COM3 …E8A7: @8000 LEAK sdDrop=595832, @7000 clean). A
             * 200 MHz-era coefficient — the #712 252 MHz CSV refit covered USB
             * only, not USB+SD. Lower single 8000->6500 (never-over, ~7% under
             * the proven-clean 7000).
             * NON-MONOTONIC BY PHYSICS (not a fit error): the multi-channel curve
             * (15000/(0+n): 2ch 7500, 3ch 5000) was walk-down-VALIDATED clean at
             * cap (2ch@7500, 3ch@5000, 0 drops/100s), yet 1ch leaks at a LOWER
             * rate (~7000) than 2ch sustains (>=7500). Cause: a 1ch CSV row is
             * tiny, so at 8 kHz the SD writer does many small (sub-sector)
             * writes -> per-write overhead binds 1ch below 2ch's wider-row rate.
             * So 1ch (6500) < 2ch (7500) is real; each cap is individually
             * never-over-validated. The usual F3 "single >= curve" shape does not
             * hold for this cell, so leave both at their measured-safe values
             * rather than force monotonicity (which would waste the validated
             * 2ch headroom). */
            else    { single =  6500u; A =  15000u; B =  0u; }
            break;
        default:
            return 1u;  /* unknown/corrupted interface -> fail-safe floor, never over-cap (Qodo) */
    }
    /* Widen the divide to 64-bit so a corrupted/huge channel count can never wrap
     * the denominator (Qodo pass-8 hardening). denom is always >= 2 in this branch
     * (totalChannels >= 2, B >= 0), so no div-by-zero guard is needed. */
    uint32_t hz = single;
    if (totalChannels != 1u) {
        uint64_t denom = (uint64_t)B + (uint64_t)totalChannels;
        hz = (uint32_t)((uint64_t)A / denom);
        /* WiFi CSV low-n clamp (2026-06-11 take-5): 20000/(2+n) is
         * validated AT CAP for n>=5 but over-caps n=2..4 — the measured
         * 3-ch soak ceiling is 3050 while the curve gives 4000.  Clamp
         * multi-channel WiFi CSV to that measured ceiling; transparent
         * for n>=5 where the curve is already below it. */
        if (interface == StreamingInterface_WiFi && !pb && hz > 3050u) {
            hz = 3050u;
        }
    }
    /* JSON emits ~2-3x CSV bytes/sample (object braces + per-sample field names),
     * so its true ceiling is below CSV's and it was not separately characterized.
     * Derate the CSV-based cap by half to stay conservative (never over-cap JSON)
     * until JSON is measured (#524 follow-up).  Intentionally applied AFTER the
     * WiFi low-n clamp above.  Rationale is BYTE-rate equivalence: the clamp
     * encodes the measured low-n WiFi byte ceiling (CSV 3ch zero-loss at
     * 3050 Hz).  JSON at the same Hz pushes 2-3x the bytes, so its Hz cap must
     * derive from the EFFECTIVE (clamped) CSV cap: clamp-then-halve gives
     * 3ch 1525 Hz ~= 3812 CSV-equivalent bytes/s — near the measured ceiling —
     * while halve-without-clamp gives 2000 Hz ~= 5000 CSV-equivalent, well
     * above it.  (An earlier comment argued this in Hz terms, which was
     * arithmetically wrong — Qodo #540 pass-2 catch.) */
    /* #529: only for interfaces with no measured JSON fit. USB/NQ1 now
     * supplies its own coefficients above and sets jsonFitted, so halving
     * there would re-apply a derate that the measurement already replaced. */
    if (json && !jsonFitted) hz /= 2u;
    return (hz == 0u) ? 1u : hz;
}

/**
 * Max safe streaming frequency for the CURRENTLY configured interface + format
 * + enabled channels.  Reads ActiveInterface / Encoding from the streaming
 * runtime config and the enabled-channel counts, then returns
 * min(Streaming_ComputeMaxFreq(...), WiFi term when ActiveInterface==WiFi).
 * This is the single "what rate can this config stream?" entry point for the
 * START cap, the channel-enable recompute, and the WiFi finder.
 *
 * @return Max safe frequency in Hz (STREAMING_ISR_MAX_HZ when no channels are
 *         enabled, matching Streaming_ComputeMaxFreq(0,0))
 */
uint32_t Streaming_ComputeMaxFreqForConfig(void);

/**
 * Same as Streaming_ComputeMaxFreqForConfig() but for an explicitly-supplied
 * interface instead of the live ActiveInterface — lets the capabilities query
 * advertise the cap for a client's detected interface without mutating the
 * shared runtime config (#524). Encoding + channel counts still come from config.
 */
uint32_t Streaming_ComputeMaxFreqForConfigIface(StreamingInterface iface);

/**
 * Count enabled public ADC channels from current board + runtime config.
 * Used by SCPI_StartStreaming, the ADC channel-enable path, and the
 * capability rollup so they all agree on what counts toward the cap.
 *
 * @param[out] out_type1Count     Enabled MC12bADC ChannelType=1 (Type 1) channels
 * @param[out] out_totalPublic    Total enabled IsPublic channels (both ADC types)
 * @param[out] out_hasAD7609      true if any enabled IsPublic channel is AD7609
 *
 * Any out_* pointer may be NULL.
 */
void Streaming_CountActiveChannels(uint16_t* out_type1Count,
                                   uint16_t* out_totalPublic,
                                   bool* out_hasAD7609);

/* --- #847: the streaming config-change claim ------------------------------
 *
 * Every cap-input setter is guarded by `IsEnabled || Running`, and #844 closed
 * that window from START's side (the arm re-validates the cap it was admitted
 * under). The SAME window exists inverted, on the SETTER's side, and no cap
 * re-validation can see it:
 *
 *   1. a setter on transport A reads the flags -- idle -- and proceeds;
 *   2. a SYST:STR:START on transport B preempts it (USB SCPI is priority 7,
 *      WiFi SCPI priority 2, so either can preempt the other), computes the
 *      cap, re-validates it and ARMS the session;
 *   3. the setter resumes and performs its store -- onto a RUNNING stream,
 *      which is exactly the state its guard exists to refuse.
 *
 * #844's re-validation happens at step 2, BEFORE the store, so it cannot see
 * it: the session is left running with a cap input the cap was never computed
 * for. The trigger window is a few instructions; the consequence window is
 * not -- step 2 contains START's SD readiness poll, which runs for seconds.
 *
 * PR #845 fixed the two setters it touched (SYST:STR:BENCH, SYST:STR:INT) by
 * testing and storing in ONE critical section. That idiom does not generalise
 * to the rest of the family, because their "store" is not a store:
 *
 *   - CONF:ADC:SAMC -> MC12b_SetAcquisitionSamc() spins up to ~20 ms waiting
 *     on ADCCON2bits.BGVRRDY (MC12bADC.c);
 *   - CONF:ADC:THREshold -> AdcThreshold_Configure() takes a FreeRTOS mutex
 *     with portMAX_DELAY (AdcThreshold.c) -- blocking with interrupts off;
 *   - CONF:ADC:CHANnel -> LOG_I plus ADC_WriteChannelStateAll() SFR writes;
 *   - CONF:ADC:USECal -> an NVM save.
 *
 * None of those may run with interrupts disabled, so for them the guard and
 * the store cannot share a critical section. This claim is the same exclusion
 * expressed as a flag instead: the setter takes it (atomically with the idle
 * test), does its work at task priority, and releases it. SCPI_StartStreaming
 * OBSERVES it in the arm-time critical section #844 added and refuses to arm
 * while it is held -- so a config change and an arm are mutually exclusive in
 * both directions, and a setter's store can no longer land on a live session.
 *
 * SCOPE. This is not a lock over the runtime config in general. It excludes a
 * guarded setter against an ARM; two STARTs racing each other is #850, and
 * serialising SCPI execution across transports outright is #694. It is also
 * advisory-only -- nothing blocks on it. The loser is refused with an error,
 * which is the behaviour these guards already had.
 *
 * The "ARM" it excludes is SCPI_StartStreaming's, and only that one. TWO OTHER
 * SITES publish IsEnabled without consulting the claim -- SYST:STR:THRoughput
 * and the WiFi rate finder, both in SCPIInterface.c -- so a setter racing
 * either of those is still exposed. Left that way deliberately: both are
 * self-contained BENCH commands that arm, measure and stop inside one SCPI
 * callback, neither appears in a production client, and wiring the claim into
 * the finder's per-step arm would need a refusal path through its search loop.
 * Named here rather than left implicit, because "nothing can arm while the
 * claim is held" is what the rest of this comment would otherwise imply.
 */
typedef enum {
    STREAM_CFG_CLAIM_OK = 0,      /* claim taken -- caller MUST release it */
    STREAM_CFG_CLAIM_STREAMING,   /* a session is armed or running */
    STREAM_CFG_CLAIM_BUSY         /* another config change is in flight */
} StreamingCfgClaim;

/**
 * Take the config-change claim if the stream is fully idle and no other config
 * change holds it. Tests IsEnabled/Running and takes the claim inside ONE
 * critical section, so the pair cannot be split by a preempting START.
 *
 * @return STREAM_CFG_CLAIM_OK when taken -- and ONLY then must the caller call
 *         Streaming_EndConfigChange() on every path out.
 */
StreamingCfgClaim Streaming_BeginConfigChange(void);

/** Release a claim taken by Streaming_BeginConfigChange(). */
void Streaming_EndConfigChange(void);

/**
 * True while a guarded config change is in flight. Read by the START arm to
 * refuse a session whose configuration is mid-change. 32-bit read, atomic on
 * PIC32MZ.
 */
bool Streaming_ConfigChangeInProgress(void);

/*! Initializes the streaming component
 * @param[in] pStreamingConfigInit Streaming configuration
 * @param[out] pStreamingRuntimeConfigInit Streaming configuration in runtime
 */
void Streaming_Init(tStreamingConfig* pStreamingConfigInit,           
                    StreamingRuntimeConfig* pStreamingRuntimeConfigInit);

/*! Updates the streaming timer 
 */
void Streaming_UpdateState( void );

/*!
 * Called to write streaming data to the underlying tasks
 * @param runtimeConfig The runtime configuration
 * @param boardData     The board data
 */
void Streaming_Tasks(   StreamingRuntimeConfig* pStreamConfig, tBoardData* boardData);

/**
 * Initializes and starts the timestamp timer
 */
void TimestampTimer_Init( void );

/* #759: release a streaming selection that points at the SD card once the card
 * is no longer available. Safe to call from any task; no-op unless the active
 * interface is SD or USB+SD. */
void Streaming_SdInterfaceReleased(void);

/* #824: the bytes to write at the head of a newly opened SD log file, so every
 * split file is self-describing. Returns 0 -- and leaves *ppHeader untouched --
 * when this file should not carry one: the session's FIRST file in any
 * encoding (it is opened before the header exists, and under CSV/JSON the
 * encoder's own inline header lands at its byte 0 instead).
 *
 * Called by the SD task from OPEN_FILE, before anything from the circular
 * buffer reaches the new handle. The bytes are built once per session by the
 * streaming task and are valid until streaming stops. */
size_t Streaming_GetSdFileHeader(const uint8_t** ppHeader);

/* #757: report SD bytes that were buffered for the next file but can never be
 * written, because the session was torn down while the rotation's open was in
 * flight. Counts into SdDroppedBytes so the loss is visible instead of silent. */
void Streaming_ReportSdDiscard(size_t bytes);

// #388 — Compile-time profiling counters for the PB streaming hot path.
// When enabled, instruments encoder + USB write paths with _CP0_GET_COUNT()
// cycle measurements.  Off by default in production: enable here for a
// characterization build, then `SYST:STR:STATS?` exposes the counters
// for the bench operator to read post-run.
//
// Cost when enabled: ~5 cycles per measurement point (TASK critical-section
// pair + 64-bit accumulate).  Six measurement points → ~30 cycles per sample
// at 16 kHz × 1 ch = 480 kHz of overhead = 0.5 % of a 200 MHz CPU.  Below
// our worst-case rate-cap headroom; safe to leave on for typical bench
// runs.  Strip by setting to 0 before shipping a production build.
//
// The gate macro + accumulator function declarations live in
// streaming_profile.h so consumers (UsbCdc.c) can pull in only that
// header instead of all of streaming.h's transitive includes.
#include "streaming_profile.h"

// Streaming loss/throughput statistics, accumulated per session.
// 32-bit fields are atomic on PIC32MZ; 64-bit fields require critical sections.
// Use Streaming_GetStats() for an atomic snapshot of all fields.
typedef struct {
    uint32_t queueDroppedSamples;   // Aggregate: poolExhaustedSamples + queueOverflowSamples
    // #499 split — two distinct mechanisms previously combined in queueDroppedSamples:
    //   poolExhaustedSamples: AInSampleList_AllocateFromPool() returned NULL
    //                         (no free slot — pool depth too shallow for rate)
    //   queueOverflowSamples: AInSampleList_PushBack() failed
    //                         (FreeRTOS queue full — streaming_Task not draining fast enough)
    // Sum equals queueDroppedSamples (kept for backward compat with existing parsers).
    uint32_t poolExhaustedSamples;
    uint32_t queueOverflowSamples;
    uint32_t usbDroppedBytes;       // USB circular buffer full (total — incl. startup transients)
    uint32_t wifiDroppedBytes;      // WiFi circular buffer full (total — incl. startup transients)
    uint32_t sdDroppedBytes;        // SD write timeout/partial (total — incl. startup transients)
    // #450 + follow-up: post-grace subsets of the above. Drops that
    // occur within the first `gLossGraceSec` of a session increment
    // only the Total above; drops after the grace expires also
    // increment these Steady counters. Steady == real-data-loss after
    // the pipeline stabilizes. Total − Steady == startup-window drops.
    uint32_t usbDroppedBytesSteady;
    uint32_t wifiDroppedBytesSteady;
    uint32_t sdDroppedBytesSteady;
    uint32_t encoderFailures;       // Encoder returned 0 with data available
    uint32_t encoderFailuresSteady;
    uint32_t encoderDroppedSamples; // AIn samples consumed by failed encode calls (#297)
    uint32_t encoderDroppedSamplesSteady;
    uint32_t dioDroppedSamples;     // DIO queue full — PushBack returned false (#296)
    uint32_t dioDroppedSamplesSteady;
    uint32_t queueDroppedSamplesSteady;  // post-grace subset of queueDroppedSamples
    uint32_t eosOverruns;      // EOS notifications coalesced (>1 per wake) (#295)
    /* #735: iterations that ran with another tick already pending, i.e. the
     * deferred task was CATCHING UP rather than keeping pace.
     *
     * Why this is worth counting. A packet's two halves come from different
     * places: the stamp is counter-derived (baseTS + tick x period, #722) and
     * fixed when the iteration runs, but the VALUE is read live from the
     * one-deep BOARDDATA_AIN_LATEST slot at that same instant. While the task
     * drains a backlog, the priority-1 ADC data-ready ISRs keep overwriting
     * that slot, so an iteration stamped N can emit tick N+1's conversion --
     * a value NEWER than its own stamp. #722 made the stamps a uniform
     * sequence; it did not change where the value comes from, so the
     * association can still vary whenever the task runs behind.
     *
     * MEASURED, and it does not: this reads 0 at the enforced cap for 1xT1,
     * 5xT1, 11xT2, 16ch CSV and 16ch PB (real ADC over USB, 2026-08-21). It
     * goes non-zero only well past cap under NOCAP, and even there stays under
     * 0.1% while QueueDroppedSamples runs to ~40,000 -- overload shows up as
     * pool exhaustion, not as notification backlog.
     *
     * Do NOT cite #717's 0.07%-at-1-kHz to 15%-at-5-kHz figures as this
     * counter's expected range, which an earlier revision of this comment did.
     * Those were #717's DUPLICATE-TIMESTAMP rates, a different failure mode
     * (a shared timestamp slot) that #722 fixed outright. Carrying them over
     * misattributes one bug's measurements to another and would send the next
     * person looking for a 15% signal that is not there.
     *
     * Nothing else detects it. ScanStaleDropped (#557/#563) fires when a scan
     * fails to COMPLETE; during catch-up the scan completes normally, so that
     * counter reads 0 while the association skews.
     *
     * NOT a loss counter -- the sample is streamed and its value is real, just
     * possibly attributed to the neighbouring tick. Excluded from the loss
     * total for the same reason as eosOverruns. It is the "inform on stale
     * data" half of the project's SCPI visibility principle: a client doing
     * absolute phase alignment, or correlating against #667 edge events, needs
     * to know the offset is not fixed. */
    uint32_t catchUpSamples;
    /* #814: samples in which at least one enabled channel sat at a rail (raw
     * code 0 or the module's full-scale code), and the OR of which channel
     * slots did so. A railed sample is INDISTINGUISHABLE from a real reading
     * once it leaves the device, which is the failure you cannot recover from
     * after a long log. These are counters for diagnostics; the live signal
     * rides the protobuf device_status word so a consumer can mark the span
     * as it plots. NOT loss -- a clipped sample is delivered, it is just not
     * trustworthy as a measurement, so it is never folded into any loss %. */
    /* 64-bit for the same reason totalSamplesStreamed is: a fully railed
     * channel makes clippedSamples EQUAL to it, so it inherits the same
     * range requirement and a uint32 would wrap inside a long log. */
    uint64_t clippedSamples;
    uint32_t clippedChannelMask;
    uint32_t scanStaleDropped; // #557: scan armed but EOS not fired by next trigger
                               // (scan-busy/stale) — counted as a dropped sample
    // #541 D-A diagnostic: ticks where a T1 (dedicated-module) channel's
    // ARDY flag was not set when the deferred task went to read its result
    // register.  Expected ~0 (T1 conversion completes ~1.3 us after trigger;
    // the task wakes several us later).  Non-zero values mean T1 samples
    // were emitted with their validMask bit clear for those ticks.
    uint32_t t1ArdyMisses;
    uint64_t totalSamplesStreamed;   // Samples successfully queued (64-bit for week-long sessions)
    uint64_t totalBytesStreamed;     // Total bytes encoded (64-bit for week-long sessions)
    uint32_t windowLossPercent;     // Windowed sample loss percentage (0-100)
    // Timer ISR tracking (#265). Distinguishes "timer firing at requested rate"
    // from downstream bottlenecks (sample pool, encoder, output transport).
    // The invariant `timerISRCalls == totalSamplesStreamed + queueDroppedSamples`
    // should always hold during a session — every timer event becomes either
    // a successfully queued sample or a pool-exhaustion drop.
    //
    // #707/#745: the value REPORTED here is therefore ticks that produced a
    // sample attempt, not raw ISR entries. At session start a tick can fire
    // before the shared MODULE7 scan has published anything, so no sample is
    // ready to emit; that is a dry call — not a generated sample and not a
    // loss — and it is excluded here so the invariant above stays exact
    // without inventing a third term. The exclusion is bounded to the
    // session's scan-priming window (at most a handful of ticks, and 0 for
    // configs with no enabled Type 2 user channel), so this remains a valid
    // basis for the #265 use below: a timerISRCalls far short of
    // `freq × duration` still means the timer itself is rate-limited.
    //
    // 64-bit so it never wraps in practice — at the ~90 kHz hardware ceiling
    // it would take ~6 million years to overflow. Matches the other 64-bit
    // session counters (totalSamplesStreamed, totalBytesStreamed).
    //
    // Storage note: this field is populated by Streaming_GetStats() from a
    // separate `static volatile uint64_t gTimerISRCalls` global. The volatile
    // global is the actual ISR-modified storage; the StreamingStats field is
    // a snapshot copy taken inside taskENTER_CRITICAL (which makes the
    // non-atomic 64-bit read coherent by blocking the timer ISR).
    uint64_t timerISRCalls;          // Sample-producing timer ticks this session
                                     // (ISR entries less scan-priming dry calls)
    // #367 diagnostics — populated at Streaming_Stop() to reconcile the
    // accounting gap (TotalBytesStreamed vs WifiTcpBytesSent at saturation).
    uint32_t circularBufferEndBytes; // Bytes still in WiFi circular buffer at Stop
#if PB_PROFILE_COUNTERS
    // #388 PB streaming bottleneck instrumentation.  All cycle fields are
    // raw `_CP0_GET_COUNT()` differences (SYSCLK/2 = 100 MHz on PIC32MZ).
    // To convert: cycles / 100_000_000 → seconds.
    uint64_t pbEncodeCycles;        // Accumulated time inside the encoder call
    uint32_t pbEncodeMaxCycles;     // Worst-case per-call time
    uint64_t pbEncodeBytesOut;      // Bytes the encoder produced (post-Nanopb)
    uint64_t usbWriteBufCycles;     // Accumulated time inside UsbCdc_WriteToBuffer
                                    // (the encoder → circular-buffer copy)
    uint64_t usbDmaCopyCycles;      // Accumulated time inside CircularBuf_ProcessBytes
                                    // (the circular → DMA buffer copy)
    uint64_t usbDmaPendingCycles;   // Accumulated time per DMA transfer between
                                    // USB_DEVICE_CDC_Write() success and the
                                    // WRITE_COMPLETE event — i.e. wire-time per
                                    // packet from the host stack's perspective
    uint32_t usbDmaIdleCount;       // State-machine iterations skipped because
                                    // the prior DMA transfer was still pending —
                                    // the "bus idle window" between completion
                                    // and next transfer start
#endif
} StreamingStats;

// Copies stats into *out inside a critical section (atomic snapshot)
void Streaming_GetStats(StreamingStats* out);
void Streaming_ClearStats(void);

// #388 profile counter accumulator hooks are declared in
// streaming_profile.h, included near the top of this file.

// Increment DIO dropped sample counter (called from DIO_StreamingTrigger).
// 32-bit increment on PIC32MZ — single writer (deferred ISR task, pri 8).
void Streaming_IncrDioDropped(void);

// Increment EOS coalesce counter (called from MC12bADC_EosInterruptTask).
// @param missed Number of coalesced notifications (notifCount - 1).
void Streaming_IncrEosOverruns(uint32_t missed);

// #557: set the scan-completed flag from the ADC EOS ISR. ISR-safe (one
// volatile write). The streaming timer ISR consumes it to detect a scan that
// didn't complete before the next trigger (scan-stale -> counted as a drop).
void Streaming_NoteEosFired(void);

/**
 * Returns current SCPI STATus:QUEStionable condition bits for streaming health.
 * Bit 4 = windowed sample loss >= threshold, Bit 8 = USB overflow,
 * Bit 9 = WiFi overflow, Bit 10 = SD overflow, Bit 11 = encoder failure,
 * Bit 12 = all-transports-down auto-stop (#397).
 * Definitions match QUES_* constants in SCPIInterface.c.
 * Called by SCPI_SyncQuesBits() in SCPIInterface.c before register queries.
 * Bits are cleared automatically when streaming stops.
 */
/**
 * @brief True while at least one enabled channel is AT A RAIL right now.
 *
 * #814: live state, republished for every DELIVERED sample -- not a sticky
 * latch and not a cumulative counter. Returns false when not streaming.
 *
 * Where this is observable matters and is easy to get wrong: streaming
 * protobuf frames do NOT carry device_status (the fast encoder emits only
 * timestamp, analog, digital and port-dir), so the bit rides the INFO and
 * DISCOVERY messages, not the stream. The live in-session surface is
 * STAT:QUES:COND? bit 0, read over a link that is not carrying the stream.
 *
 * A railed value is delivered normally; the bit says only that it cannot be
 * trusted as a measurement, because a genuine full-scale reading and a
 * clamped one are the same number on the wire.
 */
bool Streaming_IsClipping(void);

uint32_t Streaming_GetQuesBits(void);

// #589: QUES condition bit 13 — shared SPI4 bus jammed (suspect SD card).
// Owned by SpiBusHealth/wifi_manager (not the streaming session); survives
// per-session QUES clears; cleared when the SD subsystem is re-enabled.
#define STREAMING_QUES_SPI_BUS_FAULT (1UL << 13)
void Streaming_QuesExternalSet(uint32_t mask);
void Streaming_QuesExternalClear(uint32_t mask);

/**
 * #397 self-heal grace window — seconds an active transport may be
 * unhealthy before the streaming task counts it as "dead".  Default 60.
 * Range [5, 300].  Runtime-only (not NVM-persisted, reset on reboot).
 * Used by Streaming_AllConfiguredTransportsDead() inside streaming_Task.
 */
uint32_t Streaming_GetTransportGraceSec(void);
bool     Streaming_SetTransportGraceSec(uint32_t sec);

// #450 — startup-drop grace window in seconds (0..60, default 3).
// Drops before this window are counted only in *DroppedBytes totals;
// drops after also bump the *DroppedBytesSteady fields.  Setter takes
// effect immediately (matches LOSS:THREshold pattern) — the grace
// check reads gLossGraceSec live at every drop site.
uint32_t Streaming_GetLossGraceSec(void);
bool     Streaming_SetLossGraceSec(uint32_t sec);

/**
 * #486 — true when neither streaming task is currently inside the
 * region that dereferences pool / buffer / queue memory.  Polled by
 * SCPI_StartStreaming after Streaming_Stop and before the re-partition
 * of the unified pool, so the destructive operations (pool repartition,
 * encoder buffer swap, sample queue re-init, coherent pool reset) don't
 * race with an in-flight encode or sample-pool allocation.
 */
bool Streaming_TasksAreQuiescent(void);

/**
 * Compute optimal circular buffer sizes based on currently active interfaces.
 * Used by auto-balance at stream start and by SYST:MEM:AUTO SCPI command.
 *
 * @param[out] outUsbSize     Optimal USB circular buffer size (bytes)
 * @param[out] outWifiSize    Optimal WiFi circular buffer size (bytes)
 * @param[out] outSdSize      Optimal SD circular buffer size (bytes)
 * @param[out] outSdDmaSize    Optimal SD DMA write buffer size (bytes, coherent pool)
 * @param[out] outUsbDmaSize   Optimal USB DMA write buffer size (bytes, coherent pool)
 * @param[out] outWifiDmaSize  Optimal WiFi SPI staging buffer size (bytes, coherent pool)
 * @param[out] outEncoderSize  Optimal encoder buffer size (bytes)
 */
void Streaming_ComputeAutoBuffers(uint32_t* outUsbSize, uint32_t* outWifiSize,
                                   uint32_t* outSdSize, uint32_t* outSdDmaSize,
                                   uint32_t* outUsbDmaSize, uint32_t* outWifiDmaSize,
                                   uint32_t* outEncoderSize);

/**
 * Set the encoder buffer to pool-managed memory.
 * Must be called before streaming starts.
 */
void Streaming_SetEncoderBuffer(uint8_t* buf, uint32_t size);

// Flow window configuration (configurable via SCPI SYST:STR:LOSS commands).
// Loss threshold: percentage (1-100) that triggers QUES data loss bit (default 5).
uint32_t Streaming_GetLossThreshold(void);
void Streaming_SetLossThreshold(uint32_t pct);

// Flow window size override: 0 = auto (clamp(freq*2, 20, 10000)), >0 = explicit.
// Takes effect at next streaming start.
uint32_t Streaming_GetFlowWindowOverride(void);
void Streaming_SetFlowWindowOverride(uint32_t size);

// #730 streaming timebase, for clients that need exact timing.
//
// Streaming_TimestampTicksPerSample: the timestamp-domain length of one
//   streaming period for a given stream-timer period register value. This is
//   the SAME value Streaming_Start stamps with (#717 gStreamPeriodTicks) —
//   shared so a reported value can't drift from the emitted stamps. Always >= 1.
// Streaming_ActualRateMilliHz: the rate the hardware actually runs, in
//   millihertz. StreamingRuntimeConfig.Frequency stores the REQUESTED rate;
//   the period register quantizes it (4500 Hz -> 4498.714 Hz at 252 MHz).
//   Returns 0 when no period is configured.
//
// Both take ClockPeriod (the PR value, i.e. periodCycles-1) so a caller can ask
// about a hypothetical rate without mutating the runtime config.
// True once a streaming rate has been configured this boot. Gates the two
// per-config values above (both report 0 when unconfigured). Reads a 32-bit
// flag — atomic on PIC32MZ, no critical section needed.
bool Streaming_IsRateConfigured(void);
// Called by the SCPI paths that set ClockPeriod (stream START, per-channel
// enable) to mark the rate as genuinely configured. The boot defaults are
// placeholders that describe no real rate, so the flag is what makes the
// "0 = unconfigured" contract on the timebase values true.
void Streaming_NoteRateConfigured(void);
// Restore a previously captured flag value. For the benchmark / WiFi-finder
// paths, which stream at a temporary period and then restore the previous one:
// the flag belongs in the same save/restore block as Frequency and ClockPeriod.
void Streaming_RestoreRateConfigured(bool configured);
uint32_t Streaming_TimestampTicksPerSample(uint32_t clockPeriod);
uint32_t Streaming_ActualRateMilliHz(uint32_t clockPeriod);

// Test pattern streaming mode.
// 0=off (real ADC data), 1=counter, 2=midscale, 3=fullscale, 4=walking,
// 5=triangle, 6=sine. Runtime-only (not persisted to NVM).
// Counter resets each streaming session.
void Streaming_SetTestPattern(uint32_t pattern);
uint32_t Streaming_GetTestPattern(void);

// Benchmark mode: when enabled, the deferred ISR task generates test pattern
// samples as fast as possible (no timer wait), bypassing ADC timing.
// Benchmark modes isolate pipeline stages for bottleneck analysis:
//   0 = Normal (freq cap + real ADC)
//   1 = NoCap (bypass cap, real ADC timing)
//   2 = Pipeline (bypass cap + skip ADC, test pattern required)
void Streaming_SetBenchmarkMode(uint32_t mode);
uint32_t Streaming_GetBenchmarkMode(void);

/**
 * True when streaming is active AND the active interface does not use
 * WiFi. Used by the WINC idle-gate (#331) to decide when it is safe to
 * pace the WINC driver's task loop down. Safe to call from any context;
 * reads plain bools with no cross-task coordination needed.
 */
bool Streaming_IsActiveOnNonWifiInterface(void);

/**
 * True when streaming is active AND the active interface is WiFi. The
 * complement query to Streaming_IsActiveOnNonWifiInterface(); used by the
 * #29 dynamic WiFi power-save policy to force full WINC power while the
 * WiFi data path is in use. Safe to call from any context; reads plain
 * fields with no cross-task coordination needed.
 */
bool Streaming_IsActiveOnWifiInterface(void);

/**
 * Build channel mapping from current board config and runtime config.
 * Must be called before streaming starts (from SCPI_StartStreaming).
 * Stores mapping globally for ISR and encoder access.
 *
 * @param pBoardConfig     Board hardware configuration
 * @param pRuntimeChannels Runtime channel enable/disable state
 * @return Number of enabled public channels (mapping.count)
 */
uint8_t Streaming_BuildChannelMapping(const tBoardConfig* pBoardConfig,
                                       const AInRuntimeArray* pRuntimeChannels);

/**
 * Get the current channel mapping (built at stream start).
 * Valid only while streaming is active or after BuildChannelMapping.
 */
const AInChannelMapping* Streaming_GetChannelMapping(void);

#ifdef	__cplusplus
}
#endif

