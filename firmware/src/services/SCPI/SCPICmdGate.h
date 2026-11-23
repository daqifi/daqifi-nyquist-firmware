/* SCPICmdGate -- cross-transport SCPI command gate (#1279, audit r2).
 *
 * WHY: SCPI_RestoreIdleSdBuffers re-partitions the pool, and the re-partition
 * resets the USB and TCP output rings. It checks the rings are empty, but then
 * yields (SD idle, WINC SPI idle, the TCP in-flight wait), and during a yield
 * the OTHER transport's task can process a command and queue a reply that the
 * reset then deletes. The restore RAISES this gate for its own transport
 * before the emptiness check: the other transport's command-entry point then
 * defers (never blocks) until it is lowered, and the restore waits out a
 * command that was already executing.
 *
 * Tasks only (uses taskENTER_CRITICAL); never call from an ISR.
 */
#ifndef SCPI_CMD_GATE_H
#define SCPI_CMD_GATE_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SCPI_CMD_GATE_USB = 0,
    SCPI_CMD_GATE_TCP = 1,
    SCPI_CMD_GATE_COUNT
} ScpiCmdGateTransport;

/* Command-entry point of transport t. false = the gate is raised by the other
 * transport: DEFER the command (do not drop it, do not block). true = entered;
 * the caller MUST call ScpiCmdGate_Exit(t). */
bool ScpiCmdGate_TryEnter(ScpiCmdGateTransport t);
void ScpiCmdGate_Exit(ScpiCmdGateTransport t);

/* Raise the gate for `owner`. false if it is already raised (not nested). */
bool ScpiCmdGate_Raise(ScpiCmdGateTransport owner);
/* Lower it. No-op unless `owner` holds it. */
void ScpiCmdGate_Lower(ScpiCmdGateTransport owner);

/* true when the gate is raised and no OTHER transport has a command executing.
 * Once this reads true no other-transport command can start until Lower. */
bool ScpiCmdGate_OthersIdle(void);

#ifdef __cplusplus
}
#endif
#endif
