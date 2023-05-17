/*! @file AD7173.c 
 * 
 * This file implements the functions to manage the module ADC AD7173. 
 */

#include "AD7173.h"
#include "Util/Delay.h"

#define UNUSED(x) (void)(x)

//! Pointer to the module configuration data structure to be set in initialization
static AD7173ModuleConfig* pModuleConfig; 
//! Pointer to the module configuration data structure in runtime
//! to be set in initialization
static AInModuleRuntimeConfig* pModuleRuntimeConfigAD7173; 

/*! 
 * Function to configre SPI related to ADC
 */
static void AD7173_Apply_SPI_Config( void )
{
    //Disable SPI
    PLIB_SPI_Disable(pModuleConfig->SPI.spiID);
    // Optional: Clear SPI interrupts and status flag.
    //clear SPI buffer
    PLIB_SPI_BufferClear (pModuleConfig->SPI.spiID);
    // Configure General SPI Options
    PLIB_SPI_StopInIdleDisable(pModuleConfig->SPI.spiID);
    PLIB_SPI_PinEnable( pModuleConfig->SPI.spiID,                           \
                        SPI_PIN_DATA_OUT|SPI_PIN_DATA_IN);
    PLIB_SPI_CommunicationWidthSelect(                                      \
                        pModuleConfig->SPI.spiID,                           \
                        pModuleConfig->SPI.busWidth);
    PLIB_SPI_InputSamplePhaseSelect(                                        \
                        pModuleConfig->SPI.spiID,                           \
                        pModuleConfig->SPI.inSamplePhase);
    PLIB_SPI_ClockPolaritySelect(                                           \
                        pModuleConfig->SPI.spiID,                           \
                        pModuleConfig->SPI.clockPolarity);
    PLIB_SPI_OutputDataPhaseSelect(                                         \
                        pModuleConfig->SPI.spiID,                           \
                        pModuleConfig->SPI.outDataPhase);
    
    PLIB_SPI_BaudRateClockSelect(                                           \
                        pModuleConfig->SPI.spiID,                           \
                        pModuleConfig->SPI.clock);
    PLIB_SPI_BaudRateSet(pModuleConfig->SPI.spiID,                          \
                        SYS_CLK_PeripheralFrequencyGet(                     \
                        pModuleConfig->SPI.busClk_id),                      \
                        pModuleConfig->SPI.baud);
    PLIB_SPI_MasterEnable(pModuleConfig->SPI.spiID);
    PLIB_SPI_FramedCommunicationDisable(pModuleConfig->SPI.spiID);
    // Optional: Enable interrupts.
    // Enable the module
    PLIB_SPI_Enable(pModuleConfig->SPI.spiID);
    while(PLIB_SPI_IsBusy(pModuleConfig->SPI.spiID));
}

/*! 
 * Reset the module 
 */
static void AD7173_Reset( void )
{
    // Select the chip
	PLIB_PORTS_PinWrite(pModuleConfig->DataModule,                          \
                        pModuleConfig->CS_Ch,                               \
                        pModuleConfig->CS_Bit,                              \
                        false);
    
    uint8_t x;
    for(x=0;x<8;x++)
    {
        while(PLIB_SPI_IsBusy(pModuleConfig->SPI.spiID));
		PLIB_SPI_BufferWrite(pModuleConfig->SPI.spiID,0xFF);
        
        while(PLIB_SPI_IsBusy(pModuleConfig->SPI.spiID));
        PLIB_SPI_BufferRead(pModuleConfig->SPI.spiID);
	}
    
    // Deselect the chip
    PLIB_PORTS_PinWrite(pModuleConfig->DataModule,                          \
                        pModuleConfig->CS_Ch ,                              \
                        pModuleConfig->CS_Bit,                              \
                        true);
}

/*! 
 * Function to wait until a pin has the expected value
 */
