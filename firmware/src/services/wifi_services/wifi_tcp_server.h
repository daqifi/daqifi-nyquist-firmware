#ifndef _WIFI_TCP_SERVER_H    /* Guard against multiple inclusion */
#define _WIFI_TCP_SERVER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "configuration.h"
#include "definitions.h"
#include "libraries/microrl/src/microrl.h"
#include "libraries/scpi/libscpi/inc/scpi/scpi.h"
#include "Util/CircularBuffer.h"
#include "wdrv_winc_client_api.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WIFI_MAX_CLIENT 1
#define WIFI_RBUFFER_SIZE ((SOCKET_BUFFER_MAX_LENGTH/2)-1)
#define WIFI_WBUFFER_SIZE SOCKET_BUFFER_MAX_LENGTH  // Use full WINC1500 buffer capacity (1400 bytes)
#define WIFI_CIRCULAR_BUFF_SIZE SOCKET_BUFFER_MAX_LENGTH*10

// Max simultaneous m2m_send packets queued at WINC.  hif_send is synchronous
// (writes to WINC SPI before returning), so our local writeBuffer is free
// right after send() returns — the only constraint is WINC's internal HIF
// queue depth.  Microchip's reference iperf uses 2 for UDP TX queue.
// Empirically N=3 lifts 1-channel WiFi PB ceiling from ~14 kHz → ~17 kHz on
// Tesla AP (no further gain at N=4); the previous N=2 was the WiFi
// pipelining bottleneck.  Multi-channel ceilings (8ch+) are unchanged
// because they're sample-queue-limited upstream of WiFi.
#define WIFI_TCP_MAX_IN_FLIGHT 4

/**
 * Data for a particular TCP client
 */
