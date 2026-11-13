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
            if (gFailRxDoneWrites) { gFailRxDoneWrites--; return M2M_ERR_BUS_FAIL; }
            gRxDoneWrites++;
            val &= ~NBIT1;          /* the WINC consumes the bit */
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

static void ip_cb(uint8_t op, uint16_t sz, uint32_t addr) { (void)op; (void)sz; (void)addr; gIpCalls++; }

/* The WINC posts a message of `size` bytes whose header reads gid/op/len. */
static void post(uint16_t size, uint8_t gid, uint8_t op, uint16_t len)
{
    hif_init(NULL);
    hif_register_cb(M2M_REQ_GROUP_IP, ip_cb);
    gCtrl0 = ((uint32_t)size << 2) | NBIT0;
    gHdr[0] = gid; gHdr[1] = op; gHdr[2] = (uint8_t)len; gHdr[3] = (uint8_t)(len >> 8);
    gRxDoneWrites = 0; gTest881LogE = 0; gIpCalls = 0;
    gFailCtrl1Read = gFailHdrRead = gFailRxDoneWrites = 0;
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
    RUN(failed_acknowledgement_of_a_good_message_is_retried);
    return test_summary();
}
