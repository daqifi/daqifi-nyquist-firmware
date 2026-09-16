/* ==========================================================================
 * test_1112_scan_list_snapshot_race.c -- PR firmware#1112 round 3 (issue #267)
 *
 * WHAT IS UNDER TEST
 *
 * MC12b_ComputeScanList() (HAL/ADC/MC12bADC.c) builds the ADCCSS1/ADCCSS2 scan
 * mask -- "which analog inputs a streaming session would arm right now" -- by
 * walking the channel table and consulting each entry's IsEnabled flag. Its
 * css1/css2 result is what CONF:CAP:JSON? positions every channel against:
 * MC12b_ChannelScanOffsetTicks() counts how many armed inputs convert BEFORE a
 * given one and reports that as scan_offset_ticks.
 *
 * Until #1112 round 3 the walk read pRt->Data[i].IsEnabled INLINE, one entry
 * at a time, holding nothing -- up to 48 independently-timed live reads rather
 * than one observation of the channel set. The other half of the hazard is
 * that its writer is non-atomic in exactly the matching way:
 * ADCChanEnableSetClaimed()'s one-argument mask form (SCPIADC.c) assigns
 * IsEnabled per channel in ascending index order, up to 16 separate stores for
 * one CONF:ADC:CHANnel <mask> command.
 *
 * The defect (Qodo /agentic_review, PR firmware#1112, round 2, reported
 * independently by two hunters and confirmed by the arbiter; severity
 * corrected down to LOW because it corrupts one field of one read-only
 * diagnostic response and self-corrects on the next query): the mask can
 * describe a channel combination the operator never commanded.
 *
 *   Scenario 1 -- idle NQ1, OBDiag=0, SAMC=100, only channel 0 enabled. A WiFi
 *   CONF:CAP:JSON? enters the walk and reads channel 0 as enabled. The USB task
 *   then runs `CONF:ADC:CHAN 2` to completion, which CLEARS channel 0 and then
 *   SETS channel 1 -- so the true sequence of enabled-sets is {0}, {}, {1},
 *   never {0,1}. The resumed walk reads channel 1 as enabled too and reports
 *   BOTH. Channel 1's scan_offset_ticks comes out one whole (SAMC+16)*TAD7
 *   scan-position step wrong.
 *
 *   Scenario 2 -- `CONF:ADC:CHAN 3` ({0,1}) in force, and `CONF:ADC:CHAN 514`
 *   ({1,9}) lands mid-walk. Channel 0 is read before it, channels 1 and 9
 *   after, so the walk reports {0,1,9} -- three inputs, where neither the
 *   before-set ({0,1}) nor the after-set ({1,9}) has three.
 *
 * The fix is TWO matching critical sections, and this file exists mostly to
 * show why one would not have been enough:
 *
 *   READER (MC12b_ComputeScanList): phase 1 copies every IsEnabled into a
 *   local bool[] under taskENTER_CRITICAL()/taskEXIT_CRITICAL(); phase 2 builds
 *   the mask from that local only. A bounded <=48-field-read section, so the
 *   read-only query still never BLOCKS and is never REFUSED -- the PR
 *   deliberately does not give it Streaming_BeginConfigChange().
 *
 *   WRITER (ADCChanEnableSetClaimed, mask form): the variant switch resolves
 *   which runtime entries the mask writes and to what, OUTSIDE the section;
 *   one section then applies <=16 byte stores together.
 *
 * That pairing is this codebase's established shape for composite shared
 * state: #1048/#1054 for the MC12b CalM/CalB pair, and #1086 for the AD7609
 * Range, whose read-side comment states the rule outright -- "That section
 * closes only the direction where THIS READER is preempted mid-read; the
 * direction where the WRITER is preempted mid-store is closed by the matching
 * section in ADCChanRangeSetClaimed above, and neither half is sufficient
 * alone."
 *
 * NOT under test: staleness. Both post-fix shapes are still snapshots -- the
 * set can change the instant after the section is released, and the response is
 * then merely out of date. What is under test is that the mask always
 * describes a set the operator actually commanded, never a hybrid of two.
 * Also not under test: the narrower residual against cap_terms.scan_bound_hz,
 * which a different function computes earlier in the same query (documented in
 * MC12b_ChannelScanOffsetTicks' own comment and docs/ADC_HW_SEMANTICS.md).
 *
 * HOW IT IS TESTED
 *
 * Neither MC12bADC.c nor SCPIADC.c is host-compilable -- FreeRTOS, Harmony's
 * configuration.h/definitions.h, libscpi and the whole board/driver graph. So
 * this follows test_985_suspend_reason_consistency.c,
 * test_953_bench_suspend_diagnosis.c and test_943_bench_stall_bound.c: the
 * READ SHAPES and the WRITE SHAPES are re-implemented here over an injected,
 * controllable environment, and their masks compared. The Makefile guard for
 * $(SCAN1112_BIN) pins that the real sources still have the shapes modelled.
 *
 * THE ENVIRONMENT IS THE PART THAT MATTERS. Each channel carries a (before,
 * after) pair -- its enable flag under the commanded configuration before the
 * concurrent setter and under the one after it. What varies is WHEN, relative
 * to the writer's progress, each of several DIFFERENT channels' single read
 * happens. So the environment is a WRITER PROGRESS SCHEDULE: writerProgress[k]
 * is how many of the writer's ascending per-channel stores had landed at the
 * instant of the reader's k-th flag read. It is non-decreasing -- a store
 * never un-lands -- and the two shapes differ in which entries they consult:
 *
 *   READER_INLINE   -- the pre-fix walk. The k-th read is of channel k and
 *                      sees writerProgress[k]: every read is separately timed,
 *                      so the writer may advance between any two of them.
 *   READER_SNAPSHOT -- the post-fix phase 1. EVERY read sees
 *                      writerProgress[0]: no writer statement can execute
 *                      while the section is held, so the whole capture happens
 *                      at one instant. Phase 2 reads no flag at all.
 *
 * and the writer shape constrains which schedules are possible at all:
 *
 *   WRITER_TORN   -- the pre-fix mask loop. Any non-decreasing schedule over
 *                    [0, count]: a partial application is observable.
 *   WRITER_ATOMIC -- the post-fix staged apply. Only 0 and count ever appear:
 *                    the stores land together, so no partial application
 *                    exists to be observed.
 *
 * The four combinations are then the whole theorem, and the property sweep
 * asserts each one EXHAUSTIVELY over every schedule its writer shape admits:
 *
 *   INLINE   x TORN    -- the shipped defect.          non-commanded REACHABLE
 *   SNAPSHOT x TORN    -- reader-only fix.             non-commanded REACHABLE
 *   INLINE   x ATOMIC  -- writer-only fix.             non-commanded REACHABLE
 *   SNAPSHOT x ATOMIC  -- #1112 round 3, both halves.  non-commanded IMPOSSIBLE
 *
 * The two middle rows are the point: they are what "neither half is sufficient
 * alone" means for THIS defect, and a reader-only fix would leave this file
 * failing rather than quietly narrowing the window.
 *
 * CASES
 *
 *   1  audit scenario 1, {0} -> {1}         INLINE reports {0,1}; SNAPSHOT
 *                                           reports {0} or {1}, never both
 *   2  audit scenario 2, {0,1} -> {1,9}     INLINE reports {0,1,9} (3 inputs);
 *                                           SNAPSHOT reports {0,1} or {1,9}
 *   3  every snapshot instant, both         SNAPSHOT x ATOMIC is always one
 *      scenarios                            of the two commanded sets
 *   4  stable configuration, 8 enabled-sets INLINE and SNAPSHOT agree exactly,
 *                                           writer before / writer after / no
 *                                           writer at all
 *   5  exhaustive 2x2 property sweep        counts above, and the exact
 *                                           predicate for WHICH schedules go
 *                                           wrong
 *   6  read accounting                      both shapes read each channel
 *                                           exactly once
 *
 * Case 6 is why this file does not use test_985's per-flag read-count
 * timeline: here the pre-fix and post-fix shapes make the SAME NUMBER of
 * reads. A read-count model would find them identical and miss the defect
 * entirely. What differs is the timing of reads relative to a writer, which is
 * what the schedule expresses.
 *
 * FIDELITY -- what these models are and are not
 *
 * 1. The AN numbers are the REAL NQ1 ones, so the masks here are the masks the
 *    firmware would compute: channel 0 = AN11, channel 1 = AN24, channel 9 =
 *    AN5 (ADCHS_CH11/CH24/CH5, NQ1BoardConfig.c), which is where the audit's
 *    "AN5+AN11+AN24" three-input figure comes from. Nothing in the logic
 *    depends on the specific values -- only on their being distinct, so each
 *    enabled-set maps to its own mask -- and no guard pins them: a board remap
 *    would change the illustration, not the property.
 * 2. Only the ENABLE dimension is modelled. The real walk also skips by Type,
 *    ChannelType, the AN44 erratum and the >=64 bound, all read from the
 *    IMMUTABLE board config, which no runtime writer touches. Those filters
 *    cannot race and are simply absent here -- every channel modelled is one
 *    that survived them.
 * 3. The writer's loop order (ascending user-channel index) and the reader's
 *    (ascending board-config array index) coincide for NQ1 user channels,
 *    which is what lets one progress counter mean "the first p channels hold
 *    their new value" to both. A board whose table reordered user channels
 *    would need the two orders modelled separately; none does.
 * 4. ONE writer. The firmware has exactly one multi-field writer of this
 *    array -- the mask form of CONF:ADC:CHANnel (enumerated: every other site
 *    that assigns AInRuntimeConfig::IsEnabled is that same function's
 *    single-channel branch, which writes exactly one aligned bool per command
 *    and is therefore already atomic on PIC32MZ, and the boot-time static
 *    initialisers in NQ1/NQ2/NQ3RuntimeDefaults.c). So one progress counter is
 *    the whole writer state. Two concurrent mask setters would need two, but
 *    they cannot occur: the setter holds Streaming_BeginConfigChange() and
 *    SCPI commands from the two transports serialise on it.
 * 5. The schedule is per-READ, not per-microsecond. "The writer landed between
 *    these two reads" is modelled as the progress value differing between two
 *    entries, which is precisely how that race presents to the reader and all
 *    it can observe. No attempt is made to model how long anything took.
 * 6. The post-fix WRITER is modelled as progress in {0, count} rather than by
 *    compiling the real staged apply. That is the claim the Makefile guard
 *    carries instead: that the real loop's stores sit inside one
 *    taskENTER_CRITICAL()/taskEXIT_CRITICAL() pair with the table lookups
 *    outside it.
 * ========================================================================== */

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "test_framework.h"

