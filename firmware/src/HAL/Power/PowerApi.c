/*! @file PowerApi.c
 *
 * This file implements the functions to manage power API
 */
#define LOG_LVL LOG_LEVEL_POWER
#define LOG_MODULE LOG_MODULE_POWER

#include "HAL/Power/PowerApi.h"
#include <string.h>  /* memset (#409 retained-RAM defensive init) */
#include "definitions.h"
#include "state/board/BoardConfig.h"
#include "state/data/BoardData.h"
#include "HAL/ADC.h"
#include "HAL/BQ24297/BQ24297.h"
#include "Util/Logger.h"
#include "../../services/wifi_services/wifi_manager.h"
#include "../../services/UsbCdc/UsbCdc.h"
#include "../../services/daqifi_settings.h"  // #454: load autoPowerOnUsb from NVM
#include "../../services/sd_card_services/sd_card_manager.h"
#include "driver/usb/usbhs/src/plib_usbhs_header.h"
#include <xc.h>
//typedef enum
//{
//    /* Source of clock is internal fast RC */
//    SYS_CLK_SOURCE_FRC,
//
//    /* Source of clock is internal fast RC multiplied by system PLL */
//    SYS_CLK_SOURCE_FRC_SYSPLL,
//
//    /* Source of clock is primary oscillator */
//    SYS_CLK_SOURCE_PRIMARY,
//
//    /* Source of clock is primary oscillator multiplied by
//    the System PLL value and divided by the divisor configured by software */
//    SYS_CLK_SOURCE_PRIMARY_SYSPLL,
//
//    /* Source of clock is secondary oscillator */
//    SYS_CLK_SOURCE_SECONDARY,
//
//    /* Source of clock is internal low power RC */
//    SYS_CLK_SOURCE_LPRC,
//
//    /* Source of clock is internal fast RC divided by the divisor
//    configured in software */
//    SYS_CLK_SOURCE_FRC_BY_16,
//
//    /* Source of clock is internal fast RC divided by the divisor
//    configured in software */
//    SYS_CLK_SOURCE_FRC_BY_DIV,
//
//    /* Source of clock is backup fast RC */
//    SYS_CLK_SOURCE_BKP_FRC,
//
//    /* Source of clock is USB PLL output configured in software */
//    SYS_CLK_SOURCE_UPLL,
//
//    /* Source of clock is none*/
//    SYS_CLK_SOURCE_NONE,
//
//} CLK_SOURCES_SYSTEM;
#define SYS_CLK_CONFIG_FREQ_ERROR_LIMIT     10
#define SYS_CLK_CONFIG_PRIMARY_XTAL         24000000ul
/* Battery Management Thresholds
 * - BATT_EXT_DOWN_TH (15%): Below this, external power rails disabled to conserve battery
 * - BATT_LOW_TH (5%): Critical level - device must shut down
 * - BATT_HYST (10%): Hysteresis prevents oscillation at threshold boundaries
 */
#define BATT_EXT_DOWN_TH 15.0  /* External power disabled below this */
#define BATT_LOW_TH 5.0        /* Critical shutdown threshold */
/* #564: chargePct sentinel — no valid ADC reading yet. The low/critical
 * comparisons guard on battVoltageValid (chargePct holds UNKNOWN exactly when
 * !battVoltageValid), so a stale UNKNOWN never reads as a dead battery. */
#define BATT_CHARGE_UNKNOWN (-1)
#define BATT_HYST 10.0         /* Must charge 10% above threshold to re-enable */

//! Pointer to a data structure for storing the configuration data
static tPowerConfig *pConfig;
//! Pointer to a data structure with all the data fields
static tPowerData *pData;
//! Pointer to a data structure with all the write variable data fields
static tPowerWriteVars *pWriteVariables;

