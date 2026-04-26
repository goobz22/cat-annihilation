cmake_minimum_required(VERSION 3.20)

# ---------------------------------------------------------------------------
# Pre-project MSVC / Windows SDK environment bootstrap
# ---------------------------------------------------------------------------
# The `project(... LANGUAGES CXX CUDA)` call below triggers the full
# compiler-identification + try_compile sanity check chain. On Windows
# that chain runs cl.exe, nvcc, link.exe, rc.exe and mt.exe, all of which
# expect a Developer Command Prompt's INCLUDE/LIB/PATH to be set by
# vcvarsall.bat. When openclaw invokes cmake in a plain non-developer
# shell, the chain fails in order:
#
#   1. nvcc fatal : Cannot find compiler 'cl.exe' in PATH
#   2. fatal error C1083: Cannot open include file: 'cstdint'
#   3. RC Pass 1: ... no such file or directory   (rc.exe missing)
#   4. LINK : fatal error LNK1104: cannot open file 'kernel32.lib'
#
# All four symptoms come from missing env vars. This block probes the
# newest installed MSVC toolset and Windows SDK and materialises PATH,
# INCLUDE and LIB so the project() call -- and every subsequent build
# rule -- runs as if we had launched from a Developer Command Prompt.
if(WIN32 AND "$ENV{INCLUDE}" STREQUAL "")
    set(_vs_roots
        "C:/Program Files/Microsoft Visual Studio/2022/Enterprise"
        "C:/Program Files/Microsoft Visual Studio/2022/Professional"
        "C:/Program Files/Microsoft Visual Studio/2022/Community"
        "C:/Program Files/Microsoft Visual Studio/2022/BuildTools"
        "C:/Program Files (x86)/Microsoft Visual Studio/2019/Enterprise"
        "C:/Program Files (x86)/Microsoft Visual Studio/2019/Professional"
        "C:/Program Files (x86)/Microsoft Visual Studio/2019/Community"
        "C:/Program Files (x86)/Microsoft Visual Studio/2019/BuildTools"
    )
    set(_cl_exe_found "")
    foreach(_vs_root IN LISTS _vs_roots)
        set(_msvc_root "${_vs_root}/VC/Tools/MSVC")
        if(IS_DIRECTORY "${_msvc_root}")
            # Enumerate installed toolsets, pick the highest version.
            file(GLOB _toolsets RELATIVE "${_msvc_root}" "${_msvc_root}/*")
            set(_best "")
            foreach(_t IN LISTS _toolsets)
                if(IS_DIRECTORY "${_msvc_root}/${_t}")
                    if("${_best}" STREQUAL "" OR "${_t}" VERSION_GREATER "${_best}")
                        set(_best "${_t}")
                    endif()
                endif()
            endforeach()
            if(NOT "${_best}" STREQUAL "")
                set(_candidate "${_msvc_root}/${_best}/bin/Hostx64/x64/cl.exe")
                if(EXISTS "${_candidate}")
                    set(_cl_exe_found "${_candidate}")
                    break()
                endif()
            endif()
        endif()
    endforeach()

    if(NOT "${_cl_exe_found}" STREQUAL "")
        get_filename_component(_cl_bin_dir "${_cl_exe_found}" DIRECTORY)
        message(STATUS "Pre-project: prepending MSVC cl.exe directory to PATH: ${_cl_bin_dir}")
        # Use native path separators so Windows tools parse it correctly.
        file(TO_NATIVE_PATH "${_cl_bin_dir}" _cl_bin_dir_native)

        # CMake's compiler sanity check (`try_compile` → link → manifest)
        # also needs rc.exe and mt.exe, which vcvarsall would have put on
        # PATH from the Windows SDK bin directory. Find the newest SDK
        # bin dir and prepend it too, otherwise the try_compile fails with
        # `RC Pass 1: command "rc /fo ... " failed ... no such file or
        # directory`.
        set(_winsdk_bin_root "C:/Program Files (x86)/Windows Kits/10/bin")
        set(_winsdk_bin_native "")
        if(IS_DIRECTORY "${_winsdk_bin_root}")
            file(GLOB _winsdk_bin_versions RELATIVE "${_winsdk_bin_root}" "${_winsdk_bin_root}/*")
            set(_best_bin "")
            foreach(_b IN LISTS _winsdk_bin_versions)
                # Skip non-version sibling directories (x64, x86, arm64).
                if(IS_DIRECTORY "${_winsdk_bin_root}/${_b}" AND "${_b}" MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+\\.[0-9]+$")
                    if("${_best_bin}" STREQUAL "" OR "${_b}" VERSION_GREATER "${_best_bin}")
                        set(_best_bin "${_b}")
                    endif()
                endif()
            endforeach()
            if(NOT "${_best_bin}" STREQUAL "" AND EXISTS "${_winsdk_bin_root}/${_best_bin}/x64/rc.exe")
                file(TO_NATIVE_PATH "${_winsdk_bin_root}/${_best_bin}/x64" _winsdk_bin_native)
                message(STATUS "Pre-project: prepending Windows SDK bin to PATH: ${_winsdk_bin_root}/${_best_bin}/x64")
            endif()
        endif()

        if("${_winsdk_bin_native}" STREQUAL "")
            set(ENV{PATH} "${_cl_bin_dir_native};$ENV{PATH}")
        else()
            set(ENV{PATH} "${_cl_bin_dir_native};${_winsdk_bin_native};$ENV{PATH}")
        endif()

        # Tell CMake to pick this exact compiler so we don't end up with
        # clang by accident when the cache is empty.
        set(CMAKE_C_COMPILER   "${_cl_exe_found}" CACHE FILEPATH "MSVC cl.exe (pre-project bootstrap)")
        set(CMAKE_CXX_COMPILER "${_cl_exe_found}" CACHE FILEPATH "MSVC cl.exe (pre-project bootstrap)")

        # Derive the MSVC toolset include/lib paths from the cl.exe path.
        # cl.exe = .../VC/Tools/MSVC/<version>/bin/Hostx64/x64/cl.exe
        # toolset root is four directories up.
        get_filename_component(_msvc_host_root "${_cl_bin_dir}" DIRECTORY)  # bin/Hostx64
        get_filename_component(_msvc_bin_root  "${_msvc_host_root}" DIRECTORY) # bin
        get_filename_component(_msvc_toolset   "${_msvc_bin_root}" DIRECTORY) # <version>
        set(_msvc_include_dir "${_msvc_toolset}/include")
        set(_msvc_lib_dir     "${_msvc_toolset}/lib/x64")

        # Find newest installed Windows SDK (include + lib; bin was already
        # located above when searching for rc.exe).
        set(_winsdk_inc_root "C:/Program Files (x86)/Windows Kits/10/Include")
        set(_winsdk_lib_root "C:/Program Files (x86)/Windows Kits/10/Lib")
        set(_winsdk_best "")
        if(IS_DIRECTORY "${_winsdk_inc_root}")
            file(GLOB _winsdk_inc_versions RELATIVE "${_winsdk_inc_root}" "${_winsdk_inc_root}/*")
            foreach(_v IN LISTS _winsdk_inc_versions)
                if(IS_DIRECTORY "${_winsdk_inc_root}/${_v}" AND EXISTS "${_winsdk_inc_root}/${_v}/ucrt/stdio.h")
                    if("${_winsdk_best}" STREQUAL "" OR "${_v}" VERSION_GREATER "${_winsdk_best}")
                        set(_winsdk_best "${_v}")
                    endif()
                endif()
            endforeach()
        endif()

        if("${_winsdk_best}" STREQUAL "")
            message(FATAL_ERROR "Pre-project bootstrap: no Windows SDK with ucrt/stdio.h under ${_winsdk_inc_root}. \
Install a Windows 10/11 SDK via Visual Studio Installer.")
        endif()

        set(_winsdk_include "${_winsdk_inc_root}/${_winsdk_best}")
        set(_winsdk_lib     "${_winsdk_lib_root}/${_winsdk_best}")

        # Populate INCLUDE (semicolon-separated on Windows) so cl.exe and
        # nvcc-spawned cl.exe both find <cstdint>, <stdio.h>, <windows.h>.
        set(ENV{INCLUDE}
            "${_msvc_include_dir};${_winsdk_include}/ucrt;${_winsdk_include}/shared;${_winsdk_include}/um;${_winsdk_include}/winrt")
        # Populate LIB so link.exe finds kernel32.lib, user32.lib, msvcrt.lib.
        set(ENV{LIB}
            "${_msvc_lib_dir};${_winsdk_lib}/ucrt/x64;${_winsdk_lib}/um/x64")

        message(STATUS "Pre-project: MSVC toolset ${_msvc_toolset}")
        message(STATUS "Pre-project: Windows SDK  ${_winsdk_best}")

        # Remember these so the post-project block below doesn't re-probe.
        set(_catann_msvc_include_dir "${_msvc_include_dir}")
        set(_catann_msvc_lib_dir     "${_msvc_lib_dir}")
        set(_catann_winsdk_include   "${_winsdk_include}")
        set(_catann_winsdk_lib       "${_winsdk_lib}")
    endif()
endif()

project(CatAnnihilation VERSION 1.0.0 LANGUAGES CXX CUDA)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CUDA_STANDARD 17)
set(CMAKE_CUDA_ARCHITECTURES 86 75 70)  # RTX 30xx, 20xx, 10xx
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---------------------------------------------------------------------------
# Post-project MSVC hookups
# ---------------------------------------------------------------------------
# The env vars set in the pre-project block reach cmake's own subprocess
# (the CMake configure step that runs try_compile and compiler detection),
# but NOT the ninja-launched compile commands: ninja inherits its env from
# whatever shell invoked it, not from cmake. So CMake's in-process `set(ENV)`
# calls are invisible to nvcc at build time. nvcc therefore still aborts
# with "Cannot find compiler 'cl.exe' in PATH" for every .cu translation
# unit unless we teach nvcc about cl.exe via its own flags.
if(WIN32 AND MSVC AND DEFINED _catann_msvc_include_dir)
    # 1. Tell nvcc exactly which cl.exe to use as the host compiler via its
    #    --compiler-bindir (short form -ccbin) flag. This is independent of
    #    PATH -- it puts the directory on nvcc's host-compiler search path
    #    explicitly. We can't rely on CMAKE_CUDA_HOST_COMPILER alone here
    #    because CMake's NVCC rule variants vary across CMake versions and
    #    don't always translate that into -ccbin on ninja-generator builds.
    get_filename_component(_catann_cl_bin_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
    # CMake strips quoting when it splits CUDA flags, so wrap the path in
    # escaped quotes to survive the shell round-trip (the directory name
    # contains spaces: "...\\Visual Studio\\2022\\...").
    set(CMAKE_CUDA_FLAGS
        "${CMAKE_CUDA_FLAGS} -ccbin \"${_catann_cl_bin_dir}\""
        CACHE STRING "Flags used by the CUDA compiler" FORCE)
    set(CMAKE_CUDA_HOST_COMPILER "${CMAKE_CXX_COMPILER}" CACHE FILEPATH "nvcc host C++ compiler" FORCE)

    # 2. Register MSVC + Windows SDK headers as SYSTEM includes so any
    #    subsystem using warning flags (/W4 /WX) does not surface diagnostics
    #    from stdlib headers. We use /external:I via CMake's SYSTEM semantics.
    include_directories(SYSTEM
        "${_catann_msvc_include_dir}"
        "${_catann_winsdk_include}/ucrt"
        "${_catann_winsdk_include}/shared"
        "${_catann_winsdk_include}/um"
        "${_catann_winsdk_include}/winrt"
    )
    link_directories(
        "${_catann_msvc_lib_dir}"
        "${_catann_winsdk_lib}/ucrt/x64"
        "${_catann_winsdk_lib}/um/x64"
    )
endif()
# ---------------------------------------------------------------------------

# Option to build tests
option(BUILD_TESTS "Build test suite" ON)

# Shader hot-reload (debug-only feature). Compiled out of Release by
# default per the ENGINE_BACKLOG item's acceptance bar: "Must be compiled
# out of release builds." An explicit -DCAT_SHADER_HOT_RELOAD=ON still
# overrides the auto-detect so a developer can flip it on in any config
# (e.g. RelWithDebInfo) when iterating on shaders without wanting Debug's
# whole-program optimisation cost.
option(CAT_SHADER_HOT_RELOAD "Enable GLSL shader hot-reload driver (debug-only)" OFF)

# Build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Auto-enable shader hot-reload for developer-facing build types. Release
# stays off regardless of the option (the feature is not shippable — it
# shells out to glslc at runtime). Explicit -DCAT_SHADER_HOT_RELOAD=ON
# overrides this auto-detect for any build type.
if(CMAKE_BUILD_TYPE STREQUAL "Debug" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
    set(CAT_SHADER_HOT_RELOAD ON CACHE BOOL "Enable GLSL shader hot-reload driver (debug-only)" FORCE)
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
    ${THIRD_PARTY_DIR}/imgui
    ${THIRD_PARTY_DIR}/imgui/backends
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
    # touch_input.cpp implements Engine::TouchInput, whose header is consumed
    # by game/systems/mobile_controls.hpp. Omitting it from this list used to
    # work only because mobile_controls never called any of TouchInput's
    # out-of-line methods — once it does, the link would fail with
    # unresolved externals. Compile it unconditionally to avoid that trap.
    engine/core/touch_input.cpp
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
    # GraphicsSettingsPanel is the ImGui-side face of Game::GraphicsSettings.
    # Paired with the F4 hook in game/main.cpp, it gives a reviewer live
    # knobs (max FPS, VSync, profiler capture) + a next-launch staging area
    # for the display/quality fields that require a swapchain rebuild.
    engine/ui/GraphicsSettingsPanel.cpp
)

set(ENGINE_DEBUG_SOURCES
    engine/debug/Profiler.cpp
    # ProfilerOverlay is the ImGui face of Profiler — the header-only
    # SummarizeFrameTimes helper is unit-tested in isolation; only the
    # Draw() body (which pulls imgui.h) lives here.
    engine/debug/ProfilerOverlay.cpp
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
    engine/renderer/passes/ScenePass.cpp
    engine/renderer/MeshSubmissionSystem.cpp
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

# Dear ImGui (vendored in third_party/imgui) + Vulkan/GLFW backends.
# Compiled as part of CatEngine so any consumer gets the same instance.
set(IMGUI_SOURCES
    third_party/imgui/imgui.cpp
    third_party/imgui/imgui_draw.cpp
    third_party/imgui/imgui_tables.cpp
    third_party/imgui/imgui_widgets.cpp
    third_party/imgui/backends/imgui_impl_vulkan.cpp
    third_party/imgui/backends/imgui_impl_glfw.cpp
    engine/ui/ImGuiLayer.cpp
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
    ${IMGUI_SOURCES}
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

# Propagate the shader hot-reload gate to both the engine (the header lives
# under engine/rhi/) and the game binary (main.cpp instantiates the driver).
# Using PUBLIC on CatEngine would work too, but being explicit about both
# targets makes it obvious in a build log when the flag is on.
if(CAT_SHADER_HOT_RELOAD)
    target_compile_definitions(CatEngine PUBLIC CAT_ENGINE_SHADER_HOT_RELOAD)
    target_compile_definitions(CatAnnihilation PRIVATE CAT_ENGINE_SHADER_HOT_RELOAD)
    message(STATUS "Shader hot-reload: ENABLED (CAT_ENGINE_SHADER_HOT_RELOAD)")
else()
    message(STATUS "Shader hot-reload: disabled (Release build; define CAT_SHADER_HOT_RELOAD=ON to force)")
endif()

# Link pthread on Linux only
if(NOT WIN32)
    target_link_libraries(CatAnnihilation PRIVATE pthread)
endif()

# Translate the auto-generated compile_commands.json from MSVC (cl.exe)
# flag syntax to clang flags, so the openclaw `validate` sweep can run
# clang++ -fsyntax-only against the same per-file include / define set the
# real ninja build uses. CMake regenerates compile_commands.json on every
# configure with MSVC-style flags (/DWIN32, -external:I, /EHsc, …) which
# clang++ would reject as "no such file or directory: '/DWIN32'". Doing
# this as a POST_BUILD on CatAnnihilation guarantees the translated file
# is in place by the time the next sweep's validate step reads it. See
# scripts/translate_compile_commands.ts for the translation rules and a
# longer rationale (including why a CMake configure-time hook can't work).
add_custom_command(TARGET CatAnnihilation POST_BUILD
    COMMAND bun ${CMAKE_SOURCE_DIR}/scripts/translate_compile_commands.ts
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Translating compile_commands.json MSVC->clang for openclaw validate"
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
