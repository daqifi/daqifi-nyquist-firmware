/*
 * #881: a WINC message the host cannot read or parse must still be
 * acknowledged (RX done, WIFI_HOST_RCV_CTRL_0 bit 1), or the WINC raises no
 * further interrupt and grants no buffer -- WiFi stays dead until a reset.
 *
 * Includes the REAL m2m_hif.c and runs it against a fake WINC register file. Each test
 * drives hif_handle_isr() through one of hif_isr()'s error exits and checks
 * that RX done was written. On the upstream source every error-exit test
 * fails: the exits return with RX done unwritten and u8HifRXDone stuck at 1,
 * which is the state captured on hardware at the wedge
 * ("WDIAG ... rc=-3 pin=1 ... rx=1", issue #881, 2026-11-13).
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* One translation unit: the register addresses are private to m2m_hif.c. */
#include "m2m_hif.c"
#ifdef TEST881_OLD_SOURCE   /* to run this file against pre-r4 sources */
uint32_t hif_rx_reset_requests(void) { return 0; }
#endif
#include "test_framework.h"

/* ---- fake WINC ------------------------------------------------------- */
#define RX_ADDR 0x1000u

unsigned gTest881LogE;
static uint32_t gCtrl0;             /* WIFI_HOST_RCV_CTRL_0 as the host sees it */
static unsigned gRxDoneWrites;      /* writes of CTRL_0 with bit 1 set */
static uint8_t gHdr[4];             /* tstrHifHdr at RX_ADDR: gid, op, len LE */
static int gFailCtrl1Read, gFailHdrRead, gFailRxDoneWrites, gFailCtrl0Reads;
static unsigned gIpCalls;

/* A write the WINC APPLIES whose SPI response is then lost, so the host is
 * told it failed (codex audit r1 on #1259). */
static int gApplyLostRxDone;
/* When set, an applied RX done stays visible (bit 1) until wincConsume(). */
static int gHoldAck;
static int gAckPending;
/* Messages the WINC posts after the current one is acknowledged. */
struct msg { uint16_t size; uint8_t gid, op; uint16_t len; };
static struct msg gQueue[4];
static unsigned gQueued;
/* When set, the IP callback acknowledges through hif_receive(..., isDone=1),
 * as the socket layer does. */
static int gCbAcks;
/* When nonzero, a freed message's successor is not posted on consumption but
 * at this CTRL_0 read (counted from post()), to place a post between two
 * register accesses of the host. */
static unsigned gPostOnRead, gCtrl0Reads;
/* When nonzero, the successor is posted just AFTER this CTRL_0 read returns:
 * between the host's read and its next register access. */
static unsigned gPostAfterRead;
static void postNext(void);
/* hif_rx_reset_requests() is a monotonic count; tests read it per test. */
static uint32_t gResetBase;
static uint32_t resets(void);

static void deliver(const struct msg *m)
{
    gCtrl0 = ((uint32_t)m->size << 2) | NBIT0;
    gHdr[0] = m->gid; gHdr[1] = m->op;
    gHdr[2] = (uint8_t)m->len; gHdr[3] = (uint8_t)(m->len >> 8);
}

static void postNext(void)
{
    if (gQueued && !gAckPending && !(gCtrl0 & NBIT0)) {
        deliver(&gQueue[0]);
        memmove(gQueue, gQueue + 1, --gQueued * sizeof gQueue[0]);
    }
}

/* The WINC consumes RX done: frees the message it has posted -- read or not:
 * an RX done written while a new message's bit 0 is set releases THAT message
 * unread -- and posts the next, if any. */
static void wincConsume(void)
{
    gAckPending = 0;
    gCtrl0 &= ~(NBIT1 | NBIT0);
    if (gQueued && !gPostOnRead && !gPostAfterRead) {
        deliver(&gQueue[0]);
        memmove(gQueue, gQueue + 1, --gQueued * sizeof gQueue[0]);
    }
}

int8_t nm_read_reg_with_ret(uint32_t addr, uint32_t *val)
{
    if (addr == WIFI_HOST_RCV_CTRL_0) {
        if (gFailCtrl0Reads) { gFailCtrl0Reads--; return M2M_ERR_BUS_FAIL; }
        if (++gCtrl0Reads == gPostOnRead && gQueued && !gAckPending && !(gCtrl0 & NBIT0)) {
            deliver(&gQueue[0]);
            memmove(gQueue, gQueue + 1, --gQueued * sizeof gQueue[0]);
        }
        *val = gCtrl0;
        if (gCtrl0Reads == gPostAfterRead) postNext();
        return M2M_SUCCESS;
    }
    if (addr == WIFI_HOST_RCV_CTRL_1) {
        if (gFailCtrl1Read) { gFailCtrl1Read--; return M2M_ERR_BUS_FAIL; }
        *val = RX_ADDR;
        return M2M_SUCCESS;
    }
    *val = 0;
    return M2M_SUCCESS;
}

