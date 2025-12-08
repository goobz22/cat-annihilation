#!/usr/bin/env python3
"""
Shader validation for Cat Annihilation GLSL shaders
Checks shader syntax and validates uniform/attribute consistency
"""

import os
import sys
import subprocess
import re
from pathlib import Path
from typing import List, Dict, Set, Tuple
from collections import defaultdict

PROJECT_ROOT = Path(__file__).parent.parent.resolve()

def find_shader_files() -> List[Path]:
    """Find all GLSL shader files"""
    files = []
    shader_extensions = [".glsl", ".vert", ".frag", ".comp", ".geom", ".tesc", ".tese"]

    for directory in ["shaders", "game/shaders"]:
        dir_path = PROJECT_ROOT / directory
        if dir_path.exists():
            for ext in shader_extensions:
                files.extend(dir_path.rglob(f"*{ext}"))

    return sorted(files)

def check_glslang_validator() -> bool:
    """Check if glslangValidator is available"""
    try:
        result = subprocess.run(
            ["glslangValidator", "--version"],
            capture_output=True,
            timeout=5
        )
        return result.returncode == 0
    except (FileNotFoundError, subprocess.TimeoutExpired):
        return False

def validate_with_glslang(file_path: Path) -> Tuple[bool, str]:
    """Validate shader using glslangValidator"""
    try:
        result = subprocess.run(
            ["glslangValidator", str(file_path)],
            capture_output=True,
            text=True,
            timeout=10
        )
        if result.returncode == 0:
            return True, ""
        else:
            return False, result.stdout + result.stderr
    except Exception as e:
        return False, f"Error: {str(e)}"

def extract_uniforms(file_path: Path) -> Set[str]:
    """Extract uniform variable names from shader"""
    uniforms = set()
    try:
        with open(file_path, 'r', encoding='utf-8', errors='ignore') as f:
            content = f.read()
            # Match uniform declarations
            pattern = r'uniform\s+\w+\s+(\w+)(?:\s*\[|;)'
            matches = re.finditer(pattern, content)
            for match in matches:
                uniforms.add(match.group(1))
    except Exception as e:
        print(f"Error reading {file_path}: {e}")
    return uniforms

def extract_attributes(file_path: Path) -> Set[str]:
    """Extract attribute/in variable names from shader"""
    attributes = set()
    try:
        with open(file_path, 'r', encoding='utf-8', errors='ignore') as f:
            content = f.read()
            # Match attribute or in declarations (vertex shader inputs)
            patterns = [
                r'(?:attribute|in)\s+\w+\s+(\w+)\s*;',
                r'layout\s*\([^)]*\)\s*in\s+\w+\s+(\w+)\s*;'
            ]
            for pattern in patterns:
                matches = re.finditer(pattern, content)
                for match in matches:
                    attributes.add(match.group(1))
    except Exception as e:
        print(f"Error reading {file_path}: {e}")
    return attributes

def basic_syntax_check(file_path: Path) -> List[str]:
    """Perform basic shader syntax checks - only catches definite errors"""
    errors = []

    # Be less strict with include files
    is_include = is_include_file(file_path)

    try:
        with open(file_path, 'r', encoding='utf-8', errors='ignore') as f:
            content = f.read()
            lines = content.split('\n')

        # Check for definite errors only
        for i, line in enumerate(lines, 1):
            stripped = line.strip()

            # Skip comments and empty lines
            if not stripped or stripped.startswith('//') or stripped.startswith('/*'):
                continue

            # Check for invalid GLSL keywords usage
            if re.search(r'\bNULL\b', stripped):
                errors.append(f"Line {i}: 'NULL' is not valid in GLSL, use 0 or false")

            # Check for common typos
            if re.search(r'\bvoid\s+main\s*\(\s*void\s*\)', stripped):
                pass  # This is actually valid in GLSL

        # Check for unbalanced braces in the whole file (only for non-include files)
        if not is_include:
            open_braces = content.count('{')
            close_braces = content.count('}')
            if open_braces != close_braces:
                errors.append(f"Unbalanced braces: {open_braces} '{{' vs {close_braces} '}}'")

    except Exception as e:
        errors.append(f"Error reading file: {str(e)}")

    return errors

def is_include_file(file_path: Path) -> bool:
    """Check if this is a shader include file (not a standalone shader)"""
    # .glsl files in common/, lighting/, shadows/ are typically includes
    include_dirs = ["common", "lighting", "shadows"]
    if file_path.suffix == ".glsl":
        for inc_dir in include_dirs:
            if inc_dir in file_path.parts:
                return True
    return False

