/* ==========================================================================
 * test_958_sd_benchmark_filename_collision.c -- issue #958
 *
 * WHAT IS UNDER TEST
 *
 * SYST:STOR:SD:BENCHmark (SCPI_StorageSDBenchmark, SCPIStorageSD.c) names its
 * scratch file from the FreeRTOS tick and then publishes that name into the
 * shared logging target, which the SD task opens with
 * SYS_FS_FILE_OPEN_WRITE_PLUS -- i.e. TRUNCATING (sd_card_manager.c,
 * OPEN_FILE). Until #958 the name was built from a 16-BIT slice of the tick:
 *
 *     snprintf(benchLogFile, SD_CARD_MANAGER_CONF_FILE_NAME_LEN_MAX,
 *              "benchmark_%d.dat", (int)(xTaskGetTickCount() & 0xFFFF));
 *
 * so it repeated every 65536 ticks -- 65.5 s at this firmware's 1 kHz tick.
 * Two benchmarks that far apart on one board targeted the SAME file and the
 * second destroyed the first's contents: no error, no log line, and nothing a
 * client could have done to prevent it. The three python tests that snapshot
 * `benchmark_*.dat` before a run and delete only the set difference
 * (test_728 / test_851 / test_943) cannot defend against it -- their
 * protection governs which files they REMOVE, and this loss happens inside the
 * open.
 *
 * The fix uses the WHOLE tick, and pairs it with a per-boot sequence:
 *
 *     taskENTER_CRITICAL();
 *     benchNameSeq = ++gBenchNameSeq;
 *     taskEXIT_CRITICAL();
 *     snprintf(benchLogFile, SD_CARD_MANAGER_CONF_FILE_NAME_LEN_MAX,
 *              "benchmark_%lu_%lu.dat", (unsigned long)xTaskGetTickCount(),
 *              (unsigned long)benchNameSeq);
 *
 * WHERE THE WITHIN-BOOT UNIQUENESS COMES FROM: the SEQUENCE, not the tick.
 * gBenchNameSeq is incremented once per named run and is never reset while
 * the board is up, so no two runs of one boot can produce the same name.
 *
 * The tick alone does NOT give that, and this file's first revision said it
 * did. TickType_t is 32-bit here (configTICK_TYPE_WIDTH_IN_BITS is
 * TICK_TYPE_WIDTH_32_BITS, FreeRTOSConfig.h:125), so it wraps after 49.7
 * days of uptime and two runs exactly one wrap apart would share a value.
 * CASE 5 below is that scenario, and it is the reason the sequence exists.
 *
 * What the first revision's argument DOES establish, and it is still true,
 * is the narrower claim that CONSECUTIVE runs cannot share a tick: the
 * callback cannot run to completion in under a tick. Its last step is the
 * drain-and-close wait
 * (`while (!sd_card_manager_IsIdle() && idleWait < 500) vTaskDelay(10)`), and
 * the loop cannot exit without entering: the `mode = MODE_NONE` +
 * sd_card_manager_UpdateSettings() just above it forces the manager to DEINIT,
 * while IsIdle() is IDLE-or-INIT only -- so one vTaskDelay(10), i.e. ten
 * ticks, always happens. The `testInProgress` interlock means the next run's
 * name is not built until this one has returned, so consecutive names are
 * >= 10 ticks apart, whichever transport calls it. A run refused before the
 * arm creates no file and therefore cannot collide with anything. None of
 * that reaches two runs a WRAP apart, which is why it is not the guarantee.
 *
 * The tick stays as the LEADING field because it is the half a human and the
 * companion test can read: test_958_benchmark_scratch_name_unique.py asserts
 * it advances by roughly the elapsed wall clock, and that arm is what
 * discriminates pre-#958 firmware from post. The sequence makes the name
 * unique; the tick makes it legible. CASE 1 pins that the tick is still
 * carried in full, so the sequence cannot quietly become the only field.
 *
 * HOW IT IS TESTED
 *
 * SCPIStorageSD.c is not host-compilable -- libscpi, FreeRTOS and the whole SD
 * manager -- so this file follows test_943_bench_stall_bound.c and
 * test_953_bench_suspend_diagnosis.c: it re-implements the pre-fix and
 * post-fix NAME GENERATION, byte for byte including the snprintf bound and
 * the explicit terminator, and compares the two shapes' output on identical
 * tick values. What is proven is the naming algebra, which is all #958
 * changes at this site.
 *
 * The format string and the field-length constant are therefore COPIES. The
 * Makefile target greps both out of the firmware and refuses to build if
 * either has drifted -- and it also fails if the masked 16-bit form comes
 * BACK, because that is the defect itself and this suite's whole premise is
 * that it is gone.
 *
 * WHAT THIS FILE DELIBERATELY DOES NOT COVER
 *
 * The other half of #958: the reboot case. A reboot restarts the sequence at
 * 1 and the tick at 0, so NEITHER field distinguishes one boot from another,
 * and a run after a reboot can land on the name of a file left on the card
 * before it and truncate it silently.
 *
 * Being precise about when, because an earlier revision of this header was
 * not: it is NOT that every boot's first run shares one name. The tick is
 * sampled at NAMING time, not at boot, so two first-runs collide only when
 * they arrive at the same post-boot offset -- `benchmark_5000_1.dat` and
 * `benchmark_10000_1.dat` are different names. That is a coincidence nothing
 * prevents rather than a certainty, which is all the defect needs: the name
 * carries no boot identity, so the card is the only place the question can
 * be asked. No counter held in RAM can close that half. Closing it needs the
 * candidate name STAT-ed before it is armed, and that cannot be done in
 * SCPI_StorageSDBenchmark: the FAT volume is not mounted there. The SD task
 * mounts it only inside a session (sd_card_manager.c:1457, reached only when
 * `mode != MODE_NONE`) and unmounts it at the end of one (:1715);
 * SYS_FS_AUTOMOUNT_ENABLE is false (configuration.h:98). A SYS_FS_FileStat()
 * issued with the manager IDLE fails: `directory` (default "DAQiFi") carries
 * no "/mnt/" prefix, so SYS_FS_GetDisk() takes its "assume the default
 * volume" branch (sys_fs.c:196-206) and finds `gSYSFSCurrentMountPoint.inUse
 * == false` -- SYS_FS_ERROR_NO_FILESYSTEM (sys_fs.c:205), not NO_PATH /
 * NO_FILE (a "/mnt/"-prefixed path would fail the same way, by the sibling
 * branch's SYS_FS_ERROR_INVALID_NAME at sys_fs.c:191 -- same conclusion
 * either way), and neither is the "genuinely absent" NO_PATH / NO_FILE that
 * sd_BucketDirExists() treats as free: failing safe on it refuses every
 * benchmark, and reading it as "free" is a probe that establishes nothing.
 * The existence question can only be asked where the
 * volume is mounted. That is the follow-up on #958, and it is deliberately
 * NOT modelled here -- a test for a mechanism that does not exist is the
 * vacuous-gate failure mode, not coverage.
 *
 * There is likewise no assertion that the reboot collision STILL happens.
 * Pinning a defect in place makes the eventual fix fail this suite; the
 * boundary belongs in prose, here.
 *
 * A round of review added such a case anyway -- it asserted that two calls
 * with identical arguments produce identical names -- and the pre-merge audit
 * threw it out, correctly. new_bench_name() is a pure function of its two
 * arguments, so that comparison is true for EVERY possible implementation:
 * it could not fail, while its comment claimed it would fail once the SD-task
 * follow-up lands. It could not have: that follow-up resolves the candidate
 * path inside the SD task and never reaches this helper. A green case that a
 * maintainer reads as 'the boundary is still guarded' is worse than no case,
 * which is what the paragraph above already said.
 * ========================================================================== */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "test_framework.h"

