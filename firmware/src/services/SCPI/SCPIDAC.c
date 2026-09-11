#define LOG_LVL LOG_LEVEL_SCPI
#define LOG_MODULE LOG_MODULE_SCPI

#include "SCPIDAC.h"
#include "SCPIInterface.h"

// General
#include <stdlib.h>
#include <string.h>

// Harmony
#include "configuration.h"
#include "definitions.h"

// Project
#include "Util/StringFormatters.h"
#include "Util/Logger.h"
#include "state/data/BoardData.h"
#include "state/board/BoardConfig.h"
#include "state/runtime/BoardRuntimeConfig.h"
#include "state/runtime/AOutRuntimeConfig.h"
#include "HAL/DAC7718/DAC7718.h"
#include "HAL/Power/PowerApi.h"
#include "../daqifi_settings.h"

// Flag to track if DAC hardware has been initialized.
// volatile: written on whichever SCPI task initialises first and read by the
// other (CLAUDE.md "Atomicity & Concurrency Rules"). Both are plain aligned
// scalars, so the accesses themselves are atomic on PIC32MZ; volatile is what
// stops the compiler caching them or reordering the publish pair below.
// #980 item 3: this is now ALSO cleared whenever the 10V rail drops, so it
// means "a completed init is currently in effect", not just "has ever
// succeeded".
static volatile bool dacHardwareInitialized = false;

// Static DAC instance ID (returned from DAC7718_NewConfig).
// #980 item 1: RETAINED across a failed init AND across a power loss. The
// one-slot allocator (MAX_DAC7718_CONFIG == 1 in DAC7718.c) never frees a
// slot -- m_DAC7718ConfigCount only increments -- so clearing this back to
// 0xFF after we already own slot 0 would make every later DAC7718_NewConfig()
// return the table-full sentinel forever, permanently bricking the DAC. A
// failed DAC7718_Init() or a power cycle both leave this alone and only clear
// dacHardwareInitialized, so the next call retries Init() on the SAME slot.
static volatile uint8_t dacInstanceId = 0xFF; // 0xFF = uninitialized

// #980 item 1/4: claims the allocate/init/publish region below so at most one
// SCPI transport (USB or WiFi) runs it at a time. Without this, a naive
// "reuse dacInstanceId on retry" would let a second transport call
// DAC7718_Init() concurrently with the first -- and DAC7718_Init()'s GPIO
// reset pulse is not synchronized against a second, independent call to
// itself, only against DAC7718_ReadWriteReg(). A test-and-set, not a plain
// set: reading the flag and setting it is a read-modify-write, which is NOT
// atomic on PIC32MZ, hence the critical section around it below. If this ever
// leaks true the DAC is unusable for the rest of the boot, so every exit from
// the claimed region must clear it.
//
// This does NOT fully solve #980 item 4 (overlapping first-time init should
// BLOCK the loser rather than fail it) -- see issue #980 item 4 and #989 for
// why that larger, recursive-mutex change is deferred to a follow-up. What
// this claim DOES do is preserve today's existing loser behaviour (a
// transient execution error) unchanged while making the retry-on-failure
// path (item 1) safe against the same race.
static volatile bool dacInitInProgress = false;

// Static DAC configuration. #980 cleanup: both writers used to assign the
// same two pin constants on every call into a shared, writable static --
// `const` removes that redundant/competing write for free (and moves it from
// BSS to .rodata).
static const tDAC7718Config dacConfig = {
    .CS_Pin  = GPIO_PIN_RK0,    // CS on RK0
    .RST_Pin = GPIO_PIN_RJ13,   // CLR/RST on RJ13
};

