cmake_minimum_required(VERSION 3.20)
project(CatAnnihilation VERSION 1.0.0 LANGUAGES CXX CUDA)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CUDA_STANDARD 17)
set(CMAKE_CUDA_ARCHITECTURES 86 75 70)  # RTX 30xx, 20xx, 10xx
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Option to build tests
option(BUILD_TESTS "Build test suite" ON)

# Build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Find packages
find_package(Vulkan REQUIRED)
find_package(CUDAToolkit REQUIRED)
find_package(glfw3 QUIET)
find_package(OpenAL QUIET)

# If GLFW not found via package, try pkg-config
if(NOT glfw3_FOUND)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GLFW3 REQUIRED glfw3)
endif()

# Third-party directory
set(THIRD_PARTY_DIR ${CMAKE_SOURCE_DIR}/third_party)

# Download stb headers if not present
if(NOT EXISTS ${THIRD_PARTY_DIR}/stb/stb_image.h)
    file(MAKE_DIRECTORY ${THIRD_PARTY_DIR}/stb)
    file(DOWNLOAD
        https://raw.githubusercontent.com/nothings/stb/master/stb_image.h
        ${THIRD_PARTY_DIR}/stb/stb_image.h
        SHOW_PROGRESS
    )
    file(DOWNLOAD
        https://raw.githubusercontent.com/nothings/stb/master/stb_truetype.h
        ${THIRD_PARTY_DIR}/stb/stb_truetype.h
        SHOW_PROGRESS
    )
endif()

# Download GLM if not present
if(NOT EXISTS ${THIRD_PARTY_DIR}/glm/glm/glm.hpp)
    file(MAKE_DIRECTORY ${THIRD_PARTY_DIR}/glm)
    message(STATUS "GLM not found - using engine math library instead")
endif()

# Include directories
include_directories(
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/engine
    ${CMAKE_SOURCE_DIR}/game
    ${THIRD_PARTY_DIR}
    ${THIRD_PARTY_DIR}/stb
    ${Vulkan_INCLUDE_DIRS}
    ${CUDAToolkit_INCLUDE_DIRS}
)

if(GLFW3_INCLUDE_DIRS)
    include_directories(${GLFW3_INCLUDE_DIRS})
endif()

# Compiler flags
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic)
    add_compile_options(-msse4.1)  # For SIMD math
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        add_compile_options(-O3 -march=native)
    endif()
endif()

# Engine sources
set(ENGINE_CORE_SOURCES
    engine/core/Logger.cpp
    engine/core/Config.cpp
    engine/core/Timer.cpp
    engine/core/Window.cpp
    engine/core/Input.cpp
)

set(ENGINE_MEMORY_SOURCES
    engine/memory/PoolAllocator.cpp
    engine/memory/StackAllocator.cpp
    engine/memory/LinearAllocator.cpp
)

set(ENGINE_ECS_SOURCES
    engine/ecs/EntityManager.cpp
    engine/ecs/ComponentPool.cpp
    engine/ecs/SystemManager.cpp
)

set(ENGINE_JOBS_SOURCES
    engine/jobs/JobSystem.cpp
    engine/jobs/WorkerThread.cpp
)

set(ENGINE_AUDIO_SOURCES
    engine/audio/AudioEngine.cpp
    engine/audio/AudioSource.cpp
    engine/audio/AudioListener.cpp
    engine/audio/AudioBuffer.cpp
    engine/audio/AudioMixer.cpp
)

set(ENGINE_ASSETS_SOURCES
    engine/assets/AssetManager.cpp
    engine/assets/AssetLoader.cpp
    engine/assets/ModelLoader.cpp
    engine/assets/TextureLoader.cpp
)

set(ENGINE_SCENE_SOURCES
    engine/scene/Scene.cpp
    engine/scene/SceneManager.cpp
    engine/scene/SceneNode.cpp
    engine/scene/SceneSerializer.cpp
)