/* SD_CARD_MANAGER_CONF_FILE_NAME_LEN_MAX (sd_card_manager.h). The longest
 * name the SD:FILE setter accepts; the runtime field is [LEN_MAX + 1] so it
 * can carry the terminator. Guarded by the Makefile recipe. */
#define FW_FILE_NAME_LEN_MAX 40

/* One 16-bit wrap of the tick: the interval over which the pre-fix name
 * repeated. 65536 ticks = 65.536 s at 1 kHz. */
#define TICK_WRAP 65536UL

/* ==========================================================================
 * The two name-generation shapes, extracted from SCPI_StorageSDBenchmark.
 *
 * Both model the firmware exactly: a [LEN_MAX + 1] destination, snprintf
 * bounded to LEN_MAX (not LEN_MAX + 1 -- the firmware passes the constant
 * itself), then an explicit terminator at [LEN_MAX]. `truncated` reports what
 * the firmware discards: snprintf's return is the length it WOULD have
 * written, so >= the bound means the name was cut.
 * ========================================================================== */
typedef struct {
    char name[FW_FILE_NAME_LEN_MAX + 1];
    bool truncated;
} BenchName;

/* PRE-#958: the tick masked to 16 bits, printed as an int. */
static BenchName old_bench_name(uint32_t tick)
{
    BenchName b;
    int n;

    memset(&b, 0, sizeof(b));
    n = snprintf(b.name, FW_FILE_NAME_LEN_MAX,
                 "benchmark_%d.dat", (int)(tick & 0xFFFF));
    b.name[FW_FILE_NAME_LEN_MAX] = '\0';
    b.truncated = (n < 0) || (n >= (int)FW_FILE_NAME_LEN_MAX);
    return b;
}

