# Cat Engine Scene System

Complete hierarchical scene graph implementation for the Cat Annihilation CUDA/Vulkan game engine.

## Overview

The Scene System provides a hierarchical scene graph architecture integrated with the ECS (Entity Component System), enabling efficient organization and management of game objects.

## Features

- **Hierarchical Scene Graph**: Parent-child node relationships with transform propagation
- **ECS Integration**: Each scene owns its own EntityManager and Systems
- **Multi-Scene Management**: Load, unload, and switch between multiple scenes
- **Scene Stacking**: Overlay scenes for UI, pause menus, etc.
- **Serialization**: Save/load scenes to/from JSON format
- **Async Loading**: Preload scenes asynchronously for smooth transitions
- **Transform Caching**: Efficient world transform calculation with lazy updates

## Architecture

### SceneNode
Hierarchical scene graph node with:
- Local and world transforms (position, rotation, scale)
- Parent-child relationships
- Optional Entity reference
- Name-based node lookup
- Depth-first and breadth-first traversal
- Transform dirty flag optimization

### Scene
Container for a complete scene:
- Root node for scene graph
- Dedicated ECS instance (entities, components, systems)
- Scene-wide update loop
- Entity-to-node mapping cache
- Statistics tracking

### SceneManager
Global scene management:
- Create/destroy scenes
- Active scene tracking
- Overlay scene stack (for UI, pause menus)
- Scene loading (sync and async)
- Scene transitions with callbacks
- Preloading support

### SceneSerializer
JSON-based serialization:
- Save/load complete scenes
- Scene graph hierarchy preservation
- Entity and component serialization
- Custom component serializer registration
- Entity reference remapping
- Version compatibility

## Usage Examples

### Creating a Scene

```cpp
#include "scene/Scene.hpp"

// Create a scene
auto scene = std::make_unique<Scene>("MainScene");

// Create nodes
SceneNode* root = scene->getRootNode();
SceneNode* player = scene->createEntityNode("Player", root);

// Set transform
Engine::Transform transform;
transform.position = Engine::vec3(0.0f, 1.0f, 0.0f);
player->setLocalTransform(transform);

// Add components to entity
Entity entity = player->getEntity();
scene->getECS().addComponent(entity, TransformComponent{});
scene->getECS().addComponent(entity, MeshComponent{});
```

### Scene Hierarchy

```cpp
// Create hierarchy
SceneNode* world = scene->createNode("World");
SceneNode* building = scene->createNode("Building", world);
SceneNode* floor1 = scene->createNode("Floor1", building);
SceneNode* room1 = scene->createNode("Room1", floor1);

// Find nodes
SceneNode* foundNode = scene->findNode("Room1");

// Traverse hierarchy
scene->visitAllNodes([](SceneNode* node) {
    std::cout << node->getName() << std::endl;
});
```

### Scene Manager

```cpp
#include "scene/SceneManager.hpp"

SceneManager manager;

// Create scenes
Scene* mainScene = manager.createScene("MainScene");
Scene* pauseMenu = manager.createScene("PauseMenu");

// Set active scene
manager.setActiveScene("MainScene");

// Push overlay (like pause menu)
manager.pushOverlayScene("PauseMenu");

// Update all active scenes
manager.update(deltaTime);

// Pop overlay
manager.popOverlayScene();
```

### Scene Serialization

```cpp
#include "scene/SceneSerializer.hpp"

SceneSerializer serializer;

// Register component serializers
serializer.registerComponentSerializer<TransformComponent>(
    "Transform",
    // Serialize
    [](const TransformComponent& comp) {
        JsonValue json = JsonValue::object();
        // Serialize component data...
        return json;
    },
    // Deserialize
    [](const JsonValue& json, TransformComponent& comp) {
        // Deserialize component data...
    }
);

// Save scene
serializer.saveToFile(*scene, "scenes/main.json");

// Load scene
auto loadedScene = serializer.loadFromFile("scenes/main.json");
```

### Async Scene Loading

```cpp
// Preload scene asynchronously
auto future = manager.preloadSceneAsync("scenes/level2.json");

// Do other work...

// Wait for loading to complete
Scene* level2 = future.get();

// Transition with callback
manager.transitionToScene("level2", []() {
    // Transition effect code here
    std::cout << "Fading out..." << std::endl;
});
```

### Systems Integration

```cpp
// Create systems in scene
class PhysicsSystem : public System {
public:
    void update(float dt) override {
        // Update physics...
    }
};

scene->createSystem<PhysicsSystem>();

// Update all systems
scene->update(deltaTime);
```

## Transform Hierarchy

World transforms are automatically calculated from parent chain:

```cpp
// Parent transform
SceneNode* parent = scene->createNode("Parent");
parent->getLocalTransform().position = Engine::vec3(10, 0, 0);

// Child transform
SceneNode* child = scene->createNode("Child", parent);
child->getLocalTransform().position = Engine::vec3(5, 0, 0);

// World position = (15, 0, 0) (accumulated from parent)
Engine::vec3 worldPos = child->getWorldTransform().position;
```

## Performance Considerations

### Transform Caching
- World transforms are cached and only recalculated when dirty
- Setting any transform marks the node and all children as dirty
- Lazy evaluation: transforms computed on-demand

### Entity-Node Cache
- Fast Entity → SceneNode lookup via hash map
- Automatically rebuilt when invalidated
- O(1) lookup time

### Memory Ownership
- Nodes use unique_ptr for automatic memory management
- Scenes own their ECS instance
- SceneManager owns all scenes

## Integration with ECS

Each scene has its own ECS instance, allowing for:
- Scene-local entities and components
- Independent system execution
- Clean scene transitions (no entity ID conflicts)
- Parallel scene updates (if needed)

## JSON Format

Example scene file structure:

```json
{
  "version": 1,
  "metadata": {
    "name": "MainScene",
    "active": true
  },
  "sceneGraph": [
    {
      "name": "Player",
      "active": true,
      "transform": {
        "position": [0.0, 1.0, 0.0],
        "rotation": [0.0, 0.0, 0.0, 1.0],
        "scale": [1.0, 1.0, 1.0]
      },
      "entity": {
        "id": 123456789,
        "components": [
          {
            "type": "Transform",
            "data": { ... }
          }
        ]
      },
      "children": []
    }
  ]
}
```

## Dependencies

- **CatEngine::ECS**: Entity-Component-System (EntityManager, System, Component)
- **Engine::Math**: Math library (vec3, mat4, Quaternion, Transform)
- **CatEngine::Renderer**: GPU rendering (optional, for rendering integration)

## Building

```bash
# Using CMake
mkdir build && cd build
cmake ..
make CatEngineScene
```

## Future Enhancements

- [ ] Spatial partitioning (octree/BVH) for culling
- [ ] Scene prefabs (reusable node templates)
- [ ] Component references in serialization
- [ ] Binary serialization format for faster loading
- [ ] Scene streaming (load/unload parts of large scenes)
- [ ] Multi-threaded scene updates
- [ ] Integration with nlohmann/json library
- [ ] Scene validation tools
- [ ] Visual scene editor integration

## License

Part of Cat Annihilation Engine - See main project license.