/* #1071: power-state handoff across SYSTem:REboot.
 *
 * SYSTem:REboot ends in RCON_SoftwareReset() (SCPI_Reset), and Power_Init()
 * wipes tPowerData (#409), so a reboot used to come back in STANDBY with WiFi
 * and the front end unpowered. SCPI_Reset() now calls Power_ArmRebootRestore()
 * just before the reset, and the next Power_Init() consumes the handoff once.
 * *RST shares SCPI_Reset but does not arm the handoff (see SCPI_Reset).
 *
 * Which boots see it. Every boot consumes and clears the block, so a boot that
 * does not directly follow an armed reset finds no valid handoff and comes up
 * in STANDBY -- true without qualification for a power-on, brown-out or PICkit
 * reflash, none of which this SRAM class survives. It is NOT true for an MCLR
 * on a bootloader-linked image (#1081 adversarial audit round 1, falsifying an
 * earlier revision of this comment and of the PowerApi.h doc, which both
 * claimed MCLR unconditionally came up in STANDBY like the other three). MCLR
 * does not clear this SRAM -- the #409 note below relies on exactly that
 * survival for pData/pWriteVariables -- and the bootloader's forced-entry
 * check runs, and waits out an unconditional 2 s window, before the
 * application (and this file's Power_ConsumeRebootRestore()) ever executes:
 * BOOTLOADER_LEGACY is undefined project-wide, so Bootloader_Tasks() always
 * calls ForceBootloadFunc (bootloader.c:373-384) instead of the #ifdef'd
 * legacy trigger check, and that callback is APP_Bootloader_ForceEvent, whose
 * body ends in an unconditional DelayMs(2000) (bootloader/firmware/src/app.c:
 * 131-143) before it returns and lets the bootloader decide whether to jump to
 * the application. An MCLR asserted after an armed SYSTem:REboot but inside
 * that window leaves the handoff intact, so the application boot that
 * eventually follows -- not the one directly after the armed reset -- still
 * consumes and restores it. The outcome stays bounded: only a power-up is
 * ever armed (see "What it carries" below), so a stale replay is gated by the
 * normal STANDBY power-up path like any other request, never an unsafe or
 * unbounded state, and it needs physical MCLR/ICSP access inside that narrow
 * window. A firmware update through the USB bootloader is a related but
 * separate case, and IS one where a boot that does not directly follow the
 * armed reset still restores. The bootloader never writes the two trusted
 * words (see Placement), so when the bootloader entered from an armed
 * SYSTem:REboot stays for an update instead of jumping straight to the
 * application, the newly loaded application's first boot consumes the
 * handoff and restores the pre-reboot power state. That is benign, since the
 * replay is one request that the state machine gates like any other, but that
 * boot is a restore, not a STANDBY boot.
 *
 * The handoff is armed explicitly rather than inferred from the RCON reset
 * cause. On a bootloader-linked image the USB bootloader runs first on every
 * reset and clears RCON.SWR before it jumps here (Bootloader_Initialize,
 * bootloader/.../framework/bootloader/src/bootloader.c:318-320), so an SWR
 * gate would never fire on a released unit. The RCON flags are also
 * hardware-set and never hardware-cleared (DS60001320H Register 6-1), so a
 * set SWR says nothing about which reset came last.
 *
 * What it carries. The request to replay, and the SYSTem:POWer:AUTO:EXTernal
 * setting (autoExtPowerEnabled) at arm time. That setting is runtime-only and
 * Power_Init() resets it to 1 on every boot. Left to that reset, a board
 * restored into POWERED_UP_EXT_DOWN with auto recovery off would come back
 * with it on, and the next Power_HandlePoweredUpExtDownState() pass
 * (Power_Tasks() runs the state machine once per 1000 ms) would turn EN_5_10V
 * on and move it to POWERED_UP whenever USB power was present or the battery
 * read 25 % or more, undoing the user's AUTO:EXTernal 0 (#1081 adversarial
 * audit, round 1). Before #1071 a reboot could not reach that gate on its
 * own, since it always came back in STANDBY. The setting rides only with an
 * armed handoff, as part of the power state being restored: a reboot headed
 * to STANDBY, and *RST, still come back with the default. A carried 0 can
 * only keep EN_5_10V off, as that recovery is all it gates, so it is as
 * benign as the request on the bootloader-update path described above.
 *
 * Placement. `persistent` keeps our crt0 from clearing the block (XC32
 * DS50002799E 18.2.8) and implies `coherent`, so the arming stores bypass the
 * write-back D-cache. XC32 then requires a 16-byte-aligned address; this is
 * the same construct as force_bootloader_flag, one cache line below it. The
 * bootloader's startup runs before ours, and in the shipped
 * usb_bootloader.X.production.hex every .dinit record lies below 0x80000A30,
 * its stack starts at 0x8007FFE8 and grows down (its main() saves ra at
 * 0x8007FFE4, so words 0-1 here are padding), and nothing stores at or above
 * 0x8007FFE8 except the flag. The trusted words 2-3 sit at 0x8007FFE8 and
 * 0x8007FFEC. A bootloader that did write them would break the check word,
 * which reads as "not armed" and falls back to STANDBY, never to a wrong
 * state. (An unaligned `noload` slot above the flag failed to link this image
 * with xc32-ld v4.60: exit 5, no diagnostic.) */
#define POWER_REBOOT_HANDOFF_ADDR        (FORCE_BOOTLOADER_FLAG_ADDR - 16)
#define POWER_REBOOT_HANDOFF_MAGIC       0x1071B007u
#define POWER_REBOOT_HANDOFF_MAGIC_WORD  2u  /* words 0-1 are padding, never read */
#define POWER_REBOOT_HANDOFF_REQ_WORD    3u  /* payload in 15:0, complement in 31:16 */
/* Payload bits 7:0 hold the POWER_STATE_REQUEST to replay, DO_POWER_UP or
 * DO_POWER_UP_EXT_DOWN (no request value reaches bit 8). Bit 8 is set when
 * SYSTem:POWer:AUTO:EXTernal was 0; clear means the default, so a missing bit
 * never switches recovery off. Bits 15:9 are zero. The complement covers all
 * sixteen bits, so a flipped flag fails the check like any other corruption,
 * and a payload with any other bit set is rejected as not armed, as a flagged
 * payload is by an image that predates the flag. */
#define POWER_REBOOT_HANDOFF_AUTOEXT_OFF 0x0100u

static volatile uint32_t sRebootHandoff[4]
    __attribute__((persistent, coherent, address(POWER_REBOOT_HANDOFF_ADDR)));

void Power_ArmRebootRestore(void) {
    /* Replay where the board is HEADED, not only where it is: a request the
     * power task has not acted on yet wins over powerState. SYSTem:POWer:STATe
     * 0 only posts DO_POWER_DOWN, and Power_UpdateState() acts on it at its
     * next pass (Power_Tasks() gates it at 1000 ms), so a power-down sent just
     * before SYSTem:REboot can still read POWERED_UP here. Replaying that
     * would bring back up a unit the user had just switched off.
     *
     * The request is read before powerState. Every handler that consumes a
     * request writes powerState first and clears the request after, so a
     * NO_CHANGE read here means powerState already reflects it. Each read is
     * one aligned 32-bit load, atomic on PIC32MZ.
     *
     * SCPI_Reset() calls this inside the critical section that ends in
     * RCON_SoftwareReset(), after its settle delay (Qodo /agentic_review
     * finding on PR #1081), so no task can post a request between these
     * reads and the reset: a power-down posted at any point before the
     * reset is seen here, not replaced on the next boot by the power-up it
     * followed. */
    const POWER_STATE_REQUEST pending = pData->requestedPowerState;
    POWER_STATE_REQUEST replay = NO_CHANGE;   /* headed to STANDBY: none */

    if (pending == DO_POWER_UP || pending == DO_POWER_UP_EXT_DOWN) {
        replay = pending;
    } else if (pending == NO_CHANGE) {
        const POWER_STATE now = pData->powerState;
        if (now == POWERED_UP) {
            replay = DO_POWER_UP;
        } else if (now == POWERED_UP_EXT_DOWN) {
            replay = DO_POWER_UP_EXT_DOWN;
        }
    }
    /* else DO_POWER_DOWN: headed to STANDBY, nothing to replay. */

    if (replay == NO_CHANGE) {
        /* Leave the block exactly as Power_ConsumeRebootRestore() does, so
         * the next boot is the plain STANDBY boot every reset had before
         * #1071. Magic first, so it never vouches for the request word. */
        sRebootHandoff[POWER_REBOOT_HANDOFF_MAGIC_WORD] = 0;
        sRebootHandoff[POWER_REBOOT_HANDOFF_REQ_WORD] = 0;
    } else {
        /* Carry SYSTem:POWer:AUTO:EXTernal with the request (see "What it
         * carries"). autoExtPowerEnabled is a bool, one byte load, and once
         * Power_Init() has returned its only writer is SCPI_SetAutoExtPower(),
         * task code that cannot run inside the caller's critical section, so
         * this reads the setting the reset leaves behind. */
        uint32_t payload = (uint32_t)replay & 0xFFu;
        if (!pData->autoExtPowerEnabled) {
            payload |= POWER_REBOOT_HANDOFF_AUTOEXT_OFF;
        }
        /* Request word before the magic, so the magic never vouches for a
         * stale request word. */
        sRebootHandoff[POWER_REBOOT_HANDOFF_REQ_WORD] =
                payload | ((~payload & 0xFFFFu) << 16);
        sRebootHandoff[POWER_REBOOT_HANDOFF_MAGIC_WORD] =
                POWER_REBOOT_HANDOFF_MAGIC;
    }
    /* The caller resets straight after this returns, with no settle delay in
     * between. The block is coherent (uncached), so these stores bypass the
     * D-cache, and SYNC makes the core complete them before any later load or
     * store, the RSWRST access in RCON_SoftwareReset() that triggers the
     * reset included. */
    _sync();
}

