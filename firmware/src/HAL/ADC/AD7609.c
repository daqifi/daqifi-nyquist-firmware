/*! @file AD7609.c 
 * 
 * This file implements the functions to manage the module ADC AD7609. 
 */

#include "AD7609.h"
#include "Util/Delay.h"

#define UNUSED(x) (void)(x)

//! Pointer to the module configuration data structure to be set in initialization
static AD7609ModuleConfig* pModuleConfigAD7609; 
//! Pointer to the module configuration data structure in runtime
//! to be set in initialization
static AInModuleRuntimeConfig* pModuleRuntimeConfigAD7609; 

/*! 
 * Function to configre SPI related to ADC
 */
static void AD7609_Apply_SPI_Config( void )
{
    //Disable SPI
    PLIB_SPI_Disable(pModuleConfigAD7609->SPI.spiID);
    // Optional: Clear SPI interrupts and status flag.
    //clear SPI buffer
    PLIB_SPI_BufferClear (pModuleConfigAD7609->SPI.spiID);
    // Configure General SPI Options
    PLIB_SPI_StopInIdleDisable(pModuleConfigAD7609->SPI.spiID);
    PLIB_SPI_PinEnable( pModuleConfigAD7609->SPI.spiID,                     \
                        SPI_PIN_DATA_OUT|SPI_PIN_DATA_IN);
    PLIB_SPI_CommunicationWidthSelect(                                      \
                        pModuleConfigAD7609->SPI.spiID,                     \
                        pModuleConfigAD7609->SPI.busWidth);
    PLIB_SPI_InputSamplePhaseSelect(                                        \
                        pModuleConfigAD7609->SPI.spiID,                     \
                        pModuleConfigAD7609->SPI.inSamplePhase);
    PLIB_SPI_ClockPolaritySelect(                                           \
                        pModuleConfigAD7609->SPI.spiID,                     \
                        pModuleConfigAD7609->SPI.clockPolarity);
    PLIB_SPI_OutputDataPhaseSelect(                                         \
                        pModuleConfigAD7609->SPI.spiID,                     \
                        pModuleConfigAD7609->SPI.outDataPhase);
    
    PLIB_SPI_BaudRateClockSelect (                                          \
                        pModuleConfigAD7609->SPI.spiID,                     \
                        pModuleConfigAD7609->SPI.clock);
    PLIB_SPI_BaudRateSet(                                                   \
                        pModuleConfigAD7609->SPI.spiID,                     \
                        SYS_CLK_PeripheralFrequencyGet(                     \
                        pModuleConfigAD7609->SPI.busClk_id),                \
                        pModuleConfigAD7609->SPI.baud);
    PLIB_SPI_MasterEnable(pModuleConfigAD7609->SPI.spiID);
    PLIB_SPI_FramedCommunicationDisable(pModuleConfigAD7609->SPI.spiID);
    // Optional: Enable interrupts.
    // Enable the module
    PLIB_SPI_Enable(pModuleConfigAD7609->SPI.spiID);
    while(PLIB_SPI_IsBusy(pModuleConfigAD7609->SPI.spiID));
}

/*!
 *  Reset the module 
 */
static void AD7609_Reset( void )
{
    // Reset AD7609 by pulling RST line high for > 100ns
    PLIB_PORTS_PinWrite(pModuleConfigAD7609->DataModule,                    \
                        pModuleConfigAD7609->CS_Ch ,                        \
                        pModuleConfigAD7609->CS_Bit,                        \
                        true);
    PLIB_PORTS_PinWrite(pModuleConfigAD7609->DataModule,                    \
                        pModuleConfigAD7609->RST_Ch ,                       \
                        pModuleConfigAD7609->RST_Bit,                       \
                        true);
    Delay(5);

    PLIB_PORTS_PinWrite(pModuleConfigAD7609->DataModule,                    \
                        pModuleConfigAD7609->RST_Ch ,                       \
                        pModuleConfigAD7609->RST_Bit,                       \
                        false);
    PLIB_PORTS_PinWrite(pModuleConfigAD7609->DataModule,                    \
                        pModuleConfigAD7609->CS_Ch ,                        \
                        pModuleConfigAD7609->CS_Bit,                        \
                        false);
    // Delay after reset for chip to initialize
    Delay(5);
}

