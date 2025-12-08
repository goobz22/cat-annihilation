# Mobile Controls System

Complete mobile/touchscreen controls system for Cat Annihilation CUDA/Vulkan engine.

## Overview

The Mobile Controls System provides a comprehensive solution for touch-based gameplay on mobile devices, tablets, and touchscreen-enabled devices. It includes:

- **Touch Input System** - Low-level touch event handling and gesture detection
- **Mobile Controls** - High-level virtual controls (joysticks, buttons, swipe zones)
- **Custom Shaders** - Specialized rendering for visual feedback
- **Layout Presets** - Pre-configured control layouts for different play styles

## Architecture

### Component Hierarchy

```
TouchInput (engine/core/)
    ↓
MobileControlsSystem (game/systems/)
    ↓
UISystem (engine/ui/)
    ↓
UIPass (renderer/)
```

## Files Created

### Core Engine Files

1. **`/home/user/cat-annihilation/engine/core/touch_input.hpp`** (13 KB)
   - Touch input system interface
   - Gesture detection structures
   - Touch point tracking

2. **`/home/user/cat-annihilation/engine/core/touch_input.cpp`** (15 KB)
   - Touch input implementation
   - Gesture recognition algorithms
   - GLFW callback integration
   - Mouse simulation for desktop testing

### Game Systems

3. **`/home/user/cat-annihilation/game/systems/mobile_controls.hpp`** (12 KB)
   - Mobile controls system interface
   - Virtual joystick and button structures
   - Layout preset management

4. **`/home/user/cat-annihilation/game/systems/mobile_controls.cpp`** (26 KB)
   - Mobile controls implementation
   - Touch event handlers
   - Default layout creation
   - Customization mode support

### Shaders

5. **`/home/user/cat-annihilation/shaders/ui/touch_controls.vert`** (2.2 KB)
   - Vertex shader for touch controls
   - Pulse and breathing animations
   - Dynamic scaling support

6. **`/home/user/cat-annihilation/shaders/ui/touch_controls.frag`** (6.5 KB)
   - Fragment shader for touch controls
   - Joystick rendering (base + thumb)
   - Button rendering with states
   - Cooldown overlay effect
   - Glow effects for visual feedback

### Configuration

7. **`/home/user/cat-annihilation/assets/config/mobile_layouts.json`** (12 KB)
   - 5 pre-configured layouts (default, lefty, simple, pro, tablet)
   - Gesture mappings
   - Device-specific rules
   - Customization settings

## Usage

### Basic Initialization

```cpp
#include "game/systems/mobile_controls.hpp"
#include "engine/core/touch_input.hpp"

// Create systems
Engine::TouchInput touchInput(window);
Game::MobileControlsSystem mobileControls;

// Initialize
touchInput.initialize();
mobileControls.initialize(&touchInput, uiSystem, screenWidth, screenHeight);

// Auto-enable on touch devices
mobileControls.autoDetectAndEnable();
```

### Game Loop Integration

```cpp
void update(float deltaTime) {
    // Update touch input first
    touchInput.update(deltaTime);

    // Update mobile controls
    if (mobileControls.isEnabled()) {
        mobileControls.update(deltaTime);

        // Get input
        glm::vec2 movement = mobileControls.getMovementInput();
        glm::vec2 camera = mobileControls.getCameraInput();

        // Check buttons
        if (mobileControls.isButtonPressed("attack")) {
            player.attack();
        }
        if (mobileControls.isButtonPressed("dodge")) {
            player.dodge();
        }
    }
}

void render() {
    // Render mobile controls overlay
    if (mobileControls.isVisible()) {
        mobileControls.render(uiPass);
    }
}
```

### Custom Button Setup

```cpp
// Create custom attack button
Game::VirtualButton attackButton;
attackButton.id = "attack";
attackButton.label = "Attack";
attackButton.position = glm::vec2(screenWidth - 100, screenHeight - 100);
attackButton.radius = 50.0f;
attackButton.cooldown = 0.5f; // 0.5 second cooldown

// Set up callbacks
attackButton.onPress = [&player]() {
    player.startAttack();
};
attackButton.onRelease = [&player]() {
    player.endAttack();
};

// Add to system
mobileControls.addButton(attackButton);
```

