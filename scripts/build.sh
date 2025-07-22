#!/bin/bash
# Build script for DAQiFi Nyquist firmware using MPLAB X command line tools
#
# Prerequisites:
# - MPLAB X IDE installed
# - XC32 compiler installed
# - prjMakefilesGenerator in PATH or MPLABX_PATH set
#
# Usage:
#   ./build.sh                    # Build default configuration
#   ./build.sh clean              # Clean build
#   ./build.sh test               # Build and check for errors

set -e

# Configuration
PROJECT_DIR="$(dirname "$0")/../firmware/daqifi.X"
PROJECT_NAME="daqifi"
CONFIG="${CONFIG:-default}"

# Try to find MPLAB X installation
if [ -z "$MPLABX_PATH" ]; then
    # Common installation paths
    MPLAB_PATHS=(
        "/opt/microchip/mplabx"
        "/Applications/microchip/mplabx"
        "C:/Program Files/Microchip/MPLABX"
        "C:/Program Files (x86)/Microchip/MPLABX"
        "/mnt/c/Program Files/Microchip/MPLABX"
        "/mnt/c/Program Files (x86)/Microchip/MPLABX"
    )
    
    for path in "${MPLAB_PATHS[@]}"; do
        if [ -d "$path" ]; then
            # Find the latest version
            MPLABX_PATH=$(find "$path" -name "prjMakefilesGenerator*" -type f 2>/dev/null | head -1 | xargs dirname 2>/dev/null || echo "")
            if [ -n "$MPLABX_PATH" ]; then
                MPLABX_PATH=$(dirname "$MPLABX_PATH")
                break
            fi
        fi
    done
fi

# Function to build the project
build_project() {
    echo "Building project: $PROJECT_NAME"
    echo "Configuration: $CONFIG"
    echo "Project directory: $PROJECT_DIR"
    
    cd "$PROJECT_DIR"
    
    # Generate makefiles if needed
    if [ -n "$MPLABX_PATH" ] && [ -f "$MPLABX_PATH/bin/prjMakefilesGenerator" ]; then
        echo "Generating makefiles..."
        "$MPLABX_PATH/bin/prjMakefilesGenerator" -v "$PROJECT_DIR"
    fi
    
    # Build using make
    echo "Building..."
    make -f nbproject/Makefile-${CONFIG}.mk SUBPROJECTS= .build-conf
    
    # Check if build succeeded
    if [ $? -eq 0 ]; then
        echo "Build successful!"
        
        # Show memory usage if available
        if [ -f "dist/${CONFIG}/debug/memoryfile.xml" ]; then
            echo ""
            echo "Memory usage:"
            grep -E "used|free" "dist/${CONFIG}/debug/memoryfile.xml" | sed 's/^[ \t]*/  /'
        fi
        
        return 0
    else
        echo "Build failed!"
        return 1
    fi
}

# Function to clean the project
clean_project() {
    echo "Cleaning project: $PROJECT_NAME"
    cd "$PROJECT_DIR"
    make -f nbproject/Makefile-${CONFIG}.mk SUBPROJECTS= .clean-conf
    echo "Clean complete!"
}

# Function to test build (build and check for common errors)
test_build() {
    echo "Testing build..."
    
    # Redirect output to capture errors
    BUILD_OUTPUT=$(build_project 2>&1)
    BUILD_RESULT=$?
    
    echo "$BUILD_OUTPUT"
    
    if [ $BUILD_RESULT -eq 0 ]; then
        echo ""
        echo "✓ Build completed successfully"
        
        # Check for warnings
        WARNING_COUNT=$(echo "$BUILD_OUTPUT" | grep -c "warning:" || true)
        if [ $WARNING_COUNT -gt 0 ]; then
            echo "⚠ Found $WARNING_COUNT warnings"
        fi
        
        return 0
    else
        echo ""
        echo "✗ Build failed with errors"
        
        # Extract error summary
        echo ""
        echo "Error summary:"
        echo "$BUILD_OUTPUT" | grep -E "error:|Error:" | head -10
        
        return 1
    fi
}

# Main script logic
case "${1:-build}" in
    build)
        build_project
        ;;
    clean)
        clean_project
        ;;
    test)
        test_build
        ;;
    *)
        echo "Usage: $0 [build|clean|test]"
        echo "  build - Build the project (default)"
        echo "  clean - Clean build artifacts"
        echo "  test  - Build and check for errors"
        exit 1
        ;;
esac