// Helper function to ensure DAC hardware is initialized when power is up
static bool DAC_EnsureHardwareInitialized(void) {
    // #980 item 3: the power precondition is now checked on EVERY call,
    // ahead of the "already initialized" short-circuit below. The old order
    // returned true on the flag first, so once the DAC had initialized
    // successfully once, the device never re-enforced its own stated power
    // precondition -- a command issued after the 10V rail dropped would
    // still report success.
    const tPowerData* pPowerState = BoardData_Get(BOARDDATA_POWER_DATA, 0);
    if (pPowerState == NULL) {
        LOG_E("DAC_EnsureHardwareInitialized: Cannot get power state data");
        return false;
    }

    // POWERED_UP (1) has 10V rail, POWERED_UP_EXT_DOWN (2) does not have 10V rail
    if (pPowerState->powerState != POWERED_UP) {
        // Drop READY so the rail's return re-runs the full init sequence
        // (including the register write DAC7718_Init performs) rather than
        // trusting stale hardware state. dacInstanceId is deliberately left
        // alone -- see its declaration; the one slot cannot be handed back.
        dacHardwareInitialized = false;
        return false;
    }

    if (dacHardwareInitialized) {
        return true; // Already initialized and the rail is still up
    }

    // Get DAC configuration from board config and initialize hardware
    const tBoardConfig* pBoardConfig = BoardConfig_Get(BOARDCONFIG_ALL_CONFIG, 0);
    if (pBoardConfig == NULL || pBoardConfig->AOutModules.Size == 0) {
        return false;
    }

    // DAC7718 is only available on NQ3 variant
    if (pBoardConfig->BoardVariant != 3) {
        LOG_E("DAC_EnsureHardwareInitialized: DAC7718 only available on NQ3 (current: NQ%d)",
              pBoardConfig->BoardVariant);
        return false;
    }

    // Claim the allocate/init/publish region (see dacInitInProgress above).
    // Scalars only in the critical section -- no I/O, no logging, no blocking
    // call -- so it is a handful of instructions, same idiom as
    // DAC7718_NewConfig's counter guard.
    bool claimed;
    taskENTER_CRITICAL();
    if (dacInitInProgress) {
        claimed = false;
    } else {
        dacInitInProgress = true;
        claimed = true;
    }
    taskEXIT_CRITICAL();

    if (!claimed) {
        // Another transport is mid-init right now. Same outcome as today's
        // allocation-race loser (a transient execution error) -- making the
        // loser WAIT instead is #980 item 4, deferred (see the comment on
        // dacInitInProgress above).
        LOG_E("DAC_EnsureHardwareInitialized: initialization already in progress");
        return false;
    }

    // Re-check under the claim: the winner of a race may have finished
    // between our flag read above and our claim just now. Without this, a
    // redundant DAC7718_Init() would re-pulse RST and re-zero outputs a
    // concurrent caller already commanded.
    if (dacHardwareInitialized) {
        dacInitInProgress = false;
        return true;
    }

    if (dacInstanceId == 0xFF) {
        // No slot allocated yet -- allocate one. DAC7718_NewConfig hands the
        // single slot to exactly one caller; under the claim above, this is
        // the only caller that can reach it.
        uint8_t newInstanceId = DAC7718_NewConfig(&dacConfig);
        if (newInstanceId == 0xFF) {
            LOG_E("DAC_EnsureHardwareInitialized: Failed to allocate DAC configuration");
            dacInitInProgress = false;
            return false;
        }
        dacInstanceId = newInstanceId;
    }

    // #980 item 1: propagate DAC7718_Init()'s actual outcome instead of
    // assuming success. On failure, dacInstanceId is retained above (NOT
    // reset to 0xFF) so the NEXT call retries DAC7718_Init() on this SAME
    // slot instead of calling DAC7718_NewConfig() again -- which would hit
    // the one-slot allocator's table-full sentinel and brick the DAC
    // permanently. DAC7718_Init()'s sequence starts with the RST pulse, so
    // re-running it on a previously-failed id is safe to repeat.
    if (!DAC7718_Init(dacInstanceId, 1)) {
        LOG_E("DAC_EnsureHardwareInitialized: DAC7718_Init failed (id=%u); "
              "slot retained, retry permitted", (unsigned)dacInstanceId);
        dacInitInProgress = false;
        return false;
    }

    // #980 Qodo /agentic_review pass 3 (bug: "Power cycles leave the DAC
    // marked ready"): DAC7718_Init() above can take tens of ms (several SPI
    // polling loops, each with its own timeout), so the power reading taken
    // at the TOP of this function is stale by now. If the rail dropped
    // mid-init, a CONCURRENT caller on the other transport could already
    // have observed that and correctly cleared dacHardwareInitialized via
    // its own top-of-function check -- and this call publishing `true`
    // right after would CLOBBER that correct clear with a stale READY,
    // wrong until some later command happens to catch it. In the meantime a
    // real DAC write could reach DAC7718_ReadWriteReg while the analog rail
    // is actually down: the digital SPI bus and the 10V analog output can be
    // on separate supplies, so a digital transaction can still nominally
    // succeed while the requested voltage is not physically deliverable.
    // Re-validate with a FRESH read immediately before publishing, closing
    // the window this claim's own duration opened.
    pPowerState = BoardData_Get(BOARDDATA_POWER_DATA, 0);
    if ((pPowerState == NULL) || (pPowerState->powerState != POWERED_UP)) {
        dacHardwareInitialized = false;
        dacInitInProgress = false;
        return false;
    }

    dacHardwareInitialized = true;   // publish READY ...
    dacInitInProgress = false;       // ... then release the claim
    return true;
}

