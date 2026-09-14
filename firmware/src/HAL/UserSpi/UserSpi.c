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
#include "task.h"
#include "clock_config.h"
#include "HAL/DIO.h"
#include "Util/Logger.h"

/* SPI1 source clock = PBCLK2, taken from the project's single-source clock
 * config so it tracks the DAQIFI_SYSCLK_252 toggle (84 MHz at 252 MHz sysclk,
 * 100 MHz on the legacy 200 MHz build) rather than hardcoding one profile.
 * SPI_CLK = PBCLK2 / (2*(BRG+1)). */
#define USER_SPI_PBCLK_HZ   DAQIFI_PBCLK_HZ

/* Per-BYTE transfer budget, in milliseconds of wall clock (#913). Replaces a
 * bare loop counter: the wait now yields, so the bound has to be expressed in
 * the same units the deadline is compared in.
 *
 * Sizing: SPI is master-clocked, and in THIS configuration -- spi_Spi1Init
 * builds SPI1CON from 0 with only MSTEN[/CKP/CKE] set, so MODE16=MODE32=
 * ENHBUF=FRMEN=MSSEN=0 and SPI1CON2 is likewise forced to 0 (AUDEN=0, see
 * spi_Spi1Init) -- there is no mode in which SCK is gated by anything the
 * slave drives, so once SPI1BUF is written the module generates all 8 SCK
 * edges itself and nothing external can stretch them (unlike I2C's SCL
 * stretching). A byte's wire time is therefore fixed by BRG alone. The worst
 * legitimate byte is 8 bits at the lowest achievable SCK -- USER_SPI_MIN_BAUD_HZ
 * (6000), which spi_ComputeBrg meets exactly on the 84 MHz build (BRG 6999,
 * actual 6000 Hz) -- i.e. 8/6000 = 1.33 ms. (The 100 MHz legacy build's own
 * BRG-saturation floor is 6103 Hz / 1.31 ms, so the 84 MHz figure is the
 * binding worst case across both builds, not an underestimate for either.)
 * 20 ms is ~15x that. Expiry therefore means the module is not completing
 * transfers at all (SPI1 off, PMD-gated, or a stuck receive path) -- a
 * hardware/config fault, NOT a slow or absent slave.
 *
 * INVARIANT: keep this well above 8000 / USER_SPI_MIN_BAUD_HZ (ms). The only
 * in-tree change that can invalidate it is LOWERING USER_SPI_MIN_BAUD_HZ --
 * moving to a slower PBCLK does not: a slower PBCLK only lowers the
 * BRG-saturation floor, widening the accepted band down toward MIN_BAUD, so
 * the worst byte stays ~= 8000 / USER_SPI_MIN_BAUD_HZ ms regardless.
 *
 * Scope is per BYTE and deliberately NOT shared across the frame the way
 * uart_WriteLocked shares one 15 s budget -- see spi_XferByte for why the two
 * drivers differ. Because spi_TransferLocked breaks at the first failed byte,
 * at most ONE budget is ever spent per frame -- a 237-byte frame cannot sum
 * to 237x20ms; the ceiling is per-fault, not per-byte-cost. */
#define USER_SPI_BYTE_TIMEOUT_MS   20u

