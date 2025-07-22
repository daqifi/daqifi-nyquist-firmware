# Command Line Build Tools for DAQiFi Nyquist Firmware

This directory contains scripts to build the DAQiFi Nyquist firmware from the command line, enabling automated builds and continuous integration.

## Prerequisites

1. **MPLAB X IDE** (v6.00 or later)
   - Download from: https://www.microchip.com/mplab/mplab-x-ide
   - Install with default settings

2. **XC32 Compiler** 
   - Download from: https://www.microchip.com/mplab/compilers
   - Install and note the installation path

3. **Environment Setup**
   - Add MPLAB X bin directory to PATH, or
   - Set MPLABX_PATH environment variable to MPLAB X installation directory

## Build Scripts

### Windows: `build.bat`
```cmd
# Build default configuration
build.bat

# Clean build artifacts
build.bat clean

# Build and check for errors
build.bat test
```

### Linux/WSL: `build.sh`
```bash
# Build default configuration
./build.sh

# Clean build artifacts
./build.sh clean

# Build and check for errors
./build.sh test
```

## Direct Make Commands

If you have make installed and the project makefiles are already generated:

```bash
cd firmware/daqifi.X

# Build
make -f nbproject/Makefile-default.mk SUBPROJECTS= .build-conf

# Clean
make -f nbproject/Makefile-default.mk SUBPROJECTS= .clean-conf

# Build specific configuration
make -f nbproject/Makefile-default.mk CONF=debug SUBPROJECTS= .build-conf
```

## MPLAB X Command Line Tools

### prjMakefilesGenerator
Generates/updates makefiles from MPLAB X project:
```bash
prjMakefilesGenerator -v path/to/project.X
```

### IPECMD (Programming)
Program device using PICkit or ICD:
```bash
ipecmd.sh -TPPK4 -P32MZ2048EFM144 -M -F"dist/default/production/daqifi.X.production.hex"
```

### MDB (Microchip Debugger)
Debug and test firmware:
```bash
mdb.sh
# Inside MDB:
device PIC32MZ2048EFM144
set /path/to/firmware.elf
program
run
```

## Automated Testing with Claude

Claude can use these scripts to automatically verify code changes compile correctly:

1. **Quick Compile Check**:
   ```bash
   ./scripts/build.sh test
   ```
   This will build the project and report any compilation errors or warnings.

2. **Memory Usage Check**:
   After a successful build, the script displays memory usage from `memoryfile.xml`:
   - Program memory used/free
   - Data memory used/free

3. **Error Detection**:
   The test mode captures and summarizes build errors for quick diagnosis.

## Continuous Integration

These scripts can be integrated into CI/CD pipelines:

```yaml
# Example GitHub Actions workflow
- name: Build Firmware
  run: |
    cd ${{ github.workspace }}
    ./scripts/build.sh test
```

## Troubleshooting

1. **"make: command not found"**
   - Install make: `apt-get install build-essential` (Linux/WSL)
   - Or use the build scripts which handle this

2. **"MPLAB X not found"**
   - Set MPLABX_PATH environment variable
   - Or install MPLAB X in default location

3. **"XC32 compiler not found"**
   - Ensure XC32 is installed
   - Check project configuration points to correct compiler

4. **Build fails with "missing dependencies"**
   - Run prjMakefilesGenerator to regenerate makefiles
   - Ensure all Harmony 3 components are present

## Benefits for Development

1. **Rapid Verification**: Quickly check if code changes compile without opening IDE
2. **Automated Testing**: Integrate into pre-commit hooks or CI/CD
3. **Remote Development**: Build via SSH or in cloud environments
4. **Batch Operations**: Build multiple configurations automatically
5. **Error Analysis**: Capture and analyze build output programmatically