// Helper function to find DAC channel index
static size_t DAC_FindChannelIndex(uint8_t channelId) {
    AOutArray* pBoardConfigAOutChannels = BoardConfig_Get(BOARDCONFIG_AOUT_CHANNELS, 0);

    if (pBoardConfigAOutChannels == NULL) {
        return SIZE_MAX; // Invalid index
    }

    for (size_t i = 0; i < pBoardConfigAOutChannels->Size; i++) {
        if (pBoardConfigAOutChannels->Data[i].DaqifiDacChannelId == channelId) {
            return i;
        }
    }
    return SIZE_MAX; // Channel not found
}

// Helper function to convert voltage to DAC counts
// MaxVoltage is a software limit for clamping, HardwareFullScale determines actual output
// Resolution from board config supports different DAC variants (12-bit=4096, 14-bit=16384, 16-bit=65536)
static uint32_t DAC_VoltageToCounts(double voltage, const AOutModule* module) {
    // Access config directly to avoid struct alignment issues
    const double minVoltage = module->Config.DAC7718.MinVoltage;
    const double maxVoltage = module->Config.DAC7718.MaxVoltage;
    const double hardwareFullScale = module->Config.DAC7718.HardwareFullScale;
    const uint16_t resolution = module->Config.DAC7718.Resolution;

    // Validate configuration - prevent division by zero and invalid resolution
    if (hardwareFullScale <= 0.0) {
        LOG_E("DAC_VoltageToCounts: Invalid hardwareFullScale (%.2f), returning 0", hardwareFullScale);
        return 0;
    }
    if (resolution == 0) {
        LOG_E("DAC_VoltageToCounts: Invalid resolution (0), returning 0");
        return 0;
    }

    // Clamp to software-configured limits
    if (voltage < minVoltage) voltage = minVoltage;
    if (voltage > maxVoltage) voltage = maxVoltage;

    // Map voltage directly to DAC counts using hardware full scale
    // DAC outputs absolute voltage, not relative to minVoltage
    // Example: 4V command with 10V full scale -> 1638 counts -> 4V output
    double rawCounts = (voltage / hardwareFullScale) * (double)(resolution - 1);

    // Clamp to valid DAC range [0, resolution-1] to prevent overflow
    // This guards against edge cases and ensures compatibility with different DAC resolutions
    const uint32_t maxCounts = (uint32_t)(resolution - 1);
    if (rawCounts < 0.0) {
        rawCounts = 0.0;
    }
    if (rawCounts > (double)maxCounts) {
        rawCounts = (double)maxCounts;
    }

    // Round to nearest integer
    uint32_t counts = (uint32_t)(rawCounts + 0.5);

    return counts;
}

