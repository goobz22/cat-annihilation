/**
 * Cat Engine Scene System - Example Usage
 * Demonstrates all major features of the scene system
 */

#include "Scene.hpp"
#include "SceneManager.hpp"
#include "SceneSerializer.hpp"
#include <iostream>

using namespace CatEngine;
using namespace Engine;

// Example component
struct TransformComponent {
    vec3 position;
    Quaternion rotation;
    vec3 scale;
};

struct MeshComponent {
    uint32_t meshId;
    uint32_t materialId;
};

// Example system
class RenderSystem : public System {
public:
    RenderSystem(int priority = 0) : System(priority) {}

    void update(float dt) override {
        std::cout << "RenderSystem::update(" << dt << ")" << std::endl;

        // Query entities with transform and mesh components
        // auto query = ecs_->query<TransformComponent, MeshComponent>();
        // for (auto [entity, transform, mesh] : query.view()) {
        //     // Render mesh with transform
        // }
    }

    const char* getName() const override { return "RenderSystem"; }
};

// ============================================================================
// Example 1: Basic Scene Creation
// ============================================================================
void example1_basic_scene() {
    std::cout << "\n=== Example 1: Basic Scene Creation ===" << std::endl;

    // Create a scene
    auto scene = std::make_unique<Scene>("GameScene");

    // Create some nodes
    SceneNode* world = scene->createNode("World");
    SceneNode* player = scene->createEntityNode("Player", world);
    SceneNode* enemy = scene->createEntityNode("Enemy", world);

    // Set transforms
    Transform playerTransform;
    playerTransform.position = vec3(0.0f, 1.0f, 0.0f);
    player->setLocalTransform(playerTransform);

    Transform enemyTransform;
    enemyTransform.position = vec3(10.0f, 0.0f, 5.0f);
    enemy->setLocalTransform(enemyTransform);

    // Add components to entities
    Entity playerEntity = player->getEntity();
    scene->getECS().addComponent(playerEntity, TransformComponent{
        vec3(0, 1, 0),
        Quaternion::identity(),
        vec3(1, 1, 1)
    });

    // Print scene stats
    auto stats = scene->getStatistics();
    std::cout << "Scene: " << scene->getName() << std::endl;
    std::cout << "  Nodes: " << stats.nodeCount << std::endl;
    std::cout << "  Entities: " << stats.entityCount << std::endl;
    std::cout << "  Systems: " << stats.systemCount << std::endl;
}

// ============================================================================
// Example 2: Scene Hierarchy
// ============================================================================
void example2_hierarchy() {
    std::cout << "\n=== Example 2: Scene Hierarchy ===" << std::endl;

    auto scene = std::make_unique<Scene>("HierarchyTest");

    // Create a complex hierarchy
    SceneNode* building = scene->createNode("Building");

    // First floor
    SceneNode* floor1 = scene->createNode("Floor1", building);
    SceneNode* room1 = scene->createNode("Room1", floor1);
    SceneNode* desk1 = scene->createNode("Desk", room1);
    SceneNode* chair1 = scene->createNode("Chair", room1);

    // Second floor
    SceneNode* floor2 = scene->createNode("Floor2", building);
    SceneNode* room2 = scene->createNode("Room2", floor2);

    // Set transforms to demonstrate hierarchy
    building->getLocalTransform().position = vec3(100, 0, 0);
    floor1->getLocalTransform().position = vec3(0, 0, 0);
    floor2->getLocalTransform().position = vec3(0, 5, 0);
    room1->getLocalTransform().position = vec3(5, 0, 0);
    desk1->getLocalTransform().position = vec3(1, 0, 1);

    // World position is accumulated from parent chain
    vec3 deskWorldPos = desk1->getWorldTransform().position;
    std::cout << "Desk world position: " << deskWorldPos << std::endl;

    // Find nodes
    SceneNode* found = scene->findNode("Desk");
    if (found) {
        std::cout << "Found node: " << found->getName() << std::endl;
        std::cout << "  Depth: " << found->getDepth() << std::endl;
    }

    // Traverse hierarchy
    std::cout << "\nScene hierarchy:" << std::endl;
    scene->visitAllNodes([](const SceneNode* node) {
        std::string indent(node->getDepth() * 2, ' ');
        std::cout << indent << "- " << node->getName() << std::endl;
    });
}

