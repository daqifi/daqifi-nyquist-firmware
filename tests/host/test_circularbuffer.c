/* ==========================================================================
 * test_circularbuffer.c — PC host unit tests for firmware/src/Util/CircularBuffer.c
 *
 * Issue #124. Runs with plain gcc, no firmware/RTOS deps (osal + Logger are
 * stubbed under stubs/). Covers init / AddBytes / accounting / wrap-around /
 * ProcessBytes callback semantics (#126) / Reset / InitExternal / Resize /
 * SPSC counter wraparound near UINT32_MAX.
 *
 * White-box: tests read and (for the counter-wrap cases) directly seed struct
 * fields, so they include the real CircularBuffer.h layout.
 * ========================================================================== */
#include <stdint.h>
#include <string.h>

#include "CircularBuffer.h"   /* real header (via -I firmware/src/Util) */
#include "test_framework.h"

/* -------------------------------------------------------------------------
 * Callback plumbing for ProcessBytes(bytesBuf == NULL) tests.
 * The callback records the buffer/length it was handed, and returns a value
 * controlled per-test via g_cb_ret (CB_RET_FULL => "consumed everything").
 * ------------------------------------------------------------------------- */
#define CB_RET_FULL 0x7FFFFFFF

static uint8_t  g_cb_last[64];
static uint32_t g_cb_last_len;
static int      g_cb_calls;
static int      g_cb_ret;      /* value the callback returns (or CB_RET_FULL) */

static void cb_reset(int ret)
{
    memset(g_cb_last, 0, sizeof(g_cb_last));
    g_cb_last_len = 0;
    g_cb_calls    = 0;
    g_cb_ret      = ret;
}

static int cb_record(uint8_t *p, uint32_t n)
{
    g_cb_calls++;
    if (n <= sizeof(g_cb_last)) {
        memcpy(g_cb_last, p, n);
    }
    g_cb_last_len = n;
    return (g_cb_ret == CB_RET_FULL) ? (int)n : g_cb_ret;
}

/* Drives the buffer (size 8) into a known wrapped physical state:
 *   physical bytes : [I J K D E F G H]
 *   removePtr      : index 5 ('F'), Available = 6, insertPtr = index 3
 * Uses copy-mode ProcessBytes so it works regardless of the registered cb. */
static void fill_wrapped_state(CircularBuf_t *cb)
{
    uint8_t out[8];
    int err = 0;
    CircularBuf_AddBytes(cb, (uint8_t *)"ABCDEFGH", 8); /* fill */
    CircularBuf_ProcessBytes(cb, out, 5, &err);          /* consume ABCDE */
    CircularBuf_AddBytes(cb, (uint8_t *)"IJK", 3);        /* wrap insert */
}

/* ========================================================================= */
/* Init / accounting                                                         */
/* ========================================================================= */

TEST(test_init_basic)
{
    CircularBuf_t cb;
    CircularBuf_Init(&cb, NULL, 16);

    ASSERT_TRUE(cb.buf_ptr != NULL);
    ASSERT_EQ(cb.buf_size, 16);
    ASSERT_TRUE(cb._ownsMemory);
    ASSERT_EQ(CircularBuf_NumBytesAvailable(&cb), 0);
    ASSERT_EQ(CircularBuf_NumBytesFree(&cb), 16);
    ASSERT_TRUE(cb.insertPtr == cb.buf_ptr);
    ASSERT_TRUE(cb.removePtr == cb.buf_ptr);

    CircularBuf_Deinit(&cb);
    ASSERT_TRUE(cb.buf_ptr == NULL);
    ASSERT_EQ(cb.buf_size, 0);
    ASSERT_FALSE(cb._ownsMemory);
}

TEST(test_addbytes_accounting)
{
    CircularBuf_t cb;
    CircularBuf_Init(&cb, NULL, 16);

    uint32_t added = CircularBuf_AddBytes(&cb, (uint8_t *)"HELLO", 5);
    ASSERT_EQ(added, 5);
    ASSERT_EQ(CircularBuf_NumBytesAvailable(&cb), 5);
    ASSERT_EQ(CircularBuf_NumBytesFree(&cb), 11);
    ASSERT_EQ(cb.producedBytes, 5);
    ASSERT_EQ(cb.consumedBytes, 0);

    CircularBuf_Deinit(&cb);
}