/* Runs on every boot and always disarms the handoff, so it can only ever apply
 * to the boot straight after an armed reset. The replay is a request, handled
 * by the STANDBY state machine exactly like SYSTem:POWer:STATe: Power_Up()
 * sequences the rails and Power_HasSufficientPower() still gates it. */
static void Power_ConsumeRebootRestore(void) {
    const uint32_t magic = sRebootHandoff[POWER_REBOOT_HANDOFF_MAGIC_WORD];
    const uint32_t word = sRebootHandoff[POWER_REBOOT_HANDOFF_REQ_WORD];

    sRebootHandoff[POWER_REBOOT_HANDOFF_MAGIC_WORD] = 0;
    sRebootHandoff[POWER_REBOOT_HANDOFF_REQ_WORD] = 0;

    if (magic != POWER_REBOOT_HANDOFF_MAGIC ||
        (word >> 16) != (~word & 0xFFFFu)) {
        return;  /* not armed (see "Which boots see it" above) */
    }
    const uint32_t payload = word & 0xFFFFu;
    /* Clear only the AUTO:EXT flag and compare the rest as an integer, before
     * any cast to the enum. That admits exactly four payloads, DO_POWER_UP and
     * DO_POWER_UP_EXT_DOWN each with the flag clear or set; any other bit set
     * leaves a value that is neither request. */
    const uint32_t req = payload & ~POWER_REBOOT_HANDOFF_AUTOEXT_OFF;
    if (req != (uint32_t)DO_POWER_UP && req != (uint32_t)DO_POWER_UP_EXT_DOWN) {
        return;  /* only a power-up is ever armed; anything else is invalid */
    }
    pData->requestedPowerState = (POWER_STATE_REQUEST)req;
    /* #1081 adversarial audit round 1: put back the AUTO:EXTernal setting the
     * board had when it was armed (see "What it carries"). This overrides the
     * default Power_Init() set before calling here, and every return above
     * leaves that default, so an unarmed or corrupt handoff still gives the
     * plain STANDBY boot with auto recovery on. A clear flag restores the
     * default too, so a board rebooted with the setting untouched recovers
     * exactly as before. */
    pData->autoExtPowerEnabled =
            ((payload & POWER_REBOOT_HANDOFF_AUTOEXT_OFF) == 0u);
    /* #454: latch the power-up as the AUTOOn promote does when it issues its
     * own DO_POWER_UP. The replay bypasses that path, because
     * Power_HandleStandbyState() only promotes on NO_CHANGE. Unlatched, the
     * first manual power-off after the reboot would be undone with AUTOOn on
     * and VBUS present: a button long-press posts DO_POWER_DOWN without
     * setting the latch (Button_Tasks, UI.c), and the STANDBY pass that
     * executes it would promote the board straight back up. */
    pData->autoPromotedThisVbusSession = true;
}

///*! 
// * Funtion to write in power channel
// */
//static void Power_Write( void );
/*!
 * Function to active power capabilities
 * @param enableExtPower If true, enable 5V/10V external power. If false, keep external power disabled.
 */
static void Power_Up(bool enableExtPower);
/*!
 * Function to turn off power capabilities
 */
static void Power_Down(void);
/*!
 * Function to upate power state
 */
static void Power_UpdateState(void);
/*!
 * Function to update charge percentage
 */
static void Power_UpdateChgPct(void);
/*!
 * Function to update power status from GPIO and ADC (no I2C)
 */
static void Power_UpdateStatusFromGPIO(void);
/*!
 * Function to update the configuration
 */
static void Power_Update_Settings(void);

// Removed generic GPIO functions - using Harmony macros instead

bool ReadGpioPinStateLatched(GPIO_PORT port, uint32_t mask) {
    return GPIO_PortLatchRead(port)& (1 << mask);
}

