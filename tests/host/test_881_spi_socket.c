/*
 * #881 (codex r4 on #1259): RX done must survive the layers ABOVE and BELOW
 * m2m_hif.c, not only m2m_hif.c itself. test_881_hif_rx_done.c fakes
 * nm_write_reg(), so it cannot see either of these:
 *
 *  - BELOW: nm_spi_write_reg() resends a write whose SPI response was lost,
 *    up to SPI_RETRY_COUNT times. If the first attempt landed and the WINC
 *    then posted its next message, the resend acknowledges that message
 *    unread -- and reports success, so the HIF layer never learns the write
 *    was in doubt.
 *  - ABOVE: Socket_ReadSocketData() calls the application only when
 *    hif_receive() succeeds. When only the RX done write failed, the payload
 *    was read intact but the application never got it: the TCP server was
 *    never re-armed with recv(), and the client stalled.
 *
 * One translation unit with the REAL m2m_hif.c, nmbus.c, nmspi.c and
 * socket.c. The fake sits underneath all of them, at WDRV_WINC_SPISend /
 * WDRV_WINC_SPIReceive, and plays the WINC's side of the SPI protocol
 * (command, response, data packets) against a register file and a message
 * queue, so the vendor's own retry loop runs exactly as on the board.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "m2m_hif.c"
#include "nmbus.c"
#include "nmspi.c"
#include "socket.c"
#ifdef TEST881_OLD_SOURCE   /* to run this file against pre-r4 sources */
uint32_t hif_rx_reset_requests(void) { return 0; }
#endif
#ifdef TEST881_NO_ONCE      /* to run this file against pre-r5 sources */
int8_t nm_write_reg_once(uint32_t a, uint32_t v);
#endif
#include "test_framework.h"

unsigned gTest881LogE;
void nm_sleep(uint32_t ms) { (void)ms; }
int8_t chip_wake(void) { return M2M_SUCCESS; }
int8_t chip_sleep(void) { return M2M_SUCCESS; }
void nm_reset(void) { }   /* nmbus.c nm_bus_init; unused here */
WDRV_WINC_DEBUG_PRINT_CALLBACK pfWINCDebugPrintCb;

/* ---- fake WINC: register file and messages --------------------------- */
#define RX_ADDR   0x2000u
#define MEM_SIZE  0x100u
#define SESSION   7u

static uint32_t gCtrl0;             /* WIFI_HOST_RCV_CTRL_0 */
static uint8_t  gMem[MEM_SIZE];     /* WINC memory from RX_ADDR */
static unsigned gAcks;              /* RX done writes that LANDED */
static unsigned gAckedUnread;       /* of those, ones that freed a message the host never claimed */
static int      gClaimed;           /* the host cleared bit 0 of the posted message */
static int      gAckPending;        /* RX done landed, WINC not yet consumed it */
static unsigned gQueued;            /* messages behind the posted one */
static uint8_t  gNextPayload;

/* Faults. */
static int gLoseAckResponses;       /* the next N RX done writes: SPI response lost */
static int gDropAcks;               /* the next N RX done writes never reach the WINC */
static int gConsumeOnReset;         /* the WINC consumes a pending RX done and posts
                                       the next message during the SPI layer's
                                       reset delay (spi_reset) */

/* App socket callback. */
static unsigned gRecvCalls;
static uint8_t  gRecvByte[4];
static uint8_t  gUserBuf[64];

/* A socket RECV message: HIF header, tstrRecvReply, 4 payload bytes. */
static void deliver(uint8_t payload)
{
    tstrRecvReply r;
    const uint16_t size = M2M_HIF_HDR_OFFSET + sizeof r + 4;
    memset(gMem, 0, sizeof gMem);
    gMem[0] = M2M_REQ_GROUP_IP;
    gMem[1] = SOCKET_CMD_RECV;
    gMem[2] = (uint8_t)size; gMem[3] = (uint8_t)(size >> 8);
    memset(&r, 0, sizeof r);
    r.s16RecvStatus = 4;
    r.u16DataOffset = sizeof r;
    r.sock = 0;
    r.u16SessionID = SESSION;
    memcpy(&gMem[M2M_HIF_HDR_OFFSET], &r, sizeof r);
    memset(&gMem[M2M_HIF_HDR_OFFSET + sizeof r], payload, 4);
    gCtrl0 = ((uint32_t)size << 2) | NBIT0;
    gClaimed = 0;
}

static void postNext(void)
{
    if (gQueued && !gAckPending && !(gCtrl0 & NBIT0)) {
        gQueued--;
        deliver(gNextPayload++);
    }
}

