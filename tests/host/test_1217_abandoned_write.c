/* #1217: an abandoned write must not let the card store bus traffic as a sector.
 *
 * This runs the REAL drv_sdspi.c and drv_sdspi_driver_interface.c, not a copy of
 * their logic, against:
 *
 *  - a model of DRV_SPI that keeps the three properties the bug and the fix turn
 *    on: a FIFO queue where a stuck head blocks everything behind it; per-transfer
 *    CS, asserted when the transfer starts; and CS deasserted at completion only
 *    if the client's CURRENT setup still names the pin.
 *  - a model of an SPI-mode SD card, written from the behaviour #1217's image of
 *    the damaged card shows: while waiting for a data token it takes the FIRST 0
 *    bit as the end of the token and captures from there, one bit at a time.
 *
 * Faults are injected at the SPI layer and the assertions look only at what the
 * card did, so this file also builds against the unfixed driver. It fails there,
 * which is how it was checked (see the #1217 PR).
 *
 * What this cannot show is that a real card behaves like the model. That is the
 * bench's job; #1217's success criteria keep the fault-injection and soak runs. */
#include <stdbool.h>
#include <stdint.h>

#include "configuration.h"
#include "driver/sdspi/drv_sdspi.h"
#include "test_framework.h"

/* ---- the card -------------------------------------------------------------- */

#define SECTORS            64U
#define BLOCK              512U
#define CAPTURE_BYTES      (BLOCK + 2U)
#define OUT_QUEUE          64U
#define EVENT_LOG          512U

enum {
    /* Below 64 is "the card parsed command N with a good CRC7". */
    EV_BAD_CRC7 = 100,
    EV_BLOCK_REJECTED,
    EV_BLOCK_PROGRAMMED,
    EV_STOP_TOKEN,
};

typedef enum {
    CARD_READY,          /* parsing commands, bit by bit */
    CARD_WAIT_SINGLE,    /* CMD24 taken, waiting for a token */
    CARD_WAIT_MULTI,     /* CMD25 taken, waiting for a token */
    CARD_MULTI_TOKEN2,   /* saw the 0 of 0b1111110x, next bit says start or stop */
    CARD_CAPTURE,        /* taking a block */
    CARD_DEAD,           /* drives nothing, parses nothing, until power-cycled */
} CardState;

static struct {
    CardState state;
    bool isMulti;
    bool multiRejected;     /* a rejected block: ignore further data until stop */

    uint8_t sector[SECTORS][BLOCK];
    uint32_t writeLba;

    uint64_t frame;
    unsigned frameBits;

    uint8_t capture[CAPTURE_BYTES];
    unsigned captureBits;

    uint8_t out[OUT_QUEUE];
    unsigned outHead, outCount;
    unsigned busyLeft;
    bool stuckBusy;

    bool crcOn;
    bool idle;
    unsigned acmd41Tries;

    /* test knobs */
    bool ignoresCs;         /* #1217's second hypothesis: it clocks with CS high */
    bool rejectCmd59;
    /* #1217, measured on the bench SanDisk SP32G: a CRC-rejected block inside
     * a CMD25 left the card answering nothing, CMD0 included, until its power
     * was cut, whatever ended the write (flush + stop token, stop token with
     * CS held, CMD12). Set by resetAll(): the model is that card. */
    bool diesOnMultiReject;
    int nextWriteR1;        /* >= 0: answer the next CMD24/25 with this, no data phase */
    unsigned programBusy;   /* busy bytes after a block is accepted */

    int events[EVENT_LOG];
    unsigned eventCount;
} card;

static const uint8_t kCid[16] = { 3, 'S', 'D', 'T', 'E', 'S', 'T', 1, 2, 3, 4, 5, 6, 7, 8, 9 };

static void cardEvent(int ev)
{
    if (card.eventCount < EVENT_LOG)
    {
        card.events[card.eventCount] = ev;
    }
    card.eventCount++;
}

static void cardOut(uint8_t b)
{
    if (card.outCount < OUT_QUEUE)
    {
        card.out[(card.outHead + card.outCount) % OUT_QUEUE] = b;
        card.outCount++;
    }
}

/* R1 and data for the host to clock out. The leading 0xFF is NCR. */
static void cardR1(uint8_t r1)
{
    cardOut(0xFF);
    cardOut(r1);
}

/* Spec-shaped and independent of drv_sdspi_crc.h, so the card checks the driver
 * rather than agreeing with it. */
static uint8_t modelCrc7(const uint8_t* d, unsigned n)
{
    uint8_t crc = 0U;
    for (unsigned i = 0U; i < n; i++)
    {
        for (int b = 7; b >= 0; b--)
        {
            const uint8_t in = (uint8_t)((d[i] >> b) & 1U);
            const uint8_t top = (uint8_t)((crc >> 6) & 1U);
            crc = (uint8_t)((crc << 1) & 0x7FU);
            if ((in ^ top) != 0U)
            {
                crc ^= 0x09U;
            }
        }
    }
    return crc;
}

static uint16_t modelCrc16(const uint8_t* d, unsigned n)
{
    uint16_t crc = 0U;
    for (unsigned i = 0U; i < n; i++)
    {
        for (int b = 7; b >= 0; b--)
        {
            const uint16_t in = (uint16_t)((d[i] >> b) & 1U);
            const uint16_t top = (uint16_t)((crc >> 15) & 1U);
            crc = (uint16_t)(crc << 1);
            if ((in ^ top) != 0U)
            {
                crc ^= 0x1021U;
            }
        }
    }
    return crc;
}

static void cardRegister(const uint8_t* reg)
{
    uint8_t buf[16];
    memcpy(buf, reg, sizeof(buf));
    cardOut(0xFF);
    cardOut(0xFF);
    cardOut(0xFE);
    for (unsigned i = 0U; i < sizeof(buf); i++)
    {
        cardOut(buf[i]);
    }
    cardOut(0x00);
    cardOut(0x00);
}