static void AD7173_WaitForPinValue(                                         \
                        PORTS_MODULE_ID index,                              \
                        PORTS_CHANNEL channel,                              \
                        PORTS_BIT_POS bitPos,                               \
                        bool value)
{
    while(PLIB_PORTS_PinGet(index, channel, bitPos) != value)
    {
        Delay(5);
    }
}

bool AD7173_InitHardware(const AD7173ModuleConfig* pModuleConfigAD7173,     \
                         const AInArray* channelConfig)
{
    //COMMS register
	uint8_t COMMS;	
    //0x0840 IFMODE register 0x2: iostrength high, data stat on
	uint16_t IFMODE=0b0000100001000000;		
    // SETUP0 register: Unipolar output, buffers on, external Vref
    uint16_t SETUP0=0b0000111100000000;		
    uint16_t deviceID=0;
    
    pModuleConfig = (AD7173ModuleConfig*) pModuleConfigAD7173;

    // Set port directions
    PLIB_PORTS_PinDirectionOutputSet(                                       \
                        pModuleConfig->DataModule,                          \
                        pModuleConfig->CS_Ch ,                              \
                        pModuleConfig->CS_Bit);
    PLIB_PORTS_PinDirectionInputSet(                                        \
                        pModuleConfig->DataModule,                          \
                        pModuleConfig->ERR_Ch ,                             \
                        pModuleConfig->ERR_Bit);
   
	// Initialize all associated ports to known value
    PLIB_PORTS_PinWrite(pModuleConfig->DataModule,                          \
                        pModuleConfig->CS_Ch ,                              \
                        pModuleConfig->CS_Bit, true);
	
    AD7173_Apply_SPI_Config();
	
	// Send reset to AD7173
	AD7173_Reset();
	
	//  Delay at least 500us before accessing registers
    Delay(20);
	
    // Select the chip
    PLIB_PORTS_PinWrite(pModuleConfig->DataModule,                          \
                        pModuleConfig->CS_Ch ,                              \
                        pModuleConfig->CS_Bit,                              \
                        false);
    
	// ADCMODE register is written to in AD7173_StartConv() as a write causes a new conversion
    
	// Write to IFMODE register
	COMMS=0b00000010;	
	while(PLIB_SPI_IsBusy(pModuleConfig->SPI.spiID));
    PLIB_SPI_BufferRead(pModuleConfig->SPI.spiID);
	PLIB_SPI_BufferWrite(pModuleConfig->SPI.spiID,COMMS);
	while(PLIB_SPI_IsBusy(pModuleConfig->SPI.spiID));
    PLIB_SPI_BufferRead(pModuleConfig->SPI.spiID);
	PLIB_SPI_BufferWrite(pModuleConfig->SPI.spiID,(uint8_t)(IFMODE>>8));
	while(PLIB_SPI_IsBusy(pModuleConfig->SPI.spiID));
    PLIB_SPI_BufferRead(pModuleConfig->SPI.spiID);
	PLIB_SPI_BufferWrite(pModuleConfig->SPI.spiID,(uint8_t)IFMODE);
    
    // Write to SETUP0 register
    COMMS=0b00100000;	
	while(PLIB_SPI_IsBusy(pModuleConfig->SPI.spiID));
    PLIB_SPI_BufferRead(pModuleConfig->SPI.spiID);
	PLIB_SPI_BufferWrite(pModuleConfig->SPI.spiID,COMMS);
	while(PLIB_SPI_IsBusy(pModuleConfig->SPI.spiID));
    PLIB_SPI_BufferRead(pModuleConfig->SPI.spiID);
	PLIB_SPI_BufferWrite(pModuleConfig->SPI.spiID,(uint8_t)(SETUP0>>8));
	while(PLIB_SPI_IsBusy(pModuleConfig->SPI.spiID));
    PLIB_SPI_BufferRead(pModuleConfig->SPI.spiID);
	PLIB_SPI_BufferWrite(pModuleConfig->SPI.spiID,(uint8_t)SETUP0);
	
    // Read device ID which should be 0x30DX.
	COMMS=0b01000111;	
	while(PLIB_SPI_IsBusy(pModuleConfig->SPI.spiID));
    PLIB_SPI_BufferRead(pModuleConfig->SPI.spiID);
	PLIB_SPI_BufferWrite(pModuleConfig->SPI.spiID,COMMS);
	while(PLIB_SPI_IsBusy(pModuleConfig->SPI.spiID));
    PLIB_SPI_BufferRead(pModuleConfig->SPI.spiID);
	PLIB_SPI_BufferWrite(pModuleConfig->SPI.spiID,0x00);
	while(PLIB_SPI_IsBusy(pModuleConfig->SPI.spiID));
	deviceID=PLIB_SPI_BufferRead(pModuleConfig->SPI.spiID);
	PLIB_SPI_BufferWrite(pModuleConfig->SPI.spiID,0x00);
	while(PLIB_SPI_IsBusy(pModuleConfig->SPI.spiID));
	deviceID=(deviceID<<8)|PLIB_SPI_BufferRead(pModuleConfig->SPI.spiID);

    deviceID=deviceID>>4;
    SYS_ASSERT(deviceID==0x3CD, "Bad device id!");   

    PLIB_PORTS_PinWrite(pModuleConfig->DataModule,                          \
                        pModuleConfig->CS_Ch ,                              \
                        pModuleConfig->CS_Bit,                              \
                        true);
       
    return (deviceID==0x3CD);
}

