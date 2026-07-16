/**
 * @file UserUart.c
 * @brief User UART on the DIO terminal (#16). See UserUart.h.
 *
 * Hand-rolled UART PLIB per the no-MCC policy. The hardware UxART is
 * auto-selected from the requested RX/TX DIO pair via the descriptor table
 * below. PPS values are the DFP-authoritative codes (PIC32MZ2048EFM144.PIC):
 * TX RPnR output ppsval U1TX=1(g2) U5TX=3(g2) U3TX=1(g1) U2TX=2(g4) U6TX=4(g3);
 * RX UxRXR input ppsval = the pin's RPn input code (RPD1=0 RPD3=0 RPD12=10
 * RPF0=4 RPF1=4 RPG0=12 RPG1=12 RPC2=12 RPE3=6 RPE5=6 RPC1=10). Anchors:
 * U4TX=2 (Logger.c RPB0R), SDO1=5 (UserSpi) — both confirm the table.
 */
#define LOG_LVL     LOG_LEVEL_ERROR
#define LOG_MODULE  LOG_MODULE_GENERAL

#include "UserUart.h"
#include "configuration.h"
#include "definitions.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include "clock_config.h"
#include "HAL/DIO.h"
#include "Util/Logger.h"

/* UART source clock = PBCLK2, single-sourced so it tracks DAQIFI_SYSCLK_252.
 * BRGH=1: baud = PBCLK2 / (4*(BRG+1)). */
#define USER_UART_PBCLK_HZ   DAQIFI_PBCLK_HZ

/* Per-byte TX spin bound. Must exceed one frame-time at the SLOWEST baud: at
 * the ~320 Hz BRG floor a 10-bit frame is ~31 ms, and each loop iteration is a
 * peripheral-bus SFR read (tens of core cycles), so size generously — 20M gives
 * hundreds of ms of headroom per wait at 252 MHz (a byte at 115200 leaves in
 * ~87 us, so normal use never approaches it). A timeout means TX never drained
 * (hardware/config fault). */
#define USER_UART_TX_TIMEOUT  20000000UL

/* BRG is 16-bit; the slowest baud is PBCLK2/(4*65536). */
#define USER_UART_BRG_MAX     65535u

// *****************************************************************************
// Section: module state
// *****************************************************************************

static UserUartConfig_t gCfg;
static bool     gHaveConfig = false;
static bool     gEnabled    = false;
static uint32_t gActualBaud = 0;
static int8_t   gUartIdx    = -1;   /* index into gUarts[] once enabled */
static uint32_t gOverflow   = 0;    /* OERR events since Enable */

/* Serializes the public API — UART SCPI can be dispatched from both the USB
 * SCPI task (pri 7) and the WiFi TCP SCPI task (pri 2). Statically allocated,
 * lazily created (mirror of UserSpi's gSpiMutex). */
static SemaphoreHandle_t gUartMutex = NULL;
static StaticSemaphore_t gUartMutexBuf;

static SemaphoreHandle_t uart_Mutex(void) {
    if (gUartMutex == NULL) {
        taskENTER_CRITICAL();
        if (gUartMutex == NULL) {
            gUartMutex = xSemaphoreCreateMutexStatic(&gUartMutexBuf);
        }
        taskEXIT_CRITICAL();
    }
    return gUartMutex;
}

// *****************************************************************************
// Section: UART descriptor table (verified DFP PPS codes)
// *****************************************************************************

typedef struct {
    volatile uint32_t* mode;      /* UxMODE  */
    volatile uint32_t* modeSet;   /* UxMODESET */
    volatile uint32_t* sta;       /* UxSTA   */
    volatile uint32_t* staSet;    /* UxSTASET */
    volatile uint32_t* staClr;    /* UxSTACLR */
    volatile uint32_t* brg;       /* UxBRG   */
    volatile uint32_t* txreg;     /* UxTXREG */
    volatile uint32_t* rxreg;     /* UxRXREG */
    volatile uint32_t* rxr;       /* UxRXR (input-select) */
    uint32_t pmdMask;             /* PMD5 bit */
    uint8_t  txPpsval;            /* RPnR value that routes UxTX */
    uint16_t rxDioMask;           /* valid RX DIO channels */
    uint16_t txDioMask;           /* valid TX DIO channels */
} UartDesc_t;

