#include "StringFormatters.h"
#include "wdrv_winc_common.h"

#define UNUSED(x) (void)(x)
typedef union 
{
    uint16_t Val;
    uint8_t v[2];
    struct __attribute__((packed))
    {
        uint8_t LB;
        uint8_t HB;
    } byte;
    struct __attribute__((packed))
    {
         uint8_t b0:1;
         uint8_t b1:1;
         uint8_t b2:1;
         uint8_t b3:1;
         uint8_t b4:1;
         uint8_t b5:1;
         uint8_t b6:1;
         uint8_t b7:1;
         uint8_t b8:1;
         uint8_t b9:1;
         uint8_t b10:1;
         uint8_t b11:1;
         uint8_t b12:1;
         uint8_t b13:1;
         uint8_t b14:1;
         uint8_t b15:1;
    } bits;
} UINT16_BITS;
/*******************************************************************************
  Helper Functions for Microchip tcpip

  Summary:
    ARCFOUR Cryptography Library
    
  Description:
    Common Microchip helper functions
*******************************************************************************/

/*******************************************************************************
File Name:  helpers.c
Copyright © 2012 released Microchip Technology Inc.  All rights
reserved.

Microchip licenses to you the right to use, modify, copy and distribute
Software only when embedded on a Microchip microcontroller or digital signal
controller that is integrated into your product or third party product
(pursuant to the sublicense terms in the accompanying license agreement).

You should refer to the license agreement accompanying this Software for
additional information regarding your rights and obligations.

SOFTWARE AND DOCUMENTATION ARE PROVIDED “AS IS” WITHOUT WARRANTY OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION, ANY WARRANTY OF
MERCHANTABILITY, TITLE, NON-INFRINGEMENT AND FITNESS FOR A PARTICULAR PURPOSE.
IN NO EVENT SHALL MICROCHIP OR ITS LICENSORS BE LIABLE OR OBLIGATED UNDER
CONTRACT, NEGLIGENCE, STRICT LIABILITY, CONTRIBUTION, BREACH OF WARRANTY, OR
OTHER LEGAL EQUITABLE THEORY ANY DIRECT OR INDIRECT DAMAGES OR EXPENSES
INCLUDING BUT NOT LIMITED TO ANY INCIDENTAL, SPECIAL, INDIRECT, PUNITIVE OR
CONSEQUENTIAL DAMAGES, LOST PROFITS OR LOST DATA, COST OF PROCUREMENT OF
SUBSTITUTE GOODS, TECHNOLOGY, SERVICES, OR ANY CLAIMS BY THIRD PARTIES
(INCLUDING BUT NOT LIMITED TO ANY DEFENSE THEREOF), OR OTHER SIMILAR COSTS.
*******************************************************************************/



#include <ctype.h>
#include <stdarg.h>
#include <string.h>
#include "StringFormatters.h"
typedef union 
{
    uint16_t Val;
    uint8_t v[2];
} MCHP_UINT16_VAL;

typedef union
{
    uint32_t Val;
    uint16_t w[2] __attribute__((packed));
    uint8_t  v[4];
} MCHP_UINT32_VAL;

/*****************************************************************************
  Function:
	void uitoa(uint16_t Value, uint8_t* Buffer)

  Summary:
	Converts an unsigned integer to a decimal string.
	
  Description:
	Converts a 16-bit unsigned integer to a null-terminated decimal string.
	
  Precondition:
	None

  Parameters:
	Value	- The number to be converted
	Buffer	- Pointer in which to store the converted string

  Returns:
  	None
  ***************************************************************************/
static void __attribute__((unused)) uitoa(uint16_t Value, uint8_t* Buffer)
{
	uint8_t i;
	uint16_t Digit;
	uint16_t Divisor;
	bool Printed = false;

	if(Value)
	{
		for(i = 0, Divisor = 10000; i < 5u; i++)
		{
			Digit = Value/Divisor;
			if(Digit || Printed)
			{
				*Buffer++ = '0' + Digit;
				Value -= Digit*Divisor;
				Printed = true;
			}
			Divisor /= 10;
		}
	}
	else
	{
		*Buffer++ = '0';
	}

	*Buffer = '\0';
}			    


