# Streaming Bug Report - TLB Exception on StartStreamData

## Summary
Issuing `SYSTem:StartStreamData` command without enabled ADC channels causes Simple TLB Refill Exception Handler crash.

## Environment
- **Board**: Nyquist NQ1
- **Firmware**: V3.0.0b2 (01-02)
- **Branch**: nq3_testing
- **Connection**: USB CDC ACM (/dev/ttyACM0)

## Steps to Reproduce

### Minimal Reproduction (CRASHES)
```bash
# 1. Connect to board via serial (115200 baud)
# 2. Send command:
SYSTem:StartStreamData
```

**Result**: Board crashes with Simple TLB Refill Exception

### Working Sequence (NO CRASH)
```bash
# 1. Connect to board via serial (115200 baud)
# 2. Enable at least one ADC channel:
CONFigure:ADC:CHANnel 0,1

# 3. Start streaming with frequency parameter:
SYSTem:StartStreamData 1
```

**Result**: Streaming works correctly

## Analysis

### Root Cause
The firmware's `SCPI_StartStreaming` function (SCPIInterface.c:1044) or `Streaming_Start()` function (streaming.c:164) does not properly handle the case where streaming is started with **no enabled ADC channels**.

### Likely Issues

1. **Heap Allocation Failure** (streaming.c:77-86):
   ```c
   if((sizeof(AInPublicSampleList_t)+200)>xPortGetFreeHeapSize()){
       LOG_E("Streaming: Insufficient heap for sample allocation...");
       continue;
   }
   pPublicSampleList=pvPortCalloc(1,sizeof(AInPublicSampleList_t));
   ```
   - If no channels are enabled, this allocation or subsequent access may trigger TLB exception

2. **Channel Iteration Without Validation** (streaming.c:87-96):
   ```c
   for (i = 0; i < pAiRunTimeChannelConfig->Size; i++) {
       if (pAiRunTimeChannelConfig->Data[i].IsEnabled == 1
               && AInChannel_IsPublic(&pBoardConfig->AInChannels.Data[i])) {
           pAiSample = BoardData_Get(BOARDDATA_AIN_LATEST, i);
           // ...
       }
   }
   ```
   - If no channels are enabled, the loop may still try to access uninitialized `pAiSample` pointers

3. **Queue Push Without Valid Data**:
   ```c
   if(!AInSampleList_PushBack(pPublicSampleList)){//failed pushing to Q
       vPortFree(pPublicSampleList);
   }
   ```
   - Pushing empty sample list to queue may cause issues downstream

## Expected Behavior

The firmware should:
1. Validate that at least one ADC channel is enabled before starting streaming
2. Return an error code via SCPI if streaming cannot be started
3. **NOT** crash with TLB exception

Suggested error message:
```
**ERROR: -221, "Settings conflict; no channels enabled"
```

## Suggested Fix

Add validation in `SCPI_StartStreaming` before calling `Streaming_UpdateState()`:

```c
// Check if any channels are enabled
bool hasEnabledChannels = false;
for (i = 0; i < pBoardConfigADC->Size; i++) {
    if (pRuntimeAInChannels->Data[i].IsEnabled == 1) {
        hasEnabledChannels = true;
        break;
    }
}

if (!hasEnabledChannels) {
    SCPI_ErrorPush(context, SCPI_ERROR_SETTINGS_CONFLICT);
    return SCPI_RES_ERR;
}
```

## Impact

**Severity**: HIGH
- Causes complete firmware crash requiring device reset
- Affects any code that attempts to start streaming without first verifying channel state
- Python API currently vulnerable to this issue

## Workaround

Always enable at least one ADC channel before issuing `SYSTem:StartStreamData`:

```bash
# Enable channel 0
CONFigure:ADC:CHANnel 0,1

# Then start streaming
SYSTem:StartStreamData
```

## Related Files
- `firmware/src/services/SCPI/SCPIInterface.c` - Lines 1044-1100 (`SCPI_StartStreaming`)
- `firmware/src/services/streaming.c` - Lines 49-128 (`_Streaming_Deferred_Interrupt_Task`)
- `firmware/src/services/streaming.c` - Lines 164-181 (`Streaming_Start`)

## Testing

After fix, verify:
1. ✅ Starting streaming with no enabled channels returns error (no crash)
2. ✅ Starting streaming with enabled channels works normally
3. ✅ Error message is appropriate and helpful

---

**Discovered by**: Claude Code during Python API testing
**Date**: 2025-10-16
**Priority**: High (crash bug)
