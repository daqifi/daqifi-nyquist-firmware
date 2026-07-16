/**
 * @file UserSpi.c
 * @brief User SPI1 master on the DIO terminal (#665). See UserSpi.h.
 *
 * Hand-rolled SPI1 PLIB per the no-MCC policy: a minimal polled 8-bit
 * master. PPS values are taken from the PIC32MZ2048EFM144 DFP
 * (PIC32MZ-EF_DFP 1.5.173): SDO1 output code = 5 (RP<pin>R=5), SDI1 is a
 * group-1 input (SDI1R = the pin's RPn code: RPF1=4, RPG1=12, RPC1=10),
 * and SCK1 is a fixed (non-remappable) function on RD1/DIO0.
 */
#define LOG_LVL     LOG_LEVEL_ERROR
#define LOG_MODULE  LOG_MODULE_GENERAL

#include "UserSpi.h"
#include "configuration.h"
#include "definitions.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include "HAL/DIO.h"
#include "Util/Logger.h"

/* SPI1 source clock = PBCLK2. Post-#487 the peripheral bus runs at
 * SYSCLK/3 = 84 MHz (see the Clock Tree in CLAUDE.md). SPI_CLK =
 * PBCLK2 / (2*(BRG+1)). */
#define USER_SPI_PBCLK_HZ   84000000UL

/* Per-byte polled-transfer spin bound. Sized to cover the slowest byte
 * (8 bits at USER_SPI_MIN_BAUD_HZ ~= 1.3 ms) with wide margin at the
 * 252 MHz core; a real transfer at the 100 kHz default completes in ~80 us.
 * A timeout here means SCK is not toggling (hardware/config fault). */
#define USER_SPI_XFER_TIMEOUT   2000000UL

/* Max bytes per SYST:COMM:SPI:TRANsfer? frame (matches the SCPI hex cap). */
#define USER_SPI_MAX_FRAME      256u

// *****************************************************************************
// Section: module state
// *****************************************************************************

static UserSpiConfig_t gCfg;
static bool            gHaveConfig = false;
static bool            gEnabled    = false;
static uint32_t        gActualBaud = 0;

/* Serializes the public API. SPI SCPI commands can be dispatched from both the
 * USB SCPI task (pri 7) and the WiFi TCP SCPI task (pri 2), so config/enable/
 * transfer must not interleave on the single shared SPI1 peripheral. Statically
 * allocated (configSUPPORT_STATIC_ALLOCATION), lazily created on first use. */
static SemaphoreHandle_t gSpiMutex = NULL;
static StaticSemaphore_t gSpiMutexBuf;

static SemaphoreHandle_t spi_Mutex(void) {
    if (gSpiMutex == NULL) {
        taskENTER_CRITICAL();
        if (gSpiMutex == NULL) {   /* re-check: another task may have won the race */
            gSpiMutex = xSemaphoreCreateMutexStatic(&gSpiMutexBuf);
        }
        taskEXIT_CRITICAL();
    }
    return gSpiMutex;
}

static void spi_CfgconUnlockedWrite(uint32_t val);

// *****************************************************************************
// Section: PPS capability maps (verified DFP values)
// *****************************************************************************

/* MOSI: DIO channels whose pin can output SDO1 (RP<pin>R = 5). */
static bool spi_MosiCapable(uint8_t dio) {
    switch (dio) {
        case 2: case 4: case 5: case 6: case 7: case 14: case 15:
            return true;
        default:
            return false;
    }
}

/* MISO: DIO channels whose pin is a group-1 SDI1 input, and the SDI1R
 * value that selects them. Returns 0xFF for non-capable channels. */
static uint8_t spi_MisoSdi1rValue(uint8_t dio) {
    switch (dio) {
        case 5:  return 4u;   /* RPF1 */
        case 7:  return 12u;  /* RPG1 */
        case 15: return 10u;  /* RPC1 */
        default: return 0xFFu;
    }
}

/* Address of a MOSI-capable DIO's SDO1 output-select register (NULL if the
 * channel can't be MOSI). The caller writes 5 (SDO1) or 0 (no-connect). */
