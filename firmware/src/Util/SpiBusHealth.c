/**
 * @file SpiBusHealth.c
 * @brief Shared-SPI4 bus jam detection and recovery (#589 Tiers 1-2).
 *
 * See SpiBusHealth.h for the failure-mode background. Pin identities (NQ1):
 *   - SPI4 MISO (SDI4): RA15 (PPS SDI4R=13; digital per ANSELA)
 *   - SD card CS:       RD9  (DRV_SDSPI_CHIP_SELECT_PIN_IDX0)
 *   - WINC1500 CS:      RK4  (WDRV_WINC_SS_PIN)
 */

#include "SpiBusHealth.h"

#include "configuration.h"
#include "definitions.h"
#include "FreeRTOS.h"
#include "task.h"

#define LOG_MODULE LOG_MODULE_GENERAL
#include "Util/Logger.h"

// Enough dummy clocks to drain a card stuck mid-512-byte-block read
// (512 data + 2 CRC + command/response margin), sent with NO chip select.
#define SPI_BUS_RELEASE_BYTES  600U

// Probe sampling: 32 raw reads of the MISO pin; thresholds for the verdict.
#define PROBE_SAMPLES          32U
#define PROBE_JAM_THRESHOLD    24U   // >= this many low samples -> JAMMED
#define PROBE_CLEAR_THRESHOLD  8U    // <= this many low samples -> CLEAR

static volatile SpiBusHealthResult_t gLastResult = SPI_BUS_CLEAR;

// Cross-task flag: written by WifiTask (jam handler) / USB task (SCPI SD
// re-enable), read by the SD task's gate. Plain 32-bit-class bool store/load
// is atomic on PIC32MZ; volatile prevents register caching across tasks.
static volatile bool gSdQuarantined = false;

void SpiBusHealth_SetSdQuarantine(bool quarantine)
{
    gSdQuarantined = quarantine;
}

bool SpiBusHealth_IsSdQuarantined(void)
{
    return gSdQuarantined;
}

static DRV_HANDLE gSpiHandle = DRV_HANDLE_INVALID;
// DRV_SPI instance 0 uses DMA: both buffers must be coherent (KSEG1,
// uncached) and 16-byte aligned or the CPU reads stale cache lines from
// the RX landing zone (false JAMMED verdicts — hit on first bench test).
static uint8_t __attribute__((coherent, aligned(16))) gReleasePattern[SPI_BUS_RELEASE_BYTES];
static uint8_t __attribute__((coherent, aligned(16))) gActiveProbeRx[16];
// SD CMD0 frame sent with NO chip select (pass 2 of the active probe): a
// healthy card must ignore it (CS high); a card with broken/marginal CS
// gating treats itself as selected, interprets the frame, and answers R1 —
// convicting itself. Bench card ...026A pathology, 2026-07-03: quiet on pure
// 0xFF (no command start bit) but answers command-shaped traffic, which is
// why it corrupted live WINC transfers while every idle probe read CLEAR.
// (non-const: XC32 cannot combine .rodata with the coherent section
// attribute — filled once in SpiBusHealth_EnsureClient)
static uint8_t __attribute__((coherent, aligned(16))) gCmd0Frame[16];

SpiBusHealthResult_t SpiBusHealth_LastResult(void)
{
    return gLastResult;
}

SpiBusHealthResult_t SpiBusHealth_ProbeJam(void)
{
    // Precondition: nobody mid-transfer. Both CS lines must read deasserted
    // (high). If not, a legitimate transfer is in flight — don't probe.
    if ((GPIO_PinRead(GPIO_PIN_RD9) == 0U) || (GPIO_PinRead(WDRV_WINC_SS_PIN) == 0U))
    {
        gLastResult = SPI_BUS_BUSY;
        return SPI_BUS_BUSY;
    }

    // Enable the internal pull-up on RA15 (MISO) for the duration of the
    // probe so a released (floating) line reads high regardless of whether
    // the board has an external pull-up. Raw CNPUA access: the generated
    // GPIO PLIB exposes no pull-up API, and the SET/CLR atomic forms avoid
    // a read-modify-write on a live port register.
    bool pullWasEnabled = ((CNPUA & (1UL << 15)) != 0U);
    CNPUASET = (1UL << 15);

    // Let the weak pull-up (~50k) charge the trace before sampling (~us).
    // 32-sample loop below adds its own spacing; a short fixed settle is
    // enough at these RC constants.
    for (volatile uint32_t settle = 0; settle < 500U; settle++)
    {
        // spin ~a few microseconds at 252 MHz
    }

    uint32_t lows = 0;
    for (uint32_t i = 0; i < PROBE_SAMPLES; i++)
    {
        if (GPIO_PinRead(GPIO_PIN_RA15) == 0U)
        {
            lows++;
        }
        for (volatile uint32_t gap = 0; gap < 50U; gap++)
        {
            // ~sub-microsecond spacing between samples
        }
    }

    if (!pullWasEnabled)
    {
        CNPUACLR = (1UL << 15);
    }

    SpiBusHealthResult_t result;
    if (lows >= PROBE_JAM_THRESHOLD)
    {
        result = SPI_BUS_JAMMED;
    }
    else if (lows <= PROBE_CLEAR_THRESHOLD)
    {
        result = SPI_BUS_CLEAR;
    }
    else
    {
        result = SPI_BUS_INDETERMINATE;
    }
    gLastResult = result;
    return result;
}