bool AD7609_InitHardware(const AD7609ModuleConfig* pBoardConfigInit)
{
    pModuleConfigAD7609 = pBoardConfigInit;
    
    AD7609_Apply_SPI_Config();
    
    PLIB_PORTS_PinWrite(pModuleConfigAD7609->DataModule,                    \
                        pModuleConfigAD7609->CS_Ch ,                        \
                        pModuleConfigAD7609->CS_Bit,                        \
                        true);
    PLIB_PORTS_PinWrite(pModuleConfigAD7609->DataModule,                    \
                        pModuleConfigAD7609->RST_Ch ,                       \
                        pModuleConfigAD7609->RST_Bit,                       \
                        true);
    PLIB_PORTS_PinWrite(pModuleConfigAD7609->DataModule,                    \
                        pModuleConfigAD7609->Range_Ch,                      \
                        pModuleConfigAD7609->Range_Bit,                     \
                        pModuleConfigAD7609->Range10V);
    PLIB_PORTS_PinWrite(pModuleConfigAD7609->DataModule,                    \
                        pModuleConfigAD7609->OS0_Ch ,                       \
                        pModuleConfigAD7609->OS0_Bit,                       \
                        pModuleConfigAD7609->OSMode & 0b01);
    PLIB_PORTS_PinWrite(pModuleConfigAD7609->DataModule,                    \
                        pModuleConfigAD7609->OS1_Ch ,                       \
                        pModuleConfigAD7609->OS1_Bit,                       \
                        pModuleConfigAD7609->OSMode & 0b10);
    PLIB_PORTS_PinWrite(pModuleConfigAD7609->DataModule,                    \
                        pModuleConfigAD7609->CONVST_Ch,                     \
                        pModuleConfigAD7609->CONVST_Bit,                    \
                        false);
    
    // Board reset occurs later (AD7609_UpdateModuleState)
    
    return true;
}

bool AD7609_WriteModuleState(bool isPowered)
{
    if (!isPowered)
    {
        return false;
    }
    
    // TODO: Do we expose the STBY pin?
    bool isEnabled = !(PLIB_PORTS_PinGet(                                   \
                        pModuleConfigAD7609->DataModule,                    \
                        pModuleConfigAD7609->RST_Ch ,                       \
                        pModuleConfigAD7609->RST_Bit));
    if (isEnabled != pModuleRuntimeConfigAD7609->IsEnabled)
    {
        if (pModuleRuntimeConfigAD7609->IsEnabled)
        {
            AD7609_Reset( );
        }
        else
        {
            // Hold in reset
            PLIB_PORTS_PinWrite(                                            \
                        pModuleConfigAD7609->DataModule,                    \
                        pModuleConfigAD7609->RST_Ch ,                       \
                        pModuleConfigAD7609->RST_Bit,                       \
                        pModuleRuntimeConfigAD7609->IsEnabled);
        }
    }
    
    return true;
}

bool AD7609_WriteStateSingle(
        AInModuleRuntimeConfig* moduleRuntimeConfig,
        const AD7609ChannelConfig* channelConfig,
        AInRuntimeConfig* channelRuntimeConfig)
{
    UNUSED(moduleRuntimeConfig);
    UNUSED(channelConfig);
    UNUSED(channelRuntimeConfig);
    // Nothing yet!
    
    return true;
}

bool AD7609_WriteStateAll(                                                  \
                        const AInArray* channelConfig,                      \
                        AInRuntimeArray* channelRuntimeConfig)
{
    int i=0;
    bool result = true;
    for (i=0; i<channelRuntimeConfig->Size; ++i)
    {
        result &= AD7609_WriteStateSingle(                                  \
                        pModuleRuntimeConfigAD7609,                         \
                        &channelConfig->Data[i].Config.AD7609,              \
                        &channelRuntimeConfig->Data[i]);
    }
    
    return result;
}