/* DIO channel -> bitmask */
#define D(n) (1u << (n))

static const UartDesc_t gUarts[] = {
    /* U1: RX g1 {5,7,15}, TX g2 {2,4,6,14}, TX ppsval 1 */
    { &U1MODE, &U1MODESET, &U1STA, &U1STASET, &U1STACLR, &U1BRG, &U1TXREG, &U1RXREG,
      &U1RXR, _PMD5_U1MD_MASK, 1u,
      D(5)|D(7)|D(15), D(2)|D(4)|D(6)|D(14) },
    /* U3: RX g2 {2,4,6,14}, TX g1 {5,7,15}, TX ppsval 1 */
    { &U3MODE, &U3MODESET, &U3STA, &U3STASET, &U3STACLR, &U3BRG, &U3TXREG, &U3RXREG,
      &U3RXR, _PMD5_U3MD_MASK, 1u,
      D(2)|D(4)|D(6)|D(14), D(5)|D(7)|D(15) },
    /* U2: RX g3 {3,12}, TX g4 {0,11}, TX ppsval 2 */
    { &U2MODE, &U2MODESET, &U2STA, &U2STASET, &U2STACLR, &U2BRG, &U2TXREG, &U2RXREG,
      &U2RXR, _PMD5_U2MD_MASK, 2u,
      D(3)|D(12), D(0)|D(11) },
    /* U6: RX g4 {0,11}, TX g3 {3,12}, TX ppsval 4 */
    { &U6MODE, &U6MODESET, &U6STA, &U6STASET, &U6STACLR, &U6BRG, &U6TXREG, &U6RXREG,
      &U6RXR, _PMD5_U6MD_MASK, 4u,
      D(0)|D(11), D(3)|D(12) },
    /* U5: RX g1 {5,7,15}, TX g2 {2,4,6,14}, TX ppsval 3 — same pins as U1, so
     * U1 is auto-selected first; U5 is here for completeness / future N-UART. */
    { &U5MODE, &U5MODESET, &U5STA, &U5STASET, &U5STACLR, &U5BRG, &U5TXREG, &U5RXREG,
      &U5RXR, _PMD5_U5MD_MASK, 3u,
      D(5)|D(7)|D(15), D(2)|D(4)|D(6)|D(14) },
};
#define UART_COUNT ((int)(sizeof(gUarts)/sizeof(gUarts[0])))

/* The RX input ppsval (UxRXR value) for a DIO channel, or 0xFF if the channel
 * is not a UART-reachable RX pin. Same RPn input code SDI1R uses. */
static uint8_t uart_RxPpsval(uint8_t dio) {
    switch (dio) {
        case 0:  return 0u;   /* RPD1  */
        case 2:  return 0u;   /* RPD3  */
        case 3:  return 10u;  /* RPD12 */
        case 4:  return 4u;   /* RPF0  */
        case 5:  return 4u;   /* RPF1  */
        case 6:  return 12u;  /* RPG0  */
        case 7:  return 12u;  /* RPG1  */
        case 11: return 12u;  /* RPC2  */
        case 12: return 6u;   /* RPE3  */
        case 14: return 6u;   /* RPE5  */
        case 15: return 10u;  /* RPC1  */
        default: return 0xFFu;
    }
}

/* The TX RPnR output-select register for a DIO channel, or NULL if not a
 * UART-reachable TX pin. */
static volatile uint32_t* uart_TxRprAddr(uint8_t dio) {
    switch (dio) {
        case 0:  return &RPD1R;
        case 2:  return &RPD3R;
        case 3:  return &RPD12R;
        case 4:  return &RPF0R;
        case 5:  return &RPF1R;
        case 6:  return &RPG0R;
        case 7:  return &RPG1R;
        case 11: return &RPC2R;
        case 12: return &RPE3R;
        case 14: return &RPE5R;
        case 15: return &RPC1R;
        default: return NULL;
    }
}

/* Select the UART index for a (rx,tx) pair, or -1 if none matches. NONE lines
 * are wildcards (write-only / read-only). U1 wins over the pin-identical U5. */
