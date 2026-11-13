/* #1255: drv_sdspi_driver_interface.c waits for its MISO-release byte on the
 * CP0 count. On the target the DMA completion interrupt lands during that wait;
 * here each read of the count is one interrupt's worth of bus progress (the
 * test's spiRun), so the wait sees the completion as it would on the board. */
#ifndef SDSPI_1217_XC_H
#define SDSPI_1217_XC_H
#include <stdint.h>
uint32_t test1255_CpCount(void);
#define _CP0_GET_COUNT() test1255_CpCount()
#endif
