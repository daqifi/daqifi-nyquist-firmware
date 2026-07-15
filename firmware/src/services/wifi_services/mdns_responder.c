/**
 * @file mdns_responder.c
 * @brief Minimal software mDNS / DNS-SD responder (#345). See header.
 *
 * Wire format per RFC 1035 (DNS), RFC 6762 (mDNS), RFC 6763 (DNS-SD).
 * Names are encoded UNCOMPRESSED — valid per RFC 6762 §18.14 and far simpler
 * than compression-pointer bookkeeping; the full response stays well under the
 * 1400 B WINC datagram cap. The responder answers a matching query with the
 * complete PTR + SRV + TXT + A set so a browser resolves in one round trip.
 */
#include "mdns_responder.h"

#include <string.h>
#include <stdio.h>
#include "socket.h"
#include "Util/Logger.h"

/* mDNS well-known group + port. 224.0.0.251 = 0xE00000FB. */
#define MDNS_MCAST_ADDR_HOST   0xE00000FBu   /* host byte order */
#define MDNS_PORT              5353u

/* DNS record types / classes. */
#define DNS_TYPE_A      1u
#define DNS_TYPE_PTR    12u
#define DNS_TYPE_TXT    16u
#define DNS_TYPE_SRV    33u
#define DNS_TYPE_ANY    255u
#define DNS_CLASS_IN        0x0001u
#define DNS_CLASS_FLUSH_IN  0x8001u   /* cache-flush bit set (unique records) */

/* TTLs (seconds): host/SRV short, service/TXT long (RFC 6762 §10, issue #345). */
#define MDNS_TTL_HOST   120u
#define MDNS_TTL_SRV    120u
#define MDNS_TTL_PTR    4500u
#define MDNS_TTL_TXT    4500u

#define MDNS_SERVICE_TYPE   "_daqifi._tcp.local"

/* RAM is nearly maxed on this build (see the linker stack-fit budget), so keep
 * the static footprint minimal: only the recvfrom target and the identity
 * strings are static. The response (~290 B) is built in a STACK local on the
 * WINC task (4 KB stack, ~1.3 KB peak — ample headroom), not in BSS. */
#define MDNS_RX_BUF_SIZE    128u    /* inbound query — a service question is ~40 B */
/* Response scratch (stack local). Worst case with the capped identity strings
 * below is header 12 + PTR 62 + SRV 67 + TXT ~144 + A 33 ≈ 318 B; 384 leaves
 * headroom so a max-length friendly/rev name can't overflow. */
#define MDNS_TX_BUF_SIZE    384u
#define MDNS_NAME_MAX       160u

typedef struct {
    SOCKET   sock;
    bool     active;
    mdns_identity_t id;
    /* owned copies of the identity strings + derived names (right-sized) */
    char sn[18];
    char pn[8];
    char fw[12];
    char hw[12];
    char friendly[32];
    char instanceFqdn[64];   /* "<instance>._daqifi._tcp.local" */
    char hostFqdn[24];       /* "daqifi-xxxx.local"             */
    uint8_t rxBuf[MDNS_RX_BUF_SIZE];
} mdns_state_t;

static mdns_state_t gMdns = { .sock = -1, .active = false };

/* ----- small wire-write helpers (network / big-endian) ------------------- */

static size_t put_u16(uint8_t *b, size_t off, uint16_t v) {
    b[off]     = (uint8_t)(v >> 8);
    b[off + 1] = (uint8_t)(v & 0xFF);
    return off + 2u;
}

static size_t put_u32(uint8_t *b, size_t off, uint32_t v) {
    b[off]     = (uint8_t)(v >> 24);
    b[off + 1] = (uint8_t)(v >> 16);
    b[off + 2] = (uint8_t)(v >> 8);
    b[off + 3] = (uint8_t)(v & 0xFF);
    return off + 4u;
}

/* Encode a dotted name ("a.b.local") as length-prefixed labels + 0 terminator.
 * Returns the new offset. Labels with a '.' inside are not supported (our
 * instance label is dot-free). */
static size_t put_name(uint8_t *b, size_t off, const char *name) {
    const char *seg = name;
    while (*seg != '\0') {
        const char *dot = strchr(seg, '.');
        size_t len = (dot != NULL) ? (size_t)(dot - seg) : strlen(seg);
        if (len == 0u || len > 63u) {
            break;                       /* malformed — stop cleanly */
        }
        b[off++] = (uint8_t)len;
        memcpy(&b[off], seg, len);
        off += len;
        if (dot == NULL) {
            break;
        }
        seg = dot + 1;
    }
    b[off++] = 0x00u;                    /* root label */
    return off;
}