static void cardCommand(uint8_t cmd, uint32_t arg, uint8_t crcField)
{
    const uint8_t head[5] = { (uint8_t)(0x40U | cmd), (uint8_t)(arg >> 24),
                              (uint8_t)(arg >> 16), (uint8_t)(arg >> 8), (uint8_t)arg };
    static bool nextIsApp = false;
    const bool crcChecked = card.crcOn || (cmd == 0U) || (cmd == 8U);
    const uint8_t idleBit = card.idle ? 1U : 0U;

    if (crcChecked && (modelCrc7(head, 5U) != crcField))
    {
        cardEvent(EV_BAD_CRC7);
        cardR1((uint8_t)(0x08U | idleBit));
        return;
    }

    cardEvent(cmd);

    if (nextIsApp && (cmd == 41U))
    {
        nextIsApp = false;
        card.acmd41Tries++;
        if (card.acmd41Tries >= 2U)
        {
            card.idle = false;
        }
        cardR1(card.idle ? 0x01U : 0x00U);
        return;
    }
    nextIsApp = false;

    switch (cmd)
    {
        case 0U:
            card.crcOn = false;
            card.idle = true;
            card.acmd41Tries = 0U;
            cardR1(0x01U);
            break;
        case 8U:
            cardR1(0x01U);
            cardOut(0x00U);
            cardOut(0x00U);
            cardOut(0x01U);
            cardOut((uint8_t)arg);
            break;
        case 58U:
            cardR1(idleBit);
            cardOut(0xC0U);
            cardOut(0xFFU);
            cardOut(0x80U);
            cardOut(0x00U);
            break;
        case 55U:
            nextIsApp = true;
            cardR1(idleBit);
            break;
        case 9U:
        {
            uint8_t csd[16] = { 0x40U };
            csd[8] = 0x10U; /* C_SIZE: comfortably more than SECTORS */
            cardR1(0x00U);
            cardRegister(csd);
            break;
        }
        case 10U:
            cardR1(0x00U);
            cardRegister(kCid);
            break;
        case 16U:
            cardR1(0x00U);
            break;
        case 59U:
            if (card.rejectCmd59)
            {
                cardR1(0x04U);
            }
            else
            {
                card.crcOn = (arg & 1U) != 0U;
                cardR1(0x00U);
            }
            break;
        case 17U:
            cardR1(0x00U);
            cardOut(0xFFU);
            cardOut(0xFFU);
            cardOut(0xFEU);
            /* A read's data is produced lazily by the out-queue's limit: reads
             * here are only used through the sector array. */
            for (unsigned i = 0U; (i < BLOCK) && (card.outCount < OUT_QUEUE); i++)
            {
                cardOut(0xAAU);
            }
            break;
        case 24U:
        case 25U:
            if (card.nextWriteR1 >= 0)
            {
                cardR1((uint8_t)card.nextWriteR1);
                card.nextWriteR1 = -1;
                break;
            }
            cardR1(0x00U);
            card.writeLba = arg;
            card.isMulti = (cmd == 25U);
            card.multiRejected = false;
            card.state = card.isMulti ? CARD_WAIT_MULTI : CARD_WAIT_SINGLE;
            break;
        default:
            cardR1((uint8_t)(0x04U | idleBit));
            break;
    }
}

static void cardBlockDone(void)
{
    const uint16_t got = (uint16_t)(((uint16_t)card.capture[BLOCK] << 8) | card.capture[BLOCK + 1U]);
    const bool crcBad = card.crcOn && (modelCrc16(card.capture, BLOCK) != got);

    if (crcBad && card.isMulti && card.diesOnMultiReject)
    {
        cardEvent(EV_BLOCK_REJECTED);
        cardOut(0xEBU);
        card.state = CARD_DEAD;
        return;
    }
    if (crcBad || card.multiRejected)
    {
        cardEvent(EV_BLOCK_REJECTED);
        cardOut(0xEBU); /* xxx0_1011: don't-care bits set, as real cards often do */
        card.multiRejected = card.isMulti;
    }
    else
    {
        cardEvent(EV_BLOCK_PROGRAMMED);
        if (card.writeLba < SECTORS)
        {
            memcpy(card.sector[card.writeLba], card.capture, BLOCK);
        }
        card.writeLba++;
        cardOut(0xE5U); /* xxx0_0101 */
        card.busyLeft = card.programBusy;
    }
    card.state = card.isMulti ? CARD_WAIT_MULTI : CARD_READY;
}

static void cardBit(unsigned bit)
{
    switch (card.state)
    {
        case CARD_READY:
            if ((card.busyLeft > 0U) || card.stuckBusy)
            {
                return; /* "all commands rejected" while programming */
            }
            if ((card.frameBits == 0U) && (bit != 0U))
            {
                return;
            }
            card.frame = (card.frame << 1) | bit;
            card.frameBits++;
            if ((card.frameBits == 2U) && (bit == 0U))
            {
                /* Not 0b01: still a start bit in the second position. */
                card.frameBits = 1U;
                card.frame = 0U;
                return;
            }
            if (card.frameBits == 48U)
            {
                const uint8_t cmd = (uint8_t)((card.frame >> 40) & 0x3FU);
                const uint32_t arg = (uint32_t)(card.frame >> 8);
                const uint8_t crc = (uint8_t)((card.frame >> 1) & 0x7FU);
                card.frameBits = 0U;
                card.frame = 0U;
                cardCommand(cmd, arg, crc);
            }
            break;

        case CARD_DEAD:
            return;

        case CARD_WAIT_SINGLE:
            if (bit == 0U)
            {
                card.state = CARD_CAPTURE;
                card.captureBits = 0U;
                memset(card.capture, 0, sizeof(card.capture));
            }
            break;

        case CARD_WAIT_MULTI:
            if ((card.busyLeft > 0U) || card.stuckBusy)
            {
                return;
            }
            if (bit == 0U)
            {
                card.state = CARD_MULTI_TOKEN2;
            }
            break;

        case CARD_MULTI_TOKEN2:
            if (bit == 1U)
            {
                cardEvent(EV_STOP_TOKEN);
                card.state = CARD_READY;
                card.busyLeft = card.programBusy;
            }
            else
            {
                card.state = CARD_CAPTURE;
                card.captureBits = 0U;
                memset(card.capture, 0, sizeof(card.capture));
            }
            break;

        case CARD_CAPTURE:
            if (bit != 0U)
            {
                card.capture[card.captureBits / 8U] |= (uint8_t)(0x80U >> (card.captureBits % 8U));
            }
            card.captureBits++;
            if (card.captureBits == (CAPTURE_BYTES * 8U))
            {
                cardBlockDone();
            }
            break;
    }
}

static uint8_t cardClock(uint8_t mosi)
{
    uint8_t miso = 0xFFU;

    if (card.outCount > 0U)
    {
        miso = card.out[card.outHead];
        card.outHead = (card.outHead + 1U) % OUT_QUEUE;
        card.outCount--;
    }
    else if (card.stuckBusy)
    {
        miso = 0x00U;
    }
    else if (card.busyLeft > 0U)
    {
        miso = 0x00U;
        card.busyLeft--;
    }
    else
    {
        /* idle */
    }

    for (int b = 7; b >= 0; b--)
    {
        cardBit((mosi >> b) & 1U);
    }
    return miso;
}

/* ---- the pin, the clock and DRV_SPI ---------------------------------------- */

#define SD_CS 7

static bool gCsHigh = true;

void SYS_PORT_PinSet(SYS_PORT_PIN pin)
{
    if (pin == SD_CS)
    {
        gCsHigh = true;
    }
}

void SYS_PORT_PinClear(SYS_PORT_PIN pin)
{
    if (pin == SD_CS)
    {
        gCsHigh = false;
    }
}

bool SYS_PORT_PinRead(SYS_PORT_PIN pin)
{
    (void) pin;
    return gCsHigh;
}

unsigned gTest1217LogE;

