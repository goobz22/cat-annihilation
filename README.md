# Cat Annihilation - Unreal Engine Edition

A 3D game where you play as a cat in a destructible forest environment, built with Unreal Engine 5.

## System Requirements

- Windows 10/11 or macOS 12+
- GPU: NVIDIA GeForce GTX 1060 / AMD Radeon RX 580 or better (RTX 3080 recommended)
- CPU: Intel i5-7400 / AMD Ryzen 3 1300X or better
- RAM: 16GB minimum (32GB recommended)
- Storage: 20GB available space
- DirectX 12 or Metal compatible graphics card

## Getting Started

### Prerequisites

1. Download and install [Unreal Engine 5.3](https://www.unrealengine.com/download) or newer
2. Install [Visual Studio 2022](https://visualstudio.microsoft.com/downloads/) (Windows) or [Xcode](https://developer.apple.com/xcode/) (macOS)
   - For Windows: Select "Game Development with C++" workload during Visual Studio installation
3. Install [Git LFS](https://git-lfs.github.com/) for handling large asset files

### Setup

1. Clone the repository:
   ```bash
   git clone https://github.com/yourusername/cat-annihilation.git
   cd cat-annihilation
   ```

2. Right-click `CatAnnihilation.uproject` and select "Generate Visual Studio project files" (Windows) or open the project directly in Unreal Engine (macOS)

3. Open the project:
   - Windows: Double-click `CatAnnihilation.uproject` or open the generated solution file in Visual Studio
   - macOS: Double-click `CatAnnihilation.uproject`

4. Wait for Unreal Engine to compile shaders and build the project

### Running the Game

1. In the Unreal Editor, click the "Play" button in the toolbar or press Alt+P
2. To run in standalone mode, select "New Editor Window (PIE)" from the Play button dropdown

## Project Structure

- `/Content/Blueprints` - Game logic and behavior
  - `/Content/Blueprints/BP_CatCharacter` - Main playable character
  - `/Content/Blueprints/BP_GameMode` - Game rules and state management
  - `/Content/Blueprints/BP_GameUI` - User interface elements
- `/Content/Maps` - Game levels
  - `/Content/Maps/MainLevel` - Primary gameplay environment
- `/Content/Models` - 3D assets and character models
- `/Content/Materials` - Material definitions for objects
- `/Content/Textures` - Texture files
- `/Source` - C++ source code (for advanced features)

## Controls

- **WASD** - Move character
- **Space** - Jump
- **Left Mouse Button** - Attack/Interact
- **Right Mouse Button** - Special ability
- **E** - Interact with objects
- **Tab** - Toggle inventory
- **Esc** - Pause menu

## Development Workflow

### Creating New Features

1. Create a new branch for your feature:
   ```bash
   git checkout -b feature/your-feature-name
   ```

2. Make your changes in the Unreal Editor or code

3. Package the game for testing:
   - Go to File > Package Project > Windows/Mac
   - Select your output directory

4. Test your changes in the packaged build

### Blueprint Editing

1. Open the Blueprints folder in the Content Browser
2. Double-click on the Blueprint you want to edit
3. Use the Blueprint Editor to modify logic, add components, or adjust properties
4. Compile the Blueprint (Ctrl+K) and save (Ctrl+S)

### Level Editing

1. Open a map in the Content Browser
2. Use the Landscape tools to modify terrain
3. Place environment actors by dragging from the Content Browser
4. Set up lighting and post-processing effects
5. Use Play-in-Editor to test changes

## Troubleshooting

- **Crashes on startup**: Verify that your graphics drivers are up to date
- **Missing assets**: Run "Verify" in the Unreal Engine launcher
- **Compilation errors**: Clean the project (delete Binaries, Intermediate, and Saved folders) and regenerate project files
- **Performance issues**: Lower graphics settings in the game menu or adjust scalability settings in the editor

## License

This project is licensed under the MIT License - see the LICENSE file for details.