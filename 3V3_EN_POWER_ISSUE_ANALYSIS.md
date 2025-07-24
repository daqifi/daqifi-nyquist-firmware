# 3.3V_EN Power Issue Analysis

## Issue Description
Device powers off when USB is disconnected, related to GitHub issue #23: "Disconnecting USB causes device to power off"

## Key Finding
There's a critical timing issue with the 3.3V_EN signal during power-on sequence when using the power button (without USB connected).

## Hardware Context
- **Power Button**: Temporarily provides 3.3V to the rail while held
- **USB Connection**: Directly powers 3.3V rail, masking the timing issue
- **3.3V_EN Signal**: GPIO that must be set to latch 3.3V power after button release

## Current Boot Sequence (NQ1 Board)

1. User presses and holds power button → 3.3V rail powered
2. Microcontroller boots and starts executing code
3. Eventually reaches `APP_FREERTOS_Tasks()` → `app_SystemInit()` → `Power_Init()`
4. `Power_Init()` finally sets EN_3_3V GPIO based on `PowerWriteVars.EN_3_3V_Val`
5. **PROBLEM**: If user releases button before step 4, device loses power

## Code Analysis

### Key Files and Locations

1. **NQ1 Runtime Defaults** (`firmware/src/state/runtime/NQ1RuntimeDefaults.c`)
   - Line 89: `.EN_3_3V_Val = true,     // 3.3V rail on`
   - NQ1 defaults to EN_3_3V enabled (true)
   - NQ2/NQ3 have it set to false (commented out)

2. **Power API Implementation** (`firmware/src/HAL/Power/PowerApi.c`)
   - `Power_Init()` (line 104-135): Sets all power GPIOs including EN_3_3V
   - Line 119-121: Writes EN_3_3V value to GPIO
   - `Power_Up()` (line 220-260): Sets `EN_3_3V_Val = true` when powering up
   - `Power_Down()` (line 262-282): Sets `EN_3_3V_Val = false` when powering down

3. **UI/Button Handler** (`firmware/src/HAL/UI/UI.c`)
   - `Button_Tasks()` (line 44-88): Detects button press
   - After ~1 second hold, sets `requestedPowerState = DO_POWER_UP`

4. **App Initialization** (`firmware/src/app_freertos.c`)
   - Line 229-233: Comment says "Power initialization - enables 3.3V rail by default"
   - `Power_Init()` called during `app_SystemInit()`

### Power State Machine
- `POWERED_DOWN = 0`: Powered down
- `MICRO_ON = 1`: 3.3V rail enabled, ready to check initial status
- `POWERED_UP = 2`: Board fully powered
- `POWERED_UP_EXT_DOWN = 3`: Board partially powered, external power disabled

## Root Cause
The EN_3_3V signal is not being set high early enough in the boot process. By the time `Power_Init()` runs (after FreeRTOS initialization, board config loading, etc.), the user may have already released the power button, causing the 3.3V rail to lose power.

## Questions for Hardware Team

1. **Signal Polarity**: Is EN_3_3V active high or active low? (Code assumes high = enabled)
2. **Hardware Latch**: Is there a hardware latch or pull-up/pull-down on EN_3_3V?
3. **EN_12V Note**: Code shows "12V rail off (inverse logic)" - does EN_3_3V also use inverse logic?

## Potential Solutions

1. **Early GPIO Init**: Set EN_3_3V high as one of the first operations during boot, before any other initialization
2. **Hardware Fix**: Add pull-up resistor to maintain EN_3_3V state during boot
3. **Boot Order**: Move Power_Init() much earlier in the boot sequence

## Testing Notes
- Issue only affects NQ1 boards (NQ2/NQ3 not tested yet)
- USB connection masks the issue by providing constant 3.3V power
- Problem occurs when:
  - Device powered only by battery
  - User presses power button but releases before ~2-3 seconds

## Next Steps
1. Verify EN_3_3V signal polarity with circuit schematic
2. Test moving GPIO initialization earlier in boot sequence
3. Consider hardware modifications if software fix insufficient