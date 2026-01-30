# Codebase Concerns

**Analysis Date:** 2026-01-10

## Tech Debt

**SPI Bus Shared Resource Conflict:**
- Issue: WiFi and SD card share SPI0 bus with competing mutex management
- Files: `firmware/src/config/default/tasks.c`, `firmware/src/services/streaming.c`
- Why: Original design didn't anticipate concurrent high-throughput access
- Impact: WiFi+SD simultaneous streaming prohibited; forces single-interface mode
- Fix approach: Implement proper SPI bus arbitration layer with unified mutex

**DAC7718 Calibration Incomplete:**
- Issue: 10+ calibration-related TODOs in SCPI DAC module
- Files: `firmware/src/services/SCPI/SCPIDAC.c` (lines 276-353)
- Why: Initial DAC implementation focused on basic functionality
- Impact: DAC calibration values ignored; output may be inaccurate
- Fix approach: Implement calibration M/B storage in NVM, load on init

**WiFi Driver Deinitialization Bug:**
- Issue: WiFi driver enters bad state after deinit; driver must never be deinitialized
- Files: `firmware/src/services/wifi_services/wifi_manager.c` (lines 802, 826)
- Why: Microchip WINC driver bug - asserts reset but never deasserts
- Impact: WiFi must stay initialized; cannot cleanly power down WiFi module
- Fix approach: Workaround in place (never deinit); needs Microchip driver fix

**JSON/ProtoBuf Encoder Incomplete:**
- Issue: Multiple device telemetry fields marked TODO
- Files: `firmware/src/services/JSON_Encoder.c` (lines 183-288), `firmware/src/services/DaqifiPB/NanoPB_Encoder.c` (lines 484, 524)
- Why: Incremental development prioritized streaming over device status
- Impact: JSON/ProtoBuf output missing: device_status, bat_level, pwr_status, temp_status, analog_out_data
- Fix approach: Implement missing field encoders following existing patterns

## Known Bugs

**Race Condition in Sample Queue:**
- Symptoms: Sample list checked without lock, could change between check and pop
- Trigger: High sample rates with buffer pressure
- Files: `firmware/src/services/streaming.c` (lines 286-287)
- Workaround: Object pool allocation prevents memory corruption
- Root cause: `AInSampleList_IsEmpty()` and pop not atomic

**USB Task Stack Size Limitation:**
- Symptoms: Malloc fails when USB task stack increased to 4096
- Trigger: USB initialization with complex configurations
- Files: `firmware/src/app_freertos.h` (line 80)
- Workaround: Stack limited to 3072 bytes
- Root cause: Heap fragmentation or insufficient total heap

## Security Considerations

**Hardcoded Default WiFi Credentials:**
- Risk: Device ships with open WiFi network ("DAQiFi") and weak PSK ("12345678")
- Files: `firmware/src/services/daqifi_settings.h` (lines 28-35)
- Current mitigation: Users can configure via SCPI before deployment
- Recommendations: Require configuration on first use; add WPA2/WPA3 support

**Unsafe String Operations:**
- Risk: `strcpy()` and `sprintf()` without bounds checking could overflow
- Files: `firmware/src/services/daqifi_settings.c` (lines 63-64), `firmware/src/services/SCPI/SCPIInterface.c` (lines 713-743)
- Current mitigation: Fixed-size buffers sized for expected content
- Recommendations: Replace with `strlcpy()`, `snprintf()` with size limits

**MD5 for Settings Integrity:**
- Risk: MD5 is cryptographically broken; collision attacks possible
- Files: `firmware/src/services/daqifi_settings.h` (line 125)
- Current mitigation: MD5 used only for corruption detection, not security
- Recommendations: If security-critical, upgrade to SHA-256

## Performance Bottlenecks

**WiFi Buffer Size Limitation:**
- Problem: WINC1500 socket buffer limits all streaming to ~1400 bytes
- Files: `firmware/src/services/wifi_services/wifi_tcp_server.h` (lines 23-24)
- Measurement: ~5-10 Mbps current vs potential 50+ Mbps
- Cause: WINC1500 hardware limitation in per-socket buffer
- Improvement path: Implement UDP streaming; optimize packet packing

