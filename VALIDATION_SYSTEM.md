# Cat Annihilation - Build Validation System

## Overview

This build validation system provides comprehensive checking of the Cat Annihilation codebase without requiring full compilation or GPU dependencies. It validates code structure, data integrity, and catches common errors early in development.

## Components

### 1. Stub Headers (`build_stubs/`)

Minimal header files that provide type definitions without requiring actual SDKs:

- **vulkan_stubs.h** - Vulkan types and handles
- **cuda_stubs.h** - CUDA types, qualifiers, and built-in variables
- **glfw_stubs.h** - GLFW window and input types
- **openal_stubs.h** - OpenAL audio types

These allow C++ syntax checking without installing Vulkan SDK, CUDA Toolkit, etc.

### 2. Validation Scripts (`scripts/`)

#### check_compilation.py
Validates C++ syntax without linking:
- Uses g++/clang++ with `-fsyntax-only` flag
- Includes stub headers for GPU libraries
- Reports compilation errors per file
- Generates detailed JSON report

**Usage:**
```bash
python3 scripts/check_compilation.py
```

**Output:**
- Console: Progress and summary
- `build_validation_report.json`: Detailed results

#### check_includes.py
Verifies include structure:
- Checks all #includes resolve to existing files
- Detects circular dependencies using DFS
- Verifies include guards (#pragma once or #ifndef)
- Distinguishes between system and local includes

**Usage:**
```bash
python3 scripts/check_includes.py
```

**Detects:**
- Missing header files
- Circular include dependencies
- Headers without guards (potential multiple definition errors)

#### validate_json.py
Validates game data files:
- Parses all JSON in `assets/`
- Validates quest structure (required fields, objectives)
- Validates NPC data (IDs, positions, dialog references)
- Validates dialog trees (node connectivity, dead ends)
- Validates item database (categories, prices, fields)

**Usage:**
```bash
python3 scripts/validate_json.py
```

**Checks:**
- JSON syntax errors
- Missing required fields
- Invalid references between data files
- Schema violations
- Type mismatches

#### validate_shaders.py
Validates GLSL shader code:
- Uses glslangValidator if available (full validation)
- Falls back to basic syntax checking if not
- Extracts and reports uniform/attribute usage
- Checks for #version directives
- Detects common shader errors

**Usage:**
```bash
python3 scripts/validate_shaders.py
```

**Install full validator (optional):**
```bash
sudo apt-get install glslang-tools
```

### 3. Build System

#### Makefile.check
Makefile for systematic syntax checking:
- Checks each .cpp and .hpp file independently
- Uses stub headers instead of real dependencies
- Creates marker files for successful checks
- Parallel checking support with make -j

**Usage:**
```bash
make -f Makefile.check all      # Check all files
make -f Makefile.check clean    # Remove markers
make -f Makefile.check report   # Show status
```

### 4. Master Validation Script

#### full_validation.sh
Orchestrates all validation checks:
1. JSON validation (required)
2. Include structure check (optional)
3. Shader validation (optional)
4. Compilation check (optional)
5. Unit tests if available (optional)

**Usage:**
```bash
./scripts/full_validation.sh
```

**Generates:**
- `validation_report.txt` - Summary of all checks
- `validation_log.txt` - Detailed output

**Exit codes:**
- 0: All required validations passed
- 1: One or more required validations failed

## Quick Start

### Run All Validations
```bash
./scripts/full_validation.sh
```

### Run Individual Checks
```bash
# JSON only
python3 scripts/validate_json.py

# Shaders only
python3 scripts/validate_shaders.py

# Includes only
python3 scripts/check_includes.py

# Compilation only
python3 scripts/check_compilation.py
```

## Current Status (as of validation run)

### JSON Validation
- **Status:** ❌ FAILED
- **Issues Found:** 43 errors, 29 warnings
- **Key Problems:**
  - NPC positions not formatted as objects
  - Items missing 'id' fields
  - Category names have incorrect case (Consumable vs consumable)

### Include Structure
- **Status:** ⚠️ PASSED with warnings
- **Issues:** 660 unresolved includes (mostly standard library - expected)
- **Positive:**
  - All headers have include guards ✓
  - No circular dependencies ✓

### Shader Validation
- **Status:** ⚠️ PASSED with issues
- **Results:** 32/45 passed (71%)
- **Issues:**
  - 13 shaders with possible syntax errors
  - Some .glsl includes missing #version (acceptable for includes)
  - Possible missing semicolons detected

### Compilation Check
- **Status:** ⚠️ Partial
- **Issues:**
  - Missing third-party dependencies (glm, stb_image)
  - Some C++ language issues (constexpr, access control)
  - Expected without full dependency installation

## Integration with CI/CD

### Pre-commit Hook
Add to `.git/hooks/pre-commit`:
```bash
#!/bin/bash
# Run JSON validation before commit
python3 scripts/validate_json.py
if [ $? -ne 0 ]; then
    echo "JSON validation failed! Fix errors before committing."
    exit 1
fi
```

### CI Pipeline (GitHub Actions example)
```yaml
name: Validation
on: [push, pull_request]
jobs:
  validate:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v2
      - name: Run full validation
        run: ./scripts/full_validation.sh
```

## Extending the System

### Adding New Stub Headers

1. Create stub in `build_stubs/new_library_stubs.h`
2. Define minimal types needed for compilation
3. Add to `INCLUDES` in `Makefile.check`
4. Add to `check_compilation.py` includes

### Adding New Validators

1. Create script in `scripts/validate_<name>.py`
2. Follow existing pattern:
   - Print header with separator
   - Show progress
   - Report issues
   - Print summary
   - Return exit code 0 (success) or 1 (failure)
3. Add to `full_validation.sh`

### Custom Validation Rules

Edit the validation scripts to add project-specific rules:

**Example: Enforce naming convention in quests**
```python
# In validate_json.py
if not quest["id"].startswith("quest_"):
    errors.append(ValidationError(
        rel_path,
        f"Quest ID must start with 'quest_': {quest['id']}"
    ))
```

## Troubleshooting

### "No C++ compiler found"
Install g++ or clang++:
```bash
sudo apt-get install g++
# or
sudo apt-get install clang
```

### "Module not found" (Python)
All scripts use only Python standard library. Ensure Python 3.6+ is installed:
```bash
python3 --version
```

### Shader validation limited
Install glslang for full shader validation:
```bash
sudo apt-get install glslang-tools
```

### False positives in compilation check
This is expected when checking without full dependencies. Focus on:
- Actual syntax errors
- Missing project headers (not third-party)
- Logic errors

## Performance

Typical validation times on standard hardware:

- JSON validation: < 1 second
- Include check: ~2 seconds (315 files)
- Shader validation: ~3 seconds (45 shaders)
- Compilation check: ~30 seconds (315 files)
- **Total:** ~40 seconds for full validation

## Benefits

1. **Early Error Detection** - Catch issues before compilation
2. **No Dependencies Required** - Works without GPU SDKs
3. **Fast Feedback** - Complete validation in under a minute
4. **CI/CD Ready** - Easy integration with automated pipelines
5. **Data Integrity** - Validates game data structure
6. **Documentation** - Clear reports show what's working

## Maintenance

Regular tasks:

1. **Update stubs** when using new GPU API features
2. **Add validation rules** for new data types
3. **Review false positives** and adjust thresholds
4. **Keep validators in sync** with project structure

## License

Part of Cat Annihilation project. Same license applies.