#define TIMERS 8U
static uint32_t gNowMs;
static struct {
    bool live;
    uint32_t due;
    SYS_TIME_CALLBACK cb;
    uintptr_t ctx;
    uint32_t gen;
} gTimer[TIMERS];
static uint32_t gTimerGen;

SYS_TIME_HANDLE SYS_TIME_CallbackRegisterMS(SYS_TIME_CALLBACK cb, uintptr_t ctx, uint32_t ms, SYS_TIME_CALLBACK_TYPE type)
{
    (void) type;
    for (uint32_t i = 0U; i < TIMERS; i++)
    {
        if (!gTimer[i].live)
        {
            gTimerGen++;
            gTimer[i].live = true;
            gTimer[i].due = gNowMs + ms;
            gTimer[i].cb = cb;
            gTimer[i].ctx = ctx;
            gTimer[i].gen = gTimerGen;
            return (SYS_TIME_HANDLE)((gTimerGen << 8) | i);
        }
    }
    return SYS_TIME_HANDLE_INVALID;
}

SYS_TIME_RESULT SYS_TIME_TimerDestroy(SYS_TIME_HANDLE h)
{
    const uint32_t i = (uint32_t)h & 0xFFU;
    if ((h != SYS_TIME_HANDLE_INVALID) && (i < TIMERS) && gTimer[i].live &&
            (gTimer[i].gen == ((uint32_t)h >> 8)))
    {
        gTimer[i].live = false;
        return SYS_TIME_SUCCESS;
    }
    return SYS_TIME_ERROR;
}

uint64_t SYS_TIME_Counter64Get(void) { return (uint64_t)gNowMs * 1000U; }
uint32_t SYS_TIME_FrequencyGet(void) { return 1000000U; }
uint32_t SYS_TIME_MSToCount(uint32_t ms) { return ms * 1000U; }

void DRV_SDSPI_RegisterWithSysFs(const SYS_MODULE_INDEX drvIndex) { (void) drvIndex; }

typedef struct {
    bool isRead;
    uint8_t* buf;
    size_t n;
    DRV_SPI_TRANSFER_HANDLE h;
    bool corrupt;
    bool stuck;
    bool started;
} Xfer;

typedef enum { FAULT_NONE, FAULT_REJECT, FAULT_LOSE, FAULT_CORRUPT } FaultAction;

/* The next transfer matching all of these takes the action. */
static struct {
    FaultAction action;
    int isRead;      /* 0/1, -1 = either */
    int len;         /* -1 = any */
    int firstByte;   /* -1 = any; writes only */
    int afterCmd;    /* -1 = any; else only once this command's frame has been sent */
    unsigned skip;   /* matches to let through first */
} gFault;

#define SPI_QUEUE 16U
static Xfer gQueue[SPI_QUEUE];
static unsigned gQueueHead, gQueueCount;
static DRV_SPI_TRANSFER_HANDLE gNextHandle;
static DRV_SPI_TRANSFER_EVENT_HANDLER gHandler;
static uintptr_t gHandlerCtx;
static SYS_PORT_PIN gSetupCs;
static int gLastFrameCmd;

static unsigned gLockDepth;
static uint32_t gLockSince;
static uint32_t gLockMaxHeld;

DRV_HANDLE DRV_SPI_Open(uint16_t index, DRV_IO_INTENT intent)
{
    (void) index;
    (void) intent;
    return (DRV_HANDLE)0x100U;
}

void DRV_SPI_TransferEventHandlerSet(DRV_HANDLE h, DRV_SPI_TRANSFER_EVENT_HANDLER cb, uintptr_t ctx)
{
    (void) h;
    gHandler = cb;
    gHandlerCtx = ctx;
}

bool DRV_SPI_TransferSetup(DRV_HANDLE h, DRV_SPI_TRANSFER_SETUP* setup)
{
    (void) h;
    gSetupCs = setup->chipSelect;
    return true;
}

bool DRV_SPI_Lock(DRV_HANDLE h, bool lock)
{
    (void) h;
    if (lock)
    {
        if (gLockDepth == 0U)
        {
            gLockSince = gNowMs;
        }
        gLockDepth++;
        return true;
    }
    if (gLockDepth == 0U)
    {
        return false;
    }
    gLockDepth--;
    if ((gLockDepth == 0U) && ((gNowMs - gLockSince) > gLockMaxHeld))
    {
        gLockMaxHeld = gNowMs - gLockSince;
    }
    return true;
}

bool DRV_SPI_IsExclusiveHolder(DRV_HANDLE h)
{
    (void) h;
    return gLockDepth > 0U;
}

static void spiAdd(bool isRead, void* buf, size_t n, DRV_SPI_TRANSFER_HANDLE* out)
{
    bool corrupt = false;
    bool stuck = false;

    if ((gFault.action != FAULT_NONE) &&
            ((gFault.isRead < 0) || (gFault.isRead == (isRead ? 1 : 0))) &&
            ((gFault.len < 0) || ((size_t)gFault.len == n)) &&
            ((gFault.firstByte < 0) || (!isRead && (((uint8_t*)buf)[0] == (uint8_t)gFault.firstByte))) &&
            ((gFault.afterCmd < 0) || (gFault.afterCmd == gLastFrameCmd)))
    {
        if (gFault.skip > 0U)
        {
            gFault.skip--;
        }
        else
        {
            const FaultAction action = gFault.action;
            gFault.action = FAULT_NONE;
            if (action == FAULT_REJECT)
            {
                *out = DRV_SPI_TRANSFER_HANDLE_INVALID;
                return;
            }
            corrupt = (action == FAULT_CORRUPT);
            stuck = (action == FAULT_LOSE);
        }
    }

    if (!isRead && (n == 6U) && ((((uint8_t*)buf)[0] & 0xC0U) == 0x40U))
    {
        gLastFrameCmd = ((uint8_t*)buf)[0] & 0x3FU;
    }

    gNextHandle++;
    *out = gNextHandle;
    gQueue[(gQueueHead + gQueueCount) % SPI_QUEUE] = (Xfer){ isRead, (uint8_t*)buf, n, *out, corrupt, stuck, false };
    gQueueCount++;
}

void DRV_SPI_WriteTransferAdd(DRV_HANDLE h, void* buf, size_t n, DRV_SPI_TRANSFER_HANDLE* out)
{
    (void) h;
    spiAdd(false, buf, n, out);
}

void DRV_SPI_ReadTransferAdd(DRV_HANDLE h, void* buf, size_t n, DRV_SPI_TRANSFER_HANDLE* out)
{
    (void) h;
    spiAdd(true, buf, n, out);
}

static void spiClock(Xfer* x)
{
    for (size_t i = 0U; i < x->n; i++)
    {
        const bool selected = !gCsHigh;
        if (selected || card.ignoresCs)
        {
            uint8_t mosi = x->isRead ? 0xFFU : x->buf[i];
            if (x->corrupt && (i == 100U))
            {
                mosi ^= 0x10U;
            }
            const uint8_t miso = cardClock(mosi);
            if (x->isRead)
            {
                x->buf[i] = selected ? miso : 0xFFU;
            }
        }
        else if (x->isRead)
        {
            x->buf[i] = 0xFFU;
        }
        else
        {
            /* nobody listening */
        }
    }
}

