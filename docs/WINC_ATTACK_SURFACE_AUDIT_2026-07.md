# WINC1500 Network Attack-Surface Audit (2026-07)

**Ticket:** [#387 — WINC attack surface: unbound ports and stray-traffic resilience](https://github.com/daqifi/daqifi-nyquist-firmware/issues/387)
**Scope:** desk audit of the firmware's network-facing socket surface and its
resilience to unexpected / hostile traffic. **Source-only — no bench, no code
change.** Hardening is deliberately out of scope here (separate PRs); this
document produces the inventory and the prioritized recommendation list the
ticket asks for.

**Board:** NQ1, firmware branch as of `feat/589-sick-sd-tier123`.
**Variant note:** the WiFi/socket stack is variant-independent — NQ1/NQ2/NQ3
share `wifi_manager.c`, `wifi_tcp_server.c`, and the iperf2 service, and the
listen ports come from `CommonRuntimeDefaults.h` (`DEFAULT_TCP_PORT`), so every
finding below applies to all three variants.

## Evidence tags

Per the repo debugging-discipline convention, each load-bearing claim is tagged:

- **V** — Verified from primary source (firmware file:line, vendor SDK).
- **I** — Inference / hypothesis about behavior under hostile traffic. Not yet
  measured; each names the experiment that would confirm it.
- **X** — External authority (Microchip WINC SDK / datasheet).

No **E** (empirical) tags appear — this is a static audit; nothing was run on
hardware. The characterization run (ticket item 5) is what upgrades the **I**
findings to **E**.

## Threat model / what the WINC gives us

**X** The WINC1500 runs a closed TCP/IP stack on its own MCU. The
`m2m_socket` API exposes only `socket`/`bind`/`listen`/`accept`/`recv`/
`recvfrom`/`send`/`sendto`/`shutdown` — there is **no packet filter, no
"reject unbound port" toggle, and no per-peer firewall**. ICMP echo (ping) is
answered by the WINC stack and is never visible to our application. Traffic to
ports with no bound socket is rejected inside the WINC (TCP RST / ICMP
unreachable) without our involvement. Our attack surface is therefore (a) the
ports we deliberately bind, and (b) whatever stray-traffic handling bugs exist
inside the closed WINC firmware, which we can only defend against indirectly
(rate-limit our own accepts, watchdog the HIF queue, reset on wedge).

All of our listeners bind to `INADDR_ANY` (`addr.sin_addr.s_addr = 0`), so
they listen on **whichever interface the WINC currently has an IP on** — the
soft-AP in AP mode, or the joined LAN in STA mode. **V** (`wifi_tcp_server.c:366`,
`wifi_manager.c:851`, `iperf2.c:238`). In **open-AP** mode (the shipped default
soft-AP is open — see CLAUDE.md "AP-mode defaults") every listener is reachable
by *anyone able to associate*; in STA mode, by anyone on the joined subnet.

## Socket inventory

| # | Port | Proto | Bound iface | Lifetime | Input parsed? | Exposure |
|---|-----:|-------|-------------|----------|---------------|----------|
| 1 | 9760¹ | TCP | INADDR_ANY (AP/STA) | **Always, from WiFi-up** | Yes — microrl → libscpi | Full SCPI control of the device |
| 2 | 30303 | UDP | INADDR_ANY (AP/STA) | **Always, from WiFi-up** | **No** — replies to any datagram | Device-discovery announce (leaks hostname/IP/MAC) |
| 3 | 5001² | TCP+UDP | INADDR_ANY (AP/STA) | **Only while an iperf2 server run is active** | Header-length-checked | Throughput benchmark sink |
| 4 | ephemeral | TCP/UDP | — | Only during an iperf2 *client* run | — | Outbound only |
| 5 | — | ICMP | (WINC internal) | Always | (WINC) | Ping reply; not app-visible |

¹ `DEFAULT_TCP_PORT` = 9760, `tcpPort` in the runtime WiFi settings; user-
configurable. **V** `CommonRuntimeDefaults.h:119`, `NQ1RuntimeDefaults.c:10`.
² `IPERF2_DEFAULT_PORT` = 5001, overridable per SCPI command. **V** `iperf2.h:90`.

### Surface 1 — SCPI-over-TCP listener (:9760)

- **V** Opened at WiFi-up in the state machine (`wifi_manager.c:1322`, `:1384`
  → `wifi_tcp_server_OpenSocket(tcpPort)`), `bind`→`listen(…, 0)`→`accept`
  (`wifi_manager.c:560`, `:590`). It stays bound and listening for the entire
  time WiFi is up — this is the always-on control plane.
- **V One-client policy (#452):** `SOCKET_MSG_ACCEPT` refuses a second
  concurrent client by `shutdown()`-ing the *just-accepted* socket (RST to the
  new peer) and leaving the active session intact (`wifi_manager.c:633-645`).
  A running client cannot be displaced by a new connection.
- **V Input is bounded.** `recv()` reads into
  `readBuffer[WIFI_RBUFFER_SIZE+1]` with length `WIFI_RBUFFER_SIZE`
  (`=(SOCKET_BUFFER_MAX_LENGTH/2)-1` = 699 B) — no overflow
  (`wifi_manager.c:648`, `wifi_tcp_server.h:22,43`). Each received byte is fed
  to microrl (`wifi_tcp_server_ProcessReceivedBuff`, `wifi_tcp_server.c:578`).
- **V microrl line buffer is overflow-safe.** `_COMMAND_LINE_LEN` = 513
  (`config.h:16`); `buffer_insert_text` refuses to insert once
  `dataCursor + len >= _COMMAND_LINE_LEN` and returns false rather than writing
  past `cmdline[513]` (`microrl.c:530-547`). An **oversized SCPI line is
  silently truncated, not an overflow.** Completed lines dispatch to libscpi,
  which rejects unknown/malformed commands with a SCPI error (logged, not
  streamed).
- **I Connect-and-never-send is the sharpest gap.** There is **no idle or
  connect timeout on an accepted SCPI client** — `grep` for idle/timeout/
  watchdog in `wifi_tcp_server.c` / the accept path finds none. Because of the
  one-client policy, a single peer that completes the TCP handshake to :9760
  and then sends nothing **holds the only client slot indefinitely and locks
  out every legitimate client** until it disconnects or WiFi is reset. This is
  a trivial one-line DoS (`nc <ip> 9760 </dev/null &` and walk away). *This is
  precisely the listener-wedge failure class tracked in #560 / #475* — a
  half-open or idle listener slot that the firmware never reclaims on its own.
  Confirming experiment: connect to :9760, send nothing, then try a second
  client and confirm it is refused for the full idle period (ticket item 5).
  Note iperf2 *does* implement a connect deadline (`iperf2.c:369`,
  `:846` "TCP connect timeout … aborting") — the SCPI server does not, so the
  fix pattern already exists in-tree.
- **I Connection/SYN flood → WINC socket-table exhaustion.** The WINC exposes
  at most ~7 TCP sockets **X**. Every refused 2nd+ accept still momentarily
  consumes a WINC-side socket and bumps `acceptRefused`; a bind/socket failure
  bumps `socketOpenFails` ("the H2 smoking gun") **V** (`wifi_tcp_server.h:133-136`,
  `wifi_tcp_server.c:399-407`). A sustained connect flood or port scan on 9760
  churns accept→shutdown and can starve the WINC socket table, which is the
  documented **#560 "PATH-2 zombie churn"** wedge vector. The firmware *counts*
  this (`acceptRefused`, `socketOpenFails`) but takes **no defensive action**
  (no accept-rate limit, no auto-reset on churn). Confirming experiment:
  `hping3 --flood --syn -p 9760` while watching those counters + streaming
  health (ticket item 5).

### Surface 2 — UDP discovery announce (:30303)

- **V** Bound at WiFi-up (`wifi_manager.c:847-854`), `recvfrom` re-armed on
  every packet (`:707`).
- **V No request validation whatsoever.** On any inbound datagram
  (`SOCKET_MSG_RECVFROM`, `wifi_manager.c:685-711`) the handler ignores the
  payload entirely and immediately `sendto`s a fully-formed announce packet
  (`wifi_manager_FormUdpAnnouncePacketCB` → hostname/IP/MAC/board info) back to
  the source address:port. It does not check for a magic prefix, a minimum
  length, or a known request shape.
- **I Reflection / info-leak.** Because it answers *any* datagram — including
  spoofed-source and broadcast/multicast — the device (1) can be used as a
  small UDP reflector (spoofed src → device sends an announce to the victim;
  amplification factor is low, roughly one ≤1460-B announce per request, so
  this is a nuisance not a serious amplifier), and (2) advertises its
  identity to every device that broadcasts on the segment. On a hostile LAN
  this is a free asset-discovery beacon. The `udpBuffer` is a fixed 1460-B
  static and the WINC caps `recvfrom` at `SOCKET_BUFFER_MAX_LENGTH`, so there
  is **no buffer-overflow risk** here **V** (`wifi_manager.c:536-539`) — the
  concern is purely reflection + information disclosure, not memory safety.
  Confirming experiment: broadcast-UDP storm to 255.255.255.255:30303 with
  small/large/malformed payloads and confirm the device answers each (item 5).

### Surface 3 — iperf2 server (:5001, on demand)

- **V Time-limited by construction** — the listener exists only between
  `SYST:WIFI:IPERF:TCPServer` / `UDPServer` / `TXBLast` and `…:STOP`
  (`SCPIInterface.c:5230-5239`, `iperf2.c:229`, `:266`, `:303`). This already
  follows the ticket's item-1 recommendation ("open bind+listen only when
  needed").
- **V Refuses to start while streaming** (`Iperf2_RefuseIfStreaming`,
  `SCPIInterface.c:2624`), so it cannot contend with a live acquisition
  session.
- **V Short-packet-safe.** The UDP recv path validates
  `m->s16BufferSize >= sizeof(Iperf2_PktInfo)` **before** casting `gRxBuf` to
  the 12-byte header struct (`iperf2.c:566-587`), and `gRxBuf` is a fixed
  `IPERF2_UDP_BUF_SIZE` static — a runt datagram cannot trigger an
  out-of-bounds header read.
- **V Connect deadline** exists (`iperf2.c:369`, `:410`, `:846`) — a client
  that connects and never sends is timed out, unlike Surface 1.
- **I Residual risk is low** and gated behind an explicit SCPI action and the
  not-streaming precondition; the main note is simply that operators should not
  leave an iperf2 server running on an untrusted network. No hardening needed
  beyond documenting that.

### Surface 4 — iperf2 client / Surface 5 — ICMP

- **V** The iperf2 *client* paths (`iperf2.c:341`, `:389`) are outbound only
  (ephemeral local port) and live only during a run — negligible inbound
  surface.
- **X** ICMP echo is answered inside the WINC and is not reachable by our code;
  we can neither harden nor instrument it.

## Resilience summary

| Stimulus | Current behavior | Verdict |
|----------|------------------|---------|
| Oversized SCPI line (:9760) | microrl truncates at 513 B, no overflow (**V**) | Safe |
| Malformed SCPI command | libscpi returns SCPI error, logged (**V**) | Safe |
| Runt iperf2 UDP datagram | length-checked before header cast (**V**) | Safe |
| Random/large UDP to :30303 | fixed 1460-B static, WINC-capped (**V**) | Memory-safe; but reflects + leaks identity (**I**) |
| **Client connects to :9760, never sends** | **holds the sole client slot forever — no idle timeout** (**V/I**) | **Vulnerable — #560/#475 class** |
| Connection / SYN flood on :9760 | counted (`acceptRefused`/`socketOpenFails`) but no defense; can churn WINC socket table (**I**) | **Vulnerable — #560 churn wedge** |
| Partial TCP send under back-pressure | handled via in-flight ring, non-blocking (**V**, `wifi_tcp_server.c:120-152`, #362/#517) | Safe |
| Port scan (nmap) of unbound ports | rejected inside WINC (RST/unreachable) (**X**) | Safe unless it feeds the churn path above |

## Prioritized hardening recommendations

Each is a **separate follow-up PR** (this ticket is audit-only). Ordered by
risk-reduction per unit of effort.

1. **[High] Idle/connect timeout on the accepted SCPI TCP client (:9760).**
   Closes the connect-and-never-send DoS that currently locks out all clients
   under the one-client policy — the #560/#475 listener-wedge class. Close the
   client socket after N seconds with no RX (and/or no completed command). The
   pattern is already in-tree in iperf2 (`start_tick` + deadline; `iperf2.c:846`).
   Make N a `SYST:COMM:LAN:*` tunable with a sane default; 0 = disabled for
   back-compat. Lowest-risk, highest-value item.

2. **[High] Accept-rate limiting / churn auto-recovery on :9760.** When
   `acceptRefused` / `socketOpenFails` climb past a threshold within a window,
   back off (delay re-arming `accept`) and/or trigger a `SYST:COMM:LAN:HRESet`
   to reclaim the WINC socket table before the churn wedges it. Turns the
   existing #560 counters from passive telemetry into an active defense.

3. **[Medium] m2m_hif queue-depth watchdog → auto-`HRESet`** (ticket item 4).
   Periodically sample pending WINC HIF callback depth; if it backs up beyond
   the `IPERF2_MAX_PENDING_TX = 4` analog, reset the link before the wedge
   becomes invisible. Defends against stray-traffic-induced HIF starvation that
   items 1–2 don't cover.

4. **[Medium] Minimal request validation on the :30303 discovery responder.**
   Require a known magic prefix / minimum length before replying, and consider
   rate-limiting responses per source. Cheaply drops random broadcast noise and
   removes the open-reflector / unconditional-beacon behavior. Keep the DAQiFi
   discovery request shape backward-compatible.

5. **[Low, opt-in] Peer allow-list on `accept()`** (ticket item 2). Reject
   connections whose `tstrSocketAcceptMsg.strAddr` falls outside an
   operator-configured CIDR set, exposed via
   `SYST:COMM:LAN:WHITElist:ADD|CLEar|?`. Default empty = accept-all (current
   behavior). For fixed-deployment / industrial-LAN customers.

6. **[Low] Option to gate the always-on listeners.** Provide a SCPI toggle to
   disable the :30303 discovery responder (and optionally :9760) for deployments
   that don't need discovery — shrinking the 24/7 surface to zero when unused.

7. **[Process] Characterization run** (ticket item 5, the ticket's recommended
   first data-gathering step). Run `nmap -p1-65535`, `hping3 --flood --syn -p 9760`,
   an mDNS/SSDP storm, and malformed broadcast UDP against the device **while a
   normal SCPI stream is active**, recording streaming drops, WINC wedges, and
   recovery time per test class. This upgrades every **I** finding above to
   **E** and tells us whether items 1–4 need to ship urgently or can be
   scheduled. Build it in `daqifi-python-test-suite` per the repo test policy.

## Out of scope (unchanged from ticket)

- Cannot patch the WINC firmware (closed Microchip binary).
- Cannot install a low-level packet filter on the WINC.
- No firmware behavior is changed by this document.

## Source references

- SCPI TCP listener: `firmware/src/services/wifi_services/wifi_tcp_server.c`,
  `…/wifi_tcp_server.h`, accept/recv path in `…/wifi_manager.c:585-664`.
- UDP discovery: `…/wifi_manager.c:536-711`, `…/wifi_manager.h:21`.
- iperf2: `…/wifi_services/iperf2/iperf2.c`, `…/iperf2.h`,
  SCPI surface `…/SCPI/SCPIInterface.c:5230-5239`.
- microrl bounds: `firmware/src/libraries/microrl/src/config.h:16,24`,
  `…/microrl.c:528-548`.
- Listener-wedge history: issues #560, #475, #452 (referenced inline in the
  above files).