/* Append one resource record header (name,type,class,ttl) and leave RDLENGTH
 * to be back-patched by the caller. Returns offset of the 2-byte RDLENGTH. */
static size_t put_rr_head(uint8_t *b, size_t *pOff, const char *name,
                          uint16_t type, uint16_t cls, uint32_t ttl) {
    size_t off = *pOff;
    off = put_name(b, off, name);
    off = put_u16(b, off, type);
    off = put_u16(b, off, cls);
    off = put_u32(b, off, ttl);
    size_t rdlenAt = off;
    off = put_u16(b, off, 0u);           /* placeholder RDLENGTH */
    *pOff = off;
    return rdlenAt;
}

/* Append one "key=value" TXT entry (length-prefixed). */
static size_t put_txt_kv(uint8_t *b, size_t off, const char *key, const char *val) {
    if (val == NULL) {
        val = "";
    }
    size_t klen = strlen(key);
    size_t vlen = strlen(val);
    size_t total = klen + 1u + vlen;      /* key + '=' + value */
    if (total > 255u) {
        total = 255u;                     /* TXT entry length is one byte */
    }
    b[off++] = (uint8_t)total;
    size_t w = 0;
    for (size_t i = 0; i < klen && w < total; i++) { b[off++] = (uint8_t)key[i]; w++; }
    if (w < total) { b[off++] = (uint8_t)'='; w++; }
    for (size_t i = 0; i < vlen && w < total; i++) { b[off++] = (uint8_t)val[i]; w++; }
    return off;
}

/* Build the full response (PTR + SRV + TXT + A) into `b`. Returns length. */
static size_t build_response(uint8_t *b) {
    size_t off = 0;

    off = put_u16(b, off, 0u);            /* ID = 0 for mDNS */
    off = put_u16(b, off, 0x8400u);       /* flags: QR=1, AA=1 */
    off = put_u16(b, off, 0u);            /* QDCOUNT */
    off = put_u16(b, off, 1u);            /* ANCOUNT: PTR */
    off = put_u16(b, off, 0u);            /* NSCOUNT */
    off = put_u16(b, off, 3u);            /* ARCOUNT: SRV, TXT, A */

    /* --- Answer: PTR  _daqifi._tcp.local -> <instance>._daqifi._tcp.local --- */
    size_t rdlenAt = put_rr_head(b, &off, MDNS_SERVICE_TYPE,
                                 DNS_TYPE_PTR, DNS_CLASS_IN, MDNS_TTL_PTR);
    size_t rdStart = off;
    off = put_name(b, off, gMdns.instanceFqdn);
    (void)put_u16(b, rdlenAt, (uint16_t)(off - rdStart));

    /* --- Additional: SRV  <instance> -> port + host --- */
    rdlenAt = put_rr_head(b, &off, gMdns.instanceFqdn,
                          DNS_TYPE_SRV, DNS_CLASS_FLUSH_IN, MDNS_TTL_SRV);
    rdStart = off;
    off = put_u16(b, off, 0u);            /* priority */
    off = put_u16(b, off, 0u);            /* weight   */
    off = put_u16(b, off, (uint16_t)MDNS_SERVICE_PORT);
    off = put_name(b, off, gMdns.hostFqdn);
    (void)put_u16(b, rdlenAt, (uint16_t)(off - rdStart));

    /* --- Additional: TXT  <instance> -> sn/pn/fw/hw/friendly --- */
    rdlenAt = put_rr_head(b, &off, gMdns.instanceFqdn,
                          DNS_TYPE_TXT, DNS_CLASS_FLUSH_IN, MDNS_TTL_TXT);
    rdStart = off;
    off = put_txt_kv(b, off, "sn", gMdns.sn);
    off = put_txt_kv(b, off, "pn", gMdns.pn);
    off = put_txt_kv(b, off, "fw", gMdns.fw);
    off = put_txt_kv(b, off, "hw", gMdns.hw);
    off = put_txt_kv(b, off, "friendly", gMdns.friendly);
    (void)put_u16(b, rdlenAt, (uint16_t)(off - rdStart));

    /* --- Additional: A  <host> -> device IPv4 --- */
    rdlenAt = put_rr_head(b, &off, gMdns.hostFqdn,
                          DNS_TYPE_A, DNS_CLASS_FLUSH_IN, MDNS_TTL_HOST);
    rdStart = off;
    /* ipAddr is already network byte order (from the WINC DHCP lease). */
    memcpy(&b[off], &gMdns.id.ipAddr, 4);
    off += 4u;
    (void)put_u16(b, rdlenAt, (uint16_t)(off - rdStart));

    return off;
}