static volatile uint32_t* spi_MosiRprAddr(uint8_t dio) {
    switch (dio) {
        case 2:  return &RPD3R;  /* RD3 */
        case 4:  return &RPF0R;  /* RF0 */
        case 5:  return &RPF1R;  /* RF1 */
        case 6:  return &RPG0R;  /* RG0 */
        case 7:  return &RPG1R;  /* RG1 */
        case 14: return &RPE5R;  /* RE5 */
        case 15: return &RPC1R;  /* RC1 */
        default: return NULL;
    }
}

// *****************************************************************************
// Section: helpers
// *****************************************************************************

static uint8_t spi_Reverse8(uint8_t b) {
    b = (uint8_t)(((b & 0xF0u) >> 4) | ((b & 0x0Fu) << 4));
    b = (uint8_t)(((b & 0xCCu) >> 2) | ((b & 0x33u) << 2));
    b = (uint8_t)(((b & 0xAAu) >> 1) | ((b & 0x55u) << 1));
    return b;
}

/* Compute the BRG for a requested baud (same rounding as the Harmony SPI
 * PLIBs) and the actual resulting SCK. */
static uint32_t spi_ComputeBrg(uint32_t baudHz, uint32_t* actualOut) {
    uint32_t src = USER_SPI_PBCLK_HZ;
    uint32_t brg = ((src / baudHz) / 2u);
    brg = (brg == 0u) ? 0u : (brg - 1u);
    uint32_t baudHigh = src / (2u * (brg + 1u));
    uint32_t baudLow  = src / (2u * (brg + 2u));
    uint32_t errHigh  = (baudHigh > baudHz) ? (baudHigh - baudHz) : (baudHz - baudHigh);
    uint32_t errLow   = (baudHz > baudLow)  ? (baudHz - baudLow)  : (baudLow - baudHz);
    if (errHigh > errLow) {
        brg++;
    }
    if (brg > 8191u) {
        brg = 8191u;
    }
    if (actualOut != NULL) {
        *actualOut = src / (2u * (brg + 1u));
    }
    return brg;
}

/* Map / unmap SDO1 + SDI1 through PPS. Clears RPD1R so the dedicated SCK1
 * (not a remappable output) is the only driver on RD1/DIO0. */
static void spi_ApplyPps(bool enable) {
    /* PPS registers (RPnR outputs, SDI1R input) are gated by CFGCON.IOLOCK.
     * Run the whole open -> PPS writes -> close sequence as ONE interrupt-
     * disabled block so it is indivisible: no other task's PPS/SYSKEY work
     * (PWM output remap, a future DIO peripheral) can interleave, and IOLOCK
     * is never left open to a preemptor. The nested spi_CfgconUnlockedWrite
     * calls also disable interrupts, but restore to the already-disabled
     * state captured here, so interrupts stay off throughout. DIO0/RD1: drop
     * the remappable-output mux so the dedicated SCK1 owns the pad. */
    uint32_t st = __builtin_disable_interrupts();

    spi_CfgconUnlockedWrite(CFGCON & ~(uint32_t)_CFGCON_IOLOCK_MASK);
    RPD1R = 0U;
    volatile uint32_t *mosiRpr = spi_MosiRprAddr(gCfg.mosiDio);
    if (mosiRpr != NULL) {
        *mosiRpr = enable ? 5U : 0U;    /* SDO1=5, else no-connect */
    }
    SDI1R = (enable && gCfg.misoDio != USER_SPI_PIN_NONE)
                ? spi_MisoSdi1rValue(gCfg.misoDio) : 0U;
    spi_CfgconUnlockedWrite(CFGCON | (uint32_t)_CFGCON_IOLOCK_MASK);

    __builtin_mtc0(12, 0, st);
}

/* Perform a SYSKEY-unlocked write to CFGCON — the only SYSKEY-gated register
 * the SPI feature touches, to toggle IOLOCK (PPS) / PMDLOCK (module power).
 *
 * This only reconfigures at runtime because IOL1WAY / PMDL1WAY are OFF (see
 * config words in initialization.c); with the default one-way locks the plib
 * init consumes the single allowed change and every later unlock is silently
 * ignored. Interrupts are masked for the unlock so an ISR SFR access can't
 * land between the key writes and the protected store and disarm the window
 * (the same idiom plib_nvm.c uses for its runtime NVMKEY unlocks). */
static void spi_CfgconUnlockedWrite(uint32_t val) {
    uint32_t st = __builtin_disable_interrupts();
    SYSKEY = 0x00000000U;
    SYSKEY = 0xAA996655U;
    SYSKEY = 0x556699AAU;
    CFGCON = val;
    SYSKEY = 0x33333333U;
    __builtin_mtc0(12, 0, st);
}