scpi_result_t SCPI_DACVoltageSet(scpi_t * context) {
    int channel;
    double voltage;
    AOutArray* pBoardConfigAOutChannels = BoardConfig_Get(BOARDCONFIG_AOUT_CHANNELS, 0);
    AOutModule* pDACModule = BoardConfig_Get(BOARDCONFIG_AOUT_MODULE, 0);

    if (pBoardConfigAOutChannels == NULL || pDACModule == NULL) {
        LOG_E("SCPI_DACVoltageSet: No DAC channels configured");
        return SCPI_RES_ERR;
    }

    // Ensure DAC hardware is initialized (lazy initialization when power is up)
    if (!DAC_EnsureHardwareInitialized()) {
        SCPI_ExecutionError(context, "SOUR:VOLT:LEV: DAC not initialized (device powered up?)");
        return SCPI_RES_ERR;
    }

    // Try to parse first parameter as double (works for both int and double)
    if (!SCPI_ParamDouble(context, &voltage, TRUE)) {
        return SCPI_RES_ERR;
    }

    // Try to parse second parameter as double (voltage)
    //
    // #874: ABSENT selects the one-parameter all-channel form;
    // PRESENT-but-unparseable must not -- see SCPI_OptionalParamDouble
    // (SCPIInterface.h). `SOUR:VOLT:LEV 5,BANANA` used to queue -104 and then
    // drive EVERY analog output to 5 V, reading the channel index as the
    // voltage. This is the one site in the family whose fall-through moves
    // real hardware.
    double voltage2;
    SCPI_OptionalParam voltOpt = SCPI_OptionalParamDouble(context, &voltage2);
    if (voltOpt == SCPI_OPT_BAD) {
        return SCPI_RES_ERR;
    }
    if (voltOpt == SCPI_OPT_PRESENT) {
        // Two parameters: first is channel (convert to int), second is voltage
        //
        // #877: the channel arrives here as a DOUBLE -- the first parameter is
        // parsed as one so the single-argument all-channel form can take a
        // voltage -- so it is narrowed TWICE before the lookup: double -> int
        // -> uint8_t. Reject outside [0,255] BEFORE either narrowing.
        // (uint8_t)256 is 0, so `SOUR:VOLT:LEV 256,5` resolved DAC channel 0,
        // passed the resolved-index guard below, and DROVE THAT OUTPUT TO 5 V
        // while returning OK. This is the one site in the truncation family
        // that moves real hardware.
        //
        // Written as a positive range test rather than `< 0 || > 255` so a NaN
        // first parameter -- which compares false against everything -- is
        // rejected too, and so the (int) cast, whose result is undefined for a
        // double outside int range, is never reached.
        //
        // Fractional values INSIDE the range keep their existing
        // truncate-toward-zero behaviour: `3.7` still selects channel 3. The
        // bounds are deliberately the closed [0.0, 255.0] and not the wider
        // (-1.0, 256.0) that would preserve truncation at both ends: `(int)`
        // truncates toward zero, so widening the LOW end to admit -0.5 would
        // make it select channel 0 -- a negative channel silently becoming a
        // real one, which is the alias this guard exists to stop. Nothing is
        // lost at the HIGH end either: no value in (255, 256) can name a DAC
        // channel on any variant (DaqifiDacChannelId is 0..7), so 255.5 was
        // already refused before this change -- only its error code moves from
        // the resolved-index path's -200 to -222.
        if (!(voltage >= 0.0 && voltage <= 255.0)) {
            LOG_E("SOUR:VOLT:LEV: channel out of range (max 255)");
            SCPI_ErrorPush(context, SCPI_ERROR_DATA_OUT_OF_RANGE);
            return SCPI_RES_ERR;
        }
        channel = (int)voltage;
        voltage = voltage2;

        size_t index = DAC_FindChannelIndex((uint8_t)channel);
        if (index >= pBoardConfigAOutChannels->Size) {
            return SCPI_RES_ERR;
        }

        // Convert voltage to DAC counts using configuration
        uint32_t counts = DAC_VoltageToCounts(voltage, pDACModule);

        // Clamp counts to valid 16-bit resolution for DAC7718
        uint16_t counts16 = (uint16_t)(counts & 0xFFFFU);

        // Get hardware channel number from board configuration and validate
        uint8_t hwChannel = pBoardConfigAOutChannels->Data[index].Config.DAC7718.ChannelNumber;
        if (hwChannel >= DAC7718_NUM_CHANNELS) {
            LOG_E("SCPI_DACVoltageSet: Invalid DAC7718 channel %u (max %u)", hwChannel, DAC7718_NUM_CHANNELS - 1);
            return SCPI_RES_ERR;
        }

        uint8_t dacRegister = (uint8_t)(DAC7718_REGISTER_OFFSET + hwChannel);
        // #980 Qodo /improve pass 2: DAC7718_ReadWriteReg's own return
        // (already honest -- UINT32_MAX on failure, unchanged by this PR)
        // was never checked at this call site either. A failed register
        // write means the shadow register was never loaded with the new
        // code, so calling UpdateLatch anyway would commit whatever was
        // ALREADY staged (stale data, not the requested voltage) --
        // report the failure instead.
        if (DAC7718_ReadWriteReg(dacInstanceId, 0, dacRegister, counts16) == UINT32_MAX) {
            SCPI_ExecutionError(context, "SOUR:VOLT:LEV: Failed to write DAC register");
            return SCPI_RES_ERR;
        }

        // #980 Qodo pre-merge review: DAC7718_UpdateLatch's return (added by
        // this PR) must be CHECKED here, not just given a type. The register
        // write above only loads the DAC's input shadow register -- nothing
        // reaches the physical output pin until the latch update fires. If
        // that fails, publishing the new voltage into BoardData and
        // returning SCPI_RES_OK would tell a reader of SOUR:VOLT:LEV?/
        // MEAS:VOLT:DC? that the requested value is live when the DAC output
        // is still whatever it was before -- the exact "assumed success"
        // shape #980 exists to close, one call site later. So: report the
        // failure, and do NOT touch BoardData -- the old commanded value
        // is still what is physically on the pin.
        if (!DAC7718_UpdateLatch(dacInstanceId)) {
            SCPI_ExecutionError(context, "SOUR:VOLT:LEV: Failed to update DAC latch");
            return SCPI_RES_ERR;
        }

        // Store commanded voltage in BoardData for readback -- only reached
        // once the latch update above confirms the value is actually live.
        AOutSample sample = {.Channel = (uint8_t)channel, .Voltage = voltage};
        BoardData_Set(BOARDDATA_AOUT_LATEST, index, &sample);

    } else {
        // One parameter: voltage for all channels.
        //
        // #980 Qodo /agentic_review pass 4: the DAC7718's load latch is
        // GLOBAL -- one LD-bit write commits EVERY channel's shadow register
        // at once (DAC7718_UpdateLatch -> ReadWriteReg(id, 0, 0,
        // 0b110000011000), DAC7718.c). So "abort the loop before the latch"
        // -- this branch's pass-2 shape, and the claim in its comment that
        // this "leaves every channel's physical output exactly where it
        // was" -- was WRONG: it left the channels already written holding an
        // UNCOMMITTED new value in their shadow registers, which the next
        // UpdateLatch from any UNRELATED command (a single-channel
        // SOUR:VOLT:LEV, CONF:DAC:UPDATE) then drove onto their physical
        // outputs -- while BoardData, never published by the aborted call,
        // still reported the old voltage. A client polling SOUR:VOLT:LEV?
        // read the old value off a pin that had silently moved minutes
        // later, as a side effect of a command that never named it.
        //
        // The invariant this branch owes the device, per channel, is:
        //     shadow register == physical output == BoardData
        // (true from boot: DAC7718_Init ends in a latch). Two ways to
        // restore it after a mid-loop write failure: pull the shadows back
        // down (re-write every already-staged channel with its prior
        // voltage) or push the outputs up (fire the one latch). The latch is
        // chosen -- ONE SPI frame instead of up to seven MORE, over a bus
        // that just failed, each of which could fail too and leave a more
        // mixed shadow state with no recourse short of a full DAC reset.
        //
        // Firing the latch is a no-op BY CONSTRUCTION for every channel this
        // loop did NOT successfully write: their shadow registers still hold
        // whatever value is already on their outputs. That holds for the
        // pre-SPI failure modes in DAC7718_ReadWriteReg (its own input
        // validation, and a DAC7718_Lock() failure -- both before CS is ever
        // asserted) and for a channel skipped without a write at all. The one
        // residual is a write that times out MID-FRAME, where CS was already
        // asserted and a truncated frame was clocked in: that channel's
        // shadow is indeterminate either way, and this shape at least
        // commits it NOW, with an error naming the channel, instead of
        // leaving it for an unrelated later command's latch to surface.
        //
        // So: try every channel, remember which ones actually took the
        // write, latch UNCONDITIONALLY (this is what drains any uncommitted
        // shadow state, so it must fire even when every write failed), and
        // publish BoardData ONLY for the channels that were actually staged.
        // Nothing false is ever published, and this command never leaves
        // uncommitted shadow state behind it for a later command to trip
        // over. An out-of-range hwChannel now counts as a per-channel
        // failure rather than a silent skip-and-report-OK, matching the
        // single-channel branch above, which already treats the identical
        // condition as SCPI_RES_ERR.
        //
        // Known residual (not closed by this shape, and not fixable inside
        // this loop): if DAC7718_UpdateLatch() itself fails, the staged
        // shadows stay uncommitted and a LATER latch can still surface them
        // -- restoring would mean writing over the same bus that just
        // failed. This is the same class the single-channel branch and
        // SCPI_DACUpdate already carry; closing it needs a shadow-resync /
        // latch-inhibit mechanism, deferred alongside #989 (needs NQ3
        // hardware to validate the reset arm). Also residual: no lock is
        // held across this loop, so a concurrent command on the other SCPI
        // transport can interleave a latch mid-loop -- the end state still
        // converges (this call's own latch and publish run last for what it
        // staged), but the transition is not atomic; folding that into #989
        // rather than adding a second locking mechanism here.
        uint32_t counts = DAC_VoltageToCounts(voltage, pDACModule);

        // Bound by the AOutArray's own capacity, not just its live Size, so
        // `staged[]` is provably in range regardless.
        size_t nChannels = pBoardConfigAOutChannels->Size;
        if (nChannels > MAX_AOUT_CHANNEL) {
            nChannels = MAX_AOUT_CHANNEL;
        }

        bool staged[MAX_AOUT_CHANNEL];
        uint32_t failedMask = 0;
        size_t failedCount = 0;

        for (size_t i = 0; i < nChannels; i++) {
            staged[i] = false;

            uint8_t hwChannel = pBoardConfigAOutChannels->Data[i].Config.DAC7718.ChannelNumber;

            // Validate hardware channel -- a FAILURE, not a silent skip (see
            // the block comment above). Nothing is written, so its shadow is
            // untouched.
            if (hwChannel >= DAC7718_NUM_CHANNELS) {
                LOG_E("SCPI_DACVoltageSet: Invalid DAC7718 channel %u (max %u)", hwChannel, DAC7718_NUM_CHANNELS - 1);
                failedMask |= (1UL << i);
                failedCount++;
                continue;
            }

            uint8_t dacRegister = DAC7718_REGISTER_OFFSET + hwChannel;
            // A failed write means this channel's shadow was not loaded with
            // the new code. Keep going rather than abort: an SPI failure is
            // bus-level, not channel-specific, so stopping here would deny
            // the remaining channels for a reason unrelated to them. The
            // failure is reported once, after the unconditional latch below.
            if (DAC7718_ReadWriteReg(dacInstanceId, 0, dacRegister, counts) == UINT32_MAX) {
                failedMask |= (1UL << i);
                failedCount++;
                continue;
            }

            staged[i] = true;
        }

        // UNCONDITIONAL -- see the block comment above for why this must
        // fire even when every write failed.
        if (!DAC7718_UpdateLatch(dacInstanceId)) {
            // Nothing is known to be live; publish nothing.
            SCPI_ExecutionError(context, "SOUR:VOLT:LEV: Failed to update DAC latches");
            return SCPI_RES_ERR;
        }

        // Publish ONLY the channels whose register write actually succeeded
        // -- the latch above confirms those, and only those, are physically
        // at the new voltage. A channel that failed keeps its prior
        // BoardData value, which is still what its output is at.
        for (size_t i = 0; i < nChannels; i++) {
            if (!staged[i]) {
                continue;
            }
            uint8_t channelId = pBoardConfigAOutChannels->Data[i].DaqifiDacChannelId;
            AOutSample sample = {.Channel = channelId, .Voltage = voltage};
            BoardData_Set(BOARDDATA_AOUT_LATEST, i, &sample);
        }

        if (failedCount > 0) {
            // Detail goes to the log -- SCPI_ExecutionError takes no
            // varargs, and per project policy the error queue carries the
            // code while SYST:LOG? carries the detail. Mask is by
            // BoardData/channel-list index, matching SOUR:VOLT:LEV? ordering.
            LOG_E("SOUR:VOLT:LEV: %u of %u channels not set (index mask 0x%02X); the rest are live at the new voltage",
                  (unsigned)failedCount, (unsigned)nChannels, (unsigned)failedMask);
            SCPI_ExecutionError(context, "SOUR:VOLT:LEV: Failed to write DAC register (some channels not set)");
            return SCPI_RES_ERR;
        }
    }

    return SCPI_RES_OK;
}