void Power_Init(
        tPowerConfig *pInitConfig,
        tPowerData *pInitData,
        tPowerWriteVars *pInitVars) {
    pConfig = pInitConfig;
    pData = pInitData;
    pWriteVariables = pInitVars;

    /* Defensive zero-init for retained-RAM safety (#409). tPowerData and
     * tPowerWriteVars live in COHERENTBSS / kseg0 best-fit .bss.* — neither
     * is zeroed by crt0, so values survive MCLR / IPE flash unless we wipe
     * them here. */
    memset(pData, 0, sizeof(*pData));
    memset(pWriteVariables, 0, sizeof(*pWriteVariables));

    // NOTE: This is called before the RTOS is running.
    // Don't call any RTOS functions here!

    /* Auto external power recovery (SYSTem:POWer:AUTO:EXTernal) is on by
     * default at every boot; the setting is never saved to NVM. A #1071
     * reboot restore puts back the pre-reboot value, so this default must be
     * set before the Power_ConsumeRebootRestore() call below. */
    pData->autoExtPowerEnabled = true;

    /* #454: seed autoPowerOnUsb from NVM-persisted TopLevelSettings.
     * Power_Init runs AFTER app_freertos.c's TopLevelSettings load
     * (line 465), so the latest NVM value is already in NVM and we
     * just re-fetch it here.  On load failure (uninitialized NVM),
     * fall back to the safe default (false).  pData->autoPromotedThis
     * VbusSession was already zeroed by the memset above. */
    {
        DaqifiSettings nvm;
        memset(&nvm, 0, sizeof(nvm));
        if (daqifi_settings_LoadFromNvm(DaqifiSettings_TopLevelSettings, &nvm)) {
            pData->autoPowerOnUsb = nvm.settings.topLevelSettings.autoPowerOnUsb;
        } else {
            pData->autoPowerOnUsb = false;
        }
    }

    /* #1071: re-request the power state an armed SYSTem:REboot left behind
     * (*RST does not arm), with its AUTO:EXTernal setting. Plain stores are
     * enough: the power task and the SCPI transports, which read and write
     * these fields, are created by app_TasksCreate() after this returns. */
    Power_ConsumeRebootRestore();

    BQ24297_InitHardware(
            &pConfig->BQ24297Config,
            &pWriteVariables->BQ24297WriteVars,
            &(pData->BQ24297Data));

    // CRITICAL: Force 3.3V rail ON regardless of initial config
    // The microcontroller is running, so 3.3V must already be on
    // This ensures it stays on during USB disconnect.
    //
    // Order matters — RH12 must NEVER be driven LOW (see Power_Write
    // for the full rationale). Set the latch HIGH first while the pin
    // is still input (no pin change yet), THEN enable output, so the
    // pin transitions HiZ → output-HIGH directly with no LOW glitch.
    PWR_3_3V_EN_Set();           // LATH bit 12 = 1 (no pin change yet — still input)
    PWR_3_3V_EN_OutputEnable();  // TRIS bit 12 = 0 (output, drives high)
    pWriteVariables->EN_3_3V_Val = true;
    
    // Initialize other power pins to their default states
    // Note: These would need their own macros if they exist
    // For now, keeping the basic initialization
}

void Power_Tasks(void) {
    /* Initialize battery management if needed */
    if (pData->BQ24297Data.initComplete == false) {
        BQ24297_Config_Settings();
    }

    /* Manage IINLIM state machine — runs every Power_Tasks() call (~100ms)
     * Detects VBUS edges and adjusts IINLIM based on USB enumeration status */
    if (pData->BQ24297Data.initComplete) {
        USBHS_VBUS_LEVEL vbusLevel = PLIB_USBHS_VBUSLevelGet(USBHS_ID_0);
        bool vbusPresent =
            (vbusLevel == USBHS_VBUS_BELOW_VBUSVALID) ||
            (vbusLevel == USBHS_VBUS_VALID);
        BQ24297_ManageIINLIM(vbusPresent);
    }

    /* #193: Interrupt-driven BQ24297 fault monitoring.
     * The INT pin (RA4) change-notification ISR sets intFlag on any fault /
     * charge-status / power-good change. Consume it here (~100ms cadence) for
     * an immediate status refresh. Clear the flag BEFORE the I2C reads so an
     * INT that fires during the read is not lost (it stays set and is handled
     * next cycle). intFlag is a volatile bool — single-byte atomic on PIC32MZ,
     * one ISR writer / one task clearer — so no critical section is needed.
     * I2C stays task-context and mutex-protected inside the read functions. */
    if (pData->BQ24297Data.initComplete && pData->BQ24297Data.intFlag) {
        pData->BQ24297Data.intFlag = false;
        BQ24297_UpdateStatus();
        BQ24297_UpdateBatteryStatus();
    }

    /* #193 fallback: periodic REG09 fault refresh (~5s) in case a CN edge was
     * missed. Cheap belt-and-suspenders — the INT path above handles the
     * common case within ~100ms. */
    static TickType_t lastFaultPollTime = 0;
    TickType_t nowFault = xTaskGetTickCount();
    if (pData->BQ24297Data.initComplete &&
        (nowFault - lastFaultPollTime) >= pdMS_TO_TICKS(5000)) {
        lastFaultPollTime = nowFault;
        BQ24297_UpdateStatus();
        BQ24297_UpdateBatteryStatus();
    }

    /* Update power state at 1 second intervals */
    static TickType_t lastUpdateTime = 0;
    TickType_t currentTime = xTaskGetTickCount();
    if ((currentTime - lastUpdateTime) >= pdMS_TO_TICKS(1000)) {
        lastUpdateTime = currentTime;
        Power_UpdateState();
    }
}

void Power_USB_Sleep_Update(bool sleep) {
    pData->USBSleep = sleep;
}

void Power_Write(void) {
    // Current power state values

    bool EN_3_3V_Val_Current;
    bool EN_5_10V_Val_Current;
    bool EN_12V_Val_Current;
    bool EN_Vref_Val_Current;

    EN_3_3V_Val_Current = ReadGpioPinStateLatched(pConfig->EN_3_3V_Ch, pConfig->EN_3_3V_Bit);
    EN_5_10V_Val_Current = ReadGpioPinStateLatched(pConfig->EN_5_10V_Ch, pConfig->EN_5_10V_Bit);
    EN_12V_Val_Current = ReadGpioPinStateLatched(pConfig->EN_12V_Ch, pConfig->EN_12V_Bit);
    EN_Vref_Val_Current = ReadGpioPinStateLatched(pConfig->EN_Vref_Ch, pConfig->EN_Vref_Bit);

    // Check to see if we are changing the state of this power pin
    if (EN_3_3V_Val_Current != pWriteVariables->EN_3_3V_Val) {

        // CRITICAL: Never drive RH12 LOW — even momentarily.
        // RH12 (PWR_3_3V_EN) is held high externally by:
        //   - USB power (forces EN high while VBUS present)
        //   - Power-button press (pulses EN high)
        //   - MCU output-high (firmware self-hold)
        // and is pulled low by an external pulldown only when all three
        // sources release. The MCU's only legal moves are output-HIGH or
        // input (HiZ). A LOW pulse from the MCU could short an active
        // external source and disturb the rail/latch.
        //
        // Subtle ordering: at cold boot LATH bit 12 = 0 (initial 0xa114).
        // If we did OutputEnable first, the pin would transition HiZ →
        // output-LOW for one instruction before _Set() drives it high —
        // a brief but real LOW glitch on RH12. Set the latch HIGH FIRST
        // (no-op while pin is still input), THEN enable output, so the
        // pin transitions HiZ → output-HIGH directly.
        if (pWriteVariables->EN_3_3V_Val) {
            PWR_3_3V_EN_Set();           // LATH bit 12 = 1 (still input — no pin change yet)
            PWR_3_3V_EN_OutputEnable();  // TRIS bit 12 = 0 (output, drives high)
        } else {
            // Disable: HiZ. Pulldown drops the line if no other source
            // (USB / button / MCU) is asserting it. Never drive low.
            PWR_3_3V_EN_InputEnable();
        }

    }

    // Check to see if we are changing the state of these power pins
    if (EN_5_10V_Val_Current != pWriteVariables->EN_5_10V_Val) {
        // PWR_5V_EN uses Port D, bit 0
        if (pWriteVariables->EN_5_10V_Val) {
            PWR_5V_EN_OutputEnable();
            PWR_5V_EN_Set();
        } else {
            PWR_5V_EN_Clear();
        }
    }
    
    if (EN_12V_Val_Current != pWriteVariables->EN_12V_Val) {
        // PWR_12V_EN uses Port H, bit 15
        if (pWriteVariables->EN_12V_Val) {
            PWR_12V_EN_OutputEnable();
            PWR_12V_EN_Set();
        } else {
            PWR_12V_EN_Clear();
        }
    }

    // Check to see if we are changing the state of this power pin
    if (EN_Vref_Val_Current != pWriteVariables->EN_Vref_Val) {
        // PWR_VREF_EN uses Port J, bit 15
        if (pWriteVariables->EN_Vref_Val) {
            PWR_VREF_EN_OutputEnable();
            PWR_VREF_EN_Set();
        } else {
            PWR_VREF_EN_Clear();
        }
    }

}

