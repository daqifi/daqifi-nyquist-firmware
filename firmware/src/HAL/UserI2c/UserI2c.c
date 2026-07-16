/**
 * @file UserI2c.c
 * @brief Hand-rolled polled I2C2 master + PCA9516A hub control (#15, epic #664).
 *
 * No-MCC policy: only I2C5 exists in the generated tree, so I2C2 is driven
 * directly here. The register sequences mirror the hardware-proven I2C5 PLIB
 * (START/addr/ACKSTAT/TRN, RCEN/RCV/ACKEN, PEN); the state machine is folded
 * into bounded polled primitives that run from SCPI (task) context so a stuck
 * bus times out instead of hanging (a 9-clock + STOP recovery is wired into the
 * ENAble cycle). See UserI2c.h for the verified hardware topology.
 */
#include "device.h"
#include "UserI2c.h"
#include "../DIO.h"
#include "clock_config.h"          /* DAQIFI_PBCLK_HZ, DAQIFI_SYSCLK_252 */
#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

/* ------------------------------------------------------------------ */
/* Bounded waits use the CP0 core timer (SYSCLK/2). A single I2C op (one
 * byte at the 10 kHz floor ~ 0.9 ms) is far under this budget; the budget only
 * exists so a stuck SDA/SCL fails the op instead of hanging the task. */
#define I2C_OP_TIMEOUT_CP0   700000u   /* ~5.5 ms @126 MHz / ~7 ms @100 MHz */
#define I2C_HALFBIT_CP0        630u    /* ~5 us @126 MHz -- bus-recovery clock */

/* PGD term (Fpb*130 ns / 2) expressed in BRG units, integer. */
#define I2C_PGD_BRG   (((DAQIFI_PBCLK_HZ / 1000000u) * 13u) / 200u)

/* Hub segment map (verified: I2C.SchDoc). idx 0 = segment 1, idx 1 = segment 2. */
typedef struct {
    uint8_t sclDio;               /* terminal SCL DIO channel */
    uint8_t sdaDio;               /* terminal SDA DIO channel */
    volatile uint32_t* enSet;     /* LATxSET for the hub enable */
    volatile uint32_t* enClr;     /* LATxCLR */
    volatile uint32_t* enTrisClr; /* TRISxCLR (drive the enable as an output) */
    uint32_t enMask;              /* enable bit (1<<14) */
} I2cSeg_t;

static const I2cSeg_t gSegs[USER_I2C_SEGMENT_COUNT] = {
    /* Segment 1: SCL=DIO10 (RE4), SDA=DIO11 (RC2), EN1 = RH14 */
    { 10u, 11u, (volatile uint32_t*)&LATHSET, (volatile uint32_t*)&LATHCLR,
      (volatile uint32_t*)&TRISHCLR, (1u << 14) },
    /* Segment 2: SCL=DIO8 (RJ6), SDA=DIO9 (RE1), EN2 = RA14 */
    {  8u,  9u, (volatile uint32_t*)&LATASET, (volatile uint32_t*)&LATACLR,
      (volatile uint32_t*)&TRISACLR, (1u << 14) },
};

/* ------------------------------------------------------------------ */
static bool     gEnabled;
static uint32_t gFreqHz = USER_I2C_DEFAULT_FREQ_HZ;
static uint32_t gActualFreqHz;
static uint32_t gBrg;
static bool     gSegOn[USER_I2C_SEGMENT_COUNT] = { true, false }; /* seg1 on by default */

static SemaphoreHandle_t gI2cMutex;
static StaticSemaphore_t gI2cMutexBuf;

static SemaphoreHandle_t i2c_Mutex(void) {
    if (gI2cMutex == NULL) {
        taskENTER_CRITICAL();
        if (gI2cMutex == NULL) {   /* re-check: another task may have won the race */
            gI2cMutex = xSemaphoreCreateMutexStatic(&gI2cMutexBuf);
        }
        taskEXIT_CRITICAL();
    }
    return gI2cMutex;
}

/* ------------------------------------------------------------------ */
/* BRG / frequency */