static void spiComplete(Xfer* x)
{
    spiClock(x);
    /* drv_spi.c's RX DMA handler: CS follows the client's setup as it is NOW. */
    if (gSetupCs != SYS_PORT_PIN_NONE)
    {
        SYS_PORT_PinSet(gSetupCs);
    }
    gHandler(DRV_SPI_TRANSFER_EVENT_COMPLETE, x->h, gHandlerCtx);
}

/* One interrupt's worth of bus progress. A stuck head stays put, as a lost DMA
 * completion does: it blocks everything queued behind it. */
static void spiRun(void)
{
    if (gQueueCount == 0U)
    {
        return;
    }
    if (gQueue[gQueueHead].started)
    {
        return;
    }
    if (gSetupCs != SYS_PORT_PIN_NONE)
    {
        SYS_PORT_PinClear(gSetupCs);
    }
    Xfer x = gQueue[gQueueHead];
    if (x.stuck)
    {
        gQueue[gQueueHead].started = true;
        return;
    }
    gQueueHead = (gQueueHead + 1U) % SPI_QUEUE;
    gQueueCount--;
    spiComplete(&x);
}

/* The stuck head finally finishes, long after the watchdog gave up on it. */
static void spiLateCompletion(void)
{
    Xfer x = gQueue[gQueueHead];
    gQueueHead = (gQueueHead + 1U) % SPI_QUEUE;
    gQueueCount--;
    SYS_PORT_PinClear(SD_CS);
    spiComplete(&x);
}

/* WINC traffic: the card sees it if it is selected or clocks regardless. */
static void wincTraffic(unsigned bytes)
{
    static const uint8_t pattern[8] = { 0xCA, 0x00, 0x10, 0x70, 0x58, 0x00, 0x00, 0x20 };
    for (unsigned i = 0U; i < bytes; i++)
    {
        if (!gCsHigh || card.ignoresCs)
        {
            (void) cardClock(pattern[i % sizeof(pattern)]);
        }
    }
}

/* ---- running the driver ---------------------------------------------------- */

extern void test1217_DriverReset(void);
extern void DRV_SDSPI_ReleaseBus(SYS_MODULE_OBJ object);

static DRV_SDSPI_CLIENT_OBJ gClients[1];
static DRV_SDSPI_BUFFER_OBJ gBuffers[2];
static DRV_HANDLE gHandle;
static int gLastEvent;
static unsigned gEventsSeen;

static void onEvent(SYS_MEDIA_BLOCK_EVENT event, SYS_MEDIA_BLOCK_COMMAND_HANDLE h, uintptr_t ctx)
{
    (void) h;
    (void) ctx;
    gLastEvent = (int)event;
    gEventsSeen++;
}

/* One pass of the SD task's loop, about a millisecond apart. */
static void pass(void)
{
    DRV_SDSPI_Tasks(0);
    spiRun();
    gNowMs++;
    for (uint32_t i = 0U; i < TIMERS; i++)
    {
        if (gTimer[i].live && ((int32_t)(gNowMs - gTimer[i].due) >= 0))
        {
            gTimer[i].live = false;
            gTimer[i].cb(gTimer[i].ctx);
        }
    }
    if ((card.busyLeft > 0U) && !card.stuckBusy)
    {
        card.busyLeft--;
    }
}

static void passes(unsigned n)
{
    for (unsigned i = 0U; i < n; i++)
    {
        pass();
    }
}

static void resetAll(void)
{
    memset(&card, 0, sizeof(card));
    card.nextWriteR1 = -1;
    card.programBusy = 3U;
    card.diesOnMultiReject = true;
    for (unsigned s = 0U; s < SECTORS; s++)
    {
        memset(card.sector[s], (int)(0xA0U + s), BLOCK);
    }
    memset(gTimer, 0, sizeof(gTimer));
    memset(&gFault, 0, sizeof(gFault));
    gFault.isRead = -1;
    gFault.len = -1;
    gFault.firstByte = -1;
    gFault.afterCmd = -1;
    gQueueHead = gQueueCount = 0U;
    gNowMs = 0U;
    gLastFrameCmd = -1;
    gLockDepth = 0U;
    gLockMaxHeld = 0U;
    gCsHigh = true;
    gSetupCs = SYS_PORT_PIN_NONE;
    gLastEvent = -1;
    gEventsSeen = 0U;
    gTest1217LogE = 0U;
    memset(gClients, 0, sizeof(gClients));
    memset(gBuffers, 0, sizeof(gBuffers));
    test1217_DriverReset();
}

static void startDriver(void)
{
    DRV_SDSPI_INIT init;
    memset(&init, 0, sizeof(init));
    init.numClients = 1U;
    init.clientObjPool = (uintptr_t)&gClients[0];
    init.bufferObjPool = (uintptr_t)&gBuffers[0];
    init.bufferObjPoolSize = 2U;
    init.chipSelectPin = SD_CS;
    init.writeProtectPin = SYS_PORT_PIN_NONE;
    init.sdcardSpeedHz = 20000000U;
    init.pollingIntervalMs = 1000U;
    init.isFsEnabled = false;
    (void) DRV_SDSPI_Initialize(0, (const SYS_MODULE_INIT*)&init);
    gHandle = DRV_SDSPI_Open(0, DRV_IO_INTENT_READWRITE);
    DRV_SDSPI_EventHandlerSet(gHandle, (const void*)onEvent, 0U);
}

static bool runUntilAttached(unsigned maxPasses)
{
    for (unsigned i = 0U; i < maxPasses; i++)
    {
        if (DRV_SDSPI_IsAttached(gHandle))
        {
            return true;
        }
        pass();
    }
    return false;
}

static void bootCard(void)
{
    resetAll();
    startDriver();
    ASSERT_TRUE(runUntilAttached(20000U));
}

static int writeBlocks(uint32_t lba, const uint8_t* data, uint32_t n)
{
    DRV_SDSPI_COMMAND_HANDLE h = DRV_SDSPI_COMMAND_HANDLE_INVALID;
    const unsigned seen = gEventsSeen;

    DRV_SDSPI_AsyncWrite(gHandle, &h, (void*)data, lba, n);
    ASSERT_TRUE(h != DRV_SDSPI_COMMAND_HANDLE_INVALID);
    for (unsigned i = 0U; (i < 20000U) && (gEventsSeen == seen); i++)
    {
        pass();
    }
    ASSERT_TRUE(gEventsSeen != seen);
    return gLastEvent;
}

static void fill(uint8_t* buf, size_t n, uint8_t seed)
{
    for (size_t i = 0U; i < n; i++)
    {
        /* i / BLOCK: every block differs, or a stale per-block CRC would pass. */
        buf[i] = (uint8_t)(seed + (i * 7U) + (i / BLOCK));
    }
}