TEST(test_add_then_process_copy_roundtrip)
{
    CircularBuf_t cb;
    CircularBuf_Init(&cb, NULL, 16);

    CircularBuf_AddBytes(&cb, (uint8_t *)"HELLO", 5);

    uint8_t out[16] = {0};
    int err = 123;
    uint32_t removed = CircularBuf_ProcessBytes(&cb, out, 16, &err);

    ASSERT_EQ(removed, 5);
    ASSERT_EQ(err, 0);                       /* copy-mode leaves error at 0 */
    ASSERT_BYTES(out, "HELLO", 5);
    ASSERT_EQ(CircularBuf_NumBytesAvailable(&cb), 0);
    ASSERT_EQ(CircularBuf_NumBytesFree(&cb), 16);

    CircularBuf_Deinit(&cb);
}

TEST(test_add_fills_exactly_full)
{
    CircularBuf_t cb;
    CircularBuf_Init(&cb, NULL, 8);

    uint32_t added = CircularBuf_AddBytes(&cb, (uint8_t *)"ABCDEFGH", 8);
    ASSERT_EQ(added, 8);
    ASSERT_EQ(CircularBuf_NumBytesAvailable(&cb), 8);
    ASSERT_EQ(CircularBuf_NumBytesFree(&cb), 0);

    CircularBuf_Deinit(&cb);
}

TEST(test_add_rejects_when_insufficient_free)
{
    CircularBuf_t cb;
    CircularBuf_Init(&cb, NULL, 8);

    ASSERT_EQ(CircularBuf_AddBytes(&cb, (uint8_t *)"AAAAA", 5), 5);
    /* Only 3 free; a 5-byte add is all-or-nothing => rejected, returns 0. */
    ASSERT_EQ(CircularBuf_AddBytes(&cb, (uint8_t *)"BBBBB", 5), 0);
    ASSERT_EQ(CircularBuf_NumBytesAvailable(&cb), 5);

    CircularBuf_Deinit(&cb);
}

/* ========================================================================= */
/* Wrap-around                                                               */
/* ========================================================================= */

TEST(test_wraparound_copy_single_call)
{
    CircularBuf_t cb;
    CircularBuf_Init(&cb, NULL, 8);
    fill_wrapped_state(&cb);
    ASSERT_EQ(CircularBuf_NumBytesAvailable(&cb), 6);

    uint8_t out[8] = {0};
    int err = 0;
    /* Copy-mode stitches both chunks in one call. */
    uint32_t removed = CircularBuf_ProcessBytes(&cb, out, 6, &err);
    ASSERT_EQ(removed, 6);
    ASSERT_BYTES(out, "FGHIJK", 6);
    ASSERT_EQ(CircularBuf_NumBytesAvailable(&cb), 0);

    CircularBuf_Deinit(&cb);
}

TEST(test_wraparound_callback_first_chunk_only)
{
    CircularBuf_t cb;
    CircularBuf_Init(&cb, cb_record, 8);
    fill_wrapped_state(&cb);
    ASSERT_EQ(CircularBuf_NumBytesAvailable(&cb), 6);

    /* Callback-mode deliberately hands only the pre-wrap chunk (comment in
     * CircularBuf_ProcessBytes: calling the callback twice corrupts shared
     * UsbCdc buffers). First call => "FGH" (3 bytes to the physical end). */
    int err = 0;
    cb_reset(CB_RET_FULL);
    uint32_t removed = CircularBuf_ProcessBytes(&cb, NULL, 6, &err);
    ASSERT_EQ(removed, 3);
    ASSERT_EQ(g_cb_calls, 1);
    ASSERT_EQ(g_cb_last_len, 3);
    ASSERT_BYTES(g_cb_last, "FGH", 3);
    ASSERT_EQ(err, 3);
    ASSERT_EQ(CircularBuf_NumBytesAvailable(&cb), 3);

    /* Second call drains the wrapped remainder "IJK". */
    cb_reset(CB_RET_FULL);
    removed = CircularBuf_ProcessBytes(&cb, NULL, 6, &err);
    ASSERT_EQ(removed, 3);
    ASSERT_EQ(g_cb_calls, 1);
    ASSERT_BYTES(g_cb_last, "IJK", 3);
    ASSERT_EQ(CircularBuf_NumBytesAvailable(&cb), 0);

    CircularBuf_Deinit(&cb);
}

/* ========================================================================= */
/* ProcessBytes callback semantics (#126)                                    */
/* ========================================================================= */