static uint32_t i2c_ActualFromBrg(uint32_t brg) {
    return DAQIFI_PBCLK_HZ / (2u * (brg + 1u + I2C_PGD_BRG));
}

/* Round so the ACTUAL frequency never exceeds the request (conservative, and
 * keeps a near-400 kHz request from crossing the erratum-#37 line). */
static uint32_t i2c_ComputeBrg(uint32_t fsck) {
    uint32_t base = DAQIFI_PBCLK_HZ / (2u * fsck);
    uint32_t brg  = (base > (I2C_PGD_BRG + 1u)) ? (base - I2C_PGD_BRG - 1u) : 4u;
    while (brg < 65535u && i2c_ActualFromBrg(brg) > fsck) {
        brg++;
    }
    if (brg < 4u)     { brg = 4u; }
    if (brg > 65535u) { brg = 65535u; }
    return brg;
}

/* ------------------------------------------------------------------ */
/* Bounded polled primitives */

/* Each master event (START/STOP/repeated-START/byte TX/byte RX/ACK) completes
 * by setting the I2C2 master interrupt flag (IFS4.I2C2MIF). We poll that flag --
 * NOT the transient CON/STAT status bits -- because a status bit like TRSTAT
 * may not have gone high yet when we look right after kicking the op, which
 * would race us past it and read a stale ACKSTAT. The interrupt itself stays
 * masked (IEC4 cleared in i2c_ModuleInit); we only poll+clear the flag. This is
 * the same completion signal the interrupt-driven I2C5 PLIB advances on. */
static void i2c_ClearMif(void) {
    IFS4CLR = _IFS4_I2C2MIF_MASK;
}

static bool i2c_WaitMif(void) {
    uint32_t start = _CP0_GET_COUNT();
    for (;;) {
        /* Brief tight spin: a normal byte at 100 kHz completes in ~90 us, so
         * this returns without ever yielding on the common path. */
        for (uint32_t s = 0; s < 8000u; ++s) {
            if ((IFS4 & _IFS4_I2C2MIF_MASK) != 0u) { return true; }
        }
        if ((IFS4 & _IFS4_I2C2MIF_MASK) != 0u) { return true; }
        if ((uint32_t)(_CP0_GET_COUNT() - start) > I2C_OP_TIMEOUT_CP0) {
            return false;
        }
        /* A slow/low-baud/stuck op must not busy-wait the SCPI task and starve
         * lower-priority tasks (esp. a 112-address SCAN); the I2C hardware
         * clocks the bus autonomously while we sleep. Mirrors uart_WaitSta. */
        vTaskDelay(1);
    }
}

static void i2c_DelayHalfBit(void) {
    uint32_t start = _CP0_GET_COUNT();
    while ((uint32_t)(_CP0_GET_COUNT() - start) < I2C_HALFBIT_CP0) { }
}

static bool i2c_Start(void) {
    i2c_ClearMif();
    I2C2CONSET = _I2C2CON_SEN_MASK;
    return i2c_WaitMif();
}

static bool i2c_RepeatedStart(void) {
    i2c_ClearMif();
    I2C2CONSET = _I2C2CON_RSEN_MASK;
    return i2c_WaitMif();
}

static bool i2c_Stop(void) {
    i2c_ClearMif();
    I2C2CONSET = _I2C2CON_PEN_MASK;
    return i2c_WaitMif();
}

/* Transmit one byte; *ackOut = slave ACKed (ACKSTAT==0, valid after the MIF). */
static bool i2c_TxByte(uint8_t b, bool* ackOut) {
    i2c_ClearMif();
    I2C2TRN = b;
    if (!i2c_WaitMif()) {
        return false;
    }
    if (ackOut != NULL) {
        *ackOut = ((I2C2STAT & _I2C2STAT_ACKSTAT_MASK) == 0u);
    }
    return true;
}

