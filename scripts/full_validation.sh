#!/bin/bash
# Master validation script for Cat Annihilation
# Runs all validation checks and generates a comprehensive report

set -e  # Exit on error (but we'll handle errors ourselves)

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Script directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

# Log file
LOG_FILE="$PROJECT_ROOT/validation_log.txt"
REPORT_FILE="$PROJECT_ROOT/validation_report.txt"

# Initialize report
echo "CAT ANNIHILATION - BUILD VALIDATION REPORT" > "$REPORT_FILE"
echo "Generated: $(date)" >> "$REPORT_FILE"
echo "========================================" >> "$REPORT_FILE"
echo "" >> "$REPORT_FILE"

# Track overall status
OVERALL_STATUS=0

# Function to print section header
print_section() {
    echo ""
    echo -e "${BLUE}========================================${NC}"
    echo -e "${BLUE}$1${NC}"
    echo -e "${BLUE}========================================${NC}"
    echo ""

    echo "" >> "$REPORT_FILE"
    echo "========================================" >> "$REPORT_FILE"
    echo "$1" >> "$REPORT_FILE"
    echo "========================================" >> "$REPORT_FILE"
    echo "" >> "$REPORT_FILE"
}

# Function to run a validation step
run_validation() {
    local name="$1"
    local command="$2"
    local required="$3"  # "required" or "optional"

    echo -e "${YELLOW}Running: $name${NC}"
    echo "Running: $name" >> "$REPORT_FILE"

    # Run command and capture output
    if eval "$command" >> "$LOG_FILE" 2>&1; then
        echo -e "${GREEN}✓ PASSED${NC}"
        echo "✓ PASSED" >> "$REPORT_FILE"
        return 0
    else
        local exit_code=$?
        if [ "$required" = "required" ]; then
            echo -e "${RED}✗ FAILED (required)${NC}"
            echo "✗ FAILED (required)" >> "$REPORT_FILE"
            OVERALL_STATUS=1
        else
            echo -e "${YELLOW}⚠ FAILED (optional)${NC}"
            echo "⚠ FAILED (optional)" >> "$REPORT_FILE"
        fi
        return $exit_code
    fi
}

# Start validation
clear
echo -e "${BLUE}"
cat << "EOF"
   ____      _       _                   _ _     _ _       _   _
  / ___|__ _| |_    / \   _ __  _ __ (_)| |__ (_) | __ _| |_(_) ___  _ __
 | |   / _` | __|  / _ \ | '_ \| '_ \| || '_ \| | |/ _` | __| |/ _ \| '_ \
 | |__| (_| | |_  / ___ \| | | | | | | || | | | | | (_| | |_| | (_) | | | |
  \____\__,_|\__|/_/   \_\_| |_|_| |_|_||_| |_|_|_|\__,_|\__|_|\___/|_| |_|

             BUILD VALIDATION SYSTEM
EOF
echo -e "${NC}"

print_section "STEP 1: JSON VALIDATION"
run_validation "Validating JSON files" "python3 '$SCRIPT_DIR/validate_json.py'" "required"

print_section "STEP 2: INCLUDE STRUCTURE CHECK"
run_validation "Checking include structure" "python3 '$SCRIPT_DIR/check_includes.py'" "optional"

print_section "STEP 3: SHADER VALIDATION"
run_validation "Validating GLSL shaders" "python3 '$SCRIPT_DIR/validate_shaders.py'" "optional"

print_section "STEP 4: COMPILATION CHECK"
echo -e "${YELLOW}Checking C++ compilation...${NC}"
echo "Checking C++ compilation..." >> "$REPORT_FILE"

# Check if we can use the Makefile approach or Python script
if command -v make &> /dev/null; then
    echo "Using Makefile for compilation check..."
    if make -f "$PROJECT_ROOT/Makefile.check" clean all >> "$LOG_FILE" 2>&1; then
        echo -e "${GREEN}✓ PASSED${NC}"
        echo "✓ PASSED" >> "$REPORT_FILE"
    else
        echo -e "${YELLOW}Makefile approach failed, trying Python script...${NC}"
        run_validation "C++ compilation (Python)" "python3 '$SCRIPT_DIR/check_compilation.py'" "optional"
    fi
else
    echo "make not found, using Python script..."
    run_validation "C++ compilation (Python)" "python3 '$SCRIPT_DIR/check_compilation.py'" "optional"
fi

print_section "STEP 5: UNIT TESTS"
# Check if there are any unit tests to run
if [ -d "$PROJECT_ROOT/tests" ] || [ -f "$PROJECT_ROOT/package.json" ]; then
    if [ -f "$PROJECT_ROOT/package.json" ] && grep -q '"test"' "$PROJECT_ROOT/package.json"; then
        run_validation "Running npm tests" "cd '$PROJECT_ROOT' && npm test" "optional"
    else
        echo -e "${YELLOW}⚠ No unit tests found${NC}"
        echo "⚠ No unit tests found" >> "$REPORT_FILE"
    fi
else
    echo -e "${YELLOW}⚠ No test directory found${NC}"
    echo "⚠ No test directory found" >> "$REPORT_FILE"
fi

# Generate final summary
print_section "VALIDATION SUMMARY"

echo "Validation completed at: $(date)" >> "$REPORT_FILE"
echo "" >> "$REPORT_FILE"

if [ $OVERALL_STATUS -eq 0 ]; then
    echo -e "${GREEN}"
    echo "╔═══════════════════════════════════════════════════════════════╗"
    echo "║                                                               ║"
    echo "║             ✓ ALL REQUIRED VALIDATIONS PASSED!                ║"
    echo "║                                                               ║"
    echo "╚═══════════════════════════════════════════════════════════════╝"
    echo -e "${NC}"

    echo "✓ ALL REQUIRED VALIDATIONS PASSED!" >> "$REPORT_FILE"
else
    echo -e "${RED}"
    echo "╔═══════════════════════════════════════════════════════════════╗"
    echo "║                                                               ║"
    echo "║             ✗ SOME VALIDATIONS FAILED                         ║"
    echo "║                                                               ║"
    echo "╚═══════════════════════════════════════════════════════════════╝"
    echo -e "${NC}"

    echo "✗ SOME VALIDATIONS FAILED" >> "$REPORT_FILE"
fi

echo ""
echo -e "${BLUE}Reports generated:${NC}"
echo "  - Summary: $REPORT_FILE"
echo "  - Detailed log: $LOG_FILE"
echo ""

# Display the report
echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}QUICK SUMMARY${NC}"
echo -e "${BLUE}========================================${NC}"
cat "$REPORT_FILE"

exit $OVERALL_STATUS