/* Power the SPI1 module (clear its PMD bit). SPI1 is otherwise PMD-gated OFF
 * (Harmony's CLK_Initialize disables unused peripherals) so every SPI1 SFR
 * write is ignored. Clearing CFGCON.PMDLOCK opens PMD5 for a direct write. */
static void spi_SetPmd(bool enable) {
    spi_CfgconUnlockedWrite(CFGCON & ~(uint32_t)_CFGCON_PMDLOCK_MASK);
    if (enable) {
        PMD5CLR = _PMD5_SPI1MD_MASK;
    } else {
        PMD5SET = _PMD5_SPI1MD_MASK;
    }
    spi_CfgconUnlockedWrite(CFGCON | (uint32_t)_CFGCON_PMDLOCK_MASK);
}

static void spi_Spi1Init(void) {
    uint8_t mode = gCfg.mode;
    /* CKP = CPOL; CKE is inverted vs CPHA on PIC32. */
    bool ckp = (mode & 0x2u) != 0u;
    bool cke = (mode & 0x1u) == 0u;

    SPI1CON = 0;                                   /* stop, reset, ON=0 */
    (void)SPI1BUF;                                 /* drain RX */
    SPI1STATCLR = _SPI1STAT_SPIROV_MASK;
    SPI1BRG = spi_ComputeBrg(gCfg.baudHz, &gActualBaud);

    uint32_t con = _SPI1CON_MSTEN_MASK;            /* master, 8-bit, ENHBUF=0 */
    if (ckp) { con |= _SPI1CON_CKP_MASK; }
    if (cke) { con |= _SPI1CON_CKE_MASK; }
    SPI1CON = con;
    SPI1CONSET = _SPI1CON_ON_MASK;
}

static bool spi_XferByte(uint8_t txByte, uint8_t* rxByte) {
    SPI1BUF = txByte;
    uint32_t guard = USER_SPI_XFER_TIMEOUT;
    while ((SPI1STAT & _SPI1STAT_SPIRBF_MASK) == 0U) {
        if (--guard == 0U) {
            return false;
        }
    }
    *rxByte = (uint8_t)SPI1BUF;
    return true;
}

// *****************************************************************************
// Section: public API
// *****************************************************************************

static bool spi_ConfigureLocked(const UserSpiConfig_t* cfg, const char** err) {
    const char* why = NULL;
    if (cfg == NULL) {
        why = "null config";
    } else if (cfg->mode > 3u) {
        why = "mode must be 0..3";
    } else if (cfg->baudHz < USER_SPI_MIN_BAUD_HZ || cfg->baudHz > USER_SPI_MAX_BAUD_HZ) {
        why = "baud out of range (6k..42M Hz)";
    } else if (cfg->mosiDio != USER_SPI_PIN_NONE && !spi_MosiCapable(cfg->mosiDio)) {
        why = "MOSI must be DIO 2,4,5,6,7,14,15";
    } else if (cfg->misoDio != USER_SPI_PIN_NONE && spi_MisoSdi1rValue(cfg->misoDio) == 0xFFu) {
        why = "MISO must be DIO 5,7,15";
    } else if (cfg->csDio != USER_SPI_PIN_NONE && cfg->csDio >= 16u) {
        why = "CS DIO out of range";
    } else if (cfg->mosiDio == USER_SPI_PIN_NONE && cfg->misoDio == USER_SPI_PIN_NONE) {
        why = "at least one of MOSI/MISO required";
    }

    /* Collisions: SCK is DIO0; no two roles may share a pin. */
    if (why == NULL) {
        uint8_t m = cfg->mosiDio, s = cfg->misoDio, c = cfg->csDio;
        if (m == USER_SPI_SCK_DIO || s == USER_SPI_SCK_DIO || c == USER_SPI_SCK_DIO) {
            why = "DIO0 is reserved for SCK";
        } else if (m != USER_SPI_PIN_NONE && (m == s || m == c)) {
            why = "MOSI pin collides with MISO/CS";
        } else if (s != USER_SPI_PIN_NONE && s == c) {
            why = "MISO pin collides with CS";
        }
    }

    if (why != NULL) {
        if (err != NULL) { *err = why; }
        return false;
    }

    if (gEnabled) {
        if (err != NULL) { *err = "disable SPI before reconfiguring"; }
        return false;
    }

    gCfg = *cfg;
    gHaveConfig = true;
    return true;
}