/* Receive one byte, then drive ACK (sendAck=true) or NACK (false). */
static bool i2c_RxByte(bool sendAck, uint8_t* out) {
    i2c_ClearMif();
    I2C2CONSET = _I2C2CON_RCEN_MASK;
    if (!i2c_WaitMif()) {                   /* receive complete (byte in RCV) */
        return false;
    }
    *out = (uint8_t)(I2C2RCV & 0xFFu);
    if (sendAck) {
        I2C2CONCLR = _I2C2CON_ACKDT_MASK;   /* 0 = ACK */
    } else {
        I2C2CONSET = _I2C2CON_ACKDT_MASK;   /* 1 = NACK */
    }
    i2c_ClearMif();
    I2C2CONSET = _I2C2CON_ACKEN_MASK;
    return i2c_WaitMif();                    /* ACK/NACK clocked out */
}

/* Transfer outcome. NACK (device absent / declined) is normal and must NOT
 * trigger bus recovery; only a TIMEOUT means the bus may be hung (SDA stuck). */
typedef enum {
    I2C_XFER_OK = 0,
    I2C_XFER_NACK,      /* addressed/sent OK but the slave did not ACK */
    I2C_XFER_TIMEOUT,   /* a bus event never completed (MIF timed out) */
} I2cXferResult_t;

/* One combined transfer. Always issues a STOP. */
static I2cXferResult_t i2c_XferLocked(uint8_t addr7, const uint8_t* w, uint16_t wlen,
                                      uint8_t* r, uint16_t rlen) {
    bool ack = false;
    I2cXferResult_t res = I2C_XFER_TIMEOUT;   /* any primitive returning false = timeout */
    /* A write phase runs for a real write AND for an address-only probe (SCAN,
     * wlen==0 && rlen==0). A pure read (wlen==0 && rlen>0) skips it. */
    bool doWrite = (wlen > 0u) || (rlen == 0u);

    if (doWrite) {
        if (!i2c_Start()) { goto stop; }
        if (!i2c_TxByte((uint8_t)(addr7 << 1), &ack)) { goto stop; }
        if (!ack) { res = I2C_XFER_NACK; goto stop; }
        for (uint16_t i = 0; i < wlen; ++i) {
            if (!i2c_TxByte(w[i], &ack)) { goto stop; }
            if (!ack) { res = I2C_XFER_NACK; goto stop; }
        }
    }
    if (rlen > 0u) {
        if (doWrite) {
            if (!i2c_RepeatedStart()) { goto stop; }
        } else {
            if (!i2c_Start()) { goto stop; }
        }
        if (!i2c_TxByte((uint8_t)((addr7 << 1) | 1u), &ack)) { goto stop; }
        if (!ack) { res = I2C_XFER_NACK; goto stop; }
        for (uint16_t i = 0; i < rlen; ++i) {
            if (!i2c_RxByte((i + 1u) < rlen, &r[i])) { goto stop; }  /* NACK last */
        }
    }
    res = I2C_XFER_OK;
stop:
    (void)i2c_Stop();
    return res;
}

/* ------------------------------------------------------------------ */
/* Module + bus recovery */

/* SYSKEY-guarded CFGCON write (the plib_nvm idiom) so we can toggle PMDLOCK.
 * Mirrors UserSpi's spi_CfgconUnlockedWrite. */
static void i2c_CfgconUnlockedWrite(uint32_t val) {
    uint32_t st = __builtin_disable_interrupts();
    SYSKEY = 0x00000000U;
    SYSKEY = 0xAA996655U;
    SYSKEY = 0x556699AAU;
    CFGCON = val;
    SYSKEY = 0x33333333U;
    __builtin_mtc0(12, 0, st);
}

/* Power the I2C2 module by clearing its PMD bit. CLK_Initialize leaves I2C2
 * PMD-gated OFF (PMD5=0xfeefd5ff, I2C2MD set), so WITHOUT this every write to
 * I2C2CON/BRG is ignored, ON never latches, and every transfer times out --
 * indistinguishable from an empty bus. Clearing CFGCON.PMDLOCK opens PMD5 for a
 * direct write; the whole open->write->close runs interrupts-off so PMDLOCK is
 * never left open to a preemptor (PMDL1WAY is OFF per #664). Same pattern as
 * UserSpi spi_SetPmd / UserUart uart_SetPmd. */