/* Four channels is enough for both audit scenarios and keeps the exhaustive
 * schedule enumeration in case 5 small (C(2n,n) schedules for n channels). */
#define SCAN_MAX_CHANNELS 4

/* Real NQ1 AN numbers -- see FIDELITY 1. */
#define AN_USER_CH0   11u
#define AN_USER_CH1   24u
#define AN_USER_CH9    5u
#define AN_USER_CH2   25u

typedef struct {
    unsigned an;        /* CSS bit == AN number */
    bool     before;    /* enable flag under the configuration in force before */
    bool     after;     /* ... and under the one the concurrent setter installs */
} ChannelModel;

/* Which entries of the schedule a reader consults. */
typedef enum {
    READER_INLINE = 0,      /* pre-fix: read k sees writerProgress[k]  */
    READER_SNAPSHOT         /* post-fix: every read sees writerProgress[0] */
} ReaderShape;

/* Which schedules a writer can produce. */
typedef enum {
    WRITER_TORN = 0,        /* pre-fix: any non-decreasing progress   */
    WRITER_ATOMIC           /* post-fix: progress is only 0 or count  */
} WriterShape;

typedef struct {
    ChannelModel ch[SCAN_MAX_CHANNELS];
    unsigned     count;
    /* writerProgress[k] -- how many of the writer's ascending per-channel
     * stores had landed at the instant of the reader's k-th flag read. */
    unsigned     writerProgress[SCAN_MAX_CHANNELS];
    unsigned     reads;                          /* total flag reads made */
    unsigned     readsPerChannel[SCAN_MAX_CHANNELS];
} ScanEnv;

