#include "nvm.h"
#include <string.h>
/* #1153: recursive mutex -- see nvm_MutexInit()/nvm_Lock()/nvm_Unlock()
 * doc comments in nvm.h for why recursive and why an explicit init. */
#include "FreeRTOS.h"
#include "semphr.h"

#define UNUSED(x) (void)(x)

volatile static bool gNvmOperationDone=false;

/* #1153: static allocation, matching this codebase's existing recursive-
 * mutex precedent (wifi_manager.c's gProcessStateMutex /
 * gProcessStateMutexBuf) rather than a heap-backed
 * xSemaphoreCreateRecursiveMutex(), so the mutex cannot fail to allocate
 * at runtime. */
static StaticSemaphore_t gNvmMutexBuf;
static SemaphoreHandle_t gNvmMutex = NULL;

static void NvmEventHandler(uintptr_t context)
{
    gNvmOperationDone = true;
}

void nvm_MutexInit(void)
{
    /* Idempotent: a second call must not replace a handle other code may
     * already be holding (see the header doc comment). */
    if (gNvmMutex == NULL) {
        gNvmMutex = xSemaphoreCreateRecursiveMutexStatic(&gNvmMutexBuf);
    }
}

void nvm_Lock(void)
{
    /* ⚠ No NULL-guard / lazy-create here on purpose. A missing
     * nvm_MutexInit() call is a boot-sequencing bug, and the header doc
     * comment on nvm_MutexInit() explains why that should fail loudly
     * (hang/crash at the first NVM access) rather than silently construct
     * a per-caller mutex instance that coordinates with nobody. */
    xSemaphoreTakeRecursive(gNvmMutex, portMAX_DELAY);
}

void nvm_Unlock(void)
{
    xSemaphoreGiveRecursive(gNvmMutex);
}

bool nvm_ErasePage(uint32_t addr)
{
    uint32_t dmaStatus;           // storage for current DMA state
    uint32_t readByte;
    bool result;

    nvm_Lock();

    // Disable DMA
    if(!(dmaStatus=DMACONbits.SUSPEND))
    {
        DMACONSET=_DMACON_SUSPEND_MASK; // suspend
        while((DMACONbits.DMABUSY)); // wait to be actually suspended
    }


    NVM_CallbackRegister(NvmEventHandler, (uintptr_t)NULL);

    while(NVM_IsBusy() == true);

    /* Erase the Page */
    NVM_PageErase(addr);
    while(gNvmOperationDone == false);
    gNvmOperationDone = false;


    // Restore DMA
    if(!dmaStatus)
    {
        DMACONCLR=_DMACON_SUSPEND_MASK; // resume DMA activity
    }

    NVM_Read(&readByte,1,addr);

    if (readByte==0xFFFFFFFF)
    {
        result = true;
    }
    else
    {
        // TODO: Trap error/write to error log
        //LogMessage("Settings erase error. DaqifiSettings.c ln 445\n\r");
        result = true;
    }

    nvm_Unlock();
    return result;
}

bool nvm_WriteWordtoAddr(uint32_t addr, uint32_t data)
{
    uint32_t dmaStatus;           // storage for current DMA state
    bool result;

    nvm_Lock();

    // Disable DMA
    if(!(dmaStatus=DMACONbits.SUSPEND))
    {
        DMACONSET=_DMACON_SUSPEND_MASK; // suspend
        while((DMACONbits.DMABUSY)); // wait to be actually suspended
    }

    NVM_CallbackRegister(NvmEventHandler, (uintptr_t)NULL);

    while(NVM_IsBusy() == true);

    NVM_WordWrite(data,addr);
    while(gNvmOperationDone == false);
    gNvmOperationDone = false;

    // Restore DMA
    if(!dmaStatus)
    {
        DMACONCLR=_DMACON_SUSPEND_MASK; // resume DMA activity
    }


    if (!memcmp(&data, (uint32_t *) PA_TO_KVA1(addr), sizeof(data)))
    {
        result = true;
    }
    else
    {
        // TODO: Trap error/write to error log
        //LogMessage("Settings write error. DaqifiSettings.c ln 492\n\r");
        result = false;
    }

    nvm_Unlock();
    return result;
}

uint32_t nvm_ReadfromAddr(uint32_t addr)
{
    uint32_t word;
    NVM_Read(&word,1,addr);
    return word;
}

bool nvm_WriteRowtoAddr(uint32_t addr, uint8_t rowdata[])
{

    uint32_t dmaStatus;           // storage for current DMA state
    bool result;

    nvm_Lock();

    // Disable DMA
    if(!(dmaStatus=DMACONbits.SUSPEND))
    {
        DMACONSET=_DMACON_SUSPEND_MASK; // suspend
        while((DMACONbits.DMABUSY)); // wait to be actually suspended
    }


    NVM_CallbackRegister(NvmEventHandler, (uintptr_t)NULL);

    while(NVM_IsBusy() == true);

    /* Write a word of data to address, using data array as the source */
    NVM_RowWrite((uint32_t *)rowdata,addr);
    while(gNvmOperationDone == false);
    gNvmOperationDone = false;


    // Restore DMA
    if(!dmaStatus)
    {
        DMACONCLR=_DMACON_SUSPEND_MASK; // resume DMA activity
    }


    if (!memcmp(rowdata, (uint32_t *) PA_TO_KVA1(addr), NVM_FLASH_ROWSIZE))
    {
        result = true;
    }
    else
    {
        // TODO: Trap error/write to error log
        //LogMessage("Settings write error. DaqifiSettings.c ln 573\n\r");
        result = false;
    }

    nvm_Unlock();
    return result;
}