bool AD7173_WriteModuleState(bool isPowered)
{
    static bool isEnabled = false;
    
    if (!isPowered)
    {
        return false;
    }
    
    if (pModuleRuntimeConfigAD7173->IsEnabled == isEnabled)
    {
        return false;
    }
    
    // TODO: We actually have no way of enabling or disabling the module
    // leave the module alone for now.
    // AD7173_Reset();
    
    return true;
}

bool AD7173_WriteStateSingle(                                               \
                        const AD7173ChannelConfig* channelConfig,           \
                        AInRuntimeConfig* channelRuntimeConfig)
{
    uint8_t COMMS=0;	//COMMS register
    uint16_t CHSetting=0;
    uint16_t AINPOS=0;
    uint16_t AINNEG=0;
    
    if(channelRuntimeConfig->IsEnabled)
    {
        if(channelRuntimeConfig->IsDifferential)
        {
            if((channelConfig->ChannelIndex)%2==0)  // If channel is even:
            {
                AINPOS=channelConfig->ChannelIndex;
                AINNEG=(channelConfig->ChannelIndex)+1;
            }else
            {
                AINPOS=(channelConfig->ChannelIndex)-1;
                AINNEG=channelConfig->ChannelIndex;
            }
        }else
        {
            AINPOS=channelConfig->ChannelIndex;
            AINNEG=0b10110; // REF-
        }
    
        // Enable, setup sel=0, set AINPOS, AINNEG
        CHSetting=0b1000000000000000|(AINPOS<<5)|AINNEG;		
        
    
        // Select the chip
        PLIB_PORTS_PinWrite(                                                \
                        pModuleConfig->DataModule,                          \
                        pModuleConfig->CS_Ch ,                              \
                        pModuleConfig->CS_Bit, false);

        COMMS=0x10|channelConfig->ChannelIndex;	// Write to CH register
        while(PLIB_SPI_IsBusy(pModuleConfig->SPI.spiID));
        PLIB_SPI_BufferRead(pModuleConfig->SPI.spiID);
        PLIB_SPI_BufferWrite(pModuleConfig->SPI.spiID,COMMS);
        while(PLIB_SPI_IsBusy(pModuleConfig->SPI.spiID));
        PLIB_SPI_BufferRead(pModuleConfig->SPI.spiID);
        PLIB_SPI_BufferWrite(pModuleConfig->SPI.spiID,(uint8_t)(CHSetting>>8));
        while(PLIB_SPI_IsBusy(pModuleConfig->SPI.spiID));
        PLIB_SPI_BufferRead(pModuleConfig->SPI.spiID);
        PLIB_SPI_BufferWrite(pModuleConfig->SPI.spiID,(uint8_t)CHSetting);
        while(PLIB_SPI_IsBusy(pModuleConfig->SPI.spiID));
        
        PLIB_PORTS_PinWrite(                                                \
                        pModuleConfig->DataModule,                          \
                        pModuleConfig->CS_Ch ,                              \
                        pModuleConfig->CS_Bit,                              \
                        true);
    }
        
    return true;
}

