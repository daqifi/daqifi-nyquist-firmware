/**
 * @file UserOneWire.c
 * @brief Bit-banged 1-Wire master via DIO buffer-OE open-drain emulation (#669).
 *
 * Structure mirrors UserSpi.c (#665): a lazily-created static mutex serializes
 * the USB-SCPI (pri 7) and WiFi-SCPI (pri 2) tasks. Slot timing comes from the
 * core timer (CP0 Count, not interrupt-driven, so valid inside a critical
 * section). Per-BIT critical sections keep the masked window ~70 us; the reset
 * is one ~1 ms window. Standard-speed timing (Maxim AN126 / DS18B20).
 *
 * BENCH-VALIDATION PENDING (#669): needs a DS18B20 + 4.7k pull-up. The one
 * electrical risk unique to buffer-emulated open-drain is the read-0 logic
 * margin through the 100K read path — scope it on a real sensor.
 */
#define LOG_LVL    LOG_LEVEL_ERROR
#define LOG_MODULE LOG_MODULE_GENERAL
#include "UserOneWire.h"
#include "device.h"
#include "../DIO.h"
#include "../../services/streaming.h"
#include "../../state/data/BoardData.h"
#include "../../Util/Logger.h"
#include "peripheral/coretimer/plib_coretimer.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

/* Standard-speed slot timing, microseconds (Maxim AN126 / DS18B20 datasheet). */
#define OW_W1_LOW   6u     /* write-1 / read: drive-low time  (A) */
#define OW_W1_HIGH  64u    /* write-1 slot remainder          (B) */
#define OW_W0_LOW   60u    /* write-0 drive-low time          (C) */
#define OW_W0_HIGH  10u    /* write-0 slot remainder          (D) */
#define OW_RD_WAIT  8u     /* read: release -> sample (E); 6+8=14us from slot start,
                            * leaving budget for DIO-layer release/read overhead so
                            * the sample stays inside the DS18B20 t_RDV<=15us window */
#define OW_RD_TAIL  55u    /* read slot remainder             (F) */
#define OW_RST_LOW  480u   /* reset drive-low                 (H) */
#define OW_RST_WAIT 70u    /* reset release -> presence sample(I) */
#define OW_RST_TAIL 410u   /* reset slot remainder            (J) */

/* ROM function commands. */
#define OW_CMD_SEARCH  0xF0u

static uint8_t gDio = 0xFFu;   /* claimed channel, 0xFF = disabled */
static bool    gEnabled = false;
/* Set (under gMutex) while a transaction is bit-banging the bus. Read lock-free
 * by SCPI_StartStreaming so streaming can't ARM mid-transaction (the reverse of
 * ow_ready's reject-while-streaming — together they close the TOCTOU where a
 * long 1-Wire critical section would mask the just-started pri-3 stream timer). */
static volatile bool gOwBusy = false;

static SemaphoreHandle_t gMutex = NULL;
static StaticSemaphore_t gMutexBuf;

static SemaphoreHandle_t ow_Mutex(void) {
    if (gMutex == NULL) {
        taskENTER_CRITICAL();
        if (gMutex == NULL) { gMutex = xSemaphoreCreateMutexStatic(&gMutexBuf); }
        taskEXIT_CRITICAL();
    }
    return gMutex;
}

/* ---- gates ---- */

static bool ow_streaming_active(void) {
    return Streaming_IsActiveOnNonWifiInterface() ||
           Streaming_IsActiveOnWifiInterface();
}

static bool ow_powered(void) {
    /* The buffer's strong-low drive runs off +5V_D, present only when powered up
     * (require POWERED_UP; EXT_DOWN's +5V_D availability is unconfirmed). */
    tPowerData* pd = (tPowerData*)BoardData_Get(BOARDDATA_POWER_DATA, 0);
    return (pd != NULL) && (pd->powerState == POWERED_UP);
}

/* ---- core-timer microsecond delay (wrap-safe; valid in a critical section) ---- */

static inline void ow_delay_us(uint32_t us) {
    uint32_t start = CORETIMER_CounterGet();
    uint32_t ticks = us * (CORETIMER_FrequencyGet() / 1000000u);
    while ((uint32_t)(CORETIMER_CounterGet() - start) < ticks) { /* spin */ }
}

/* ---- electrical primitives (open-drain via buffer OE) ---- */

static void ow_drive_low(void) {
    /* Latch 0 while still high-Z so the buffer's first driven value is 0, never a
     * stray 1 (never push-pull +5V_D against the sensor). */
    DIO_DriveChannel(gDio, false);
    DIO_SetChannelPeripheralOutput(gDio);   /* enable buffer driving 0 */
}

static void ow_release(void) {
    DIO_SetChannelPeripheralInput(gDio);    /* buffer off -> terminal high-Z */
}

