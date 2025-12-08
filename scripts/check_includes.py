#!/usr/bin/env python3
"""
Include structure checker for Cat Annihilation
Verifies all #includes resolve to existing files and checks for circular dependencies
"""

import os
import sys
import re
from pathlib import Path
from typing import List, Set, Dict, Tuple
from collections import defaultdict

PROJECT_ROOT = Path(__file__).parent.parent.resolve()

def find_cpp_files() -> List[Path]:
    """Find all C++ source and header files"""
    files = []
    for directory in ["engine", "game"]:
        dir_path = PROJECT_ROOT / directory
        if dir_path.exists():
            files.extend(dir_path.rglob("*.cpp"))
            files.extend(dir_path.rglob("*.hpp"))
            files.extend(dir_path.rglob("*.h"))
    return sorted(files)

# Standard library and system headers that should be ignored
STD_HEADERS = {
    # C++ standard library
    "algorithm", "any", "array", "atomic", "bitset", "cassert", "cctype", "cerrno",
    "cfloat", "chrono", "climits", "cmath", "complex", "condition_variable", "cstdarg",
    "cstddef", "cstdint", "cstdio", "cstdlib", "cstring", "ctime", "deque", "exception",
    "filesystem", "fstream", "functional", "future", "iomanip", "ios", "iosfwd",
    "iostream", "istream", "iterator", "limits", "list", "locale", "map", "memory",
    "mutex", "new", "numeric", "optional", "ostream", "queue", "random", "ratio",
    "regex", "set", "sstream", "stack", "stdexcept", "streambuf", "string", "string_view",
    "thread", "tuple", "type_traits", "typeindex", "typeinfo", "unordered_map",
    "unordered_set", "utility", "valarray", "variant", "vector",
    # C headers
    "assert.h", "ctype.h", "errno.h", "float.h", "limits.h", "locale.h", "math.h",
    "setjmp.h", "signal.h", "stdarg.h", "stddef.h", "stdio.h", "stdlib.h", "string.h",
    "time.h",
}

def is_system_include(include_path: str) -> bool:
    """Check if include is a system/standard library header"""
    # Get just the filename
    name = Path(include_path).name
    if name in STD_HEADERS:
        return True
    # Check for common system paths
    if include_path.startswith(("vulkan/", "cuda", "GL/", "GLFW/", "AL/", "glm/")):
        return True
    return False

def extract_includes(file_path: Path) -> List[Tuple[str, int, bool]]:
    """
    Extract all #include directives from a file
    Returns list of (include_path, line_number, is_angle_bracket)
    """
    includes = []
    try:
        with open(file_path, 'r', encoding='utf-8', errors='ignore') as f:
            for line_num, line in enumerate(f, 1):
                # Match #include "..." (project includes)
                match_quotes = re.match(r'^\s*#include\s+"([^"]+)"', line)
                if match_quotes:
                    includes.append((match_quotes.group(1), line_num, False))
                    continue
                # Match #include <...> (system includes)
                match_angle = re.match(r'^\s*#include\s+<([^>]+)>', line)
                if match_angle:
                    includes.append((match_angle.group(1), line_num, True))
    except Exception as e:
        print(f"Error reading {file_path}: {e}")
    return includes

def resolve_include(include_path: str, source_file: Path) -> Path:
    """
    Resolve an include path to an actual file
    Tries multiple search strategies:
    1. Relative to source file
    2. Relative to project root
    3. In engine/ directory
    4. In game/ directory
    """
    # Try relative to source file
    relative_path = source_file.parent / include_path
    if relative_path.exists():
        return relative_path.resolve()

    # Try relative to project root
    root_path = PROJECT_ROOT / include_path
    if root_path.exists():
        return root_path.resolve()

    # Try in engine/
    engine_path = PROJECT_ROOT / "engine" / include_path
    if engine_path.exists():
        return engine_path.resolve()

    # Try in game/
    game_path = PROJECT_ROOT / "game" / include_path
    if game_path.exists():
        return game_path.resolve()

    return None