/* POST-#958: the whole tick, then the per-boot sequence. */
static BenchName new_bench_name(uint32_t tick, uint32_t seq)
{
    BenchName b;
    int n;

    memset(&b, 0, sizeof(b));
    n = snprintf(b.name, FW_FILE_NAME_LEN_MAX,
                 "benchmark_%lu_%lu.dat", (unsigned long)tick,
                 (unsigned long)seq);
    b.name[FW_FILE_NAME_LEN_MAX] = '\0';
    b.truncated = (n < 0) || (n >= (int)FW_FILE_NAME_LEN_MAX);
    return b;
}

/* The firmware does not format an arbitrary sequence value -- it ADVANCES the
 * counter and formats the result, in that order:
 *
 *     taskENTER_CRITICAL();
 *     benchNameSeq = ++gBenchNameSeq;
 *     taskEXIT_CRITICAL();
 *     snprintf(..., "benchmark_%lu_%lu.dat", tick, benchNameSeq);
 *
 * Every case that calls new_bench_name() with a seq of its own choosing is
 * blind to that ordering, because an INJECTED value cannot be stale. The
 * pre-merge audit on this PR reproduced the consequence: swap those two steps
 * in the firmware and every name carries the counter's initial value, which
 * reopens the exact collision this change closes -- and it passed every
 * Makefile guard and every case in this file.
 *
 * The Makefile's order pin is what catches that in the FIRMWARE; a host model
 * cannot. This wrapper is here so the ordering is expressed in the test too,
 * rather than living only in a grep: CASE 6 drives it, and a future edit that
 * moves the increment after the format breaks that case.
 */
typedef struct {
    uint32_t seq;          /* gBenchNameSeq: starts at 0, never reset */
} BenchSeqCounter;

static BenchName next_bench_name(BenchSeqCounter *c, uint32_t tick)
{
    uint32_t seq = ++c->seq;      /* PRE-increment, as the firmware does */
    return new_bench_name(tick, seq);
}

/* ==========================================================================
 * Fixtures
 *
 * A tick table rather than a sweep: the values that matter are the wrap
 * boundaries, the extremes of the 32-bit range, and one ordinary mid-uptime
 * value. Anything a sweep would add is a repetition of one of these.
 * ========================================================================== */
static const uint32_t kTicks[] = {
    0UL,                    /* boot */
    1UL,
    65535UL,                /* last tick before the pre-fix name repeats */
    65536UL,                /* the repeat itself: same name as tick 0 */
    65537UL,                /* ... and as tick 1 */
    131072UL,               /* two wraps on from 0 */
    1000UL,                 /* 1 s of uptime */
    3600000UL,              /* 1 h */
    86400000UL,             /* 24 h */
    4294901759UL,           /* UINT32_MAX - 65536 */
    4294967294UL,
    4294967295UL            /* UINT32_MAX: the longest name, 49.7 days */
};
#define N_TICKS (sizeof(kTicks) / sizeof(kTicks[0]))

/* Sequence values. gBenchNameSeq is PRE-incremented, so the first named run
 * of a boot is 1 and 0 is never produced -- which is why 1, not 0, is what
 * the reboot arm of CASE 5 uses on both sides. UINT32_MAX is the longest
 * field the counter can print and is what CASE 4 sizes against. */
static const uint32_t kSeqs[] = {
    1UL,                    /* the first named run of a boot */
    2UL,
    99UL,
    65536UL,
    4294967295UL            /* UINT32_MAX */
};
#define N_SEQS (sizeof(kSeqs) / sizeof(kSeqs[0]))