/* The WINC consumes RX done: it frees the message it has posted, whether or
 * not the host claimed it, then posts the next. */
static void wincConsume(void)
{
    if (!gAckPending) return;
    gAckPending = 0;
    if (!gClaimed) gAckedUnread++;
    gCtrl0 &= ~(NBIT1 | NBIT0);
    postNext();
}

static uint32_t regRead(uint32_t addr)
{
    if (addr == WIFI_HOST_RCV_CTRL_0) return gCtrl0;
    if (addr == WIFI_HOST_RCV_CTRL_1) return RX_ADDR;
    return 0;
}

static void regWrite(uint32_t addr, uint32_t val)
{
    if (addr != WIFI_HOST_RCV_CTRL_0) return;
    if (val & NBIT1) {
        if (gDropAcks) { gDropAcks--; return; }
        gAcks++;
        gCtrl0 = val;
        gAckPending = 1;
        return;                     /* consumed later: wincConsume() */
    }
    if (!(val & NBIT0) && (gCtrl0 & NBIT0)) gClaimed = 1;
    gCtrl0 = val;
}

/* ---- fake WINC: the SPI slave ---------------------------------------- */
static uint8_t  gRsp[MEM_SIZE + 16];
static unsigned gRspLen, gRspPos;
static int      gFailNextReceive;

static void rspByte(uint8_t b) { gRsp[gRspLen++] = b; }
static void rspData(const uint8_t *b, unsigned n)
{
    rspByte(0xF3);                  /* data start token */
    while (n--) rspByte(*b++);
    rspByte(0); rspByte(0);         /* CRC: not checked by the host */
}

bool WDRV_WINC_SPISend(void *p, size_t sz)
{
    const uint8_t *b = p;
    uint32_t addr = ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | b[3];
    (void)sz;
    gRspLen = gRspPos = 0;
    switch (b[0]) {
    case CMD_SINGLE_WRITE: {
        uint32_t v = ((uint32_t)b[4] << 24) | ((uint32_t)b[5] << 16) |
                     ((uint32_t)b[6] << 8) | b[7];
        int ack = (addr == WIFI_HOST_RCV_CTRL_0) && (v & NBIT1);
        regWrite(addr, v);
        rspByte(CMD_SINGLE_WRITE); rspByte(0);
        if (ack && gLoseAckResponses) { gLoseAckResponses--; gFailNextReceive = 1; }
        return true;
    }
    case CMD_SINGLE_READ: {
        uint32_t v = regRead(addr);
        uint8_t d[4] = { (uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24) };
        rspByte(CMD_SINGLE_READ); rspByte(0); rspData(d, 4);
        return true;
    }
    case CMD_DMA_EXT_READ: {
        uint32_t n = ((uint32_t)b[4] << 16) | ((uint32_t)b[5] << 8) | b[6];
        if (addr < RX_ADDR || addr + n > RX_ADDR + MEM_SIZE) return false;
        rspByte(CMD_DMA_EXT_READ); rspByte(0); rspData(&gMem[addr - RX_ADDR], n);
        return true;
    }
    case CMD_RESET:
        /* spi_reset(): the vendor's resync between write retries. */
        if (gConsumeOnReset) { gConsumeOnReset = 0; wincConsume(); }
        rspByte(0xFF); rspByte(CMD_RESET); rspByte(0);
        return true;
    default:
        return false;               /* nothing else is expected here */
    }
}

bool WDRV_WINC_SPIReceive(void *p, size_t sz)
{
    uint8_t *b = p;
    if (gFailNextReceive) { gFailNextReceive = 0; return false; }
    for (size_t i = 0; i < sz; i++)
        b[i] = gRspPos < gRspLen ? gRsp[gRspPos++] : 0xFF;
    return true;
}

/* ---- the application ------------------------------------------------- */
static void app_socket_cb(SOCKET sock, uint8_t msg, void *p)
{
    tstrSocketRecvMsg *m = p;
    (void)sock;
    if (msg == SOCKET_MSG_RECV && m->pu8Buffer && m->s16BufferSize == 4) {
        gRecvByte[gRecvCalls % 4] = m->pu8Buffer[0];
        gRecvCalls++;
        /* re-arm, as the TCP server does with recv() */
        gastrSockets[0].pu8UserBuffer = gUserBuf;
        gastrSockets[0].u16UserBufferSize = sizeof gUserBuf;
    }
}

static uint32_t gResetBase;
static uint32_t resets(void) { return hif_rx_reset_requests() - gResetBase; }

