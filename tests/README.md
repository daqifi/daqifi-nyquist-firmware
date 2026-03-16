# Firmware Tests

## Python Test Suite

The main Python test scripts live in the **[daqifi-python-test-suite](https://github.com/daqifi/daqifi-python-test-suite)** repository. This includes:

- `verify_test_patterns.py` — 100% data integrity verification for test pattern streaming (patterns 1-6)
- `test_sd_streaming_regression.py` — Long-duration SD streaming regression tests with baseline comparison
- `comprehensive_test.py` — Full YAML-driven device test framework
- SD card analysis and download utilities

### Quick Start

```bash
git clone https://github.com/daqifi/daqifi-python-test-suite.git
cd daqifi-python-test-suite
pip install -r requirements.txt

# Verify all test patterns (requires device connected):
python3 verify_test_patterns.py --run-all

# Run SD streaming regression test:
python3 test_sd_streaming_regression.py --pattern 1
```

### Firmware Compatibility

Test pattern formulas in `verify_test_patterns.py` must match `Streaming_GenerateTestValue()` in `firmware/src/services/streaming.c`. When changing pattern formulas, update both repos.

| Firmware Version | Test Suite Version | Notes |
|-----------------|-------------------|-------|
| feat/test-pattern-streaming | 0.2.0+ | Test patterns 1-6, precision=0 millivolt verification |

## Local Tests

- `test_sd_integrity.py` — SD card write integrity test (firmware-specific benchmark commands)
