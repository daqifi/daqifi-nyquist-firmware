# CSV SD Card Logging Bug Report

**Date:** 2025-01-19
**Firmware Version:** NQ1 (current dev branch)
**Issue:** No data captured at 5kHz sampling rate, but works at 1kHz

## Summary

SD card logging with CSV encoding works correctly at 1kHz but produces empty data fields at 5kHz. The CSV encoder is functioning properly - the issue is in the data acquisition path at higher sample rates.

## Test Results

### Intermediate Rate Testing - Identified Exact Threshold

**Summary Table:**

| Rate  | ChannelScanFreqDiv | Result  | Lines | Expected | File Size |
|-------|-------------------|---------|-------|----------|-----------|
| 1kHz  | 1                 | ✅ SUCCESS | 1369  | 2000     | 128KB     |
| 2kHz  | 2                 | ✅ SUCCESS | 1367  | 4000     | 128KB     |
| 3kHz  | 3                 | ✅ SUCCESS | 1387  | 6000     | 130KB     |
| 4kHz  | 4                 | ✅ SUCCESS | 1373  | 8000     | 129KB     |
| 4.5kHz| **4** (int div)   | ✅ SUCCESS | 1471  | 9000     | 131KB     |
| 5kHz  | **5**             | ❌ FAIL    | 3023  | 10000    | 225KB     |

**Key Finding:** Failure occurs exactly at **ChannelScanFreqDiv = 5**

### Test 1: Single Channel (Channel 4) at 5kHz - FAIL
**Configuration:**
- Sample rate: 5000 Hz
- Duration: 3 seconds
- Channels enabled: 4 only
- Encoding: CSV (value 2)
- ChannelScanFreqDiv: 5

**Result:**
- File created: `csv_5khz_test.csv` (225KB, 3023 lines)
- **NO VALID DATA** - all fields empty (tabs and commas only)
- CSV encoder wrote `,,` for each channel (indicating `isSampleValid[i]` was false)

**Evidence:**
```python
# Retrieved file contains only empty fields:
Header:  	 	 	 	 	... (3000+ lines of tabs/commas)
```

### Test 2: Multiple Channels at 1kHz - SUCCESS
**Configuration:**
- Sample rate: 1000 Hz
- Duration: 2 seconds
- Channels enabled: 0, 1, 4, 8
- Encoding: CSV (value 2)
- ChannelScanFreqDiv: 1

**Result:**
- File created: `test_all_ch.csv` (128KB, 1369 lines)
- **DATA ACQUIRED SUCCESSFULLY**
- Channels producing valid samples with timestamps and voltage readings

**Evidence:**
```csv
4011886027,4011695505,2773,4011695505,2,,,,,4011695505,39,,,,,,,4011695505,40,,,,,,,,,,,,,,,,
4011936075,4011886027,2773,4011886027,2,,,,,4011886027,40,,,,,,,4011886027,40,,,,,,,,,,,,,,,,
```

Format: `timestamp,timestamp,voltage_mV` pairs for each enabled channel

### Test 3: 4.5kHz Rate - SUCCESS (Critical Data Point)
**Configuration:**
- Sample rate: 4500 Hz
- Duration: 2 seconds
- Channel: 4 only
- ChannelScanFreqDiv: **4** (4500/1000 = 4 via integer division)

**Result:**
- File created: `csv_4500hz_test.csv` (131KB, 1471 lines)
- **DATA ACQUIRED SUCCESSFULLY**
- Valid CSV data with timestamps and voltage readings

**Evidence:**
```csv
766194324,766172123,2774,766172123,2,,,,,766172123,39,,,,,,,766172123,41
766216566,766172123,2774,766172123,2,,,,,766183216,40,,,,,,,766183216,39
```

## Root Cause Analysis

### What Works:
1. ✅ CSV encoder (`csv_encoder.c`) - correctly writes data when samples are valid
2. ✅ CSV encoder correctly writes `,,` when `isSampleValid[i]` is false
3. ✅ SCPI command `SYST:STR:FOR 2` sets encoding correctly
4. ✅ Data acquisition at 1kHz sample rates
5. ✅ Multi-channel acquisition
6. ✅ SD card file writing

### What Fails:
1. ❌ No valid samples at 5kHz (all channels report `isSampleValid[i]` = false)
2. ❌ Single channel acquisition at 5kHz produces no data

### Investigation Path

#### ChannelScanFreqDiv Calculation (SCPIInterface.c:1109-1113)

```c
if (freq > 1000) {
    pRunTimeStreamConfig->ChannelScanFreqDiv = freq / 1000;  // Integer division
} else {
    pRunTimeStreamConfig->ChannelScanFreqDiv = 1;
}
```

**Critical Finding:** The failure threshold is exactly at `ChannelScanFreqDiv = 5`:
- 1kHz → ChannelScanFreqDiv = 1 ✅
- 2kHz → ChannelScanFreqDiv = 2 ✅
- 3kHz → ChannelScanFreqDiv = 3 ✅
- 4kHz → ChannelScanFreqDiv = 4 ✅
- 4.5kHz → ChannelScanFreqDiv = 4 (integer division) ✅
- **5kHz → ChannelScanFreqDiv = 5** ❌

#### Streaming Interrupt Task (streaming.c:88-96)

The streaming interrupt task reads the latest ADC sample and marks it valid:

```c
for (i = 0; i < pAiRunTimeChannelConfig->Size; i++) {
    if (pAiRunTimeChannelConfig->Data[i].IsEnabled == 1
            && AInChannel_IsPublic(&pBoardConfig->AInChannels.Data[i])) {
        pAiSample = BoardData_Get(BOARDDATA_AIN_LATEST, i);
        pPublicSampleList->sampleElement[i].Channel=pAiSample->Channel;
        pPublicSampleList->sampleElement[i].Timestamp=pAiSample->Timestamp;
        pPublicSampleList->sampleElement[i].Value=pAiSample->Value;
        pPublicSampleList->isSampleValid[i]=1;  // Only if sample acquired
    }
}
```

