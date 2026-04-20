#ifndef _WINC_IDLE_GATE_H
#define _WINC_IDLE_GATE_H

/*
 * WincIdleGate.h — public interface for the WINC idle-gate pacing
 * logic and its debug accessor.
 *
 * The gate itself lives in config/default/tasks.c (inline with the
 * WINC task loop, per #335). This header owns the prototype so
 * multiple translation units (tasks.c implementation,
 * services/SCPI/SCPIInterface.c SCPI callback) stay in sync on the
 * signature without relying on forward `extern` declarations.
 */

#include "configuration.h"   /* SYS_STATUS type */
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Compute the WINC task's per-iteration vTaskDelay in milliseconds
 * based on the current SYS_STATUS. Tier policy:
 *   ERROR / UNINITIALIZED         → 50 ms (recovery backoff)
 *   BUSY / any non-READY state    → 0 ms  (tight polling for init)
 *   READY + streaming on non-WiFi interface + no TCP client → 50 ms
 *   READY + WiFi in use (streaming/TCP)                      → 0 ms
 *
 * See #335 for the full rationale. Do not add a delay to the
 * non-gated path — a 1 ms delay there bricked USB CDC during
 * STA-mode TCP streaming on PR #335.
 */
uint32_t WincIdleGate_ComputeDelay(SYS_STATUS status);

/*
 * Snapshot of the live gate inputs + resulting computed delay.
 * Used by the SYSTem:WINC:GATE? SCPI diagnostic (#55) to verify
 * tier transitions across WiFi state, streaming start/stop, and
 * TCP client connect/disconnect. Any out_* pointer may be NULL.
 */
void WincIdleGate_GetDebugState(int* out_status,
                                bool* out_streaming_non_wifi,
                                bool* out_tcp_client,
                                uint32_t* out_delay_ms);

#ifdef __cplusplus
}
#endif

#endif /* _WINC_IDLE_GATE_H */
