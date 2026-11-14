#ifndef HIF881_SPI_WDRV_WINC_SPI_H
#define HIF881_SPI_WDRV_WINC_SPI_H
/* #881 host-test stub: the SPI transfer nmspi.c sits on. The test provides
 * both functions and plays the WINC's side of the protocol. */
#include <stdbool.h>
#include <stddef.h>
bool WDRV_WINC_SPISend(void *pTransmitData, size_t txSize);
bool WDRV_WINC_SPIReceive(void *pReceiveData, size_t rxSize);
#endif
