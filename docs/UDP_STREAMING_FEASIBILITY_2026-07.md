# UDP Streaming Channel — Feasibility Assessment

**Issue:** [#13 — Add optional UDP streaming channel](https://github.com/daqifi/daqifi-nyquist-firmware/issues/13)
**Date:** 2026-07
**Status:** Feasibility / design only. Implementation is a follow-up feature.
**Scope:** Assess streaming Protobuf samples over UDP instead of / alongside the
existing TCP path (TCP:9760). No firmware behavior changes in this PR.

> Evidence tags used throughout: **V** = verified from primary source (firmware /
> vendor SDK / datasheet, cited file:line); **E** = empirical (a test we ran);
> **I** = inference / hypothesis (mechanism-plus-symptom reasoning, with the
> experiment that would confirm it). This doc is design-time, so most claims are
> **V** (code-grounded) or **I** (design projections). There is **no E** here —
> no UDP streaming code exists to measure yet, and no bench hardware was touched.

---

## 1. Executive summary

UDP streaming is **feasible and low-risk to prototype**. The WINC1500 socket API
the firmware already links exposes the full BSD-style datagram surface
(`socket(AF_INET, SOCK_DGRAM, …)`, `bind`, `sendto`, `recvfrom`) — the device's
network-discovery announce already runs over exactly this path (**V**,
`wifi_manager.c`). No new driver work, no new SPI traffic pattern, no MCC
regeneration.

The engineering value is **narrow but real**: UDP removes TCP's
head-of-line-blocking and retransmit stalls, which is the right trade for
*lossy-tolerant real-time* consumers (live plotting, closed-loop control) that
would rather drop a stale sample than wait for its retransmit. For *archival /
integrity-critical* capture, TCP (or SD) remains correct — a dropped UDP datagram
is permanently lost, unordered, and unacknowledged.

**Recommendation (see §9):** Build it as an **opt-in third WiFi transport**, not a
replacement. Gate it behind a new `StreamingInterface` enum value and a
configurable UDP port in the existing `SYST:COMM:LAN` namespace. Ship a per-tick
**sequence number** (largely free — the Protobuf message already carries an
incrementing `msg_time_stamp`) and a new `UdpDroppedDatagrams`/loss-visibility
story wired into the existing QUES register. Keep TCP as the default and the
integrity path.

---

## 2. What exists today (all V — code-grounded)

### 2.1 The TCP streaming path
- TCP streaming server binds `tcpPort` (default **9760**, `DEFAULT_TCP_PORT`) —
  `CommonRuntimeDefaults.h:119`, `NQ1RuntimeDefaults.c`, and
  `wifi_tcp_server_OpenSocket(pInstance->pWifiSettings->tcpPort)`
  (`wifi_manager.c:1322,1384`). **V**
- The send path is a **non-blocking `send()`** with explicit back-pressure:
  `SOCK_ERR_BUFFER_FULL` leaves data in the write buffer for retry, and an
  in-flight ring (`WIFI_TCP_MAX_IN_FLIGHT`, decremented by the
  `SOCKET_MSG_SEND` callback) lets the streaming task queue multiple sends
  without waiting for each ACK — `wifi_tcp_server.c:104-152, 571`. **V**
- Streaming output selection is by a single active-interface enum
  (`streaming.c:1125, 1401, 2321`): `hasWifi = (ActiveInterface ==
  StreamingInterface_WiFi)`. **V**

### 2.2 The interface model
`StreamingRuntimeConfig.h:17-23` (**V**):
```c
typedef enum eStreamingInterface {
    StreamingInterface_USB      = 0,
    StreamingInterface_WiFi     = 1,
    StreamingInterface_SD       = 2,
    StreamingInterface_UsbAndSd = 3,  // WiFi excluded — SPI bus conflict with SD
} StreamingInterface;
```
Set at runtime via `SYST:STR:INT <n>`. WiFi is always solo (its SPI4 bus is shared
with the SD card). A UDP streaming mode is naturally a **new value in this enum**
rather than a parallel command surface — consistent with the "keep the SCPI
command surface lean; extend existing enums before adding commands" rule.

### 2.3 UDP is already a first-class citizen in this firmware
Network discovery answers on UDP port **30303**
(`WIFI_MANAGER_UDP_LISTEN_PORT`, `wifi_manager.h:21`). The relevant primitives,
all already compiled and exercised (**V**, `wifi_manager.c`):
```c
*pSocket = socket(AF_INET, SOCK_DGRAM, 0);              // 847
bind(*pSocket, (struct sockaddr*)&addr, sizeof(addr));  // 853
recvfrom(socket, udpBuffer, UDP_BUFFER_SIZE, 0);        // 556, 707
sendto(socket, udpBuffer, announcePacktLen, 0,
       (struct sockaddr*)&addr, sizeof(struct sockaddr_in)); // 706
```
So the *transport mechanism* for UDP streaming already exists and is proven on
this hardware. The work is plumbing the streaming encoder output into a
`sendto()` fan-out and adding the control surface.

### 2.4 The Protobuf frame already carries an ordering key
`DaqifiOutMessage.msg_time_stamp` (field 1) is documented as *"Incrementing
timestamp for each streaming trigger"* (**V**, `DaqifiOutMessage.proto:8`). This
is per-**tick**, not per-**datagram**, but it gives a client a monotonic key to
detect gaps and reorder — a large part of the "sequence number" requirement is
already on the wire.

### 2.5 The single-send size ceiling
`SOCKET_BUFFER_MAX_LENGTH = 1400` (**V**, `winc/include/drv/socket/socket.h:78`);
`send()`/`sendto()` reject payloads above it (`socket.c:824,879`). This is the hard
datagram-size ceiling and directly sets the MTU design constraint in §5.

---

## 3. Why UDP — the benefit vs the existing TCP path

| Property | TCP (today) | UDP (proposed) |
|---|---|---|
| Delivery | Reliable, retransmitted | Best-effort, **no retransmit** |
| Ordering | In-order guaranteed | **Unordered** (client must reorder by timestamp/seq) |
| Head-of-line blocking | Yes — a lost segment stalls *all* later data until retransmit | **No** — a lost datagram is simply absent; newer data arrives on time |
| Latency under loss | Spikes (RTO-bound retransmit waits) | Flat — newest sample always delivered promptly |
| Connection | Stateful handshake / teardown; one client | Connectionless; trivial multi-listener / broadcast possible |
| Firmware back-pressure | Ring + in-flight window absorbs bursts | `sendto` either goes out or is dropped — **no queue growth**, simpler |
| Integrity for capture | Correct | **Wrong** — silent permanent sample loss |

**The core argument (I):** for a live-monitoring consumer, a *late* sample is
worthless — by the time TCP retransmits a dropped segment, the plot has moved on,
and meanwhile every newer sample was held behind it (head-of-line blocking). UDP
converts "delayed everything" into "dropped the one that was lost," which is the
better failure mode for real-time display and control. This is a well-established
transport trade-off, not a novel claim; but the *magnitude* of the win on this
specific WINC1500 path is **unmeasured (I)** — see §8.

**What UDP does NOT fix (I):** the WINC1500 host-side throughput ceiling
(~200–340 KB/s on the bench AP) is dominated by SPI send pipelining and host
CPU, **not** by TCP's reliability overhead (per the WiFi-characterization notes in
CLAUDE.md: SPI4 bus measured 92.9% idle during a saturated stream). So UDP is
**not** expected to raise peak throughput much — its win is **latency and
tail-behavior under loss**, not bandwidth. The ticket frames UDP as "maximize
streaming throughput"; that framing should be tempered — the realistic benefit is
latency/jitter, with throughput roughly at parity or modestly better (fewer
ACK-driven stalls). Confirming that requires the §8 A/B.