/* ==========================================================================
 * Environment
 * ========================================================================== */

static uint32_t css_bit(unsigned an)
{
    return (uint32_t)1u << an;
}

/* The one place a flag is ever read. `slot` is the schedule entry this read is
 * timed against; `i` is the channel. A channel holds its new value once the
 * writer's store for it has landed, i.e. once progress has passed its index. */
static bool env_read(ScanEnv *e, unsigned slot, unsigned i)
{
    unsigned progress = e->writerProgress[slot];
    e->reads++;
    e->readsPerChannel[i]++;
    return (i < progress) ? e->ch[i].after : e->ch[i].before;
}

static void env_reset_reads(ScanEnv *e)
{
    unsigned i;
    e->reads = 0;
    for (i = 0; i < SCAN_MAX_CHANNELS; i++) {
        e->readsPerChannel[i] = 0;
    }
}

/* The two commanded configurations -- the only two masks a correct reader may
 * ever return, whatever the interleaving. */
static uint32_t mask_before(const ScanEnv *e)
{
    uint32_t css = 0;
    unsigned i;
    for (i = 0; i < e->count; i++) {
        if (e->ch[i].before) {
            css |= css_bit(e->ch[i].an);
        }
    }
    return css;
}

static uint32_t mask_after(const ScanEnv *e)
{
    uint32_t css = 0;
    unsigned i;
    for (i = 0; i < e->count; i++) {
        if (e->ch[i].after) {
            css |= css_bit(e->ch[i].an);
        }
    }
    return css;
}

