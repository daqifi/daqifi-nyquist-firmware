# SD Card Performance Analysis and Benchmarking Documentation

## Current SD Card Implementation Overview

The DAQiFi Nyquist firmware implements SD card functionality through a layered architecture, but currently lacks dedicated benchmarking capabilities for measuring write performance.

### Architecture Stack

1. **Hardware Layer**
   - SPI4 bus shared with WiFi module
   - Configured at 20 MHz SPI clock
   - Uses chip select pin RD9
   - Initial card detection at 400 kHz

2. **Driver Layer** (`drv_sdspi.c`)
   - Microchip Harmony SPI-based SD card driver
   - Implements standard SD card protocol over SPI
   - Uses DMA for SPI transfers (good!)
   - Single-sector operations (512 bytes)

3. **File System Layer**
   - FAT filesystem via Harmony File System service
   - Mount point: `/mnt/Daqifi`
   - Device name: `/dev/mmcblka1`

4. **Application Layer** (`sd_card_manager.c`)
   - Circular buffer implementation
   - State machine-based operation
   - Mutex-protected write buffer
   - Periodic flush mechanism

## Current Performance Characteristics

### Buffer Configuration
```c
// Write buffer size
#define SD_CARD_MANAGER_CONF_WBUFFER_SIZE  5120  // 5KB per write

// Circular buffer size  
#define SD_CARD_MANAGER_CIRCULAR_BUFFER_SIZE (SD_CARD_MANAGER_CONF_WBUFFER_SIZE*12)  // 60KB total

// But streaming is limited to:
#define BUFFER_SIZE 700  // Due to USB CDC constraint!
```

### Write Performance Limiters

1. **700-byte Streaming Buffer**
   - Primary bottleneck
   - All data limited to 700-byte chunks
   - Results in ~14% buffer utilization (700/5120)

2. **Flush Policy**
   ```c
   // Flush triggers (sd_card_manager.c:330-340)
   if ((currentMillis - gSdCardData.lastFlushMillis > 5000 ||  // 5 seconds
        gSdCardData.totalBytesFlushPending > 4096)) {          // 4KB data
       SYS_FS_FileSync(gSdCardData.fileHandle);
   }
   ```
   - Flushes every 5 seconds OR when 4KB accumulated
   - FileSync operation blocks other writes
   - No write combining optimization

3. **SPI Bus Contention**
   - Shared with WiFi module
   - No high-level coordination
   - Potential for interleaved operations

4. **Single-Sector Writes**
   - No multi-block write optimization
   - Each write potentially updates FAT
   - No write caching at filesystem level

### Theoretical vs. Actual Performance

**Theoretical Maximum:**
- SPI clock: 20 MHz
- SPI efficiency: ~80% (overhead for commands)
- Theoretical throughput: 20 MHz × 80% / 8 bits = 2 MB/s

**Observed Performance:**
- Reported: 2-5 MB/s (matches theoretical for SPI)
- Limited by 700-byte packets and flush policy
- Further reduced by filesystem overhead

## Current Benchmarking Capabilities

### 1. No Dedicated SD Card Benchmark
Currently, there is **no built-in SD card write/read speed benchmark**. The firmware lacks:
- Test data generation
- Throughput measurement
- Latency profiling
- Performance statistics

### 2. Available Performance Tools

#### DIO Timing Test (Partially Implemented)
```c
// In BoardConfig.h - currently commented out
//#define DIO_TIMING_TEST

// When enabled, provides:
DIO_TIMING_TEST_INIT()           // Initialize GPIO for timing
DIO_TIMING_TEST_WRITE_STATE(x)   // Set pin high/low
DIO_TIMING_TEST_TOGGLE_STATE()   // Toggle pin
```

Used in `streaming.c` to measure encoding performance:
```c
DIO_TIMING_TEST_WRITE_STATE(1);
packetSize = csv_Encode(pBoardData, &nanopbFlag, buffer, maxSize);
DIO_TIMING_TEST_WRITE_STATE(0);
```

#### FreeRTOS Performance Monitoring
- Stack high water marks: **Enabled**
- Task runtime stats: **Available but disabled**
- Can be enabled via `configGENERATE_RUN_TIME_STATS`

#### SCPI Commands (Limited)
```c
// Currently not implemented
{"BENCHmark?", SCPI_BenchMarkGet},  // Returns SCPI_NotImplemented

// Streaming stats commented out
//{"STReam:STATs?", SCPI_SystStreamStatsQuery},
//{"STReam:STATs:CLEar", SCPI_SystStreamStatsClear},
```

### 3. Manual Performance Testing

Currently, SD card performance must be measured manually by:
1. Writing known data volumes
2. Measuring time externally
3. Calculating throughput offline

## Recommended SD Card Benchmark Implementation

### 1. Dedicated Benchmark SCPI Command
```c
// Add to SCPI command set
{"STORage:SD:BENCHmark", SCPI_StorageSDBenchmark},
{"STORage:SD:BENCHmark?", SCPI_StorageSDBenchmarkQuery},

// Benchmark parameters
typedef struct {
    uint32_t testSizeKB;      // Test data size (KB)
    uint32_t blockSize;       // Write block size
    uint32_t pattern;         // Data pattern (0xFF, 0xAA, random)
    bool sequential;          // Sequential vs random access
    bool skipFlush;           // Test with/without flush
} SDBenchmarkParams_t;

// Benchmark results
typedef struct {
    uint32_t writeBytesPerSec;
    uint32_t readBytesPerSec;
    uint32_t totalTimeMs;
    uint32_t flushTimeMs;
    uint32_t minLatencyMs;
    uint32_t maxLatencyMs;
    uint32_t avgLatencyMs;
} SDBenchmarkResults_t;
```

