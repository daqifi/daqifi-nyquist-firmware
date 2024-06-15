#include "StringFormatters.h"

#define UNUSED(x) (void)(x)

size_t IpAddr_ToString(bool ipv6, IP_MULTI_ADDRESS* addr, char* buffer, size_t maxLen)
{
    bool converted = false;
    if (ipv6)
    {
        // IPV6
        converted = TCPIP_Helper_IPv6AddressToString(&addr->v6Add, buffer, maxLen);
    }
    else
    {
        // IPV4
        converted = TCPIP_Helper_IPAddressToString(&addr->v4Add, buffer, maxLen);
    }
    
    if (!converted)
    {
        return 0;
    }
    
    return strlen(buffer);
}

bool IpAddr_FromString(const char* buffer, const size_t bufferLen, IP_MULTI_ADDRESS* addr)
{
    if (bufferLen > MAX_TCPIPV4_LEN)
    {
        return TCPIP_Helper_StringToIPv6Address(buffer, &addr->v6Add);
    }
    else
    {
        return TCPIP_Helper_StringToIPAddress(buffer, &addr->v4Add);
    }
}

bool IpAddr_IsIpv6(const char* buffer, const size_t bufferLen)
{
    // TODO: Something more thorough
    UNUSED(buffer);
    if (bufferLen > MAX_TCPIPV4_LEN)
    {
        return true;
    }
    
    return false;
}

size_t MacAddr_ToString(TCPIP_MAC_ADDR* addr, char* buffer, size_t maxLen)
{
    if (!TCPIP_Helper_MACAddressToString(addr, buffer, maxLen))
    {
        return 0;
    }
    
    return strlen(buffer);
}

bool MacAddr_FromString(const char* buffer, const size_t bufferLen, TCPIP_MAC_ADDR* addr)
{
    UNUSED(bufferLen);
    return TCPIP_Helper_StringToMACAddress(buffer, addr->v);
}