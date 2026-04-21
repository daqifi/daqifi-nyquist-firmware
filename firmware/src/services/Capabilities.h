#ifndef _DAQIFI_CAPABILITIES_H
#define _DAQIFI_CAPABILITIES_H

/*
 * Capabilities.h — vendor-neutral capability framework for DAQiFi.
 *
 * See issue #327 for the full vision. This module is the firmware
 * reference implementation of the capability schema: clients query
 * SCPI capability commands and render themselves from the response
 * rather than hard-coding board variants or channel counts.
 *
 * Current coverage:
 *   - DAQIFI_CAPABILITIES_VERSION (schema version byte)
 *   - Streaming cap + formula constants
 *     (CONFigure:ADC:MAXFreq?)
 *   - AIN / DIO topology summaries
 *     (CONFigure:CAPabilities:AIN? / :DIO?)
 *   - Unified JSON rollup
 *     (CONFigure:CAPabilities:JSON?)
 *   - Capability fields embedded in DaqifiOutMessage protobuf
 *     (tags 70..75, see DaqifiOutMessage.proto)
 */

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * SCPI capability schema version byte.
 *
 * Clients use this to decide which parser to apply before issuing
 * any other capability query. Increment on any BREAKING change to
 * the capability schema — i.e. field renames/removals, type
 * changes, semantics changes on existing fields.
 *
 * Do NOT bump for additive changes — adding a new capability
 * command or a new key=value field in an existing response is
 * additive and safe for older clients.
 *
 * History:
 *   1 — Initial version. AIN/DIO capability summaries + API
 *       version query. (#327)
 */
#define DAQIFI_CAPABILITIES_VERSION ((uint8_t)1)

/*
 * Summary of analog-input topology from the current board config.
 * All counts are for PUBLIC channels (user-facing) — internal
 * monitoring channels are excluded because clients rendering a UI
 * shouldn't see or control them. See issue #327.
 */
typedef struct {
    uint16_t publicChannelCount;   /* public AIn channels total */
    uint16_t type1Count;           /* dedicated-ADC (simultaneous) channels */
    uint16_t type2Count;           /* shared-ADC (muxed) channels */
    uint16_t type1Bitmask;         /* bit N set ⇒ channel N is Type 1 */
    uint16_t type2Bitmask;         /* bit N set ⇒ channel N is Type 2 */
    bool     hasAD7609;            /* any AD7609 (external 18-bit) channel present */
    uint8_t  primaryResolutionBits;/* dominant ADC resolution (12=MC12b, 18=AD7609) */
} CapabilitiesAinSummary;

/*
 * Summary of DIO topology. Bitmasks are indexed by DIO channel
 * number (bit 0 = DIO_0, etc). Clients use these to know which
 * pins accept which runtime configurations without probing.
 */
typedef struct {
    uint16_t channelCount;      /* configured DIO channels (typically 16) */
    uint16_t pwmCapableMask;    /* bit N set ⇒ DIO_N has an OCMP mapping */
} CapabilitiesDioSummary;

/*
 * Streaming cap state for the currently-enabled channel set.
 * The cap formula constants (ISR ceiling, Type 1 aggregate max,
 * per-tick budget and overhead) are compile-time in the firmware
 * but emitted here so clients can predict caps for hypothetical
 * configurations locally without round-tripping.
 */
typedef struct {
    uint32_t maxFreqHz;         /* current-config cap; 0 if no channels */
    uint32_t isrMaxHz;
    uint32_t type1AggMaxHz;
    uint32_t tickBudget;
    uint32_t tickOverhead;
} CapabilitiesStreamingSummary;

/**
 * Fill out_summary with current streaming cap + formula constants.
 *
 * @param[out] out_summary must not be NULL.
 */
void Capabilities_GetStreamingSummary(CapabilitiesStreamingSummary* out_summary);

/**
 * Fill out_summary from the current tBoardConfig snapshot.
 * Safe to call from any task context — reads are idempotent.
 *
 * @param[out] out_summary must not be NULL.
 */
void Capabilities_GetAinSummary(CapabilitiesAinSummary* out_summary);

/**
 * Fill out_summary from the current tBoardConfig snapshot.
 *
 * @param[out] out_summary must not be NULL.
 */
void Capabilities_GetDioSummary(CapabilitiesDioSummary* out_summary);

#ifdef __cplusplus
}
#endif

#endif /* _DAQIFI_CAPABILITIES_H */