/* The python suite's cleanup in test_728 / test_851 / test_943 matches
 * `benchmark_` + non-space + `.dat`. Both halves are a contract with those
 * tests, not cosmetics: change either and their snapshot-and-diff stops
 * recognising the firmware's own files. */
static const char *const kPrefix = "benchmark_";
static const char *const kSuffix = ".dat";

/* The decimal expansion of a tick, for asserting that a generated name really
 * carries THAT tick rather than some other varying field. Two rotating
 * buffers so two ticks can be checked in one expression. */
static const char *decimal(uint32_t v)
{
    static char buf[2][12];
    static unsigned slot = 0u;
    slot ^= 1u;
    snprintf(buf[slot], sizeof(buf[slot]), "%lu", (unsigned long)v);
    return buf[slot];
}

static bool has_prefix(const char *s, const char *p)
{
    return strncmp(s, p, strlen(p)) == 0;
}

static bool has_suffix(const char *s, const char *p)
{
    size_t ls = strlen(s);
    size_t lp = strlen(p);
    return (ls >= lp) && (strcmp(s + (ls - lp), p) == 0);
}

/* ==========================================================================
 * Tests
 * ========================================================================== */

/* CASE 1 -- THE BUG. Two runs one 16-bit wrap apart. The pre-fix shape
 * produces the SAME name, so the second run's truncating open destroys the
 * first run's file; the post-fix shape produces different names.
 *
 * Swept over several bases and several whole multiples of the wrap, because a
 * shape that only fixed "exactly one wrap from tick 0" would pass a
 * single-case version of this. This is the assertion that screams if the mask
 * is reinstated.
 *
 * THE SEQUENCE IS HELD FIXED here, deliberately. With two different sequence
 * values the names would differ whatever the tick did, and this case would
 * pass with the tick masked again -- it would be measuring the wrong field.
 * Pinning seq makes the tick the only thing that can separate the two names,
 * so the mask cannot come back unnoticed. CASE 5 is the mirror image: the
 * tick held fixed so only the sequence can separate them. */
TEST(one_tick_wrap_apart_collides_pre_fix_and_not_post_fix)
{
    static const uint32_t bases[] = { 0UL, 1UL, 1000UL, 40000UL, 3600000UL };
    static const uint32_t multiples[] = { 1UL, 2UL, 17UL, 1000UL };
    static const uint32_t kFixedSeq = 7UL;   /* see the note above */
    size_t b, m;
    unsigned oldCollisions = 0;

    for (b = 0; b < sizeof(bases) / sizeof(bases[0]); b++) {
        for (m = 0; m < sizeof(multiples) / sizeof(multiples[0]); m++) {
            uint32_t t1 = bases[b];
            uint32_t t2 = (uint32_t)(bases[b] + multiples[m] * TICK_WRAP);
            BenchName o1 = old_bench_name(t1);
            BenchName o2 = old_bench_name(t2);
            BenchName n1 = new_bench_name(t1, kFixedSeq);
            BenchName n2 = new_bench_name(t2, kFixedSeq);

            /* The defect: one name for two runs. */
            ASSERT_TRUE(strcmp(o1.name, o2.name) == 0);
            if (strcmp(o1.name, o2.name) == 0) {
                oldCollisions++;
            }

            /* The fix: two names, and the ticks really were different. */
            ASSERT_TRUE(t1 != t2);
            ASSERT_TRUE(strcmp(n1.name, n2.name) != 0);

            /* Not merely "two different strings": each name must carry its
             * OWN tick in full. A shape that differed by some other varying
             * field would satisfy the compare above while still masking the
             * tick, so the tick's decimal expansion is what gets pinned. */
            ASSERT_TRUE(strstr(n1.name, decimal(t1)) != NULL);
            ASSERT_TRUE(strstr(n2.name, decimal(t2)) != NULL);
        }
    }

    /* Every pair collided pre-fix -- it is systematic, not incidental. */
    ASSERT_EQ(oldCollisions,
              (sizeof(bases) / sizeof(bases[0])) *
              (sizeof(multiples) / sizeof(multiples[0])));
}

