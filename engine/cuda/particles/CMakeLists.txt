# CUDA Particle System
cmake_minimum_required(VERSION 3.18)

# Find CUDA
find_package(CUDAToolkit REQUIRED)

# Particle system library
add_library(particle_system
    ParticleSystem.cpp
    ParticleKernels.cu
)

target_include_directories(particle_system
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..
        ${CMAKE_CURRENT_SOURCE_DIR}/../..
)

target_link_libraries(particle_system
    PUBLIC
        CUDA::cudart
        CUDA::curand
)

# Set CUDA architecture (adjust for your GPU)
set_target_properties(particle_system PROPERTIES
    CUDA_SEPARABLE_COMPILATION ON
    CUDA_ARCHITECTURES "75;86;89"  # Turing, Ampere, Ada Lovelace
)

# C++20 for host code
target_compile_features(particle_system PUBLIC cxx_std_20)

# CUDA flags
target_compile_options(particle_system PRIVATE
    $<$<COMPILE_LANGUAGE:CUDA>:
        --extended-lambda
        --expt-relaxed-constexpr
        -use_fast_math
        -lineinfo
    >
)

# Example executable (optional)
option(BUILD_PARTICLE_EXAMPLE "Build particle system example" ON)

if(BUILD_PARTICLE_EXAMPLE)
    add_executable(particle_example
        example.cpp
    )

    target_link_libraries(particle_example
        PRIVATE
            particle_system
    )
endif()
