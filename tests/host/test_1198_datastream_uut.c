/* #1198: the REAL bootloader datastream.c, as one host translation unit, for
 * test_1198_bootloader_drop_rejected_frame.c. Built with warnings off, like
 * test_1217_driver_uut.c: this is vendored Harmony code compiled for a
 * platform it was not written for, and its warnings are not this test's
 * subject. stubs/btl_1198/ supplies the two headers that cannot be used on a
 * host (<sys/kmem.h>, system/reset/sys_reset.h); every other header is the
 * bootloader's own.
 *
 * What is NOT the real code here, and why:
 *  - bootloaderData and its buffer: defined in bootloader.c, which pulls in
 *    the NVM/USB PLIBs and is not host-compilable.
 *  - APP_ProgramHexRecord / APP_FlashErase (nvm.c) and APP_CalculateCrc
 *    (bootloader.c): stubs. The record parser is #1141's test's subject; here
 *    only its RESULT matters, which is what datastream.c branches on.
 *  - Bootloader_Tasks' transitions around Bootloader_ProcessBuffer
 *    (t1198_EnterGetCommand, t1198_DeliverWire's WAIT_FOR_DONE step,
 *    t1198_DispatchPasses): mirrors of bootloader.c's GET_COMMAND,
 *    WAIT_FOR_DONE and PROCESS_COMMAND cases. The Makefile pins the one the
 *    replay depends on -- that the PROCESS_COMMAND case only calls
 *    Bootloader_ProcessBuffer and sets no state of its own. */
#include "datastream/datastream.c"

#include "test_1198_uut.h"

BOOTLOADER_DATA bootloaderData;
static BOOTLOADER_BUFFER uutBuffer;

static char hexResult;
static unsigned hexCalls;
static const uint8_t *lastHexRecord;
static int32_t lastHexLen;
static unsigned eraseCalls;
static unsigned resetCalls;

const int t1198_ST_GET_COMMAND = BOOTLOADER_GET_COMMAND;
const int t1198_ST_PROCESS_COMMAND = BOOTLOADER_PROCESS_COMMAND;
const int t1198_ST_WAIT_FOR_NVM = BOOTLOADER_WAIT_FOR_NVM;
const int t1198_ST_SEND_RESPONSE = BOOTLOADER_SEND_RESPONSE;
const int t1198_ST_WAIT_FOR_DONE = BOOTLOADER_WAIT_FOR_DONE;
const int t1198_ST_CLOSE_DATASTREAM = BOOTLOADER_CLOSE_DATASTREAM;

const int t1198_CMD_READ_BOOT_INFO = READ_BOOT_INFO;
const int t1198_CMD_ERASE_FLASH = ERASE_FLASH;
const int t1198_CMD_PROGRAM_FLASH = PROGRAM_FLASH;
const int t1198_CMD_READ_CRC = READ_CRC;
const int t1198_CMD_JMP_TO_APP = JMP_TO_APP;

const int t1198_HEX_NORMAL = HEX_REC_NORMAL;
const int t1198_HEX_CRC_ERROR = HEX_REC_CRC_ERROR;
const int t1198_HEX_PGM_ERROR = HEX_REC_PGM_ERROR;
const int t1198_HEX_UNKNOW_TYPE = HEX_REC_UNKNOW_TYPE;

const uint8_t t1198_SOH = SOH;
const uint8_t t1198_EOT = EOT;
const uint8_t t1198_DLE = DLE;

char APP_ProgramHexRecord(uint8_t *HexRecord, int32_t totalLen)
{
    hexCalls++;
    lastHexRecord = HexRecord;
    lastHexLen = totalLen;
    return hexResult;
}

void APP_FlashErase(void)
{
    eraseCalls++;
}

void SYS_RESET_SoftwareReset(void)
{
    resetCalls++;
}

uint16_t t1198_Crc(const uint8_t *data, uint32_t len)
{
    uint16_t sum = 0x5A5Au;
    for (uint32_t i = 0; i < len; i++)
    {
        sum = (uint16_t)((sum << 1) ^ data[i] ^ (sum >> 15));
    }
    return sum;
}

/* Real signature from bootloader.h. Called on buff2 by the handler's inbound
 * gate, and on a KVA0_TO_KVA1 flash address by READ_CRC -- which must not be
 * dereferenced on a host, so anything outside our own buffer reads as 0. */