static bool ow_sample(void) {
    /* Fail-safe default: a read failure (misconfigured pin) reads LOW, which
     * ow_ready()'s idle-must-be-high check rejects — never masquerade as a
     * released (high) bus and pass the health check into a long CS (#704). On
     * success DIO_ReadChannelRaw overwrites this with the real level. */
    bool level = false;
    (void)DIO_ReadChannelRaw(gDio, &level);
    return level;
}

/* ---- slot primitives (each bit in its own short critical section) ---- */

static void ow_write_bit(bool bit) {
    taskENTER_CRITICAL();
    ow_drive_low();
    if (bit) {
        ow_delay_us(OW_W1_LOW);
        ow_release();
        ow_delay_us(OW_W1_HIGH);
    } else {
        ow_delay_us(OW_W0_LOW);
        ow_release();
        ow_delay_us(OW_W0_HIGH);
    }
    taskEXIT_CRITICAL();
}

static bool ow_read_bit(void) {
    bool bit;
    taskENTER_CRITICAL();
    ow_drive_low();
    ow_delay_us(OW_W1_LOW);      /* short low, then let the slave hold or release */
    ow_release();
    ow_delay_us(OW_RD_WAIT);
    bit = ow_sample();
    ow_delay_us(OW_RD_TAIL);
    taskEXIT_CRITICAL();
    return bit;
}

/* Reset + presence detect: one ~1 ms critical section (uninterruptible by design). */
static bool ow_reset_pulse(void) {
    bool present;
    taskENTER_CRITICAL();
    ow_drive_low();
    ow_delay_us(OW_RST_LOW);
    ow_release();
    ow_delay_us(OW_RST_WAIT);
    present = (ow_sample() == false);   /* a slave pulls the bus low = presence */
    ow_delay_us(OW_RST_TAIL);
    taskEXIT_CRITICAL();
    return present;
}

static void ow_write_byte(uint8_t b) {
    for (uint8_t i = 0; i < 8u; i++) { ow_write_bit(((b >> i) & 1u) != 0u); }
}

static uint8_t ow_read_byte(void) {
    uint8_t b = 0u;
    for (uint8_t i = 0; i < 8u; i++) { if (ow_read_bit()) { b |= (uint8_t)(1u << i); } }
    return b;
}

/* ------------------------------------------------------------------ */
/* Public API */

bool UserOneWire_Enable(uint8_t dio, bool enable, const char** err) {
    xSemaphoreTake(ow_Mutex(), portMAX_DELAY);
    bool ok = true;
    if (enable) {
        if (ow_streaming_active()) {
            if (err) { *err = "1-Wire: cannot enable while streaming"; }
            ok = false;
        } else if (!ow_powered()) {
            if (err) { *err = "1-Wire: device must be powered up (SYST:POW:STAT 1)"; }
            ok = false;
        } else if (gEnabled && gDio != dio) {
            if (err) { *err = "1-Wire: already enabled on another channel"; }
            ok = false;
        } else if (!gEnabled && !DIO_ClaimChannel(dio, DIO_OWNER_ONEWIRE)) {
            if (err) { *err = "1-Wire: DIO pin is claimed by another peripheral"; }
            ok = false;
        } else {
            gDio = dio;
            gEnabled = true;
            ow_release();   /* idle bus released (high-Z, sensor pull-up holds high) */
        }
    } else {
        if (gEnabled) {
            ow_release();
            DIO_ReleaseChannel(gDio, DIO_OWNER_ONEWIRE);
            DIO_RestoreChannel(gDio);
            gEnabled = false;
            gDio = 0xFFu;
        }
    }
    xSemaphoreGive(gMutex);
    return ok;
}

bool UserOneWire_IsEnabled(uint8_t* dio) {
    xSemaphoreTake(ow_Mutex(), portMAX_DELAY);
    if (dio) { *dio = gEnabled ? gDio : 0xFFu; }
    bool en = gEnabled;
    xSemaphoreGive(gMutex);
    return en;
}

/* Common preconditions for a transaction; returns false + *err on failure.
 * Re-checks power and streaming EVERY transaction (both can change after enable),
 * and probes bus health (an idle released bus must read high via the pull-up). */
static bool ow_ready(const char** err) {
    if (!gEnabled) { if (err) { *err = "1-Wire: not enabled"; } return false; }
    if (!ow_powered()) {
        if (err) { *err = "1-Wire: device is not powered up (SYST:POW:STAT 1)"; }
        return false;
    }
    if (ow_streaming_active()) {
        if (err) { *err = "1-Wire: unavailable while streaming"; }
        return false;
    }
    /* The bus is left released after every slot, so at idle the pull-up must hold
     * it high; a low reading = short or missing 4.7k pull-up (also stops a
     * shorted bus from fabricating phantom ROM ids in SEARch). */
    if (!ow_sample()) {
        if (err) { *err = "1-Wire: bus stuck low (short or missing pull-up)"; }
        return false;
    }
    return true;
}