scpi_result_t SCPI_DACVoltageGet(scpi_t * context) {
    int channel;
    AOutArray* pBoardConfigAOutChannels = BoardConfig_Get(BOARDCONFIG_AOUT_CHANNELS, 0);

    if (pBoardConfigAOutChannels == NULL) {
        LOG_E("SCPI_DACVoltageGet: No DAC channels configured");
        return SCPI_RES_ERR;
    }

    StreamingRuntimeConfig *pStreamCfg = BoardRunTimeConfig_Get(
            BOARDRUNTIME_STREAMING_CONFIGURATION);
    uint8_t precision = (pStreamCfg != NULL) ? pStreamCfg->VoltagePrecision : 4;

    // Note: DAC7718 does not support hardware readback
    // Return last commanded voltage from BoardData
    // #874: a malformed channel argument must not silently answer with every
    // channel instead -- see SCPI_OptionalParamInt32 (SCPIInterface.h).
    SCPI_OptionalParam chanOpt = SCPI_OptionalParamInt32(context, &channel);
    if (chanOpt == SCPI_OPT_BAD) {
        return SCPI_RES_ERR;
    }
    if (chanOpt == SCPI_OPT_PRESENT) {
        // Get single channel
        // #877: reject before the (uint8_t) narrowing -- 256 would alias onto
        // DAC channel 0 and report ITS last commanded voltage as this
        // channel's. Same test as the setter above, on an int rather than a
        // double because this form parses the channel directly.
        //
        // Which also means this one does NOT cover a decimal token: libscpi
        // truncates `-0.5` to 0 before the callback sees it, so this test
        // passes and the reply is channel 0's (#880). The SETTER above is
        // immune only because its first parameter arrives as a double and is
        // range-checked in that form -- the accidental upside of a shape that
        // exists for a different reason.
        if (channel < 0 || channel > 255) {
            LOG_E("SOUR:VOLT:LEV?: channel out of range (max 255)");
            SCPI_ErrorPush(context, SCPI_ERROR_DATA_OUT_OF_RANGE);
            return SCPI_RES_ERR;
        }
        size_t index = DAC_FindChannelIndex((uint8_t)channel);
        if (index >= pBoardConfigAOutChannels->Size) {
            LOG_E("SCPI_DACVoltageGet: Invalid channel %d", channel);
            return SCPI_RES_ERR;
        }

        // Read last commanded voltage from BoardData
        AOutSample* pSample = (AOutSample*)BoardData_Get(BOARDDATA_AOUT_LATEST, index);
        if (pSample != NULL) {
            SCPI_ResultVoltage(context, pSample->Voltage, precision);
        } else {
            SCPI_ResultVoltage(context, 0.0, precision);
        }
    } else {
        // Get all channels
        for (size_t i = 0; i < pBoardConfigAOutChannels->Size; i++) {
            AOutSample* pSample = (AOutSample*)BoardData_Get(BOARDDATA_AOUT_LATEST, i);
            if (pSample != NULL) {
                SCPI_ResultVoltage(context, pSample->Voltage, precision);
            } else {
                SCPI_ResultVoltage(context, 0.0, precision);
            }
        }
    }

    return SCPI_RES_OK;
}

