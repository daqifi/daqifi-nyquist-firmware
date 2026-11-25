/* #1198: the interface between test_1198_bootloader_drop_rejected_frame.c and
 * test_1198_datastream_uut.c (the REAL bootloader datastream.c as one host
 * translation unit). Only plain C types cross it: datastream.h DEFINES globals
 * (no `extern`), so a second TU that included bootloader.h would collide with
 * the UUT at link time. Every enum value below is exported FROM the real
 * headers by the UUT, so none of them is a copy. */
#ifndef TEST_1198_UUT_H
#define TEST_1198_UUT_H

#include <stdbool.h>
#include <stdint.h>

/* BOOTLOADER_STATES (bootloader.h) */
extern const int t1198_ST_GET_COMMAND;
extern const int t1198_ST_PROCESS_COMMAND;
extern const int t1198_ST_WAIT_FOR_NVM;
extern const int t1198_ST_SEND_RESPONSE;
extern const int t1198_ST_WAIT_FOR_DONE;
extern const int t1198_ST_CLOSE_DATASTREAM;

/* T_COMMANDS (bootloader.h) */
extern const int t1198_CMD_READ_BOOT_INFO;
extern const int t1198_CMD_ERASE_FLASH;
extern const int t1198_CMD_PROGRAM_FLASH;
extern const int t1198_CMD_READ_CRC;
extern const int t1198_CMD_JMP_TO_APP;

/* HEX_RECORD_STATUS (nvm.h) */
extern const int t1198_HEX_NORMAL;
extern const int t1198_HEX_CRC_ERROR;
extern const int t1198_HEX_PGM_ERROR;
extern const int t1198_HEX_UNKNOW_TYPE;

/* Framing bytes (bootloader.h) */
extern const uint8_t t1198_SOH;
extern const uint8_t t1198_EOT;
extern const uint8_t t1198_DLE;

/* Fresh bootloaderData, buffers and stub counters. */
void t1198_Reset(void);

/* What the APP_ProgramHexRecord stub returns from now on. */
void t1198_SetHexResult(int result);

/* Put an already-unescaped frame (command byte, body, two CRC bytes) straight
 * into the command buffer, as Bootloader_BufferEventHandler leaves it, and set
 * currentState = PROCESS_COMMAND, as Bootloader_Tasks' WAIT_FOR_DONE case does
 * once the handler reports a complete frame. */
void t1198_LoadFrame(const uint8_t *frame, int len);

/* The CRC APP_CalculateCrc's stub computes, so a caller can build a frame the
 * REAL handler's inbound CRC gate accepts. */
uint16_t t1198_Crc(const uint8_t *data, uint32_t len);

/* Bootloader_Tasks' GET_COMMAND case for the USB-device datastream (the
 * client is always READY there): re-arm the read, prevState = GET_COMMAND,
 * currentState = WAIT_FOR_DONE. */
void t1198_EnterGetCommand(void);

/* One USB report's worth of wire bytes through the REAL
 * Bootloader_BufferEventHandler, then -- if it reported a complete frame --
 * Bootloader_Tasks' WAIT_FOR_DONE transition. Returns whether it did. */
bool t1198_DeliverWire(const uint8_t *wire, int n);

/* One pass of Bootloader_Tasks' PROCESS_COMMAND case. */
void t1198_ProcessOnce(void);

/* Bootloader_Tasks' PROCESS_COMMAND case run once per scheduler pass, for as
 * long as the state stays PROCESS_COMMAND, up to maxPasses. Returns the number
 * of Bootloader_ProcessBuffer calls made. */
unsigned t1198_DispatchPasses(unsigned maxPasses);

int t1198_State(void);
int t1198_PrevState(void);
int t1198_ResponseSize(void);       /* bootloaderData.bufferSize */
int t1198_ResponseByte0(void);      /* buff1[0], the response's command echo */
int t1198_CmdBufferLength(void);
const uint8_t *t1198_CmdBuffer(void);  /* buff2 */

unsigned t1198_HexCalls(void);
const uint8_t *t1198_LastHexRecord(void);
int32_t t1198_LastHexLen(void);
unsigned t1198_EraseCalls(void);
unsigned t1198_ResetCalls(void);

#endif
