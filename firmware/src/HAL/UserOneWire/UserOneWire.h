/**
 * @file UserOneWire.h
 * @brief Bit-banged 1-Wire master on a DIO channel (#669, epic #664).
 *
 * No 1-Wire silicon exists on the PIC32MZ, so this emulates open-drain with the
 * DIO channel's output BUFFER (a 2G241 half whose OE is the channel-enable GPIO):
 *   drive-low = enable the buffer with data LOW (strong low);
 *   release   = disable the buffer -> terminal high-Z, the sensor's external
 *               4.7 kOhm pull-up to 3.3 V raises the bus;
 *   sample    = read the pin through the 100K series read path (settled well
 *               within the 15 us read slot).
 * The buffer is NEVER enabled with data HIGH (that would push-pull the +5V_D rail
 * against the sensor). Standard-speed slots are timed off the core timer inside
 * short per-bit critical sections.
 *
 * Because a reset holds interrupts off for ~1 ms (masking the pri-3 streaming
 * timer), 1-Wire transactions are REJECTED while streaming is active. The bus
 * buffer is powered from +5V_D, so the device must be POWERED_UP.
 *
 * Firmware is mechanism only: CRC and sensor conveniences (DS18B20 convert-T,
 * scratchpad decode) live client-side.
 */
#ifndef USER_ONEWIRE_H
#define USER_ONEWIRE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define USER_OWIRE_ROM_BYTES   8u    /*!< 64-bit ROM id */
#define USER_OWIRE_MAX_DEVICES 16u   /*!< SEARch enumeration cap (bus-fault guard) */

/** Enable (claim + configure) or disable 1-Wire on DIO channel @p dio.
 *  @return false (reason in @p err) if the pin is claimed elsewhere, the device
 *  is not powered, or streaming is active. */
bool UserOneWire_Enable(uint8_t dio, bool enable, const char** err);

/** Query whether 1-Wire is enabled, and on which channel (0xFF if none). */
bool UserOneWire_IsEnabled(uint8_t* dio);

/** True while a 1-Wire transaction is bit-banging the bus. Read by the streaming
 *  start path so it can't arm the stream timer mid-transaction. */
bool UserOneWire_IsBusy(void);

/** Issue a reset pulse; @p present = true if a slave asserted presence.
 *  @return false (reason in @p err) if not enabled / streaming / bus fault. */
bool UserOneWire_Reset(bool* present, const char** err);

/** Byte-level transfer: write @p nWrite bytes from @p wbuf (after a reset), then
 *  read @p nRead bytes into @p rbuf. Either count may be 0. @return false
 *  (reason in @p err) on not-enabled / not-powered / streaming / stuck-bus /
 *  no-presence. */
bool UserOneWire_Transfer(const uint8_t* wbuf, size_t nWrite,
                          uint8_t* rbuf, size_t nRead, const char** err);

/** ROM SEARCH: enumerate up to @p maxRoms devices into @p roms (8 bytes each).
 *  @p count returns the number found. @return false (reason in @p err) on
 *  not-enabled / streaming / bus fault. */
bool UserOneWire_Search(uint8_t* roms, uint8_t maxRoms, uint8_t* count,
                        const char** err);

#ifdef __cplusplus
}
#endif

#endif /* USER_ONEWIRE_H */
