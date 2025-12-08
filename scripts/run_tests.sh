#!/bin/bash

# Cat Annihilation Test Runner
# Builds and runs all unit and integration tests

set -e

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$PROJECT_ROOT"

echo "======================================"
echo "Cat Annihilation Test Runner"
echo "======================================"
echo ""

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Configuration
BUILD_DIR="build_tests"
BUILD_TYPE="${BUILD_TYPE:-Debug}"
VERBOSE="${VERBOSE:-0}"

# Parse command line arguments
SKIP_BUILD=0
ONLY_UNIT=0
ONLY_INTEGRATION=0

while [[ $# -gt 0 ]]; do
    case $1 in
        --skip-build)
            SKIP_BUILD=1
            shift
            ;;
        --unit)
            ONLY_UNIT=1
            shift
            ;;
        --integration)
            ONLY_INTEGRATION=1
            shift
            ;;
        --verbose|-v)
            VERBOSE=1
            shift
            ;;
        --help|-h)
            echo "Usage: $0 [options]"
            echo ""
            echo "Options:"
            echo "  --skip-build        Skip the build step"
            echo "  --unit              Run only unit tests"
            echo "  --integration       Run only integration tests"
            echo "  --verbose, -v       Verbose output"
            echo "  --help, -h          Show this help message"
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            echo "Use --help for usage information"
            exit 1
            ;;
    esac
done

# Step 1: Build tests
if [ $SKIP_BUILD -eq 0 ]; then
    echo -e "${BLUE}Building tests...${NC}"
    echo ""

    # Create build directory
    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"

    # Configure with CMake
    echo "Configuring CMake..."
    if [ $VERBOSE -eq 1 ]; then
        cmake ../tests -DCMAKE_BUILD_TYPE=$BUILD_TYPE
    else
        cmake ../tests -DCMAKE_BUILD_TYPE=$BUILD_TYPE > /dev/null
    fi

    # Build
    echo "Compiling..."
    if [ $VERBOSE -eq 1 ]; then
        cmake --build . -j$(nproc)
    else
        cmake --build . -j$(nproc) > /dev/null 2>&1
    fi

    if [ $? -eq 0 ]; then
        echo -e "${GREEN}✓ Build successful${NC}"
    else
        echo -e "${RED}✗ Build failed${NC}"
        exit 1
    fi

    cd "$PROJECT_ROOT"
    echo ""
else
    echo -e "${YELLOW}Skipping build step${NC}"
    echo ""
fi

# Step 2: Run tests
TOTAL_TESTS=0
PASSED_TESTS=0
FAILED_TESTS=0

# Run unit tests
if [ $ONLY_INTEGRATION -eq 0 ]; then
    echo -e "${BLUE}Running unit tests...${NC}"
    echo ""

    if [ -f "$BUILD_DIR/unit_tests" ]; then
        if [ $VERBOSE -eq 1 ]; then
            "$BUILD_DIR/unit_tests" -r compact
        else
            "$BUILD_DIR/unit_tests" -r compact --success
        fi

        UNIT_EXIT_CODE=$?
        TOTAL_TESTS=$((TOTAL_TESTS + 1))

        if [ $UNIT_EXIT_CODE -eq 0 ]; then
            echo -e "${GREEN}✓ Unit tests passed${NC}"
            PASSED_TESTS=$((PASSED_TESTS + 1))
        else
            echo -e "${RED}✗ Unit tests failed${NC}"
            FAILED_TESTS=$((FAILED_TESTS + 1))
        fi
    else
        echo -e "${YELLOW}⚠ Unit test executable not found${NC}"
    fi
    echo ""
fi

# Run integration tests
if [ $ONLY_UNIT -eq 0 ]; then
    echo -e "${BLUE}Running integration tests...${NC}"
    echo ""

    if [ -f "$BUILD_DIR/integration_tests" ]; then
        if [ $VERBOSE -eq 1 ]; then
            "$BUILD_DIR/integration_tests" -r compact
        else
            "$BUILD_DIR/integration_tests" -r compact --success
        fi

        INTEGRATION_EXIT_CODE=$?
        TOTAL_TESTS=$((TOTAL_TESTS + 1))

        if [ $INTEGRATION_EXIT_CODE -eq 0 ]; then
            echo -e "${GREEN}✓ Integration tests passed${NC}"
            PASSED_TESTS=$((PASSED_TESTS + 1))
        else
            echo -e "${RED}✗ Integration tests failed${NC}"
            FAILED_TESTS=$((FAILED_TESTS + 1))
        fi
    else
        echo -e "${YELLOW}⚠ Integration test executable not found${NC}"
    fi
    echo ""
fi

# Summary
echo "======================================"
echo "Test Summary"
echo "======================================"
echo "Total test suites: $TOTAL_TESTS"
echo -e "Passed: ${GREEN}$PASSED_TESTS${NC}"
echo -e "Failed: ${RED}$FAILED_TESTS${NC}"
echo ""

if [ $FAILED_TESTS -eq 0 ]; then
    echo -e "${GREEN}All tests passed!${NC}"
    exit 0
else
    echo -e "${RED}Some tests failed.${NC}"
    exit 1
fi