/* HAL scratch capacity per UserSpi_Transfer() frame. NOTE (#695): a single
 * SYST:COMM:SPI:TRANsfer? command cannot deliver a full 256 B frame -- the hex
 * payload (2 chars/byte) plus the command framing and CRLF terminator must fit
 * SCPI_INPUT_BUFFER_LENGTH (512, one byte reserved). Bench-measured 2026-07-19
 * (FW 3.7.2): the long query form `SYSTem:COMMunicate:SPI:TRANsfer? "<hex>"`
 * tops out at 237 payload bytes (238 overruns); the abbreviated
 * `SYST:COMM:SPI:TRAN?` form reaches 243. Larger requests are rejected by
 * libscpi (INPUT_BUFFER_OVERRUN, -363) BEFORE this callback runs. Use 237 B as
 * the safe single-command ceiling across command forms. This 256 B is the HAL
 * capability for any non-buffer-bound caller; SCPI clients split larger frames.
 * See SCPI_SpiTransfer() and the wiki SPI:TRANsfer? row. */
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
    /* Round the SPI clock DOWN, never up: the achieved SCK must not exceed the
     * requested baud. spi_ConfigureLocked validates only the *requested* rate
     * against USER_SPI_MAX_BAUD_HZ, so a round-to-nearest that lands above the
     * request would silently overclock a slave rated for the requested max
     * (data corruption). SCK = src / (2*(BRG+1)), so the smallest divisor
     * giving SCK <= baud is ceil(src / (2*baud)) = BRG+1. baudHz is validated
     * >= USER_SPI_MIN_BAUD_HZ upstream, so 2*baud never overflows uint32 (baud
     * <= 42 MHz) and is non-zero. The BRG clamp only bites below the hardware
     * SCK floor (~5.1 kHz at 84 MHz), which the 6 kHz min baud keeps requests
     * above — so actual <= baud always holds. */
    uint32_t twoBaud = 2u * baudHz;
    uint32_t divisor = (src + twoBaud - 1u) / twoBaud;   /* ceil(src / (2*baud)) */
    if (divisor == 0u) {
        divisor = 1u;
    }
    uint32_t brg = divisor - 1u;
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
        /* #761: DO NOT re-lock. On a bootloader-configured part
         * IOL1WAY/PMDL1WAY are ON, and FRM DS60001120F 12.3.1.6.2 says a
         * set BLOCKS the bit from ever being cleared again (until reset).
         * Setting it here would make this the LAST reconfiguration the
         * device ever accepts. The unlock above stays - it is idempotent
         * and still correct on L1WAY=OFF bench parts. */

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
    /* Indivisible like spi_ApplyPps: whole open -> PMD write -> close in one
     * interrupt-disabled block so no other task's SYSKEY/PMD work interleaves
     * and PMDLOCK is never left open to a preemptor. */
    uint32_t st = __builtin_disable_interrupts();
    spi_CfgconUnlockedWrite(CFGCON & ~(uint32_t)_CFGCON_PMDLOCK_MASK);
    if (enable) {
        PMD5CLR = _PMD5_SPI1MD_MASK;
    } else {
        PMD5SET = _PMD5_SPI1MD_MASK;
    }
        /* #761: DO NOT re-lock. On a bootloader-configured part
         * IOL1WAY/PMDL1WAY are ON, and FRM DS60001120F 12.3.1.6.2 says a
         * set BLOCKS the bit from ever being cleared again (until reset).
         * Setting it here would make this the LAST reconfiguration the
         * device ever accepts. The unlock above stays - it is idempotent
         * and still correct on L1WAY=OFF bench parts. */
    __builtin_mtc0(12, 0, st);
}

static void spi_Spi1Init(void) {
    uint8_t mode = gCfg.mode;
    /* CKP = CPOL; CKE is inverted vs CPHA on PIC32. */
    bool ckp = (mode & 0x2u) != 0u;
    bool cke = (mode & 0x1u) == 0u;

    SPI1CON = 0;                                   /* stop, reset, ON=0 */
    /* AUDEN=0: ordinary SPI framing, so one spi_XferByte is 8 bits of wire
     * time -- which is the premise the per-byte #913 timeout is sized against.
     *
     * V, PIC32 Family Reference Manual Section 23 "Serial Peripheral
     * Interface (SPI)", DS61106G, Register 23-2 (SPIxCON2) bit 7:
     *   AUDEN: Enable Audio CODEC Support bit
     *     1 = Audio protocol enabled
     *     0 = Audio protocol disabled
     * Cross-checked against the device pack, which fixes the bit POSITION but
     * not its meaning (this is the case CLAUDE.md warns about): p32mz2048efm144.h
     * has _SPI1CON2_AUDEN_MASK = 0x00000080, i.e. bit 7, and SPI1CON2 at
     * 0xBF821040.
     *
     * AUDEN matters here because when it is 1 the module OVERRIDES SPIxCON
     * settings this function sets -- DS61106G Register 23-2 note 3 lists
     * FRMEN=1, FRMCNT=1, SMP=0 being forced internally, and the frame then
     * carries a 16/24/32-bit audio word rather than the 8-bit transfer
     * MODE32=MODE16=0 selects. A 32-bit frame is four times the wire time this
     * timeout assumes.
     *
     * WHY WRITE THE WHOLE REGISTER rather than clearing AUDEN alone: nothing
     * else in this tree writes SPI1CON2 at all (verified by grep -- the only
     * other occurrences are this comment), so its contents here are whatever
     * the module powered up with or was last left holding. That is an absence
     * of a writer, not a guarantee of zero.
     *
     * WHAT ELSE THE WRITE CLEARS, and why it is inert on this peripheral: the
     * register's only non-audio bits are SPISGNEXT (15), FRMERREN (12),
     * SPIROVEN (11) and SPITUREN (10). The last three "Enable Interrupt Events
     * via" the FRMERR / SPIROV / SPITUR flags (DS61106G Register 23-2), and
     * SPI1's three interrupt vectors are only DECLARED by the generated EVIC
     * header -- nothing in this firmware enables or handles them -- so gating
     * an interrupt that is never taken changes nothing observable. SPISGNEXT
     * sign-extends RX FIFO reads, and ENHBUF is 0 here so there is no FIFO;
     * spi_XferByte reads SPI1BUF a byte at a time. */
    SPI1CON2 = 0;
    (void)SPI1BUF;                                 /* drain RX */
    SPI1STATCLR = _SPI1STAT_SPIROV_MASK;
    SPI1BRG = spi_ComputeBrg(gCfg.baudHz, &gActualBaud);

    uint32_t con = _SPI1CON_MSTEN_MASK;            /* master, 8-bit, ENHBUF=0 */
    if (ckp) { con |= _SPI1CON_CKP_MASK; }
    if (cke) { con |= _SPI1CON_CKE_MASK; }
    SPI1CON = con;
    SPI1CONSET = _SPI1CON_ON_MASK;
}