set(ENGINE_ANIMATION_SOURCES
    engine/animation/Animation.cpp
    engine/animation/Animator.cpp
    engine/animation/Skeleton.cpp
    engine/animation/AnimationBlend.cpp
)

set(ENGINE_AI_SOURCES
    engine/ai/AISystem.cpp
    engine/ai/BehaviorTree.cpp
    engine/ai/Blackboard.cpp
    engine/ai/Navigation.cpp
)

set(ENGINE_UI_SOURCES
    engine/ui/UISystem.cpp
    engine/ui/UIWidget.cpp
    engine/ui/UIText.cpp
    engine/ui/UIImage.cpp
    engine/ui/UIButton.cpp
    engine/ui/UIPanel.cpp
    engine/ui/FontRenderer.cpp
)

set(ENGINE_DEBUG_SOURCES
    engine/debug/Profiler.cpp
)

set(ENGINE_RENDERER_SOURCES
    engine/renderer/Renderer.cpp
    engine/renderer/RenderGraph.cpp
    engine/renderer/GPUScene.cpp
    engine/renderer/Camera.cpp
    engine/renderer/Mesh.cpp
    engine/renderer/Material.cpp
    engine/renderer/passes/GeometryPass.cpp
    engine/renderer/passes/LightingPass.cpp
    engine/renderer/passes/ForwardPass.cpp
    engine/renderer/passes/ShadowPass.cpp
    engine/renderer/passes/SkyboxPass.cpp
    engine/renderer/passes/PostProcessPass.cpp
    engine/renderer/passes/UIPass.cpp
    engine/renderer/lighting/LightManager.cpp
    engine/renderer/lighting/ClusteredLighting.cpp
    engine/renderer/lighting/ShadowAtlas.cpp
)

set(ENGINE_RHI_VULKAN_SOURCES
    engine/rhi/vulkan/VulkanRHI.cpp
    engine/rhi/vulkan/VulkanDevice.cpp
    engine/rhi/vulkan/VulkanSwapchain.cpp
    engine/rhi/vulkan/VulkanBuffer.cpp
    engine/rhi/vulkan/VulkanTexture.cpp
    engine/rhi/vulkan/VulkanPipeline.cpp
    engine/rhi/vulkan/VulkanShader.cpp
    engine/rhi/vulkan/VulkanCommandBuffer.cpp
    engine/rhi/vulkan/VulkanDescriptor.cpp
    engine/rhi/vulkan/VulkanRenderPass.cpp
    engine/rhi/vulkan/VulkanSync.cpp
    engine/rhi/vulkan/VulkanDebug.cpp
    engine/rhi/vulkan/VulkanCudaInterop.cpp
)

set(ENGINE_CUDA_SOURCES
    engine/cuda/CudaContext.cpp
    engine/cuda/CudaStream.cpp
)

set(ENGINE_CUDA_PHYSICS_SOURCES
    engine/cuda/physics/PhysicsWorld.cpp
    engine/cuda/physics/Collider.cpp
)

set(ENGINE_CUDA_KERNELS
    engine/cuda/physics/PhysicsWorld.cu
    engine/cuda/physics/SpatialHash.cu
    engine/cuda/physics/NarrowPhase.cu
    engine/cuda/physics/Integration.cu
    engine/cuda/particles/ParticleKernels.cu
)

set(ENGINE_CUDA_PARTICLES_SOURCES
    engine/cuda/particles/ParticleSystem.cpp
)

