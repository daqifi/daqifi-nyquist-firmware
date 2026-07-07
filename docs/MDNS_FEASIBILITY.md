# mDNS / DNS-SD (`_daqifi._tcp`) Feasibility — Issue #345

**Status: BLOCKED-by-driver.** The WINC1500 host driver bundled in this
firmware tree exposes **no mDNS / DNS-SD responder API**. Advertising a
`_daqifi._tcp.local` service therefore cannot be wired up by calling a
vendor helper — it would require writing a full mDNS responder on top of
the raw multicast socket API. Per the #345 scope ("check whether the
WINC1500 driver bundle includes an mDNS shim before rolling our own"),
this document records what exists, what is missing, and what a real
implementation would take, so the ticket can be re-scoped rather than
faked with a non-broadcasting stub.

Investigated on branch `feat/345-mdns`, 2026-07-06.

## What the driver in THIS tree provides

Driver location: `firmware/src/config/default/driver/winc/`

| Capability | Present? | Evidence |
|---|---|---|
| Native mDNS responder (`m2m_mdns_*`) | **No** | `grep -rilE 'mdns\|dns_sd\|m2m_mdns'` over `firmware/src` returns **zero** WINC hits (only unrelated hits: wolfSSL, header include guards, FatFs unicode). |
| DNS-SD / service-discovery helper (`DNS_SD`, `serviceDiscovery`) | **No** | `grep -rniE 'DNS_SD\|serviceDiscovery'` over the driver — no matches. |
| DNS **resolver** callback (client-side `gethostbyname`) | Yes (client only) | `dnsResolveCallback` exists in the socket API, but it *resolves* names, it does not *advertise* / respond to queries. |
| Raw UDP sockets (`socket`/`bind`/`recvfrom`/`sendto`) | Yes | `wdrv_winc_socket.c`; already used by the UDP-30303 discovery listener in `wifi_manager.c`. |
| Multicast group membership (IGMP join/leave) | Yes | `socket.h`: `IP_ADD_MEMBERSHIP (0x01)` / `IP_DROP_MEMBERSHIP` / `SOL_SOCKET`, applied via `setsockopt`. Not currently used anywhere in `firmware/src/services`. |

### WINC firmware / driver version

- `M2M_RELEASE_VERSION` = **19.7.7**
  (`.../driver/winc/include/drv/driver/m2m_types.h`:
  `M2M_RELEASE_VERSION_MAJOR_NO 19`, `MINOR_NO 7`, `PATCH_NO 7`).
- This is the standard Harmony 3 `wireless_wifi` WINC1500 socket-mode
  driver. **No revision of the WINC1500 host driver — including 19.7.7 —
  ships an on-chip or host-side mDNS responder.** mDNS on WINC1500 has
  always been an application-layer responsibility (unlike some ESP/newer
  parts that bundle one). Upgrading the WINC firmware does **not** unblock
  this ticket.

## Why a non-broadcasting stub was NOT added

The one thing worse than no mDNS is an enable flag that claims mDNS is on
while nothing ever multicasts a response — clients would trust
`.local` resolution that silently never works. Per #345 ("do NOT fake it
or add a stub that doesn't broadcast"), no SCPI flag, no `wifi_manager`
hook, and no empty responder module were added. The advertising path
must either genuinely respond to `224.0.0.251:5353` queries or not exist.

## What a real implementation would take

The raw multicast primitives are all present, so an mDNS responder is
*possible* here — it is simply a new firmware component of real size, not
a driver-shim wire-up. Scope for a follow-up ticket:

1. **New module** `firmware/src/services/wifi_services/mdns_responder.{c,h}`
   (~400–700 LOC): a minimal single-service mDNS/DNS-SD responder.
   - Open a UDP socket, `bind` to port **5353**.
   - `setsockopt(sock, SOL_SOCKET, IP_ADD_MEMBERSHIP, &{224.0.0.251}, ...)`
     to join the mDNS multicast group.
   - Parse inbound DNS queries; answer `PTR` (`_daqifi._tcp.local`),
     `SRV` (host `daqifi-<mac4>.local` : **9760**), `TXT`
     (`sn=`, `pn=`, `fw=`, `hw=`, `friendly=` — same fields the
     UDP-30303 protobuf response carries), and `A` (device IP) records.
   - Send responses to `224.0.0.251:5353` (and unicast when the QU bit
     is set). TTL 4500 s for the stable records per #345.
   - Simple conflict handling / probing on the chosen host name.

2. **Lifecycle wiring in `wifi_manager.c`**: start the responder only
   **in STA mode once the link has a real LAN IP** (DHCP bound); stop /
   leave the group on disconnect and in AP/self-hosted mode (#345
   explicitly excludes AP mode). Reuse the existing DHCP-up transition
   that today arms the UDP-30303 listener.

3. **Name/metadata source**: reuse `wifi_manager_FormUdpAnnouncePacketCB`
   inputs — `pWifiSettings` already carries the friendly name, and the
   MAC last-4 is available for the `daqifi-<mac4>.local` host name.

4. **Buffer / task budget**: the responder can run on the existing
   `app_WifiTask` (priority 2) alongside the UDP listener — inbound query
   rate is low. Reuse a ~1.5 KB static parse/response buffer (mirror the
   `udpBuffer[UDP_BUFFER_SIZE]` pattern); no heap, per project memory
   rules.

5. **SCPI surface** (per "keep it lean"): add an enable flag by extending
   the existing LAN command family rather than a new command tree —
   e.g. a `SYST:COMM:LAN:MDNS <0|1>` / `MDNS?` pair persisted in the
   existing LAN NVM settings block, default ON. No new top-level node.

6. **Keep UDP-30303 untouched** — #345 is explicitly additive.

## Acceptance (deferred to the implementation ticket)

- `avahi-browse -r _daqifi._tcp` (Linux) / `dns-sd -B _daqifi._tcp`
  (macOS) shows the device.
- `daqifi-<mac4>.local` resolves to the device IP from any mDNS host.
- `zeroconf.ServiceBrowser('_daqifi._tcp.local.', ...)` finds it from
  WSL2 (where raw UDP broadcast is dropped at the NAT bridge — the
  original #345 motivation).
- Existing `DAQiFi?\r\n` UDP-30303 path unchanged.

## Verification commands used

```bash
# No mDNS responder symbols anywhere in the WINC driver / firmware
grep -rilE 'mdns|dns_sd|dns-sd|m2m_mdns|serviceDiscovery' firmware/src   # -> no WINC hits
# Multicast join IS available
grep -nE 'IP_ADD_MEMBERSHIP|IP_DROP_MEMBERSHIP|SOL_SOCKET' \
  firmware/src/config/default/driver/winc/include/drv/socket/socket.h
# WINC release version
grep -nE 'M2M_RELEASE_VERSION_(MAJOR|MINOR|PATCH)_NO' \
  firmware/src/config/default/driver/winc/include/drv/driver/m2m_types.h
```