TEST(test_callback_full_consume)
{
    CircularBuf_t cb;
    CircularBuf_Init(&cb, cb_record, 32);
    CircularBuf_AddBytes(&cb, (uint8_t *)"0123456789", 10);

    int err = 0;
    cb_reset(CB_RET_FULL);                 /* return n => consume everything */
    uint32_t removed = CircularBuf_ProcessBytes(&cb, NULL, 10, &err);

    ASSERT_EQ(removed, 10);
    ASSERT_EQ(g_cb_last_len, 10);
    ASSERT_BYTES(g_cb_last, "0123456789", 10);
    ASSERT_EQ(err, 10);
    ASSERT_EQ(CircularBuf_NumBytesAvailable(&cb), 0);

    CircularBuf_Deinit(&cb);
}

TEST(test_callback_partial_consume)
{
    CircularBuf_t cb;
    CircularBuf_Init(&cb, cb_record, 32);
    CircularBuf_AddBytes(&cb, (uint8_t *)"0123456789", 10);

    int err = 0;
    cb_reset(3);                           /* callback processed only 3 of 10 */
    uint32_t removed = CircularBuf_ProcessBytes(&cb, NULL, 10, &err);

    /* #126: ring advances by the callback's returned count only. */
    ASSERT_EQ(removed, 3);
    ASSERT_EQ(err, 3);
    ASSERT_EQ(CircularBuf_NumBytesAvailable(&cb), 7);   /* 7 remain */

    /* The remaining 7 are still there and start at '3'. */
    cb_reset(CB_RET_FULL);
    removed = CircularBuf_ProcessBytes(&cb, NULL, 10, &err);
    ASSERT_EQ(removed, 7);
    ASSERT_BYTES(g_cb_last, "3456789", 7);
    ASSERT_EQ(CircularBuf_NumBytesAvailable(&cb), 0);

    CircularBuf_Deinit(&cb);
}

TEST(test_callback_negative_consumes_nothing)
{
    CircularBuf_t cb;
    CircularBuf_Init(&cb, cb_record, 32);
    CircularBuf_AddBytes(&cb, (uint8_t *)"0123456789", 10);

    int err = 0;
    cb_reset(-5);                          /* negative => error, consume none */
    uint32_t removed = CircularBuf_ProcessBytes(&cb, NULL, 10, &err);

    /* #126: negative callback return consumes nothing; error propagates. */
    ASSERT_EQ(removed, 0);
    ASSERT_EQ(err, -5);
    ASSERT_EQ(CircularBuf_NumBytesAvailable(&cb), 10);  /* untouched */
    ASSERT_EQ(cb.consumedBytes, 0);

    CircularBuf_Deinit(&cb);
}

TEST(test_callback_overconsume_clamped)
{
    CircularBuf_t cb;
    CircularBuf_Init(&cb, cb_record, 32);
    CircularBuf_AddBytes(&cb, (uint8_t *)"0123456789", 10);

    int err = 0;
    cb_reset(999);                         /* buggy callback over-reports */
    uint32_t removed = CircularBuf_ProcessBytes(&cb, NULL, 10, &err);

    /* Clamped to the bytes actually offered (10) — no pointer overrun. */
    ASSERT_EQ(removed, 10);
    ASSERT_EQ(CircularBuf_NumBytesAvailable(&cb), 0);
    ASSERT_EQ(cb.consumedBytes, 10);

    CircularBuf_Deinit(&cb);
}

TEST(test_process_empty_and_zero_max)
{
    CircularBuf_t cb;
    CircularBuf_Init(&cb, cb_record, 16);

    int err = 55;
    /* Empty buffer => nothing to do, callback never fires. */
    cb_reset(CB_RET_FULL);
    ASSERT_EQ(CircularBuf_ProcessBytes(&cb, NULL, 16, &err), 0);
    ASSERT_EQ(g_cb_calls, 0);

    /* maxBytes == 0 => nothing removed even with data present. */
    CircularBuf_AddBytes(&cb, (uint8_t *)"XY", 2);
    cb_reset(CB_RET_FULL);
    ASSERT_EQ(CircularBuf_ProcessBytes(&cb, NULL, 0, &err), 0);
    ASSERT_EQ(g_cb_calls, 0);
    ASSERT_EQ(CircularBuf_NumBytesAvailable(&cb), 2);

    CircularBuf_Deinit(&cb);
}

/* ========================================================================= */
/* Reset / InitExternal / Resize                                             */
/* ========================================================================= */