static bool mask_is_commanded(const ScanEnv *e, uint32_t css)
{
    return (css == mask_before(e)) || (css == mask_after(e));
}

/* ==========================================================================
 * PRE-#1112-round-3 READER: read and decide interleaved, one channel at a
 * time, nothing held. Each read is separately timed, so it sees its own
 * schedule entry.
 * ========================================================================== */
static uint32_t reader_inline(ScanEnv *e)
{
    uint32_t css = 0;
    unsigned i;
    for (i = 0; i < e->count; i++) {
        if (env_read(e, i, i)) {
            css |= css_bit(e->ch[i].an);
        }
    }
    return css;
}

/* ==========================================================================
 * POST-#1112-round-3 READER: phase 1 captures every flag at ONE instant --
 * modelled as every read consulting schedule entry 0, because no writer
 * statement can execute while the critical section is held. Phase 2 is a pure
 * function of that capture and reads no flag at all.
 * ========================================================================== */
static uint32_t reader_snapshot(ScanEnv *e)
{
    bool snapshot[SCAN_MAX_CHANNELS];
    uint32_t css = 0;
    unsigned i;

    /* Phase 1 -- the critical section. */
    for (i = 0; i < e->count; i++) {
        snapshot[i] = env_read(e, 0, i);
    }
    /* Phase 2 -- no further reads. */
    for (i = 0; i < e->count; i++) {
        if (snapshot[i]) {
            css |= css_bit(e->ch[i].an);
        }
    }
    return css;
}

static uint32_t run_reader(ScanEnv *e, ReaderShape shape)
{
    env_reset_reads(e);
    return (shape == READER_SNAPSHOT) ? reader_snapshot(e) : reader_inline(e);
}

/* ==========================================================================
 * Schedules
 * ========================================================================== */

/* Fill a whole schedule with one progress value -- "the writer's state did not
 * change at any point during this read", which is every stable case and also
 * the only kind of schedule the snapshot reader can distinguish. */
static void sched_flat(ScanEnv *e, unsigned progress)
{
    unsigned k;
    for (k = 0; k < SCAN_MAX_CHANNELS; k++) {
        e->writerProgress[k] = progress;
    }
}

/* An ATOMIC writer that lands entirely between read `splitAt-1` and read
 * `splitAt`: reads before it see nothing applied, reads from it on see
 * everything applied. splitAt == 0 is "landed before the read began",
 * splitAt == count is "had not started when the read ended". */
static void sched_atomic(ScanEnv *e, unsigned splitAt)
{
    unsigned k;
    for (k = 0; k < SCAN_MAX_CHANNELS; k++) {
        e->writerProgress[k] = (k < splitAt) ? 0u : e->count;
    }
}

/* How many channels the concurrent setter actually changes -- the ones whose
 * before and after differ. A hybrid mask needs at least TWO of them, which is
 * exactly why the single-channel setter branch (one aligned bool store, already
 * atomic on PIC32MZ) needs no protection and the mask branch does. */
static unsigned changed_count(const ScanEnv *e)
{
    unsigned i, n = 0;
    for (i = 0; i < e->count; i++) {
        if (e->ch[i].before != e->ch[i].after) {
            n++;
        }
    }
    return n;
}

/* ==========================================================================
 * Scenario fixtures -- both taken verbatim from the audit.
 * ========================================================================== */

/* Scenario 1: `CONF:ADC:CHAN 1` ({0}) in force, `CONF:ADC:CHAN 2` ({1}) lands.
 * Channel 0 is cleared and channel 1 set, in that order. */
static void scenario_chan1_to_chan2(ScanEnv *e)
{
    memset(e, 0, sizeof(*e));
    e->count = 2;
    e->ch[0].an = AN_USER_CH0;  e->ch[0].before = true;  e->ch[0].after = false;
    e->ch[1].an = AN_USER_CH1;  e->ch[1].before = false; e->ch[1].after = true;
}