/*****************************************************************************
  Function:
	uint8_t hexatob(uint16_t AsciiVal)

  Summary:
	Converts a hex string to a single byte.
	
  Description:
	Converts a two-character ASCII hex string to a single packed byte.
	
  Precondition:
	None

  Parameters:
	AsciiVal - uint16_t where the LSB is the ASCII value for the lower nibble
					and MSB is the ASCII value for the upper nibble.  Each
					must range from '0'-'9', 'A'-'F', or 'a'-'f'.

  Returns:
  	Resulting packed byte 0x00 - 0xFF.
  ***************************************************************************/
static uint8_t hexatob(uint16_t AsciiVal)
{
    MCHP_UINT16_VAL AsciiChars;
    AsciiChars.Val = AsciiVal;

	// Convert lowercase to uppercase
	if(AsciiChars.v[1] > 'F')
		AsciiChars.v[1] -= 'a'-'A';
	if(AsciiChars.v[0] > 'F')
		AsciiChars.v[0] -= 'a'-'A';

	// Convert 0-9, A-F to 0x0-0xF
	if(AsciiChars.v[1] > '9')
		AsciiChars.v[1] -= 'A' - 10;
	else
		AsciiChars.v[1] -= '0';

	if(AsciiChars.v[0] > '9')
		AsciiChars.v[0] -= 'A' - 10;
	else
		AsciiChars.v[0] -= '0';

	// Concatenate
	return (AsciiChars.v[1]<<4) |  AsciiChars.v[0];
}

/*****************************************************************************
  Function:
	uint8_t btohexa_high(uint8_t b)

  Summary:
	Converts the upper nibble of a binary value to a hexadecimal ASCII byte.

  Description:
	Converts the upper nibble of a binary value to a hexadecimal ASCII byte.
	For example, btohexa_high(0xAE) will return 'a'.

  Precondition:
	None

  Parameters:
	b - the byte to convert

  Returns:
  	The upper hexadecimal ASCII byte '0'-'9' or 'a'-'f'.
  ***************************************************************************/
static uint8_t __attribute__((unused)) btohexa_high(uint8_t b)
{
	b >>= 4;
	return (b>0x9u) ? b+'a'-10:b+'0';
}

/*****************************************************************************
  Function:
	uint8_t btohexa_high(uint8_t b)

  Summary:
	Converts the lower nibble of a binary value to a hexadecimal ASCII byte.

  Description:
	Converts the lower nibble of a binary value to a hexadecimal ASCII byte.
	For example, btohexa_high(0xAE) will return 'e'.

  Precondition:
	None

  Parameters:
	b - the byte to convert

  Returns:
  	The lower hexadecimal ASCII byte '0'-'9' or 'a'-'f'.
  ***************************************************************************/
static uint8_t __attribute__((unused)) btohexa_low(uint8_t b)
{
	b &= 0x0F;
	return (b>9u) ? b+'a'-10:b+'0';
}

/*****************************************************************************
  Function:
	char* strncpy_m(char* destStr, size_t destSize, int nStrings, ...)

  Summary:
	Copies multiple strings to a destination

  Description:
	Copies multiple strings to a destination
    but doesn't copy more than destSize characters.
    Useful where the destination is actually an array and an extra \0
    won't be appended to overflow the buffer
    
  Precondition:
	- valid string pointers
    - destSize should be > 0

  Parameters:
	destStr - Pointer to a string to be initialized with the multiple strings provided as arguments.

    destSize    - the maximum size of the destStr field, that cannot be exceeded.
                  An \0 won't be appended if the resulting size is > destSize

    nStrings    - number of string parameters to be copied into destStr

    ...         - variable number of arguments
    
	
  Returns:
	Length of the destination string, terminating \0 (if exists) not included
  ***************************************************************************/