static unsigned cmdCount(int cmd)
{
    unsigned c = 0U;
    for (unsigned i = 0U; (i < card.eventCount) && (i < EVENT_LOG); i++)
    {
        if (card.events[i] == cmd)
        {
            c++;
        }
    }
    return c;
}

/* First event at or after `from` that is any of the given ones. -1 if none. */
static int firstOf(unsigned from, int a, int b, int c)
{
    for (unsigned i = from; (i < card.eventCount) && (i < EVENT_LOG); i++)
    {
        const int e = card.events[i];
        if ((e == a) || (e == b) || (e == c))
        {
            return e;
        }
    }
    return -1;
}

static bool sectorIs(uint32_t lba, const uint8_t* expected)
{
    return memcmp(card.sector[lba], expected, BLOCK) == 0;
}

static bool sectorUntouched(uint32_t lba)
{
    for (unsigned i = 0U; i < BLOCK; i++)
    {
        if (card.sector[lba][i] != (uint8_t)(0xA0U + lba))
        {
            return false;
        }
    }
    return true;
}

/* The card is out of any write: the next write works and lands where it should. */
static void assertCardHealthy(uint32_t lba)
{
    uint8_t data[BLOCK];
    fill(data, sizeof(data), 0x3CU);
    ASSERT_EQ(writeBlocks(lba, data, 1U), DRV_SDSPI_EVENT_COMMAND_COMPLETE);
    ASSERT_TRUE(sectorIs(lba, data));
    ASSERT_EQ(card.state, CARD_READY);
    ASSERT_EQ(gLockDepth, 0);
}

/* ---- normal operation ------------------------------------------------------ */

TEST(init_turns_crc_on_with_valid_frames)
{
    bootCard();
    ASSERT_TRUE(card.crcOn);
    ASSERT_EQ(cmdCount(EV_BAD_CRC7), 0);
    ASSERT_EQ(cmdCount(59), 1);
}

TEST(normal_single_and_multi_writes_are_accepted)
{
    uint8_t data[3U * BLOCK];

    bootCard();
    fill(data, sizeof(data), 0x11U);

    ASSERT_EQ(writeBlocks(5U, data, 1U), DRV_SDSPI_EVENT_COMMAND_COMPLETE);
    ASSERT_TRUE(sectorIs(5U, data));
    ASSERT_EQ(writeBlocks(10U, data, 3U), DRV_SDSPI_EVENT_COMMAND_COMPLETE);
    ASSERT_TRUE(sectorIs(10U, &data[0U]));
    ASSERT_TRUE(sectorIs(11U, &data[BLOCK]));
    ASSERT_TRUE(sectorIs(12U, &data[2U * BLOCK]));

    /* A write that finished cleanly leaves no doubt behind: no resync before the
     * next command, whose 0xFD would show up here as a bad-CRC ghost frame. */
    ASSERT_EQ(writeBlocks(6U, data, 1U), DRV_SDSPI_EVENT_COMMAND_COMPLETE);
    ASSERT_EQ(writeBlocks(7U, data, 1U), DRV_SDSPI_EVENT_COMMAND_COMPLETE);

    ASSERT_EQ(cmdCount(EV_BLOCK_REJECTED), 0);
    ASSERT_EQ(cmdCount(EV_BAD_CRC7), 0);
    ASSERT_EQ(card.state, CARD_READY);
    ASSERT_EQ(gLockDepth, 0);

    /* #1217: with CRC on, the 3-block request went out as three CMD24s. */
    ASSERT_EQ(cmdCount(25), 0);
    ASSERT_EQ(cmdCount(24), 6);
}

TEST(detect_polling_works_with_crc_on)
{
    bootCard();
    const unsigned before = cmdCount(10);
    passes(5000U);
    ASSERT_TRUE(cmdCount(10) > before);
    ASSERT_EQ(cmdCount(EV_BAD_CRC7), 0);
    ASSERT_TRUE(DRV_SDSPI_IsAttached(gHandle));
}

/* ---- a write abandoned before the data token ------------------------------- */

/* The issue's headline. The driver gives up after CMD24; the card waits. */
static void abandonBeforeToken(uint32_t lba, const uint8_t* data)
{
    gFault.action = FAULT_REJECT;
    gFault.isRead = 0;
    gFault.len = 1;
    gFault.firstByte = 0xFE;
    gFault.afterCmd = 24;
    ASSERT_EQ(writeBlocks(lba, data, 1U), DRV_SDSPI_EVENT_COMMAND_ERROR);
    ASSERT_EQ(card.state, CARD_WAIT_SINGLE); /* the premise: it really is waiting */
}

TEST(abandoned_write_next_write_does_not_store_its_own_command)
{
    uint8_t data[BLOCK];

    bootCard();
    fill(data, sizeof(data), 0x22U);
    abandonBeforeToken(20U, data);
    ASSERT_TRUE(gCsHigh);
    ASSERT_EQ(gLockDepth, 0);

    /* The retry: FatFs still holds the dirty sector. */
    const unsigned mark = card.eventCount;
    ASSERT_EQ(writeBlocks(20U, data, 1U), DRV_SDSPI_EVENT_COMMAND_COMPLETE);
    ASSERT_EQ(firstOf(mark, EV_BLOCK_REJECTED, 24, -1), EV_BLOCK_REJECTED);
    ASSERT_TRUE(sectorIs(20U, data));
    ASSERT_EQ(card.state, CARD_READY);
}

TEST(abandoned_write_detect_poll_is_not_captured)
{
    uint8_t data[BLOCK];

    bootCard();
    fill(data, sizeof(data), 0x23U);
    abandonBeforeToken(21U, data);

    /* No retry: the next thing on the bus is the once-a-second CMD10. */
    const unsigned mark = card.eventCount;
    passes(1500U);
    ASSERT_EQ(firstOf(mark, EV_BLOCK_REJECTED, 10, 0), EV_BLOCK_REJECTED);
    ASSERT_TRUE(sectorUntouched(21U));
    ASSERT_TRUE(cmdCount(10) > 0U);
    ASSERT_TRUE(DRV_SDSPI_IsAttached(gHandle));
    assertCardHealthy(22U);
}

/* The sequence in #1217's damaged sectors: WiFi traffic fills the rest of the
 * 512 bytes. Worst case: the card clocks in its data phase even with CS high. */
TEST(abandoned_write_winc_traffic_is_not_programmed)
{
    uint8_t data[BLOCK];

    bootCard();
    fill(data, sizeof(data), 0x24U);
    card.ignoresCs = true;
    abandonBeforeToken(23U, data);
    wincTraffic(600U);
    card.ignoresCs = false;

    ASSERT_EQ(cmdCount(EV_BLOCK_PROGRAMMED), 0);
    ASSERT_TRUE(sectorUntouched(23U));
    assertCardHealthy(24U);
}

