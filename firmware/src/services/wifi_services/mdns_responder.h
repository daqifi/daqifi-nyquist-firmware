/**
 * @file mdns_responder.h
 * @brief Minimal software mDNS / DNS-SD responder for the WINC1500 (#345).
 *
 * Advertises the device as a `_daqifi._tcp.local` service so standard
 * zero-config tooling (`dns-sd -B`, `avahi-browse -r`, Bonjour, the Python
 * `zeroconf` module) discovers it on the LAN without the custom UDP-30303
 * broadcast protocol — which WSL2 and AP-isolation networks drop.
 *
 * This is purely ADDITIVE: the UDP-30303 discovery responder is unchanged.
 *
 * Model (see reference_winc_udp_socket_model): the responder owns one UDP
 * socket bound to :5353, joined to the 224.0.0.251 multicast group. It parses
 * inbound mDNS queries and, when asked for our service/host, sends a response
 * carrying PTR + SRV + TXT + A records. All socket ops run from the WINC
 * socket-callback context (like the discovery responder), so there is no
 * cross-task HIF re-entrancy hazard.
 *
 * STA-only: mDNS advertises a service reachable on a real LAN, so it runs only
 * when the device is an STA with a valid IP — never in AP / self-hosted mode.
 */
#ifndef MDNS_RESPONDER_H
#define MDNS_RESPONDER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "socket.h"

#ifdef __cplusplus
extern "C" {
#endif

/** The TCP service port advertised in the SRV record (SCPI TCP console). */
#define MDNS_SERVICE_PORT   9760u

/** On-device diagnostics snapshot (#58). Lets `SYST:COMM:LAN:MDNS?` report
 *  whether the responder is still receiving/answering queries — independent of
 *  host-side mDNS tooling, which is unreliable on multi-homed test hosts. */
typedef struct {
    bool     active;        /**< responder owns a live socket */
    bool     recvArmed;     /**< a recvfrom is currently armed (false = deaf) */
    uint32_t rxCount;       /**< inbound datagrams delivered to the responder */
    uint32_t matchCount;    /**< queries that matched our service/host */
    uint32_t respCount;     /**< responses sent */
    uint32_t armFailCount;  /**< recvfrom re-arm failures (the wedge signal) */
    uint32_t healCount;     /**< self-heal socket re-opens performed */
    int8_t   lastArmRc;     /**< rc of the most recent recvfrom re-arm */
    char     instance[64];  /**< advertised instance FQDN */
    char     host[24];      /**< advertised host FQDN */
} mdns_diag_t;

/** Runtime identity + TXT metadata for the advertised service. Strings are
 *  copied into the responder at Start; caller-owned buffers need not persist. */
typedef struct {
    uint32_t ipAddr;          /**< device IPv4, network byte order (A record) */
    uint8_t  mac[6];          /**< device MAC (low 2 bytes -> host/instance name) */
    const char *serialNumber; /**< TXT sn= */
    const char *partNumber;   /**< TXT pn= */
    const char *fwRev;        /**< TXT fw= */
    const char *hwRev;        /**< TXT hw= */
    const char *friendlyName; /**< TXT friendly= */
} mdns_identity_t;

/**
 * Start the responder: open the UDP socket, bind :5353, join 224.0.0.251,
 * and arm the first receive. Call on entering STA-connected (STA mode only).
 * Refresh semantics — if already running it is stopped and reopened with the
 * new identity, so a reconnect always yields a clean socket with current
 * metadata (and any socket leaked by a missed teardown is reclaimed).
 * @param id device identity + TXT metadata (copied internally).
 * @return true if the socket/bind sequence was kicked off.
 */
bool mdns_responder_Start(const mdns_identity_t *id);

/**
 * Refresh the advertised A-record IP. The DHCP lease arrives after STA
 * association, so wifi_manager calls this from its DHCP callback once the
 * station IP is known. Stores the value only (no socket op — safe from the
 * WINC callback context). Until a non-zero IP is set the responder stays
 * silent (won't answer with 0.0.0.0).
 * @param ipAddrNetworkOrder device IPv4, network byte order.
 */
void mdns_responder_UpdateIp(uint32_t ipAddrNetworkOrder);

/** Stop the responder: leave the group and shut the socket. Call on STA
 *  disconnect, AP-mode entry, or deinit. Safe to call when not running. */
void mdns_responder_Stop(void);

/** True while the responder owns a live socket. */
bool mdns_responder_IsActive(void);

/**
 * True if @p sock is the responder's socket. wifi_manager's SocketEventCallback
 * calls this first (like Iperf2_HandleSocketEvent) to route BIND/RECVFROM/SENDTO
 * events here before its own discovery/TCP handling.
 */
bool mdns_responder_OwnsSocket(SOCKET sock);

/**
 * Handle a WINC socket event for the responder's socket. Only call when
 * mdns_responder_OwnsSocket(sock) is true.
 * @param sock the socket, @param msgType SOCKET_MSG_*, @param pvMsg WINC payload.
 */
void mdns_responder_HandleSocketEvent(SOCKET sock, uint8_t msgType, void *pvMsg);

/**
 * Periodic self-heal (#58). Call from WifiTask's ProcessState loop (same
 * context that Start/Stop already run in — NOT the WINC callback context, so
 * no new HIF re-entrancy hazard). If the responder is active but its recvfrom
 * re-arm has failed (it would otherwise be permanently deaf while still
 * "active" — the observed long-run silence), this re-opens the socket using
 * the stored identity so it recovers. Rate-limited internally; a no-op when
 * healthy or inactive. Safe to call every iteration.
 */
void mdns_responder_ServiceHealth(void);

/** Copy the current diagnostics snapshot (#58). Never fails; zeros @p out
 *  when the responder is inactive. */
void mdns_responder_GetDiag(mdns_diag_t *out);

#ifdef __cplusplus
}
#endif

#endif /* MDNS_RESPONDER_H */
