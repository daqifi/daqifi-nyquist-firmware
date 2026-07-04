#pragma once

/**
 * @file SpiBusHealth.h
 * @brief Shared-SPI4 bus jam detection and recovery (#589 Tiers 1-2).
 *
 * The SD card and the WINC1500 WiFi module share SPI4. A failing SD card can
 * hold the shared MISO line (DO) even with its CS deasserted — electrically
 * jamming the bus so the WINC becomes unreachable (all-zero reads), with
 * symptoms identical to a dead/corrupted WiFi module. Bench-proven 2026-07-03
 * on Nq1 ...026A. The slot has no power switch (VDD hardwired), so firmware
 * cannot power-cycle the card; what it CAN do:
 *
 *  - Tier 1 (probe): with both CS lines deasserted, enable the PIC32 internal
 *    pull-up on the MISO pin and sample it. Driven low with nothing selected
 *    means something is jamming the bus — and the SD card is the only
 *    removable device on it.
 *  - Tier 2 (release): a card that merely BELIEVES it is selected (glitched
 *    CS / aborted mid-read) legitimately drives DO until clocked out. Clock
 *    dummy bytes with no CS asserted (enough for a full 512-byte block +
 *    CRC) and re-probe. A hard-stuck output driver will not release — only
 *    card removal (or a future VDD switch, hardware-design#1) cures that.
 *
 * Thread-safety: the probe is read-only on PORT registers plus a CNPU
 * set/restore; the release uses a dedicated DRV_SPI client (thread-safe at
 * the driver layer) with no chip select. Both are safe to call from any task
 * context; do not call from ISRs.
 */

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SPI_BUS_CLEAR = 0,       //!< MISO floats high with no CS asserted — bus healthy
    SPI_BUS_JAMMED,          //!< MISO held low with no CS asserted — bus jammed (suspect SD card)
    SPI_BUS_BUSY,            //!< A CS line was asserted — probe skipped, try again when idle
    SPI_BUS_INDETERMINATE,   //!< Mixed samples — retry; treat as suspicious, not proven
} SpiBusHealthResult_t;

/** Tier 1: sample the shared MISO with both CS lines deasserted. */
SpiBusHealthResult_t SpiBusHealth_ProbeJam(void);

/**
 * Tier 2: attempt to release a soft-stuck card — clock ~600 dummy bytes with
 * no chip select asserted (drains a stuck mid-block read incl. CRC), then
 * re-probe. Returns the post-release probe result.
 */
SpiBusHealthResult_t SpiBusHealth_TryRelease(void);

/**
 * Tier 1b: ACTIVE probe — clock 16 dummy bytes with no chip select and check
 * whether anything answers (non-0xFF on MISO with the pull-up held). Catches
 * a card with broken CS gating that corrupts live traffic while leaving the
 * idle bus clean (invisible to ProbeJam). Safe when the bus is idle only —
 * returns SPI_BUS_BUSY if a CS is asserted.
 */
SpiBusHealthResult_t SpiBusHealth_ProbeActive(void);

/** Last probe result (for diagnostics queries; SPI_BUS_CLEAR if never run). */
SpiBusHealthResult_t SpiBusHealth_LastResult(void);

/**
 * Tier 3 quarantine: when true, the SD task suspends (no chip select, no
 * clocks toward the card) so a jammed card stops being driven. Set by the
 * jam handler in wifi_manager; cleared by SYST:STOR:SD:ENAble 1 (manual
 * retry after the user reseats/removes the card).
 */
void SpiBusHealth_SetSdQuarantine(bool quarantine);
bool SpiBusHealth_IsSdQuarantined(void);

#ifdef __cplusplus
}
#endif
