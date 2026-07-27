/* ************************************************************************** */
/** Descriptive File Name

  @Company
    Company Name

  @File Name
    filename.h

  @Summary
    Brief description of the file.

  @Description
    Describe the purpose of this file.
 */
/* ************************************************************************** */

#ifndef _TIMER_API_H    /* Guard against multiple inclusion */
#define _TIMER_API_H

#include "configuration.h"
#include "definitions.h"


/* Provide C++ Compatibility */
#ifdef __cplusplus
extern "C" {
#endif
#include "clock_config.h"

/* The peripheral-bus clock this image was BUILT for. NOT necessarily the clock
 * the silicon is running — use TimerApi_PeripheralClockHz() for that (#716).
 * Kept as the reference value the boot-time mismatch check compares against. */
#define TIMER_CLOCK_FRQ_BUILT DAQIFI_PBCLK_HZ

/**
 * @brief Actual PBCLK3 in Hz, derived from the live clock hardware.
 *
 * #716: the PLL multiplier lives in DEVCFG2, a device Configuration Word, and
 * our USB bootloader deliberately refuses to program config words
 * (bootloader/.../nvm.c:244 "Make sure we are not writing boot area and device
 * configuration bits"), which erratum 45 in DS80000663 Rev R says could not be
 * done at run time anyway ("RTSP of Configuration Words is not functional.
 * Work around: None."). So a field device that takes a firmware update keeps
 * whatever PLL it was manufactured with, while PBxDIV — an ordinary runtime
 * register write in SystemInit — DOES get updated.
 *
 * A unit built at 200 MHz and updated to the 252 MHz image therefore runs
 * 200/3/2 = 33.33 MHz while a compile-time constant claims 42 MHz, and every
 * streamed rate comes out at 33.333/42 = 79.4% of what was asked for. That was
 * measured in the field and reproduced on the bench.
 *
 * Reading the clock tree instead makes one image correct on both silicon
 * configurations. Derived per DS60001320G Register 8-3 (SPLLCON) and
 * Register 8-9 (PBxDIV); see the implementation for the bit encodings.
 *
 * @return PBCLK3 frequency in Hz.
 */
uint32_t TimerApi_PeripheralClockHz(void);

/**
 * @brief True when the running clock matches what this image was built for.
 *
 * False means a config-word/application mismatch (#716) — the device works but
 * every clock-derived rate is scaled. Reported at boot and via CONF:CAP:JSON?.
 */
bool TimerApi_ClockMatchesBuild(void);

typedef enum {
    TMR_INDEX_2 = 2,
    TMR_INDEX_3 = 3,
    TMR_INDEX_4 = 4,
    TMR_INDEX_6 = 6
} timerApi_index_t;

typedef enum {

    TMR_PRESCALE_VALUE_1 = 0x00,
    TMR_PRESCALE_VALUE_2 = 0x01,
    TMR_PRESCALE_VALUE_4 = 0x02,
    TMR_PRESCALE_VALUE_8 = 0x03,
    TMR_PRESCALE_VALUE_16 = 0x04,
    TMR_PRESCALE_VALUE_32 = 0x05,
    TMR_PRESCALE_VALUE_64 = 0x06,
    TMR_PRESCALE_VALUE_256 = 0x07

} timerApi_presScale_t;
void TimerApi_Initialize(uint8_t index);

void TimerApi_Start(uint8_t index);

void TimerApi_Stop(uint8_t index);

void TimerApi_PeriodSet(uint8_t index,uint32_t period);

uint32_t TimerApi_PeriodGet(uint8_t index);

uint32_t TimerApi_CounterGet(uint8_t index);

uint32_t TimerApi_FrequencyGet(uint8_t index);

void TimerApi_PreScalerSet(uint8_t index, timerApi_presScale_t preScale);

uint16_t TimerApi_PreScalerGet(uint8_t index);

void TimerApi_InterruptEnable(uint8_t index);

void TimerApi_InterruptDisable(uint8_t index);

void TimerApi_CallbackRegister( uint8_t index,TMR_CALLBACK callback_fn, uintptr_t context );

    /* Provide C++ Compatibility */
#ifdef __cplusplus
}
#endif

#endif /* _EXAMPLE_FILE_NAME_H */

/* *****************************************************************************
 End of File
 */