/* Wait for a SPI1STAT bit to reach @p want (true = wait for set, false =
 * clear), YIELDING so a low-baud frame doesn't busy-spin at the dispatching
 * SCPI task's priority and starve the pipeline (#913). A SCPI command over USB
 * runs on app_USBDeviceTask at priority 7 -- above the streaming encoder (6),
 * the USB device stack (6) and SD (5) -- so a 237 B frame at 6 kHz used to hold
 * the CPU for ~316 ms while the pri-9 deferred task kept filling the sample
 * pool and nothing drained it. A brief tight spin covers the fast path (a byte
 * at the 100 kHz default SCK completes in ~80 us -- no context switch); if
 * still not ready, vTaskDelay(1) lets everything below the SCPI task run while
 * the SPI module finishes clocking the byte on its own. The 8000-iteration
 * bound puts the spin/yield cutover at roughly 20 kHz SCK (estimate, not
 * bench-measured): configs at or above the documented 100 kHz default never
 * take a yield at all; only the low-baud tail (6-20 kHz) pays a tick sleep
 * per byte, which is the tail #913 is actually about. Matches i2c_WaitMif's
 * 8000 (also a command/response terminal default of ~100 kHz), not
 * uart_WaitSta's 4000 -- halving it would move the cutover to ~40 kHz and
 * start charging every byte in the common 20-40 kHz SPI range a full tick
 * sleep for no benefit.
 *
 * Note the ordering: the bit is tested before the deadline is consulted on
 * every pass, so a byte that completed while this task was preempted is
 * reported as success however late it is observed -- EXCEPT right at the
 * boundary, which is why the deadline branch re-checks rather than trusting
 * the pre-check above (opus review, #913): this task can be preempted in the
 * gap between that pre-check and the deadline test, and on the WiFi SCPI path
 * (app_WifiTask, priority 2 -- below the encoder/SD/USB tasks and both pri-9
 * deferred tasks) a >20 ms gap there is reachable under streaming load, not
 * merely theoretical. Deciding on a fresh read at expiry closes that window:
 * only a bit STILL not set at the moment the budget is spent can return
 * false, which is what makes the budget genuinely immune to scheduling
 * latency and sizeable against wire time alone. (uart_WaitSta / i2c_WaitMif
 * carried this identical narrow window pre-#913; ported here to both in the
 * same PR -- see UserUart.c / UserI2c.c.) */
static bool spi_WaitStat(uint32_t mask, bool want,
                         TickType_t start, TickType_t timeoutTicks) {
    for (;;) {
        for (uint32_t s = 0; s < 8000u; ++s) {
            if (((SPI1STAT & mask) != 0u) == want) { return true; }
        }
        if (((SPI1STAT & mask) != 0u) == want) { return true; }
        /* Rollover-safe: unsigned (now - start) is the true elapsed count even
         * across a tick-counter wrap, unlike an absolute-deadline compare. */
        if ((TickType_t)(xTaskGetTickCount() - start) >= timeoutTicks) {
            /* Fresh read, not a reuse of the pre-check above: this task can be
             * preempted between that check and this one, and a bit that set
             * during the preemption must still count as success. */
            return (((SPI1STAT & mask) != 0u) == want);
        }
        vTaskDelay(1);
    }
}