---

## 4. Proposed control surface (SCPI, lean-extension style)

Two additions, both extending existing namespaces rather than inventing new ones:

1. **Opt-in via the existing interface selector.** Add
   `StreamingInterface_WiFiUdp = 4` to the enum and accept it in `SYST:STR:INT`.
   `SYST:STR:INT 4` = "stream Protobuf over UDP." No new streaming-control verb.
   - Keeps `StreamingInterface_WiFi` (TCP) as the default and unchanged.
   - The streaming dispatch in `streaming.c` gains one `case`/branch that calls a
     new `wifi_udp_server_WriteDatagram()` instead of `wifi_tcp_server_WriteBuffer()`.

2. **Configurable UDP stream port**, mirroring the existing TCP `tcpPort` field.
   Extend the `SYST:COMM:LAN` namespace (which already owns `NETType`, `ADDRess?`,
   etc. — `SCPIInterface.c:5098-5105`):
   ```
   SYSTem:COMMunicate:LAN:UDPStream:PORT <port>    # set (default e.g. 30304)
   SYSTem:COMMunicate:LAN:UDPStream:PORT?          # query
   ```
   Persist alongside `tcpPort` in the WiFi settings struct + NVM. Default should
   **not** collide with 9760 (TCP stream) or 30303 (discovery); **30304** is a
   natural pick.

   Client-target model: because UDP is connectionless, the firmware needs a
   destination. Two viable designs (**I**, pick during implementation):
   - **(a) Client-registered:** the client sends a small "subscribe" datagram to a
     known device UDP port; the firmware captures the source IP:port (exactly as
     the discovery responder already does — `wifi_manager.c:701-706`) and streams
     there. Cleanest; no extra config; auto-handles NAT/DHCP.
   - **(b) SCPI-configured destination:** `...:UDPStream:DEST "<ip>",<port>`.
     Explicit but brittle (client IP churn). (a) is preferred.