int8_t nm_write_reg(uint32_t addr, uint32_t val)
{
    if (addr == WIFI_HOST_RCV_CTRL_0) {
        if (val & NBIT1) {
            int lost;
            if (gFailRxDoneWrites) { gFailRxDoneWrites--; return M2M_ERR_BUS_FAIL; }
            lost = gApplyLostRxDone > 0;
            if (lost) gApplyLostRxDone--;
            gRxDoneWrites++;        /* counts RX done writes that LANDED */
            gCtrl0 = val;
            gAckPending = 1;
            if (!gHoldAck) wincConsume();
            return lost ? M2M_ERR_BUS_FAIL : M2M_SUCCESS;
        }
        gCtrl0 = val;
    }
    return M2M_SUCCESS;
}

int8_t nm_read_block(uint32_t addr, uint8_t *buf, uint32_t sz)
{
    if (gFailHdrRead) { gFailHdrRead--; return M2M_ERR_BUS_FAIL; }
    if (addr == RX_ADDR && sz <= sizeof gHdr) { memcpy(buf, gHdr, sz); return M2M_SUCCESS; }
    memset(buf, 0, sz);
    return M2M_SUCCESS;
}

int8_t nm_write_block(uint32_t a, uint8_t *b, uint32_t s) { (void)a; (void)b; (void)s; return M2M_SUCCESS; }
void nm_sleep(uint32_t ms) { (void)ms; }
int8_t chip_wake(void) { return M2M_SUCCESS; }
int8_t chip_sleep(void) { return M2M_SUCCESS; }
WDRV_WINC_DEBUG_PRINT_CALLBACK pfWINCDebugPrintCb;

static void ip_cb(uint8_t op, uint16_t sz, uint32_t addr)
{
    uint8_t b[1];
    (void)op; (void)sz;
    gIpCalls++;
    if (gCbAcks) (void)hif_receive(addr, b, 1, 1);
}

/* The WINC posts a message of `size` bytes whose header reads gid/op/len. */
static void post(uint16_t size, uint8_t gid, uint8_t op, uint16_t len)
{
    struct msg m = { size, gid, op, len };
    hif_init(NULL);
    hif_register_cb(M2M_REQ_GROUP_IP, ip_cb);
    deliver(&m);
    gRxDoneWrites = 0; gTest881LogE = 0; gIpCalls = 0;
    gFailCtrl1Read = gFailHdrRead = gFailRxDoneWrites = gFailCtrl0Reads = 0;
    gApplyLostRxDone = gHoldAck = gAckPending = gCbAcks = 0;
    gQueued = 0;
    gPostOnRead = gPostAfterRead = gCtrl0Reads = 0;
    gResetBase = hif_rx_reset_requests();
}

static uint32_t resets(void) { return hif_rx_reset_requests() - gResetBase; }

/* The WINC has a further message queued behind the current one. */
static void queue_next(uint16_t size, uint8_t gid, uint8_t op, uint16_t len)
{
    struct msg m = { size, gid, op, len };
    gQueue[gQueued++] = m;
}

/* ---- tests ------------------------------------------------------------ */

/* Baseline: a good message is dispatched and acknowledged exactly once. */
TEST(valid_message_is_dispatched_and_acknowledged)
{
    post(16, M2M_REQ_GROUP_IP, 0x45, 16);
    ASSERT_EQ(hif_handle_isr(), M2M_SUCCESS);
    ASSERT_EQ(gIpCalls, 1);
    ASSERT_EQ(gRxDoneWrites, 1);
    ASSERT_EQ(gTest881LogE, 0);
}

/* Hardware run C: gid 0x82 (IP with one extra bit), op 0x45, size 16. */
TEST(invalid_group_id_is_acknowledged)
{
    post(16, 0x82, 0x45, 16);
    ASSERT_TRUE(hif_handle_isr() != M2M_SUCCESS);
    ASSERT_EQ(gIpCalls, 0);
    ASSERT_EQ(gRxDoneWrites, 1);
    ASSERT_EQ(gTest881LogE, 1);
}

