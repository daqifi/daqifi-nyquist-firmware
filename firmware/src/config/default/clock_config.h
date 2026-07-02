/*******************************************************************************
  DAQiFi clock configuration — single 200/252 MHz toggle (#487)

  Flip DAQIFI_SYSCLK_252 and rebuild to move the whole device between the
  252 MHz (chip-rated max, DS60001320H §39) and the legacy 200 MHz operating
  points. Every clock-derived constant is computed here so there is exactly
  one place to change; the scattered consumers reference these macros:

    - config/default/initialization.c   PLL config bits (#if), PBxDIV, PFMWS
    - HAL/TimerApi/TimerApi.h            TIMER_CLOCK_FRQ  (streaming timer + timestamps)
    - config/default/FreeRTOSConfig.h    configPERIPHERAL_CLOCK_HZ (tick on PBCLK3)
    - config/default/peripheral/coretimer/plib_coretimer.h  CORE_TIMER_FREQUENCY
    - config/default/configuration.h     SYS_TIME_CPU_CLOCK_FREQUENCY
    - config/default/definitions.h       CPU_CLOCK_FREQUENCY (informational)
    - HAL/ADC/MC12bADC.c                 ADC scan-busy TCLK (#539 cap)
    - config/default/driver/spi/src/drv_spi_local.h  SPI4 (SD+WINC) source clock
    - config/default/peripheral/i2c/master/plib_i2c5_master.c  I2C5 BRG (BQ24297)
    - Util/Logger.c                      UART4 (ICSP debug) BRG

  Header is preprocessor-only (safe to include from C and .S).

  NOTE (#487): the FPLLMULT config-bit pragma cannot take a macro value, so
  initialization.c selects it with `#if DAQIFI_SYSCLK_252` directly rather than
  a DAQIFI_FPLLMULT token. Keep that #if in sync with this toggle.
*******************************************************************************/
#ifndef CLOCK_CONFIG_H
#define CLOCK_CONFIG_H

/* 1 = 252 MHz (chip-rated max, #487) · 0 = 200 MHz (legacy revert path) */
#define DAQIFI_SYSCLK_252   1

#if DAQIFI_SYSCLK_252
  #define DAQIFI_SYSCLK_HZ   252000000UL   /* SPLL: 24 MHz POSC /3 x63 /2 */
  #define DAQIFI_PBCLK_HZ     84000000UL   /* peripheral buses at PBxDIV /3 */
  #define DAQIFI_PBCLK_MHZ           84u    /* = DAQIFI_PBCLK_HZ / 1e6 */
  #define DAQIFI_PBDIV                2u    /* PBxDIVbits.PBDIV = divisor-1 → /3 */
  #define DAQIFI_PFMWS                5u    /* flash wait states (bring-up conservative; datasheet tune-down pending) */
#else
  #define DAQIFI_SYSCLK_HZ   200000000UL   /* SPLL: 24 MHz POSC /3 x50 /2 */
  #define DAQIFI_PBCLK_HZ    100000000UL   /* peripheral buses at PBxDIV /2 */
  #define DAQIFI_PBCLK_MHZ          100u
  #define DAQIFI_PBDIV                1u    /* → /2 */
  #define DAQIFI_PFMWS                3u    /* errata #38: >184 MHz w/ ECC needs 3 */
#endif

/* MIPS M-class core timer (CP0 Count) increments at SYSCLK/2. */
#define DAQIFI_CORE_TIMER_HZ   (DAQIFI_SYSCLK_HZ / 2UL)

/* UARTx BRG for a target baud in BRGH=1 (high-speed) mode: PBCLK/(4·baud) - 1,
 * rounded to nearest (add half-divisor before the integer divide). */
#define DAQIFI_UART_BRGH_DIV(baud)  \
    (((DAQIFI_PBCLK_HZ + (2UL * (baud))) / (4UL * (baud))) - 1UL)

#endif /* CLOCK_CONFIG_H */