*Wiki note:* any command added here must be mirrored into the wiki
`01-SCPI-Interface.md` per the SCPI-wiki-maintenance rule.

---

## 5. Datagram design (MTU-sized, sequence-numbered)

- **One Protobuf `DaqifiOutMessage` per datagram, ≤ 1400 B.** The encoder already
  produces one message per streaming tick; the streaming buffer/pool sizing keeps
  per-tick messages well under 1400 B for realistic channel counts. The
  implementation must **reject/​split** any encoded message that would exceed
  `SOCKET_BUFFER_MAX_LENGTH` (1400) rather than truncate — a truncated PB message
  is undecodable. For high channel counts where a single tick could approach the
  limit, either (i) cap the per-datagram tick count, or (ii) fall back to TCP with
  a logged advisory. **V** on the 1400 limit; **I** on the mitigation choice.

- **Sequence number for loss detection.** `msg_time_stamp` (per tick) already lets
  a client detect gaps *between ticks*. If we batch multiple ticks per datagram
  (to amortize UDP/IP header overhead), add an explicit **per-datagram** monotonic
  `udp_seq` so the client can distinguish "datagram lost" from "device paused."
  Cheapest implementation: a new optional Protobuf field (e.g.
  `uint32 udp_sequence = <next free tag>`) set only on the UDP path — costs one
  varint on the wire and a `uint32` counter in the streaming context (**I**).
  Reusing the proto (rather than a raw header) keeps every existing decoder
  working and avoids a second wire format.

- **No datagram batching in v1 (recommended).** Start with **one tick = one
  datagram**. It is the simplest correct design, makes `msg_time_stamp` a
  sufficient sequence key, and sidesteps the >1400 B split problem entirely for
  low/moderate channel counts. Batching is a later optimization if header overhead
  proves material (**I**).

---

## 6. Loss accounting story (QUES bits + stats)

This is the part that must be right for UDP to be *honest* rather than a silent
data-shredder. The firmware already has the scaffolding.

- **Existing QUES register** (`SCPIInterface.c:74` and CLAUDE.md): bit 4 = Data
  Loss (windowed), bit 9 = WiFi Overflow, bit 12 = Transport Down. **V**
- **Existing stats** (`SYST:STR:STATS?`): `WifiDroppedBytes`, `SampleLossPercent`,
  `ByteLossPercent`, windowed loss. **V**

**What UDP changes about accounting (I):** with TCP, "the firmware handed it to
the socket" ≈ "it will arrive." With UDP that implication is gone — the firmware
can only account for what it *failed to hand to `sendto`* (local drop: ring full,
`sendto` returned error). **On-wire loss (a datagram that left the device but never
arrived) is invisible to the firmware by construction** and can only be measured
by the client via the sequence number. The doc-worthy consequence:

- **Firmware-side (device can see):** count `sendto` failures /
  `SOCK_ERR_BUFFER_FULL` as `UdpDroppedDatagrams` and fold into the existing
  WiFi-overflow QUES bit (bit 9) and loss percentages. This reuses the TCP
  back-pressure accounting pattern almost verbatim.
- **Client-side (only place true delivery loss is observable):** gap detection on
  `udp_sequence` / `msg_time_stamp`. The client SDKs must own the "N datagrams
  missing between seq X and Y" metric. **The device cannot report it.**