bool AD7609_ReadSamples(AInSampleArray* samples,                            \
                        const AInArray* channelConfigList,                  \
                        AInRuntimeArray* channelRuntimeConfigList,          \
                        uint32_t triggerTimeStamp)
{
    uint8_t x=0;
	uint8_t SPIData=0;;
	uint16_t Data16[9];
    
    if (!pModuleRuntimeConfigAD7609->IsEnabled)
    {
        return false;
    }
    
    AD7609_Apply_SPI_Config();
    
    // If the chip is not ready, bail out
    if (PLIB_PORTS_PinGet(                                                  \
                        pModuleConfigAD7609->DataModule,                    \
                        pModuleConfigAD7609->BSY_Ch,                        \
                        pModuleConfigAD7609->BSY_Bit))
    {
        return false;
    }
	
    // Select the chip
	PLIB_PORTS_PinWrite(pModuleConfigAD7609->DataModule,                    \
                        pModuleConfigAD7609->CS_Ch,                         \
                        pModuleConfigAD7609->CS_Bit,                        \
                        false);
    
    // Wait for the board to settle
    Delay(5); 
    
    for(x=0;x<9;x++)
    {
        while(PLIB_SPI_IsBusy(pModuleConfigAD7609->SPI.spiID));
		PLIB_SPI_BufferWrite(pModuleConfigAD7609->SPI.spiID,0x00);
        
        while(PLIB_SPI_IsBusy(pModuleConfigAD7609->SPI.spiID));
        // Read buffer
		SPIData=PLIB_SPI_BufferRead(pModuleConfigAD7609->SPI.spiID);	
		Data16[x]=SPIData;	// Store into temporary data array
        
        while(PLIB_SPI_IsBusy(pModuleConfigAD7609->SPI.spiID));
		PLIB_SPI_BufferWrite(pModuleConfigAD7609->SPI.spiID,0x00);
        
        while(PLIB_SPI_IsBusy(pModuleConfigAD7609->SPI.spiID));
        // Read buffer
		SPIData=PLIB_SPI_BufferRead(pModuleConfigAD7609->SPI.spiID);	
		Data16[x]=(Data16[x]<<8)|SPIData;	// Store into temporary data array
	}

    // TODO: Double check calculations
 	while(PLIB_SPI_IsBusy(pModuleConfigAD7609->SPI.spiID));
    
    // Deselect the chip
	PLIB_PORTS_PinWrite(pModuleConfigAD7609->DataModule,                    \
                        pModuleConfigAD7609->CS_Ch,                         \
                        pModuleConfigAD7609->CS_Bit,                        \
                        false);
    
    // Convert the raw data to samples
    for (x=0; x<channelConfigList->Size; ++x)
    {
        const AInRuntimeConfig* runtimeConfig =                             \
                        &(channelRuntimeConfigList->Data[x]);
        if (!runtimeConfig->IsEnabled)
        {
            continue;
        }
        
        // Rather than sending us each 18 bit sample individually (which would waste 6 bits per channel),
        // the AD7609 sends us 18 bytes that we need to dice up into 18-bit chucks
        const AInChannel* channelConfig = &(channelConfigList->Data[x]);
        uint8_t index = channelConfig->Config.AD7609.ChannelIndex;
        uint8_t bitOffset = 2 * channelConfig->Config.AD7609.ChannelIndex;
        // Take the remainder of the current index
        uint16_t lowBits = Data16[index] >> bitOffset; 
        // Take as many bits as needed to make 18
        uint16_t highBits = Data16[index + 1] & (0xFFFF >> (14 - bitOffset)); 
        
        volatile uint32_t tmpData = lowBits & (((uint32_t)highBits) << (16 - bitOffset));
        if( tmpData & 0b100000000000000000 )
        {
            // Convert from negative 18 bit to negative 32 bit 2s compelement
            tmpData = 0xFFFC0000 | tmpData; 
        }
        
        AInSample* sample = &samples->Data[samples->Size];
        sample->Channel = channelConfig->ChannelId;
        // We are using the module trigger timestamp here to allow streaming 
        //to know which are part of the same set
        sample->Timestamp = triggerTimeStamp;  
        // The XYZ_ConvertToVoltage functions are called downstream for 
        //conversion (FPU doesn't work in an ISR)
        sample->Value = tmpData; 
        samples->Size += 1;
	}
    
    return true;
}

bool AD7609_TriggerConversion(const AD7609ModuleConfig* moduleConfig)
{
    AD7609_Apply_SPI_Config();
    
    // If the AD7609 is busy the trigger fails
    if (PLIB_PORTS_PinGet(                                                  \
                        moduleConfig->DataModule,                           \
                        moduleConfig->BSY_Ch,                               \
                        moduleConfig->BSY_Bit))
    {
        return false;
    }

    AD7609_Reset();
    
    // Force the CONVST pin low
    PLIB_PORTS_PinWrite(moduleConfig->DataModule,                           \
                        moduleConfig->CONVST_Ch,                            \
                        moduleConfig->CONVST_Bit,                           \
                        false);	
    Delay(5); // Wait for the board to settle
    // Pull CONVST pin high to start conversion
    PLIB_PORTS_PinWrite(moduleConfig->DataModule,                           \
                        moduleConfig->CONVST_Ch,                            \
                        moduleConfig->CONVST_Bit,                           \
                        true);	

    // Wait until the conversion actually starts
    Delay(1);
    
    // Enable interrupt on change since a conversion has been started
    // TODO: Use semaphore/ to disallow other SPI (temp sensor) transfer while we are waiting for a sample to complete
    PLIB_PORTS_ChannelChangeNoticeEnable(                                   \
                        moduleConfig->DataModule,                           \
                        moduleConfig->BSY_Ch,                               \
                        moduleConfig->BSY_BitMask);
    
    return true;
}

double AD7609_ConvertToVoltage(                                             \
                        const AInRuntimeConfig* runtimeConfig,              \
                        const AInSample* sample)
{
    double vRef = pModuleConfigAD7609->Range10V ? 10.0 : 5.0;
    double divVal = runtimeConfig->IsDifferential ? 512.0 : 262144.0;
    
    return vRef * (sample->Value / divVal);
}