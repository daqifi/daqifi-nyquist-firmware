#ifndef IPERF2_H
#define IPERF2_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "socket.h"

#ifdef __cplusplus
extern "C" {
#endif

// iperf2 wire protocol — see avrxml/asf WINC1500 iperf_server_example
// (module mode) and the original iperf source.  These structs are the
// on-the-wire format and are NOT subject to local style rules.

#define IPERF2_HEADER_VERSION1   0x80000000U

// UDP datagram header — first 12 bytes of every UDP iperf2 packet.
// Negative `id` marks the last packet (end-of-test signal).
typedef struct {
    int32_t  id;
    uint32_t tv_sec;
    uint32_t tv_usec;
} Iperf2_PktInfo;

// server → client at end of test (sent over the same connection)
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
} Iperf2_ServerHdr;

// client → server at start of test (sent as first 24 bytes)
typedef struct {
    uint32_t flags;
    uint32_t numThreads;
    uint32_t mPort;
    uint32_t bufferlen;
    uint32_t mWinBand;
    uint32_t mAmount;
} Iperf2_ClientHdr;

typedef enum {
    IPERF2_MODE_IDLE = 0,
    IPERF2_MODE_TCP_SERVER,
    IPERF2_MODE_TCP_CLIENT,
    IPERF2_MODE_UDP_SERVER,
    IPERF2_MODE_UDP_CLIENT
} Iperf2_Mode;

typedef struct {
    Iperf2_Mode mode;
    uint64_t bytes_transferred;
    uint32_t duration_ms;
    uint32_t kbps;          // computed at end of test
    // UDP-specific (zero for TCP modes):
    uint32_t udp_total_pkt;
    uint32_t udp_lost_pkt;
    uint32_t udp_outoforder;
    bool     active;
    bool     completed;
} Iperf2_Stats;

// Default iperf2 port (matches `iperf -s` default)
#define IPERF2_DEFAULT_PORT  5001

/**
 * Initialize the iperf2 module.  Call once at boot after wifi_manager init.
 * Does not open any sockets.
 */
void Iperf2_Initialize(void);

/**
 * Start an iperf2 TCP server on `port`.  Returns true if the listening
 * socket was opened.  Refuses if streaming is active.
 */
bool Iperf2_StartTcpServer(uint16_t port);

/**
 * Start an iperf2 UDP server on `port`.  Counts received bytes/datagrams,
 * tracks sequence/loss, detects end-of-test via negative ID.
 */
bool Iperf2_StartUdpServer(uint16_t port);

/**
 * Start an iperf2 TCP client connecting to `remote_ip:remote_port`,
 * sending for `duration_sec` seconds.  Returns true if connect issued.
 */
bool Iperf2_StartTcpClient(const char* remote_ip, uint16_t remote_port,
                           uint32_t duration_sec);

/**
 * Start an iperf2 UDP client sending to `remote_ip:remote_port` for
 * `duration_sec` seconds.  Sends 1470-byte datagrams (matches
 * IPERF2_UDP_BUF_SIZE) with monotonic sequence IDs.  Final 10
 * packets carry negated ID for end-of-test.
 */
bool Iperf2_StartUdpClient(const char* remote_ip, uint16_t remote_port,
                           uint32_t duration_sec);

/**
 * Cancel the in-flight iperf2 session (closes sockets, finalizes stats).
 */
void Iperf2_Stop(void);

/**
 * Snapshot the current stats — safe to call at any time.  Stats reflect
 * the most recent completed test until a new one starts.
 */
void Iperf2_GetStats(Iperf2_Stats* out);

/**
 * Hook into wifi_manager's SocketEventCallback dispatcher.  Returns true
 * if the socket belongs to iperf2 and was handled here.  Wifi_manager
 * calls this as the first step in its dispatch.
 */
bool Iperf2_HandleSocketEvent(SOCKET sock, uint8_t msg_type, void* pMessage);

/**
 * Driver-task hook: must be called periodically when iperf2 is in *_CLIENT
 * mode.  Drives the TX deadline + send loop.  No-op in IDLE / *_SERVER
 * modes (servers are purely callback-driven).
 *
 * Called from the dedicated Iperf2 task at adaptive cadence (1 ms when
 * active, 50 ms idle).
 */
void Iperf2_Tasks(void);

/**
 * Create the dedicated iperf2 FreeRTOS task.  Call once at boot, after
 * Iperf2_Initialize().  Task self-paces: 1 ms tick during active client
 * mode, 50 ms tick when idle/server (cheap).
 *
 * Task priority is 5 (above WifiTask 2, SD 5; below encoder 6, USB SCPI 7).
 */
void Iperf2_StartTask(void);

/**
 * Active-mode task delay tunable for sweep testing.  ms range 0..100;
 * 0 means taskYIELD() (no explicit delay, just give other priorities a
 * chance).  Default 2 ms (empirical peak — see iperf2.c).  Idle
 * (server/IDLE) uses fixed 50 ms.
 */
void Iperf2_SetActiveDelayMs(uint8_t ms);
uint8_t Iperf2_GetActiveDelayMs(void);

#ifdef __cplusplus
}
#endif

#endif  // IPERF2_H