# Game sources
set(GAME_SOURCES
    game/CatAnnihilation.cpp
    game/systems/PlayerControlSystem.cpp
    game/systems/CombatSystem.cpp
    game/systems/EnemyAISystem.cpp
    game/systems/ProjectileSystem.cpp
    game/systems/WaveSystem.cpp
    game/systems/HealthSystem.cpp
    game/entities/CatEntity.cpp
    game/entities/DogEntity.cpp
    game/entities/ProjectileEntity.cpp
    game/world/GameWorld.cpp
    game/world/Terrain.cpp
    game/world/Forest.cpp
    game/world/Environment.cpp
    game/ui/GameUI.cpp
    game/ui/HUD.cpp
    game/ui/MainMenu.cpp
    game/ui/PauseMenu.cpp
    game/ui/WavePopup.cpp
    game/audio/GameAudio.cpp
)

set(GAME_CUDA_KERNELS
    game/world/Terrain.cu
)

# Create engine library
add_library(CatEngine STATIC
    ${ENGINE_CORE_SOURCES}
    ${ENGINE_MEMORY_SOURCES}
    ${ENGINE_ECS_SOURCES}
    ${ENGINE_JOBS_SOURCES}
    ${ENGINE_AUDIO_SOURCES}
    ${ENGINE_ASSETS_SOURCES}
    ${ENGINE_SCENE_SOURCES}
    ${ENGINE_ANIMATION_SOURCES}
    ${ENGINE_AI_SOURCES}
    ${ENGINE_UI_SOURCES}
    ${ENGINE_DEBUG_SOURCES}
    ${ENGINE_RENDERER_SOURCES}
    ${ENGINE_RHI_VULKAN_SOURCES}
    ${ENGINE_CUDA_SOURCES}
    ${ENGINE_CUDA_PHYSICS_SOURCES}
    ${ENGINE_CUDA_PARTICLES_SOURCES}
    ${ENGINE_CUDA_KERNELS}
)

target_link_libraries(CatEngine PUBLIC
    Vulkan::Vulkan
    CUDA::cudart
    CUDA::cuda_driver
)

if(glfw3_FOUND)
    target_link_libraries(CatEngine PUBLIC glfw)
elseif(GLFW3_LIBRARIES)
    target_link_libraries(CatEngine PUBLIC ${GLFW3_LIBRARIES})
endif()

if(OPENAL_FOUND)
    target_link_libraries(CatEngine PUBLIC ${OPENAL_LIBRARY})
else()
    target_link_libraries(CatEngine PUBLIC openal)
endif()

# Create game executable
add_executable(CatAnnihilation
    game/main.cpp
    ${GAME_SOURCES}
    ${GAME_CUDA_KERNELS}
)

target_link_libraries(CatAnnihilation PRIVATE
    CatEngine
    pthread
)

# Copy assets to build directory
add_custom_command(TARGET CatAnnihilation POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/assets $<TARGET_FILE_DIR:CatAnnihilation>/assets
    COMMENT "Copying assets to build directory"
)

# Copy shaders to build directory
add_custom_command(TARGET CatAnnihilation POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/shaders $<TARGET_FILE_DIR:CatAnnihilation>/shaders
    COMMENT "Copying shaders to build directory"
)

# Shader compilation (optional - compile GLSL to SPIR-V)
find_program(GLSLC glslc HINTS $ENV{VULKAN_SDK}/bin)
if(GLSLC)
    message(STATUS "Found glslc: ${GLSLC}")
    # Add custom target for shader compilation
    add_custom_target(compile_shaders
        COMMAND ${CMAKE_SOURCE_DIR}/shaders/compile_shaders.sh
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/shaders
        COMMENT "Compiling shaders to SPIR-V"
    )
    add_dependencies(CatAnnihilation compile_shaders)
endif()

# Add test subdirectory
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Print configuration
message(STATUS "")
message(STATUS "=== Cat Annihilation Build Configuration ===")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "CUDA Architectures: ${CMAKE_CUDA_ARCHITECTURES}")
message(STATUS "Vulkan: ${Vulkan_LIBRARIES}")
message(STATUS "CUDA: ${CUDAToolkit_LIBRARY_DIR}")
message(STATUS "Build tests: ${BUILD_TESTS}")
message(STATUS "")