scpi_result_t SCPI_DACUseCalSet(scpi_t * context) {
    int useCal;

    if (!SCPI_ParamInt32(context, &useCal, TRUE)) {
        return SCPI_RES_ERR;
    }

    // TODO: Implement calibration preference setting

    return SCPI_RES_OK;
}

scpi_result_t SCPI_DACUseCalGet(scpi_t * context) {
    // TODO: Get calibration preference from NVM
    SCPI_ResultInt32(context, 0); // Default to factory calibration
    return SCPI_RES_OK;
}

scpi_result_t SCPI_DACUpdate(scpi_t * context) {
    // Ensure DAC hardware is initialized and instance ID is valid
    if (!DAC_EnsureHardwareInitialized() || dacInstanceId == 0xFF) {
        SCPI_ExecutionError(context, "CONF:DAC:UPDATE: DAC not initialized");
        return SCPI_RES_ERR;
    }

    // Update all DAC latches to reflect current values. This command's
    // entire purpose IS the latch update (same check as both SCPI_DACVoltageSet
    // branches above, added for the same reason -- see their comments).
    if (!DAC7718_UpdateLatch(dacInstanceId)) {
        SCPI_ExecutionError(context, "CONF:DAC:UPDATE: Failed to update DAC latches");
        return SCPI_RES_ERR;
    }
    return SCPI_RES_OK;
}