static int uart_Select(uint8_t rxDio, uint8_t txDio) {
    bool haveRx = (rxDio != USER_UART_PIN_NONE);
    bool haveTx = (txDio != USER_UART_PIN_NONE);
    if (!haveRx && !haveTx) {
        return -1;
    }
    for (int i = 0; i < UART_COUNT; ++i) {
        bool rxOk = !haveRx || ((gUarts[i].rxDioMask & D(rxDio)) != 0u);
        bool txOk = !haveTx || ((gUarts[i].txDioMask & D(txDio)) != 0u);
        if (rxOk && txOk) {
            return i;
        }
    }
    return -1;
}

// *****************************************************************************
// Section: SYSKEY-protected PPS / PMD (mirror of UserSpi; the interrupt-masked
// unlock is the plib_nvm idiom / rule-68847 vendor-unlock exception)
// *****************************************************************************

static void uart_CfgconUnlockedWrite(uint32_t val) {
    uint32_t st = __builtin_disable_interrupts();
    SYSKEY = 0x00000000U;
    SYSKEY = 0xAA996655U;
    SYSKEY = 0x556699AAU;
    CFGCON = val;
    SYSKEY = 0x33333333U;
    __builtin_mtc0(12, 0, st);
}

/* Route/clear the selected UART's TX output + RX input in one indivisible
 * interrupt-disabled IOLOCK window. */
static void uart_ApplyPps(const UartDesc_t* u, bool enable) {
    uint32_t st = __builtin_disable_interrupts();
    uart_CfgconUnlockedWrite(CFGCON & ~(uint32_t)_CFGCON_IOLOCK_MASK);
    if (gCfg.txDio != USER_UART_PIN_NONE) {
        volatile uint32_t* rpr = uart_TxRprAddr(gCfg.txDio);
        if (rpr != NULL) { *rpr = enable ? (uint32_t)u->txPpsval : 0u; }
    }
    if (gCfg.rxDio != USER_UART_PIN_NONE) {
        *(u->rxr) = enable ? (uint32_t)uart_RxPpsval(gCfg.rxDio) : 0u;
    }
    uart_CfgconUnlockedWrite(CFGCON | (uint32_t)_CFGCON_IOLOCK_MASK);
    __builtin_mtc0(12, 0, st);
}

/* Un/gate the selected UART's PMD5 bit in one indivisible PMDLOCK window. */
static void uart_SetPmd(const UartDesc_t* u, bool enable) {
    uint32_t st = __builtin_disable_interrupts();
    uart_CfgconUnlockedWrite(CFGCON & ~(uint32_t)_CFGCON_PMDLOCK_MASK);
    if (enable) { PMD5CLR = u->pmdMask; } else { PMD5SET = u->pmdMask; }
    uart_CfgconUnlockedWrite(CFGCON | (uint32_t)_CFGCON_PMDLOCK_MASK);
    __builtin_mtc0(12, 0, st);
}

// *****************************************************************************
// Section: helpers
// *****************************************************************************

/* BRG for a requested baud, BRGH=1, round-to-NEAREST (unlike SPI's round-down:
 * UART baud is a match-tolerance constraint, both directions are fine within
 * ~2-3%, so nearest minimizes error). Also returns the achieved baud. Uses the
 * project's DAQIFI_UART_BRGH_DIV so the rounding matches the debug UART. */
static uint32_t uart_ComputeBrg(uint32_t baudHz, uint32_t* actualOut) {
    uint32_t brg = DAQIFI_UART_BRGH_DIV(baudHz);
    if (brg > USER_UART_BRG_MAX) { brg = USER_UART_BRG_MAX; }
    if (actualOut != NULL) {
        *actualOut = USER_UART_PBCLK_HZ / (4u * (brg + 1u));
    }
    return brg;
}

/* Lowest achievable baud at this PBCLK (BRG saturates at 65535). */
static uint32_t uart_BaudFloor(void) {
    return USER_UART_PBCLK_HZ / (4u * (USER_UART_BRG_MAX + 1u));
}

// *****************************************************************************
// Section: locked core
// *****************************************************************************

