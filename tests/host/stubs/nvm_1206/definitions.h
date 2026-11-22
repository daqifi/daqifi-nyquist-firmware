/* #1206 host stub for the slice of Harmony's definitions.h that nvm.c uses:
 * the DMACON register (bits, SET and CLR aliases) and the NVM PLIB. Every
 * accessor is a function implemented in test_1206_nvm_bounded_waits.c, so a
 * read is observable and advances the simulated clock -- including the
 * `while (DMACONbits.DMABUSY);` shape the pre-#1206 source spun on. */
#ifndef NVM1206_STUB_DEFINITIONS_H
#define NVM1206_STUB_DEFINITIONS_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct {
    unsigned SUSPEND;
    unsigned DMABUSY;
} nvm1206_DmaconBits_t;

nvm1206_DmaconBits_t *nvm1206_DmaconBits(void);
uint32_t *nvm1206_DmaconSet(void);
uint32_t *nvm1206_DmaconClr(void);
#define DMACONbits (*nvm1206_DmaconBits())
#define DMACONSET  (*nvm1206_DmaconSet())
#define DMACONCLR  (*nvm1206_DmaconClr())
#define _DMACON_SUSPEND_MASK 0x00001000u   /* DMACON bit 12 (DS60001320G) */

typedef void (*NVM_CALLBACK)(uintptr_t context);
void NVM_CallbackRegister(NVM_CALLBACK callback, uintptr_t context);
bool NVM_IsBusy(void);
bool NVM_PageErase(uint32_t address);
bool NVM_WordWrite(uint32_t data, uint32_t address);
bool NVM_RowWrite(uint32_t *data, uint32_t address);
bool NVM_Read(uint32_t *data, uint32_t length, const uint32_t address);

#define NVM_FLASH_ROWSIZE (2048U)                  /* plib_nvm.h */

void *nvm1206_FlashKva1(uint32_t address);
#define PA_TO_KVA1(a) ((uintptr_t)nvm1206_FlashKva1(a))
#endif
