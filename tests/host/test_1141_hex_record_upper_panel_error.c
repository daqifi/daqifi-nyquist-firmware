/* ==========================================================================
 * test_1141_hex_record_upper_panel_error.c -- issue #909 / PR #1141
 *
 * WHAT IS UNDER TEST
 *
 * #909 narrowed the bootloader's erase to the LOWER program-flash panel only
 * (0x9D000000-0x9D0FFFFF), so an in-app update stops destroying the NVM
 * settings pages at 0x9D1E0000. APP_ProgramHexRecord (framework/.../nvm.c)
 * narrowed APP_FLASH_END_ADDRESS to match -- but that constant feeds a bounds
 * test (nvm.c, "Make sure we are not writing boot area and device
 * configuration bits") that was ALSO the only thing standing between a record
 * and an unerased write. Before #1141:
 *
 *     if ((ProgAddress >= APP_FLASH_BASE_ADDRESS) &&
 *         (ProgAddress <= APP_FLASH_END_ADDRESS))
 *     {
 *         ... write ...
 *     }
 *     else    // Out of boundaries. Adjust and move on.
 *     {
 *         ... silently skip, advance 4 bytes, keep going ...
 *     }
 *
 * A record whose address is a genuine boot-area/config-word address (outside
 * the whole 2 MB PFM span, e.g. the boot flash at KVA0 ~0x9FC0xxxx -- nvm.c's
 * own APP_NVMWordWrite/RowWrite/QuadWordWrite compare against physical
 * 0x1FC00000, nvm.c:205/222/242) SHOULD take that skip path -- that is #764's
 * contract for a standalone-linked hex, and #1141 must not change it. But a
 * record whose address is in the UPPER program-flash panel (0x9D100000-
 * 0x9D1FFFFF) -- still application code space, just the half #909 stopped
 * erasing -- took the exact SAME skip path, and PROGRAM_FLASH still ACKed it
 * (datastream.c's PROGRAM_FLASH case only fails the response when
 * APP_ProgramHexRecord returns something other than HEX_REC_NORMAL). So an
 * oversized or hand-built .hex silently produced a corrupt image that
 * reported a clean update. Confirmed independently by two audit hunters
 * (issuecomment-5736996706): one found the required CONSTANT at 0x9D100000
 * reading 0xFFFFFFFF, the other the required FUNCTION there reading blank
 * flash at boot.
 *
 * THE FIX (remedy (a) from the brief: give the application-range case a
 * distinguishable failure instead of expanding the doc comment that already
 * conceded the gap): nvm.c now has a THIRD branch, between the write branch
 * and the original skip branch, that classifies an address in
 * (APP_FLASH_END_ADDRESS, APP_FLASH_PFM_END_ADDRESS] -- i.e. still inside the
 * application's 2 MB program-flash span, just above the erased/writable
 * window -- as HEX_REC_PGM_ERROR. Only an address OUTSIDE that whole span
 * (below APP_FLASH_BASE_ADDRESS, or above APP_FLASH_PFM_END_ADDRESS, which is
 * where boot flash and config words actually live) still takes the original
 * skip-and-succeed path. APP_FLASH_PFM_END_ADDRESS (system_config.h) is a new
 * constant equal to the OLD (pre-#909) value of APP_FLASH_END_ADDRESS --
 * 0x9D000000 + 0x200000 - 1 = 0x9D1FFFFF, the full 2 MB PFM span nvm.c's own
 * FRM DS60001193B comment already cites.
 *
 * HOW IT IS TESTED
 *
 * nvm.c is not host-compilable: it #includes "peripheral/nvm/plib_nvm.h",
 * "system/devcon/sys_devcon.h", "peripheral/int/plib_int.h" and <sys/kmem.h>,
 * none of which exist outside the MPLAB X / XC32 install (confirmed
 * empirically -- a bare `gcc -c` on it fails immediately on the first missing
 * header). So, like every other non-includable UUT in this Makefile
 * (SCPIInterface.c, SCPIStorageSD.c, DAC7718.c, ...), this test re-implements
 * the SHAPE of APP_ProgramHexRecord's per-chunk address classification and
 * its DATA_RECORD loop as pure functions, and the recipe's grep guards pin
 * that the real source still has the branch structure this model assumes.
 *
 * UNLIKE most of this Makefile's other model-based tests, the two boundary
 * CONSTANTS are not copied: system_config.h has no hardware #includes at all
 * (confirmed empirically) and this file #includes it directly, so
 * APP_FLASH_BASE_ADDRESS / APP_FLASH_END_ADDRESS / APP_FLASH_PFM_END_ADDRESS
 * below are the REAL values, not a copy that could drift. nvm.h is likewise
 * hardware-free (only <stdint.h>) and is included directly for the real
 * HEX_RECORD_STATUS enum (HEX_REC_NORMAL, HEX_REC_PGM_ERROR).
 *
 * FIDELITY -- what the model is NOT
 *
 * 1. USE_QUAD_WORD_WRITE (nvm.c's 16-byte quad-word path) changes HOW a
 *    CHUNK_WRITE chunk is physically written and its address stride (16
 *    bytes vs. 4), never WHETHER an address classifies as write/error/skip.
 *    The classification is address-only and quad-word-agnostic, so modelling
 *    plain 4-byte strides is sufficient for what #1141 changes; the address
 *    stride itself is untouched by this fix.
 * 2. No actual NVM write is modelled -- CHUNK_WRITE is a classification, not
 *    a performed operation. #1141 does not touch the write path itself
 *    (APP_NVMWordWrite/QuadWordWrite, PLIB_NVM_WriteOperationHasTerminated),
 *    only what happens to an address that never reaches it.
 * 3. Record types other than DATA_RECORD (EXT_SEG_ADRS_RECORD,
 *    EXT_LIN_ADRS_RECORD, END_OF_FILE_RECORD) are untouched by #1141 and not
 *    modelled -- the bounds test lives entirely inside DATA_RECORD's chunk
 *    loop.
 * 4. The CRC check and the "unknown record type" path are untouched and not
 *    modelled.
 *
 * BASELINE: run against c03d13899 (the audited, pre-fix head) first --
 * APP_FLASH_PFM_END_ADDRESS did not exist there, so this file fails to
 * compile ("APP_FLASH_PFM_END_ADDRESS undeclared"), and the Makefile's own
 * grep guards (below) fail first, by name, before the compiler even runs.
 * Both observed failures are reported verbatim in the PR/fire report.
 * ========================================================================== */

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "test_framework.h"

