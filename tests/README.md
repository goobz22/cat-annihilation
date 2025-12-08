# Cat Annihilation Test Suite

Comprehensive testing infrastructure for the Cat Annihilation CUDA/Vulkan game engine.

## Overview

This test suite provides:

- **Unit Tests**: Test individual game systems in isolation
- **Integration Tests**: Test system interactions and game flow
- **Mock Implementations**: GPU-free testing using mock Vulkan/CUDA
- **Validation Scripts**: Code quality and syntax checking
- **CI/CD Pipeline**: Automated testing via GitHub Actions

## Test Coverage

### Unit Tests (12 test files, ~2800 lines)

- **test_leveling_system.cpp**: XP, level ups, abilities, weapon skills
- **test_quest_system.cpp**: Quest activation, objectives, rewards, prerequisites
- **test_combat_system.cpp**: Damage calculation, blocking, dodging, combos
- **test_story_mode.cpp**: Clans, territories, ranks
- **test_day_night.cpp**: Time progression, lighting, day/night cycles
- **test_serialization.cpp**: Save/load, binary serialization
- **test_elemental_magic.cpp**: Spell system, elements, mana
- **test_cat_customization.cpp**: Appearance, accessories, weapon skins
- **test_dialog_system.cpp**: Dialog parsing, variables, branching
- **test_npc_system.cpp**: NPC interactions, schedules, inventory
- **test_combo_system.cpp**: Combo tracking, input sequences
- **test_status_effects.cpp**: DOT/HOT effects, buffs, debuffs

### Integration Tests (3 test files, ~1000 lines)

- **test_game_flow.cpp**: Complete game progression
- **test_event_system.cpp**: Cross-system communication
- **test_system_integration.cpp**: All systems working together

## Building and Running Tests

### Quick Start

```bash
# Run all tests
./scripts/run_tests.sh

# Run only unit tests
./scripts/run_tests.sh --unit

# Run only integration tests
./scripts/run_tests.sh --integration

# Skip build step (if already built)
./scripts/run_tests.sh --skip-build

# Verbose output
./scripts/run_tests.sh --verbose
```

### Manual Build

```bash
# Create build directory
mkdir build_tests
cd build_tests

# Configure with CMake
cmake ../tests -DCMAKE_BUILD_TYPE=Debug

# Build tests
cmake --build . -j$(nproc)

# Run unit tests
./unit_tests

# Run integration tests
./integration_tests

# Run with Catch2 options
./unit_tests --success              # Show successful tests
./unit_tests -r compact             # Compact output
./unit_tests -r junit > results.xml # JUnit XML output
./unit_tests --help                 # Show all options
```

### Building with Main Project

```bash
# Build main project with tests enabled (default)
mkdir build && cd build
cmake .. -DBUILD_TESTS=ON
cmake --build .

# Run tests via CTest
ctest

# Or disable tests
cmake .. -DBUILD_TESTS=OFF
```

## Code Validation

### Validation Script

The validation script checks:

- C++ syntax (using g++ -fsyntax-only)
- Header include guards
- Common C++ issues (missing includes)
- JSON file validity
- Shader syntax (if glslangValidator available)
- CUDA kernel syntax (basic checks)
- CMakeLists.txt structure

```bash
# Run validation
./scripts/validate_code.sh
```

### What Gets Validated

- All `.cpp` and `.hpp` files in `game/`, `engine/`, `tests/`
- All `.json` files in `assets/`, `game/`
- All shader files (`.vert`, `.frag`, `.comp`, `.geom`)
- All `.cu` CUDA kernel files
- CMakeLists.txt files

## Mock Implementations

Tests run WITHOUT GPU hardware using mocks:

- **mock_vulkan.hpp**: Stub Vulkan API calls
- **mock_cuda.hpp**: Stub CUDA API calls
- **mock_renderer.hpp**: Fake renderer for testing
- **mock_ecs.hpp**: Minimal ECS for testing

All tests compile with:
- `USE_MOCK_GPU=1`: Enable GPU mocks
- `TESTING_MODE=1`: Enable test-specific features

## CI/CD Pipeline

### GitHub Actions Workflow

Located at `.github/workflows/ci.yml`

**Triggers:**
- Push to `main` or `develop` branches
- Pull requests to `main` or `develop`
- Manual workflow dispatch

