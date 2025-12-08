#!/bin/bash

# Cat Annihilation Code Validation Script
# Validates C++ code syntax, headers, JSON files, and shaders

set -e

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$PROJECT_ROOT"

echo "======================================"
echo "Cat Annihilation Code Validation"
echo "======================================"
echo ""

TOTAL_CHECKS=0
PASSED_CHECKS=0
FAILED_CHECKS=0

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Function to print status
print_status() {
    if [ $1 -eq 0 ]; then
        echo -e "${GREEN}✓ PASS${NC}: $2"
        PASSED_CHECKS=$((PASSED_CHECKS + 1))
    else
        echo -e "${RED}✗ FAIL${NC}: $2"
        FAILED_CHECKS=$((FAILED_CHECKS + 1))
    fi
    TOTAL_CHECKS=$((TOTAL_CHECKS + 1))
}

# Check 1: Verify all C++ files have valid syntax
echo "Checking C++ syntax..."
CPP_FILES=$(find game engine tests -name "*.cpp" -o -name "*.hpp" 2>/dev/null | grep -v "third_party" || true)
CPP_SYNTAX_ERRORS=0

if command -v g++ &> /dev/null; then
    for file in $CPP_FILES; do
        if [[ "$file" == *.cpp ]] || [[ "$file" == *.hpp ]]; then
            g++ -std=c++20 -fsyntax-only -I. -Iengine -Igame -Ithird_party -DUSE_MOCK_GPU=1 -DTESTING_MODE=1 "$file" 2>/dev/null
            if [ $? -ne 0 ]; then
                echo "  Syntax error in: $file"
                CPP_SYNTAX_ERRORS=$((CPP_SYNTAX_ERRORS + 1))
            fi
        fi
    done
    print_status $CPP_SYNTAX_ERRORS "C++ syntax validation (checked $(echo "$CPP_FILES" | wc -l) files)"
else
    echo -e "${YELLOW}⚠ WARNING${NC}: g++ not found, skipping C++ syntax checks"
fi

# Check 2: Verify all headers have include guards
echo ""
echo "Checking header include guards..."
HEADER_FILES=$(find game engine -name "*.hpp" 2>/dev/null | grep -v "third_party" || true)
MISSING_GUARDS=0

for file in $HEADER_FILES; do
    if ! grep -q "#pragma once" "$file" && ! grep -q "#ifndef" "$file"; then
        echo "  Missing include guard: $file"
        MISSING_GUARDS=$((MISSING_GUARDS + 1))
    fi
done
print_status $MISSING_GUARDS "Header include guards"

# Check 3: Check for common C++ issues
echo ""
echo "Checking for common issues..."
COMMON_ISSUES=0

# Check for missing includes
for file in $CPP_FILES; do
    # Check for std::string usage without <string>
    if grep -q "std::string" "$file" && ! grep -q "#include.*<string>" "$file" && ! grep -q "#include.*\".*string" "$file"; then
        echo "  Possible missing <string> include: $file"
        COMMON_ISSUES=$((COMMON_ISSUES + 1))
    fi

    # Check for std::vector usage without <vector>
    if grep -q "std::vector" "$file" && ! grep -q "#include.*<vector>" "$file" && ! grep -q "#include.*\".*vector" "$file"; then
        echo "  Possible missing <vector> include: $file"
        COMMON_ISSUES=$((COMMON_ISSUES + 1))
    fi
done
print_status $COMMON_ISSUES "Common C++ issues"

# Check 4: Validate JSON files
echo ""
echo "Checking JSON files..."
JSON_FILES=$(find assets game -name "*.json" 2>/dev/null || true)
JSON_ERRORS=0

if command -v python3 &> /dev/null; then
    for file in $JSON_FILES; do
        python3 -c "import json; json.load(open('$file'))" 2>/dev/null
        if [ $? -ne 0 ]; then
            echo "  Invalid JSON: $file"
            JSON_ERRORS=$((JSON_ERRORS + 1))
        fi
    done
    print_status $JSON_ERRORS "JSON file validation (checked $(echo "$JSON_FILES" | wc -l) files)"
else
    echo -e "${YELLOW}⚠ WARNING${NC}: python3 not found, skipping JSON validation"
fi

# Check 5: Validate shader files
echo ""
echo "Checking shader files..."
SHADER_FILES=$(find shaders -name "*.vert" -o -name "*.frag" -o -name "*.comp" -o -name "*.geom" 2>/dev/null || true)
SHADER_ERRORS=0

if command -v glslangValidator &> /dev/null; then
    for file in $SHADER_FILES; do
        glslangValidator "$file" &>/dev/null
        if [ $? -ne 0 ]; then
            echo "  Shader error: $file"
            SHADER_ERRORS=$((SHADER_ERRORS + 1))
        fi
    done
    print_status $SHADER_ERRORS "Shader validation (checked $(echo "$SHADER_FILES" | wc -l) files)"
else
    echo -e "${YELLOW}⚠ WARNING${NC}: glslangValidator not found, skipping shader validation"
fi

# Check 6: Check for CUDA kernel syntax (basic check)
echo ""
echo "Checking CUDA files..."
CUDA_FILES=$(find engine game -name "*.cu" 2>/dev/null || true)
CUDA_ERRORS=0

if [ -n "$CUDA_FILES" ]; then
    for file in $CUDA_FILES; do
        # Basic syntax check - look for __global__ or __device__
        if ! grep -q "__global__\|__device__" "$file"; then
            echo "  No CUDA kernels found: $file"
            CUDA_ERRORS=$((CUDA_ERRORS + 1))
        fi
    done
    print_status $CUDA_ERRORS "CUDA kernel checks (checked $(echo "$CUDA_FILES" | wc -l) files)"
else
    echo "  No CUDA files found"
fi

# Check 7: Check CMakeLists.txt files
echo ""
echo "Checking CMakeLists.txt..."
CMAKE_ERRORS=0

if [ -f "CMakeLists.txt" ]; then
    # Check for common CMake issues
    if ! grep -q "cmake_minimum_required" "CMakeLists.txt"; then
        echo "  Missing cmake_minimum_required in CMakeLists.txt"
        CMAKE_ERRORS=$((CMAKE_ERRORS + 1))
    fi

    if ! grep -q "project(" "CMakeLists.txt"; then
        echo "  Missing project() in CMakeLists.txt"
        CMAKE_ERRORS=$((CMAKE_ERRORS + 1))
    fi
fi

if [ -f "tests/CMakeLists.txt" ]; then
    if ! grep -q "enable_testing" "tests/CMakeLists.txt"; then
        echo "  Missing enable_testing() in tests/CMakeLists.txt"
        CMAKE_ERRORS=$((CMAKE_ERRORS + 1))
    fi
fi

print_status $CMAKE_ERRORS "CMakeLists.txt validation"

# Summary
echo ""
echo "======================================"
echo "Validation Summary"
echo "======================================"
echo "Total checks: $TOTAL_CHECKS"
echo -e "Passed: ${GREEN}$PASSED_CHECKS${NC}"
echo -e "Failed: ${RED}$FAILED_CHECKS${NC}"
echo ""

if [ $FAILED_CHECKS -eq 0 ]; then
    echo -e "${GREEN}All validation checks passed!${NC}"
    exit 0
else
    echo -e "${RED}Some validation checks failed.${NC}"
    echo "Please fix the issues above before committing."
    exit 1
fi