/* Scenario 2: `CONF:ADC:CHAN 3` ({0,1}) in force, `CONF:ADC:CHAN 514` ({1,9})
 * lands. Channel 0 is cleared, channel 1 rewritten with the value it already
 * had, channel 9 set. */
static void scenario_chan3_to_chan514(ScanEnv *e)
{
    memset(e, 0, sizeof(*e));
    e->count = 3;
    e->ch[0].an = AN_USER_CH0;  e->ch[0].before = true;  e->ch[0].after = false;
    e->ch[1].an = AN_USER_CH1;  e->ch[1].before = true;  e->ch[1].after = true;
    e->ch[2].an = AN_USER_CH9;  e->ch[2].before = false; e->ch[2].after = true;
}

/* A four-channel fixture for the exhaustive sweep: two channels change and two
 * do not, so both the hybrid and the stable dimensions are exercised. */
static void scenario_four_channel(ScanEnv *e)
{
    memset(e, 0, sizeof(*e));
    e->count = 4;
    e->ch[0].an = AN_USER_CH0;  e->ch[0].before = true;  e->ch[0].after = false;
    e->ch[1].an = AN_USER_CH1;  e->ch[1].before = true;  e->ch[1].after = true;
    e->ch[2].an = AN_USER_CH2;  e->ch[2].before = false; e->ch[2].after = false;
    e->ch[3].an = AN_USER_CH9;  e->ch[3].before = false; e->ch[3].after = true;
}

static unsigned popcount32(uint32_t v)
{
    unsigned n = 0;
    while (v != 0u) {
        v &= (v - 1u);
        n++;
    }
    return n;
}

/* ==========================================================================
 * CASE 1 -- the audit's first scenario, reproduced exactly.
 *
 * The pre-fix reader reads channel 0 before the setter runs and channel 1
 * after it, so it reports {0,1} = AN11|AN24 -- a two-input scan that neither
 * the before-set ({0} = AN11) nor the after-set ({1} = AN24) ever was. The
 * post-fix reader, at that same instant, reports one or the other.
 * ========================================================================== */
TEST(inline_read_reports_a_pair_that_never_existed)
{
    ScanEnv env;
    uint32_t css;

    scenario_chan1_to_chan2(&env);
    /* The setter lands, in full, between the reader's first and second read.
     * count == 2, so "progress 2" is the whole update applied. */
    env.writerProgress[0] = 0;
    env.writerProgress[1] = 2;

    css = run_reader(&env, READER_INLINE);
    ASSERT_EQ(css, css_bit(AN_USER_CH0) | css_bit(AN_USER_CH1));
    ASSERT_EQ(popcount32(css), 2);
    ASSERT_FALSE(mask_is_commanded(&env, css));

    /* Pin what the two commanded sets actually are, so the assertion above
     * cannot pass by both of them accidentally matching. */
    ASSERT_EQ(mask_before(&env), css_bit(AN_USER_CH0));
    ASSERT_EQ(mask_after(&env),  css_bit(AN_USER_CH1));

    /* Same instant, post-fix reader: the capture is one observation, so it
     * lands wholly on one side. Here entry 0 says nothing had landed yet. */
    css = run_reader(&env, READER_SNAPSHOT);
    ASSERT_EQ(css, css_bit(AN_USER_CH0));
    ASSERT_TRUE(mask_is_commanded(&env, css));

    /* ... and if the capture happens after the setter completed instead. */
    sched_flat(&env, 2);
    css = run_reader(&env, READER_SNAPSHOT);
    ASSERT_EQ(css, css_bit(AN_USER_CH1));
    ASSERT_TRUE(mask_is_commanded(&env, css));

    /* Never the empty set either: {} is an intermediate of the WRITER's own
     * loop, and the atomic apply makes it unobservable. */
    ASSERT_TRUE(css != 0u);

    /* ---- and now the two HALF fixes, on this same scenario. ----
     *
     * WRITER-ONLY. The setter's stores now land together, but the pre-fix
     * reader still straddles them: it reads channel 0 before the apply and
     * channel 1 after it, and reports {0,1} again. Protecting the writer alone
     * does not help a reader that is preempted mid-walk. */
    sched_atomic(&env, 1u);
    css = run_reader(&env, READER_INLINE);
    ASSERT_EQ(css, css_bit(AN_USER_CH0) | css_bit(AN_USER_CH1));
    ASSERT_FALSE(mask_is_commanded(&env, css));

    /* READER-ONLY. The capture is now one observation -- but the torn writer
     * is mid-apply at that instant: its store clearing channel 0 has landed and
     * its store setting channel 1 has not. The mask is {}, the audit's own
     * middle intermediate, which the operator commanded no more than {0,1}.
     * This is why the fix is not reader-only: the section moves the window from
     * "during the reader's 48 reads" to "during the writer's 16 stores", which
     * is narrower and still open. */
    sched_flat(&env, 1u);
    css = run_reader(&env, READER_SNAPSHOT);
    ASSERT_EQ(css, 0u);
    ASSERT_FALSE(mask_is_commanded(&env, css));

    /* BOTH halves: progress 1 is not a schedule WRITER_ATOMIC can produce at
     * all, so the instant above does not exist. Case 5 proves that
     * exhaustively rather than by this one example. */
}