static bool uart_ConfigureLocked(const UserUartConfig_t* cfg, const char** err) {
    /* Reject a reconfigure while enabled (mirror of spi_ConfigureLocked).
     * Overwriting gCfg while the hardware stays on the old pins would desync the
     * config query from the real wiring AND strand the originally-claimed pins:
     * a later Disable releases the NEW gCfg pins (never claimed -> no-op) and
     * never releases the still-owned old pins, leaking them until reboot. */
    if (gEnabled) {
        if (err != NULL) { *err = "disable UART before reconfiguring"; }
        return false;
    }
    const char* why = NULL;
    if (cfg == NULL) {
        why = "null config";
    } else if (cfg->rxDio == USER_UART_PIN_NONE && cfg->txDio == USER_UART_PIN_NONE) {
        why = "at least one of RX/TX required";
    } else if (cfg->rxDio != USER_UART_PIN_NONE && cfg->rxDio == cfg->txDio) {
        why = "RX and TX must differ";
    } else if (cfg->dataBits != 8u) {
        /* PIC32 UART frames are 8- or 9-bit (PDSEL); true 7-bit data isn't a
         * hardware mode. Phase 1 is 8-bit (8N/8E/8O); 7-bit would need software
         * masking — tracked follow-up. */
        why = "data bits must be 8 (7-bit not supported yet)";
    } else if (cfg->parity > (uint8_t)USER_UART_PARITY_ODD) {
        why = "parity must be 0=N 1=E 2=O";
    } else if (cfg->stopBits != 1u && cfg->stopBits != 2u) {
        why = "stop bits must be 1 or 2";
    } else if (cfg->baudHz < USER_UART_MIN_BAUD_HZ || cfg->baudHz > USER_UART_MAX_BAUD_HZ) {
        why = "baud out of range (300..3M Hz)";
    } else if (cfg->baudHz < uart_BaudFloor()) {
        why = "baud below the PBCLK BRG floor";
    } else if (cfg->rxDio != USER_UART_PIN_NONE && uart_RxPpsval(cfg->rxDio) == 0xFFu) {
        why = "RX pin not UART-capable";
    } else if (cfg->txDio != USER_UART_PIN_NONE && uart_TxRprAddr(cfg->txDio) == NULL) {
        why = "TX pin not UART-capable";
    } else if (uart_Select(cfg->rxDio, cfg->txDio) < 0) {
        why = "RX/TX pair maps to no hardware UART";
    }
    if (why != NULL) {
        if (err != NULL) { *err = why; }
        return false;
    }
    gCfg = *cfg;
    gHaveConfig = true;
    return true;
}

static bool uart_EnableLocked(const char** err) {
    if (!gHaveConfig) {
        if (err != NULL) { *err = "no UART config set"; }
        return false;
    }
    if (gEnabled) {
        return true;
    }
    int idx = uart_Select(gCfg.rxDio, gCfg.txDio);
    if (idx < 0) {
        if (err != NULL) { *err = "no hardware UART for this pin pair"; }
        return false;
    }
    const UartDesc_t* u = &gUarts[idx];

    /* Claim pins through the ownership registry (rollback on any failure). */
    uint8_t claimed[2];
    uint8_t nClaimed = 0;
    #define UART_TRY_CLAIM(ch)                                       \
        do {                                                         \
            if (!DIO_ClaimChannel((ch), DIO_OWNER_UART)) {           \
                for (uint8_t k = 0; k < nClaimed; ++k) {             \
                    DIO_ReleaseChannel(claimed[k], DIO_OWNER_UART);  \
                }                                                    \
                if (err != NULL) { *err = "pin already in use"; }    \
                return false;                                        \
            }                                                        \
            claimed[nClaimed++] = (ch);                              \
        } while (0)
    if (gCfg.rxDio != USER_UART_PIN_NONE) { UART_TRY_CLAIM(gCfg.rxDio); }
    if (gCfg.txDio != USER_UART_PIN_NONE) { UART_TRY_CLAIM(gCfg.txDio); }
    #undef UART_TRY_CLAIM

    /* Power the module, then configure it fully and turn it ON *before* routing
     * the pin PPS. UxTX idles at its mark level as soon as ON=1, so when we
     * then map the pin to UxTX and enable its buffer the terminal sees a steady
     * idle level — no start-bit glitch (the checklist's pre-drive requirement,
     * met here by ordering rather than a LAT pre-drive). */
    uart_SetPmd(u, true);

    /* UxMODE/UxSTA bit positions are identical across all UARTs, so the U1 mask
     * macros carry the generic values. PDSEL: parity 0/1/2 = 8N/8E/8O (the enum
     * matches the field). */
    uint32_t mode = _U1MODE_BRGH_MASK;                                   /* BRGH=1 */
    mode |= ((uint32_t)gCfg.parity << _U1MODE_PDSEL_POSITION);           /* 8N/8E/8O */
    if (gCfg.stopBits == 2u) { mode |= _U1MODE_STSEL_MASK; }
    if (gCfg.rxInv)          { mode |= _U1MODE_RXINV_MASK; }
    *(u->mode) = mode;

    uint32_t sta = 0;
    if (gCfg.txDio != USER_UART_PIN_NONE) { sta |= _U1STA_UTXEN_MASK; }
    if (gCfg.rxDio != USER_UART_PIN_NONE) { sta |= _U1STA_URXEN_MASK; }
    if (gCfg.txInv)                       { sta |= _U1STA_UTXINV_MASK; }
    *(u->sta) = sta;

    *(u->brg) = uart_ComputeBrg(gCfg.baudHz, &gActualBaud);
    *(u->modeSet) = _U1MODE_ON_MASK;

    /* Now route PPS (pin follows the already-idling UxTX) and switch the
     * terminal buffers: TX = peripheral output, RX = input. */
    uart_ApplyPps(u, true);
    if (gCfg.txDio != USER_UART_PIN_NONE) { DIO_SetChannelPeripheralOutput(gCfg.txDio); }
    if (gCfg.rxDio != USER_UART_PIN_NONE) { DIO_SetChannelPeripheralInput(gCfg.rxDio); }

    gUartIdx = (int8_t)idx;
    gOverflow = 0;
    gEnabled = true;
    return true;
}