TEST(test_reset)
{
    CircularBuf_t cb;
    CircularBuf_Init(&cb, NULL, 16);
    CircularBuf_AddBytes(&cb, (uint8_t *)"SOMEDATA", 8);
    ASSERT_EQ(CircularBuf_NumBytesAvailable(&cb), 8);

    CircularBuf_Reset(&cb);
    ASSERT_EQ(CircularBuf_NumBytesAvailable(&cb), 0);
    ASSERT_EQ(CircularBuf_NumBytesFree(&cb), 16);
    ASSERT_EQ(cb.producedBytes, 0);
    ASSERT_EQ(cb.consumedBytes, 0);
    ASSERT_TRUE(cb.insertPtr == cb.buf_ptr);
    ASSERT_TRUE(cb.removePtr == cb.buf_ptr);
    ASSERT_TRUE(cb.buf_ptr != NULL);       /* Reset keeps the allocation */

    CircularBuf_Deinit(&cb);
}

TEST(test_init_external)
{
    uint8_t backing[16];
    CircularBuf_t cb;
    CircularBuf_InitExternal(&cb, NULL, backing, sizeof(backing));

    ASSERT_TRUE(cb.buf_ptr == backing);
    ASSERT_FALSE(cb._ownsMemory);
    ASSERT_EQ(cb.buf_size, 16);

    /* External buffer still functions for add/process. */
    CircularBuf_AddBytes(&cb, (uint8_t *)"EXT", 3);
    uint8_t out[4] = {0};
    int err = 0;
    ASSERT_EQ(CircularBuf_ProcessBytes(&cb, out, 4, &err), 3);
    ASSERT_BYTES(out, "EXT", 3);

    /* Deinit must NOT free borrowed memory (backing is a stack array). */
    CircularBuf_Deinit(&cb);
    ASSERT_TRUE(cb.buf_ptr == NULL);       /* pointer cleared, memory not freed */
}

TEST(test_init_external_null_buffer)
{
    CircularBuf_t cb;
    CircularBuf_InitExternal(&cb, NULL, NULL, 0);
    ASSERT_TRUE(cb.buf_ptr == NULL);
    ASSERT_EQ(cb.buf_size, 0);
    ASSERT_FALSE(cb._ownsMemory);
    ASSERT_EQ(CircularBuf_NumBytesAvailable(&cb), 0);
}

TEST(test_resize_grow_owned)
{
    CircularBuf_t cb;
    CircularBuf_Init(&cb, NULL, 8);
    CircularBuf_AddBytes(&cb, (uint8_t *)"XYZ", 3);

    ASSERT_TRUE(CircularBuf_Resize(&cb, 16));
    ASSERT_EQ(cb.buf_size, 16);
    /* Resize resets accounting (documented: data is discarded). */
    ASSERT_EQ(CircularBuf_NumBytesAvailable(&cb), 0);
    ASSERT_EQ(CircularBuf_NumBytesFree(&cb), 16);
    ASSERT_TRUE(cb.insertPtr == cb.buf_ptr);

    /* Same-size resize is a successful no-op. */
    ASSERT_TRUE(CircularBuf_Resize(&cb, 16));
    ASSERT_EQ(cb.buf_size, 16);

    /* Zero size rejected. */
    ASSERT_FALSE(CircularBuf_Resize(&cb, 0));
    ASSERT_EQ(cb.buf_size, 16);

    CircularBuf_Deinit(&cb);
}

TEST(test_resize_external_fails)
{
    uint8_t backing[8];
    CircularBuf_t cb;
    CircularBuf_InitExternal(&cb, NULL, backing, sizeof(backing));

    /* Cannot resize a borrowed buffer. */
    ASSERT_FALSE(CircularBuf_Resize(&cb, 16));
    ASSERT_EQ(cb.buf_size, 8);
    ASSERT_TRUE(cb.buf_ptr == backing);

    CircularBuf_Deinit(&cb);
}

/* ========================================================================= */
/* SPSC counter wraparound (near UINT32_MAX)                                 */
/* ========================================================================= */

TEST(test_spsc_counter_wrap_math)
{
    CircularBuf_t cb;
    CircularBuf_Init(&cb, NULL, 16);

    /* Available = producedBytes - consumedBytes as unsigned modular math.
     * Seed the counters directly (white-box) to straddle the 2^32 boundary. */
    cb.producedBytes = 0xFFFFFFF0u;
    cb.consumedBytes = 0xFFFFFFE0u;
    ASSERT_EQ(CircularBuf_NumBytesAvailable(&cb), 16);   /* 0xF0 - 0xE0 */

    /* Producer has wrapped past the ceiling; consumer has not. */
    cb.producedBytes = 0x00000005u;
    cb.consumedBytes = 0xFFFFFFF5u;
    ASSERT_EQ(CircularBuf_NumBytesAvailable(&cb), 16);   /* 5 - (-11) mod 2^32 */

    /* Both counters equal (any value) => empty. */
    cb.producedBytes = 0xFFFFFFFFu;
    cb.consumedBytes = 0xFFFFFFFFu;
    ASSERT_EQ(CircularBuf_NumBytesAvailable(&cb), 0);

    CircularBuf_Deinit(&cb);
}