static bool spi_XferByte(uint8_t txByte, uint8_t* rxByte) {
    SPI1BUF = txByte;
    /* FRESH deadline per byte, not one budget shared across the frame.
     * uart_WriteLocked shares a single 15 s budget across its whole write for
     * two reasons that both invert here: a UART byte at its ~320 Hz floor is
     * ~31 ms, the same order as any per-byte budget, so per-byte scoping would
     * buy it nothing; and its TX is FIFO-buffered, so "a byte" is not a unit of
     * wire progress there. SPI is the opposite on both counts -- ENHBUF=0 means
     * exactly one byte is in flight, and the worst byte is 1.33 ms, 23x shorter
     * -- so a per-byte budget is both far more generous relative to the
     * legitimate case AND far tighter in absolute terms: spi_TransferLocked
     * breaks at the FIRST byte that fails, so a stuck bus is reported after one
     * budget (~20 ms) rather than a frame-sized one. */
    if (!spi_WaitStat(_SPI1STAT_SPIRBF_MASK, true, xTaskGetTickCount(),
                      pdMS_TO_TICKS(USER_SPI_BYTE_TIMEOUT_MS))) {
        return false;
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
    } else if (cfg->baudHz < (USER_SPI_PBCLK_HZ / (2u * 8192u))) {
        /* Below the BRG-saturation SCK floor at this PBCLK: the 13-bit BRG
         * clamps at 8191, so a lower request would be met with a SCK ABOVE it
         * (overclock). Reject here so spi_ComputeBrg's "achieved <= requested"
         * contract always holds. On the 84 MHz build the floor (~5.1 kHz) is
         * below the 6 kHz min above, so this only bites the 100 MHz legacy
         * clock (floor ~6.1 kHz). */
        why = "baud below the PBCLK SCK floor";
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
        if (err != NULL) { *err = "SCK/DIO0 in use (probe or PWM ch0) - free it first"; }
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

    /* Pre-drive SCK to its CKP idle level BEFORE enabling its output buffer, so
     * the terminal never sees a spurious clock edge when spi_Spi1Init drives SCK
     * to idle below. SCK (DIO0/RD1) is exposed as a LAT-driven GPIO output while
     * SPI1 is still OFF, so a prior LAT differing from the CKP idle would glitch
     * the pad at ON=1 — and a no-CS slave (csDio=-1) with CS tied active would
     * clock that edge as a bit, slipping the first frame. Mirror of the CS idle
     * pre-drive below; CKP = mode bit 1 (idle high when set). */
    DIO_DriveChannel(USER_SPI_SCK_DIO, (gCfg.mode & 0x2u) != 0u);
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

    bool haveCs   = (gCfg.csDio  != USER_SPI_PIN_NONE);
    bool haveMiso = (gCfg.misoDio != USER_SPI_PIN_NONE);
    if (haveCs) {
        /* CS is asserted for the WHOLE frame and STAYS asserted across the
         * vTaskDelay inside spi_XferByte (#913). That is intentional, not an
         * oversight: SPI has no bus-idle timeout, the master owns SCK, and
         * deasserting CS between bytes would abort the command in essentially
         * every SPI slave. A yield only widens an inter-byte gap that already
         * exists -- this transfer is preemptible by the pri-9 deferred tasks
         * between any two bytes today -- so it introduces no new class of gap,
         * only a longer one at low baud. The single device class that could
         * care is a slave with its own SPI frame/CS watchdog; that is a
         * documented property of this polled HAL (see the SPI:TRANsfer? wiki
         * row), not something to "fix" here by dropping CS mid-frame. */
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
            /* On a MOSI-only (write-only) bus no MISO pin is configured, yet
             * SDI1R still selects a real PPS pin (input PPS has no no-connect
             * encoding) whose read-back is arbitrary noise. Report the
             * documented 0x00 for the no-MISO case instead of that pin's level. */
            rx[i] = haveMiso ? (gCfg.lsbFirst ? spi_Reverse8(r) : r) : 0u;
        }
    }

    if (!ok) {
        /* Per-byte timeout (SCK not toggling): reset the module so the next
         * transfer starts from a clean slate. Toggling ON=0→1 flushes the shift
         * register and FIFOs if SCK halted mid-byte — otherwise the next
         * spi_XferByte (which only writes SPI1BUF, it does not re-init) could
         * clock out residual bits. MSTEN/CKP/CKE/BRG live in SPI1CON/SPI1BRG and
         * persist across the ON toggle, so no reconfigure is needed. Then clear
         * the (sticky) overflow flag and bounded-drain any residual RX — bounded
         * so a stuck SPIRBF from a hardware fault can't hang the caller; the RX
         * FIFO holds only a few bytes, so a small cap is ample. */
        SPI1CONCLR = _SPI1CON_ON_MASK;
        SPI1CONSET = _SPI1CON_ON_MASK;
        SPI1STATCLR = _SPI1STAT_SPIROV_MASK;
        /* This drain deliberately does NOT use spi_WaitStat. It is not waiting
         * for a future SCK edge: each iteration only consumes a byte SPIRBF
         * says is ALREADY in the receive buffer, and reading SPI1BUF is itself
         * what clears SPIRBF. The loop therefore terminates on its own in a
         * handful of back-to-back register reads; drainGuard exists only so a
         * hardware fault that pins SPIRBF set cannot hang the caller. Yielding
         * here would add up to 32 tick-sleeps to an error path for nothing,
         * while CS is still asserted and the module is half-reset. Bounded,
         * non-waiting spins stay spins. */
        uint32_t drainGuard = 32u;
        while (((SPI1STAT & _SPI1STAT_SPIRBF_MASK) != 0U) && (drainGuard-- > 0U)) {
            (void)SPI1BUF;
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