### Gesture Handling

```cpp
// Set up gesture callbacks
touchInput.setSwipeCallback([](const Engine::SwipeGesture& swipe) {
    if (swipe.direction.y < -0.5f) {
        // Swipe up - jump
        player.jump();
    } else if (swipe.direction.y > 0.5f) {
        // Swipe down - dodge
        player.dodge();
    }
});

touchInput.setTapCallback([](glm::vec2 position, int tapCount) {
    if (tapCount == 2) {
        // Double tap - target lock
        combat.toggleTargetLock();
    }
});

touchInput.setPinchCallback([](float scale) {
    // Pinch to zoom camera
    camera.setZoom(camera.getZoom() * scale);
});
```

### Layout Presets

```cpp
// Apply a preset
mobileControls.applyLayoutPreset("default");  // Right-handed
mobileControls.applyLayoutPreset("lefty");     // Left-handed
mobileControls.applyLayoutPreset("simple");    // Minimal/gestures
mobileControls.applyLayoutPreset("pro");       // All controls
mobileControls.applyLayoutPreset("tablet");    // Tablet optimized

// Load from JSON
mobileControls.loadPresetsFromFile("assets/config/mobile_layouts.json");

// Get available presets
std::vector<std::string> presets = mobileControls.getLayoutPresets();
for (const auto& preset : presets) {
    std::cout << "Available preset: " << preset << std::endl;
}
```

### Customization Mode

```cpp
// Enter customization mode (allows repositioning controls)
mobileControls.enterCustomizationMode();

// Move a control
mobileControls.moveElement("attack", glm::vec2(newX, newY));

// Resize a control
mobileControls.resizeElement("attack", 1.5f); // 150% scale

// Save custom layout
mobileControls.saveCurrentLayout("my_custom_layout");

// Exit customization mode
mobileControls.exitCustomizationMode();
```

### Visual Customization

```cpp
// Adjust global opacity
mobileControls.setOpacity(0.7f); // 0.0 - 1.0

// Adjust global scale
mobileControls.setScale(1.2f); // Larger controls

// Hide/show controls
mobileControls.setVisible(false);
mobileControls.setVisible(true);
```

## Layout Presets

### Default Layout
- **Movement joystick** - Bottom left
- **Attack button** - Bottom right (large)
- **Dodge button** - Right of attack
- **Jump button** - Above attack
- **Spell button** - Above dodge
- **Block button** - Above movement joystick
- **Sprint button** - Right of block
- **Pause button** - Top right
- **Camera swipe zone** - Center area

### Lefty Layout
- Mirrored version of default for left-handed players

### Simple Layout
- **Dynamic movement joystick** - Appears where you touch
- **Single action button** - Context-sensitive
- **Large gesture zone** - For swipes and gestures
- Minimal UI, maximum screen space

### Pro Layout
- All buttons from default
- **Camera joystick** enabled
- **Extra action buttons** (E1, E2)
- For advanced players who want full control

### Tablet Layout
- Scaled up for larger screens
- Both joysticks enabled
- Larger buttons (60px radius)
- Optimized spacing for tablet screens

## Gesture Mappings

### Default Gestures
- **Swipe Up** → Jump
- **Swipe Down** → Dodge/Roll
- **Swipe Left** → Quick dodge left
- **Swipe Right** → Quick dodge right
- **Double Tap** → Target lock/unlock
- **Long Press** → Camera control mode
- **Pinch** → Zoom (if applicable)

### Simple Layout Gestures
- **Swipe Up** → Jump
- **Swipe Down** → Dodge
- **Swipe Left** → Previous weapon
- **Swipe Right** → Next weapon
- **Double Tap** → Attack
- **Long Press** → Block/Shield

## Touch Input Features