/* A lost completion in the buffer path's own wait state: the token's write. */
TEST(buffer_abort_raises_chip_select)
{
    uint8_t data[BLOCK];

    bootCard();
    fill(data, sizeof(data), 0x33U);
    gFault.action = FAULT_LOSE;
    gFault.isRead = 0;
    gFault.len = 1;
    gFault.firstByte = 0xFE;
    gFault.afterCmd = 24;
    ASSERT_EQ(writeBlocks(58U, data, 1U), DRV_SDSPI_EVENT_COMMAND_ERROR);
    ASSERT_TRUE(gCsHigh);
    ASSERT_EQ(gLockDepth, 0);
    spiLateCompletion();
    assertCardHealthy(59U);
}

/* A lost completion in the command executor, on a path with no buffer abort
 * after it to cover for it: the detect poll. */
TEST(command_abort_raises_chip_select)
{
    bootCard();
    gFault.action = FAULT_LOSE;
    gFault.isRead = 1;
    gFault.afterCmd = 10;
    gLastFrameCmd = -1;
    for (unsigned i = 0U; (i < 5000U) && (gFault.action != FAULT_NONE); i++)
    {
        pass();
    }
    ASSERT_EQ(gFault.action, FAULT_NONE);
    ASSERT_FALSE(gCsHigh); /* the premise: the stuck read holds it low */
    passes(600U);          /* past the #567 watchdog */
    ASSERT_TRUE(gCsHigh);
    spiLateCompletion();
    ASSERT_TRUE(runUntilAttached(20000U));
    ASSERT_EQ(gLockDepth, 0);
}

TEST(abort_leaves_chip_select_high_for_winc_traffic)
{
    uint8_t data[BLOCK];

    bootCard();
    fill(data, sizeof(data), 0x25U);

    /* The completion for the R1 read never arrives, so the driver aborts with
     * the transfer still holding CS low. */
    gFault.action = FAULT_LOSE;
    gFault.isRead = 1;
    gFault.afterCmd = 24;
    ASSERT_EQ(writeBlocks(25U, data, 1U), DRV_SDSPI_EVENT_COMMAND_ERROR);

    ASSERT_TRUE(gCsHigh);
    wincTraffic(600U);
    ASSERT_EQ(cmdCount(EV_BLOCK_PROGRAMMED), 0);
    ASSERT_TRUE(sectorUntouched(25U));

    spiLateCompletion();
    ASSERT_TRUE(gCsHigh);
}

/* Handler ordering: DRV_SPI raises CS itself only if setup still names the pin. */
TEST(late_completion_of_an_aborted_transfer_raises_chip_select)
{
    uint8_t data[BLOCK];
    DRV_SPI_TRANSFER_SETUP noCs;

    bootCard();
    fill(data, sizeof(data), 0x26U);
    gFault.action = FAULT_LOSE;
    gFault.isRead = 1;
    gFault.afterCmd = 24;
    ASSERT_EQ(writeBlocks(26U, data, 1U), DRV_SDSPI_EVENT_COMMAND_ERROR);

    memset(&noCs, 0, sizeof(noCs));
    noCs.chipSelect = SYS_PORT_PIN_NONE;
    (void) DRV_SPI_TransferSetup(gHandle, &noCs);
    spiLateCompletion();
    ASSERT_TRUE(gCsHigh);

    gSetupCs = SD_CS;
    passes(10U);
    assertCardHealthy(27U);
}

/* The first R1 is what the abort lost, so the flag cannot wait for it. */
TEST(write_in_doubt_is_recorded_before_the_r1_is_read)
{
    uint8_t data[BLOCK];

    bootCard();
    fill(data, sizeof(data), 0x27U);
    gFault.action = FAULT_REJECT;
    gFault.isRead = 1;
    gFault.afterCmd = 24;
    ASSERT_EQ(writeBlocks(28U, data, 1U), DRV_SDSPI_EVENT_COMMAND_ERROR);
    ASSERT_EQ(card.state, CARD_WAIT_SINGLE);

    const unsigned mark = card.eventCount;
    ASSERT_EQ(writeBlocks(28U, data, 1U), DRV_SDSPI_EVENT_COMMAND_COMPLETE);
    ASSERT_EQ(firstOf(mark, EV_BLOCK_REJECTED, 24, -1), EV_BLOCK_REJECTED);
    ASSERT_TRUE(sectorIs(28U, data));
}

TEST(write_in_doubt_survives_release_bus)
{
    uint8_t data[BLOCK];

    bootCard();
    fill(data, sizeof(data), 0x28U);
    abandonBeforeToken(29U, data);

    const unsigned mark = card.eventCount;
    DRV_SDSPI_ReleaseBus(0);
    ASSERT_TRUE(gCsHigh);
    passes(1500U);
    ASSERT_TRUE(runUntilAttached(20000U));

    /* ReleaseBus restarts detection, so the next command is CMD0. */
    ASSERT_EQ(firstOf(mark, EV_BLOCK_REJECTED, 0, 10), EV_BLOCK_REJECTED);
    ASSERT_TRUE(sectorUntouched(29U));
    ASSERT_EQ(cmdCount(EV_BAD_CRC7), 0);
    ASSERT_TRUE(card.crcOn); /* CMD0 turned it off; re-init turned it back on */
    assertCardHealthy(30U);
}

/* ---- a write abandoned mid-block ------------------------------------------- */

TEST(single_block_abandoned_after_data_is_flushed_and_not_programmed)
{
    uint8_t data[BLOCK];

    bootCard();
    fill(data, sizeof(data), 0x29U);
    gFault.action = FAULT_REJECT;
    gFault.isRead = 0;
    gFault.len = 2;
    gFault.afterCmd = 24;
    ASSERT_EQ(writeBlocks(31U, data, 1U), DRV_SDSPI_EVENT_COMMAND_ERROR);
    ASSERT_EQ(card.state, CARD_CAPTURE);

    const unsigned mark = card.eventCount;
    uint8_t other[BLOCK];
    fill(other, sizeof(other), 0x77U);
    ASSERT_EQ(writeBlocks(32U, other, 1U), DRV_SDSPI_EVENT_COMMAND_COMPLETE);
    ASSERT_EQ(firstOf(mark, EV_BLOCK_REJECTED, 24, -1), EV_BLOCK_REJECTED);
    ASSERT_TRUE(sectorUntouched(31U));
    ASSERT_TRUE(sectorIs(32U, other));
}

/* Just after the token, so the card is at the start of the capture: the whole
 * 514-byte flush is what ends it. Stop-token-only would be swallowed as data. */
TEST(single_block_abandoned_at_start_of_data_is_flushed)
{
    uint8_t data[BLOCK];

    bootCard();
    fill(data, sizeof(data), 0x35U);
    gFault.action = FAULT_REJECT;
    gFault.isRead = 0;
    gFault.len = 512;
    gFault.afterCmd = 24;
    ASSERT_EQ(writeBlocks(63U, data, 1U), DRV_SDSPI_EVENT_COMMAND_ERROR);
    ASSERT_EQ(card.state, CARD_CAPTURE);

    const unsigned mark = card.eventCount;
    assertCardHealthy(38U);
    ASSERT_EQ(firstOf(mark, EV_BLOCK_REJECTED, 24, -1), EV_BLOCK_REJECTED);
    ASSERT_TRUE(sectorUntouched(63U));
}