static bool uart_DisableLocked(void) {
    if (!gEnabled) {
        return true;
    }
    const UartDesc_t* u = &gUarts[gUartIdx];
    *(u->mode) = 0;              /* UART off (ON=0) */
    uart_ApplyPps(u, false);     /* unmap TX/RX PPS */
    uart_SetPmd(u, false);       /* gate the module */

    /* Release the claim BEFORE restoring: DIO_RestoreChannel -> DIO_WriteStateSingle
     * short-circuits on a still-owned channel (its own contract), so restoring
     * first would be a no-op and leave the pin in peripheral-buffer mode. Match
     * the spi_DisableLocked order. */
    if (gCfg.rxDio != USER_UART_PIN_NONE) { DIO_ReleaseChannel(gCfg.rxDio, DIO_OWNER_UART); DIO_RestoreChannel(gCfg.rxDio); }
    if (gCfg.txDio != USER_UART_PIN_NONE) { DIO_ReleaseChannel(gCfg.txDio, DIO_OWNER_UART); DIO_RestoreChannel(gCfg.txDio); }

    gEnabled = false;
    gUartIdx = -1;
    gActualBaud = 0;
    return true;
}

static bool uart_WriteLocked(const uint8_t* data, uint16_t len) {
    if (!gEnabled || gCfg.txDio == USER_UART_PIN_NONE || data == NULL) {
        return false;
    }
    const UartDesc_t* u = &gUarts[gUartIdx];
    for (uint16_t i = 0; i < len; ++i) {
        uint32_t guard = USER_UART_TX_TIMEOUT;
        while ((*(u->sta) & _U1STA_UTXBF_MASK) != 0u) {   /* TX buffer full */
            if (--guard == 0u) { return false; }
        }
        *(u->txreg) = data[i];
    }
    /* Wait for the shifter to empty so the caller knows the frame is on the
     * wire (bounded). */
    uint32_t guard = USER_UART_TX_TIMEOUT;
    while ((*(u->sta) & _U1STA_TRMT_MASK) == 0u) {
        if (--guard == 0u) { return false; }
    }
    return true;
}

/* Drain the hardware RX FIFO into @p out (up to maxLen); fold an OERR overrun
 * (erratum #8 — the shifter stalls until OERR is cleared) into the overflow
 * counter and clear it so RX resumes. */