**Jobs:**

1. **Code Validation**
   - Checks C++ syntax
   - Validates JSON and shaders
   - Verifies code structure

2. **Build and Test**
   - Builds test executables
   - Runs unit tests
   - Runs integration tests
   - Uploads test results

3. **Code Formatting** (optional)
   - Checks clang-format compliance

4. **CI Success**
   - Summary of all checks

### CI Requirements

The CI runs on Ubuntu with:
- GCC compiler
- CMake 3.20+
- Python 3
- No GPU hardware (uses mocks)

## Test Framework

### Catch2 v2.13.10

Single-header test framework located at `tests/catch2/catch.hpp`

**Common Patterns:**

```cpp
#include "catch2/catch.hpp"
#include "game/systems/leveling_system.hpp"

using namespace CatGame;

TEST_CASE("System name - Feature", "[tag]") {
    // Setup
    LevelingSystem leveling;
    leveling.initialize();

    SECTION("Specific test case") {
        // Test code
        REQUIRE(leveling.getLevel() == 1);
        REQUIRE(leveling.getXP() == 0);
    }

    SECTION("Another test case") {
        // Test code
        bool result = leveling.addXP(100);
        REQUIRE(result);
    }
}
```

**Assertions:**
- `REQUIRE(expr)`: Fatal assertion (stops test on failure)
- `CHECK(expr)`: Non-fatal assertion (continues test)
- `REQUIRE_FALSE(expr)`: Require false
- `REQUIRE_NOTHROW(expr)`: No exception thrown
- `REQUIRE_THROWS(expr)`: Exception thrown
- `Approx(value)`: Floating-point comparison

## Writing New Tests

### Unit Test Template

```cpp
/**
 * Unit Tests for [System Name]
 *
 * Tests:
 * - Feature 1
 * - Feature 2
 * - Feature 3
 */

#include "catch2/catch.hpp"
#include "game/systems/your_system.hpp"

using namespace CatGame;

TEST_CASE("System - Feature", "[tag]") {
    SECTION("Test case 1") {
        // Setup
        // Execute
        // Assert
    }

    SECTION("Test case 2") {
        // Setup
        // Execute
        // Assert
    }
}
```

### Integration Test Template

```cpp
#include "catch2/catch.hpp"
#include "game/systems/system1.hpp"
#include "game/systems/system2.hpp"
#include "mocks/mock_ecs.hpp"

using namespace CatGame;

TEST_CASE("Integration - Feature", "[integration][tag]") {
    MockECS::ECS ecs;
    System1 sys1;
    System2 sys2;

    sys1.init(&ecs);
    sys2.init(&ecs);

    SECTION("Systems work together") {
        // Test interaction
    }
}
```

## Troubleshooting

### Tests Won't Build

1. Check that Catch2 header exists:
   ```bash
   ls -la tests/catch2/catch.hpp
   ```

2. Verify CMake configuration:
   ```bash
   cd build_tests
   cmake ../tests -DCMAKE_BUILD_TYPE=Debug
   ```

3. Check for missing source files in `tests/CMakeLists.txt`

### Tests Fail

1. Run with verbose output:
   ```bash
   ./unit_tests --success -r compact
   ```

2. Run specific test:
   ```bash
   ./unit_tests "[leveling]"  # Run tests tagged with [leveling]
   ./unit_tests "Leveling System*"  # Run tests matching pattern
   ```

3. Check system implementation matches test expectations

### Validation Script Issues

1. If C++ syntax checks time out:
   - Comment out the syntax check loop
   - Or run on fewer files

2. If JSON validation fails:
   - Check that JSON files are valid
   - Install `python3` if missing

3. If shader validation fails:
   - Install `glslangValidator` (from Vulkan SDK)
   - Or skip shader validation

## Statistics

- **Total Test Files**: 15
- **Total Test Lines**: ~3,800
- **Mock Implementations**: 4
- **Systems Tested**: 12+
- **Integration Scenarios**: 20+

## Future Enhancements

Potential additions:

- Performance benchmarks
- Memory leak detection (Valgrind)
- Code coverage reports (gcov/lcov)
- Fuzzing tests
- Stress tests
- GPU integration tests (when available)

## License

Same license as Cat Annihilation project.
