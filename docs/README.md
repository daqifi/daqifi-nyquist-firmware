# DAQiFi Nyquist Documentation

This directory contains documentation for the DAQiFi Nyquist firmware project.

## Python API and Test Suite

The Python API and test suite have been moved to separate repositories:

### daqifi-python-core
**Python API for DAQiFi Nyquist devices**
- Private repository: https://github.com/daqifi/daqifi-python-core
- Complete Python API for controlling NQ1, NQ2, and NQ3 devices
- Project-agnostic library for general use
- Installation: `pip install git+https://github.com/daqifi/daqifi-python-core.git`

### daqifi-python-test-suite
**Comprehensive firmware test suite**
- Private repository: https://github.com/daqifi/daqifi-python-test-suite
- YAML-configured test suite using daqifi-python-core
- Tests USB CDC, WiFi, streaming (JSON/CSV/ProtoBuf), and more
- Installation: Clone repo and follow README instructions

## Firmware Documentation

For firmware-specific documentation, see the main repository files:
- `CLAUDE.md` - Development guide for working with the firmware
- `FIRMWARE_ANALYSIS_REPORT.md` - Technical analysis and optimization roadmap
- `README.md` - Project overview

## Contributing

For Python API contributions, see the daqifi-python-core repository.
For test suite improvements, see the daqifi-python-test-suite repository.
For firmware contributions, follow the guidelines in CLAUDE.md.
