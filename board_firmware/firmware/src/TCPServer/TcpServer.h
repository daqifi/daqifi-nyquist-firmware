/* 
 * File:   IpSockets.h
 * Author: Daniel
 *
 * Created on June 23, 2016, 5:21 PM
 */
#pragma once

#include "state/runtime/TcpServerConfig.h"

#ifdef	__cplusplus
extern "C" {
#endif
    /**
     * Initialization of the TCP server
     */
    void TcpServer_Initialize();
    
    /**
     * State machine entry point for the TCP server
     */
    void TcpServer_ProcessState();
    
    /*! Used for knowing if TCP Server is trying to flush data
     * so we should not try yo put additional data on the buffer
     * @return 1 When bloked, 0 when it is not
     */
    uint8_t TCP_Server_Is_Blocked( void );
    
    bool TCPServer_IsConnected(void);

#ifdef	__cplusplus
}
#endif