static void Power_Up(bool enableExtPower) {
    
    /* Wait for battery management initialization with timeout
     * Prevents indefinite blocking if BQ24297 fails to initialize
     * Timeout: 5 seconds (50 x 100ms)
     */
    int initTimeout = 50;
    while (!pData->BQ24297Data.initComplete && initTimeout > 0) {
        vTaskDelay(pdMS_TO_TICKS(100));
        initTimeout--;
    }
    
    if (initTimeout == 0) {
        LOG_E("Power_Up: BQ24297 initialization timeout - proceeding anyway");
    }

    /* Power sequencing delay - allows voltage regulators to stabilize */
    vTaskDelay(pdMS_TO_TICKS(50));

    /* Enable 3.3V rail (critical - MCU power) */
    pWriteVariables->EN_3_3V_Val = true;
    Power_Write();
    
    /* 5V/10V External Power Rail Decision
     * Enable when:
     *   - User requested (enableExtPower=true) AND
     *   - Either external power present OR battery sufficient
     * Keep disabled when:
     *   - User wants conservation mode (enableExtPower=false) OR
     *   - Battery too low for safe operation
     */
    if (enableExtPower) {
        /* User wants external power - check if we should enable it
         * Use GPIO/ADC to get current status (no I2C) */
        Power_UpdateStatusFromGPIO();
        bool hasExternalPower = pData->BQ24297Data.status.pgStat;

        if (hasExternalPower) {
            /* External power present - always safe to enable */
            pWriteVariables->EN_5_10V_Val = true;
        } else if (!pData->battVoltageValid || pData->chargePct >= BATT_EXT_DOWN_TH) {
            /* Battery has sufficient charge, OR the VBATT ADC hasn't validated
             * yet (cold boot) so chargePct is a stale 0% — don't refuse the rail
             * and demote to EXT_DOWN on phantom data.  Power-up already required
             * BQ vsysStat to clear, so the battery is above SYS_MIN here. #564. */
            pWriteVariables->EN_5_10V_Val = true;
        } else {
            /* Battery too low - don't enable external power to avoid immediate re-transition */
            pWriteVariables->EN_5_10V_Val = false;
        }
    } else {
        /* User explicitly wants external power disabled (state 2) */
        pWriteVariables->EN_5_10V_Val = false;
    }
    Power_Write();
    vTaskDelay(pdMS_TO_TICKS(50));

    // 12V Enable (set low to turn on, set as input (or high if configured
    // as open collector) to turn off)
    pWriteVariables->EN_12V_Val = false;
    Power_Write();
    vTaskDelay(pdMS_TO_TICKS(50));

    // Vref Enable
    pWriteVariables->EN_Vref_Val = true;
    Power_Write();
    vTaskDelay(pdMS_TO_TICKS(50));

    // Set power state based on actual hardware outcome
    // If we requested external power but couldn't enable it due to low battery,
    // we should be in POWERED_UP_EXT_DOWN state to reflect reality
    if (enableExtPower && pWriteVariables->EN_5_10V_Val) {
        // External power was requested AND successfully enabled
        pData->powerState = POWERED_UP;
    } else {
        // External power was either not requested OR couldn't be enabled
        pData->powerState = POWERED_UP_EXT_DOWN;
    }
    // Don't reset requestedPowerState here - let the state machine handle it
}

void Power_Down(void) {

    /* Warn if SD card operations still in progress — data may be lost */
    if (sd_card_manager_IsBusy()) {
        LOG_E("[POWER] Power_Down with SD busy - potential data loss");
    }

    /* Disable external power rails */
    pWriteVariables->EN_5_10V_Val = false;   /* 5V/10V off */
    pWriteVariables->EN_12V_Val = true;      /* 12V off (inverted logic) */
    pWriteVariables->EN_Vref_Val = false;    /* ADC reference off */
    
    /* 3.3V rail handling - special case
     * Setting to false triggers HiZ mode in Power_Write()
     * This allows pulldown resistor to control the rail
     * Also enables button-based power control
     */
    pWriteVariables->EN_3_3V_Val = false;
    Power_Write();  /* Executes HiZ transition */

    pData->powerState = STANDBY;
    pData->requestedPowerState = NO_CHANGE;
}

/*
 * Update power status from GPIO and ADC (no I2C)
 *
 * Replaces I2C polling with:
 *   - USB VBUS detection for external power (pgStat equivalent)
 *   - ADC battery voltage for vsysStat equivalent
 *   - BATT_MAN_STAT GPIO for charging status
 *
 * This eliminates I2C bus hang risk during normal operation.
 * The previous I2C polling (~70 reads/sec on USB power) caused intermittent
 * hangs in PowerAndUITask, leading to LED unresponsiveness (issue #172).
 *
 * Future: Issue #173 tracks adding interrupt-driven I2C for scenarios that
 * require BQ24297 register access (config changes, detailed diagnostics).
 */