def has_include_guard(file_path: Path) -> Tuple[bool, str]:
    """
    Check if a header file has include guards
    Returns (has_guard, guard_name)
    """
    if file_path.suffix not in ['.h', '.hpp']:
        return True, "N/A (not a header)"

    try:
        with open(file_path, 'r', encoding='utf-8', errors='ignore') as f:
            content = f.read()

            # Check for #pragma once
            if re.search(r'^\s*#pragma\s+once', content, re.MULTILINE):
                return True, "#pragma once"

            # Check for traditional include guards
            ifndef_match = re.search(r'^\s*#ifndef\s+(\w+)', content, re.MULTILINE)
            if ifndef_match:
                guard_name = ifndef_match.group(1)
                # Check for corresponding #define
                if re.search(rf'^\s*#define\s+{guard_name}', content, re.MULTILINE):
                    return True, guard_name

            return False, "No guard found"
    except Exception as e:
        return False, f"Error: {e}"

def detect_circular_deps(file_graph: Dict[Path, Set[Path]]) -> List[List[Path]]:
    """
    Detect circular dependencies using DFS
    Returns list of cycles found
    """
    cycles = []
    visited = set()
    rec_stack = set()

    def dfs(node: Path, path: List[Path]):
        visited.add(node)
        rec_stack.add(node)
        path.append(node)

        for neighbor in file_graph.get(node, set()):
            if neighbor not in visited:
                dfs(neighbor, path.copy())
            elif neighbor in rec_stack:
                # Found a cycle
                cycle_start = path.index(neighbor)
                cycle = path[cycle_start:] + [neighbor]
                cycles.append(cycle)

        rec_stack.remove(node)

    for node in file_graph:
        if node not in visited:
            dfs(node, [])

    return cycles

def main():
    print("=" * 80)
    print("CAT ANNIHILATION - INCLUDE STRUCTURE CHECK")
    print("=" * 80)
    print()

    files = find_cpp_files()
    print(f"Analyzing {len(files)} files...")
    print()

    # Track issues
    missing_includes = []
    missing_guards = []
    file_graph = defaultdict(set)

    # Check each file
    for file_path in files:
        rel_path = file_path.relative_to(PROJECT_ROOT)
        includes = extract_includes(file_path)

        # Check if includes resolve
        for include_path, line_num, is_angle in includes:
            # Skip system/standard library includes
            if is_angle or is_system_include(include_path):
                continue

            resolved = resolve_include(include_path, file_path)
            if resolved:
                # Add to dependency graph
                file_graph[file_path].add(resolved)
            else:
                missing_includes.append({
                    "file": str(rel_path),
                    "line": line_num,
                    "include": include_path
                })

        # Check include guards for headers
        if file_path.suffix in ['.h', '.hpp']:
            has_guard, guard_info = has_include_guard(file_path)
            if not has_guard:
                missing_guards.append({
                    "file": str(rel_path),
                    "info": guard_info
                })

    # Check for circular dependencies
    print("Checking for circular dependencies...")
    cycles = detect_circular_deps(file_graph)

    # Print results
    print()
    print("=" * 80)
    print("RESULTS")
    print("=" * 80)
    print()

    # Missing includes
    if missing_includes:
        print(f"⚠ MISSING INCLUDES: {len(missing_includes)}")
        for item in missing_includes[:10]:  # Show first 10
            print(f"  {item['file']}:{item['line']} - Cannot resolve: {item['include']}")
        if len(missing_includes) > 10:
            print(f"  ... and {len(missing_includes) - 10} more")
        print()
    else:
        print("✓ All includes resolve correctly")
        print()

    # Missing include guards
    if missing_guards:
        print(f"⚠ MISSING INCLUDE GUARDS: {len(missing_guards)}")
        for item in missing_guards[:10]:
            print(f"  {item['file']} - {item['info']}")
        if len(missing_guards) > 10:
            print(f"  ... and {len(missing_guards) - 10} more")
        print()
    else:
        print("✓ All headers have include guards")
        print()

    # Circular dependencies
    if cycles:
        print(f"⚠ CIRCULAR DEPENDENCIES: {len(cycles)}")
        for i, cycle in enumerate(cycles[:5], 1):
            print(f"  Cycle {i}:")
            for file in cycle:
                print(f"    → {file.relative_to(PROJECT_ROOT)}")
        if len(cycles) > 5:
            print(f"  ... and {len(cycles) - 5} more cycles")
        print()
    else:
        print("✓ No circular dependencies detected")
        print()

    # Summary
    print("=" * 80)
    print("SUMMARY")
    print("=" * 80)
    total_issues = len(missing_includes) + len(missing_guards) + len(cycles)
    print(f"Total issues found: {total_issues}")
    print()

    if total_issues == 0:
        print("SUCCESS: Include structure is clean!")
        return 0
    else:
        print("WARNING: Issues found in include structure")
        return 1

if __name__ == "__main__":
    sys.exit(main())