/* ==========================================================================
 * CASE 2 -- the audit's second scenario. Three inputs reported where neither
 * commanded set has three.
 * ========================================================================== */
TEST(inline_read_reports_a_three_input_scan_from_two_two_input_sets)
{
    ScanEnv env;
    uint32_t css;

    scenario_chan3_to_chan514(&env);
    ASSERT_EQ(mask_before(&env), css_bit(AN_USER_CH0) | css_bit(AN_USER_CH1));
    ASSERT_EQ(mask_after(&env),  css_bit(AN_USER_CH1) | css_bit(AN_USER_CH9));
    ASSERT_EQ(popcount32(mask_before(&env)), 2);
    ASSERT_EQ(popcount32(mask_after(&env)),  2);

    /* Channel 0 read before the setter, channels 1 and 9 after it. */
    env.writerProgress[0] = 0;
    env.writerProgress[1] = 3;
    env.writerProgress[2] = 3;

    css = run_reader(&env, READER_INLINE);
    ASSERT_EQ(css, css_bit(AN_USER_CH0) | css_bit(AN_USER_CH1)
                   | css_bit(AN_USER_CH9));
    ASSERT_EQ(popcount32(css), 3);
    ASSERT_FALSE(mask_is_commanded(&env, css));

    /* Post-fix reader at the same instant. */
    css = run_reader(&env, READER_SNAPSHOT);
    ASSERT_EQ(css, mask_before(&env));
    ASSERT_TRUE(mask_is_commanded(&env, css));
}

/* ==========================================================================
 * CASE 3 -- every instant at which the post-fix capture could happen, against
 * the post-fix writer, for both audit scenarios. Unconditional, not
 * merely-less-likely: there is no instant that yields anything but one of the
 * two commanded sets.
 * ========================================================================== */
TEST(snapshot_capture_lands_on_a_commanded_set_at_every_instant)
{
    ScanEnv env;
    unsigned progress;

    scenario_chan1_to_chan2(&env);
    for (progress = 0; progress <= env.count; progress++) {
        uint32_t css;
        /* WRITER_ATOMIC admits only 0 and count; both are swept here, and the
         * intermediate values are swept by the TORN arm of case 5. */
        if (progress != 0u && progress != env.count) {
            continue;
        }
        sched_flat(&env, progress);
        css = run_reader(&env, READER_SNAPSHOT);
        ASSERT_TRUE(mask_is_commanded(&env, css));
        ASSERT_EQ(css, (progress == 0u) ? mask_before(&env) : mask_after(&env));
    }

    scenario_chan3_to_chan514(&env);
    for (progress = 0; progress <= env.count; progress++) {
        uint32_t css;
        if (progress != 0u && progress != env.count) {
            continue;
        }
        sched_flat(&env, progress);
        css = run_reader(&env, READER_SNAPSHOT);
        ASSERT_TRUE(mask_is_commanded(&env, css));
        ASSERT_EQ(css, (progress == 0u) ? mask_before(&env) : mask_after(&env));
    }
}

/* ==========================================================================
 * CASE 4 -- REGRESSION. With no concurrent setter at all, the two reader
 * shapes must produce identical masks, for every enabled-set. This is what
 * stops a mutation that makes the post-fix reader return some fixed or empty
 * mask: such a mutant passes every race assertion above and fails here.
 *
 * Run three ways: no writer, the writer fully landed before the read, and the
 * writer not started until after it -- all three are stable from the reader's
 * point of view and must all agree.
 * ========================================================================== */
