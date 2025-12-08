# Scene System Quick Reference

## Include Files
```cpp
#include "scene/SceneNode.hpp"     // For scene graph nodes
#include "scene/Scene.hpp"         // For scene management
#include "scene/SceneManager.hpp"  // For multi-scene apps
#include "scene/SceneSerializer.hpp" // For save/load
```

## Common Operations

### Create a Scene
```cpp
auto scene = std::make_unique<Scene>("MyScene");
```

### Create Nodes
```cpp
// Basic node
SceneNode* node = scene->createNode("NodeName");

// Node with entity
SceneNode* entity = scene->createEntityNode("EntityName");

// With parent
SceneNode* child = scene->createNode("Child", parentNode);
```

### Transform Management
```cpp
// Set local transform
Transform t;
t.position = vec3(1, 2, 3);
t.rotation = Quaternion::fromEuler(0, 45, 0);
t.scale = vec3(1, 1, 1);
node->setLocalTransform(t);

// Get world transform
Transform worldT = node->getWorldTransform();
mat4 worldMat = node->getWorldMatrix();
```

### Hierarchy Operations
```cpp
// Add child
parent->addChild(std::move(childNode));

// Remove child
auto removed = parent->removeChild("ChildName");

// Find nodes
SceneNode* found = scene->findNode("NodeName");
SceneNode* foundRecursive = node->findChildRecursive("DeepChild");

// Iterate children
for (auto* child : parent->getChildren()) {
    // Process child
}
```

### Entity & Components
```cpp
// Create entity with node
SceneNode* node = scene->createEntityNode("Player");
Entity entity = node->getEntity();

// Add components
scene->getECS().addComponent(entity, TransformComponent{});
scene->getECS().addComponent(entity, MeshComponent{});

// Get components
auto* transform = scene->getECS().getComponent<TransformComponent>(entity);

// Find node by entity
SceneNode* node = scene->findNodeByEntity(entity);
```

### Systems
```cpp
// Create system
scene->createSystem<PhysicsSystem>();
scene->createSystem<RenderSystem>(100); // with priority

// Update all systems
scene->update(deltaTime);
```

### Scene Manager
```cpp
SceneManager manager;

// Create scenes
manager.createScene("MainMenu");
manager.createScene("Level1");

// Switch scenes
manager.setActiveScene("MainMenu");
manager.transitionToScene("Level1", []() {
    // Transition callback
});

// Overlay scenes
manager.pushOverlayScene("PauseMenu");
manager.popOverlayScene();

// Update
manager.update(deltaTime);
```

### Serialization
```cpp
SceneSerializer serializer;

// Register component serializer
serializer.registerComponentSerializer<MyComponent>(
    "MyComponent",
    [](const MyComponent& c) { return JsonValue::object(); },
    [](const JsonValue& j, MyComponent& c) { }
);

// Save
serializer.saveToFile(*scene, "scene.json");

// Load
auto scene = serializer.loadFromFile("scene.json");
```

### Traversal
```cpp
// Depth-first
scene->visitAllNodes([](SceneNode* node) {
    std::cout << node->getName() << std::endl;
});

// Breadth-first
node->visitBreadthFirst([](SceneNode* node) {
    // Process node
});
```

## Common Patterns

### Game Level Structure
```cpp
auto level = std::make_unique<Scene>("Level1");

SceneNode* world = level->createNode("World");
SceneNode* terrain = level->createNode("Terrain", world);
SceneNode* buildings = level->createNode("Buildings", world);
SceneNode* characters = level->createNode("Characters", world);

SceneNode* player = level->createEntityNode("Player", characters);
```

### UI Overlay
```cpp
SceneManager manager;
manager.setActiveScene("Game");
manager.pushOverlayScene("HUD");
manager.pushOverlayScene("PauseMenu"); // Stacked on top
```

### Prefab Pattern
```cpp
// Clone a node tree
auto prefab = templateNode->clone();
scene->getRootNode()->addChild(std::move(prefab));
```

### Component Query
```cpp
auto query = scene->getECS().query<Transform, Mesh>();
for (auto [entity, transform, mesh] : query.view()) {
    // Process entities with both components
}
```

## Performance Tips

1. **Transform Updates**: Only dirty-flagged nodes recalculate world transforms
2. **Entity Lookup**: Use cached Entity→Node mapping for O(1) access
3. **Hierarchy Depth**: Keep hierarchies shallow for faster transform updates
4. **System Priority**: Lower priority = runs first
5. **Overlay Scenes**: Minimize overlay count for better performance

## Common Pitfalls

1. Don't store raw pointers to nodes (may be invalidated)
2. Always check if entity is valid before use
3. Remember to register component serializers before saving
4. Active state affects isActiveInHierarchy() for all children
5. World transform changes when parent transform changes

## File Locations
```
/home/user/cat-annihilation/engine/scene/
├── SceneNode.hpp/cpp          # Hierarchy nodes
├── Scene.hpp/cpp              # Scene container
├── SceneManager.hpp/cpp       # Multi-scene
├── SceneSerializer.hpp/cpp    # Save/load
├── CMakeLists.txt            # Build config
├── README.md                 # Full docs
├── QUICK_REFERENCE.md        # This file
└── example_usage.cpp         # Examples
```

## Getting Help

- See `README.md` for detailed documentation
- See `example_usage.cpp` for working examples
- See `IMPLEMENTATION_SUMMARY.md` for architecture details
