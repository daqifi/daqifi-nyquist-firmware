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
 * The fix uses the tick WHOLE:
 *
 *     snprintf(benchLogFile, SD_CARD_MANAGER_CONF_FILE_NAME_LEN_MAX,
 *              "benchmark_%lu.dat", (unsigned long)xTaskGetTickCount());
 *
 * WHY THE WHOLE TICK IS SUFFICIENT WITHIN ONE BOOT, and it is not "1 ms is
 * short". It is that the callback cannot run to completion in under a tick.
 * Its last step is the drain-and-close wait
 * (`while (!sd_card_manager_IsIdle() && idleWait < 500) vTaskDelay(10)`), and
 * the loop cannot exit without entering: the `mode = MODE_NONE` +
 * sd_card_manager_UpdateSettings() just above it forces the manager to DEINIT,
 * while IsIdle() is IDLE-or-INIT only -- so one vTaskDelay(10), i.e. ten
 * ticks, always happens. The `testInProgress` interlock means the next run's
 * name is not built until this one has returned, so consecutive names are
 * >= 10 ticks apart, whichever transport calls it. A run refused before the
 * arm creates no file and therefore cannot collide with anything. TickType_t
 * is 32-bit here (configTICK_TYPE_WIDTH_IN_BITS is TICK_TYPE_WIDTH_32_BITS,
 * FreeRTOSConfig.h:125), so the value itself only
 * repeats after 49.7 days of uptime.
 *
 * That premise is load-bearing, so CASE 5 below asserts its consequence
 * (equal ticks give equal names) rather than leaving it implicit -- a reader
 * of this suite should see exactly what the fix rests on.
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
 * The other half of #958: the reboot case. The tick restarts at 0 on reboot,
 * so a run after a reboot can still land on the name of a file left on the
 * card before it, and still truncates it silently. Closing that needs the
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

/* POST-#958: the whole tick. */
static BenchName new_bench_name(uint32_t tick)
{
    BenchName b;
    int n;

    memset(&b, 0, sizeof(b));
    n = snprintf(b.name, FW_FILE_NAME_LEN_MAX,
                 "benchmark_%lu.dat", (unsigned long)tick);
    b.name[FW_FILE_NAME_LEN_MAX] = '\0';
    b.truncated = (n < 0) || (n >= (int)FW_FILE_NAME_LEN_MAX);
    return b;
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
 * is reinstated. */
TEST(one_tick_wrap_apart_collides_pre_fix_and_not_post_fix)
{
    static const uint32_t bases[] = { 0UL, 1UL, 1000UL, 40000UL, 3600000UL };
    static const uint32_t multiples[] = { 1UL, 2UL, 17UL, 1000UL };
    size_t b, m;
    unsigned oldCollisions = 0;

    for (b = 0; b < sizeof(bases) / sizeof(bases[0]); b++) {
        for (m = 0; m < sizeof(multiples) / sizeof(multiples[0]); m++) {
            uint32_t t1 = bases[b];
            uint32_t t2 = (uint32_t)(bases[b] + multiples[m] * TICK_WRAP);
            BenchName o1 = old_bench_name(t1);
            BenchName o2 = old_bench_name(t2);
            BenchName n1 = new_bench_name(t1);
            BenchName n2 = new_bench_name(t2);

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
            BenchName ni = new_bench_name(kTicks[i]);
            BenchName nj = new_bench_name(kTicks[j]);
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
        BenchName n = new_bench_name(kTicks[i]);

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
 * is UINT32_MAX: "benchmark_" (10) + 10 digits + ".dat" (4) = 24. */
TEST(longest_name_fits_the_field_without_truncation)
{
    size_t i;
    BenchName worst = new_bench_name(4294967295UL);

    ASSERT_EQ(strlen(worst.name), 24u);
    ASSERT_FALSE(worst.truncated);
    ASSERT_TRUE(strcmp(worst.name, "benchmark_4294967295.dat") == 0);

    /* Headroom, stated as a number so a future widening that eats it fails
     * here rather than in the field. */
    ASSERT_TRUE(strlen(worst.name) < FW_FILE_NAME_LEN_MAX);
    ASSERT_EQ(FW_FILE_NAME_LEN_MAX - strlen(worst.name), 16u);

    for (i = 0; i < N_TICKS; i++) {
        BenchName n = new_bench_name(kTicks[i]);
        ASSERT_FALSE(n.truncated);
        ASSERT_TRUE(strlen(n.name) <= 24u);
    }
}

/* CASE 5 -- the premise, made visible. The fix does NOT make the name unique
 * by adding state; it relies on two runs that each create a file never sharing
 * a tick value (see WHY THE WHOLE TICK IS SUFFICIENT in the header). So equal
 * ticks give equal names, by construction, and that is the one thing standing
 * between this fix and a same-tick collision.
 *
 * Asserted rather than left in prose because it is the assumption a reviewer
 * has to check against the firmware, and because a later change that DOES add
 * a discriminator (a sequence counter, a sub-tick read) will fail here -- at
 * which point the header's argument is no longer what holds, and both should
 * be updated together. */
TEST(equal_ticks_give_equal_names_which_is_what_the_fix_relies_on)
{
    size_t i;

    for (i = 0; i < N_TICKS; i++) {
        BenchName a = new_bench_name(kTicks[i]);
        BenchName b = new_bench_name(kTicks[i]);

        ASSERT_TRUE(strcmp(a.name, b.name) == 0);
    }

    /* Stated the other way as well: the name is a pure function of the tick,
     * so it carries no boot identity -- which is exactly why the reboot half
     * of #958 is still open (header, WHAT THIS FILE DELIBERATELY DOES NOT
     * COVER). */
    {
        BenchName firstEverRun = new_bench_name(0UL);
        BenchName firstRunAfterAReboot = new_bench_name(0UL);
        ASSERT_TRUE(strcmp(firstEverRun.name,
                           firstRunAfterAReboot.name) == 0);
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
    RUN(equal_ticks_give_equal_names_which_is_what_the_fix_relies_on);
    return TEST_SUMMARY();
}