/* True if a pin is unavailable to claim — owned by the probe/another
 * peripheral, or currently driving a PWM output. */
static bool spi_PinInUse(uint8_t dio) {
    return (DIO_ChannelBlockedReason(dio) != NULL) || DIO_IsPwmActive(dio);
}

static bool spi_EnableLocked(const char** err) {
    if (!gHaveConfig) {
        if (err != NULL) { *err = "no SPI config set"; }
        return false;
    }
    if (gEnabled) {
        return true;
    }

    /* Pre-check every needed pin before claiming any, so a failure leaves
     * nothing half-claimed. */
    if (spi_PinInUse(USER_SPI_SCK_DIO)) {
        if (err != NULL) { *err = "SCK/DIO0 in use (probe or PWM ch0) — free it first"; }
        return false;
    }
    if (gCfg.mosiDio != USER_SPI_PIN_NONE && spi_PinInUse(gCfg.mosiDio)) {
        if (err != NULL) { *err = "MOSI pin in use (probe/peripheral/PWM)"; }
        return false;
    }
    if (gCfg.misoDio != USER_SPI_PIN_NONE && spi_PinInUse(gCfg.misoDio)) {
        if (err != NULL) { *err = "MISO pin in use (probe/peripheral/PWM)"; }
        return false;
    }
    if (gCfg.csDio != USER_SPI_PIN_NONE && spi_PinInUse(gCfg.csDio)) {
        if (err != NULL) { *err = "CS pin in use (probe/peripheral/PWM)"; }
        return false;
    }

    /* Claim in order; unwind on any (racing) failure. */
    uint8_t claimed[4];
    int nClaimed = 0;
    #define SPI_TRY_CLAIM(ch) do {                                   \
        if (!DIO_ClaimChannel((ch), DIO_OWNER_SPI)) {                \
            while (nClaimed > 0) {                                   \
                DIO_ReleaseChannel(claimed[--nClaimed], DIO_OWNER_SPI); \
            }                                                        \
            if (err != NULL) { *err = "pin claim failed (raced)"; }  \
            return false;                                            \
        }                                                            \
        claimed[nClaimed++] = (ch);                                  \
    } while (0)

    SPI_TRY_CLAIM(USER_SPI_SCK_DIO);
    if (gCfg.mosiDio != USER_SPI_PIN_NONE) { SPI_TRY_CLAIM(gCfg.mosiDio); }
    if (gCfg.misoDio != USER_SPI_PIN_NONE) { SPI_TRY_CLAIM(gCfg.misoDio); }
    if (gCfg.csDio  != USER_SPI_PIN_NONE)  { SPI_TRY_CLAIM(gCfg.csDio); }
    #undef SPI_TRY_CLAIM

    /* PPS map, then set buffer directions, then start SPI1. */
    spi_ApplyPps(true);

    DIO_SetChannelPeripheralOutput(USER_SPI_SCK_DIO);
    if (gCfg.mosiDio != USER_SPI_PIN_NONE) {
        DIO_SetChannelPeripheralOutput(gCfg.mosiDio);
    }
    if (gCfg.misoDio != USER_SPI_PIN_NONE) {
        DIO_SetChannelPeripheralInput(gCfg.misoDio);
    }
    if (gCfg.csDio != USER_SPI_PIN_NONE) {
        /* Latch CS idle-high BEFORE enabling the output buffer, so the terminal
         * never sees a brief active-low assertion if the pin's prior LAT was
         * low (a spurious select to the slave). */
        DIO_DriveChannel(gCfg.csDio, true);
        DIO_SetChannelPeripheralOutput(gCfg.csDio);
    }

    spi_SetPmd(true);    /* power the SPI1 module before touching its SFRs */
    spi_Spi1Init();
    gEnabled = true;
    return true;
}

