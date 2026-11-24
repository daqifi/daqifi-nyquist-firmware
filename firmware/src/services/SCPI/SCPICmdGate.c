/* SCPICmdGate -- see SCPICmdGate.h. All state is touched only inside a
 * critical section, so on this single core TryEnter's check-and-increment is
 * atomic against Raise. A command already running when the gate is raised is
 * handled by OthersIdle (depth must reach 0), not by memory ordering. */
#include "SCPICmdGate.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stdint.h>

static int8_t gOwner = -1;
static uint8_t gDepth[SCPI_CMD_GATE_COUNT];

bool ScpiCmdGate_TryEnter(ScpiCmdGateTransport t) {
    bool ok = true;
    taskENTER_CRITICAL();
    if (gOwner >= 0 && gOwner != (int8_t)t) {
        ok = false;
    } else {
        gDepth[t]++;
    }
    taskEXIT_CRITICAL();
    return ok;
}

void ScpiCmdGate_Exit(ScpiCmdGateTransport t) {
    taskENTER_CRITICAL();
    if (gDepth[t] > 0u) {
        gDepth[t]--;
    }
    taskEXIT_CRITICAL();
}

bool ScpiCmdGate_Raise(ScpiCmdGateTransport owner) {
    bool ok = false;
    taskENTER_CRITICAL();
    if (gOwner < 0) {
        gOwner = (int8_t)owner;
        ok = true;
    }
    taskEXIT_CRITICAL();
    return ok;
}

void ScpiCmdGate_Lower(ScpiCmdGateTransport owner) {
    taskENTER_CRITICAL();
    if (gOwner == (int8_t)owner) {
        gOwner = -1;
    }
    taskEXIT_CRITICAL();
}

bool ScpiCmdGate_OthersIdle(void) {
    bool idle;
    taskENTER_CRITICAL();
    idle = (gOwner >= 0);
    for (int t = 0; idle && t < (int)SCPI_CMD_GATE_COUNT; t++) {
        if (t != (int)gOwner && gDepth[t] != 0u) {
            idle = false;
        }
    }
    taskEXIT_CRITICAL();
    return idle;
}
