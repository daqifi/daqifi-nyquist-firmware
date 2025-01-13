#include "nvm.h"
#include <string.h>

#define UNUSED(x) (void)(x)

volatile static bool gNvmOperationDone=false;

static void NvmEventHandler(uintptr_t context)
{
    gNvmOperationDone = true;
}


bool nvm_ErasePage(uint32_t addr)
{
    uint32_t dmaStatus;           // storage for current DMA state
    uint32_t readByte; 
    
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
        return true;
    }
    else
    {
        // TODO: Trap error/write to error log
        //LogMessage("Settings erase error. DaqifiSettings.c ln 445\n\r");
        return true;
    }
}

bool nvm_WriteWordtoAddr(uint32_t addr, uint32_t data)
{
    uint32_t dmaStatus;           // storage for current DMA state
    
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
        return true;
    }
    else
    {
        // TODO: Trap error/write to error log
        //LogMessage("Settings write error. DaqifiSettings.c ln 492\n\r");
        return false;
    }
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
        return true;
    }
    else
    {
        // TODO: Trap error/write to error log
        //LogMessage("Settings write error. DaqifiSettings.c ln 573\n\r");
        return false;
    }
}