static bool spi_DisableLocked(void) {
    if (!gEnabled) {
        return true;
    }
    SPI1CON = 0;               /* SPI1 off (module stays PMD-powered from boot) */
    spi_ApplyPps(false);       /* unmap SDO1/SDI1, leave RPD1R cleared */

    /* Release claims, then restore each channel's runtime DIO state. */
    DIO_ReleaseChannel(USER_SPI_SCK_DIO, DIO_OWNER_SPI);
    DIO_RestoreChannel(USER_SPI_SCK_DIO);
    if (gCfg.mosiDio != USER_SPI_PIN_NONE) {
        DIO_ReleaseChannel(gCfg.mosiDio, DIO_OWNER_SPI);
        DIO_RestoreChannel(gCfg.mosiDio);
    }
    if (gCfg.misoDio != USER_SPI_PIN_NONE) {
        DIO_ReleaseChannel(gCfg.misoDio, DIO_OWNER_SPI);
        DIO_RestoreChannel(gCfg.misoDio);
    }
    if (gCfg.csDio != USER_SPI_PIN_NONE) {
        DIO_ReleaseChannel(gCfg.csDio, DIO_OWNER_SPI);
        DIO_RestoreChannel(gCfg.csDio);
    }

    gEnabled = false;
    gActualBaud = 0;
    return true;
}

bool UserSpi_IsEnabled(void) {
    return gEnabled;
}

static bool spi_TransferLocked(const uint8_t* tx, uint8_t* rx, uint16_t len) {
    if (!gEnabled || len == 0u || len > USER_SPI_MAX_FRAME) {
        return false;
    }

    bool haveCs = (gCfg.csDio != USER_SPI_PIN_NONE);
    if (haveCs) {
        DIO_DriveChannel(gCfg.csDio, false);   /* assert (active low) */
    }

    bool ok = true;
    for (uint16_t i = 0; i < len; ++i) {
        uint8_t t = (tx != NULL) ? tx[i] : 0xFFu;
        if (gCfg.lsbFirst) { t = spi_Reverse8(t); }
        uint8_t r = 0;
        if (!spi_XferByte(t, &r)) {
            ok = false;
            break;
        }
        if (rx != NULL) {
            rx[i] = gCfg.lsbFirst ? spi_Reverse8(r) : r;
        }
    }

    if (haveCs) {
        DIO_DriveChannel(gCfg.csDio, true);    /* deassert */
    }
    return ok;
}

uint32_t UserSpi_GetActualBaud(void) {
    return gEnabled ? gActualBaud : 0u;
}

// *****************************************************************************
// Section: public API mutex wrappers
//
// The SPI SCPI commands can be dispatched concurrently from the USB SCPI task
// (pri 7) and the WiFi TCP SCPI task (pri 2). These thin wrappers serialize
// every operation that touches the config, the shared SPI1 peripheral, or the
// PPS/PMD reconfiguration, so the two interfaces cannot interleave. The inner
// *_Locked functions keep their early-return structure; the single take/give
// here avoids sprinkling gives across every exit path. IsEnabled/GetActualBaud
// stay lock-free — each is a single aligned 32-bit read (atomic on PIC32MZ).
// *****************************************************************************

bool UserSpi_Configure(const UserSpiConfig_t* cfg, const char** err) {
    xSemaphoreTake(spi_Mutex(), portMAX_DELAY);
    bool r = spi_ConfigureLocked(cfg, err);
    xSemaphoreGive(gSpiMutex);
    return r;
}

bool UserSpi_GetConfig(UserSpiConfig_t* out) {
    if (out == NULL) {
        return false;
    }
    xSemaphoreTake(spi_Mutex(), portMAX_DELAY);
    bool r = false;
    if (gHaveConfig) {
        *out = gCfg;
        r = true;
    }
    xSemaphoreGive(gSpiMutex);
    return r;
}

bool UserSpi_Enable(const char** err) {
    xSemaphoreTake(spi_Mutex(), portMAX_DELAY);
    bool r = spi_EnableLocked(err);
    xSemaphoreGive(gSpiMutex);
    return r;
}

bool UserSpi_Disable(void) {
    xSemaphoreTake(spi_Mutex(), portMAX_DELAY);
    bool r = spi_DisableLocked();
    xSemaphoreGive(gSpiMutex);
    return r;
}

bool UserSpi_Transfer(const uint8_t* tx, uint8_t* rx, uint16_t len) {
    xSemaphoreTake(spi_Mutex(), portMAX_DELAY);
    bool r = spi_TransferLocked(tx, rx, len);
    xSemaphoreGive(gSpiMutex);
    return r;
}