/* CASE 2 -- injectivity over the whole fixture. Distinct ticks must give
 * distinct names post-fix, and the pre-fix shape must be shown to fail that
 * for at least one pair (otherwise the fixture is not exercising the defect
 * and CASE 1 would be the only thing holding the line). */
TEST(distinct_ticks_give_distinct_names)
{
    size_t i, j;
    unsigned oldCollisions = 0;

    for (i = 0; i < N_TICKS; i++) {
        for (j = i + 1; j < N_TICKS; j++) {
            /* Same sequence on both sides, for CASE 1's reason. */
            BenchName ni = new_bench_name(kTicks[i], 5UL);
            BenchName nj = new_bench_name(kTicks[j], 5UL);
            BenchName oi = old_bench_name(kTicks[i]);
            BenchName oj = old_bench_name(kTicks[j]);

            ASSERT_TRUE(kTicks[i] != kTicks[j]);
            ASSERT_TRUE(strcmp(ni.name, nj.name) != 0);

            if (strcmp(oi.name, oj.name) == 0) {
                oldCollisions++;
            }
        }
    }

    ASSERT_TRUE(oldCollisions > 0);
}

/* CASE 3 -- the contract with the python suite. Every generated name must
 * keep the `benchmark_` prefix and the `.dat` suffix, or test_728 / test_851 /
 * test_943's snapshot-and-diff stops recognising the firmware's own scratch
 * files and starts either deleting a preserved file or leaving litter. */
TEST(names_keep_the_benchmark_prefix_and_dat_suffix)
{
    size_t i;

    for (i = 0; i < N_TICKS; i++) {
        /* Rotate the sequence too, so the contract is checked against both
         * fields varying rather than against one fixed suffix. */
        BenchName n = new_bench_name(kTicks[i], kSeqs[i % N_SEQS]);

        ASSERT_TRUE(has_prefix(n.name, kPrefix));
        ASSERT_TRUE(has_suffix(n.name, kSuffix));
        /* Nothing between the prefix and the suffix may be whitespace: the
         * python matcher uses `\S`, and a name with a space in it would be
         * split by every whitespace-delimited parser in that suite. */
        ASSERT_TRUE(strchr(n.name, ' ') == NULL);
        ASSERT_TRUE(strchr(n.name, '\t') == NULL);
        /* And there IS a discriminator between them -- an empty middle would
         * be one fixed name for every run, which is the collision this issue
         * is about in its worst form. */
        ASSERT_TRUE(strlen(n.name) >
                    strlen(kPrefix) + strlen(kSuffix));
    }
}

/* CASE 4 -- the widened field must still fit. The name goes into a
 * [LEN_MAX + 1] buffer through an snprintf bounded to LEN_MAX, so a name that
 * did not fit would be silently CUT -- and two cut names can be equal again,
 * which would reintroduce the very collision this fix removes. The worst case
 * is both fields at UINT32_MAX: "benchmark_" (10) + 10 digits + "_" (1) +
 * 10 digits + ".dat" (4) = 35, against a 40-character field. */
TEST(longest_name_fits_the_field_without_truncation)
{
    size_t i;
    BenchName worst = new_bench_name(4294967295UL, 4294967295UL);

    ASSERT_EQ(strlen(worst.name), 35u);
    ASSERT_FALSE(worst.truncated);
    ASSERT_TRUE(strcmp(worst.name,
                       "benchmark_4294967295_4294967295.dat") == 0);

    /* Headroom, stated as a number so a future widening that eats it fails
     * here rather than in the field. */
    ASSERT_TRUE(strlen(worst.name) < FW_FILE_NAME_LEN_MAX);
    ASSERT_EQ(FW_FILE_NAME_LEN_MAX - strlen(worst.name), 5u);

    for (i = 0; i < N_TICKS; i++) {
        size_t k;
        for (k = 0; k < N_SEQS; k++) {
            BenchName n = new_bench_name(kTicks[i], kSeqs[k]);
            ASSERT_FALSE(n.truncated);
            ASSERT_TRUE(strlen(n.name) <= 35u);
        }
    }
}

