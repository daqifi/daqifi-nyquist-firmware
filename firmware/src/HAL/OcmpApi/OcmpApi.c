/* ************************************************************************** */
/** Descriptive File Name

  @Company
    Company Name

  @File Name
    filename.c

  @Summary
    Brief description of the file.

  @Description
    Describe the purpose of this file.
 */
/* ************************************************************************** */
#include "OcmpApi.h"
#define DEVCON_PPS_REGISTERS 4
#define OUTPUT_FUNC_OC1 0x0C
#define OUTPUT_FUNC_OC3 0x0B
#define OUTPUT_FUNC_OC4 0x0B
#define OUTPUT_FUNC_OC6 0x0C
#define OUTPUT_FUNC_OC7 0x0C
#define OUTPUT_FUNC_OC8 0x0C
#define OUTPUT_FUNC_NO_CONNECT 0x00
PLIB_TEMPLATE void SystemUnlock()
{	
	SYSKEY = 0x00000000;
	SYSKEY = 0xAA996655;
	SYSKEY = 0x556699AA;
}
PLIB_TEMPLATE void DeviceRegistersUnlock()
{
	*((uint32_t *)(&CFGCON)) &= ~((_CFGCON_IOLOCK_MASK | _CFGCON_PMDLOCK_MASK | _CFGCON_PGLOCK_MASK ) & (DEVCON_PPS_REGISTERS << _CFGCON_PGLOCK_POSITION));
}
PLIB_TEMPLATE void PORTS_RemapOutput(uint8_t outputFunction , GPIO_PIN  remapOutputPin )
{
    *(&RPB0R - 2 + remapOutputPin) = ( ( *((uint32_t *)(&RPB0R - 2 + remapOutputPin)) ) & ~(_RPB0R_RPB0R_MASK) ) | ( (_RPB0R_RPB0R_MASK) & ((outputFunction)<<(_RPB0R_RPB0R_POSITION)) );
}

PLIB_TEMPLATE void SystemLock()
{
	SYSKEY = 0x33333333;
}
void OcmpApi_Initialize(OcmpApi_id_t id) {
    switch (id) {
        case OCMPAPI_ID_1:
            OCMP1_Initialize();
            break;
        case OCMPAPI_ID_3:
            OCMP3_Initialize();
            break;
        case OCMPAPI_ID_4:
            OCMP4_Initialize();
            break;
        case OCMPAPI_ID_6:
            OCMP6_Initialize();
            break;
        case OCMPAPI_ID_7:
            OCMP7_Initialize();
            break;
        case OCMPAPI_ID_8:
            OCMP8_Initialize();
            break;
        default:
            break;
    }
}

void OcmpApi_Enable(OcmpApi_id_t id, uint16_t portRemapPin) {

    switch (id) {
        case OCMPAPI_ID_1:
            SystemUnlock();
            DeviceRegistersUnlock();
            PORTS_RemapOutput(OUTPUT_FUNC_OC1,portRemapPin);
            //SystemLock();
            OCMP1_Enable();
            break;
        case OCMPAPI_ID_3:
            SystemUnlock();
            PORTS_RemapOutput(OUTPUT_FUNC_OC3,portRemapPin);
            SystemLock();
            OCMP3_Enable();
            break;
        case OCMPAPI_ID_4:
            SystemUnlock();
            PORTS_RemapOutput(OUTPUT_FUNC_OC4,portRemapPin);
            SystemLock();
            OCMP4_Enable();
            break;
        case OCMPAPI_ID_6:
            SystemUnlock();
            PORTS_RemapOutput(OUTPUT_FUNC_OC6,portRemapPin);
            SystemLock();
            OCMP6_Enable();
            break;
        case OCMPAPI_ID_7:
            SystemUnlock();
            PORTS_RemapOutput(OUTPUT_FUNC_OC7,portRemapPin);
            SystemLock();
            OCMP7_Enable();
            break;
        case OCMPAPI_ID_8:
            SystemUnlock();
            PORTS_RemapOutput(OUTPUT_FUNC_OC8,portRemapPin);
            SystemLock();
            OCMP8_Enable();
            break;
        default:
            break;
    }
}

void OcmpApi_Disable(OcmpApi_id_t id, uint16_t portRemapPin) {
    switch (id) {
        case OCMPAPI_ID_1:
            SystemUnlock();
            DeviceRegistersUnlock();
            PORTS_RemapOutput(OUTPUT_FUNC_NO_CONNECT,portRemapPin);
            //SystemLock();
            OCMP1_Disable();
            break;
        case OCMPAPI_ID_3:
            SystemUnlock();
            PORTS_RemapOutput(OUTPUT_FUNC_NO_CONNECT,portRemapPin);
            SystemLock();
            OCMP3_Disable();
            break;
        case OCMPAPI_ID_4:
            SystemUnlock();
            PORTS_RemapOutput(OUTPUT_FUNC_NO_CONNECT,portRemapPin);
            SystemLock();
            OCMP4_Disable();
            break;
        case OCMPAPI_ID_6:
            SystemUnlock();
            PORTS_RemapOutput(OUTPUT_FUNC_NO_CONNECT,portRemapPin);
            SystemLock();
            OCMP6_Disable();
            break;
        case OCMPAPI_ID_7:
            SystemUnlock();
            PORTS_RemapOutput(OUTPUT_FUNC_NO_CONNECT,portRemapPin);
            SystemLock();
            OCMP7_Disable();
            break;
        case OCMPAPI_ID_8:
            SystemUnlock();
            PORTS_RemapOutput(OUTPUT_FUNC_NO_CONNECT,portRemapPin);
            SystemLock();
            OCMP8_Disable();
            break;
        default:
            break;
    }
}

void OcmpApi_CompareValueSet(OcmpApi_id_t id, uint16_t value) {
    switch (id) {
        case OCMPAPI_ID_1:
            OCMP1_CompareSecondaryValueSet(value);
            break;
        case OCMPAPI_ID_3:
            OCMP3_CompareSecondaryValueSet(value);
            break;
        case OCMPAPI_ID_4:
            OCMP4_CompareSecondaryValueSet(value);
            break;
        case OCMPAPI_ID_6:
            OCMP6_CompareSecondaryValueSet(value);
            break;
        case OCMPAPI_ID_7:
            OCMP7_CompareSecondaryValueSet(value);
            break;
        case OCMPAPI_ID_8:
            OCMP8_CompareSecondaryValueSet(value);
            break;
        default:
            break;
    }
}

uint16_t OcmpApi_CompareValueGet(OcmpApi_id_t id) {
    uint16_t ret;
    switch (id) {
        case OCMPAPI_ID_1:
            ret=OCMP1_CompareValueGet();
            break;
        case OCMPAPI_ID_3:
            ret=OCMP3_CompareValueGet();
            break;
        case OCMPAPI_ID_4:
            ret=OCMP4_CompareValueGet();
            break;
        case OCMPAPI_ID_6:
            ret=OCMP6_CompareValueGet();
            break;
        case OCMPAPI_ID_7:
            ret=OCMP7_CompareValueGet();
            break;
        case OCMPAPI_ID_8:
            ret=OCMP8_CompareValueGet();
            break;
        default:
            ret=0;
            break;
    }
    return ret;
}