typedef struct s_tcpClientContext
{
    SOCKET clientSocket;
    /** Client read buffer */
    uint8_t readBuffer[WIFI_RBUFFER_SIZE+1];

    /** The current length of the read buffer */
    size_t readBufferLength;

    /** Client write buffer */
    uint8_t writeBuffer[WIFI_WBUFFER_SIZE+1];

    /** The current length of the write buffer */
    size_t writeBufferLength;

    CircularBuf_t wCirbuf;
    SemaphoreHandle_t wMutex;
    
    /** The Microrl console */
    microrl_t console;

    /** The associated SCPI context */
    scpi_t scpiContext;
    
    /** Count of m2m_send calls queued at WINC, decremented by SOCKET_MSG_SEND
     *  callback.  Caps at WIFI_TCP_MAX_IN_FLIGHT.  Replaces the prior
     *  `bool tcpSendPending` (which capped at 1).  Updated under
     *  taskENTER_CRITICAL — concurrent writers are streaming_Task (priority 6)
     *  and WDRV_WINC_Tasks (DRV_WIFI_WINC_RTOS_TASK_PRIORITY, currently 1 per
     *  PR #492 — formerly 2).  Reference the macro rather than hard-coding to
     *  avoid stale comment drift if priority changes again. */
    volatile uint8_t tcpInFlight;

    /** #437: pending CircularBuf_Reset deferred from a context that
     *  couldn't acquire wMutex (notably WINC driver task running
     *  CloseClientSocket from SocketEventCallback).  Drained by the
     *  next wMutex holder before its operation, ensuring the reset
     *  happens under lock without ever blocking the WINC driver task. */
    volatile bool pendingBufferReset;

    /** Bytes handed to radio send() successfully (64-bit for long sessions) */
    uint64_t wifiTcpBytesSent;
    /** Bytes confirmed by radio send callback (64-bit for long sessions) */
    uint64_t wifiTcpBytesConfirmed;
    /** Radio send errors (negative sentBytes in callback — real failures) */
    uint32_t wifiTcpSendErrors;
    /** Partial sends (callback confirmed fewer bytes than requested).  #500: an
     *  elevated band around 23-26 KB/s of WIRE BYTE RATE is EXPECTED — it keys
     *  on byte rate rather than sample rate (1xT1 @ 2250 Hz and 5xT1 @ 1250 Hz
     *  peak together), is near-zero below ~22 KB/s and low again by ~33 KB/s,
     *  and is not circular-buffer-size dependent.  **#935/#956: this counter is
     *  NOT a loss indicator — it is inflated by a send-completion ring-pairing
     *  defect** (see wifiPartialBytesMissing below and #956 for the mechanism),
     *  which is also why the band is byte-rate-keyed: the pairing only shows up
     *  when consecutive send lengths differ, which peaks mid-band and vanishes
     *  once sends saturate to a constant 1400 B above it.  Treat a rise as a
     *  diagnostic-counter artifact, not stream damage, until #956 lands.  See
     *  docs/STREAMING_AND_ADC.md, "WiFi characterization — lessons that survive".
     *
     *  #956 STATUS: the two producer-side pairing defects it named are now
     *  closed in source (the unlocked cap check moved into TcpServerFlush; the
     *  two SCPI sites no longer reset the live ring AT ALL -- see
     *  SCPI_ClearStreamStats for why resetting both halves is worse than
     *  resetting neither, and why only socket teardown may reset a live ring).  The caution above is deliberately LEFT
     *  STANDING rather than replaced: the band has not been re-measured on
     *  fixed firmware, and two things #956 did NOT fix still detach this
     *  counter from real loss — the Sent/Confirmed reset asymmetry documented
     *  on wifiPartialBytesMissing below, and the never-retried short send.
     *  Re-measure before promoting a rise here to evidence of loss. */
    uint32_t wifiTcpPartialSends;
    /** #367 diagnostics: cumulative byte shortfall (sendSize - sentBytes) across
     *  all partial sends, summed only where the difference is positive
     *  (wifi_manager.c's `<` test at the increment site silently discards the
     *  negative direction).  **#935/#956: this is NOT a permanent-loss counter.**
     *  It assumes `inflightSizes[inflightTail]` (popped in SOCKET_MSG_SEND)
     *  always belongs to the completion currently firing; #956 documents two
     *  independently-verified ways that pairing breaks (an unlocked
     *  WIFI_TCP_MAX_IN_FLIGHT check racing the ring push in TcpServerFlush, and
     *  SYST:STR:START / SYST:STR:STATS:CLEar zeroing the ring without
     *  tcpInFlight — the missed twin of #519's ResetInflightRing fix). When
     *  mis-paired, this field sums the positive half of a length difference
     *  between two UNRELATED sends and the matching negative half is discarded,
     *  so it grows with no byte actually lost.  The counter that DOES bound
     *  real un-confirmed payload is wifiTcpBytesSent - wifiTcpBytesConfirmed.
     *  That difference ALREADY INCLUDES payload still in flight -- BytesSent is
     *  incremented when the send is issued and BytesConfirmed only when the
     *  completion fires -- so do not add the in-flight amount to it again.
     *  Outstanding payload is NOMINALLY bounded by WIFI_TCP_MAX_IN_FLIGHT *
     *  WIFI_WBUFFER_SIZE = 5600 B, but that is design intent, NOT a guaranteed
     *  ceiling: TransmitBufferedData() tests tcpInFlight BEFORE taking the
     *  mutex while TcpServerFlush() increments it inside a later critical
     *  section, so two producers can pass the same check at 3 and both
     *  increment -- the same unlocked cap that mis-pairs the ring (#956).  So a
     *  difference under that figure is CONSISTENT WITH zero loss, not proof of
     *  it; the reading the race cannot inflate is one taken after outstanding
     *  completions have drained.
     *
     *  PRECONDITION, and it is stronger than "no reset in the window".  THE
     *  ABSOLUTE TOTALS ARE VALID ONLY SINCE A RESET TAKEN WITH NOTHING IN
     *  FLIGHT (tcpInFlight == 0).
     *
     *  SYST:STR:START and SYST:STR:STATS:CLEar zero BytesSent and
     *  BytesConfirmed WITHOUT draining outstanding sends (SCPIInterface.c) --
     *  the same reset asymmetry #956 names for the ring, applied to these two
     *  counters.  A completion landing after such a reset adds to Confirmed
     *  while its Sent contribution was erased, and NOTHING EVER PUTS IT BACK:
     *  BytesSent is written in exactly three places, the += at flush and the
     *  two resets.  The offset is PERMANENT for the rest of the session, so a
     *  later drain does not repair it and a later reset-free window inherits
     *  it.
     *
     *  Both directions of that offset are bad, and the SILENT one is the
     *  likelier hazard.  Confirmed > Sent makes the difference negative, and
     *  since both are uint64_t an unsigned subtraction WRAPS to ~1.8e19,
     *  which reads as catastrophic loss.  But the offset can equally CANCEL a
     *  real loss: 100 B sent, CLEar before its completion, drain (Sent=0,
     *  Confirmed=100), then a clean window of 1400 issued and 1300 confirmed
     *  leaves Sent == Confirmed == 1400 with 100 bytes genuinely gone.  That
     *  reads as zero loss and satisfies "no reset in this window".
     *
     *  BEFORE #956 it was invisible in the partial counters too, and that half
     *  NO LONGER HOLDS.  The old claim, stated so it is withdrawn rather than
     *  quietly swapped: CLEar zeroed the ring, so the popped sendSize was 0,
     *  the `sendSize > 0` guard below suppressed the partial-send flag, and
     *  only Confirmed moved.
     *
     *  #956 stopped CLEar touching the ring at all, so a completion landing
     *  after a mid-flight CLEar pops its ORIGINAL sendSize and a genuine short
     *  send increments wifiTcpPartialSends and this field normally.  Concretely:
     *  issue 1400 B, CLEar while it is outstanding, then a 1300 B completion
     *  now yields PartialSends 1 and PartialBytesMissing 100, where it
     *  previously yielded 0 and 0.
     *
     *  The old sentence was also wrong in a second way once wifiTcpOverBytesExtra
     *  existed: its arm below is deliberately UNGUARDED on sendSize, so even a
     *  sendSize == 0 pop moved that counter.  "Only Confirmed moves" failed
     *  either way.
     *
     *  The Sent/Confirmed offset described above is a DIFFERENT pair of counters
     *  with its own reset sites, is untouched by #956, and remains exactly as
     *  silent as described.  Do not read the partial counters as blind after a
     *  clear on post-#956 firmware.
     *
     *  WHAT IS SAFE: a DELTA between two drained snapshots inside one
     *  uncontaminated epoch.  To re-establish one, reset with the ring
     *  drained.
     *
     *  SEPARATELY, and NOT fixed by #956: a genuine short send is never
     *  retried.  TcpServerFlush zeroes writeBufferLength immediately after a
     *  successful send(), and no resend path exists -- so bytes the WINC did
     *  not accept are gone whatever the ring pairing does.  That is a second,
     *  independent risk source; do not let the ring-pairing story absorb it.
     *
     *  #935 measured 187 B and 190 B in two
     *  independent bench runs (~0.016-0.017% of bytes sent), far inside that
     *  figure and far below this field's reading in the same run.  Until #956
     *  lands, do not cite a rise here as evidence of lost stream bytes.
     *
     *  #956 STATUS: its two producer-side defects are fixed (see
     *  wifiTcpPartialSends above), and the new wifiTcpOverBytesExtra below now
     *  counts the negative half this field discards, so
     *  BytesSent - BytesConfirmed == PartialBytesMissing - OverBytesExtra is
     *  checkable rather than merely hoped for.  This status paragraph supersedes
     *  EXACTLY ONE claim above -- the partial-counter blindness after a CLEar,
     *  corrected in place two paragraphs up -- and nothing else.  An earlier
     *  revision of this line reaffirmed everything above unchanged, which was
     *  false once the ring stopped being reset.  Still standing and NOT
     *  superseded: the epoch precondition, the permanent Sent/Confirmed offset a
     *  reset taken with sends outstanding leaves behind, the two directions that
     *  offset can take, and the independent never-retried short send (now filed
     *  as #1041).  Nothing here has been re-measured on fixed firmware. */
    uint32_t wifiPartialBytesMissing;
    /** #956: the OPPOSITE direction of wifiPartialBytesMissing — cumulative
     *  (sentBytes - sendSize) where the completion confirms MORE bytes than the
     *  popped ring slot claims were sent.  Arithmetically impossible when the
     *  pairing is correct, so any non-zero reading is direct evidence that a
     *  completion was matched to the wrong slot (or to no slot at all, which is
     *  the sendSize == 0 case left deliberately UNGUARDED below).
     *
     *  Counting both directions is what restores the accounting identity the
     *  one-sided `<` test broke: for a bijection between pushes and pops,
     *  sum(sendSize) over pops equals sum(sendSize) over pushes REGARDLESS of the
     *  order they are paired in, so
     *      wifiTcpBytesSent - wifiTcpBytesConfirmed
     *          == wifiPartialBytesMissing - wifiTcpOverBytesExtra
     *  holds even under a permuted ring — it is only the discarded negative half
     *  that made a mis-pair look like loss.  Reset alongside
     *  wifiPartialBytesMissing (same epoch, or the identity is meaningless). */
    uint32_t wifiTcpOverBytesExtra;
    /** #371 diagnostics: count of wifi_tcp_server_WriteBuffer calls that returned 0
     *  because the circular buffer didn't have enough free space.  Streaming task
     *  charges the same packet to wifiDroppedBytes; if these don't match the
     *  per-call mismatch is a bug in the silent-loss path. */
    uint32_t wifiWriteBufferRejectedCalls;
    uint32_t wifiWriteBufferRejectedBytes;
    /** FIFO ring of in-flight send sizes — one slot per concurrent send.
     *  Producer (TcpServerFlush) writes at inflightHead, consumer (SOCKET_MSG_SEND
     *  callback) reads at inflightTail.  Both indices wrap mod WIFI_TCP_MAX_IN_FLIGHT.
     *  Replaces the prior scalar `lastSendSize` which raced under N>1 concurrent sends. */
    volatile uint16_t inflightSizes[WIFI_TCP_MAX_IN_FLIGHT];
    volatile uint8_t inflightHead;
    volatile uint8_t inflightTail;

    /** #956: flush attempts refused by TcpServerFlush's own authoritative
     *  WIFI_TCP_MAX_IN_FLIGHT check, taken under taskENTER_CRITICAL.
     *
     *  READ IT AS A COUNT OF PREVENTED RING OVERRUNS, NOT OF RESIDUAL ONES.
     *  Each increment is one attempt that the OLD code would have let through —
     *  the callers' cap checks were unlocked reads taken before wMutex, so a
     *  producer could pass at 3, queue on wMutex behind another producer that
     *  pushed to 4, and then push a 5th onto a 4-slot ring.  That attempt is now
     *  refused instead: the bytes stay in wCirbuf and the next drain retries
     *  them, exactly as SOCK_ERR_BUFFER_FULL already behaves.
     *
     *  So NON-ZERO UNDER WIFI STREAMING LOAD IS EXPECTED and is not a fault —
     *  it measures producer contention at the cap.  It should read 0 on an idle
     *  or control-only channel, where the ring never fills.  What WOULD be a
     *  finding is this counter rising while the ring is provably not full, or a
     *  ratio to WifiTcpBytesSent large enough to mean sends are being deferred
     *  often enough to cost throughput. */
    uint32_t wifiTcpInflightOverflow;

    /** #599: monotonically-increasing accepted-connection counter.  Bumped
     *  once per SOCKET_MSG_ACCEPT that installs a client (single writer:
     *  SocketEventCallback on the WINC driver task — same pattern as
     *  acceptRefused/acceptFails, so a plain ++ is safe; the 32-bit read is
     *  atomic).  Lets an in-flight async SD GET/LIST reply verify it still
     *  targets the connection that issued it, so a later client inheriting
     *  the single slot can never receive it. */
    volatile uint32_t connGeneration;

    /** #663: tick of the last RX or TX activity on this connection. Stamped at
     *  ACCEPT, on every SOCKET_MSG_RECV, and on every successful send(). The
     *  console idle-timeout watchdog (wifi_manager) closes a client that has
     *  had no RX *or* TX for the configured deadline — the connect-and-never-
     *  send DoS. Because a streaming client is continuously TX-active it is
     *  never idle by this definition, so streaming is never torn down. */
    volatile TickType_t lastActivityTick;

    /** #663 health counter: connections closed by the idle-timeout watchdog
     *  (single writer: app_WifiTask ProcessState). Sibling of acceptRefused/
     *  acceptFails for #560-style listener observability. */
    uint32_t idleClosed;
} wifi_tcp_server_clientContext_t;

