# Throughput Bottleneck Analysis

**Date:** 2025-01-19
**Context:** Investigating why 1-4.5kHz streaming loses ~40-60% of samples

## Observed Performance Issues

| Rate  | Expected Samples | Got Samples | Loss % | ChannelScanFreqDiv |
|-------|-----------------|-------------|--------|-------------------|
| 1kHz  | 2000            | 1369        | 32%    | 1                 |
| 2kHz  | 4000            | 1367        | 66%    | 2                 |
| 3kHz  | 6000            | 1387        | 77%    | 3                 |
| 4kHz  | 8000            | 1373        | 83%    | 4                 |
| 4.5kHz| 9000            | 1471        | 84%    | 4                 |

**Critical Finding**: Even at 1kHz (well within ADC capabilities), we're losing 32% of samples.

## Data Flow Architecture

```
Timer Interrupt (1-5kHz)
    ↓
Streaming_TimerHandler()
    ↓
Streaming_Defer_Interrupt()
    ↓
_Streaming_Deferred_Interrupt_Task (Priority 8) ← HIGH PRIORITY
    ├─ pvPortCalloc(208 bytes)              [BOTTLENECK 1]
    ├─ Copy sample data
    ├─ AInSampleList_PushBack()              [BOTTLENECK 2]
    └─ xTaskNotifyGive(streaming_Task)
         ↓
streaming_Task (Priority 2) ← LOW PRIORITY
    ├─ Check buffer space (USB/WiFi/SD)      [BOTTLENECK 3]
    ├─ csv_Encode()                          [BOTTLENECK 4]
    │   ├─ Loop: AInSampleList_PeekFront()
    │   ├─ Multiple snprintf() per sample
    │   ├─ Float math: ADC_ConvertToVoltage() * 1000.0
    │   ├─ AInSampleList_PopFront()
    │   └─ vPortFree()                       [BOTTLENECK 5]
    └─ WriteToBuffer (USB/WiFi/SD)           [BOTTLENECK 6]
```

## Identified Bottlenecks

### 1. **Memory Allocation on Every Sample** (CRITICAL)
**Location**: `firmware/src/services/streaming.c:83`

```c
pPublicSampleList=pvPortCalloc(1,sizeof(AInPublicSampleList_t));  // ~208 bytes
```

**Impact**:
- At 4kHz: 4000 malloc operations per second
- At 4kHz: 4000 free operations per second (in encoder)
- FreeRTOS heap_4 uses best-fit algorithm with O(n) search
- Heap fragmentation risk at high rates
- Each malloc/free involves:
  - Critical section entry/exit
  - Free block list traversal
  - Block splitting/coalescing

**Measurement**:
- Heap operations likely take 10-50µs each
- At 4kHz (250µs period): malloc+free = 20-100µs = 8-40% of period!

---

### 2. **Sample Queue Management Overhead**
**Location**: `firmware/src/services/streaming.c:98`

```c
if(!AInSampleList_PushBack(pPublicSampleList)){
    vPortFree(pPublicSampleList);
}
```

**Impact**:
- Queue is FIFO with linked list implementation
- Each push/pop involves list manipulation
- `AInSampleList_PushBack()` likely uses FreeRTOS queue (more overhead)

---

### 3. **Buffer Space Checks Every Iteration**
**Location**: `firmware/src/services/streaming.c:246-268`

```c
usbSize = UsbCdc_WriteBuffFreeSize(NULL);
wifiSize = wifi_manager_GetWriteBuffFreeSize();
sdSize = sd_card_manager_GetWriteBuffFreeSize();

hasUsb = (usbSize > BUFFER_SIZE);
hasWifi = (wifiSize > BUFFER_SIZE);
hasSD = (sdSize > BUFFER_SIZE);

maxSize = BUFFER_SIZE;
if (hasUsb) maxSize = min(maxSize, usbSize);
if (hasWifi) maxSize = min(maxSize, wifiSize);
if (hasSD) maxSize = min(maxSize, sdSize);

if (maxSize < 128) {
    continue;  // SAMPLE DROPPED!
}
```

**Impact**:
- 3 function calls to check buffer space
- If any buffer is full → sample dropped
- No buffering/retry logic
- SD card write latency can block entire pipeline

---

### 4. **CSV Encoder Performance**
**Location**: `firmware/src/services/csv_encoder.c:36-90`

**Inefficiencies**:

a) **Floating Point Math** (Line 67):
```c
int mv = (int)(ADC_ConvertToVoltage(s) * 1000.0);
```
- Floating point multiplication on every sample
- Should use integer math: `(ADC_raw * scale) >> shift`

b) **Multiple snprintf() Calls**:
```c
w = snprintf(q, rem, "%u", state->StreamTrigStamp);           // Line 59
w = snprintf(q, rem, ",%u,%d", s->Timestamp, mv);             // Line 68
w = snprintf(q, rem, ",,");                                   // Line 70
w = snprintf(q, rem, ",%u,%u", dioPeek.Timestamp, dioPeek.Values);  // Line 78
```
- snprintf is SLOW (format string parsing, bounds checking)
- For 16 channels: 1 + 16 + 1 = 18 snprintf calls per sample!
- Each snprintf ~2-5µs = 36-90µs per sample

c) **ADC_ConvertToVoltage Overhead**:
- Function call overhead
- Floating point division/multiplication
- Should pre-calculate calibration and use integer math

**Estimated CSV encoding time per sample**:
- 18 snprintf calls: 36-90µs
- Float math: 5-10µs
- **Total: 41-100µs per sample**
- At 4kHz: 164-400ms of CPU time per second just for CSV encoding!

---

