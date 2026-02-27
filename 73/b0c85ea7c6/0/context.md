# Session Context

## User Prompts

### Prompt 1

Implement the following plan:

# Robust REG09 Fault Handling for BQ24297

## Context

BQ24297 REG09 is a latched fault register — reading it clears all latched faults. The current firmware has three problems:

1. **Latched faults silently lost**: Every `BQ24297_UpdateStatus()` call does a double-read (clear latched, read current). The latched faults from the first read are discarded. If a transient fault occurred between reads, it vanishes.

2. **SCPI diagnostic commands inconsistent*...

### Prompt 2

the device is connected and flashed. check the device for proper operation. use powershell - perhaps you can use powershell to interact live with the device instead of creating a script?

### Prompt 3

commit and push.

### Prompt 4

check Add timeout to prevent indefinite blocking in USB write loop and Fix fault field bit masking qodo comments