static uint16_t uart_ReadLocked(uint8_t* out, uint16_t maxLen) {
    if (!gEnabled || gCfg.rxDio == USER_UART_PIN_NONE || out == NULL) {
        return 0;
    }
    const UartDesc_t* u = &gUarts[gUartIdx];
    /* Drain the FIFO BEFORE clearing OERR: on PIC32 clearing OERR resets the RX
     * FIFO, so clearing first would discard the bytes already buffered. Reading
     * empties the FIFO; then clear the (sticky) overrun to un-stall the shifter
     * (erratum #8) and count the event. */
    uint16_t n = 0;
    while (n < maxLen && (*(u->sta) & _U1STA_URXDA_MASK) != 0u) {
        out[n++] = (uint8_t)(*(u->rxreg) & 0xFFu);
    }
    if ((*(u->sta) & _U1STA_OERR_MASK) != 0u) {
        gOverflow++;
        *(u->staClr) = _U1STA_OERR_MASK;
    }
    return n;
}

static uint16_t uart_RxPendingLocked(void) {
    if (!gEnabled || gCfg.rxDio == USER_UART_PIN_NONE) {
        return 0;
    }
    const UartDesc_t* u = &gUarts[gUartIdx];
    return ((*(u->sta) & _U1STA_URXDA_MASK) != 0u) ? 1u : 0u;   /* >=1 byte ready */
}

// *****************************************************************************
// Section: public API mutex wrappers
// *****************************************************************************

bool UserUart_Configure(const UserUartConfig_t* cfg, const char** err) {
    xSemaphoreTake(uart_Mutex(), portMAX_DELAY);
    bool r = uart_ConfigureLocked(cfg, err);
    xSemaphoreGive(gUartMutex);
    return r;
}

bool UserUart_GetConfig(UserUartConfig_t* out) {
    if (out == NULL) { return false; }
    xSemaphoreTake(uart_Mutex(), portMAX_DELAY);
    bool r = false;
    if (gHaveConfig) { *out = gCfg; r = true; }
    xSemaphoreGive(gUartMutex);
    return r;
}

bool UserUart_Enable(const char** err) {
    xSemaphoreTake(uart_Mutex(), portMAX_DELAY);
    bool r = uart_EnableLocked(err);
    xSemaphoreGive(gUartMutex);
    return r;
}

bool UserUart_Disable(void) {
    xSemaphoreTake(uart_Mutex(), portMAX_DELAY);
    bool r = uart_DisableLocked();
    xSemaphoreGive(gUartMutex);
    return r;
}

bool UserUart_SetInvert(bool rxInv, bool txInv) {
    xSemaphoreTake(uart_Mutex(), portMAX_DELAY);
    bool r = false;
    if (gHaveConfig) {
        gCfg.rxInv = rxInv;
        gCfg.txInv = txInv;
        if (gEnabled) {
            const UartDesc_t* u = &gUarts[gUartIdx];
            if (rxInv) { *(u->modeSet) = _U1MODE_RXINV_MASK; } else { *(u->mode) &= ~(uint32_t)_U1MODE_RXINV_MASK; }
            if (txInv) { *(u->staSet)  = _U1STA_UTXINV_MASK; } else { *(u->staClr) = _U1STA_UTXINV_MASK; }
        }
        r = true;
    }
    xSemaphoreGive(gUartMutex);
    return r;
}

bool UserUart_Write(const uint8_t* data, uint16_t len) {
    xSemaphoreTake(uart_Mutex(), portMAX_DELAY);
    bool r = uart_WriteLocked(data, len);
    xSemaphoreGive(gUartMutex);
    return r;
}

uint16_t UserUart_Read(uint8_t* out, uint16_t maxLen) {
    xSemaphoreTake(uart_Mutex(), portMAX_DELAY);
    uint16_t r = uart_ReadLocked(out, maxLen);
    xSemaphoreGive(gUartMutex);
    return r;
}

uint16_t UserUart_RxPending(void) {
    xSemaphoreTake(uart_Mutex(), portMAX_DELAY);
    uint16_t r = uart_RxPendingLocked();
    xSemaphoreGive(gUartMutex);
    return r;
}

uint32_t UserUart_RxOverflowCount(void) {
    return gOverflow;   /* single aligned 32-bit read — atomic on PIC32MZ */
}

bool UserUart_IsEnabled(void) {
    return gEnabled;
}

uint32_t UserUart_GetActualBaud(void) {
    return gEnabled ? gActualBaud : 0u;
}