// Lazily open a dedicated DRV_SPI client on the shared bus (instance 0).
// A separate client keeps the release transfer thread-safe against the SD
// and WINC clients without touching either driver's state machine.
static bool SpiBusHealth_EnsureClient(void)
{
    if (gSpiHandle != DRV_HANDLE_INVALID)
    {
        return true;
    }

    DRV_HANDLE h = DRV_SPI_Open(DRV_SPI_INDEX_0, DRV_IO_INTENT_READWRITE);
    if (h == DRV_HANDLE_INVALID)
    {
        LOG_E("SpiBusHealth: DRV_SPI_Open failed (clients exhausted?)");
        return false;
    }

    DRV_SPI_TRANSFER_SETUP setup = {
        .baudRateInHz = 14000000U,         // match the WINC's real bus speed: marginal card inputs can pass at 1 MHz and fail at 14 MHz
        .clockPhase = DRV_SPI_CLOCK_PHASE_VALID_TRAILING_EDGE,
        .clockPolarity = DRV_SPI_CLOCK_POLARITY_IDLE_LOW,
        .dataBits = DRV_SPI_DATA_BITS_8,
        .chipSelect = SYS_PORT_PIN_NONE,    // dummy clocks: nobody selected
        .csPolarity = DRV_SPI_CS_POLARITY_ACTIVE_LOW,
    };
    if (!DRV_SPI_TransferSetup(h, &setup))
    {
        LOG_E("SpiBusHealth: DRV_SPI_TransferSetup failed");
        DRV_SPI_Close(h);
        return false;
    }

    for (uint32_t i = 0; i < SPI_BUS_RELEASE_BYTES; i++)
    {
        gReleasePattern[i] = 0xFFU;
    }
    for (uint32_t i = 0; i < sizeof(gCmd0Frame); i++)
    {
        gCmd0Frame[i] = 0xFFU;
    }
    gCmd0Frame[1] = 0x40U;  // CMD0 (GO_IDLE) ...
    gCmd0Frame[2] = 0x00U;
    gCmd0Frame[3] = 0x00U;
    gCmd0Frame[4] = 0x00U;
    gCmd0Frame[5] = 0x00U;
    gCmd0Frame[6] = 0x95U;  // ... with its fixed, correct CRC7
    gSpiHandle = h;
    return true;
}


