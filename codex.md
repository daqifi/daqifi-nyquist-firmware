# Codex Working Notes

## Communication Accuracy
- Use precise, verifiable wording in all technical responses.
- Avoid ambiguous timing terms like "every tick" unless RTOS tick cadence is explicitly intended and stated.
- When discussing frequency/cadence, include concrete units and source references (for example, "called every 100 ms in `app_freertos.c`").
- Distinguish clearly between "function is called" and "function performs I2C/SPI/network work".

## Workflow Preferences
- If `gh` access fails in sandbox (API/connectivity restrictions), rerun the GitHub command with elevated permissions.
- Prefer hardening at module/public API boundaries first (argument validation, fail-fast behavior, bounded retries), then tighten internal paths only if needed.
