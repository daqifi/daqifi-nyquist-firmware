@echo off
REM Build script for DAQiFi Nyquist firmware using MPLAB X command line tools
REM
REM Prerequisites:
REM - MPLAB X IDE installed
REM - XC32 compiler installed  
REM - MPLAB X bin directory in PATH or MPLABX_PATH set
REM
REM Usage:
REM   build.bat                    - Build default configuration
REM   build.bat clean              - Clean build
REM   build.bat test               - Build and check for errors

setlocal enabledelayedexpansion

REM Configuration
set PROJECT_DIR=%~dp0..\firmware\daqifi.X
set PROJECT_NAME=daqifi
if not defined CONFIG set CONFIG=default

REM Try to find MPLAB X installation if not set
if not defined MPLABX_PATH (
    REM Check common installation paths
    for %%P in (
        "C:\Program Files\Microchip\MPLABX\v6.20"
        "C:\Program Files\Microchip\MPLABX\v6.15"
        "C:\Program Files\Microchip\MPLABX\v6.10"
        "C:\Program Files\Microchip\MPLABX\v6.05"
        "C:\Program Files\Microchip\MPLABX\v6.00"
        "C:\Program Files (x86)\Microchip\MPLABX\v6.20"
        "C:\Program Files (x86)\Microchip\MPLABX\v6.15"
        "C:\Program Files (x86)\Microchip\MPLABX\v6.10"
        "C:\Program Files (x86)\Microchip\MPLABX\v6.05"
        "C:\Program Files (x86)\Microchip\MPLABX\v6.00"
    ) do (
        if exist "%%~P\bin\prjMakefilesGenerator.bat" (
            set MPLABX_PATH=%%~P
            goto :found_mplab
        )
    )
    
    echo Warning: MPLAB X installation not found. Set MPLABX_PATH environment variable.
    echo Attempting to build with existing makefiles...
)
:found_mplab

REM Parse command
if "%1"=="" goto :build
if /i "%1"=="build" goto :build
if /i "%1"=="clean" goto :clean
if /i "%1"=="test" goto :test
goto :usage

:build
echo Building project: %PROJECT_NAME%
echo Configuration: %CONFIG%
echo Project directory: %PROJECT_DIR%

cd /d "%PROJECT_DIR%"

REM Generate makefiles if MPLAB X found
if defined MPLABX_PATH (
    if exist "%MPLABX_PATH%\bin\prjMakefilesGenerator.bat" (
        echo Generating makefiles...
        call "%MPLABX_PATH%\bin\prjMakefilesGenerator.bat" -v "%PROJECT_DIR%"
    )
)

REM Build using make
echo Building...
make -f nbproject/Makefile-%CONFIG%.mk SUBPROJECTS= .build-conf

if errorlevel 1 (
    echo Build failed!
    exit /b 1
)

echo Build successful!

REM Show memory usage if available
if exist "dist\%CONFIG%\debug\memoryfile.xml" (
    echo.
    echo Memory usage:
    findstr /C:"used" /C:"free" "dist\%CONFIG%\debug\memoryfile.xml"
)

exit /b 0

:clean
echo Cleaning project: %PROJECT_NAME%
cd /d "%PROJECT_DIR%"
make -f nbproject/Makefile-%CONFIG%.mk SUBPROJECTS= .clean-conf
echo Clean complete!
exit /b 0

:test
echo Testing build...
cd /d "%PROJECT_DIR%"

REM Build and capture output
make -f nbproject/Makefile-%CONFIG%.mk SUBPROJECTS= .build-conf > build_output.tmp 2>&1
set BUILD_RESULT=%errorlevel%

REM Display output
type build_output.tmp

if %BUILD_RESULT% equ 0 (
    echo.
    echo Build completed successfully
    
    REM Count warnings
    findstr /C:"warning:" build_output.tmp > nul
    if not errorlevel 1 (
        for /f %%A in ('findstr /C:"warning:" build_output.tmp ^| find /c /v ""') do set WARNING_COUNT=%%A
        echo Found !WARNING_COUNT! warnings
    )
) else (
    echo.
    echo Build failed with errors
    
    REM Show error summary
    echo.
    echo Error summary:
    findstr /C:"error:" /C:"Error:" build_output.tmp | findstr /n "^" | findstr "^[1-9]:"
)

REM Cleanup
del build_output.tmp

exit /b %BUILD_RESULT%

:usage
echo Usage: %0 [build^|clean^|test]
echo   build - Build the project (default)
echo   clean - Clean build artifacts
echo   test  - Build and check for errors
exit /b 1