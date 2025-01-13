/* 
 * File:   StringFormatter.h
 * Author: Daniel
 *
 * Created on September 13, 2016, 1:15 PM
 */

#ifndef STRINGFORMATTER_H
#define	STRINGFORMATTER_H
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#define MAX_MAC_ADDR_STR_LEN 50
#define MAX_TCPIPV4_STR_LEN 18
#define MAX_TCPIPV6_STR_LEN 50
#ifdef	__cplusplus
extern "C" {
#endif

//    /**
//     * Converts an IP address to a string
//     * @param ipv6 Indicates whether the address is an ipv4 or ipv6 address
//     * @param addr The address to convert
//     * @param buffer [out] The buffer to populate
//     * @param maxLen [out] The buffer length
//     * @return The size of the converted string or 0 on error
//     */
//    size_t IpAddr_ToString(bool ipv6, IP_MULTI_ADDRESS* addr, char* buffer, size_t maxLen);
//
//    /**
//     * Converts a string to an ip address
//     * @param buffer The buffer to read from
//     * @param bufferLen The length of the data in buffer
//     * @param addr [out] The converted address
//     * @return True on success, false otherwise
//     */
//    bool IpAddr_FromString(const char* buffer, const size_t bufferLen, IP_MULTI_ADDRESS* addr);
//    
//    /**
//     * Indicates whether the given ip address is an ipv6 (true) or ipv4 (false) address
//     * @param buffer The buffer
//     * @param bufferLen The length of the data in the buffer
//     */
//    bool IpAddr_IsIpv6(const char* buffer, const size_t bufferLen);
    
        /**
     * Converts an mac address to a string
     * @param addr The address to convert
     * @param buffer [out] The buffer to populate
     * @param maxLen [out] The buffer length
     * @return The size of the converted string or 0 on error
     */
    size_t MacAddr_ToString(uint8_t addr[6], char* buffer, size_t maxLen);

    /**
     * Converts a string to a max address
     * @param buffer The buffer to read from
     * @param bufferLen The length of the data in buffer
     * @param addr [out] The converted address
     * @return True on success, false otherwise
     */
    bool MacAddr_FromString(const char* buffer, const size_t bufferLen, uint8_t addr[6]);
    
#ifdef	__cplusplus
}
#endif

#endif	/* STRINGFORMATTER_H */