/**
 * Tracks TCP Server Data
 */
typedef struct s_tcpServerContext
{
    //TcpServerState state;

    SOCKET serverSocket;

    wifi_tcp_server_clientContext_t client;

    /* #560/#475 listener-health observability (Opt 0).  uint32_t counters
     * surfaced in SYST:STReam:STATS? (read there under taskENTER_CRITICAL).
     * They PERSIST across streaming sessions (deliberately NOT reset at stream
     * start) so the slow PATH-1 listen-slot leak stays visible cross-session;
     * zeroed at boot (wifi_manager_BootInit memset) and on operator STATS:CLEar.
     *
     * Writer model / atomicity (PIC32MZ):
     *  - socketOpenFails is written from TWO tasks: wifi_tcp_server_OpenSocket()
     *    on app_WifiTask (pri 2) AND SocketEventCallback()'s SOCKET_MSG_BIND
     *    handler on the WINC driver task lWDRV_WINC_Tasks (pri 1).  Cross-task
     *    RMW → guarded by taskENTER_CRITICAL at every write site (matches the
     *    team pattern in #223/#451).
     *  - listenFails / acceptFails / acceptRefused are written ONLY from
     *    SocketEventCallback (single task) — plain ++ is safe; the 32-bit read
     *    is atomic and the SCPI reader's critical section keeps the snapshot
     *    coherent.
     *  - clientForceClosed / listenReopens / listenHardResets: self-heal action
     *    counts, single-writer (0 until Opt 1/2/3 land). */
    uint32_t socketOpenFails;   /* socket()/bind() HIF-send failure in OpenSocket — nonzero = WINC TCP-table exhaustion (the H2 smoking gun). CROSS-TASK: guard with taskENTER_CRITICAL */
    uint32_t listenFails;       /* SOCKET_MSG_LISTEN reported status != 0 (single-writer: SocketEventCallback) */
    uint32_t acceptFails;       /* SOCKET_MSG_ACCEPT arrived with a NULL message (single-writer: SocketEventCallback) */
    uint32_t acceptRefused;     /* one-client policy refused a 2nd connect — climbing = PATH-2 zombie churn (single-writer: SocketEventCallback) */
    uint32_t clientForceClosed; /* self-heal: dead client force-closed (0 until Opt 1) */
    uint32_t listenReopens;     /* self-heal: host re-listen count (0 until Opt 2) */
    uint32_t listenHardResets;  /* self-heal: WINC HardReset escalations (0 until Opt 3) */
} wifi_tcp_server_context_t;