TEST(test_counter_wrap_through_real_api)
{
    CircularBuf_t cb;
    CircularBuf_Init(&cb, NULL, 8);

    /* Park both counters just below the 2^32 ceiling with the buffer empty. */
    cb.producedBytes = 0xFFFFFFFEu;
    cb.consumedBytes = 0xFFFFFFFEu;
    ASSERT_EQ(CircularBuf_NumBytesAvailable(&cb), 0);
    ASSERT_EQ(CircularBuf_NumBytesFree(&cb), 8);

    /* AddBytes drives producedBytes across the wrap (0xFFFFFFFE + 4 -> 0x2). */
    ASSERT_EQ(CircularBuf_AddBytes(&cb, (uint8_t *)"WXYZ", 4), 4);
    ASSERT_EQ(cb.producedBytes, 0x00000002u);            /* wrapped */
    ASSERT_EQ(CircularBuf_NumBytesAvailable(&cb), 4);    /* 0x2 - 0xFFFFFFFE */
    ASSERT_EQ(CircularBuf_NumBytesFree(&cb), 4);

    /* And consume it back out — data must survive the counter wrap intact. */
    uint8_t out[4] = {0};
    int err = 0;
    ASSERT_EQ(CircularBuf_ProcessBytes(&cb, out, 4, &err), 4);
    ASSERT_BYTES(out, "WXYZ", 4);
    ASSERT_EQ(cb.consumedBytes, 0x00000002u);            /* wrapped */
    ASSERT_EQ(CircularBuf_NumBytesAvailable(&cb), 0);

    CircularBuf_Deinit(&cb);
}

/* ========================================================================= */
/* NULL safety                                                               */
/* ========================================================================= */

TEST(test_null_safety)
{
    int err = 0;
    uint8_t buf[4] = {0};

    /* None of these may crash; all return the documented safe defaults. */
    CircularBuf_Init(NULL, NULL, 16);
    CircularBuf_InitExternal(NULL, NULL, buf, 4);
    CircularBuf_Reset(NULL);
    CircularBuf_Deinit(NULL);
    ASSERT_FALSE(CircularBuf_Resize(NULL, 16));
    ASSERT_EQ(CircularBuf_AddBytes(NULL, buf, 4), 0);
    ASSERT_EQ(CircularBuf_NumBytesAvailable(NULL), 0);
    ASSERT_EQ(CircularBuf_NumBytesFree(NULL), 0);
    ASSERT_EQ(CircularBuf_ProcessBytes(NULL, buf, 4, &err), 0);

    /* AddBytes with NULL data pointer is also rejected. */
    CircularBuf_t cb;
    CircularBuf_Init(&cb, NULL, 16);
    ASSERT_EQ(CircularBuf_AddBytes(&cb, NULL, 4), 0);
    /* ProcessBytes with NULL error pointer is rejected (returns 0). */
    ASSERT_EQ(CircularBuf_ProcessBytes(&cb, buf, 4, NULL), 0);
    CircularBuf_Deinit(&cb);
}

/* ========================================================================= */

int main(void)
{
    printf("CircularBuffer host unit tests (issue #124)\n");
    printf("=============================================\n");

    RUN(test_init_basic);
    RUN(test_addbytes_accounting);
    RUN(test_add_then_process_copy_roundtrip);
    RUN(test_add_fills_exactly_full);
    RUN(test_add_rejects_when_insufficient_free);

    RUN(test_wraparound_copy_single_call);
    RUN(test_wraparound_callback_first_chunk_only);

    RUN(test_callback_full_consume);
    RUN(test_callback_partial_consume);
    RUN(test_callback_negative_consumes_nothing);
    RUN(test_callback_overconsume_clamped);
    RUN(test_process_empty_and_zero_max);

    RUN(test_reset);
    RUN(test_init_external);
    RUN(test_init_external_null_buffer);
    RUN(test_resize_grow_owned);
    RUN(test_resize_external_fails);

    RUN(test_spsc_counter_wrap_math);
    RUN(test_counter_wrap_through_real_api);

    RUN(test_null_safety);

    return TEST_SUMMARY();
}