/* Decode a QNAME at rx[*pOff] into a lowercase dotted string. Does NOT follow
 * compression pointers (questions are not compressed); bails to false if one
 * is seen or the name overruns. Advances *pOff past the QNAME on success. */
static bool decode_qname(const uint8_t *rx, size_t len, size_t *pOff,
                         char *out, size_t outSize) {
    size_t off = *pOff;
    size_t o = 0;
    while (off < len) {
        uint8_t l = rx[off++];
        if (l == 0u) {
            if (o > 0u && out[o - 1u] == '.') {
                o--;                      /* strip trailing dot */
            }
            out[o < outSize ? o : outSize - 1u] = '\0';
            *pOff = off;
            return true;
        }
        if ((l & 0xC0u) != 0u) {
            return false;                 /* compression pointer — unsupported */
        }
        if ((off + l) > len || (o + l + 1u) >= outSize) {
            return false;
        }
        for (uint8_t i = 0; i < l; i++) {
            char c = (char)rx[off++];
            if (c >= 'A' && c <= 'Z') {
                c = (char)(c - 'A' + 'a'); /* DNS names are case-insensitive */
            }
            out[o++] = c;
        }
        out[o++] = '.';
    }
    return false;
}

/* Case-insensitive ASCII string equality (strcasecmp isn't used elsewhere in
 * this firmware, so don't assume the XC32 newlib exposes it). */
static bool str_ci_equal(const char *a, const char *b) {
    while (*a != '\0' && *b != '\0') {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') { ca = (char)(ca - 'A' + 'a'); }
        if (cb >= 'A' && cb <= 'Z') { cb = (char)(cb - 'A' + 'a'); }
        if (ca != cb) {
            return false;
        }
        a++; b++;
    }
    return (*a == '\0') && (*b == '\0');
}

/* True if the query asks about our service type, instance, or host. */
static bool query_matches(const uint8_t *rx, size_t len) {
    if (len < 12u) {
        return false;
    }
    uint16_t qd = (uint16_t)((rx[4] << 8) | rx[5]);
    uint16_t flags = (uint16_t)((rx[2] << 8) | rx[3]);
    if (flags & 0x8000u) {
        return false;                     /* a response, not a query */
    }
    size_t off = 12u;
    char name[MDNS_NAME_MAX];
    for (uint16_t q = 0; q < qd; q++) {
        if (!decode_qname(rx, len, &off, name, sizeof(name))) {
            return false;
        }
        if ((off + 4u) > len) {
            return false;
        }
        off += 4u;                        /* skip QTYPE + QCLASS */
        if (str_ci_equal(name, MDNS_SERVICE_TYPE) ||
            str_ci_equal(name, gMdns.instanceFqdn) ||
            str_ci_equal(name, gMdns.hostFqdn)) {
            return true;
        }
    }
    return false;
}

/* ----- socket lifecycle -------------------------------------------------- */

static void mdns_arm_recv(void) {
    if (gMdns.sock >= 0) {
        recvfrom(gMdns.sock, gMdns.rxBuf, sizeof(gMdns.rxBuf), 0);
    }
}

static void mdns_send_response(void) {
    if (gMdns.id.ipAddr == 0u) {
        return;                           /* no DHCP lease yet — stay silent */
    }
    uint8_t resp[MDNS_TX_BUF_SIZE];       /* stack (WINC task) — not BSS */
    size_t n = build_response(resp);
    if (n == 0u || n > SOCKET_BUFFER_MAX_LENGTH) {
        return;
    }
    struct sockaddr_in dst;
    memset(&dst, 0, sizeof(dst));
    dst.sin_family = AF_INET;
    dst.sin_port = _htons(MDNS_PORT);
    dst.sin_addr.s_addr = _htonl(MDNS_MCAST_ADDR_HOST);
    sendto(gMdns.sock, resp, (uint16_t)n, 0,
           (struct sockaddr *)&dst, sizeof(dst));
}

static void copy_str(char *dst, size_t dstSize, const char *src) {
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }
    size_t n = strlen(src);
    if (n >= dstSize) {
        n = dstSize - 1u;
    }
    memcpy(dst, src, n);
    dst[n] = '\0';
}

