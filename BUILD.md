# Building Cat Annihilation

## System Requirements

### Hardware
- **GPU**: NVIDIA RTX 2000 series or newer (for CUDA + Vulkan)
- **RAM**: 8GB minimum, 16GB recommended
- **Storage**: 2GB for build

### Software Dependencies

#### Ubuntu/Debian
```bash
# Update package list
sudo apt update

# Install Vulkan SDK
wget -qO- https://packages.lunarg.com/lunarg-signing-key-pub.asc | sudo tee /etc/apt/trusted.gpg.d/lunarg.asc
sudo wget -qO /etc/apt/sources.list.d/lunarg-vulkan-jammy.list https://packages.lunarg.com/vulkan/lunarg-vulkan-jammy.list
sudo apt update
sudo apt install vulkan-sdk

# Install CUDA Toolkit (12.x)
wget https://developer.download.nvidia.com/compute/cuda/repos/ubuntu2204/x86_64/cuda-keyring_1.1-1_all.deb
sudo dpkg -i cuda-keyring_1.1-1_all.deb
sudo apt update
sudo apt install cuda-toolkit-12-3

# Install other dependencies
sudo apt install -y \
    build-essential \
    cmake \
    ninja-build \
    libglfw3-dev \
    libopenal-dev \
    libssl-dev \
    pkg-config

# Set CUDA environment
echo 'export PATH=/usr/local/cuda/bin:$PATH' >> ~/.bashrc
echo 'export LD_LIBRARY_PATH=/usr/local/cuda/lib64:$LD_LIBRARY_PATH' >> ~/.bashrc
source ~/.bashrc
```

#### Arch Linux
```bash
sudo pacman -S vulkan-devel cuda glfw-x11 openal cmake ninja
```

#### Windows (Visual Studio 2022)
1. Install [Vulkan SDK](https://vulkan.lunarg.com/sdk/home)
2. Install [CUDA Toolkit 12.x](https://developer.nvidia.com/cuda-downloads)
3. Install [Visual Studio 2022](https://visualstudio.microsoft.com/) with C++ workload
4. Install [CMake](https://cmake.org/download/)
5. Install vcpkg and get dependencies:
```powershell
git clone https://github.com/Microsoft/vcpkg.git
cd vcpkg
.\bootstrap-vcpkg.bat
.\vcpkg install glfw3:x64-windows openal-soft:x64-windows
```

## Build Instructions

### Linux
```bash
# Clone or navigate to project
cd cat-annihilation

# Create build directory
mkdir build && cd build

# Configure with CMake
cmake -GNinja -DCMAKE_BUILD_TYPE=Release ..

# Build (use -j for parallel jobs)
ninja -j$(nproc)

# Run
./CatAnnihilation
```

### Windows (PowerShell)
```powershell
cd cat-annihilation
mkdir build
cd build

# Configure (adjust vcpkg path as needed)
cmake -G "Visual Studio 17 2022" -A x64 `
    -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake ..

# Build
cmake --build . --config Release

# Run
.\Release\CatAnnihilation.exe
```

## Build Options

| Option | Default | Description |
|--------|---------|-------------|
| CMAKE_BUILD_TYPE | Release | Debug, Release, RelWithDebInfo |
| CMAKE_CUDA_ARCHITECTURES | 86;75;70 | CUDA compute capabilities |

Example:
```bash
cmake -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CUDA_ARCHITECTURES=86 ..
```

## Compile Shaders

Shaders are compiled automatically during build if `glslc` is found.

Manual compilation:
```bash
cd shaders
chmod +x compile_shaders.sh
./compile_shaders.sh
```

## Troubleshooting

### "Vulkan not found"
```bash
# Verify Vulkan installation
vulkaninfo
# If not found, reinstall Vulkan SDK
```

### "CUDA not found"
```bash
# Verify CUDA installation
nvcc --version
nvidia-smi
# Ensure CUDA is in PATH
```

### "GLFW not found"
```bash
# Ubuntu/Debian
sudo apt install libglfw3-dev

# Check pkg-config
pkg-config --libs glfw3
```

### "OpenAL not found"
```bash
# Ubuntu/Debian
sudo apt install libopenal-dev

# Fedora
sudo dnf install openal-soft-devel
```

### Shader compilation fails
```bash
# Ensure glslc is installed (part of Vulkan SDK)
which glslc
# Or install shaderc separately
sudo apt install glslang-tools
```

## Asset Requirements

The game needs assets in `assets/` directory:

```
assets/
├── models/
│   ├── cat.gltf          # Player model
│   ├── dog.gltf          # Enemy model
│   ├── tree_pine.gltf    # Environment
│   └── ...
├── textures/
│   ├── cat_albedo.png
│   ├── terrain_grass.png
│   └── ...
├── audio/
│   ├── music/
│   │   └── gameplay.ogg
│   └── sfx/
│       ├── sword_swing.wav
│       └── ...
└── fonts/
    └── game_font.ttf
```

For testing without full assets, the game can run with placeholder/procedural geometry.

## Running the Game

```bash
# From build directory
./CatAnnihilation

# Command line options
./CatAnnihilation --fullscreen
./CatAnnihilation --width 2560 --height 1440
./CatAnnihilation --validation   # Enable Vulkan validation layers
```

## Performance Tips

1. **Use Release build** - Debug builds are 5-10x slower
2. **Update GPU drivers** - Latest NVIDIA drivers recommended
3. **Close other GPU apps** - VRAM competition affects performance
4. **Disable validation** - Validation layers add overhead

## Development

### Adding new shaders
1. Add `.vert`, `.frag`, or `.comp` file to `shaders/` subdirectory
2. Add to `compile_shaders.sh`
3. Rebuild project

### Adding new systems
1. Create in `engine/` for reusable or `game/` for game-specific
2. Add source files to CMakeLists.txt
3. Rebuild

## Project Structure

```
cat-annihilation/
├── CMakeLists.txt      # Main build file
├── BUILD.md            # This file
├── engine/             # Reusable engine code
│   ├── core/           # Window, input, config
│   ├── cuda/           # CUDA physics, particles
│   ├── rhi/vulkan/     # Vulkan rendering
│   └── ...
├── game/               # Game-specific code
│   ├── systems/        # Game systems
│   ├── components/     # Game components
│   └── ...
├── shaders/            # GLSL shaders
├── assets/             # Game assets
└── third_party/        # External dependencies (stb, etc.)
```
