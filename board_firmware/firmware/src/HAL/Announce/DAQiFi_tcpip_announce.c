/*******************************************************************************
  DAQiFi TCPIP Announce Return
    
  Description:
    - Provides device info protobuf string

*******************************************************************************/

#include "DAQiFi_tcpip_announce.h"
#include "nanopb/DaqifiOutMessage.pb.h"
#include "tcpip/tcpip.h"
#include "Util/ArrayWrapper.h"
#include "../src/state/data/BoardData.h"
#include "tcpip/src/tcpip_private.h"

extern const NanopbFlagsArray fields_discovery;
extern size_t Nanopb_Encode(tBoardData* state, const NanopbFlagsArray* fields, uint8_t* buffer, size_t bufferLen);

size_t DAQiFi_TCPIP_ANNOUNCE_Create(uint8_t *buffer, size_t bufferLen)
{
    tBoardData * pBoardData = BoardData_Get(                                \
                            BOARDDATA_ALL_DATA,                             \
                            0 ); 
    size_t count = Nanopb_Encode(                                           \
                        pBoardData,                                         \
                        &fields_discovery,                                  \
                        buffer,                                             \
                        DaqifiOutMessage_size);
    return (count);
}

void DAQiFi_TCPIP_ANNOUNCE_Update (TCPIP_NET_IF* pNetIf)
{
    DaqifiSettings * pWifiSettings = BoardData_Get(                         \
                        BOARDDATA_WIFI_SETTINGS,                            \
                        0 ); 
    bool ipv6 = pWifiSettings->settings.wifi.configFlags &                  \
                        TCPIP_NETWORK_CONFIG_IPV6_ADDRESS;
    if(!ipv6)
    {
        // Store IPv4 Address
        memcpy(         &(pWifiSettings->settings.wifi.ipAddr.v4Add),       \
                        &pNetIf->netIPAddr,                                 \
                        sizeof (IPV4_ADDR));
        // Store subnet mask 
        memcpy(         &(pWifiSettings->settings.wifi.ipMask.v4Add),       \
                        &pNetIf->netMask,                                   \
                        sizeof (IPV4_ADDR));
        // Store default gateway        
        memcpy(         &(pWifiSettings->settings.wifi.gateway.v4Add),      \
                        &pNetIf->netGateway,                                \
                        sizeof (IPV4_ADDR));
        // Store primary DNS        
        memcpy(         &(pWifiSettings->settings.wifi.priDns.v4Add),       \
                        &pNetIf->dnsServer[0],                              \
                        sizeof (IPV4_ADDR));
        // Store secondary DNS
        memcpy(         &(pWifiSettings->settings.wifi.secDns.v4Add),       \
                        &pNetIf->dnsServer[1],                              \
                        sizeof (IPV4_ADDR));
    }

    //  Store MAC Address
    memcpy(             &(pWifiSettings->settings.wifi.macAddr),            \
                        &pNetIf->netMACAddr,                                \
                        sizeof (TCPIP_MAC_ADDR));
}