def check_version_directive(file_path: Path) -> Tuple[bool, str]:
    """Check if shader has a #version directive"""
    # Include files don't need #version
    if is_include_file(file_path):
        return True, "Include file (no #version required)"

    try:
        with open(file_path, 'r', encoding='utf-8', errors='ignore') as f:
            lines = f.readlines()
            # Check first 10 lines for #version
            for line in lines[:10]:
                if re.match(r'^\s*#version', line):
                    return True, line.strip()
            return False, "No #version directive found"
    except Exception as e:
        return False, f"Error: {str(e)}"

def main():
    print("=" * 80)
    print("CAT ANNIHILATION - SHADER VALIDATION")
    print("=" * 80)
    print()

    files = find_shader_files()
    print(f"Found {len(files)} shader files")

    # Check for glslangValidator
    has_validator = check_glslang_validator()
    if has_validator:
        print("✓ glslangValidator found - will perform full validation")
    else:
        print("⚠ glslangValidator not found - performing basic validation only")
        print("  Install glslang for full validation: apt-get install glslang-tools")
    print()

    # Track results
    results = {
        "passed": [],
        "failed": [],
        "warnings": []
    }

    # Track uniforms across shaders
    all_uniforms = defaultdict(set)

    # Validate each shader
    for file_path in files:
        rel_path = file_path.relative_to(PROJECT_ROOT)
        print(f"Validating {rel_path}...", end=" ")

        file_errors = []

        # Check version directive
        has_version, version_info = check_version_directive(file_path)
        if not has_version:
            file_errors.append(version_info)

        # Perform validation
        if has_validator:
            success, error = validate_with_glslang(file_path)
            if not success:
                file_errors.append(error)
        else:
            # Basic syntax check
            syntax_errors = basic_syntax_check(file_path)
            if syntax_errors:
                file_errors.extend(syntax_errors)

        # Extract uniforms
        uniforms = extract_uniforms(file_path)
        for uniform in uniforms:
            all_uniforms[uniform].add(str(rel_path))

        # Determine result
        if file_errors:
            print("✗ FAIL")
            results["failed"].append((str(rel_path), file_errors))
            # Print first few errors
            for error in file_errors[:3]:
                for line in str(error).split('\n')[:3]:
                    if line.strip():
                        print(f"    {line}")
        else:
            print("✓ PASS")
            results["passed"].append(str(rel_path))

    # Check for uniform name consistency
    print()
    print("Checking uniform consistency...")

    # Look for similarly named uniforms that might be typos
    uniform_list = list(all_uniforms.keys())
    for i, uniform in enumerate(uniform_list):
        for other_uniform in uniform_list[i+1:]:
            # Check if names are very similar (might be typo)
            if len(uniform) > 5 and len(other_uniform) > 5:
                # Simple similarity check
                common_prefix_len = 0
                for c1, c2 in zip(uniform, other_uniform):
                    if c1 == c2:
                        common_prefix_len += 1
                    else:
                        break

                # If they share >80% of the shorter name, might be suspicious
                min_len = min(len(uniform), len(other_uniform))
                if common_prefix_len / min_len > 0.8 and common_prefix_len > 5:
                    results["warnings"].append(
                        f"Similar uniform names: '{uniform}' and '{other_uniform}' - possible typo?"
                    )

    # Print results
    print()
    print("=" * 80)
    print("RESULTS")
    print("=" * 80)
    print()

    if results["failed"]:
        print(f"FAILED SHADERS: {len(results['failed'])}")
        for file, errors in results["failed"][:10]:
            print(f"  ✗ {file}")
            for error in errors[:2]:
                print(f"      {str(error)[:100]}")
        if len(results["failed"]) > 10:
            print(f"  ... and {len(results['failed']) - 10} more")
        print()

    if results["warnings"]:
        print(f"WARNINGS: {len(results['warnings'])}")
        for warning in results["warnings"][:10]:
            print(f"  ⚠ {warning}")
        if len(results["warnings"]) > 10:
            print(f"  ... and {len(results['warnings']) - 10} more")
        print()

    # Summary
    print("=" * 80)
    print("SUMMARY")
    print("=" * 80)
    print(f"Total shaders:     {len(files)}")
    print(f"Passed:            {len(results['passed'])}")
    print(f"Failed:            {len(results['failed'])}")
    print(f"Warnings:          {len(results['warnings'])}")
    print()

    # Show most common uniforms
    if all_uniforms:
        print("Most common uniforms:")
        sorted_uniforms = sorted(all_uniforms.items(), key=lambda x: len(x[1]), reverse=True)
        for uniform, files in sorted_uniforms[:10]:
            print(f"  {uniform:30} used in {len(files)} shader(s)")
        print()

    if results["failed"]:
        print("FAILED: Shader validation found errors")
        return 1
    elif results["warnings"]:
        print("PASSED: Shader validation passed with warnings")
        return 0
    else:
        print("SUCCESS: All shaders validated successfully!")
        return 0

if __name__ == "__main__":
    sys.exit(main())