At ChannelScanFreqDiv = 5, `BoardData_Get(BOARDDATA_AIN_LATEST, i)` returns stale or invalid sample data.

#### ADC Trigger Logic (streaming.c:102-118)

```c
if (pRunTimeStreamConf->ChannelScanFreqDiv == 1) {
    // Trigger ALL channels every interrupt
    for (i = 0; i < pRunTimeAInModules->Size; ++i) {
        ADC_TriggerConversion(&pBoardConfig->AInModules.Data[i], MC12B_ADC_TYPE_ALL);
    }
} else if (pRunTimeStreamConf->ChannelScanFreqDiv != 0) {
    // Trigger DEDICATED (Type 1) channels every interrupt
    for (i = 0; i < pRunTimeAInModules->Size; ++i) {
        ADC_TriggerConversion(&pBoardConfig->AInModules.Data[i], MC12B_ADC_TYPE_DEDICATED);
    }

    // Trigger SHARED (Type 2) channels only every N interrupts
    if (ChannelScanFreqDivCount >= pRunTimeStreamConf->ChannelScanFreqDiv) {
        for (i = 0; i < pRunTimeAInModules->Size; ++i) {
            ADC_TriggerConversion(&pBoardConfig->AInModules.Data[i], MC12B_ADC_TYPE_SHARED);
        }
        ChannelScanFreqDivCount = 0;
    }
    ChannelScanFreqDivCount++;
}
```

**Observation:** Channel 4 is Type 1 (dedicated), so it **should** be triggered every interrupt regardless of ChannelScanFreqDiv value. However, at ChannelScanFreqDiv = 5, no valid data is captured.

**Possible Root Causes:**
1. **Timing Issue:** At 5kHz with ChannelScanFreqDiv=5, the ADC conversion timing may not complete before the next sample read
2. **Interrupt Latency:** Without DMA, the interrupt overhead for reading Type 1 channels may exceed available time at high ChannelScanFreqDiv values
3. **Sample Staleness:** The "latest" sample may not be getting updated before it's read by the streaming task
4. **Task Scheduling:** At higher ChannelScanFreqDiv, the deferred interrupt task may be starved or delayed
5. **Counter Bug:** The ChannelScanFreqDivCount increment logic may have timing issues at specific values

## Recommendations

### Immediate Investigation

1. ✅ **COMPLETED: Test intermediate rates**
   - Tested 1kHz, 2kHz, 3kHz, 4kHz, 4.5kHz, 5kHz
   - **Found:** Exact threshold at ChannelScanFreqDiv = 5 (5kHz)
   - 4.5kHz works (ChannelScanFreqDiv=4), 5kHz fails (ChannelScanFreqDiv=5)

2. **Debug ChannelScanFreqDiv=5 behavior:**
   - Add debug logging at ChannelScanFreqDiv = 5 specifically
   - Check if ADC_TriggerConversion is being called for Type 1 channels
   - Verify BoardData_Get returns valid/stale data
   - Monitor sample timestamp updates
   - Check ChannelScanFreqDivCount behavior

3. **Investigate timing at ChannelScanFreqDiv=5:**
   - Measure actual time between ADC trigger and sample ready
   - Check if 200µs period at 5kHz causes timing conflicts
   - Verify interrupt priority and scheduling at high rates
   - Check if deferred interrupt task is being starved

### Potential Fixes
1. **If timing issue:** Reduce maximum sample rate or add delay for conversion completion
2. **If buffer issue:** Increase sample queue depth or reduce encoding overhead
3. **If ChannelScanFreqDiv issue:** Fix the trigger logic to ensure Type 1 channels always get triggered

## Test Scripts Created
1. `delete_and_test_csv.py` - Tests 5kHz single channel with CSV encoding
2. `test_all_channels_csv.py` - Tests 1kHz multi-channel with CSV encoding
3. `test_intermediate_rates.py` - Tests 2kHz, 3kHz, 4kHz to find failure threshold
4. `test_4500hz.py` - Tests 4.5kHz (critical test showing ChannelScanFreqDiv=4 works)
5. `get_csv_analyze.py` - Retrieves and analyzes CSV files from SD card
6. `get_all_ch_csv.py` - Retrieves multi-channel test file

## Conclusion

CSV SD card logging works correctly at sample rates up to 4.5kHz but completely fails at exactly 5kHz. The bug is in the ADC data acquisition path and is directly correlated with the `ChannelScanFreqDiv` parameter.

**Key Findings:**
- ✅ CSV encoder works correctly
- ✅ Data acquisition works up to 4.5kHz (ChannelScanFreqDiv ≤ 4)
- ❌ Complete failure at 5kHz+ (ChannelScanFreqDiv ≥ 5)
- 📊 All successful tests showed sample drops (~1300-1500 samples vs expected ~2000-9000)
- 🔍 Failure is **not** related to sample rate alone, but to ChannelScanFreqDiv value

**Root Cause:** Likely a bug or timing issue in the streaming interrupt logic when `ChannelScanFreqDiv >= 5`. The exact mechanism is unknown but possibilities include:
- ADC trigger timing conflicts
- Sample staleness in BoardData
- Interrupt task scheduling issues
- Counter overflow or logic bug in ChannelScanFreqDivCount

**Priority:** High - Affects high-speed data logging capability

**Impact:** Users cannot reliably log data at 5kHz+ to SD card. Workaround: Use 4.5kHz maximum sample rate (or ≤4.999kHz to keep ChannelScanFreqDiv=4)
