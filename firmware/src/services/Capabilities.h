#ifndef _DAQIFI_CAPABILITIES_H
#define _DAQIFI_CAPABILITIES_H

/*
 * Capabilities.h — vendor-neutral capability schema for DAQiFi.
 *
 * See issue #327 for the full vision. This module is the firmware
 * reference implementation: clients issue CONFigure:CAPabilities:JSON?
 * and render themselves from the returned schema rather than
 * hard-coding board variants or channel counts.
 *
 * Coverage in V2:
 *   - Schema version byte (DAQIFI_CAPABILITIES_VERSION)
 *     also queryable via CONFigure:CAPabilities:APIVersion?
 *   - Full JSON rollup via CONFigure:CAPabilities:JSON? including:
 *       identity, channels[] (flat, kind-grouped), streaming,
 *       storage, power, transports, triggers
 *   - Capability fields embedded in DaqifiOutMessage protobuf
 *     (tags 70..75 — streaming cap subset; typed nested
 *      Capabilities message is a follow-up PR)
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
 * any other capability query. Increment on any BREAKING change —
 * field renames/removals, type changes, semantics changes on
 * existing fields, or layout reshape (flat vs grouped).
 *
 * Do NOT bump for additive changes — adding a new optional field
 * or a new channel `kind` / `signal_type` value is additive and
 * safe for older clients (which MUST ignore unknown fields).
 *
 * History:
 *   1 — Initial skeleton. Per-block subset queries, ain/dio
 *       summary objects. (#327 phase 1, never shipped publicly.)
 *   2 — Flat channels[] array with kind dispatch; per-channel
 *       signal_type / units / ranges / scaling / calibration;
 *       expanded streaming/storage/power/transports blocks;
 *       subset SCPI queries removed — rollup is the single source
 *       of truth. (#327 V1 release schema.)
 */
#define DAQIFI_CAPABILITIES_VERSION ((uint8_t)2)

/*
 * Topology summaries
 * ------------------
 * These structs feed the JSON emitter in SCPI_CapabilitiesJsonGet.
 * They capture board-wide counts/flags that are cheap to pre-compute
 * once per rollup; per-channel data is iterated directly from
 * BoardConfig in the emitter (no intermediate struct for each channel
 * because the per-channel JSON is only needed in the rollup path).
 */

/* AIn topology summary. Counts are for PUBLIC channels only —
 * internal monitoring (OBDiag) channels are excluded so a client
 * rendering UI doesn't see or control them. */
typedef struct {
    uint16_t publicChannelCount;   /* total public AIn channels */
    uint16_t type1Count;           /* dedicated-ADC (simultaneous) */
    uint16_t type2Count;           /* shared-ADC (muxed) */
    bool     hasAD7609;            /* any AD7609 (18-bit) channel public */
    uint8_t  primaryResolutionBits;/* 12 for MC12b, 18 for AD7609 */
} CapabilitiesAinSummary;

/* AOut topology summary. publicChannelCount is 0 on boards without a
 * DAC (NQ1), 8 on NQ3 (DAC7718). */
typedef struct {
    uint16_t publicChannelCount;
    uint8_t  resolutionBits;       /* 12 for DAC7718 */
    double   moduleMinVoltage;     /* from DAC7718ModuleConfig */
    double   moduleMaxVoltage;
} CapabilitiesAoutSummary;

/* DIO topology summary. */
typedef struct {
    uint16_t channelCount;         /* typically 16 */
    uint16_t pwmCapableMask;       /* bit N set ⇒ DIO_N supports PWM */
} CapabilitiesDioSummary;

/* Streaming capability. The cap formula constants come from
 * streaming.h compile-time defines; maxFreqHz is computed for the
 * currently-enabled channel set. */
typedef struct {
    uint32_t maxFreqHz;            /* 0 if no channels enabled */
    uint32_t isrMaxHz;
    uint32_t type1AggMaxHz;
    uint32_t tickBudget;
    uint32_t tickOverhead;
} CapabilitiesStreamingSummary;

/* Storage capability. Covers board-level storage support only
 * (structural). Current card presence and free space are runtime
 * state — clients that need them should query SYST:STORage:SD:*. */
typedef struct {
    bool     sdSupported;          /* SD hardware fitted to this board */
    uint32_t nvmSettingsSlots;     /* how many NVM config slots exist */
} CapabilitiesStorageSummary;

/* Power capability. Battery presence is board-topological; OTG
 * support reflects the BQ24297's available mode set. */
typedef struct {
    bool     batteryPresent;
    bool     externalPowerSupported;
    bool     otgSupported;
} CapabilitiesPowerSummary;

/* Transport capability. Flags for each supported physical channel. */
typedef struct {
    bool     usbSupported;
    bool     wifiSupported;
    bool     ethernetSupported;
    bool     serialDebugSupported;
} CapabilitiesTransportsSummary;

/* ------------------------------------------------------------------
 * Getters
 * ------------------------------------------------------------------
 * All getters read immutable board config + current runtime state.
 * Safe to call from any task context; no side effects. Any out_*
 * parameter must be non-NULL — all getters memset their output on
 * entry. */

void Capabilities_GetAinSummary      (CapabilitiesAinSummary*       out);
void Capabilities_GetAoutSummary     (CapabilitiesAoutSummary*      out);
void Capabilities_GetDioSummary      (CapabilitiesDioSummary*       out);
void Capabilities_GetStreamingSummary(CapabilitiesStreamingSummary* out);
void Capabilities_GetStorageSummary  (CapabilitiesStorageSummary*   out);
void Capabilities_GetPowerSummary    (CapabilitiesPowerSummary*     out);
void Capabilities_GetTransportsSummary(CapabilitiesTransportsSummary* out);

#ifdef __cplusplus
}
#endif

#endif /* _DAQIFI_CAPABILITIES_H */
