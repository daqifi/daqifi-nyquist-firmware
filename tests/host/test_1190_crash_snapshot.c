/* test_1190_crash_snapshot.c -- the #1190 crash-record reader reports a record
 * only if its own SNAPSHOT validates.
 *
 * Audit round 1 (fw#1334): CrashRecord_Read validated the LIVE block and then
 * copied the payload, so a SYSTem:LOG:CLEar on the other transport landing
 * between the two made the reader return true with a zeroed or mixed payload
 * ("CRASH reason=0(unknown) epc=0x00000000"). The fix copies the 12 words into a
 * local snapshot under the clear's critical section and validates the copy.
 *
 * REAL code: Util/CrashRecordCore.h is included unchanged. Not reachable here:
 * the critical section itself (FreeRTOS). Instead the interleaving is modelled
 * by injecting the clear after word k of the copy, for every k, which is exactly
 * what the critical section forbids in firmware; and a source pin at the bottom
 * asserts exceptions.c still wraps the copy and the clear in taskENTER_CRITICAL
 * and decodes the snapshot, not the live block. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "test_framework.h"

#ifndef EXCEPTIONS_C
#error "the Makefile passes -DEXCEPTIONS_C=\"<path to exceptions.c>\""
#endif

#define FORCE_BOOTLOADER_FLAG_ADDR 0u
#include "CrashRecordCore.h"

#define RUNNING_ID 0xA1B2C3D4u   /* the running image's identity in these tests */
#define OLD_ID     0x0BADF00Du   /* a different (previous) image's identity */

static void seal_id(uint32_t *w, uint32_t reason, uint32_t epc, uint32_t id)
{
    memset(w, 0, CRASH_RECORD_WORDS * sizeof(uint32_t));
    w[CRASH_W_REASON]  = reason;
    w[CRASH_W_EXCCODE] = 5u;
    w[CRASH_W_EXCADDR] = epc;
    w[CRASH_W_TCB]     = 0x80001234u;
    w[CRASH_W_NAME0]   = 0x4B534154u; /* "TASK" */
    w[CRASH_W_IMAGE]   = id;
    w[CRASH_W_CHECK]   = ~(CRASH_RECORD_MAGIC ^ CrashRecord_FoldWords(w));
    w[CRASH_W_MAGIC]   = CRASH_RECORD_MAGIC;
}

/* The shipped reader's shape: copy word by word; `clear_after` words in, the
 * "other transport" wipes the live block (what the critical section prevents). */
static void seal(uint32_t *w, uint32_t reason, uint32_t epc)
{
    seal_id(w, reason, epc, RUNNING_ID);
}

static bool read_with_clear_after(uint32_t *live, int clear_after, CrashRecordView *v)
{
    uint32_t snap[CRASH_RECORD_WORDS];
    uint32_t i;
    for (i = 0; i < CRASH_RECORD_WORDS; i++) {
        if ((int)i == clear_after) {
            memset(live, 0, CRASH_RECORD_WORDS * sizeof(uint32_t));
        }
        snap[i] = live[i];
    }
    return CrashRecord_SnapshotToView(snap, RUNNING_ID, v);
}

/* The pre-fix shape, kept to prove the model can see the defect. */
static bool old_validate_live_then_copy(uint32_t *live, CrashRecordView *v)
{
    uint32_t i;
    uint32_t copy[CRASH_RECORD_WORDS];
    if (!CrashRecord_SnapshotValid(live)) return false;
    memset(live, 0, CRASH_RECORD_WORDS * sizeof(uint32_t)); /* clear lands here */
    for (i = 0; i < CRASH_RECORD_WORDS; i++) copy[i] = live[i];
    v->reason = copy[CRASH_W_REASON];
    return true; /* old code returned true here */
}

TEST(whole_record_decodes)
{
    uint32_t live[CRASH_RECORD_WORDS];
    CrashRecordView v;
    seal(live, CRASH_REASON_EXCEPTION, 0x9D001000u);
    ASSERT_TRUE(read_with_clear_after(live, -1, &v));
    ASSERT_TRUE(v.reason == CRASH_REASON_EXCEPTION);
    ASSERT_TRUE(v.excAddr == 0x9D001000u);
    ASSERT_TRUE(strcmp(v.name, "TASK") == 0);
}

TEST(clear_between_any_two_word_loads_reads_as_absent)
{
    int k;
    for (k = 0; k < (int)CRASH_RECORD_WORDS; k++) {
        uint32_t live[CRASH_RECORD_WORDS];
        CrashRecordView v;
        bool got;
        seal(live, CRASH_REASON_EXCEPTION, 0x9D001000u);
        /* A clear after word k leaves the first k words old and the rest zero:
         * a mix that must fail EVERY time, never decode as a record. */
        got = read_with_clear_after(live, k, &v);
        ASSERT_TRUE(!got);
    }
}

TEST(old_shape_fabricated_a_zero_record_and_the_model_sees_it)
{
    uint32_t live[CRASH_RECORD_WORDS];
    CrashRecordView v;
    seal(live, CRASH_REASON_EXCEPTION, 0x9D001000u);
    ASSERT_TRUE(old_validate_live_then_copy(live, &v));
    ASSERT_TRUE(v.reason == 0u); /* the "CRASH reason=0(unknown)" of the audit */
}

