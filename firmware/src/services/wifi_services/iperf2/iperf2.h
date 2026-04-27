#ifndef IPERF2_H
#define IPERF2_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "socket.h"

#ifdef __cplusplus
extern "C" {
#endif

// iperf2 wire protocol — see Microchip Harmony net/tcpip/src/iperf.c and
// the original iperf source.  These structs are the on-the-wire format
// and are NOT subject to local style rules (long, big-endian, packed).

#define IPERF2_HEADER_VERSION1   0x80000000U

typedef struct {
    int32_t  id;
    uint32_t tv_sec;
    uint32_t tv_usec;
} Iperf2_PktInfo;  // UDP datagram header

typedef struct {
    uint32_t flags;
    uint32_t total_len1;
    uint32_t total_len2;
    uint32_t stop_sec;
    uint32_t stop_usec;
    uint32_t error_cnt;
    uint32_t outorder_cnt;
    uint32_t datagrams;
    uint32_t jitter1;
    uint32_t jitter2;
} Iperf2_ServerHdr;  // server → client at end of test

typedef struct {
    uint32_t flags;
    uint32_t numThreads;
    uint32_t mPort;
    uint32_t bufferlen;
    uint32_t mWinBand;
    uint32_t mAmount;
} Iperf2_ClientHdr;  // client → server at start of test

typedef enum {
    IPERF2_MODE_IDLE = 0,
    IPERF2_MODE_SERVER,
    IPERF2_MODE_CLIENT
} Iperf2_Mode;

typedef struct {
    Iperf2_Mode mode;
    uint64_t bytes_transferred;
    uint32_t duration_ms;
    uint32_t kbps;          // computed at end of test
    bool     active;
    bool     completed;
} Iperf2_Stats;

// Default iperf2 port (matches `iperf -s` default)
#define IPERF2_DEFAULT_PORT  5001

/**
 * Initialize the iperf2 module.  Call once at boot after wifi_manager init.
 * Allocates the listening/client socket slots; does not open any sockets.
 */
void Iperf2_Initialize(void);

/**
 * Start an iperf2 TCP server on `port`.  Returns true if the listening socket
 * was opened.  Quiesces ADC streaming + SD logging before binding (caller
 * responsibility — this just refuses to start if streaming is active).
 */
bool Iperf2_StartServer(uint16_t port);

/**
 * Start an iperf2 TCP client connecting to `remote_ip:remote_port`, sending
 * for `duration_sec` seconds.  Returns true if the connect was issued.
 * Same quiesce requirement as the server.
 */
bool Iperf2_StartClient(const char* remote_ip, uint16_t remote_port,
                        uint32_t duration_sec);

/**
 * Cancel the in-flight iperf2 session (closes sockets, finalizes stats).
 */
void Iperf2_Stop(void);

/**
 * Snapshot the current stats — safe to call at any time.  Stats reflect the
 * most recent completed test until a new one starts.
 */
void Iperf2_GetStats(Iperf2_Stats* out);

/**
 * Hook into wifi_manager's SocketEventCallback dispatcher.  Returns true if
 * the socket belongs to iperf2 and was handled here.  Wifi_manager calls this
 * as a first step in its dispatch and skips its own handling on true.
 */
bool Iperf2_HandleSocketEvent(SOCKET sock, uint8_t msg_type, void* pMessage);

/**
 * Driver-task hook: must be called once per WifiTask iteration when iperf2
 * is in CLIENT mode.  Performs the deadline check and the main TX send loop.
 * No-op in IDLE / SERVER modes (server is purely callback-driven).
 */
void Iperf2_Tasks(void);

#ifdef __cplusplus
}
#endif

#endif  // IPERF2_H