TEST(a_stable_configuration_makes_both_shapes_agree)
{
    unsigned set;

    for (set = 0; set < 16u; set++) {
        ScanEnv envInline, envSnapshot;
        unsigned phase;

        for (phase = 0; phase < 3u; phase++) {
            uint32_t cssInline, cssSnapshot, expected;
            unsigned i;

            memset(&envInline, 0, sizeof(envInline));
            envInline.count = 4;
            envInline.ch[0].an = AN_USER_CH0;
            envInline.ch[1].an = AN_USER_CH1;
            envInline.ch[2].an = AN_USER_CH2;
            envInline.ch[3].an = AN_USER_CH9;
            for (i = 0; i < 4u; i++) {
                bool on = ((set >> i) & 1u) != 0u;
                /* Stable: before == after, so the writer's progress cannot
                 * change what any read returns. */
                envInline.ch[i].before = on;
                envInline.ch[i].after  = on;
            }
            /* phase 0: writer never ran. 1: landed before. 2: still pending. */
            sched_flat(&envInline, (phase == 1u) ? envInline.count : 0u);
            envSnapshot = envInline;

            cssInline   = run_reader(&envInline,   READER_INLINE);
            cssSnapshot = run_reader(&envSnapshot, READER_SNAPSHOT);

            expected = mask_before(&envInline);
            ASSERT_EQ(cssInline,   expected);
            ASSERT_EQ(cssSnapshot, expected);
            ASSERT_EQ(cssInline,   cssSnapshot);
            ASSERT_EQ(mask_before(&envInline), mask_after(&envInline));
            ASSERT_EQ(popcount32(cssInline), popcount32(cssSnapshot));
        }
    }
}

/* ==========================================================================
 * CASE 5 -- the 2x2 property sweep, exhaustive over every schedule each writer
 * shape admits.
 *
 * Enumerated recursively: all non-decreasing writerProgress[0..count-1] over
 * [0, count] for WRITER_TORN, and the subset whose entries are only 0 or count
 * for WRITER_ATOMIC.
 *
 * Two things are asserted per schedule, not just "some schedule goes wrong":
 *
 *   -- the mask equals the one implied by which progress value each channel's
 *      single read saw (channel i holds its AFTER value iff i < that value);
 *
 *   -- and, structurally, the mask is one the operator commanded IFF the reader
 *      saw the setter's change either wholly applied or wholly unapplied ACROSS
 *      THE CHANNELS IT CHANGES. Anything in between is by definition a hybrid of
 *      two configurations. Stated that way the property is not a restatement of
 *      the mask arithmetic, and it is why a setter that changes only ONE channel
 *      can never produce a hybrid -- the single-channel branch's exemption.
 *
 * Note the inline reader's outcome is NOT describable by one split point even
 * though the schedule is non-decreasing: progress [1,1,3,3] makes channels 0
 * and 2 hold AFTER while 1 and 3 hold BEFORE. That is a real interleaving (the
 * writer advancing by different amounts between different reads) and the
 * per-channel form above covers it; a split-point model would not.
 * ========================================================================== */
static void sweep_recurse(ScanEnv *e, ReaderShape reader, WriterShape writer,
                          unsigned k, unsigned floorValue,
                          unsigned *reachableNonCommanded,
                          unsigned *schedules)
{
    unsigned p;

    if (k == e->count) {
        uint32_t css = run_reader(e, reader);
        bool commanded = mask_is_commanded(e, css);
        uint32_t expected = 0;
        unsigned i, changedAfter = 0, changedBefore = 0;

        for (i = 0; i < e->count; i++) {
            /* Which progress value did THIS channel's single read see? The
             * inline reader's k-th read is of channel k and sees entry k; every
             * read of the snapshot capture sees entry 0. */
            unsigned progress = (reader == READER_SNAPSHOT)
                              ? e->writerProgress[0]
                              : e->writerProgress[i];
            bool holdsAfter = (i < progress);
            bool on = holdsAfter ? e->ch[i].after : e->ch[i].before;
            if (on) {
                expected |= css_bit(e->ch[i].an);
            }
            if (e->ch[i].before != e->ch[i].after) {
                if (holdsAfter) {
                    changedAfter++;
                } else {
                    changedBefore++;
                }
            }
        }

        (*schedules)++;
        if (!commanded) {
            (*reachableNonCommanded)++;
        }
        ASSERT_EQ(css, expected);
        ASSERT_EQ(commanded, (changedAfter == 0u) || (changedBefore == 0u));
        return;
    }

    for (p = floorValue; p <= e->count; p++) {
        if (writer == WRITER_ATOMIC && p != 0u && p != e->count) {
            continue;       /* an atomic apply is never partially observable */
        }
        e->writerProgress[k] = p;
        sweep_recurse(e, reader, writer, k + 1u, p,
                      reachableNonCommanded, schedules);
    }
}