/* ---- the card rejects a block ---------------------------------------------- */

TEST(crc_reject_is_reported_once_and_not_retried)
{
    uint8_t data[BLOCK];

    bootCard();
    fill(data, sizeof(data), 0x2CU);
    gFault.action = FAULT_CORRUPT; /* a bit flips on the wire, not in the buffer */
    gFault.isRead = 0;
    gFault.len = 512;
    const unsigned before = cmdCount(24);
    ASSERT_EQ(writeBlocks(44U, data, 1U), DRV_SDSPI_EVENT_COMMAND_ERROR);

    ASSERT_EQ(cmdCount(24) - before, 1);
    ASSERT_EQ(cmdCount(EV_BLOCK_REJECTED), 1);
    ASSERT_TRUE(sectorUntouched(44U));
    assertCardHealthy(45U);
}

/* ---- resync on a card that was not waiting ---------------------------------- */

TEST(resync_is_harmless_when_cmd24_was_refused)
{
    uint8_t data[BLOCK];

    bootCard();
    fill(data, sizeof(data), 0x2EU);
    card.nextWriteR1 = 0x40; /* parameter error: the card stays ready */
    ASSERT_EQ(writeBlocks(49U, data, 1U), DRV_SDSPI_EVENT_COMMAND_ERROR);
    ASSERT_EQ(card.state, CARD_READY);

    assertCardHealthy(50U);
    ASSERT_TRUE(sectorUntouched(49U));
    ASSERT_EQ(cmdCount(EV_BLOCK_PROGRAMMED), 1); /* only the healthy-check's */
    ASSERT_TRUE(DRV_SDSPI_IsAttached(gHandle));
}

TEST(resync_is_harmless_when_a_two_block_write_is_refused)
{
    uint8_t data[2U * BLOCK];

    bootCard();
    fill(data, sizeof(data), 0x2FU);
    card.nextWriteR1 = 0x40;
    ASSERT_EQ(writeBlocks(51U, data, 2U), DRV_SDSPI_EVENT_COMMAND_ERROR);
    ASSERT_EQ(card.state, CARD_READY);

    assertCardHealthy(53U);
    ASSERT_TRUE(sectorUntouched(51U));
    ASSERT_TRUE(sectorUntouched(52U));
    ASSERT_EQ(cmdCount(EV_BLOCK_PROGRAMMED), 1);
    ASSERT_EQ(cmdCount(EV_STOP_TOKEN), 0);
    ASSERT_TRUE(DRV_SDSPI_IsAttached(gHandle));
}

/* ---- a multi-block request with CRC on (SD:PROTect 1) ----------------------- */

/* The bench card dies on a CRC reject inside a CMD25 (diesOnMultiReject). With
 * every block a CMD24, the reject costs that block and nothing else. */
TEST(multi_block_crc_reject_leaves_the_card_working)
{
    uint8_t data[3U * BLOCK];

    bootCard();
    fill(data, sizeof(data), 0x2DU);
    gFault.action = FAULT_CORRUPT;
    gFault.isRead = 0;
    gFault.len = 512;
    gFault.skip = 1U; /* the first block goes through, the second is corrupted */
    ASSERT_EQ(writeBlocks(46U, data, 3U), DRV_SDSPI_EVENT_COMMAND_ERROR);

    ASSERT_TRUE(card.state != CARD_DEAD);
    ASSERT_EQ(cmdCount(25), 0);
    ASSERT_EQ(cmdCount(EV_BLOCK_REJECTED), 1);
    ASSERT_TRUE(sectorIs(46U, &data[0U]));
    ASSERT_TRUE(sectorUntouched(47U));
    ASSERT_TRUE(sectorUntouched(48U));
    assertCardHealthy(49U);
}

/* A request abandoned between blocks leaves one CMD24 in doubt, and the
 * single-block resync ends it. */
TEST(multi_block_request_abandoned_between_blocks_is_resynced)
{
    uint8_t data[3U * BLOCK];

    bootCard();
    fill(data, sizeof(data), 0x2AU);
    gFault.action = FAULT_REJECT;
    gFault.isRead = 0;
    gFault.len = 1;
    gFault.firstByte = 0xFE;
    gFault.afterCmd = 24;
    gFault.skip = 1U; /* the first block's token goes through */
    ASSERT_EQ(writeBlocks(33U, data, 3U), DRV_SDSPI_EVENT_COMMAND_ERROR);
    ASSERT_EQ(card.state, CARD_WAIT_SINGLE);
    ASSERT_TRUE(sectorIs(33U, &data[0U]));

    const unsigned mark = card.eventCount;
    assertCardHealthy(40U);
    ASSERT_EQ(firstOf(mark, EV_BLOCK_REJECTED, 24, -1), EV_BLOCK_REJECTED);
    ASSERT_TRUE(sectorUntouched(34U));
    ASSERT_TRUE(sectorUntouched(35U));
    ASSERT_EQ(cmdCount(25), 0);
}

/* ---- SD:PROTect 0 ------------------------------------------------------------ */

/* DRV_SDSPI_SetCrcMode re-runs media init on an attached card, as a
 * reinsertion would. Returns once the card is attached again. */
static void switchCrcMode(bool protect)
{
    const unsigned cmd0Before = cmdCount(0);
    DRV_SDSPI_SetCrcMode(0, protect);
    for (unsigned i = 0U; i < 40000U; i++)
    {
        pass();
        if ((cmdCount(0) > cmd0Before) && DRV_SDSPI_IsAttached(gHandle) &&
                (cmdCount(16) > 0U) && (card.state == CARD_READY) &&
                (card.events[(card.eventCount - 1U) % EVENT_LOG] == 16))
        {
            break;
        }
    }
    ASSERT_TRUE(cmdCount(0) > cmd0Before);
    ASSERT_TRUE(DRV_SDSPI_IsAttached(gHandle));
}

TEST(fast_mode_reinits_without_crc_and_writes_with_cmd25)
{
    uint8_t data[3U * BLOCK];
    bool crcOn = true;
    uint32_t resyncs = 99U;

    bootCard();
    ASSERT_EQ(cmdCount(59), 1);
    switchCrcMode(false);
    ASSERT_FALSE(card.crcOn);
    ASSERT_EQ(cmdCount(59), 1); /* not sent again */
    DRV_SDSPI_GetCrcStats(0, &crcOn, &resyncs);
    ASSERT_FALSE(crcOn);

    fill(data, sizeof(data), 0x41U);
    ASSERT_EQ(writeBlocks(20U, data, 3U), DRV_SDSPI_EVENT_COMMAND_COMPLETE);
    ASSERT_EQ(cmdCount(25), 1);
    ASSERT_TRUE(sectorIs(20U, &data[0U]));
    ASSERT_TRUE(sectorIs(22U, &data[2U * BLOCK]));

    switchCrcMode(true);
    ASSERT_TRUE(card.crcOn);
    ASSERT_EQ(cmdCount(59), 2);
    assertCardHealthy(25U);
}