static void Power_UpdateStatusFromGPIO(void) {
    /* Update battery voltage and charge percentage from ADC */
    Power_UpdateChgPct();

    /* Update external power status from hardware VBUS detection
     * Read directly from USB hardware register - more reliable than software events
     * which may not trigger correctly for wall chargers without USB data lines.
     *
     * Use USBHS_VBUS_BELOW_VBUSVALID threshold (~4.0V) instead of USBHS_VBUS_VALID
     * (~4.75V) to handle wall chargers with minor voltage sag. This aligns better
     * with BQ24297's pgStat threshold (3.88V) for external power detection. */
    USBHS_VBUS_LEVEL vbusLevel = PLIB_USBHS_VBUSLevelGet(USBHS_ID_0);
    bool hasExternalPower = (vbusLevel >= USBHS_VBUS_BELOW_VBUSVALID);
    pData->BQ24297Data.status.pgStat = hasExternalPower;

    /* vsysStat — the BQ24297's authoritative low-battery signal (REG08 bit0:
     * 1 = BAT < VSYSMIN 3.0V). #564: read it over I2C; do NOT synthesize it from
     * the PIC ADC battVoltage. battVoltage reads a stale 0 before the ADC samples
     * (cold boot / STANDBY), which made this line force vsysStat=1 → a healthy
     * 3.97V battery looked dead → Power_HasSufficientPower() refused battery-only
     * power-up (USB masked it via pgStat). The BQ correctly reports the real
     * battery. Mutex-protected, cheap; on a read failure keep the last value
     * rather than clobbering with bad data. #564: gate on initComplete — before
     * the BQ driver is configured the read just fails (+ spams the I2C error log)
     * every ~100ms; the memset-0 vsysStat default is the safe "not-low" value
     * until then. */
    if (pData->BQ24297Data.initComplete) {
        uint8_t reg08;
        if (BQ24297_Read_I2C(0x08, &reg08)) {
            pData->BQ24297Data.status.vsysStat = (bool)(reg08 & 0x01);
        }
    }

    /* Read charging status from BATT_MAN_STAT GPIO
     * BQ24297 STAT pin: LOW = charging, HIGH = not charging/complete
     * Map to chgStat: 0=not charging, 1=pre-charge, 2=fast charge, 3=done */
    bool isCharging = !BATT_MAN_STAT_Get();  /* Active low */
    if (isCharging) {
        pData->BQ24297Data.status.chgStat = 2;  /* Fast charging */
    } else if (hasExternalPower && pData->chargePct >= 95.0f) {
        pData->BQ24297Data.status.chgStat = 3;  /* Charge complete */
    } else {
        pData->BQ24297Data.status.chgStat = 0;  /* Not charging */
    }

    /* Update external power source type
     * Without I2C, we can't distinguish USB types, so assume USB 500mA when VBUS present */
    if (hasExternalPower) {
        pData->externalPowerSource = USB_500MA_EXT_POWER;
    } else {
        pData->externalPowerSource = NO_EXT_POWER;
    }
}

/*
 * Rate-limited status update helper
 * Uses GPIO and ADC instead of I2C to avoid bus hang risk
 *
 * @param updateIntervalMs Minimum time between updates (ms)
 * @param lastUpdateTime Pointer to timestamp of last update
 * @return true if update performed, false if rate-limited
 */
static bool Power_UpdateStatusIfNeeded(uint32_t updateIntervalMs, TickType_t* lastUpdateTime) {
    TickType_t currentTime = xTaskGetTickCount();
    if ((currentTime - *lastUpdateTime) >= pdMS_TO_TICKS(updateIntervalMs)) {
        *lastUpdateTime = currentTime;
        Power_UpdateStatusFromGPIO();
        return true;
    }
    return false;
}

/*
 * Check if system has sufficient power to operate
 * Returns true if:
 *   - Battery voltage > 3.0V (vsysStat = 0) OR
 *   - External power present (pgStat = 1)
 */
static bool Power_HasSufficientPower(void) {
    return (!pData->BQ24297Data.status.vsysStat || pData->BQ24297Data.status.pgStat);
}

/*
 * STANDBY State Handler
 * 
 * Responsibilities:
 *   - Process power-up requests from user
 *   - Execute power-down sequence when needed
 *   - Monitor battery and external power
 *   - Update status (faster on USB for button response)
 */
static void Power_HandleStandbyState(void) {
    /* #454: Auto-power-up on VBUS-present.  Fires at boot (if USB
     * plugged at power-on) and on VBUS-rising mid-session (cable
     * inserted while device is in STANDBY on battery).  Suppressed
     * after the user explicitly returns to STANDBY in the same VBUS
     * session — only re-arms when VBUS goes away and comes back.
     * Skipped on battery-only (pgStat=0).  */
    if (pData->autoPowerOnUsb && pData->requestedPowerState == NO_CHANGE) {
        bool vbusPresent = pData->BQ24297Data.status.pgStat;
        if (!vbusPresent) {
            /* VBUS gone — re-arm auto-promote for next plug-in */
            pData->autoPromotedThisVbusSession = false;
        } else if (!pData->autoPromotedThisVbusSession) {
            /* VBUS present, haven't auto-promoted in this session yet */
            LOG_I("Power: auto-power-up on USB (#454)");
            pData->requestedPowerState = DO_POWER_UP;
            pData->autoPromotedThisVbusSession = true;
        }
    }

    /* Priority 1: Handle user power-up requests */
    if (pData->requestedPowerState == DO_POWER_UP ||
        pData->requestedPowerState == DO_POWER_UP_EXT_DOWN) {

        /* Refresh power status from GPIO/ADC before power-up decision */
        Power_UpdateStatusFromGPIO();

        if (Power_HasSufficientPower()) {
            // Power up with or without external power based on request
            bool enableExtPower = (pData->requestedPowerState == DO_POWER_UP);
            Power_Up(enableExtPower);
            pData->requestedPowerState = NO_CHANGE;
            return;
        } else {
            // Insufficient power for power-up request
            LOG_D("Power_UpdateState: Insufficient power - battery < 3.0V and no external power");
            pData->shutdownNotified = false;
            pData->requestedPowerState = NO_CHANGE;
        }
    }
    
    /* Execute power-down sequence (one-shot flag)
     * shutdownNotified is set by UI task after LED warning sequence completes
     * This ensures user is notified before shutdown */
    if (pData->shutdownNotified == true) {
        Power_Down();
        pData->shutdownNotified = false;  /* Clear flag to prevent repeated calls */
        
        /* Exit early on battery to conserve power */
        if (!pData->BQ24297Data.status.pgStat) {
            return;
        }
    }
    
    /* Status update with adaptive rate
     * - 100ms on USB: Better button/command responsiveness
     * - 1000ms on battery: Conserve power
     */
    static TickType_t lastStandbyUpdate = 0;
    uint32_t updateInterval = pData->BQ24297Data.status.pgStat ? 100 : 1000;
    if (Power_UpdateStatusIfNeeded(updateInterval, &lastStandbyUpdate)) {
        Power_Update_Settings();
    }
}

