#include "MCP73871.h"

void MCP73871_Init(sMCP73871Config config, sMCP73871WriteVars write)
{

    // Battery management initialization
    
//    data->SEL_Val = true;    // true = charger
//    data->PROG2_Val = false;
//    data->TE_Val = true;
//    data->CE_Val = false;
//    data->STAT1_Val = true;
//    data->STAT2_Val = true;
//    data->PG_Val = true;
//    data->status = 0;

    MCP73871_Write(config, write);

    // NOTE: Directions should already be set in MHC
//	PLIB_PORTS_PinDirectionOutputSet(PORTS_ID_0, config.SEL_Ch, config.SEL_Bit);    // Input type selection (Low for USB port, High for AC-DC adapter)
//	PLIB_PORTS_PinDirectionOutputSet(PORTS_ID_0, config.PROG2_Ch, config.PROG2_Bit);// USB port input current limit selection (Low = 100 mA, High = 500 mA)
//	PLIB_PORTS_PinDirectionOutputSet(PORTS_ID_0, config.TE_Ch, config.TE_Bit);      // Timer Enable; Enables Safety Timer when active Low
//	PLIB_PORTS_PinDirectionOutputSet(PORTS_ID_0, config.CE_Ch, config.CE_Bit);      // Device Charge Enable; Enabled when CE = High
//    
//	PLIB_PORTS_PinDirectionInputSet(PORTS_ID_0, config.STAT1_Ch, config.STAT1_Bit);	//STAT1 Charge Status Output 1 (Open-Drain). Low battery output indicator when VBAT>VIN
//	PLIB_PORTS_PinDirectionInputSet(PORTS_ID_0, config.STAT2_Ch, config.STAT2_Bit);	//STAT2 Charge Status Output 2 (Open-Drain)
//	PLIB_PORTS_PinDirectionInputSet(PORTS_ID_0, config.PG_Ch, config.PG_Bit);		//PG* Power-Good Status Output (Open-Drain)

    //ConfigCNPullups(CN20_PULLUP_ENABLE); //Configure weak pullup on CN20 (RD14/STAT1)
}

void MCP73871_Write(sMCP73871Config config, sMCP73871WriteVars write)
{
    PLIB_PORTS_PinWrite(PORTS_ID_0, config.SEL_Ch, config.SEL_Bit, write.SEL_Val);
	PLIB_PORTS_PinWrite(PORTS_ID_0, config.PROG2_Ch, config.PROG2_Bit, write.PROG2_Val);
    PLIB_PORTS_PinWrite(PORTS_ID_0, config.TE_Ch, config.TE_Bit, write.TE_Val);
	PLIB_PORTS_PinWrite(PORTS_ID_0, config.CE_Ch, config.CE_Bit, write.CE_Val);
}

void MCP73871_ChargeEnable(sMCP73871Config config, sMCP73871Data *data, sMCP73871WriteVars *write, bool chargeEnable, bool pONBattPresent)
{
    if(data->chargeAllowed && chargeEnable && data->status!=FAULT && pONBattPresent)
    {
        write->CE_Val = true;
        PLIB_PORTS_PinWrite(PORTS_ID_0, config.CE_Ch, config.CE_Bit, write->CE_Val);
        
        write->TE_Val = false;
        PLIB_PORTS_PinWrite(PORTS_ID_0, config.TE_Ch, config.TE_Bit, write->TE_Val);
    }
    else
    {
        write->TE_Val = true;
        PLIB_PORTS_PinWrite(PORTS_ID_0, config.TE_Ch, config.TE_Bit, write->TE_Val);
        
        write->CE_Val = false;
        PLIB_PORTS_PinWrite(PORTS_ID_0, config.CE_Ch, config.CE_Bit, write->CE_Val);
    }
}


void MCP73871_Read(sMCP73871Config config, sMCP73871Data *data)
{
	data->STAT1_Val=PLIB_PORTS_PinGet(PORTS_ID_0, config.STAT1_Ch, config.STAT1_Bit);
	data->STAT2_Val=PLIB_PORTS_PinGet(PORTS_ID_0, config.STAT2_Ch, config.STAT2_Bit);
	data->PG_Val=PLIB_PORTS_PinGet(PORTS_ID_0, config.PG_Ch, config.PG_Bit);
}


/*
 *  CHARGE CYCLE STATE      STAT1   STAT2   PG
 *   No Input Power Present  Hi-Z    Hi-Z    Hi-Z
 *   Shutdown (VDD = VBAT)   Hi-Z    Hi-Z    Hi-Z
 * 
 *   No battery or charge disabled:
 *   Shutdown (VDD = IN)     Hi-Z    Hi-Z    L
 *   Shutdown (CE = L)       Hi-Z    Hi-Z    L
 *   No Battery Present      Hi-Z    Hi-Z    L
 * 
 *   Charging:
 *   Preconditioning         L       Hi-Z    L
 *   Constant Current        L       Hi-Z    L
 *   Constant Voltage        L       Hi-Z    L
 * 
 *   Charge finished:
 *   Charge Cmplt - Stby     Hi-Z    L       L
 * 
 *   Fault:
 *   Temperature Fault       L       L       L
 *   Timer Fault             L       L       L
 * 
 *   Low Battery:
 *   Low Battery Output      L       Hi-Z    Hi-Z
 * 
 * The UVLO circuit places the device in Shutdown mode
 * if the input supply falls to within approximately 100 mV
 * of the battery voltage.
 * 
 * UVLO Start Threshold =   4.35V
 * UVLO Stop Threshold =    4.13V
 * Low Batt Threshold =     3.1V
 * 
*/

void MCP73871_ComputeStatus(sMCP73871Data *data)
{
    if(data->STAT1_Val && data->STAT2_Val && data->PG_Val)
    {
        //  No input power present or battery only
        data->status = NO_INPUT;
    }
    
    if(data->STAT1_Val && data->STAT2_Val && !data->PG_Val)
    {
        //  No battery or charge disabled
        data->status = NO_BATT;
    }
    
    if(!data->STAT1_Val && data->STAT2_Val && !data->PG_Val)
    {
        //  Charging
        data->status = CHARGING;
    }
    
    if(data->STAT1_Val && !data->STAT2_Val && !data->PG_Val)
    {
        //  Charge complete
        data->status = CHARGE_COMPLETE;
    }
    
    if(!data->STAT1_Val && !data->STAT2_Val && !data->PG_Val)
    {
        //  Fault
        data->status = FAULT;
    }
    
    if(!data->STAT1_Val && data->STAT2_Val && data->PG_Val)
    {
        //  Low battery
        data->status = LOW_BATT;
    }
    
}