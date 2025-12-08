cmake_minimum_required(VERSION 3.20)

project(CatEngineJobSystem CXX)

# Set C++20 standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Job System Library
add_library(CatEngineJobSystem STATIC
    JobSystem.cpp
    WorkerThread.cpp
)

target_include_directories(CatEngineJobSystem
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# Enable threading support
find_package(Threads REQUIRED)
target_link_libraries(CatEngineJobSystem
    PUBLIC
        Threads::Threads
)

# Compiler-specific flags
if(MSVC)
    target_compile_options(CatEngineJobSystem PRIVATE /W4 /WX)
else()
    target_compile_options(CatEngineJobSystem PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -Wno-unused-parameter
    )
endif()

# Optional: Build example
option(BUILD_JOB_SYSTEM_EXAMPLE "Build job system example" ON)

if(BUILD_JOB_SYSTEM_EXAMPLE)
    add_executable(JobSystemExample example_usage.cpp)
    target_link_libraries(JobSystemExample PRIVATE CatEngineJobSystem)
endif()