bool UserOneWire_Reset(bool* present, const char** err) {
    xSemaphoreTake(ow_Mutex(), portMAX_DELAY);
    bool ok = ow_ready(err);
    if (ok) {
        gOwBusy = true;
        bool p = ow_reset_pulse();
        gOwBusy = false;
        if (present) { *present = p; }
    }
    xSemaphoreGive(gMutex);
    return ok;
}

bool UserOneWire_IsBusy(void) {
    return gOwBusy;   /* lock-free: single volatile bool, atomic on PIC32MZ */
}

bool UserOneWire_Transfer(const uint8_t* wbuf, size_t nWrite,
                          uint8_t* rbuf, size_t nRead, const char** err) {
    /* Reject a NULL buffer with a non-zero length before dereferencing it in the
     * byte loops (matches UserI2c_Transfer's contract) — #704. */
    if ((nWrite > 0u && wbuf == NULL) || (nRead > 0u && rbuf == NULL)) {
        if (err) { *err = "1-Wire: NULL buffer with non-zero length"; }
        return false;
    }
    xSemaphoreTake(ow_Mutex(), portMAX_DELAY);
    bool ok = ow_ready(err);
    if (ok) {
        gOwBusy = true;
        /* Every 1-Wire transaction begins with reset + presence. */
        if (!ow_reset_pulse()) {
            if (err) { *err = "1-Wire: no presence pulse (no device on the bus)"; }
            ok = false;
        } else {
            for (size_t i = 0; i < nWrite; i++) { ow_write_byte(wbuf[i]); }
            for (size_t i = 0; i < nRead; i++)  { rbuf[i] = ow_read_byte(); }
        }
        gOwBusy = false;
    }
    xSemaphoreGive(gMutex);
    return ok;
}

/* Enumerate ROM ids into roms[] (8 bytes each), Maxim AN187 search algorithm.
 * rom[] MUST persist across passes: a discrepancy below lastDiscrepancy re-treads
 * the previous device's path from its stored bits. Always succeeds (returns the
 * devices found); a mid-pass "11" (device dropped off) or reset-with-no-presence
 * gracefully ends enumeration keeping what was already found. */
static bool ow_search(uint8_t* roms, uint8_t maxRoms, uint8_t* count) {
    uint8_t rom[USER_OWIRE_ROM_BYTES] = {0};   /* persistent across passes */
    int lastDiscrepancy = 0;
    bool lastDevice = false;
    uint8_t found = 0u;

    while (!lastDevice && found < maxRoms) {
        if (!ow_reset_pulse()) { break; }   /* no (more) devices on the bus */
        ow_write_byte(OW_CMD_SEARCH);

        int lastZero = 0;
        bool completed = true;
        for (int bitPos = 1; bitPos <= 64; bitPos++) {
            bool idBit  = ow_read_bit();
            bool cmpBit = ow_read_bit();
            int idx = (bitPos - 1) / 8;
            int sh  = (bitPos - 1) % 8;
            int dir;
            if (idBit && cmpBit) {
                /* No device responded this bit (e.g. hot-unplug mid-search) —
                 * end enumeration, keep prior finds. This partial pass is NOT a
                 * valid device, so don't record it. */
                completed = false;
                lastDevice = true;
                break;
            } else if (idBit != cmpBit) {
                dir = idBit ? 1 : 0;        /* all present devices agree */
            } else {                        /* 00 -> discrepancy */
                if (bitPos == lastDiscrepancy) {
                    dir = 1;
                } else if (bitPos > lastDiscrepancy) {
                    dir = 0;
                } else {
                    dir = (rom[idx] >> sh) & 1;   /* re-tread the prior path */
                }
                if (dir == 0) { lastZero = bitPos; }
            }
            /* Fully define this bit so rom[] stays valid for the next pass. */
            if (dir == 1) { rom[idx] |= (uint8_t)(1u << sh); }
            else          { rom[idx] &= (uint8_t)~(1u << sh); }
            ow_write_bit(dir != 0);         /* deselect the other branch */
        }

        if (completed) {
            for (uint8_t i = 0; i < USER_OWIRE_ROM_BYTES; i++) {
                roms[found * USER_OWIRE_ROM_BYTES + i] = rom[i];
            }
            found++;
            lastDiscrepancy = lastZero;
            if (lastDiscrepancy == 0) { lastDevice = true; }
        }
    }
    *count = found;
    return true;
}

bool UserOneWire_Search(uint8_t* roms, uint8_t maxRoms, uint8_t* count,
                        const char** err) {
    xSemaphoreTake(ow_Mutex(), portMAX_DELAY);
    bool ok = ow_ready(err);
    if (ok) {
        uint8_t n = 0u;
        gOwBusy = true;
        bool sok = ow_search(roms, maxRoms, &n);
        gOwBusy = false;
        if (!sok) {
            if (err) { *err = "1-Wire: search bus fault"; }
            ok = false;
        } else if (count) {
            *count = n;
        }
    }
    xSemaphoreGive(gMutex);
    return ok;
}