bool AD7173_WriteStateAll(                                                  \
                        const AInArray* channelConfig,                      \
                        AInRuntimeArray* channelRuntimeConfig)
{
    int i=0;
    bool result = true;
    for (i=0; i<channelRuntimeConfig->Size; ++i)
    {
        result &= AD7173_WriteStateSingle(                                  \
                        &channelConfig->Data[i].Config.AD7173,              \
                        &channelRuntimeConfig->Data[i]);
    }
    
    return result;
}

bool AD7173_ReadSamples(AInSampleArray* samples,                            \
                        const AInArray* channelConfigList,                  \
                        AInRuntimeArray* channelRuntimeConfigList,          \
                        uint32_t triggerTimeStamp)
{
    uint8_t x=0;
	uint32_t ADCData=0;
    uint8_t ADCStat=0;
    uint8_t ADCCh=0;
    
    if (!pModuleRuntimeConfigAD7173->IsEnabled)
    {
        return false;
    }
   
    AD7173_Apply_SPI_Config();
    
	//Enable CS_DDS
    PLIB_PORTS_PinWrite(pModuleConfig->DataModule,                          \
                        pModuleConfig->CS_Ch,                               \
                        pModuleConfig->CS_Bit,                              \
                        false);
    
    // If the AD7173 is busy, wait to read data
	AD7173_WaitForPinValue(                                                 \
                        pModuleConfig->DataModule,                          \
                        pModuleConfig->SPI_SDI_Ch,                          \
                        pModuleConfig->SPI_SDI_Bit,                         \
                        false);
    
    while(PLIB_SPI_IsBusy(pModuleConfig->SPI.spiID));
    
    // Read 24 data bits and 8 status bits
    // Send read buffer command
    PLIB_SPI_BufferWrite(pModuleConfig->SPI.spiID,0x44);   
    while(PLIB_SPI_IsBusy(pModuleConfig->SPI.spiID));
    
    PLIB_SPI_BufferRead(pModuleConfig->SPI.spiID);

    for(x=0;x<3;x++)
    {
        PLIB_SPI_BufferWrite(pModuleConfig->SPI.spiID,0x00);
        while(PLIB_SPI_IsBusy(pModuleConfig->SPI.spiID));
        
        ADCData=(ADCData<<8)|                                               \
                    ((uint8_t)PLIB_SPI_BufferRead(pModuleConfig->SPI.spiID));
    }
    
    PLIB_SPI_BufferWrite(pModuleConfig->SPI.spiID,0x00);
    while(PLIB_SPI_IsBusy(pModuleConfig->SPI.spiID));
    
    ADCStat=PLIB_SPI_BufferRead(pModuleConfig->SPI.spiID);
    
    ADCCh=ADCStat & 0x0F;
    
    //Disable CS_DDS
	PLIB_PORTS_PinWrite(pModuleConfig->DataModule,                          \
                        pModuleConfig->CS_Ch,                               \
                        pModuleConfig->CS_Bit,                              \
                        true);	
    
    // Convert the raw data to samples
    for (x=0; x<channelConfigList->Size; ++x)
    {
        const AInRuntimeConfig* runtimeConfig =                             \
                        &(channelRuntimeConfigList->Data[x]);
        if (!runtimeConfig->IsEnabled)
        {
            continue;
        }

        const AInChannel* channelConfig = &(channelConfigList->Data[x]);
        uint8_t index = channelConfig->Config.AD7173.ChannelIndex;
        
        if(index==ADCCh)
        {
            AInSample* sample = &samples->Data[samples->Size];
            sample->Channel = channelConfig->ChannelId;
            // We are using the module trigger timestamp here to allow 
            //streaming to know which are part of the same set
            sample->Timestamp = triggerTimeStamp;   
            // The XYZ_ConvertToVoltage functions are called downstream for 
            //conversion (FPU doesn't work in an ISR)
            sample->Value = ADCData; 
            samples->Size += 1;
        }
        
    }
    PLIB_PORTS_ChannelChangeNoticeEnable(                                   \
                        pModuleConfig->DataModule,                          \
                        pModuleConfig->SPI_SDI_Ch,                          \
                        pModuleConfig->SPI_SDI_BitMask);
    return true;
}