SpiBusHealthResult_t SpiBusHealth_ProbeActive(void)
{
    // Clock 16 dummy bytes with NO chip select asserted while the MISO
    // pull-up is enabled. A healthy bus reads back all 0xFF (floating line
    // pulled high). Any other pattern means some device is answering bare
    // clocks with nothing selected — an active jammer (broken CS gating);
    // the SD card is the only removable device on the bus.
    if ((GPIO_PinRead(GPIO_PIN_RD9) == 0U) || (GPIO_PinRead(WDRV_WINC_SS_PIN) == 0U))
    {
        gLastResult = SPI_BUS_BUSY;
        return SPI_BUS_BUSY;
    }
    if (!SpiBusHealth_EnsureClient())
    {
        return gLastResult;
    }

    bool pullWasEnabled = ((CNPUA & (1UL << 15)) != 0U);
    CNPUASET = (1UL << 15);

    // Pass 1: pure 0xFF (no command start bit) — catches a driver stuck
    // mid-transfer. Pass 2: a CMD0 frame with nobody selected — catches
    // broken CS gating (card answers a command it must not see).
    uint8_t *passTx[2] = { gReleasePattern, gCmd0Frame };
    const char *passName[2] = { "bare clocks", "unselected CMD0" };
    SpiBusHealthResult_t result = SPI_BUS_CLEAR;
    for (uint32_t pass = 0; (pass < 2U) && (result == SPI_BUS_CLEAR); pass++)
    {
        for (uint32_t i = 0; i < sizeof(gActiveProbeRx); i++)
        {
            gActiveProbeRx[i] = 0U;
        }
        DRV_SPI_TRANSFER_HANDLE xfer = DRV_SPI_TRANSFER_HANDLE_INVALID;
        DRV_SPI_WriteReadTransferAdd(gSpiHandle,
                                     (void *)passTx[pass], sizeof(gActiveProbeRx),
                                     gActiveProbeRx, sizeof(gActiveProbeRx), &xfer);
        if (xfer == DRV_SPI_TRANSFER_HANDLE_INVALID)
        {
            result = SPI_BUS_INDETERMINATE;
            break;
        }
        result = SPI_BUS_INDETERMINATE;
        for (uint32_t waitMs = 0; waitMs < 50U; waitMs += 5U)
        {
            DRV_SPI_TRANSFER_EVENT ev = DRV_SPI_TransferStatusGet(xfer);
            if (ev == DRV_SPI_TRANSFER_EVENT_COMPLETE)
            {
                uint32_t nonFF = 0;
                for (uint32_t i = 0; i < sizeof(gActiveProbeRx); i++)
                {
                    if (gActiveProbeRx[i] != 0xFFU)
                    {
                        nonFF++;
                    }
                }
                result = (nonFF == 0U) ? SPI_BUS_CLEAR : SPI_BUS_JAMMED;
                if (nonFF != 0U)
                {
                    LOG_E("SpiBusHealth: ACTIVE jammer - %lu/16 bytes answered %s",
                          (unsigned long)nonFF, passName[pass]);
                }
                break;
            }
            if (ev == DRV_SPI_TRANSFER_EVENT_ERROR)
            {
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(5));
        }
    }

    if (!pullWasEnabled)
    {
        CNPUACLR = (1UL << 15);
    }
    gLastResult = result;
    return result;
}

/**
 * DIAG (#589 deep-dive): cross-CS probe. Clock a 0xFF burst while MANUALLY
 * holding the WINC's CS (RK4) low - the card must ignore clocks (its own CS
 * on RD9 stays high), so any non-0xFF on MISO means the card reacts when the
 * WINC's CS asserts: CS-edge crosstalk onto the card, or card CS-input
 * sensitivity. Only call when WiFi is already failing (this sends junk to
 * the WINC, which resyncs on its next reset).
 */
SpiBusHealthResult_t SpiBusHealth_ProbeCrossCs(void)
{
    if ((GPIO_PinRead(GPIO_PIN_RD9) == 0U) || (GPIO_PinRead(WDRV_WINC_SS_PIN) == 0U))
    {
        gLastResult = SPI_BUS_BUSY;
        return SPI_BUS_BUSY;
    }
    if (!SpiBusHealth_EnsureClient())
    {
        return gLastResult;
    }

    bool pullWasEnabled = ((CNPUA & (1UL << 15)) != 0U);
    CNPUASET = (1UL << 15);

    GPIO_PinClear(WDRV_WINC_SS_PIN);   // assert WINC CS like a real transfer

    for (uint32_t i = 0; i < sizeof(gActiveProbeRx); i++)
    {
        gActiveProbeRx[i] = 0U;
    }
    DRV_SPI_TRANSFER_HANDLE xfer = DRV_SPI_TRANSFER_HANDLE_INVALID;
    DRV_SPI_WriteReadTransferAdd(gSpiHandle, gReleasePattern,
                                 sizeof(gActiveProbeRx),
                                 gActiveProbeRx, sizeof(gActiveProbeRx), &xfer);
    SpiBusHealthResult_t result = SPI_BUS_INDETERMINATE;
    if (xfer != DRV_SPI_TRANSFER_HANDLE_INVALID)
    {
        for (uint32_t waitMs = 0; waitMs < 50U; waitMs += 5U)
        {
            DRV_SPI_TRANSFER_EVENT ev = DRV_SPI_TransferStatusGet(xfer);
            if (ev == DRV_SPI_TRANSFER_EVENT_COMPLETE)
            {
                // The WINC itself answers when its CS is low, so non-FF here
                // is expected FROM THE WINC. The discriminator is the card:
                // repeat with a second burst pattern and compare against the
                // card-out baseline the bench records. Report raw count.
                uint32_t nonFF = 0;
                for (uint32_t i = 0; i < sizeof(gActiveProbeRx); i++)
                {
                    if (gActiveProbeRx[i] != 0xFFU)
                    {
                        nonFF++;
                    }
                }
                LOG_E("SpiBusHealth: cross-CS probe rx nonFF=%lu first4=%02X %02X %02X %02X",
                      (unsigned long)nonFF, gActiveProbeRx[0], gActiveProbeRx[1],
                      gActiveProbeRx[2], gActiveProbeRx[3]);
                result = (nonFF == 0U) ? SPI_BUS_CLEAR : SPI_BUS_JAMMED;
                break;
            }
            if (ev == DRV_SPI_TRANSFER_EVENT_ERROR)
            {
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(5));
        }
    }

    GPIO_PinSet(WDRV_WINC_SS_PIN);     // deassert WINC CS

    if (!pullWasEnabled)
    {
        CNPUACLR = (1UL << 15);
    }
    gLastResult = result;
    return result;
}