static void i2c_SetPmd(void) {
    uint32_t st = __builtin_disable_interrupts();
    i2c_CfgconUnlockedWrite(CFGCON & ~(uint32_t)_CFGCON_PMDLOCK_MASK);
    PMD5CLR = _PMD5_I2C2MD_MASK;
    i2c_CfgconUnlockedWrite(CFGCON | (uint32_t)_CFGCON_PMDLOCK_MASK);
    __builtin_mtc0(12, 0, st);
}

/* @return true iff the module actually powered up. A PMD-gated module ignores
 * every SFR write and reads back 0, so ON failing to latch is the definitive
 * "not clocked" signal -- the guard that keeps a dead module from masquerading
 * as an empty bus (both scan empty otherwise). */
static bool i2c_ModuleInit(void) {
    i2c_SetPmd();                             /* power I2C2 before touching its SFRs */
    ANSELACLR = (1u << 2) | (1u << 3);        /* RA2/RA3 digital (defensive; ODCA set in GPIO init) */
    IEC4CLR = _IEC4_I2C2MIE_MASK;             /* poll the master flag, never take the interrupt */
    IEC4CLR = _IEC4_I2C2BIE_MASK;             /* bus-collision interrupt off too */
    IFS4CLR = _IFS4_I2C2MIF_MASK;
    I2C2CONCLR = _I2C2CON_ON_MASK;
    I2C2BRG    = gBrg;
    I2C2CONCLR = _I2C2CON_SIDL_MASK;
    I2C2CONCLR = _I2C2CON_DISSLW_MASK;        /* slew-rate control on (<=400 kHz) */
    I2C2CONCLR = _I2C2CON_SMEN_MASK;
    I2C2CONSET = _I2C2CON_ON_MASK;
    return (I2C2CON & _I2C2CON_ON_MASK) != 0u;
}

/* Free a slave that is holding SDA low: pulse SCL up to 9 times, then STOP. */
static void i2c_BusRecover(void) {
    I2C2CONCLR = _I2C2CON_ON_MASK;            /* pins revert to (open-drain) GPIO */
    TRISACLR = (1u << 2) | (1u << 3);         /* SCL=RA2, SDA=RA3 as outputs */
    LATASET  = (1u << 3);                     /* release SDA */
    for (int i = 0; i < 9; ++i) {
        LATACLR = (1u << 2); i2c_DelayHalfBit();
        LATASET = (1u << 2); i2c_DelayHalfBit();
    }
    LATACLR = (1u << 3); i2c_DelayHalfBit();  /* STOP: SDA low, */
    LATASET = (1u << 2); i2c_DelayHalfBit();  /*   SCL high, */
    LATASET = (1u << 3); i2c_DelayHalfBit();  /*   then SDA high */
    /* Release RA2/RA3 back to inputs before the module retakes them, so we never
     * leave a GPIO output fighting the open-drain bus (the module overrides TRIS
     * once ON, but don't rely on that -- restore the pre-recovery direction). */
    TRISASET = (1u << 2) | (1u << 3);
    I2C2CONSET = _I2C2CON_ON_MASK;
}

/* ------------------------------------------------------------------ */
/* Segment (PCA9516A + DIO claim) -- all-or-nothing acquire */

static bool i2c_SegAcquire(uint8_t idx, const char** err) {
    const I2cSeg_t* s = &gSegs[idx];
    if (!DIO_ClaimChannel(s->sclDio, DIO_OWNER_I2C)) {
        if (err) { *err = "I2C: SCL pin owned by another peripheral"; }
        return false;
    }
    if (!DIO_ClaimChannel(s->sdaDio, DIO_OWNER_I2C)) {
        DIO_ReleaseChannel(s->sclDio, DIO_OWNER_I2C);
        if (err) { *err = "I2C: SDA pin owned by another peripheral"; }
        return false;
    }
    DIO_SetChannelPeripheralInput(s->sclDio);   /* buffer off -> hub drives terminal */
    DIO_SetChannelPeripheralInput(s->sdaDio);
    *(s->enSet) = s->enMask;                     /* raise hub enable */
    return true;
}