bool AD7173_TriggerConversion(const AD7173ModuleConfig* moduleConfig)
{
    //The ADC mode register controls the operating mode of the ADC and the master 
    //clock selection. A write to the ADC mode register resets
    //the filter and the RDY bits and starts a new conversion or calibration.
    
    //0xA010 ADCMODE register 0x1: int ref on, sing_cyc on, single conversion mode  
	uint16_t ADCMODE=0b1010000000010000;
	//Enable CS_DDS	//Enable CS_DDS	
	PLIB_PORTS_PinWrite(moduleConfig->DataModule,                           \
                        moduleConfig->CS_Ch,                                \
                        moduleConfig->CS_Bit,                               \
                        false);
	
	while(PLIB_SPI_IsBusy(moduleConfig->SPI.spiID));
	PLIB_SPI_BufferWrite(moduleConfig->SPI.spiID,0b00000001);
	while(PLIB_SPI_IsBusy(moduleConfig->SPI.spiID));
    PLIB_SPI_BufferRead(moduleConfig->SPI.spiID);
	PLIB_SPI_BufferWrite(moduleConfig->SPI.spiID,(uint8_t)(ADCMODE>>8));
	while(PLIB_SPI_IsBusy(moduleConfig->SPI.spiID));
    PLIB_SPI_BufferRead(moduleConfig->SPI.spiID);
	PLIB_SPI_BufferWrite(moduleConfig->SPI.spiID,(uint8_t)ADCMODE);
    while(PLIB_SPI_IsBusy(moduleConfig->SPI.spiID));
    PLIB_SPI_BufferRead(moduleConfig->SPI.spiID);
      
    // In order to detect the end of conversion, the CS line must be held 
    //low (active) until conversion is complete
    // At that time, CS line can be disabled.
    /* PLIB_PORTS_PinWrite(moduleConfig->DataModule,                        \
                        moduleConfig->CS_Ch,                                \
                        moduleConfig->CS_Bit,                               \
                        true);	//Disable CS_DDS*/
    
    // Enable interrupt on change since a conversion has been started
    // TODO: Use semaphore/ to disallow other SPI (temp sensor) transfer 
    //while we are waiting for a sample to complete
    PLIB_PORTS_ChannelChangeNoticeEnable(                                   \
                        moduleConfig->DataModule,                           \
                        moduleConfig->SPI_SDI_Ch,                           \
                        moduleConfig->SPI_SDI_BitMask);
    
    return false;
}

double AD7173_ConvertToVoltage(                                             \
                        const AInRuntimeConfig* runtimeConfig,              \
                        const AInSample* sample)
{
    double vRef = 5.0;
    double divVal = runtimeConfig->IsDifferential ? 4096.0 :16777216.0;
    
    return vRef * (sample->Value / divVal);
}