### 2. Performance Metrics Collection
```c
// Add to sd_card_manager.c
typedef struct {
    uint32_t totalBytesWritten;
    uint32_t totalWriteTimeMs;
    uint32_t writeCount;
    uint32_t flushCount;
    uint32_t errorCount;
    uint32_t lastWriteSpeedBps;
    uint32_t peakWriteSpeedBps;
    uint32_t averageWriteSpeedBps;
} SDCardStats_t;
```

### 3. Test Data Generator
```c
// Generate test patterns
void SD_GenerateTestData(uint8_t* buffer, size_t size, uint32_t pattern) {
    switch(pattern) {
        case PATTERN_ALL_ONES:
            memset(buffer, 0xFF, size);
            break;
        case PATTERN_ALL_ZEROS:
            memset(buffer, 0x00, size);
            break;
        case PATTERN_ALTERNATING:
            for(int i = 0; i < size; i++) {
                buffer[i] = (i % 2) ? 0xAA : 0x55;
            }
            break;
        case PATTERN_RANDOM:
            // Use PRNG for reproducible random
            break;
        case PATTERN_SEQUENTIAL:
            for(int i = 0; i < size; i++) {
                buffer[i] = i & 0xFF;
            }
            break;
    }
}
```

## Performance Optimization Opportunities

### Immediate Optimizations (Quick Wins)

1. **Increase Streaming Buffer**
   ```c
   // Instead of using minimum of all buffers
   uint8_t sdBuffer[SD_CARD_MANAGER_CONF_WBUFFER_SIZE];  // Use full 5KB
   ```

2. **Optimize Flush Policy**
   ```c
   // Increase flush thresholds
   #define FLUSH_TIMEOUT_MS    30000   // 30 seconds
   #define FLUSH_SIZE_BYTES    32768   // 32KB
   ```

3. **Implement Write Combining**
   ```c
   // Combine small writes into larger blocks
   if (writeSize < SECTOR_SIZE && !isUrgent) {
       // Buffer until sector-aligned
   }
   ```

### Medium-Term Optimizations

1. **Multi-Block Writes**
   ```c
   // Use SD card multi-block commands
   SYS_FS_FileWrite_MultiBlock(handle, buffer, numBlocks);
   ```

2. **Double Buffering**
   ```c
   // Write to one buffer while SD writes the other
   uint8_t buffer1[BUFFER_SIZE];
   uint8_t buffer2[BUFFER_SIZE];
   uint8_t* activeBuffer = buffer1;
   uint8_t* sdBuffer = buffer2;
   ```

3. **Dedicated SD Task Priority**
   ```c
   // Higher priority for SD writes
   #define SD_TASK_PRIORITY 4  // Above other app tasks
   ```

### Long-Term Optimizations

1. **Native SD Mode** (if hardware supports)
   - 4-bit SDIO interface
   - 50 MHz clock possible
   - 25 MB/s theoretical throughput

2. **DMA Chain for Streaming**
   - Configure DMA to stream directly to SD
   - Bypass CPU for data movement

3. **Custom FAT Implementation**
   - Optimized for streaming writes
   - Pre-allocated file clusters
   - Minimal metadata updates

## Testing Methodology

### 1. Baseline Performance Test
```bash
# SCPI commands for baseline test
STOR:SD:ENAB 1
STOR:SD:LOG "benchmark.dat"
# Stream data for 60 seconds
# Measure file size and calculate throughput
```

### 2. Variable Block Size Test
Test different write sizes to find optimal block size:
- 512 bytes (1 sector)
- 4KB (cluster size)
- 32KB (multiple clusters)
- Full buffer (5KB current)

### 3. Flush Impact Test
Measure performance with different flush policies:
- Every write
- Every 1KB
- Every 4KB
- Every 32KB
- Never (until close)

### 4. Concurrent Operation Test
Measure SD performance while:
- WiFi active
- USB streaming
- ADC sampling at max rate

## Current State Summary

**Implemented:**
- ✓ Basic SD card write functionality
- ✓ Circular buffer with mutex protection
- ✓ FAT filesystem support
- ✓ SCPI commands for enable/disable/logging

**Not Implemented:**
- ✗ Performance benchmarking
- ✗ Write speed measurement
- ✗ Performance statistics
- ✗ Optimization for streaming
- ✗ Multi-block operations

**Performance Limiting Factors:**
1. 700-byte streaming buffer (PRIMARY)
2. 5-second flush interval
3. No write combining
4. Single-sector operations
5. Shared SPI bus contention

**Recommended Priority:**
1. Implement SD benchmark command
2. Fix 700-byte buffer limitation
3. Optimize flush policy
4. Add performance statistics
5. Implement multi-block writes

The current implementation provides functional SD card storage but operates well below the hardware's capability. With the recommended optimizations, performance could improve from 2-5 MB/s to 15-25 MB/s, limited primarily by the SPI interface speed.