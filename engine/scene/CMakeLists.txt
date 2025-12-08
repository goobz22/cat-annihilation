cmake_minimum_required(VERSION 3.20)

project(CatEngineScene CXX)

# Set C++20 standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Scene System Library
add_library(CatEngineScene STATIC
    SceneNode.cpp
    Scene.cpp
    SceneManager.cpp
    SceneSerializer.cpp
)

target_include_directories(CatEngineScene
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)

# Link dependencies
target_link_libraries(CatEngineScene
    PUBLIC
        CatEngineECS
        CatEngineMath
)

# Compiler-specific flags
if(MSVC)
    target_compile_options(CatEngineScene PRIVATE /W4 /WX)
else()
    target_compile_options(CatEngineScene PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -Wno-unused-parameter
    )
endif()

# Optional: Build example
option(BUILD_SCENE_EXAMPLE "Build scene system example" OFF)

if(BUILD_SCENE_EXAMPLE)
    add_executable(SceneExample example_usage.cpp)
    target_link_libraries(SceneExample PRIVATE CatEngineScene)
endif()
