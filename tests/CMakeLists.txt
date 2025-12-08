# Cat Annihilation Test Suite
cmake_minimum_required(VERSION 3.20)

# Test configuration
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Include Catch2 header-only library
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/catch2)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/mocks)

# Project root is parent of tests directory
set(PROJECT_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)

# Include game and engine directories
include_directories(${PROJECT_ROOT})
include_directories(${PROJECT_ROOT}/engine)
include_directories(${PROJECT_ROOT}/game)
include_directories(${PROJECT_ROOT}/third_party)
include_directories(${PROJECT_ROOT}/third_party/stb)
include_directories(${PROJECT_ROOT}/build_stubs)

# Define mocks to avoid GPU dependencies
add_compile_definitions(USE_MOCK_GPU=1)
add_compile_definitions(TESTING_MODE=1)

# Collect all unit test sources
set(UNIT_TEST_SOURCES
    unit/test_leveling_system.cpp
    unit/test_quest_system.cpp
    unit/test_combat_system.cpp
    unit/test_story_mode.cpp
    unit/test_day_night.cpp
    unit/test_serialization.cpp
    unit/test_elemental_magic.cpp
    unit/test_cat_customization.cpp
    unit/test_dialog_system.cpp
    unit/test_npc_system.cpp
    unit/test_combo_system.cpp
    unit/test_status_effects.cpp
)

# Collect integration test sources
set(INTEGRATION_TEST_SOURCES
    integration/test_game_flow.cpp
    integration/test_event_system.cpp
    integration/test_system_integration.cpp
)

# Game source files needed for testing (non-GPU parts)
set(GAME_TEST_SOURCES
    ${PROJECT_ROOT}/game/systems/leveling_system.cpp
    ${PROJECT_ROOT}/game/systems/quest_system.cpp
    ${PROJECT_ROOT}/game/systems/CombatSystem.cpp
    ${PROJECT_ROOT}/game/systems/DialogSystem.cpp
    ${PROJECT_ROOT}/game/systems/NPCSystem.cpp
    ${PROJECT_ROOT}/game/systems/status_effects.cpp
    ${PROJECT_ROOT}/game/systems/combo_system.cpp
    ${PROJECT_ROOT}/game/systems/elemental_magic.cpp
    ${PROJECT_ROOT}/game/systems/cat_customization.cpp
    ${PROJECT_ROOT}/game/systems/day_night_cycle.cpp
    ${PROJECT_ROOT}/game/systems/story_mode.cpp
    ${PROJECT_ROOT}/game/systems/clan_territory.cpp
    ${PROJECT_ROOT}/game/systems/night_effects.cpp
    ${PROJECT_ROOT}/game/systems/story_skills.cpp
    ${PROJECT_ROOT}/game/systems/MerchantSystem.cpp
)

# Engine source files needed for testing (non-GPU parts)
set(ENGINE_TEST_SOURCES
    ${PROJECT_ROOT}/engine/core/Logger.cpp
    ${PROJECT_ROOT}/engine/core/Timer.cpp
)

# Mock implementations
set(MOCK_SOURCES
    mocks/mock_renderer.cpp
    mocks/mock_ecs.cpp
)

# Create unit test executable
add_executable(unit_tests
    ${UNIT_TEST_SOURCES}
    ${GAME_TEST_SOURCES}
    ${ENGINE_TEST_SOURCES}
    ${MOCK_SOURCES}
    test_main.cpp
)

# Create integration test executable
add_executable(integration_tests
    ${INTEGRATION_TEST_SOURCES}
    ${GAME_TEST_SOURCES}
    ${ENGINE_TEST_SOURCES}
    ${MOCK_SOURCES}
    test_main.cpp
)

# Link libraries (no GPU libraries needed for tests)
target_link_libraries(unit_tests PRIVATE pthread)
target_link_libraries(integration_tests PRIVATE pthread)

# Add test targets
enable_testing()
add_test(NAME UnitTests COMMAND unit_tests)
add_test(NAME IntegrationTests COMMAND integration_tests)

# Print test configuration
message(STATUS "")
message(STATUS "=== Test Suite Configuration ===")
message(STATUS "Unit test sources: ${UNIT_TEST_SOURCES}")
message(STATUS "Integration test sources: ${INTEGRATION_TEST_SOURCES}")
message(STATUS "Using mock GPU implementations")
message(STATUS "")