/* Real constants -- both headers are hardware-free (verified empirically),
 * so these are not copies. */
#include "system_config.h"
#include "nvm.h"

/* ==========================================================================
 * Model of nvm.c's per-4-byte-chunk address classification (nvm.c's
 * "Make sure we are not writing boot area and device configuration bits"
 * test, post-#1141). Mirrors the real conditions' operators exactly
 * (>=/<= for the write window, >/<= for the new error window); the grep
 * guards in the Makefile recipe pin that nvm.c's actual text still matches.
 * ========================================================================== */
typedef enum {
    CHUNK_WRITE,   /* in [APP_FLASH_BASE_ADDRESS, APP_FLASH_END_ADDRESS] */
    CHUNK_ERROR,   /* in (APP_FLASH_END_ADDRESS, APP_FLASH_PFM_END_ADDRESS] --
                       #1141: application space, unerased upper panel */
    CHUNK_SKIP     /* outside the whole PFM span -- boot area / config words,
                       #764's unchanged skip-and-succeed contract */
} ChunkClass;

static ChunkClass ClassifyProgAddress(uint32_t progAddress)
{
    if (progAddress >= (uint32_t)APP_FLASH_BASE_ADDRESS &&
        progAddress <= (uint32_t)APP_FLASH_END_ADDRESS)
    {
        return CHUNK_WRITE;
    }
    if (progAddress > (uint32_t)APP_FLASH_END_ADDRESS &&
        progAddress <= (uint32_t)APP_FLASH_PFM_END_ADDRESS)
    {
        return CHUNK_ERROR;
    }
    return CHUNK_SKIP;
}

/* Counts what a modelled record actually did, so a test can assert the
 * boot/config skip path ran UNCHANGED (still counted as "skipped", never as
 * an error), not just that the overall status matched. */
typedef struct {
    unsigned wrote;
    unsigned skipped;
} ChunkCounts;

/* Models APP_ProgramHexRecord's DATA_RECORD chunk loop: `n` consecutive
 * 4-byte chunks starting at `firstAddr`, exactly as the real loop advances
 * HexRecordSt.Address += 4 each iteration. Returns HEX_REC_PGM_ERROR the
 * INSTANT a chunk classifies CHUNK_ERROR, matching the real function's
 * immediate `return` (no further bytes of this record, or the buffer, are
 * processed); returns HEX_REC_NORMAL if every chunk is WRITE or SKIP,
 * matching the tail switch's DATA_RECORD case. */