static size_t __attribute__((unused)) strncpy_m(char* destStr, size_t destSize, int nStrings, ...)
{
    va_list     args = {0};
    const char* str;
    char*       end;
    size_t      len;

    destStr[0] = '\0';
    end = destStr + destSize - 1;
    *end = '\0';
    len = 0;
    
    va_start( args, nStrings );
    
    while(nStrings--)
    {
        if(*end)
        {   // if already full don't calculate strlen outside the string area
            len = destSize;
            break;
        }
        
        str = va_arg(args, const char*);
        strncpy(destStr + len, str, destSize - len);
        len += strlen(str);
    }

    va_end( args );
    
    return len;
}


static bool MACAddressToString(const uint8_t macAddr[8], char* buff, size_t buffSize)
{
    if(macAddr && buff && buffSize >= 18)
    {
        const uint8_t *pAdd = (const uint8_t*)macAddr;

        sprintf(buff, "%.2x:%.2x:%.2x:%.2x:%.2x:%.2x", *pAdd, *(pAdd+1), *(pAdd+2), *(pAdd+3), *(pAdd+4), *(pAdd+5));
        return true;
    }
    return false;
}
bool StringToMACAddress(const char* str, uint8_t macAddr[6])
{
    const char  *beg;
    UINT16_BITS    hexDigit;
    int         ix;
    

    beg = str;
    for(ix=0; ix<6; ix++)
    {
        if(!isxdigit(beg[0]) || !isxdigit(beg[1]))
        {
            return false;
        }

        // found valid byte
        hexDigit.v[0] = beg[1];
        hexDigit.v[1] = beg[0];
        *macAddr++ = hexatob(hexDigit.Val);

        // next colon number
        beg += 2;
        if(beg[0] == '\0')
        {
            break;  // done
        }
        else if(beg[0] != ':' && beg[0] != '-')
        {
            return false;   // invalid delimiter
        }
        beg++; // next digit
    }
    
    return ix==5?true:false;    // false if not enough digits    
    
}
//size_t IpAddr_ToString(bool ipv6, IP_MULTI_ADDRESS* addr, char* buffer, size_t maxLen)
//{
//    bool converted = false;
//    if (ipv6)
//    {
//        // IPV6
//        converted = TCPIP_Helper_IPv6AddressToString(&addr->v6Add, buffer, maxLen);
//    }
//    else
//    {
//        // IPV4
//        converted = TCPIP_Helper_IPAddressToString(&addr->v4Add, buffer, maxLen);
//    }
//    
//    if (!converted)
//    {
//        return 0;
//    }
//    
//    return strlen(buffer);
//}
//
//bool IpAddr_FromString(const char* buffer, const size_t bufferLen, IP_MULTI_ADDRESS* addr)
//{
//    if (bufferLen > MAX_TCPIPV4_LEN)
//    {
//        return TCPIP_Helper_StringToIPv6Address(buffer, &addr->v6Add);
//    }
//    else
//    {
//        return TCPIP_Helper_StringToIPAddress(buffer, &addr->v4Add);
//    }
//}
//
//bool IpAddr_IsIpv6(const char* buffer, const size_t bufferLen)
//{
//    // TODO: Something more thorough
//    UNUSED(buffer);
//    if (bufferLen > MAX_TCPIPV4_LEN)
//    {
//        return true;
//    }
//    
//    return false;
//}

 size_t MacAddr_ToString(uint8_t addr[6], char* buffer, size_t maxLen)
{
    if (!MACAddressToString(addr, buffer, maxLen))
    {
        return 0;
    }
    
    return strlen(buffer);
}

bool MacAddr_FromString(const char* buffer, const size_t bufferLen, uint8_t addr[6])
{
    UNUSED(bufferLen);
    return StringToMACAddress(buffer, addr);
}