TEST(cleared_block_is_absent)
{
    uint32_t z[CRASH_RECORD_WORDS] = {0};
    CrashRecordView v;
    ASSERT_TRUE(!CrashRecord_SnapshotToView(z, RUNNING_ID, &v));
}

TEST(torn_payload_or_check_is_absent)
{
    uint32_t live[CRASH_RECORD_WORDS];
    CrashRecordView v;
    seal(live, CRASH_REASON_OVERFLOW, 0u);
    live[CRASH_W_EXCCODE] ^= 1u;                 /* payload word torn */
    ASSERT_TRUE(!CrashRecord_SnapshotToView(live, RUNNING_ID, &v));
    seal(live, CRASH_REASON_OVERFLOW, 0u);
    live[CRASH_W_CHECK] ^= 1u;                   /* check word torn */
    ASSERT_TRUE(!CrashRecord_SnapshotToView(live, RUNNING_ID, &v));
    seal(live, 9u, 0u);                          /* unknown reason */
    ASSERT_TRUE(!CrashRecord_SnapshotToView(live, RUNNING_ID, &v));
}

/* ---- #1190 round 3: image identity ---------------------------------------- */

/* The decoder under test, as a function pointer so a MUTANT can stand in: the
 * mutant IGNORES identity (treats every record as written by the running
 * image), which is exactly the defect round 3 closes. */
typedef bool (*decode_fn)(const uint32_t *w, CrashRecordView *v);

static bool decode_real(const uint32_t *w, CrashRecordView *v)
{
    return CrashRecord_SnapshotToView(w, RUNNING_ID, v);
}

static bool decode_ignoring_identity(const uint32_t *w, CrashRecordView *v)
{
    return CrashRecord_SnapshotToView(w, w[CRASH_W_IMAGE], v); /* "always current" */
}

/* The row a mismatched identity must satisfy; returns whether the decoder passes. */
static bool mismatch_row_passes(decode_fn dec)
{
    uint32_t w[CRASH_RECORD_WORDS];
    CrashRecordView v;
    char line[CRASH_LINE_MAX];
    seal_id(w, CRASH_REASON_EXCEPTION, 0x9D001000u, OLD_ID);
    if (!dec(w, &v)) return false;
    if (v.current) return false;
    if (v.imageId != OLD_ID) return false;
    (void)CrashRecord_FormatLine(line, sizeof(line), &v);
    return strstr(line, "epc(prev)=0x9D001000") != NULL
        && strstr(line, "image=previous id=0BADF00D") != NULL
        && strstr(line, " epc=0x") == NULL;
}

TEST(matching_identity_decodes_as_current)
{
    uint32_t w[CRASH_RECORD_WORDS];
    CrashRecordView v;
    char line[CRASH_LINE_MAX];
    seal_id(w, CRASH_REASON_EXCEPTION, 0x9D001000u, RUNNING_ID);
    ASSERT_TRUE(CrashRecord_SnapshotToView(w, RUNNING_ID, &v));
    ASSERT_TRUE(v.current);
    ASSERT_TRUE(v.imageId == RUNNING_ID);
    (void)CrashRecord_FormatLine(line, sizeof(line), &v);
    ASSERT_TRUE(strcmp(line, "CRASH reason=2(general-exception) exccode=5 "
        "epc=0x9D001000 tcb=0x80001234 task=TASK image=current id=A1B2C3D4\r\n") == 0);
}

TEST(mismatched_identity_decodes_as_previous_and_keeps_the_old_id)
{
    ASSERT_TRUE(mismatch_row_passes(decode_real));
}

TEST(unknown_identity_zero_is_never_current)
{
    uint32_t w[CRASH_RECORD_WORDS];
    CrashRecordView v;
    seal_id(w, CRASH_REASON_MALLOC, 0u, 0u);       /* capture before main published */
    ASSERT_TRUE(CrashRecord_SnapshotToView(w, RUNNING_ID, &v));
    ASSERT_TRUE(!v.current);
    seal_id(w, CRASH_REASON_MALLOC, 0u, 0u);       /* and a running image of 0 (unpublished) */
    ASSERT_TRUE(CrashRecord_SnapshotToView(w, 0u, &v));
    ASSERT_TRUE(!v.current);
}

TEST(corrupted_identity_word_fails_the_check_and_reads_as_absent)
{
    uint32_t w[CRASH_RECORD_WORDS];
    CrashRecordView v;
    int bit;
    seal_id(w, CRASH_REASON_EXCEPTION, 0x9D001000u, RUNNING_ID);
    for (bit = 0; bit < 32; bit++) {
        uint32_t t[CRASH_RECORD_WORDS];
        memcpy(t, w, sizeof(t));
        t[CRASH_W_IMAGE] ^= (1u << bit);           /* the identity word is covered */
        ASSERT_TRUE(!CrashRecord_SnapshotToView(t, RUNNING_ID, &v));
        ASSERT_TRUE(!CrashRecord_SnapshotToView(t, t[CRASH_W_IMAGE], &v));
    }
}