uint32_t APP_CalculateCrc(uint8_t *data, uint32_t len)
{
    const uintptr_t lo = (uintptr_t)uutBuffer.buffer;
    const uintptr_t hi = lo + sizeof(uutBuffer.buffer);
    const uintptr_t p = (uintptr_t)data;
    if ((p < lo) || (p >= hi) || (len > (uint32_t)(hi - p)))
    {
        return 0u;
    }
    return t1198_Crc(data, len);
}

void t1198_Reset(void)
{
    (void) memset(&bootloaderData, 0, sizeof(bootloaderData));
    (void) memset(&uutBuffer, 0, sizeof(uutBuffer));
    bootloaderData.data = &uutBuffer;
    bootloaderData.datastreamBufferHandle = DRV_HANDLE_INVALID;
    bootloaderData.currentState = BOOTLOADER_GET_COMMAND;
    bootloaderData.prevState = BOOTLOADER_GET_COMMAND;
    hexResult = HEX_REC_NORMAL;
    hexCalls = 0u;
    lastHexRecord = NULL;
    lastHexLen = -1;
    eraseCalls = 0u;
    resetCalls = 0u;
}

void t1198_SetHexResult(int result)
{
    hexResult = (char)result;
}

void t1198_LoadFrame(const uint8_t *frame, int len)
{
    (void) memcpy(uutBuffer.buffers.buff2, frame, (size_t)len);
    bootloaderData.cmdBufferLength = len;
    bootloaderData.prevState = BOOTLOADER_GET_COMMAND;
    bootloaderData.currentState = BOOTLOADER_PROCESS_COMMAND;
}

void t1198_EnterGetCommand(void)
{
    /* bootloader.c, case BOOTLOADER_GET_COMMAND, with
     * DATASTREAM_ClientStatus() == DRV_CLIENT_STATUS_READY (datastream.c's
     * weak default and the USB-device build's behaviour). */
    bootloaderData.bufferSize = 512;
    DATASTREAM_Data_Read(&(bootloaderData.datastreamBufferHandle),
                         bootloaderData.data->buffers.buff1, bootloaderData.bufferSize);
    bootloaderData.prevState = BOOTLOADER_GET_COMMAND;
    bootloaderData.currentState = BOOTLOADER_WAIT_FOR_DONE;
}

bool t1198_DeliverWire(const uint8_t *wire, int n)
{
    (void) memcpy(uutBuffer.buffers.buff1, wire, (size_t)n);
    Bootloader_BufferEventHandler(DATASTREAM_BUFFER_EVENT_COMPLETE,
                                  (DATASTREAM_BUFFER_HANDLE)0, (uint16_t)n);
    /* bootloader.c, case BOOTLOADER_WAIT_FOR_DONE. */
    if ((bootloaderData.currentState == BOOTLOADER_WAIT_FOR_DONE) &&
        bootloaderData.usrBufferEventComplete)
    {
        bootloaderData.usrBufferEventComplete = false;
        bootloaderData.currentState = (bootloaderData.prevState == BOOTLOADER_GET_COMMAND)
                                      ? BOOTLOADER_PROCESS_COMMAND : BOOTLOADER_GET_COMMAND;
        return true;
    }
    return false;
}

void t1198_ProcessOnce(void)
{
    Bootloader_ProcessBuffer(&bootloaderData);
}

unsigned t1198_DispatchPasses(unsigned maxPasses)
{
    unsigned calls = 0u;
    /* bootloader.c, case BOOTLOADER_PROCESS_COMMAND: the case body is the one
     * call, so the state machine re-enters it on every Bootloader_Tasks pass
     * until Bootloader_ProcessBuffer itself moves the state on. */
    while ((calls < maxPasses) && (bootloaderData.currentState == BOOTLOADER_PROCESS_COMMAND))
    {
        Bootloader_ProcessBuffer(&bootloaderData);
        calls++;
    }
    return calls;
}

int t1198_State(void) { return (int)bootloaderData.currentState; }
int t1198_PrevState(void) { return (int)bootloaderData.prevState; }
int t1198_ResponseSize(void) { return (int)bootloaderData.bufferSize; }
int t1198_ResponseByte0(void) { return (int)uutBuffer.buffers.buff1[0]; }
int t1198_CmdBufferLength(void) { return bootloaderData.cmdBufferLength; }
const uint8_t *t1198_CmdBuffer(void) { return uutBuffer.buffers.buff2; }

unsigned t1198_HexCalls(void) { return hexCalls; }
const uint8_t *t1198_LastHexRecord(void) { return lastHexRecord; }
int32_t t1198_LastHexLen(void) { return lastHexLen; }
unsigned t1198_EraseCalls(void) { return eraseCalls; }
unsigned t1198_ResetCalls(void) { return resetCalls; }
