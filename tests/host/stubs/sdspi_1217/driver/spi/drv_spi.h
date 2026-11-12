#ifndef SDSPI_1217_DRV_SPI_H
#define SDSPI_1217_DRV_SPI_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "driver/driver_common.h"
#include "system/ports/sys_ports.h"
typedef uintptr_t DRV_SPI_TRANSFER_HANDLE;
#define DRV_SPI_TRANSFER_HANDLE_INVALID ((DRV_SPI_TRANSFER_HANDLE)(-1))
typedef enum { DRV_SPI_TRANSFER_EVENT_PENDING, DRV_SPI_TRANSFER_EVENT_COMPLETE, DRV_SPI_TRANSFER_EVENT_ERROR } DRV_SPI_TRANSFER_EVENT;
typedef void (*DRV_SPI_TRANSFER_EVENT_HANDLER)(DRV_SPI_TRANSFER_EVENT event, DRV_SPI_TRANSFER_HANDLE h, uintptr_t context);
typedef enum { DRV_SPI_CLOCK_PHASE_VALID_LEADING_EDGE } DRV_SPI_CLOCK_PHASE;
typedef enum { DRV_SPI_CLOCK_POLARITY_IDLE_LOW } DRV_SPI_CLOCK_POLARITY;
typedef enum { DRV_SPI_DATA_BITS_8 } DRV_SPI_DATA_BITS;
typedef enum { DRV_SPI_CS_POLARITY_ACTIVE_LOW } DRV_SPI_CS_POLARITY;
typedef struct {
    uint32_t baudRateInHz;
    DRV_SPI_CLOCK_PHASE clockPhase;
    DRV_SPI_CLOCK_POLARITY clockPolarity;
    DRV_SPI_DATA_BITS dataBits;
    SYS_PORT_PIN chipSelect;
    DRV_SPI_CS_POLARITY csPolarity;
} DRV_SPI_TRANSFER_SETUP;
DRV_HANDLE DRV_SPI_Open(uint16_t index, DRV_IO_INTENT intent);
void DRV_SPI_TransferEventHandlerSet(DRV_HANDLE h, DRV_SPI_TRANSFER_EVENT_HANDLER cb, uintptr_t ctx);
bool DRV_SPI_TransferSetup(DRV_HANDLE h, DRV_SPI_TRANSFER_SETUP* setup);
void DRV_SPI_WriteTransferAdd(DRV_HANDLE h, void* buf, size_t n, DRV_SPI_TRANSFER_HANDLE* out);
void DRV_SPI_ReadTransferAdd(DRV_HANDLE h, void* buf, size_t n, DRV_SPI_TRANSFER_HANDLE* out);
bool DRV_SPI_Lock(DRV_HANDLE h, bool lock);
bool DRV_SPI_IsExclusiveHolder(DRV_HANDLE h);
bool DRV_SPI_TransferIsPending(DRV_HANDLE h, DRV_SPI_TRANSFER_HANDLE xfer);
#endif