/**
 * DIAG (#589 deep-dive): MISO drive-fight probe. Temporarily drive RA15
 * (the shared MISO net) as a GPIO output through 0/1 patterns and read the
 * pin back. With both CS lines high the WINC tri-states MISO, so any
 * read-back mismatch means another driver is fighting us - a card whose
 * damaged DO stage clamps the net when it is actively driven (the one
 * stimulus every passive/active probe so far lacked: they only ever
 * observed a floating pulled-up line). Restores RA15 to input; safe at
 * idle only (returns BUSY if a CS is asserted).
 */
uint32_t SpiBusHealth_ProbeMisoFight(void)
{
    if ((GPIO_PinRead(GPIO_PIN_RD9) == 0U) || (GPIO_PinRead(WDRV_WINC_SS_PIN) == 0U))
    {
        return 0xFFFFFFFFU;  // busy sentinel
    }

    uint32_t mismatches = 0;
    // Drive RA15 as output: PPS input (SDI4) keeps reading the pad, which
    // is exactly what we want - LAT vs PORT disagreement = bus fight.
    TRISACLR = (1UL << 15);
    for (uint32_t i = 0; i < 64U; i++)
    {
        uint32_t level = (i & 1U);
        if (level != 0U) { LATASET = (1UL << 15); } else { LATACLR = (1UL << 15); }
        for (volatile uint32_t settle = 0; settle < 100U; settle++) { }
        uint32_t readBack = ((PORTA >> 15) & 1U);
        if (readBack != level)
        {
            mismatches++;
        }
    }
    TRISASET = (1UL << 15);  // restore input for SDI4
    LOG_E("SpiBusHealth: MISO drive-fight probe - %lu/64 mismatches",
          (unsigned long)mismatches);
    return mismatches;
}

SpiBusHealthResult_t SpiBusHealth_TryRelease(void)
{
    if (!SpiBusHealth_EnsureClient())
    {
        return SpiBusHealth_ProbeJam();  // couldn't clock — report current state
    }

    DRV_SPI_TRANSFER_HANDLE xfer = DRV_SPI_TRANSFER_HANDLE_INVALID;
    DRV_SPI_WriteTransferAdd(gSpiHandle, gReleasePattern,
                             SPI_BUS_RELEASE_BYTES, &xfer);
    if (xfer == DRV_SPI_TRANSFER_HANDLE_INVALID)
    {
        LOG_E("SpiBusHealth: release transfer rejected");
        return SpiBusHealth_ProbeJam();
    }

    // 600 bytes at 1 MHz is ~5 ms; poll with a generous bound.
    for (uint32_t waitMs = 0; waitMs < 100U; waitMs += 5U)
    {
        DRV_SPI_TRANSFER_EVENT ev = DRV_SPI_TransferStatusGet(xfer);
        if ((ev == DRV_SPI_TRANSFER_EVENT_COMPLETE) ||
            (ev == DRV_SPI_TRANSFER_EVENT_ERROR))
        {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }

    SpiBusHealthResult_t after = SpiBusHealth_ProbeJam();
    if (after == SPI_BUS_CLEAR)
    {
        // Idle-clear does not prove transfer-clear: a card with a broken CS
        // input answers bare clocks and only corrupts LIVE traffic (bench
        // card, 2026-07-03). Follow up with the active probe.
        after = SpiBusHealth_ProbeActive();
    }
    LOG_E("SpiBusHealth: release ritual done - bus now %s",
          (after == SPI_BUS_CLEAR) ? "CLEAR" :
          (after == SPI_BUS_JAMMED) ? "STILL JAMMED" :
          (after == SPI_BUS_BUSY) ? "BUSY" : "INDETERMINATE");
    return after;
}