/*
 * POWERED_UP State Handler
 * 
 * All power rails enabled, monitoring for:
 *   - Battery drops below conservation threshold (15%)
 *   - User requests for state changes
 *   - Regular status updates
 */
static void Power_HandlePoweredUpState(void) {
    /* Clear redundant request (already in this state) */
    if (pData->requestedPowerState == DO_POWER_UP) {
        pData->requestedPowerState = NO_CHANGE;
    }
    
    // Rate limit status updates
    static TickType_t lastStatusUpdate = 0;
    if (Power_UpdateStatusIfNeeded(1000, &lastStatusUpdate)) {
        Power_Update_Settings();
    }
    
    bool hasExternalPower = pData->BQ24297Data.status.pgStat;
    
    /* User-requested transition to conservation mode */
    if (pData->requestedPowerState == DO_POWER_UP_EXT_DOWN) {
        pWriteVariables->EN_5_10V_Val = false;
        Power_Write();
        pData->powerState = POWERED_UP_EXT_DOWN;
        pData->requestedPowerState = NO_CHANGE;
    }
    /* Automatic transition when battery needs conservation */
    else if (!hasExternalPower) {
        /* Refresh power status from GPIO/ADC before threshold decision */
        Power_UpdateStatusFromGPIO();

        /* Demote to conservation mode on either:
         *   - the BQ's authoritative critical signal (vsysStat = BAT < SYS_MIN),
         *     REGARDLESS of chargePct validity — so a critically low battery is
         *     caught even before the VBATT ADC has sampled (EXT_DOWN then shuts
         *     to STANDBY on the same vsysStat). Without this, the critical path
         *     was unreachable while chargePct was UNKNOWN (Qodo #564). OR
         *   - a VALID chargePct below the conservation threshold. #564: only act
         *     on chargePct once the ADC reading is valid — at cold boot it reads
         *     a stale 0% that must not force a spurious demote/shutdown. */
        if (pData->BQ24297Data.status.vsysStat ||
            (pData->battVoltageValid && pData->chargePct < BATT_EXT_DOWN_TH)) {
            LOG_D("Power_UpdateState: Battery low (vsysStat=%u, %d%%), transitioning to POWERED_UP_EXT_DOWN",
                  pData->BQ24297Data.status.vsysStat, pData->chargePct);
            pWriteVariables->EN_5_10V_Val = false;
            Power_Write();
            pData->powerState = POWERED_UP_EXT_DOWN;
        }
    }
}

/*
 * POWERED_UP_EXT_DOWN State Handler (Conservation Mode)
 * 
 * Core system powered, external rails disabled to save battery
 * Monitoring for:
 *   - Battery recovery (charge > 25% with hysteresis)
 *   - External power connection
 *   - Critical battery level (<5%)
 */
static void Power_HandlePoweredUpExtDownState(void) {
    /* Rate limit status updates - uses GPIO/ADC, no I2C */
    static TickType_t lastExtDownUpdate = 0;
    Power_UpdateStatusIfNeeded(1000, &lastExtDownUpdate);

    bool hasExternalPower = pData->BQ24297Data.status.pgStat;

    /* Check for automatic recovery if auto external power control is enabled */
    if (pData->autoExtPowerEnabled) {
        /* Check recovery conditions (with hysteresis to prevent oscillation) */
        if (hasExternalPower || pData->chargePct >= (BATT_EXT_DOWN_TH + BATT_HYST)) {
            /* Auto-recovery - re-enable external power */
            LOG_D("Auto-recovery: Enabling external power - %s, battery at %d%%",
                  hasExternalPower ? "external power present" : "battery recovered", pData->chargePct);
            pWriteVariables->EN_5_10V_Val = true;
            Power_Write();
            pData->powerState = POWERED_UP;
            return;  /* Exit early after state change */
        }
    }

    /* Handle user power state change requests */
    if (pData->requestedPowerState == DO_POWER_UP) {
        /* Refresh power status from GPIO/ADC before decision */
        Power_UpdateStatusFromGPIO();
        hasExternalPower = pData->BQ24297Data.status.pgStat;

        /* Check recovery conditions (with hysteresis to prevent oscillation) */
        if (hasExternalPower || pData->chargePct >= (BATT_EXT_DOWN_TH + BATT_HYST)) {
            /* Manual recovery - re-enable external power */
            LOG_D("Manual recovery: Enabling external power - %s, battery at %d%%",
                  hasExternalPower ? "external power present" : "battery sufficient", pData->chargePct);
            pWriteVariables->EN_5_10V_Val = true;
            Power_Write();
            pData->powerState = POWERED_UP;
        } else {
            /* Cannot enable external power due to low battery */
            LOG_D("Power_HandlePoweredUpExtDownState: Cannot enable external power - battery at %d%%, needs %u%%",
                  pData->chargePct, (unsigned)(BATT_EXT_DOWN_TH + BATT_HYST));
        }
        pData->requestedPowerState = NO_CHANGE;
    }
    /* Critical battery check - must shut down to prevent damage.
     * #564: rely on the BQ24297 vsysStat (REG08, I2C — valid before the ADC
     * powers up) as the authoritative critical signal: it asserts when the
     * battery can no longer hold the system above SYS_MIN (3.0V).  The
     * chargePct (ADC-derived) path is only consulted once the voltage reading
     * has validated, so a stale 0% at cold boot can't force a false shutdown. */
    else if (!hasExternalPower &&
             (pData->BQ24297Data.status.vsysStat ||
              (pData->battVoltageValid && pData->chargePct < BATT_LOW_TH))) {
        LOG_D("Power_UpdateState: Battery critically low (vsysStat=%u, %d%%), transitioning to STANDBY",
              pData->BQ24297Data.status.vsysStat, pData->chargePct);

        /* Explicitly disable all external rails before shutdown */
        pWriteVariables->EN_5_10V_Val = false;
        pWriteVariables->EN_12V_Val = true;     /* Inverted logic - true = off */
        pWriteVariables->EN_Vref_Val = false;
        Power_Write();

        pData->shutdownNotified = false;
        pData->powerState = STANDBY;
    }
}