static char ProcessDataRecordChunks(uint32_t firstAddr, unsigned n, ChunkCounts *counts)
{
    unsigned i;
    if (counts) { counts->wrote = 0; counts->skipped = 0; }

    for (i = 0; i < n; i++)
    {
        uint32_t addr = firstAddr + 4u * (uint32_t)i;
        switch (ClassifyProgAddress(addr))
        {
            case CHUNK_WRITE:
                if (counts) counts->wrote++;
                break;
            case CHUNK_ERROR:
                return HEX_REC_PGM_ERROR;
            case CHUNK_SKIP:
                if (counts) counts->skipped++;
                break;
        }
    }
    return HEX_REC_NORMAL;
}

/* ==========================================================================
 * Boundary values used throughout. Derived from the REAL constants, not
 * hardcoded, so a future panel-size change re-derives these automatically.
 * ========================================================================== */
#define LAST_LOWER_PANEL_CHUNK   ((uint32_t)APP_FLASH_END_ADDRESS - 3u) /* last 4B-aligned chunk <= END */
#define FIRST_GAP_CHUNK          ((uint32_t)APP_FLASH_END_ADDRESS + 1u) /* first byte of the unerased upper panel */
#define LAST_GAP_CHUNK           ((uint32_t)APP_FLASH_PFM_END_ADDRESS - 3u) /* last 4B-aligned chunk <= PFM end */
#define FIRST_BEYOND_PFM         ((uint32_t)APP_FLASH_PFM_END_ADDRESS + 1u) /* first address genuinely outside the app's PFM span */

/* Boot flash physical base, KVA0-translated: nvm.c itself compares the
 * PHYSICAL address against 0x1FC00000 (nvm.c:205/222/242, "Ensure we write
 * to the other program flash"), and its own APP_FlashErase comment
 * (nvm.c:151-153) cites the bootloader's KVA0 kseg0_program_mem as starting
 * at 0x9FC01000. Both are real, cited addresses -- not fabricated. */
#define BOOT_FLASH_KVA0_BASE     ((uint32_t)0x9FC00000u)
#define BOOTLOADER_KSEG0_START   ((uint32_t)0x9FC01000u)

/* ==========================================================================
 * TESTS
 * ========================================================================== */

/* The property the brief requires FIRST: a record targeting the application
 * range above the lower panel must NOT come back as a success status. */
TEST(app_range_above_lower_panel_does_not_succeed)
{
    ChunkCounts counts;
    char status = ProcessDataRecordChunks(FIRST_GAP_CHUNK, 1, &counts);

    ASSERT_TRUE(status != HEX_REC_NORMAL);
    ASSERT_EQ(status, HEX_REC_PGM_ERROR);
    /* The offending chunk must not have been silently counted as skipped --
     * that would just be the old bug with a different label. */
    ASSERT_EQ(counts.wrote, 0);
    ASSERT_EQ(counts.skipped, 0);
}

/* The property the brief requires SECOND: a boot-area/config-word record
 * still takes its existing path, completely unchanged -- silently skipped,
 * overall status still success. */
TEST(boot_area_record_still_skips_and_succeeds_unchanged)
{
    ChunkCounts counts;
    /* Four chunks spanning the bootloader's own KVA0 program-flash region --
     * a realistic multi-word boot-area record, not a single cherry-picked
     * address. */
    char status = ProcessDataRecordChunks(BOOTLOADER_KSEG0_START, 4, &counts);

    ASSERT_EQ(status, HEX_REC_NORMAL);
    ASSERT_EQ(counts.skipped, 4);
    ASSERT_EQ(counts.wrote, 0);
}

TEST(boot_flash_base_address_still_skips)
{
    ChunkCounts counts;
    char status = ProcessDataRecordChunks(BOOT_FLASH_KVA0_BASE, 1, &counts);

    ASSERT_EQ(status, HEX_REC_NORMAL);
    ASSERT_EQ(counts.skipped, 1);
}

/* Symmetry: an address below APP_FLASH_BASE_ADDRESS (the other side of the
 * original bound) is not something #1141 touches, and must still skip. */
TEST(address_below_base_still_skips)
{
    ChunkCounts counts;
    char status = ProcessDataRecordChunks((uint32_t)APP_FLASH_BASE_ADDRESS - 4u, 1, &counts);

    ASSERT_EQ(status, HEX_REC_NORMAL);
    ASSERT_EQ(counts.skipped, 1);
}

/* An address genuinely beyond the application's whole 2 MB PFM span (not
 * just beyond the lower panel) is the same class as boot flash/config words
 * and must also still skip -- the error branch is bounded on BOTH sides. */