### Gesture Detection
- **Tap** - Single touch, quick release
- **Double Tap** - Two taps within 300ms
- **Long Press** - Hold for 500ms+ without movement
- **Swipe** - Directional gesture with minimum velocity
- **Pinch** - Two-finger scale gesture
- **Rotation** - Two-finger rotation gesture

### Configurable Thresholds

```cpp
auto& thresholds = touchInput.getThresholds();
thresholds.swipeMinDistance = 50.0f;     // Minimum pixels for swipe
thresholds.swipeMinVelocity = 100.0f;    // Minimum velocity
thresholds.tapMaxMovement = 10.0f;       // Max movement for tap
thresholds.tapMaxDuration = 0.3f;        // Max time for tap
thresholds.doubleTapMaxDelay = 0.3f;     // Time between taps
thresholds.longPressMinDuration = 0.5f;  // Min time for long press
```

## Shader Features

### Visual Feedback
- **Pulse animation** when controls are pressed
- **Breathing animation** when idle (subtle)
- **Glow effects** on touch
- **Radial gradients** for depth
- **Smooth anti-aliasing** on all edges
- **Cooldown overlays** (pie chart effect)

### Control Types
- **Type 0** - Joystick Base (outer ring + fill)
- **Type 1** - Joystick Thumb (solid circle with glow)
- **Type 2** - Button (gradient circle with icons)

### Customization
All visual properties can be customized:
- Base color / Pressed color / Disabled color
- Opacity (per-control and global)
- Border width and color
- Glow intensity
- Feather amount (anti-aliasing)

## Device Detection

The system automatically detects:
- Touch screen support
- Screen size (phone/tablet/desktop)
- Orientation changes
- Safe areas (notches, rounded corners)

Auto-enable logic:
```cpp
if (hasTouchScreen() && screenWidth < 768) {
    // Phone - use default layout
    applyLayoutPreset("default");
} else if (hasTouchScreen() && screenWidth < 1366) {
    // Tablet - use tablet layout
    applyLayoutPreset("tablet");
} else if (hasTouchScreen()) {
    // Desktop with touch - use pro layout
    applyLayoutPreset("pro");
}
```

## Performance Considerations

- Touch input runs at native refresh rate
- Gesture detection is frame-independent
- Rendering uses instanced draws for controls
- Shaders are optimized for mobile GPUs
- No allocations during gameplay
- Touch events processed on main thread

## Integration Checklist

- [ ] Add touch_input files to CMakeLists.txt
- [ ] Add mobile_controls files to game systems build
- [ ] Compile touch_controls shaders with compile_shaders.sh
- [ ] Initialize TouchInput in main game loop
- [ ] Initialize MobileControlsSystem after UISystem
- [ ] Call update() and render() in game loop
- [ ] Set up button callbacks for game actions
- [ ] Test on target devices
- [ ] Implement save/load for custom layouts
- [ ] Add haptic feedback support (platform-specific)

## Testing

### Desktop Testing
The system includes mouse simulation for testing on desktop:
- Left mouse button = Touch ID 0
- Mouse movement = Touch move
- Works in windowed mode

### Mobile Testing
Test on actual devices:
- Various screen sizes (phone/tablet)
- Different orientations (portrait/landscape)
- Multi-touch gestures
- Performance under load

## Future Enhancements

Potential additions:
- [ ] Haptic feedback integration
- [ ] Audio feedback on touch
- [ ] Additional gesture types (3-finger, 4-finger)
- [ ] Context-sensitive control layouts
- [ ] Training mode to teach gestures
- [ ] Analytics for control usage
- [ ] Accessibility options (larger buttons, simplified controls)
- [ ] VR/AR touch integration

## Troubleshooting

### Controls Not Visible
- Check `mobileControls.isEnabled()`
- Check `mobileControls.isVisible()`
- Verify UIPass is rendering
- Check global opacity setting

### Gestures Not Detected
- Verify `touchInput.initialize()` was called
- Check gesture thresholds
- Ensure callbacks are set
- Test with mouse simulation

### Poor Performance
- Reduce control count
- Lower shader quality
- Disable animations
- Use simpler layout preset

## License

Part of Cat Annihilation game engine.
All rights reserved.
