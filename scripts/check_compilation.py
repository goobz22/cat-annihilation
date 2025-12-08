#!/usr/bin/env python3
"""
Compilation checker for Cat Annihilation C++ files
Checks if each .cpp and .hpp file can be parsed using compiler syntax checking
"""

import os
import sys
import subprocess
import json
from pathlib import Path
from typing import List, Dict, Tuple

# Project root
PROJECT_ROOT = Path(__file__).parent.parent.resolve()
BUILD_STUBS = PROJECT_ROOT / "build_stubs"

# Compiler preference order
COMPILERS = ["g++", "clang++", "c++"]

def find_compiler() -> str:
    """Find available C++ compiler"""
    for compiler in COMPILERS:
        try:
            result = subprocess.run([compiler, "--version"],
                                  capture_output=True,
                                  timeout=5)
            if result.returncode == 0:
                return compiler
        except (FileNotFoundError, subprocess.TimeoutExpired):
            continue
    return None

def find_cpp_files() -> List[Path]:
    """Find all C++ source and header files"""
    files = []

    # Search in engine/ and game/ directories
    for directory in ["engine", "game"]:
        dir_path = PROJECT_ROOT / directory
        if dir_path.exists():
            files.extend(dir_path.rglob("*.cpp"))
            files.extend(dir_path.rglob("*.hpp"))
            files.extend(dir_path.rglob("*.h"))

    return sorted(files)

def check_file_compilation(file_path: Path, compiler: str) -> Tuple[bool, str]:
    """
    Check if a single file compiles (syntax only)
    Returns (success, error_message)
    """
    # Build compiler arguments
    args = [
        compiler,
        "-fsyntax-only",  # Only check syntax, don't generate code
        "-std=c++17",     # C++17 standard
        "-I", str(PROJECT_ROOT),  # Include project root
        "-I", str(PROJECT_ROOT / "engine"),
        "-I", str(PROJECT_ROOT / "game"),
        "-I", str(BUILD_STUBS),  # Include stubs
        # Define stub macros to use our stubs instead of real headers
        "-DVK_NO_PROTOTYPES",  # Don't include Vulkan prototypes
        "-DUSING_STUBS",       # Custom flag to indicate stub mode
        str(file_path)
    ]

    try:
        result = subprocess.run(
            args,
            capture_output=True,
            text=True,
            timeout=30
        )

        if result.returncode == 0:
            return True, ""
        else:
            # Return error output
            error_msg = result.stderr if result.stderr else result.stdout
            return False, error_msg

    except subprocess.TimeoutExpired:
        return False, "Compilation check timeout"
    except Exception as e:
        return False, f"Exception: {str(e)}"

def main():
    print("=" * 80)
    print("CAT ANNIHILATION - COMPILATION CHECK")
    print("=" * 80)
    print()

    # Find compiler
    compiler = find_compiler()
    if not compiler:
        print("ERROR: No C++ compiler found!")
        print("Please install g++ or clang++")
        sys.exit(1)

    print(f"Using compiler: {compiler}")

    # Get compiler version
    version_result = subprocess.run([compiler, "--version"],
                                   capture_output=True,
                                   text=True)
    print(f"Version: {version_result.stdout.split(chr(10))[0]}")
    print()

    # Find all C++ files
    files = find_cpp_files()
    print(f"Found {len(files)} C++ files to check")
    print()

    # Check each file
    results = []
    passed = 0
    failed = 0

    for i, file_path in enumerate(files, 1):
        rel_path = file_path.relative_to(PROJECT_ROOT)
        print(f"[{i}/{len(files)}] Checking {rel_path}...", end=" ")

        success, error = check_file_compilation(file_path, compiler)

        if success:
            print("✓ PASS")
            passed += 1
            results.append({
                "file": str(rel_path),
                "status": "pass",
                "error": ""
            })
        else:
            print("✗ FAIL")
            failed += 1
            results.append({
                "file": str(rel_path),
                "status": "fail",
                "error": error
            })
            # Print first few lines of error
            error_lines = error.split('\n')[:5]
            for line in error_lines:
                if line.strip():
                    print(f"    {line}")

    # Print summary
    print()
    print("=" * 80)
    print("SUMMARY")
    print("=" * 80)
    print(f"Total files:   {len(files)}")
    print(f"Passed:        {passed} ({100*passed//len(files) if files else 0}%)")
    print(f"Failed:        {failed} ({100*failed//len(files) if files else 0}%)")
    print()

    # Save detailed report
    report_path = PROJECT_ROOT / "build_validation_report.json"
    with open(report_path, "w") as f:
        json.dump({
            "compilation_check": {
                "total": len(files),
                "passed": passed,
                "failed": failed,
                "files": results
            }
        }, f, indent=2)

    print(f"Detailed report saved to: {report_path}")
    print()

    # Exit with error if any failures
    if failed > 0:
        print(f"WARNING: {failed} file(s) failed compilation check")
        return 1
    else:
        print("SUCCESS: All files passed compilation check!")
        return 0

if __name__ == "__main__":
    sys.exit(main())