**Blocking Operations in Task Paths:**
- Problem: `vTaskDelay()` calls can cause sample loss at high rates
- Files: `firmware/src/services/UsbCdc/UsbCdc.c` (lines 251, 422, 501), `firmware/src/services/SCPI/SCPIInterface.c` (lines 288, 1274)
- Measurement: 100ms delays in USB path
- Cause: Legacy polling patterns
- Improvement path: Replace delays with event-driven waits

**Encoding Bottleneck:**
- Problem: Serial encoding in streaming task limits throughput
- Files: `firmware/src/services/streaming.c`
- Measurement: Encoder becomes bottleneck at 10+ kHz sample rates
- Cause: Single-threaded encoding, no parallelization
- Improvement path: Pre-encode common patterns; optimize encoder algorithms

## Fragile Areas

**Streaming Engine Startup:**
- Files: `firmware/src/services/streaming.c` (lines 207-213)
- Why fragile: Stale samples only cleared on next start; leaks if streaming stops
- Common failures: Heap exhaustion after repeated start/stop cycles
- Safe modification: Always pair start/stop; add explicit cleanup command
- Test coverage: No automated tests; hardware verification only

**Task Priority Structure:**
- Files: `firmware/src/config/default/tasks.c`
- Why fragile: Mixed priorities with self-boosting; no clear hierarchy
- Common failures: Priority inversion at high sample rates
- Safe modification: Document all priority changes; test under load
- Test coverage: Manual stress testing only

**Board Variant Configuration:**
- Files: `firmware/src/state/board/NQ1BoardConfig.c`, `NQ2BoardConfig.c`, `NQ3BoardConfig.c`
- Why fragile: Changes in one variant may not propagate to others
- Common failures: Missing channels or features in specific variants
- Safe modification: Update all variant files together; test each variant
- Test coverage: Requires separate builds and hardware for each variant

## Scaling Limits

**FreeRTOS Heap:**
- Current capacity: 284KB total, ~225KB free at steady state
- Limit: Allocation fails if free drops below ~20KB
- Symptoms at limit: Task creation fails, sample allocation fails
- Scaling path: Increase `configTOTAL_HEAP_SIZE`; move more to static allocation

**Sample Queue Depth:**
- Current capacity: 20 samples (MAX_AIN_SAMPLE_COUNT)
- Limit: Older samples dropped when queue full
- Symptoms at limit: Silent data loss, no error indication
- Scaling path: Increase pool size; add drop counter for monitoring

**SD Card File Size:**
- Current capacity: 3.9GB per file (FAT32 limit handling)
- Limit: 9999 split files per session
- Symptoms at limit: File creation fails
- Scaling path: Already implemented file splitting; consider exFAT

## Dependencies at Risk

**WINC1500 WiFi Driver:**
- Risk: Known deinit bug; Microchip driver needs workaround
- Impact: WiFi cannot be cleanly disabled; must work around
- Migration plan: Monitor Microchip driver updates; workaround in place

**FatFS Library:**
- Risk: Elm Chan FatFS is stable but not actively developed
- Impact: No new features; FAT32 4GB limit
- Migration plan: File splitting implemented; exFAT would require license

## Missing Critical Features

**ADC Module Enable/Disable:**
- Problem: Cannot power down AD7173 module
- Files: `firmware/src/HAL/ADC/AD7173.c` (line 209)
- Current workaround: AD7173 always powered when system on
- Blocks: Low-power modes, selective channel power management
- Implementation complexity: Low (add register writes for power control)

**NVM Error Logging:**
- Problem: Flash operation failures not logged
- Files: `firmware/src/HAL/NVM/nvm.c` (lines 51, 89, 138)
- Current workaround: None - failures are silent
- Blocks: Diagnosing settings corruption issues
- Implementation complexity: Low (add LOG_E calls on error paths)

## Test Coverage Gaps

**Main Firmware Unit Tests:**
- What's not tested: All HAL, service, and state management code
- Risk: Regressions go undetected until hardware testing
- Priority: High
- Difficulty to test: Hardware dependencies require stubs or simulation

**Streaming Engine:**
- What's not tested: High-rate streaming, buffer overflow recovery
- Risk: Data loss at edge conditions
- Priority: High
- Difficulty to test: Requires sustained high sample rates

**Power State Transitions:**
- What's not tested: All transitions between STANDBY/POWERED_UP/EXT_DOWN
- Risk: Hang or crash during power transitions
- Priority: Medium
- Difficulty to test: Requires battery and external power manipulation

---

*Concerns audit: 2026-01-10*
*Update as issues are fixed or new ones discovered*