/* Hardware run D: CTRL_0 = 0xC0 (size 48), header length disagrees by > 4. */
TEST(corrupted_size_is_acknowledged)
{
    post(48, M2M_REQ_GROUP_IP, 0x45, 16);
    ASSERT_TRUE(hif_handle_isr() != M2M_SUCCESS);
    ASSERT_EQ(gIpCalls, 0);
    ASSERT_EQ(gRxDoneWrites, 1);
}

TEST(zero_size_is_acknowledged)
{
    post(0, M2M_REQ_GROUP_IP, 0x45, 0);
    ASSERT_TRUE(hif_handle_isr() != M2M_SUCCESS);
    ASSERT_EQ(gRxDoneWrites, 1);
}

TEST(ctrl1_read_failure_is_acknowledged)
{
    post(16, M2M_REQ_GROUP_IP, 0x45, 16);
    gFailCtrl1Read = 1;
    ASSERT_TRUE(hif_handle_isr() != M2M_SUCCESS);
    ASSERT_EQ(gRxDoneWrites, 1);
}

TEST(header_read_failure_is_acknowledged)
{
    post(16, M2M_REQ_GROUP_IP, 0x45, 16);
    gFailHdrRead = 1;
    ASSERT_TRUE(hif_handle_isr() != M2M_SUCCESS);
    ASSERT_EQ(gRxDoneWrites, 1);
}

/* The acknowledgement itself can fail on a bad bus, and a failed write is
 * ambiguous: it may have landed. Writing it again could acknowledge the
 * WINC's NEXT message unread (codex r1-r3 on #1259), so the next pass must
 * not write; it reads CTRL_0, and when nothing shows the write landed it
 * requests a WINC reset. Upstream cleared the pending flag before the write
 * and did neither, so the WINC waited forever. */
TEST(failed_acknowledgement_that_did_not_land_requests_a_reset)
{
    post(16, 0x82, 0x45, 16);
    gFailRxDoneWrites = 1;
    ASSERT_TRUE(hif_handle_isr() != M2M_SUCCESS);
    ASSERT_EQ(gRxDoneWrites, 0);
    ASSERT_EQ(hif_handle_isr(), M2M_SUCCESS);       /* the driver's retry pass */
    ASSERT_EQ(gRxDoneWrites, 0);                    /* never written twice */
    ASSERT_EQ(resets(), 1);
    ASSERT_EQ(hif_handle_isr(), M2M_SUCCESS);       /* resolved: no further request */
    ASSERT_EQ(resets(), 1);
}

/* While CTRL_0 cannot even be read, the retry pass must keep failing: the WINC
 * task re-runs the handler only after an error. */
TEST(unreadable_ctrl0_keeps_the_retry_pass_failing)
{
    post(16, 0x82, 0x45, 16);
    gFailRxDoneWrites = 1;
    ASSERT_TRUE(hif_handle_isr() != M2M_SUCCESS);
    gFailCtrl0Reads = 2;
    ASSERT_TRUE(hif_handle_isr() != M2M_SUCCESS);
    ASSERT_TRUE(hif_handle_isr() != M2M_SUCCESS);
    ASSERT_EQ(resets(), 0);
    ASSERT_EQ(hif_handle_isr(), M2M_SUCCESS);
    ASSERT_EQ(resets(), 1);
    ASSERT_EQ(gRxDoneWrites, 0);
}

/* Same, for a good message whose acknowledgement (by the callback, through
 * hif_receive) fails without landing. */
TEST(failed_acknowledgement_of_a_good_message_requests_a_reset)
{
    post(16, M2M_REQ_GROUP_IP, 0x45, 16);
    gCbAcks = 1;
    gFailRxDoneWrites = 1;
    hif_handle_isr();
    ASSERT_EQ(gIpCalls, 1);
    hif_handle_isr();
    ASSERT_EQ(gRxDoneWrites, 0);
    ASSERT_EQ(resets(), 1);
}

/* codex r1 on #1259: the callback's RX done LANDS -- the WINC frees A and
 * posts B -- but the host is told the write failed. Writing RX done again
 * would acknowledge B unread and lose it. B must be dispatched instead. */
TEST(landed_ack_with_lost_response_does_not_release_the_next_message)
{
    post(16, M2M_REQ_GROUP_IP, 0x45, 16);           /* A */
    queue_next(16, M2M_REQ_GROUP_IP, 0x46, 16);     /* B */
    gCbAcks = 1;
    gApplyLostRxDone = 1;
    hif_handle_isr();                               /* A: ack lands, reported failed */
    ASSERT_EQ(gIpCalls, 1);
    ASSERT_EQ(gRxDoneWrites, 1);
    hif_handle_isr();                               /* B's interrupt */
    ASSERT_EQ(gIpCalls, 2);                         /* B dispatched, not dropped */
    ASSERT_EQ(gRxDoneWrites, 2);
    ASSERT_EQ(gQueued, 0);
}