/* With CRC off the resync's 0xFF block would be PROGRAMMED by a waiting card,
 * so fast mode must never start one. */
TEST(fast_mode_abort_does_not_resync)
{
    uint8_t data[BLOCK];
    bool crcOn = true;
    uint32_t resyncs = 99U;

    bootCard();
    switchCrcMode(false);
    fill(data, sizeof(data), 0x42U);
    gFault.action = FAULT_REJECT;
    gFault.isRead = 0;
    gFault.len = 1;
    gFault.firstByte = 0xFE;
    gFault.afterCmd = 24;
    ASSERT_EQ(writeBlocks(30U, data, 1U), DRV_SDSPI_EVENT_COMMAND_ERROR);
    ASSERT_EQ(card.state, CARD_WAIT_SINGLE);

    /* The next command is where a resync would run. With CRC off the waiting
     * card captures this CMD24 instead -- the hazard SD:PROTect 0 accepts --
     * so its outcome is not asserted, only that no filler block went out. */
    (void) writeBlocks(31U, data, 1U);

    DRV_SDSPI_GetCrcStats(0, &crcOn, &resyncs);
    ASSERT_EQ(resyncs, 0U);
    for (unsigned i = 0U; i < 4U; i++)
    {
        ASSERT_FALSE(card.sector[30U][i] == 0xFFU); /* no 0xFF filler block */
    }
}

/* ---- bounded ---------------------------------------------------------------- */

TEST(stuck_busy_card_gets_no_commands_and_never_pins_the_bus)
{
    uint8_t data[BLOCK];

    bootCard();
    fill(data, sizeof(data), 0x30U);

    /* Busy for good, holding MISO low: the write fails and stays in doubt. */
    card.stuckBusy = true;
    ASSERT_EQ(writeBlocks(54U, data, 1U), DRV_SDSPI_EVENT_COMMAND_ERROR);

    const unsigned cmd0 = cmdCount(0);
    const unsigned cmd10 = cmdCount(10);
    const unsigned logs = gTest1217LogE;
    gLockMaxHeld = 0U;
    passes(10000U);

    ASSERT_EQ(cmdCount(0), cmd0);
    ASSERT_EQ(cmdCount(10), cmd10);
    ASSERT_TRUE(gTest1217LogE > logs);
    /* One attempt's budget (500 ms) plus one transfer's watchdog (500 ms). */
    ASSERT_TRUE(gLockMaxHeld <= 1100U);

    /* It recovers by itself once the card lets go. */
    card.stuckBusy = false;
    ASSERT_TRUE(runUntilAttached(20000U));
    ASSERT_TRUE(card.crcOn);
    assertCardHealthy(55U);
}

/* The watchdog (#567) is what bounds a resync transfer whose completion is lost. */
TEST(resync_with_a_lost_completion_fails_and_retries)
{
    uint8_t data[BLOCK];

    bootCard();
    fill(data, sizeof(data), 0x31U);
    abandonBeforeToken(56U, data);

    gFault.action = FAULT_LOSE;
    gFault.isRead = -1;
    gFault.len = -1;
    gFault.firstByte = -1;
    gFault.afterCmd = -1;
    gLockMaxHeld = 0U;
    ASSERT_EQ(writeBlocks(56U, data, 1U), DRV_SDSPI_EVENT_COMMAND_ERROR);
    ASSERT_TRUE(gCsHigh);
    ASSERT_EQ(gLockDepth, 0);
    ASSERT_EQ(cmdCount(EV_BLOCK_PROGRAMMED), 0);
    ASSERT_TRUE(gLockMaxHeld <= 1100U);

    /* The stuck transfer drains and the bus works again; the flag is still set. */
    spiLateCompletion();
    const unsigned mark = card.eventCount;
    ASSERT_EQ(writeBlocks(56U, data, 1U), DRV_SDSPI_EVENT_COMMAND_COMPLETE);
    ASSERT_EQ(firstOf(mark, EV_BLOCK_REJECTED, 24, -1), EV_BLOCK_REJECTED);
    ASSERT_TRUE(sectorIs(56U, data));
}

/* ---- a card that refuses CRC ------------------------------------------------ */

TEST(card_that_rejects_cmd59_is_not_mounted_and_says_so)
{
    uint8_t data[BLOCK];
    DRV_SDSPI_COMMAND_HANDLE h = DRV_SDSPI_COMMAND_HANDLE_INVALID;

    resetAll();
    startDriver();
    card.rejectCmd59 = true;
    fill(data, sizeof(data), 0x32U);

    passes(5000U);

    ASSERT_TRUE(cmdCount(59) >= 1U);
    ASSERT_FALSE(DRV_SDSPI_IsAttached(gHandle));
    ASSERT_TRUE(gTest1217LogE > 0U);
    DRV_SDSPI_AsyncWrite(gHandle, &h, data, 57U, 1U);
    ASSERT_EQ(h, DRV_SDSPI_COMMAND_HANDLE_INVALID);
    ASSERT_EQ(cmdCount(24), 0);
}

int main(void)
{
    RUN(init_turns_crc_on_with_valid_frames);
    RUN(normal_single_and_multi_writes_are_accepted);
    RUN(detect_polling_works_with_crc_on);
    RUN(abandoned_write_next_write_does_not_store_its_own_command);
    RUN(abandoned_write_detect_poll_is_not_captured);
    RUN(abandoned_write_winc_traffic_is_not_programmed);
    RUN(buffer_abort_raises_chip_select);
    RUN(command_abort_raises_chip_select);
    RUN(abort_leaves_chip_select_high_for_winc_traffic);
    RUN(late_completion_of_an_aborted_transfer_raises_chip_select);
    RUN(write_in_doubt_is_recorded_before_the_r1_is_read);
    RUN(write_in_doubt_survives_release_bus);
    RUN(single_block_abandoned_after_data_is_flushed_and_not_programmed);
    RUN(single_block_abandoned_at_start_of_data_is_flushed);
    RUN(crc_reject_is_reported_once_and_not_retried);
    RUN(resync_is_harmless_when_cmd24_was_refused);
    RUN(resync_is_harmless_when_a_two_block_write_is_refused);
    RUN(multi_block_crc_reject_leaves_the_card_working);
    RUN(multi_block_request_abandoned_between_blocks_is_resynced);
    RUN(fast_mode_reinits_without_crc_and_writes_with_cmd25);
    RUN(fast_mode_abort_does_not_resync);
    RUN(stuck_busy_card_gets_no_commands_and_never_pins_the_bus);
    RUN(resync_with_a_lost_completion_fails_and_retries);
    RUN(card_that_rejects_cmd59_is_not_mounted_and_says_so);
    return TEST_SUMMARY();
}