/**
 * Resize the WiFi TCP circular write buffer.
 * Must only be called when streaming is stopped (no data in flight).
 * @param newSize New buffer size in bytes (minimum WIFI_WBUFFER_SIZE)
 * @return true if resized, false if failed or same size
 */
bool wifi_tcp_server_ResizeWriteBuffer(uint32_t newSize);

/** Queue bytes to the TCP client write buffer (drained by WifiTask).
 *  Returns bytes accepted (0 = buffer full or no client - see #371 counters). */
size_t wifi_tcp_server_WriteBuffer(const char* data, size_t len);

/** #598: true when the given SCPI context is the WiFi TCP console's. */
bool wifi_tcp_server_ContextIsTcp(const scpi_t* context);

/** #599: current accepted-connection generation (0 if no client has ever
 *  been accepted).  Captured at SD GET/LIST time to bind an async reply. */
uint32_t wifi_tcp_server_GetConnGeneration(void);

/** #599: true only if a client is currently connected AND its generation
 *  equals `generation` — i.e. the originating connection still owns the
 *  single TCP slot.  False after that client disconnects, or once a
 *  different client has taken the slot.  Backpressure (buffer full while the
 *  same client stays connected) does NOT flip this false. */
bool wifi_tcp_server_ConnIsCurrent(uint32_t generation);

/**
 * Swap the WiFi TCP circular write buffer to pool-managed memory.
 * Resets the buffer (discards pending data). Called by StreamingBufferPool
 * after partitioning.
 * @param buf Pointer to buffer memory (must remain valid)
 * @param size Buffer size in bytes
 */
void wifi_tcp_server_SetWriteBuffer(uint8_t* buf, uint32_t size);

/**
 * True when a TCP client is currently connected (socket open).
 * Used by the WINC idle-gate (#331) to skip pacing when the control
 * plane is in use.
 */
bool wifi_tcp_server_HasActiveClient(void);


/**
 * Returns the current count of bytes sitting in the WiFi TCP write
 * circular buffer (queued for send() but not yet drained).
 * Used by Streaming_Stop to capture session-end "tail" bytes for the
 * #367 accounting reconciliation. Returns 0 if not initialized.
 */
uint32_t wifi_tcp_server_GetCircularBufferAvailable(void);

    /* Provide C++ Compatibility */
#ifdef __cplusplus
}
#endif

#endif /* _WIFI_TCP_SERVER_H */

/* *****************************************************************************
 End of File
 */