/*
 * Main Power State Machine
 * 
 * State transitions:
 *   STANDBY -> POWERED_UP: User request with sufficient power
 *   POWERED_UP -> POWERED_UP_EXT_DOWN: Battery < 15% or user request
 *   POWERED_UP_EXT_DOWN -> POWERED_UP: Battery > 25% or external power
 *   POWERED_UP_EXT_DOWN -> STANDBY: Battery < 5% (critical)
 *   Any state -> STANDBY: User power-down request
 */
static void Power_UpdateState(void) {
    static POWER_STATE lastLoggedState = -1;
    /* #334/#637: WiFi's enabled state captured on the way into STANDBY, so
     * we know whether to bring it back up on the way out. */
    static bool sWifiEnabledPreStandby = false;

    /* Monitor 3.3V rail state (debug/diagnostic) */
    static bool last3v3State = false;
    static bool first3v3Check = true;
    bool current3v3State = ReadGpioPinStateLatched(pConfig->EN_3_3V_Ch, pConfig->EN_3_3V_Bit);
    
    if (first3v3Check || (current3v3State != last3v3State)) {
        last3v3State = current3v3State;
        first3v3Check = false;
    }
    
    // Log state changes
    if (pData->powerState != lastLoggedState) {
        LOG_D("Power_UpdateState: State changed from %d to %d", lastLoggedState, pData->powerState);

        /* #334/#637: keep WiFi silent in STANDBY — no beacon, no STA
         * reconnect. Hold the WINC in reset (CHIP_EN/RESET_N low, via the
         * deep-power-down latch) on the way in; on the way out restore it
         * ONLY if it was enabled before standby. wifi_manager_PowerOn()
         * force-sets isEnabled=1, so an unconditional restore would turn
         * WiFi on for a user who had it off; and Deinit (inside PowerOff)
         * clears isEnabled, so the pre-standby value must be captured first.
         * The (POWER_STATE)-1 sentinel skips first-boot: the normal WiFi
         * init path owns the initial power-up, and PowerOff/PowerOn are
         * NULL-guarded no-ops before wifi_manager is initialized anyway. */
        if (lastLoggedState != (POWER_STATE)-1) {
            if (pData->powerState == STANDBY) {
                sWifiEnabledPreStandby = wifi_manager_IsEnabled();
                wifi_manager_PowerOff();
            } else if (lastLoggedState == STANDBY && sWifiEnabledPreStandby) {
                wifi_manager_PowerOn();
            }
        }

        lastLoggedState = pData->powerState;
    }
    
    /* Common handler: Power-down request (works in any state) */
    if (pData->requestedPowerState == DO_POWER_DOWN) {
        pData->powerState = STANDBY;
        pData->requestedPowerState = NO_CHANGE;
        /* Note: Actual power-down executed by STANDBY handler */
    }
    
    /* Delegate to state-specific handlers */
    switch (pData->powerState) {
        case STANDBY:
            Power_HandleStandbyState();
            break;
            
        case POWERED_UP:
            Power_HandlePoweredUpState();
            break;
            
        case POWERED_UP_EXT_DOWN:
            Power_HandlePoweredUpExtDownState();
            break;
            
        default:
            LOG_E("Power_UpdateState: Unknown power state %d", pData->powerState);
            break;
    }

}

static void Power_UpdateChgPct(void) {
    /* Read battery voltage from ADC */
    size_t index = ADC_FindChannelIndex(ADC_CHANNEL_VBATT);
    const AInSample *pAnalogSample = BoardData_Get(
            BOARDDATA_AIN_LATEST,
            index);
    if (NULL != pAnalogSample) {
        float newVoltage = ADC_ConvertToVoltage(pAnalogSample);
        /* Validate reading (ignore noise near 0V).  A reading <=0.1V means the
         * VBATT ADC isn't powered/sampled yet (cold boot) — don't trust it.
         * #564: track validity per-reading — clear it when the reading is
         * invalid/missing so a stale value can't be trusted later (e.g. battery
         * removed). chargePct then reads UNKNOWN, and the critical decision
         * falls back to the BQ vsysStat (authoritative). */
        if (newVoltage > 0.1) {
            pData->battVoltage = newVoltage;
            pData->battVoltageValid = true;
        } else {
            pData->battVoltageValid = false;
        }
    } else {
        pData->battVoltageValid = false;
    }

    /* Convert voltage to percentage using linear approximation
     * Valid range: 3.17V (0%) to 3.868V (100%)
     * Formula: percentage = 142.92 * voltage - 452.93
     *
     * #564: until the VBATT ADC has produced a real reading, battVoltage is a
     * stale 0 — report UNKNOWN, NOT 0%, so no consumer treats it as a dead
     * battery. (The power-up gate uses the BQ's authoritative vsysStat, not this.)
     */
    if (!pData->battVoltageValid) {
        pData->chargePct = BATT_CHARGE_UNKNOWN;
    } else if (pData->battVoltage < 3.17) {
        pData->chargePct = 0;
    } else if (pData->battVoltage > 3.868) {
        pData->chargePct = 100;
    } else {
        pData->chargePct = (int16_t)(142.92 * pData->battVoltage - 452.93);
    }
}

static void Power_Update_Settings(void) {
    /* Update power settings based on current status
     *
     * NOTE: External power source is now updated in Power_UpdateStatusFromGPIO()
     * I2C-based BQ24297 calls (AutoSetILim, SetPowerMode) are only done during
     * initial configuration in BQ24297_Config_Settings() to avoid I2C bus hangs.
     *
     * Charging is configured once at startup. The BQ24297 handles charging
     * autonomously based on its hardware configuration.
     */

    /* Nothing to do here anymore - status updates happen in Power_UpdateStatusFromGPIO()
     * and BQ24297 configuration happens once at init */
}


