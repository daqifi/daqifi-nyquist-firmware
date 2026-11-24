/* #1279 host stub: the SPI driver API wdrv_winc_spi.c calls. Transfer-Add
 * only RECORDS the buffer the DMA would use; the test lands the data. */
#ifndef WINCSPI1279_DRV_SPI_H
#define WINCSPI1279_DRV_SPI_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef uintptr_t DRV_HANDLE;
typedef uint32_t DRV_SPI_TRANSFER_HANDLE;
#define DRV_HANDLE_INVALID ((DRV_HANDLE)-1)
#define DRV_SPI_TRANSFER_HANDLE_INVALID ((DRV_SPI_TRANSFER_HANDLE)0xFFFFFFFFu)
#define DRV_IO_INTENT_READWRITE 3
#define DRV_IO_INTENT_BLOCKING 0
typedef enum { DRV_SPI_TRANSFER_EVENT_COMPLETE, DRV_SPI_TRANSFER_EVENT_ERROR } DRV_SPI_TRANSFER_EVENT;
typedef enum { DRV_SPI_CLOCK_PHASE_VALID_LEADING_EDGE } DRV_SPI_CLOCK_PHASE;
typedef enum { DRV_SPI_CLOCK_POLARITY_IDLE_LOW } DRV_SPI_CLOCK_POLARITY;
typedef enum { DRV_SPI_DATA_BITS_8 } DRV_SPI_DATA_BITS;
typedef enum { DRV_SPI_CS_POLARITY_ACTIVE_LOW } DRV_SPI_CS_POLARITY;
typedef struct {
    uint32_t baudRateInHz; uint32_t chipSelect;
    DRV_SPI_CLOCK_PHASE clockPhase; DRV_SPI_CLOCK_POLARITY clockPolarity;
    DRV_SPI_DATA_BITS dataBits; DRV_SPI_CS_POLARITY csPolarity;
} DRV_SPI_TRANSFER_SETUP;
typedef void (*DRV_SPI_TRANSFER_EVENT_HANDLER)(DRV_SPI_TRANSFER_EVENT, DRV_SPI_TRANSFER_HANDLE, uintptr_t);
extern void test1279_spi_rx_add(void *rx, size_t n, DRV_SPI_TRANSFER_HANDLE *h);
extern void test1279_spi_tx_add(const void *tx, size_t n, DRV_SPI_TRANSFER_HANDLE *h);
static inline void DRV_SPI_WriteReadTransferAdd(DRV_HANDLE d, void *tx, size_t ntx, void *rx, size_t nrx, DRV_SPI_TRANSFER_HANDLE *h)
{ (void)d; (void)tx; (void)ntx; test1279_spi_rx_add(rx, nrx, h); }
static inline void DRV_SPI_WriteTransferAdd(DRV_HANDLE d, void *tx, size_t ntx, DRV_SPI_TRANSFER_HANDLE *h)
{ (void)d; test1279_spi_tx_add(tx, ntx, h); }
static inline DRV_HANDLE DRV_SPI_Open(uint32_t i, int intent) { (void)i; (void)intent; return 1; }
static inline void DRV_SPI_Close(DRV_HANDLE h) { (void)h; }
static inline bool DRV_SPI_TransferSetup(DRV_HANDLE h, DRV_SPI_TRANSFER_SETUP *s) { (void)h; (void)s; return true; }
static inline void DRV_SPI_TransferEventHandlerSet(DRV_HANDLE h, DRV_SPI_TRANSFER_EVENT_HANDLER f, uintptr_t c) { (void)h; (void)f; (void)c; }
#endif
