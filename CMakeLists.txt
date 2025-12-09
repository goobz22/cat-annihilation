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

# If GLFW not found, download and build it
if(NOT glfw3_FOUND)
    if(WIN32)
        # Download pre-built GLFW for Windows
        set(GLFW_VERSION "3.4")
        set(GLFW_DIR ${CMAKE_SOURCE_DIR}/third_party/glfw-${GLFW_VERSION})
        if(NOT EXISTS ${GLFW_DIR})
            message(STATUS "Downloading GLFW ${GLFW_VERSION}...")
            file(DOWNLOAD
                "https://github.com/glfw/glfw/releases/download/${GLFW_VERSION}/glfw-${GLFW_VERSION}.bin.WIN64.zip"
                ${CMAKE_SOURCE_DIR}/third_party/glfw.zip
                SHOW_PROGRESS
            )
            execute_process(
                COMMAND ${CMAKE_COMMAND} -E tar xzf ${CMAKE_SOURCE_DIR}/third_party/glfw.zip
                WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/third_party
            )
            file(RENAME ${CMAKE_SOURCE_DIR}/third_party/glfw-${GLFW_VERSION}.bin.WIN64 ${GLFW_DIR})
            file(REMOVE ${CMAKE_SOURCE_DIR}/third_party/glfw.zip)
        endif()
        set(GLFW3_INCLUDE_DIRS ${GLFW_DIR}/include)
        set(GLFW3_LIBRARIES ${GLFW_DIR}/lib-vc2022/glfw3.lib)
        set(GLFW3_FOUND TRUE)
    else()
        # Linux: try pkg-config
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(GLFW3 REQUIRED glfw3)
    endif()
endif()

# Third-party directory
set(THIRD_PARTY_DIR ${CMAKE_SOURCE_DIR}/third_party)

# If OpenAL not found on Windows, use downloaded OpenAL-Soft
if(WIN32 AND NOT OPENAL_FOUND)
    set(OPENAL_VERSION "1.23.1")
    # The downloaded zip extracts to openal-soft-VERSION-bin
    set(OPENAL_DIR ${THIRD_PARTY_DIR}/openal-soft-${OPENAL_VERSION}-bin)
    if(NOT EXISTS ${OPENAL_DIR})
        message(STATUS "Downloading OpenAL-Soft ${OPENAL_VERSION}...")
        file(DOWNLOAD
            "https://github.com/kcat/openal-soft/releases/download/${OPENAL_VERSION}/openal-soft-${OPENAL_VERSION}-bin.zip"
            ${THIRD_PARTY_DIR}/openal.zip
            SHOW_PROGRESS
        )
        execute_process(
            COMMAND ${CMAKE_COMMAND} -E tar xzf ${THIRD_PARTY_DIR}/openal.zip
            WORKING_DIRECTORY ${THIRD_PARTY_DIR}
        )
        file(REMOVE ${THIRD_PARTY_DIR}/openal.zip)
    endif()
    set(OPENAL_INCLUDE_DIR ${OPENAL_DIR}/include)
    set(OPENAL_LIBRARY ${OPENAL_DIR}/libs/Win64/OpenAL32.lib)
    set(OPENAL_FOUND TRUE)
endif()

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
    message(STATUS "Downloading GLM...")
    file(DOWNLOAD
        "https://github.com/g-truc/glm/releases/download/1.0.1/glm-1.0.1-light.zip"
        ${THIRD_PARTY_DIR}/glm.zip
        SHOW_PROGRESS
    )
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E tar xzf ${THIRD_PARTY_DIR}/glm.zip
        WORKING_DIRECTORY ${THIRD_PARTY_DIR}
    )
    file(REMOVE ${THIRD_PARTY_DIR}/glm.zip)
endif()

# Download nlohmann/json if not present
if(NOT EXISTS ${THIRD_PARTY_DIR}/nlohmann/json.hpp)
    message(STATUS "Downloading nlohmann/json...")
    file(MAKE_DIRECTORY ${THIRD_PARTY_DIR}/nlohmann)
    file(DOWNLOAD
        "https://github.com/nlohmann/json/releases/download/v3.11.3/json.hpp"
        ${THIRD_PARTY_DIR}/nlohmann/json.hpp
        SHOW_PROGRESS
    )
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

if(OPENAL_INCLUDE_DIR)
    include_directories(${OPENAL_INCLUDE_DIR})
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
    engine/core/serialization.cpp
    engine/core/save_system.cpp
    engine/core/settings_manager.cpp
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
    engine/renderer/BitmapFont.cpp
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
    engine/cuda/particles/ParticleSystem.cu
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
    game/systems/cat_customization.cpp
    game/systems/clan_territory.cpp
    game/systems/combo_system.cpp
    game/systems/damage_numbers.cpp
    game/systems/day_night_cycle.cpp
    game/systems/DialogSystem.cpp
    game/systems/elemental_magic.cpp
    game/systems/leveling_system.cpp
    game/systems/MerchantSystem.cpp
    game/systems/mobile_controls.cpp
    game/systems/night_effects.cpp
    game/systems/NPCSystem.cpp
    game/systems/quest_system.cpp
    game/systems/status_effects.cpp
    game/systems/story_mode.cpp
    game/systems/story_skills.cpp
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
)

# Link pthread on Linux only
if(NOT WIN32)
    target_link_libraries(CatAnnihilation PRIVATE pthread)
endif()

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
find_program(GLSLC glslc HINTS $ENV{VULKAN_SDK}/Bin $ENV{VULKAN_SDK}/bin)
if(GLSLC)
    message(STATUS "Found glslc: ${GLSLC}")

    # Create output directory for compiled shaders
    set(SHADER_OUTPUT_DIR ${CMAKE_SOURCE_DIR}/shaders/compiled)
    file(MAKE_DIRECTORY ${SHADER_OUTPUT_DIR})

    # Collect all shader files
    file(GLOB_RECURSE VERTEX_SHADERS ${CMAKE_SOURCE_DIR}/shaders/*.vert)
    file(GLOB_RECURSE FRAGMENT_SHADERS ${CMAKE_SOURCE_DIR}/shaders/*.frag)
    file(GLOB_RECURSE COMPUTE_SHADERS ${CMAKE_SOURCE_DIR}/shaders/*.comp)

    set(ALL_SHADERS ${VERTEX_SHADERS} ${FRAGMENT_SHADERS} ${COMPUTE_SHADERS})
    set(SPIRV_OUTPUTS "")

    # Create compilation commands for each shader
    foreach(SHADER ${ALL_SHADERS})
        get_filename_component(SHADER_NAME ${SHADER} NAME)
        set(SPIRV_OUTPUT ${SHADER_OUTPUT_DIR}/${SHADER_NAME}.spv)
        list(APPEND SPIRV_OUTPUTS ${SPIRV_OUTPUT})

        add_custom_command(
            OUTPUT ${SPIRV_OUTPUT}
            COMMAND ${GLSLC} -I${CMAKE_SOURCE_DIR}/shaders ${SHADER} -o ${SPIRV_OUTPUT}
            DEPENDS ${SHADER}
            COMMENT "Compiling shader ${SHADER_NAME}"
            VERBATIM
        )
    endforeach()

    # Add custom target for shader compilation
    add_custom_target(compile_shaders DEPENDS ${SPIRV_OUTPUTS})
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
