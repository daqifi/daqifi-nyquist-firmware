# Logging system

> Split out of `CLAUDE.md` on 2026-09-10. That file is loaded into **every turn of
> every agent**, and at ~38k tokens it was ~13% of all token spend; this material is
> reference — needed when you work in this area, not on every task. **It is
> unchanged, not summarised.** Read it in full before changing anything it
> describes, and update it here rather than re-adding it to `CLAUDE.md`.

Compile-time ceilings vs runtime levels, ISR-safe logging, one-shot suppression, and the module map.

---

### Logging System

Two-layer logging: compile-time ceilings control which log calls exist in the binary (all modules compile at DEBUG ceiling — every `LOG_E`/`LOG_I`/`LOG_D` is present), runtime levels control which execute (~2 CPU cycles per call when disabled). All modules boot at ERROR. Levels: `0`=NONE, `1`=ERROR, `2`=INFO, `3`=DEBUG. Module names: `POWER`, `WIFI`, `SD`, `USB`, `SCPI`, `ADC`, `DAC`, `STREAM`, `ENCODER`, `GENERAL`. Runtime-only — resets to ERROR on reboot.

**SCPI Commands:**
```bash
SYST:LOG?              # Retrieve all log messages (clears buffer after dump)
SYST:LOG:CLEAR         # Clear log buffer without reading
SYST:LOG:TEST          # Add test messages (for verification)
SYST:LOG:LEVel <module>,<level>   # e.g. SYST:LOG:LEV STREAM,2
SYST:LOG:LEVel? [module]          # With module: level only; no arg: all modules with level+ceiling
SYST:LOG:LEVel:ALL <level>        # Set all modules at once
```

**ISR-safe logging:** `LOG_E`/`LOG_I`/`LOG_D` are ISR-aware — they detect ISR context via FreeRTOS `uxInterruptNesting` and automatically route through a deferred queue (`xQueueSendFromISR` + drain task). No separate ISR macros needed. Caveat: format args (`%d`, `%u`, etc.) are ignored in ISR context to avoid `vsnprintf` on the ISR stack — use static strings in ISR handlers.

**One-shot suppression:** Two bitmask-based one-shot systems prevent log flooding from high-frequency errors:

| Macro | Bitmask | Reset When | Use Case |
|-------|---------|-----------|----------|
| `LOG_E_ONCE(bit, ...)` | `gLogOneShot` | `SYST:LOG?` / `SYST:LOG:CLEAR` | ISR context (8-entry deferred queue) |
| `LOG_E_SESSION(bit, ...)` | `gSessionOneShot` | `Streaming_ClearStats()` at stream start | Streaming engine per-sample errors |

Both use `volatile uint32_t` bitmasks (up to 32 call sites each). Bit indices defined in `LogOnceBit_t` and `LogSessionBit_t` enums in `Logger.h`. The `|=` is not critical-section-protected — worst case is one extra duplicate message per priority-crossing race. Also available: `LOG_I_ONCE`, `LOG_D_ONCE`, `LOG_I_SESSION`, `LOG_D_SESSION`.

**Module Mapping:**

| Module | Files |
|--------|-------|
| POWER | PowerApi.c, BQ24297.c |
| WIFI | wifi_manager.c, wifi_tcp_server.c, WINC driver |
| SD | sd_card_manager.c |
| USB | UsbCdc.c |
| SCPI | SCPIInterface.c, SCPIADC.c, SCPIDAC.c, SCPIDIO.c, SCPILAN.c, SCPIStorageSD.c |
| ADC | ADC.c, AD7609.c |
| DAC | DAC7718.c |
| STREAM | streaming.c |
| ENCODER | NanoPB_Encoder.c, csv_encoder.c, JSON_Encoder.c |
| GENERAL | app_freertos.c, AInSample.c, CircularBuffer.c, others |

**Circular buffer:** 64 messages × 128 bytes, mutex-protected; oldest message dropped when full.

**Compile-time ceiling override:** `#define LOG_LEVEL_WIFI LOG_LEVEL_NONE` (in Logger.h or project defines) strips a module's log calls from the binary entirely.

**Real-time UART logging (development only):** `#define ENABLE_ICSP_REALTIME_LOG 1` outputs logs via UART4 on ICSP pin 4 (RB0) at 921600 baud. Must be disabled before release.

**Implementation:** `firmware/src/Util/Logger.c`, `firmware/src/Util/Logger.h`