TEST(mutant_that_ignores_identity_turns_the_mismatch_row_red)
{
    ASSERT_TRUE(mismatch_row_passes(decode_real));               /* green */
    ASSERT_TRUE(!mismatch_row_passes(decode_ignoring_identity)); /* mutant: RED */
}

TEST(worst_case_line_fits_the_scpi_buffer)
{
    CrashRecordView v;
    char line[CRASH_LINE_MAX];
    size_t n;
    memset(&v, 0, sizeof(v));
    v.reason = CRASH_REASON_OVERFLOW; v.excCode = 0xFFFFFFFFu;
    v.excAddr = 0xFFFFFFFFu; v.tcb = 0xFFFFFFFFu; v.imageId = 0xFFFFFFFFu;
    memset(v.name, 'N', CRASH_TASK_NAME_MAX);
    n = CrashRecord_FormatLine(line, sizeof(line), &v);
    ASSERT_TRUE(n < CRASH_LINE_MAX - 1u);
    ASSERT_TRUE(line[n - 1u] == '\n');             /* not truncated */
}


/* ---- source pin: the firmware reader/clear keep the shape modelled above ---- */
static char *slurp(void)
{
    FILE *f = fopen(EXCEPTIONS_C, "rb");
    long n;
    char *b;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    b = (char *)malloc((size_t)n + 1);
    if (b && fread(b, 1, (size_t)n, f) == (size_t)n) { b[n] = '\0'; }
    fclose(f);
    return b;
}

/* Text of the function whose definition starts with `sig`, up to its closing
 * brace at column 0 (CRLF-tolerant: the source is CRLF). */
static char *body(char *src, const char *sig)
{
    char *p = strstr(src, sig);
    char *e;
    if (!p) return NULL;
    e = strstr(p, "\n}");
    if (!e) return NULL;
    *e = '\0';
    return p;
}

TEST(firmware_reader_and_clear_share_a_critical_section)
{
    char *src = slurp();
    char *rd, *cl;
    ASSERT_TRUE(src != NULL);
    if (!src) return;
    rd = body(src, "bool CrashRecord_Read( CrashRecordView * out )");
    ASSERT_TRUE(rd != NULL);
    if (!rd) return;
    {
        char *enter = strstr(rd, "taskENTER_CRITICAL()");
        char *copy  = enter ? strstr(enter, "CrashRecord_Copy(") : NULL;
        char *exitp = copy ? strstr(copy, "taskEXIT_CRITICAL()") : NULL;
        char *dec   = exitp ? strstr(exitp, "CrashRecord_SnapshotToView( snap") : NULL;
        ASSERT_TRUE(enter && copy && exitp && dec);
        ASSERT_TRUE(strstr(rd, "sCrashRecord[") == NULL); /* never the live block */
    }
    free(src);
    src = slurp();
    ASSERT_TRUE(src != NULL);
    if (!src) return;
    cl = body(src, "void CrashRecord_Clear( void )");
    ASSERT_TRUE(cl != NULL);
    if (!cl) return;
    {
        char *enter = strstr(cl, "taskENTER_CRITICAL()");
        char *clr   = enter ? strstr(enter, "CrashRecord_ClearUnlocked()") : NULL;
        char *exitp = clr ? strstr(clr, "taskEXIT_CRITICAL()") : NULL;
        ASSERT_TRUE(enter && clr && exitp);
    }
    free(src);
}

TEST(firmware_seals_the_running_identity_into_word_9)
{
    char *src = slurp();
    char *sl;
    ASSERT_TRUE(src != NULL);
    if (!src) return;
    sl = body(src, "void CrashRecord_Seal( void )");
    ASSERT_TRUE(sl != NULL);
    if (!sl) return;
    {
        char *img = strstr(sl, "sCrashRecord[CRASH_W_IMAGE] = sCrashImageId");
        char *chk = strstr(sl, "sCrashRecord[CRASH_W_CHECK] =");
        ASSERT_TRUE(img && chk && img < chk);   /* identity stored BEFORE the fold */
    }
    free(src);
}
int main(void)
{
    RUN(whole_record_decodes);
    RUN(clear_between_any_two_word_loads_reads_as_absent);
    RUN(old_shape_fabricated_a_zero_record_and_the_model_sees_it);
    RUN(cleared_block_is_absent);
    RUN(torn_payload_or_check_is_absent);
    RUN(matching_identity_decodes_as_current);
    RUN(mismatched_identity_decodes_as_previous_and_keeps_the_old_id);
    RUN(unknown_identity_zero_is_never_current);
    RUN(corrupted_identity_word_fails_the_check_and_reads_as_absent);
    RUN(mutant_that_ignores_identity_turns_the_mismatch_row_red);
    RUN(worst_case_line_fits_the_scpi_buffer);
    RUN(firmware_seals_the_running_identity_into_word_9);
    RUN(firmware_reader_and_clear_share_a_critical_section);
    return TEST_SUMMARY();
}