static void i2c_SegRelease(uint8_t idx) {
    const I2cSeg_t* s = &gSegs[idx];
    *(s->enClr) = s->enMask;                     /* lower hub enable first */
    DIO_ReleaseChannel(s->sclDio, DIO_OWNER_I2C);/* release BEFORE restore */
    DIO_ReleaseChannel(s->sdaDio, DIO_OWNER_I2C);
    DIO_RestoreChannel(s->sclDio);
    DIO_RestoreChannel(s->sdaDio);
}

/* ------------------------------------------------------------------ */
/* Public API */

void UserI2c_InitEnablesLow(void) {
    /* Create the bus mutex here, at boot (DIO_InitHardware, single-threaded
     * before the scheduler), so the lazy check-then-create in i2c_Mutex() can
     * never race two SCPI interfaces (USB pri-7 vs WiFi pri-2) on a concurrent
     * first Enable -- by the time either runs, gI2cMutex is already non-NULL.
     * Created directly (no critical section) because boot is single-threaded. */
    if (gI2cMutex == NULL) {
        gI2cMutex = xSemaphoreCreateMutexStatic(&gI2cMutexBuf);
    }
    for (uint8_t i = 0; i < USER_I2C_SEGMENT_COUNT; ++i) {
        *(gSegs[i].enClr)     = gSegs[i].enMask;   /* drive low first (no glitch) */
        *(gSegs[i].enTrisClr) = gSegs[i].enMask;   /* then enable as output */
    }
}

bool UserI2c_Enable(const char** err) {
    xSemaphoreTake(i2c_Mutex(), portMAX_DELAY);
    bool ok = true;
    if (!gEnabled) {
        if (gBrg == 0u) { gBrg = i2c_ComputeBrg(gFreqHz); }
        gActualFreqHz = i2c_ActualFromBrg(gBrg);
        /* Acquire the currently-enabled segments (all-or-nothing). */
        uint8_t acquired = 0;
        for (uint8_t i = 0; i < USER_I2C_SEGMENT_COUNT && ok; ++i) {
            if (gSegOn[i]) {
                if (i2c_SegAcquire(i, err)) {
                    acquired |= (uint8_t)(1u << i);
                } else {
                    ok = false;
                }
            }
        }
        if (ok && !i2c_ModuleInit()) {
            /* module never latched ON -> not clocked (PMD/clock fault). */
            ok = false;
            if (err) { *err = "I2C: module failed to power up (PMD/clock)"; }
        }
        if (!ok) {
            for (uint8_t i = 0; i < USER_I2C_SEGMENT_COUNT; ++i) {
                if ((acquired & (1u << i)) != 0u) { i2c_SegRelease(i); }
            }
        } else {
            gEnabled = true;
        }
    }
    xSemaphoreGive(gI2cMutex);
    return ok;
}

bool UserI2c_Disable(void) {
    xSemaphoreTake(i2c_Mutex(), portMAX_DELAY);
    if (gEnabled) {
        I2C2CONCLR = _I2C2CON_ON_MASK;
        for (uint8_t i = 0; i < USER_I2C_SEGMENT_COUNT; ++i) {
            if (gSegOn[i]) { i2c_SegRelease(i); }
        }
        gEnabled = false;
    }
    xSemaphoreGive(gI2cMutex);
    return true;
}

bool UserI2c_IsEnabled(void) {
    xSemaphoreTake(i2c_Mutex(), portMAX_DELAY);
    bool e = gEnabled;
    xSemaphoreGive(gI2cMutex);
    return e;
}

bool UserI2c_SetSegment(uint8_t segment, bool on, const char** err) {
    if (segment < 1u || segment > USER_I2C_SEGMENT_COUNT) {
        if (err) { *err = "I2C: segment must be 1 or 2"; }
        return false;
    }
    uint8_t idx = (uint8_t)(segment - 1u);
    xSemaphoreTake(i2c_Mutex(), portMAX_DELAY);
    bool ok = true;
    if (gSegOn[idx] != on) {
        if (gEnabled) {
            if (on) {
                ok = i2c_SegAcquire(idx, err);
            } else {
                i2c_SegRelease(idx);
            }
        }
        if (ok) { gSegOn[idx] = on; }
    }
    xSemaphoreGive(gI2cMutex);
    return ok;
}

