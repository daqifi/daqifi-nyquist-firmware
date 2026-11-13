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
#include "test_framework.h"

/* ---- fake WINC ------------------------------------------------------- */
#define RX_ADDR 0x1000u

unsigned gTest881LogE;
static uint32_t gCtrl0;             /* WIFI_HOST_RCV_CTRL_0 as the host sees it */
static unsigned gRxDoneWrites;      /* writes of CTRL_0 with bit 1 set */
static uint8_t gHdr[4];             /* tstrHifHdr at RX_ADDR: gid, op, len LE */
static int gFailCtrl1Read, gFailHdrRead, gFailRxDoneWrites;
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

static void deliver(const struct msg *m)
{
    gCtrl0 = ((uint32_t)m->size << 2) | NBIT0;
    gHdr[0] = m->gid; gHdr[1] = m->op;
    gHdr[2] = (uint8_t)m->len; gHdr[3] = (uint8_t)(m->len >> 8);
}

/* The WINC consumes RX done: frees the message and posts the next, if any. */
static void wincConsume(void)
{
    gAckPending = 0;
    gCtrl0 &= ~NBIT1;
    if (gQueued) {
        deliver(&gQueue[0]);
        memmove(gQueue, gQueue + 1, --gQueued * sizeof gQueue[0]);
    }
}

int8_t nm_read_reg_with_ret(uint32_t addr, uint32_t *val)
{
    if (addr == WIFI_HOST_RCV_CTRL_0) { *val = gCtrl0; return M2M_SUCCESS; }
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
    gFailCtrl1Read = gFailHdrRead = gFailRxDoneWrites = 0;
    gApplyLostRxDone = gHoldAck = gAckPending = gCbAcks = 0;
    gQueued = 0;
}

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

/* The acknowledgement itself can fail on a bad bus. The WINC task re-runs the
 * handler after any error (wdrv_winc.c re-posts its event semaphore); that
 * re-run finds no new interrupt and must retry the acknowledgement. Upstream
 * cleared the pending flag before the write, so a failed write was never
 * retried. */
TEST(failed_acknowledgement_is_retried_on_the_next_pass)
{
    post(16, 0x82, 0x45, 16);
    gFailRxDoneWrites = 1;
    ASSERT_TRUE(hif_handle_isr() != M2M_SUCCESS);
    ASSERT_EQ(gRxDoneWrites, 0);
    hif_handle_isr();               /* the driver's retry: CTRL_0 bit 0 is clear */
    ASSERT_EQ(gRxDoneWrites, 1);
    hif_handle_isr();               /* nothing pending any more: no extra write */
    ASSERT_EQ(gRxDoneWrites, 1);
}

/* While the bus keeps failing, every pass must report an error: the WINC task
 * re-runs the handler only after an error, so a pass that swallowed its failed
 * retry would end the retries with the message still pending (codex r1). */
TEST(acknowledgement_is_retried_until_it_succeeds)
{
    post(16, 0x82, 0x45, 16);
    gFailRxDoneWrites = 3;
    ASSERT_TRUE(hif_handle_isr() != M2M_SUCCESS);
    ASSERT_TRUE(hif_handle_isr() != M2M_SUCCESS);
    ASSERT_TRUE(hif_handle_isr() != M2M_SUCCESS);
    ASSERT_EQ(gRxDoneWrites, 0);
    ASSERT_EQ(hif_handle_isr(), M2M_SUCCESS);
    ASSERT_EQ(gRxDoneWrites, 1);
}

/* Same, for a good message whose post-callback acknowledgement fails. */
TEST(failed_acknowledgement_of_a_good_message_is_retried)
{
    post(16, M2M_REQ_GROUP_IP, 0x45, 16);
    gFailRxDoneWrites = 1;
    ASSERT_TRUE(hif_handle_isr() != M2M_SUCCESS);
    ASSERT_EQ(gIpCalls, 1);
    ASSERT_EQ(gRxDoneWrites, 0);
    hif_handle_isr();
    ASSERT_EQ(gRxDoneWrites, 1);
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

int main(void)
{
    printf("#881 hif_isr RX done on error exits\n");
    RUN(valid_message_is_dispatched_and_acknowledged);
    RUN(invalid_group_id_is_acknowledged);
    RUN(corrupted_size_is_acknowledged);
    RUN(zero_size_is_acknowledged);
    RUN(ctrl1_read_failure_is_acknowledged);
    RUN(header_read_failure_is_acknowledged);
    RUN(failed_acknowledgement_is_retried_on_the_next_pass);
    RUN(acknowledgement_is_retried_until_it_succeeds);
    RUN(failed_acknowledgement_of_a_good_message_is_retried);
    RUN(landed_ack_with_lost_response_does_not_release_the_next_message);
    RUN(landed_ack_not_yet_consumed_is_not_written_again);
    return test_summary();
}