static void setup(unsigned queued)
{
    hif_init(NULL);
    gbSocketInit = 0;
    socketInit();
    registerSocketCallback(app_socket_cb, NULL);
    gastrSockets[0].bIsUsed = 1;
    gastrSockets[0].u16SessionID = SESSION;
    gastrSockets[0].pu8UserBuffer = gUserBuf;
    gastrSockets[0].u16UserBufferSize = sizeof gUserBuf;
    gAcks = gAckedUnread = 0; gAckPending = 0;
    gLoseAckResponses = gDropAcks = gConsumeOnReset = gFailNextReceive = 0;
    gRecvCalls = 0; memset(gRecvByte, 0, sizeof gRecvByte);
    gNextPayload = 0xB0;
    gQueued = queued;
    deliver(0xA0);
    gResetBase = hif_rx_reset_requests();
}

/* Run the WINC task: handle interrupts until it would block, letting the WINC
 * consume RX done between passes as it does on its own clock. */
static void run(int passes)
{
    while (passes--) {
        (void)hif_handle_isr();
        wincConsume();
    }
}

/* ---- tests ------------------------------------------------------------ */

/* Baseline: two RECV messages, each delivered once and acknowledged once. */
TEST(two_messages_are_delivered_and_acknowledged)
{
    setup(1);
    run(3);
    ASSERT_EQ(gRecvCalls, 2);
    ASSERT_EQ(gRecvByte[0], 0xA0);
    ASSERT_EQ(gRecvByte[1], 0xB0);
    ASSERT_EQ(gAcks, 2);
    ASSERT_EQ(gAckedUnread, 0);
    ASSERT_EQ(resets(), 0);
}

/* r4 finding 1: A's RX done lands, its response is lost, and during the SPI
 * layer's reset delay the WINC consumes it and posts B. The vendor retry loop
 * then resends A's CTRL_0 value with bit 1 set -- acknowledging B unread --
 * and reports success. The RX done write must be a single attempt, so the
 * failure reaches hif_set_rx_done(), which reads B's bit 0 and leaves B for
 * the next pass. */
TEST(spi_retry_does_not_acknowledge_the_next_message_unread)
{
    setup(1);
    gLoseAckResponses = 1;
    gConsumeOnReset = 1;
    run(3);
    ASSERT_EQ(gAckedUnread, 0);
    ASSERT_EQ(gRecvCalls, 2);
    ASSERT_EQ(gRecvByte[1], 0xB0);
    ASSERT_EQ(resets(), 0);
}

/* r4 finding 2: the RX done write for a RECV whose payload was read intact
 * lands but reports failure -- on every attempt the SPI layer makes, so the
 * failure is returned whatever the retry policy. The application must still get the payload --
 * Socket_ReadSocketData() drops the callback when hif_receive() fails, and
 * the TCP server is then never re-armed with recv(). The landed RX done is
 * confirmed (bit 1 still visible), so no reset is needed. */
TEST(payload_is_delivered_when_only_its_rx_done_response_is_lost)
{
    setup(0);
    gLoseAckResponses = SPI_RETRY_COUNT;
    run(2);
    ASSERT_EQ(gRecvCalls, 1);
    ASSERT_EQ(gRecvByte[0], 0xA0);
    ASSERT_EQ(gAcks, 1);
    ASSERT_EQ(resets(), 0);
    ASSERT_EQ(gastrSockets[0].pu8UserBuffer == gUserBuf, 1);   /* re-armed */
}

/* Same, but the RX done never reached the WINC: the payload is still
 * delivered, and the unconfirmed RX done ends in a WINC reset (never a
 * second write). */
TEST(payload_is_delivered_and_a_reset_requested_when_rx_done_did_not_land)
{
    setup(0);
    gDropAcks = SPI_RETRY_COUNT;
    gLoseAckResponses = SPI_RETRY_COUNT;  /* each attempt is reported failed */
    run(2);
    ASSERT_EQ(gRecvCalls, 1);
    ASSERT_EQ(gAcks, 0);
    ASSERT_EQ(resets(), 1);
}

int main(void)
{
    printf("#881 RX done through the real SPI and socket layers\n");
    RUN(two_messages_are_delivered_and_acknowledged);
    RUN(spi_retry_does_not_acknowledge_the_next_message_unread);
    RUN(payload_is_delivered_when_only_its_rx_done_response_is_lost);
    RUN(payload_is_delivered_and_a_reset_requested_when_rx_done_did_not_land);
    return TEST_SUMMARY();
}
