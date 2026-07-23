#pragma once

#include "SCPIInterface.h"

#ifdef	__cplusplus
extern "C" {
#endif

/**
 * SCPI Callback: Sets the direction of one or more pins
 * @param context The associated SCPI context
 * @return SCPI_RES_OK on success SCPI_RES_ERR on error
 */
scpi_result_t SCPI_GPIODirectionSet(scpi_t * context);

/**
 * SCPI Callback: Gets the direction of one or more pins
 * @param context The associated SCPI context
 * @return SCPI_RES_OK on success SCPI_RES_ERR on error
 */
scpi_result_t SCPI_GPIODirectionGet(scpi_t * context);

/**
 * SCPI Callback: Sets the state of one or more pins
 * @param context The associated SCPI context
 * @return SCPI_RES_OK on success SCPI_RES_ERR on error
 */
scpi_result_t SCPI_GPIOStateSet(scpi_t * context);

/**
 * SCPI Callback: Gets the state of one or more pins
 * @param context The associated SCPI context
 * @return SCPI_RES_OK on success SCPI_RES_ERR on error
 */
scpi_result_t SCPI_GPIOStateGet(scpi_t * context);

/**
 * SCPI Callback: Enables or disables one or more more pins
 * @param context The associated SCPI context
 * @return SCPI_RES_OK on success SCPI_RES_ERR on error
 */
scpi_result_t SCPI_GPIOEnableSet(scpi_t * context);

/**
 * SCPI Callback: Gets the enable status of one or more pins
 * @param context The associated SCPI context
 * @return SCPI_RES_OK on success SCPI_RES_ERR on error
 */
scpi_result_t SCPI_GPIOEnableGet(scpi_t * context);


/**
 * SCPI Callback:  Enable PWM on a DIO channel
 * @param context The associated SCPI context
 * @return SCPI_RES_OK on success SCPI_RES_ERR on error
 */
scpi_result_t SCPI_PWMChannelEnableSet (scpi_t * context);

/**
 * SCPI Callback: Get the enable status of a PWM channel
 * @param context The associated SCPI context
 * @return SCPI_RES_OK on success SCPI_RES_ERR on error
 */
scpi_result_t SCPI_PWMChannelEnableGet(scpi_t * context);

/**
 * SCPI Callback:  Set PWM frequency on a DIO channel
 * @param context The associated SCPI context
 * @return SCPI_RES_OK on success SCPI_RES_ERR on error
 */
scpi_result_t SCPI_PWMChannelFrequencySet(scpi_t * context);

/**
 * SCPI Callback: Get PWM frequency of a DIO channel
 * @param context The associated SCPI context
 * @return SCPI_RES_OK on success SCPI_RES_ERR on error
 */
scpi_result_t SCPI_PWMChannelFrequencyGet(scpi_t * context);

/**
 * SCPI Callback:  Set PWM Duty Cycle on a DIO channel
 * @param context The associated SCPI context
 * @return SCPI_RES_OK on success SCPI_RES_ERR on error
 */
scpi_result_t SCPI_PWMChannelDUTYSet(scpi_t * context);

/**
 * SCPI Callback:  Get PWM Duty Cycle of a DIO channel
 * @param context The associated SCPI context
 * @return SCPI_RES_OK on success SCPI_RES_ERR on error
 */
scpi_result_t SCPI_PWMChannelDUTYGet(scpi_t * context);

/* --- programmable clock outputs (#668, epic #664) — DIO:CLOCk:* --- */
/*! SCPI: DIO:CLOCk:CONFig <dio>,<hz> -> configure a REFCLKO output (silent setter;
 *  read the achieved/quantized Hz back with DIO:CLOCk? after enabling). */
scpi_result_t SCPI_DioClockConfig(scpi_t * context);
/*! SCPI: DIO:CLOCk:ENAble <dio>,<0|1> -> start/stop the clock (claims/releases the pin). */
scpi_result_t SCPI_DioClockEnable(scpi_t * context);
/*! SCPI: DIO:CLOCk? <dio> -> achieved output Hz (0 = off). */
scpi_result_t SCPI_DioClockGet(scpi_t * context);

/* --- edge events + pulse-count totalizers (#667, epic #664) --- */
/*! SCPI: DIO:EVENt:ENAble <dio>,<mode> -> arm/disarm edge events on a DIO pin
 *  (mode 0=off,1=rising,2=falling,3=both). Claims the pin; rejected while streaming. */
scpi_result_t SCPI_DioEventEnable(scpi_t * context);
/*! SCPI: DIO:EVENt:ENAble? <dio> -> current armed mode (0 = off). */
scpi_result_t SCPI_DioEventEnableGet(scpi_t * context);
/*! SCPI: DIO:EVENt:COUNt? <dio> -> edge count since armed (0 if not an event pin). */
scpi_result_t SCPI_DioEventCount(scpi_t * context);
/*! SCPI: DIO:EVENt:NEXT? -> pop the oldest FIFO event as "<dio>,<ts>,<edge>,<dropped>"
 *  (or "-1,0,0,<dropped>" when empty). <dropped> is the cumulative FIFO drop-oldest
 *  loss count, so a client sees gaps even on a successful pop. */
scpi_result_t SCPI_DioEventNext(scpi_t * context);
/*! SCPI: DIO:COUNter:ENAble <dio>,<0|1> -> arm/disarm a hardware pulse totalizer
 *  (DIO 0/3/11/12). Claims the pin; rejected while streaming. */
scpi_result_t SCPI_DioCounterEnable(scpi_t * context);
/*! SCPI: DIO:COUNter:ENAble? <dio> -> 1 if a totalizer is armed on the pin, else 0. */
scpi_result_t SCPI_DioCounterEnableGet(scpi_t * context);
/*! SCPI: DIO:COUNter? <dio> -> hardware totalizer count (0 if not armed). */
scpi_result_t SCPI_DioCounterGet(scpi_t * context);
/*! SCPI: DIO:COUNter:CLEar <dio> -> reset the totalizer to 0 (allowed while streaming). */
scpi_result_t SCPI_DioCounterClear(scpi_t * context);
/* --- input-capture measurements (#666, epic #664) — DIO:MEASure:* --- */
/*! SCPI: DIO:MEASure:FREQuency? <dio>[,<gate_ms>] -> frequency in Hz. */
scpi_result_t SCPI_DioMeasFrequency(scpi_t * context);
/*! SCPI: DIO:MEASure:PERiod? <dio> -> rising-to-rising period in microseconds. */
scpi_result_t SCPI_DioMeasPeriod(scpi_t * context);
/*! SCPI: DIO:MEASure:PWIDth? <dio>[,<pol>] -> pulse width us (pol 1=high,0=low). */
scpi_result_t SCPI_DioMeasPulseWidth(scpi_t * context);
/*! SCPI: DIO:MEASure:DUTY? <dio> -> duty cycle percent (0..100). */
scpi_result_t SCPI_DioMeasDuty(scpi_t * context);

#ifdef	__cplusplus
}
#endif