/* A dropped message's RX done lands but is reported failed, and the WINC has
 * not consumed it yet (bit 1 still set). The retry pass must not write it
 * again: when the WINC does consume, a second RX done would be applied to
 * whatever it posts next. */
TEST(landed_ack_not_yet_consumed_is_not_written_again)
{
    post(16, 0x82, 0x45, 16);                       /* unreadable A */
    queue_next(16, M2M_REQ_GROUP_IP, 0x46, 16);     /* B */
    gHoldAck = 1;
    gApplyLostRxDone = 1;
    ASSERT_TRUE(hif_handle_isr() != M2M_SUCCESS);   /* drop: ack lands, reported failed */
    ASSERT_EQ(gRxDoneWrites, 1);
    ASSERT_EQ(hif_handle_isr(), M2M_SUCCESS);       /* retry pass: sees bit 1, stops */
    ASSERT_EQ(gRxDoneWrites, 1);
    gHoldAck = 0;
    wincConsume();                                  /* WINC frees A, posts B */
    ASSERT_EQ(hif_handle_isr(), M2M_SUCCESS);
    ASSERT_EQ(gIpCalls, 1);                         /* B dispatched */
}

/* codex r2 on #1259: the check and the write must not be separate reads.
 * A's RX done lands but is reported failed; the WINC posts B at the host's
 * 4th CTRL_0 read -- in r2, the second read inside the write, which then
 * acknowledged B unread. (Deciding from one read does NOT by itself prevent
 * an unread acknowledgement -- see the next test; not writing does.) */
TEST(message_posted_after_the_read_back_is_not_acknowledged_unread)
{
    post(16, M2M_REQ_GROUP_IP, 0x45, 16);           /* A */
    queue_next(16, M2M_REQ_GROUP_IP, 0x46, 16);     /* B, posted at read 4 */
    gCbAcks = 1;
    gApplyLostRxDone = 1;
    gPostOnRead = 4;
    hif_handle_isr();                               /* A, then the fallback ack */
    ASSERT_EQ(gIpCalls, 1);
    hif_handle_isr();                               /* B's interrupt */
    ASSERT_EQ(gIpCalls, 2);                         /* B dispatched */
    ASSERT_EQ(gQueued, 0);
}

/* codex r3 on #1259: B is posted just after the read that found nothing
 * pending, before any write. r3 then wrote that stale value with bit 1 set,
 * which cleared B's interrupt and acknowledged it unread -- a read-modify-
 * write race no read ordering closes. With no write after an ambiguous
 * failure, B is dispatched; the price is a WINC reset that, in this schedule,
 * was not needed. */
TEST(message_posted_between_the_check_and_any_write_is_dispatched)
{
    post(16, M2M_REQ_GROUP_IP, 0x45, 16);           /* A */
    queue_next(16, M2M_REQ_GROUP_IP, 0x46, 16);     /* B */
    gCbAcks = 1;
    gApplyLostRxDone = 1;
    gPostAfterRead = 3;                             /* the fallback's check read */
    hif_handle_isr();
    ASSERT_EQ(gIpCalls, 1);
    hif_handle_isr();                               /* B's interrupt */
    ASSERT_EQ(gIpCalls, 2);                         /* B dispatched */
    ASSERT_EQ(gRxDoneWrites, 2);                    /* A's (landed) and B's own */
    ASSERT_EQ(gQueued, 0);
}

int main(void)
{
    printf("#881 hif_isr RX done on error exits\n");
    RUN(valid_message_is_dispatched_and_acknowledged);
    RUN(invalid_group_id_is_acknowledged);
    RUN(corrupted_size_is_acknowledged);
    RUN(zero_size_is_acknowledged);
    RUN(ctrl1_read_failure_is_acknowledged);
    RUN(header_read_failure_is_acknowledged);
    RUN(failed_acknowledgement_that_did_not_land_requests_a_reset);
    RUN(unreadable_ctrl0_keeps_the_retry_pass_failing);
    RUN(failed_acknowledgement_of_a_good_message_requests_a_reset);
    RUN(landed_ack_with_lost_response_does_not_release_the_next_message);
    RUN(landed_ack_not_yet_consumed_is_not_written_again);
    RUN(message_posted_after_the_read_back_is_not_acknowledged_unread);
    RUN(message_posted_between_the_check_and_any_write_is_dispatched);
    return test_summary();
}