// ============================================================================
// Example 3: Scene Manager
// ============================================================================
void example3_scene_manager() {
    std::cout << "\n=== Example 3: Scene Manager ===" << std::endl;

    SceneManager manager;

    // Create multiple scenes
    Scene* mainMenu = manager.createScene("MainMenu");
    Scene* level1 = manager.createScene("Level1");
    Scene* level2 = manager.createScene("Level2");
    Scene* pauseMenu = manager.createScene("PauseMenu");

    // Setup main menu
    mainMenu->createNode("Title");
    mainMenu->createNode("StartButton");
    mainMenu->createNode("QuitButton");

    // Setup level 1
    level1->createEntityNode("Player");
    level1->createEntityNode("Enemy1");
    level1->createEntityNode("Enemy2");

    // Set active scene
    manager.setActiveScene("MainMenu");
    std::cout << "Active scene: " << manager.getActiveSceneName() << std::endl;

    // Transition to level
    manager.transitionToScene("Level1", []() {
        std::cout << "Transitioning (fade out, load assets, etc.)..." << std::endl;
    });
    std::cout << "Active scene: " << manager.getActiveSceneName() << std::endl;

    // Push pause menu as overlay
    manager.pushOverlayScene("PauseMenu");
    std::cout << "Overlay scenes: " << manager.getOverlayScenes().size() << std::endl;

    // Update (would update both Level1 and PauseMenu)
    float dt = 0.016f; // 60 FPS
    manager.update(dt);

    // Pop pause menu
    manager.popOverlayScene();

    // Print statistics
    auto stats = manager.getStatistics();
    std::cout << "\nScene Manager Stats:" << std::endl;
    std::cout << "  Total scenes: " << stats.totalScenes << std::endl;
    std::cout << "  Active scene nodes: " << stats.activeSceneNodes << std::endl;
    std::cout << "  Active scene entities: " << stats.activeSceneEntities << std::endl;
}

// ============================================================================
// Example 4: Systems
// ============================================================================
void example4_systems() {
    std::cout << "\n=== Example 4: Systems ===" << std::endl;

    auto scene = std::make_unique<Scene>("SystemTest");

    // Create entities
    for (int i = 0; i < 5; i++) {
        SceneNode* node = scene->createEntityNode("Entity" + std::to_string(i));
        Entity entity = node->getEntity();

        scene->getECS().addComponent(entity, TransformComponent{
            vec3(i * 2.0f, 0, 0),
            Quaternion::identity(),
            vec3(1, 1, 1)
        });

        scene->getECS().addComponent(entity, MeshComponent{0, 0});
    }

    // Add systems
    scene->createSystem<RenderSystem>(100);

    // Update scene (runs all systems)
    float dt = 0.016f;
    scene->update(dt);
}

// ============================================================================
// Example 5: Serialization
// ============================================================================
void example5_serialization() {
    std::cout << "\n=== Example 5: Serialization ===" << std::endl;

    SceneSerializer serializer;

    // Register component serializers
    serializer.registerComponentSerializer<TransformComponent>(
        "Transform",
        // Serialize
        [](const TransformComponent& comp) {
            JsonValue json = JsonValue::object();

            JsonValue pos = JsonValue::array();
            pos.push(JsonValue(comp.position.x));
            pos.push(JsonValue(comp.position.y));
            pos.push(JsonValue(comp.position.z));
            json["position"] = pos;

            return json;
        },
        // Deserialize
        [](const JsonValue& json, TransformComponent& comp) {
            if (json.has("position")) {
                const JsonValue& pos = json["position"];
                comp.position = vec3(
                    static_cast<float>(pos[0].asNumber()),
                    static_cast<float>(pos[1].asNumber()),
                    static_cast<float>(pos[2].asNumber())
                );
            }
        }
    );

    // Create a scene
    auto scene = std::make_unique<Scene>("SavedScene");
    SceneNode* player = scene->createEntityNode("Player");
    player->getLocalTransform().position = vec3(5, 10, 15);

    // Save to string
    std::string json = serializer.saveToString(*scene);
    std::cout << "Serialized scene:" << std::endl;
    std::cout << json << std::endl;

    // In production, save to file:
    // serializer.saveToFile(*scene, "scenes/level1.json");

    // Load from file:
    // auto loadedScene = serializer.loadFromFile("scenes/level1.json");
}

// ============================================================================
// Example 6: Node Manipulation
// ============================================================================
void example6_node_manipulation() {
    std::cout << "\n=== Example 6: Node Manipulation ===" << std::endl;

    auto scene = std::make_unique<Scene>("ManipulationTest");

    // Create nodes
    SceneNode* parent = scene->createNode("Parent");
    SceneNode* child1 = scene->createNode("Child1", parent);
    SceneNode* child2 = scene->createNode("Child2", parent);

    std::cout << "Parent has " << parent->getChildCount() << " children" << std::endl;

    // Remove and re-add child
    auto removed = parent->removeChild("Child1");
    std::cout << "After removal: " << parent->getChildCount() << " children" << std::endl;

    parent->addChild(std::move(removed));
    std::cout << "After re-add: " << parent->getChildCount() << " children" << std::endl;

    // Clone subtree
    auto cloned = parent->clone();
    std::cout << "Cloned node: " << cloned->getName() << std::endl;
    std::cout << "  Children: " << cloned->getChildCount() << std::endl;

    // Active state
    parent->setActive(false);
    std::cout << "Parent active: " << parent->isActive() << std::endl;
    std::cout << "Child1 active in hierarchy: " << child1->isActiveInHierarchy() << std::endl;
}

// ============================================================================
// Main
// ============================================================================
int main() {
    std::cout << "Cat Engine Scene System - Examples" << std::endl;
    std::cout << "===================================" << std::endl;

    try {
        example1_basic_scene();
        example2_hierarchy();
        example3_scene_manager();
        example4_systems();
        example5_serialization();
        example6_node_manipulation();

        std::cout << "\n=== All examples completed successfully ===" << std::endl;
    }
    catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