/* CASE 5 -- THE 49.7-DAY WRAP, which is the whole reason the sequence field
 * exists.
 *
 * The mirror of CASE 1. There the sequence was held fixed so that only the
 * tick could separate two names; here the TICK is held fixed so that only the
 * sequence can. Two runs exactly one 32-bit tick wrap apart read the SAME
 * value from xTaskGetTickCount(), so a name built from the tick alone repeats
 * -- the same silent truncation as the 16-bit mask, 49.7 days apart instead
 * of 65.5 seconds. Qodo raised it on this PR's first review; TickType_t is
 * 32 bits here (FreeRTOSConfig.h:125), so it is reachable and not academic.
 *
 * This is the case that fails if the sequence field is ever dropped, which is
 * the regression the Makefile's format pin cannot catch on its own: a pin
 * proves the text is there, and this proves the text does something. */
TEST(same_tick_one_wrap_apart_still_gives_distinct_names)
{
    size_t i;

    for (i = 0; i < N_TICKS; i++) {
        /* Two runs at the same tick value, consecutive sequence numbers --
         * i.e. one wrap of uptime apart, with no other run in between. */
        BenchName first = new_bench_name(kTicks[i], 41UL);
        BenchName second = new_bench_name(kTicks[i], 42UL);

        ASSERT_TRUE(strcmp(first.name, second.name) != 0);

        /* Not merely two different strings: each carries its OWN sequence, so
         * the field doing the separating is the one this case is about. */
        ASSERT_TRUE(strstr(first.name, decimal(41UL)) != NULL);
        ASSERT_TRUE(strstr(second.name, decimal(42UL)) != NULL);

        /* And the tick survives in full -- the sequence was ADDED to the name,
         * it did not replace the field the companion test reads. */
        ASSERT_TRUE(strstr(first.name, decimal(kTicks[i])) != NULL);
    }

    /* Non-adjacent sequence values as well. A shape that only distinguished
     * n from n+1 -- a parity bit, say -- would pass the loop above. */
    {
        size_t a, b;
        for (a = 0; a < N_SEQS; a++) {
            for (b = a + 1; b < N_SEQS; b++) {
                BenchName x = new_bench_name(1000UL, kSeqs[a]);
                BenchName y = new_bench_name(1000UL, kSeqs[b]);

                ASSERT_TRUE(kSeqs[a] != kSeqs[b]);
                ASSERT_TRUE(strcmp(x.name, y.name) != 0);
            }
        }
    }
}

/* CASE 6 -- the firmware ADVANCES the counter before formatting, and the
 * order matters as much as the field's presence.
 *
 * Driven through next_bench_name() rather than by injecting a sequence,
 * because an injected value cannot be stale. Format the name first and
 * advance the counter afterwards -- the mutation the pre-merge audit
 * reproduced -- and every run of a boot emits the counter's INITIAL value,
 * so two runs at one tick collide exactly as they did before this fix.
 *
 * Two properties, and both are needed. The first run must not carry 0 (the
 * pre-increment is what makes the initial value unreachable), and successive
 * runs at the SAME tick must differ (the advance is what makes each name
 * new). A post-increment satisfies neither. */
TEST(the_counter_is_advanced_before_the_name_is_formatted)
{
    BenchSeqCounter c = { 0u };
    size_t i;

    /* The first name of a boot carries 1, not the counter's initial 0. */
    {
        BenchName first = next_bench_name(&c, 4242UL);
        ASSERT_TRUE(strcmp(first.name, "benchmark_4242_1.dat") == 0);
    }

    /* And every later run at the SAME tick differs from every earlier one.
     * Held at one tick deliberately: with the tick varying, the names would
     * differ whatever the counter did, and this case would pass against a
     * counter that never moved. */
    {
        BenchName seen[6];
        size_t j;

        seen[0] = next_bench_name(&c, 777UL);
        for (i = 1; i < 6; i++) {
            seen[i] = next_bench_name(&c, 777UL);
            for (j = 0; j < i; j++) {
                ASSERT_TRUE(strcmp(seen[i].name, seen[j].name) != 0);
            }
        }
    }
}

int main(void)
{
    printf("#958 -- SD:BENCHmark scratch filename (extracted name generation)\n");
    printf("---------------------------------------------\n");
    RUN(one_tick_wrap_apart_collides_pre_fix_and_not_post_fix);
    RUN(distinct_ticks_give_distinct_names);
    RUN(names_keep_the_benchmark_prefix_and_dat_suffix);
    RUN(longest_name_fits_the_field_without_truncation);
    RUN(same_tick_one_wrap_apart_still_gives_distinct_names);
    RUN(the_counter_is_advanced_before_the_name_is_formatted);
    return TEST_SUMMARY();
}
