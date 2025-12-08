cmake_minimum_required(VERSION 3.20)

project(CatEngineCUDA LANGUAGES CXX CUDA)

# Set C++20 standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CUDA_STANDARD 17)
set(CMAKE_CUDA_STANDARD_REQUIRED ON)

# Find CUDA toolkit
find_package(CUDAToolkit REQUIRED)

# CUDA Module Library
add_library(CatEngineCUDA STATIC
    CudaContext.cpp
    CudaStream.cpp
)

target_include_directories(CatEngineCUDA
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# Link CUDA runtime
target_link_libraries(CatEngineCUDA
    PUBLIC
        CUDA::cudart
)

# Set CUDA properties
set_target_properties(CatEngineCUDA PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    CUDA_SEPARABLE_COMPILATION ON
)

# Compiler-specific flags
if(MSVC)
    target_compile_options(CatEngineCUDA PRIVATE /W4)
else()
    target_compile_options(CatEngineCUDA PRIVATE
        -Wall -Wextra -Wpedantic
        -Wno-unused-parameter
    )
endif()

# CUDA-specific flags
target_compile_options(CatEngineCUDA PRIVATE
    $<$<COMPILE_LANGUAGE:CUDA>:--expt-relaxed-constexpr>
)

# Optional: Build example
option(BUILD_CUDA_EXAMPLE "Build CUDA module example" ON)

if(BUILD_CUDA_EXAMPLE)
    add_executable(CudaExample example.cpp)
    target_link_libraries(CudaExample PRIVATE CatEngineCUDA)
endif()