bool UserI2c_GetSegment(uint8_t segment, bool* on) {
    if (segment < 1u || segment > USER_I2C_SEGMENT_COUNT) { return false; }
    xSemaphoreTake(i2c_Mutex(), portMAX_DELAY);
    if (on != NULL) { *on = gSegOn[segment - 1u]; }
    xSemaphoreGive(gI2cMutex);
    return true;
}

bool UserI2c_SetFrequency(uint32_t hz, const char** err) {
    if (hz < USER_I2C_MIN_FREQ_HZ || hz >= USER_I2C_MAX_FREQ_HZ) {
        if (err) { *err = "I2C: frequency out of range (10000..399999 Hz; >=400 kHz barred by erratum #37)"; }
        return false;
    }
    xSemaphoreTake(i2c_Mutex(), portMAX_DELAY);
    gFreqHz = hz;
    gBrg = i2c_ComputeBrg(hz);
    gActualFreqHz = i2c_ActualFromBrg(gBrg);
    if (gEnabled) { I2C2BRG = gBrg; }
    xSemaphoreGive(gI2cMutex);
    return true;
}

uint32_t UserI2c_GetFrequency(void) {
    return gFreqHz;
}

uint32_t UserI2c_GetActualFrequency(void) {
    return gActualFreqHz;
}

uint8_t UserI2c_Scan(uint8_t* addrs, uint8_t maxAddrs) {
    if (addrs == NULL || maxAddrs == 0u) { return 0u; }
    xSemaphoreTake(i2c_Mutex(), portMAX_DELAY);
    uint8_t found = 0u;
    if (gEnabled) {
        for (uint8_t a = 0x08u; a <= 0x77u && found < maxAddrs; ++a) {
            if (i2c_XferLocked(a, NULL, 0u, NULL, 0u) == I2C_XFER_OK) {
                addrs[found++] = a;
            }
        }
    }
    xSemaphoreGive(gI2cMutex);
    return found;
}

bool UserI2c_Transfer(uint8_t addr7, const uint8_t* wdata, uint16_t wlen,
                      uint8_t* rdata, uint16_t rlen, const char** err) {
    if (addr7 > 0x7Fu) {
        if (err) { *err = "I2C: address must be 7-bit (0..0x7F)"; }
        return false;
    }
    if ((wlen > 0u && wdata == NULL) || (rlen > 0u && rdata == NULL)) {
        if (err) { *err = "I2C: null buffer with non-zero length"; }
        return false;
    }
    /* Erratum #6 (A1/A3): I2C misbehaves on a >500 B continuous transfer. Our
     * write+read is one START..STOP transaction, so bound the combined length. */
    if ((uint32_t)wlen + (uint32_t)rlen > USER_I2C_MAX_CONTINUOUS_BYTES) {
        if (err) { *err = "I2C: combined transfer exceeds 500 B (erratum #6)"; }
        return false;
    }
    xSemaphoreTake(i2c_Mutex(), portMAX_DELAY);
    bool ok = false;
    if (!gEnabled) {
        if (err) { *err = "I2C: not enabled"; }
    } else {
        I2cXferResult_t res = i2c_XferLocked(addr7, wdata, wlen, rdata, rlen);
        ok = (res == I2C_XFER_OK);
        if (res == I2C_XFER_TIMEOUT) {
            /* Only a hung bus warrants recovery -- NOT a plain NACK, which is a
             * normal "device absent/declined" and must not toggle the bus. */
            i2c_BusRecover();
            if (err) { *err = "I2C: bus timeout (bus recovered)"; }
        } else if (res == I2C_XFER_NACK) {
            if (err) { *err = "I2C: no ACK from device"; }
        }
    }
    xSemaphoreGive(gI2cMutex);
    return ok;
}