TEST(only_both_halves_together_make_a_non_commanded_mask_impossible)
{
    struct {
        ReaderShape reader;
        WriterShape writer;
        bool        expectReachable;
        const char *label;
    } combos[] = {
        { READER_INLINE,   WRITER_TORN,   true,  "shipped defect"   },
        { READER_SNAPSHOT, WRITER_TORN,   true,  "reader-only fix"  },
        { READER_INLINE,   WRITER_ATOMIC, true,  "writer-only fix"  },
        { READER_SNAPSHOT, WRITER_ATOMIC, false, "#1112 round 3"    },
    };
    size_t c;

    for (c = 0; c < sizeof(combos) / sizeof(combos[0]); c++) {
        ScanEnv env;
        unsigned nonCommanded = 0, schedules = 0;

        scenario_four_channel(&env);
        /* The sweep is only meaningful if the setter changes at least two
         * channels -- one changed channel cannot make a hybrid, whatever the
         * interleaving, which is the single-channel branch's exemption. */
        ASSERT_TRUE(changed_count(&env) >= 2u);
        sweep_recurse(&env, combos[c].reader, combos[c].writer, 0u, 0u,
                      &nonCommanded, &schedules);

        ASSERT_TRUE(schedules > 0u);
        if (combos[c].expectReachable) {
            if (nonCommanded == 0u) {
                printf("    %s: expected a reachable non-commanded mask over "
                       "%u schedules, found none\n",
                       combos[c].label, schedules);
            }
            ASSERT_TRUE(nonCommanded > 0u);
        } else {
            if (nonCommanded != 0u) {
                printf("    %s: %u of %u schedules produced a mask for a "
                       "configuration that was never commanded\n",
                       combos[c].label, nonCommanded, schedules);
            }
            ASSERT_EQ(nonCommanded, 0);
        }
    }

    /* The same four rows against the audit's own two-channel scenario, so the
     * result is not an artefact of the four-channel fixture. */
    for (c = 0; c < sizeof(combos) / sizeof(combos[0]); c++) {
        ScanEnv env;
        unsigned nonCommanded = 0, schedules = 0;

        scenario_chan1_to_chan2(&env);
        sweep_recurse(&env, combos[c].reader, combos[c].writer, 0u, 0u,
                      &nonCommanded, &schedules);

        ASSERT_TRUE(schedules > 0u);
        ASSERT_EQ(nonCommanded > 0u, combos[c].expectReachable);
    }
}

/* ==========================================================================
 * CASE 6 -- read accounting. Both shapes read each channel EXACTLY ONCE. This
 * is the fidelity claim that makes the schedule model necessary: the fix does
 * not change how many reads happen, only when they happen relative to a
 * writer, so a read-count model (test_985's) would find the two shapes
 * identical and would not be able to express this defect at all.
 * ========================================================================== */
TEST(both_shapes_read_every_channel_exactly_once)
{
    ScanEnv env;
    unsigned i;

    scenario_chan3_to_chan514(&env);
    sched_flat(&env, 0u);

    (void)run_reader(&env, READER_INLINE);
    ASSERT_EQ(env.reads, env.count);
    for (i = 0; i < env.count; i++) {
        ASSERT_EQ(env.readsPerChannel[i], 1);
    }

    (void)run_reader(&env, READER_SNAPSHOT);
    ASSERT_EQ(env.reads, env.count);
    for (i = 0; i < env.count; i++) {
        ASSERT_EQ(env.readsPerChannel[i], 1);
    }
}

int main(void)
{
    printf("#1112 round 3 -- MC12b_ComputeScanList enable-flag snapshot\n");
    printf("---------------------------------------------\n");
    RUN(inline_read_reports_a_pair_that_never_existed);
    RUN(inline_read_reports_a_three_input_scan_from_two_two_input_sets);
    RUN(snapshot_capture_lands_on_a_commanded_set_at_every_instant);
    RUN(a_stable_configuration_makes_both_shapes_agree);
    RUN(only_both_halves_together_make_a_non_commanded_mask_impossible);
    RUN(both_shapes_read_every_channel_exactly_once);
    return TEST_SUMMARY();
}