This split ("error on config problems, inform on stale/lossy data; firmware ships
mechanism, client owns the loss-visibility UX") matches the project's
firmware-vs-client division-of-responsibility rule.

---

## 7. Client impact

- **daqifi-python-core / daqifi-core (.NET) / java / node / labview / arduino:**
  all currently open a **TCP** socket to 9760 and parse length-delimited PB. A UDP
  consumer is a **new code path**, not a drop-in: open a `SOCK_DGRAM` socket, bind
  a local port (or subscribe per §4-a), and decode **one PB message per datagram**
  (no length-prefix framing needed — the datagram boundary *is* the frame, which is
  actually simpler than TCP's byte-stream reframing). **I**
- **Reordering/loss handling becomes the client's job.** Clients that opt into UDP
  must tolerate gaps and out-of-order arrival, keyed on `msg_time_stamp` /
  `udp_sequence`. Clients that don't want that complexity simply keep using TCP —
  which is why UDP must be **opt-in, never the default**.
- **Discovery unchanged.** The existing 30303 announce still locates the device;
  only the stream transport differs.
- **No breaking change.** TCP:9760 stays exactly as-is; UDP is additive.

---

## 8. Open questions / experiments to close the I-gaps

Before committing to the feature, one bench A/B (in `daqifi-python-test-suite`)
would convert the key inferences to evidence:

1. **Latency/jitter win (I→E):** stream identical config over TCP vs UDP under
   induced AP loss; measure end-to-end sample latency distribution and tail. This
   is the feature's actual justification — measure it.
2. **Throughput parity/regression (I→E):** confirm UDP does **not** regress wire
   rate vs TCP at the enforced caps (expectation: parity, since the bottleneck is
   SPI/host pipelining, not TCP reliability — CLAUDE.md WiFi notes).
3. **`sendto` back-pressure behavior (I→E):** does the WINC drop-vs-`BUFFER_FULL`
   under a saturated UDP send match the TCP path's ring accounting? Determines how
   `UdpDroppedDatagrams` is wired.
4. **>1400 B tick handling (I→E):** verify the highest-channel-count PB message
   size vs the 1400 ceiling to decide whether v1 needs the split/fallback of §5.

## 8a. Firmware cost / RAM (design constraint)

RAM headroom is tight (~500 B static above the min-stack floor; a +876 B BSS
change broke the link historically). A UDP path should **reuse** the existing WiFi
streaming circular buffer / SPI staging rather than allocate a second ring —
because WiFi-UDP and WiFi-TCP are mutually exclusive `ActiveInterface` values, the
same pool partition can serve either. New static cost is then only a small context
(socket handle, dest sockaddr, `uint32` sequence counter, a few stat counters) —
tens of bytes, not a new KB-scale buffer. Any implementation that needs a second
large buffer should be rejected in favor of reusing the WiFi partition. **I** —
confirm the exact byte delta at implementation time against the link map.

---

## 9. Recommendation

**Proceed, as an opt-in latency-oriented transport — not a throughput play and not
a TCP replacement.**

1. Add `StreamingInterface_WiFiUdp = 4`; select via `SYST:STR:INT 4`. Default and
   integrity path stay TCP.
2. Add a configurable UDP stream port under `SYST:COMM:LAN:UDPStream:PORT`
   (default 30304), persisted with `tcpPort`. Prefer **client-subscribe**
   destination capture (§4-a) over a statically configured destination.
3. v1 = **one tick per datagram**, ≤1400 B, carrying the existing `msg_time_stamp`
   plus a new per-datagram `udp_sequence` PB field for gap detection.
4. Firmware accounts only for **local** drops (`sendto`/BUFFER_FULL →
   `UdpDroppedDatagrams`, folded into QUES bit 9 + loss %). Document loudly that
   **on-wire loss is client-observable only** via the sequence number; ship the
   gap-detection metric in the client SDKs, not the firmware.
5. Gate the go/no-go on the §8 latency A/B: the feature earns its keep only if the
   TCP-vs-UDP tail-latency-under-loss difference is materially better. Reuse the
   existing WiFi buffer partition — no new large static allocation (§8a).

Framed honestly: this is a **modest, well-contained** feature with a clear correct
use case (real-time lossy-tolerant consumers), built almost entirely on transport
primitives the firmware already ships. The main risks are (a) overselling it as a
throughput feature when it is a latency feature, and (b) shipping silent loss
without the client-side sequence-gap accounting. Both are addressed above.