### 5. **Sample Deallocation in Encoder**
**Location**: `firmware/src/services/csv_encoder.c:118-120`

```c
if (hadAIN) {
    AInPublicSampleList_t *tmp = NULL;
    AInSampleList_PopFront(&tmp);
    vPortFree(tmp);  // Free allocated memory
}
```

**Impact**:
- Free operation in low-priority task
- Adds to overall heap churn
- Combined with allocation: double heap overhead

---

### 6. **SD Card Write Latency**
**Location**: `firmware/src/services/streaming.c:306-312`

```c
if (hasSD) {
    size_t written = sd_card_manager_WriteToBuffer((const char *) buffer, packetSize);
}
```

**Potential Issues**:
- SD card writes can block (SPI bus)
- Circular buffer may be full
- If write fails/blocks → samples accumulate in queue
- No flow control or backpressure

---

### 7. **Task Priority Inversion**
**Location**: `firmware/src/services/streaming.c:115-125`

```c
xTaskCreate((TaskFunction_t) streaming_Task,
            "Stream task",
            4096, NULL, 2, &gStreamingTaskHandle);  // Priority 2 (LOW)

xTaskCreate((TaskFunction_t) _Streaming_Deferred_Interrupt_Task,
            "Stream Interrupt",
            4096, NULL, 8, &gStreamingInterruptHandle);  // Priority 8 (HIGH)
```

**Impact**:
- High-priority task (8) allocates and queues samples
- Low-priority task (2) processes and frees them
- If encoding is slow → queue fills → allocations fail → samples dropped
- Classic producer-consumer rate mismatch

---

## DIO Timing Test Infrastructure

Already instrumented in `streaming.c`:

```c
// Measure CSV encoding time
DIO_TIMING_TEST_WRITE_STATE(1);  // Set DIO_0 HIGH
packetSize = csv_Encode(...);
DIO_TIMING_TEST_WRITE_STATE(0);  // Set DIO_0 LOW

// Measure write buffer time
DIO_TIMING_TEST_WRITE_STATE(1);  // Set DIO_0 HIGH
if (hasUsb) UsbCdc_WriteToBuffer(...);
if (hasWifi) wifi_manager_WriteToBuffer(...);
if (hasSD) sd_card_manager_WriteToBuffer(...);
DIO_TIMING_TEST_WRITE_STATE(0);  // Set DIO_0 LOW
```

**To enable**: Define `DIO_TIMING_TEST` in build or add to `DIO.h`

---

## Recommendations (Prioritized)

### IMMEDIATE (High Impact, Low Risk)

#### 1. Eliminate Floating Point Math in CSV Encoder
**File**: `csv_encoder.c:67`

**Current**:
```c
int mv = (int)(ADC_ConvertToVoltage(s) * 1000.0);
```

**Replace with**:
```c
// Pre-calculated integer conversion (from ADC layer)
int mv = ADC_RawToMillivolts(s->Value, s->Channel);
```

**Impact**: Saves 5-10µs per sample → 20-40ms/sec at 4kHz

---

#### 2. Replace snprintf with Direct Integer-to-String
**File**: `csv_encoder.c`

**Current**: 18 snprintf calls per sample
**Replace with**: Custom `itoa_fast()` or lookup table

**Estimated speedup**: 36-90µs → 10-20µs = **16-70µs saved per sample**

**Impact**: At 4kHz, saves 64-280ms of CPU time per second!

---

#### 3. Batch Sample Processing (Reduce Malloc/Free)
**File**: `streaming.c:83`

**Current**: Allocate individual samples
**Proposed**: Pre-allocate pool of sample buffers, reuse circular

**Impact**:
- Eliminates 4000-8000 heap operations per second
- Reduces fragmentation
- Saves 20-100µs per sample

---

### MEDIUM (Performance Optimization)

#### 4. Optimize Buffer Space Checks
**File**: `streaming.c:246-268`

**Current**: Check all 3 buffers every iteration
**Proposed**: Only check if recently changed, cache results for 10-100ms

**Impact**: Minor, but reduces function call overhead

---

#### 5. Increase Streaming Task Priority
**File**: `streaming.c:115`

**Current**: Priority 2
**Proposed**: Priority 5-6 (between interrupt task and background tasks)

**Impact**: Reduces queue buildup, improves latency

---

### LONG-TERM (Architectural Changes)

#### 6. Implement Zero-Copy Streaming
- Use DMA for ADC → circular buffer
- Encoder reads directly from circular buffer
- No malloc/free in critical path

#### 7. Dedicated Encoding Task per Channel
- USB, WiFi, SD each have dedicated encoder
- Parallel processing
- Better CPU utilization

---

## Measurement Plan

1. **Enable DIO timing tests** to measure:
   - CSV encoding time per packet
   - Write buffer time per packet
   - Total iteration time

2. **Add timing instrumentation**:
   - Measure interrupt task execution time
   - Measure heap allocation time
   - Count queue depth over time

3. **Profile with different rates**:
   - 1kHz, 2kHz, 3kHz, 4kHz
   - Identify where bottleneck shifts

---

## Expected Improvements

| Optimization | Est. Time Saved | Impact on 4kHz Throughput |
|--------------|----------------|---------------------------|
| Remove float math | 5-10µs/sample | +2-4% |
| Fast int-to-string | 16-70µs/sample | +6-28% |
| Eliminate malloc/free | 20-100µs/sample | +8-40% |
| **Combined** | **41-180µs/sample** | **+16-72% throughput** |

At 4kHz with 250µs period, saving 41-180µs per sample could increase effective throughput from ~1400 samples/sec to **2000-3500 samples/sec** (50-250% improvement).