TEST(address_beyond_whole_pfm_span_still_skips)
{
    ChunkCounts counts;
    char status = ProcessDataRecordChunks(FIRST_BEYOND_PFM, 1, &counts);

    ASSERT_EQ(status, HEX_REC_NORMAL);
    ASSERT_EQ(counts.skipped, 1);
}

/* A record entirely inside the lower (erased) panel is untouched by #1141 --
 * every chunk writes, overall status success. */
TEST(record_fully_within_lower_panel_still_succeeds)
{
    ChunkCounts counts;
    char status = ProcessDataRecordChunks((uint32_t)APP_FLASH_BASE_ADDRESS, 8, &counts);

    ASSERT_EQ(status, HEX_REC_NORMAL);
    ASSERT_EQ(counts.wrote, 8);
    ASSERT_EQ(counts.skipped, 0);
}

/* A record that STRADDLES the panel boundary -- legitimate application code
 * whose tail spills into the unerased upper panel, the realistic shape of
 * the oversized-image hazard the audit found. The chunks before the
 * boundary must still have been processed as writes; the function must
 * abort (not keep scanning) the instant it hits the first offending chunk. */
TEST(record_straddling_the_boundary_fails_at_first_offending_chunk)
{
    ChunkCounts counts;
    /* Chunks at END-12, END-8, END-4, END (all in-panel, WRITE), then
     * END+4 (first chunk in the gap, must abort there). */
    uint32_t firstAddr = LAST_LOWER_PANEL_CHUNK - 12u;
    char status = ProcessDataRecordChunks(firstAddr, 5, &counts);

    ASSERT_EQ(status, HEX_REC_PGM_ERROR);
    ASSERT_EQ(counts.wrote, 4);   /* the four in-panel chunks were processed */
    ASSERT_EQ(counts.skipped, 0);
}

/* Exact boundary values, pinned individually so a failure names which edge
 * moved rather than only that "something" changed. */
TEST(boundary_classification_is_exact)
{
    ASSERT_EQ(ClassifyProgAddress((uint32_t)APP_FLASH_END_ADDRESS), CHUNK_WRITE);
    ASSERT_EQ(ClassifyProgAddress(FIRST_GAP_CHUNK), CHUNK_ERROR);
    ASSERT_EQ(ClassifyProgAddress(LAST_GAP_CHUNK), CHUNK_ERROR);
    ASSERT_EQ(ClassifyProgAddress((uint32_t)APP_FLASH_PFM_END_ADDRESS), CHUNK_ERROR);
    ASSERT_EQ(ClassifyProgAddress(FIRST_BEYOND_PFM), CHUNK_SKIP);
    ASSERT_EQ(ClassifyProgAddress((uint32_t)APP_FLASH_BASE_ADDRESS), CHUNK_WRITE);
    ASSERT_EQ(ClassifyProgAddress((uint32_t)APP_FLASH_BASE_ADDRESS - 1u), CHUNK_SKIP);
}

/* #1141 must not have narrowed the write window itself -- the lower panel
 * boundaries are exactly what #909 shipped. A regression here would mean
 * this fix accidentally shrank the ERASED/writable window, not just added
 * an error path above it. */
TEST(lower_panel_bounds_are_unchanged_by_1141)
{
    ASSERT_EQ((uint32_t)APP_FLASH_BASE_ADDRESS, 0x9D000000u);
    ASSERT_EQ((uint32_t)APP_FLASH_END_ADDRESS, 0x9D0FFFFFu);
}

/* APP_FLASH_PFM_END_ADDRESS must equal the OLD (pre-#909) APP_FLASH_END_ADDRESS
 * value -- the full 2 MB PFM span nvm.c's own FRM comment cites. If this ever
 * drifts from 0x9D1FFFFF, every boundary test above is checking the wrong
 * span. */
TEST(pfm_end_address_matches_the_full_2mb_span)
{
    ASSERT_EQ((uint32_t)APP_FLASH_PFM_END_ADDRESS, 0x9D1FFFFFu);
}

int main(void)
{
    RUN(app_range_above_lower_panel_does_not_succeed);
    RUN(boot_area_record_still_skips_and_succeeds_unchanged);
    RUN(boot_flash_base_address_still_skips);
    RUN(address_below_base_still_skips);
    RUN(address_beyond_whole_pfm_span_still_skips);
    RUN(record_fully_within_lower_panel_still_succeeds);
    RUN(record_straddling_the_boundary_fails_at_first_offending_chunk);
    RUN(boundary_classification_is_exact);
    RUN(lower_panel_bounds_are_unchanged_by_1141);
    RUN(pfm_end_address_matches_the_full_2mb_span);
    return TEST_SUMMARY();
}