bool mdns_responder_Start(const mdns_identity_t *id) {
    if (id == NULL) {
        return false;
    }
    if (gMdns.active) {
        mdns_responder_Stop();            /* refresh: reclaim + re-open clean */
    }

    gMdns.id = *id;
    copy_str(gMdns.sn, sizeof(gMdns.sn), id->serialNumber);
    copy_str(gMdns.pn, sizeof(gMdns.pn), id->partNumber);
    copy_str(gMdns.fw, sizeof(gMdns.fw), id->fwRev);
    copy_str(gMdns.hw, sizeof(gMdns.hw), id->hwRev);
    copy_str(gMdns.friendly, sizeof(gMdns.friendly), id->friendlyName);

    /* Derived names from the low 2 MAC bytes: host "daqifi-95a7.local",
     * instance "DAQiFi-95A7" (RFC 6763 human-readable instance). */
    uint8_t a = id->mac[4], b = id->mac[5];
    snprintf(gMdns.hostFqdn, sizeof(gMdns.hostFqdn), "daqifi-%02x%02x.local", a, b);
    snprintf(gMdns.instanceFqdn, sizeof(gMdns.instanceFqdn),
             "DAQiFi-%02X%02X.%s", a, b, MDNS_SERVICE_TYPE);

    gMdns.sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (gMdns.sock < 0) {
        LOG_E("[mDNS] socket() failed: %d", gMdns.sock);
        gMdns.sock = -1;
        return false;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = _htons(MDNS_PORT);
    addr.sin_addr.s_addr = 0;             /* INADDR_ANY */
    /* Join happens after SOCKET_MSG_BIND completes (setsockopt requires a
     * bound socket); recvfrom is armed then too. */
    bind(gMdns.sock, (struct sockaddr *)&addr, sizeof(addr));
    gMdns.active = true;
    LOG_I("[mDNS] starting: %s on %s", gMdns.instanceFqdn, gMdns.hostFqdn);
    return true;
}

void mdns_responder_Stop(void) {
    if (gMdns.sock >= 0) {
        uint32_t grp = _htonl(MDNS_MCAST_ADDR_HOST);
        setsockopt(gMdns.sock, SOL_SOCKET, IP_DROP_MEMBERSHIP, &grp, sizeof(grp));
        shutdown(gMdns.sock);
    }
    gMdns.sock = -1;
    gMdns.active = false;
}

void mdns_responder_UpdateIp(uint32_t ipAddrNetworkOrder) {
    gMdns.id.ipAddr = ipAddrNetworkOrder;
}

bool mdns_responder_IsActive(void) {
    return gMdns.active;
}

bool mdns_responder_OwnsSocket(SOCKET sock) {
    return gMdns.active && (sock >= 0) && (sock == gMdns.sock);
}

void mdns_responder_HandleSocketEvent(SOCKET sock, uint8_t msgType, void *pvMsg) {
    if (!mdns_responder_OwnsSocket(sock)) {
        return;
    }
    switch (msgType) {
    case SOCKET_MSG_BIND: {
        tstrSocketBindMsg *pBind = (tstrSocketBindMsg *)pvMsg;
        if (pBind != NULL && pBind->status == 0) {
            /* Join 224.0.0.251, then arm the first receive. */
            uint32_t grp = _htonl(MDNS_MCAST_ADDR_HOST);
            setsockopt(gMdns.sock, SOL_SOCKET, IP_ADD_MEMBERSHIP, &grp, sizeof(grp));
            mdns_arm_recv();
        } else {
            LOG_E("[mDNS] bind failed");
            mdns_responder_Stop();
        }
        break;
    }
    case SOCKET_MSG_RECVFROM: {
        tstrSocketRecvMsg *pRx = (tstrSocketRecvMsg *)pvMsg;
        if (pRx != NULL && pRx->pu8Buffer != NULL && pRx->s16BufferSize > 0) {
            /* Use the WINC event's own buffer pointer (== gMdns.rxBuf, the
             * buffer we armed recvfrom with) rather than the global — more
             * idiomatic and robust to any future buffer indirection. */
            if (query_matches(pRx->pu8Buffer, (size_t)pRx->s16BufferSize)) {
                mdns_send_response();
            }
        }
        mdns_arm_recv();                  /* always re-arm */
        break;
    }
    case SOCKET_MSG_SENDTO:
    default:
        break;
    }
}
