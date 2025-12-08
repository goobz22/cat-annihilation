# Enemy System and Wave Management Implementation

## Overview
Complete implementation of the enemy AI, projectile, wave, and health management systems for Cat Annihilation.

## Created Files

### Components (/game/components/)
1. **HealthComponent.hpp**
   - Health tracking (current/max)
   - Invincibility frames
   - Death state handling
   - Health regeneration support

2. **EnemyComponent.hpp**
   - Enemy types: Dog, BigDog, FastDog, BossDog
   - AI states: Idle, Chasing, Attacking, Dead
   - Target tracking
   - Attack parameters (damage, cooldown, range)
   - Aggro range configuration

3. **ProjectileComponent.hpp**
   - Projectile types: Spell, Arrow, EnemyAttack
   - Velocity-based movement
   - Damage and lifetime tracking
   - Owner reference (prevent self-damage)
   - Homing behavior support

4. **MovementComponent.hpp**
   - Velocity and acceleration
   - Speed control with modifiers
   - Jump mechanics
   - Gravity support
   - Ground state tracking

### Systems (/game/systems/)

1. **EnemyAISystem** (hpp + cpp)
   - State machine implementation
   - Pathfinding (direct movement toward target)
   - Aggro range detection
   - Attack logic with cooldowns
   - Face target rotation
   - Priority: 100

2. **ProjectileSystem** (hpp + cpp)
   - Projectile movement and physics
   - Lifetime management
   - Collision detection (sphere-based)
   - Damage application
   - Homing behavior
   - Hit effect spawning
   - Priority: 50

3. **WaveSystem** (hpp + cpp)
   - Wave progression management
   - Enemy spawning with delays
   - Configurable wave parameters
   - Health scaling per wave
   - Boss waves (every N waves)
   - Spawn positioning (circular around player)
   - Callbacks for wave events
   - Priority: 200

4. **HealthSystem** (hpp + cpp)
   - Health updates and damage processing
   - Death handling (enemies vs player)
   - Invincibility frames
   - Health regeneration
   - Death animation timing
   - Callbacks for death/damage events
   - Priority: 10

### Entities (/game/entities/)

1. **DogEntity** (hpp + cpp)
   Factory for creating enemy dogs:
   - **Dog**: 50 HP, 6 speed, 10 damage
   - **BigDog**: 100 HP (2x), 4.2 speed (0.7x), 15 damage (1.5x)
   - **FastDog**: 25 HP (0.5x), 9 speed (1.5x), 7.5 damage (0.75x)
   - **BossDog**: 300 HP, 5 speed, 25 damage
   - Health multiplier support for wave scaling

2. **ProjectileEntity** (hpp + cpp)
   Factory for creating projectiles:
   - **Spell**: 20 damage, 30 speed, 3s lifetime
   - **Arrow**: 30 damage, 40 speed, 2s lifetime
   - **EnemyAttack**: 15 damage, 20 speed, 1s lifetime
   - Homing projectile variant support

## System Integration

### Enemy AI Flow
1. **Idle State**: Wait and check for player in aggro range
2. **Chasing State**: Move toward player, switch to attack when in range
3. **Attacking State**: Deal damage with cooldown, face player
4. **Dead State**: Play death animation, handled by HealthSystem

### Wave System Flow
1. **Transition**: Wait between waves, show UI
2. **Spawning**: Spawn enemies with delays
3. **InProgress**: Track enemy deaths
4. **Completed**: Trigger callbacks, start transition

### Projectile Flow
1. Move by velocity * deltaTime
2. Update homing (if enabled)
3. Check collisions:
   - Player projectiles → Enemies
   - Enemy projectiles → Player
4. Apply damage on hit
5. Destroy on hit or timeout

### Health Management
1. Update invincibility frames
2. Process damage events
3. Handle death:
   - Enemies: Spawn loot, add score
   - Player: Trigger game over
4. Update regeneration
5. Play death animations

## Configuration

### Wave Configuration (WaveConfig)
```cpp
baseEnemyCount = 5           // Starting enemies
enemyCountMultiplier = 1.5   // Increase per wave
healthScalingPerWave = 0.1   // 10% HP increase per wave
spawnDelay = 0.5s            // Delay between spawns
transitionDelay = 3.0s       // Delay between waves
bossWaveInterval = 5         // Boss every 5 waves
spawnRadius = 20.0f          // Spawn distance from player
minSpawnDistance = 10.0f     // Minimum spawn distance
```

### Enemy Stats

| Type     | Health | Speed | Damage | Scale |
|----------|--------|-------|--------|-------|
| Dog      | 50     | 6.0   | 10     | 1.0   |
| BigDog   | 100    | 4.2   | 15     | 1.5   |
| FastDog  | 25     | 9.0   | 7.5    | 0.8   |
| BossDog  | 300    | 5.0   | 25     | 2.0   |

### Projectile Stats

| Type         | Damage | Speed | Lifetime | Radius |
|--------------|--------|-------|----------|--------|
| Spell        | 20     | 30    | 3.0s     | 0.3    |
| Arrow        | 30     | 40    | 2.0s     | 0.2    |
| EnemyAttack  | 15     | 20    | 1.0s     | 0.25   |

## Usage Example

```cpp
// Initialize ECS and systems
CatEngine::ECS ecs;

// Create systems (in priority order)
auto* healthSystem = ecs.createSystem<HealthSystem>(10);
auto* projectileSystem = ecs.createSystem<ProjectileSystem>(50);
auto* enemyAISystem = ecs.createSystem<EnemyAISystem>(100);
auto* waveSystem = ecs.createSystem<WaveSystem>(200);

// Set up player entity (assumed to exist)
CatEngine::Entity player = /* ... */;
waveSystem->setPlayer(player);

// Configure wave system
WaveConfig config;
config.baseEnemyCount = 5;
config.enemyCountMultiplier = 1.5f;
waveSystem->setConfig(config);

// Set callbacks
waveSystem->setOnWaveStart([](int wave) {
    std::cout << "Wave " << wave << " started!" << std::endl;
});

waveSystem->setOnWaveComplete([](int wave) {
    std::cout << "Wave " << wave << " completed!" << std::endl;
});

// Start waves
waveSystem->startWaves();

// Game loop
while (running) {
    float dt = getDeltaTime();
    ecs.update(dt);  // Updates all systems
}

// Create enemy manually
auto enemy = DogEntity::createBigDog(&ecs,
                                     Engine::vec3(10, 0, 10),
                                     player);

// Fire projectile
auto projectile = ProjectileEntity::createSpell(&ecs,
                                                playerPos,
                                                aimDirection,
                                                player);

// Create homing projectile
auto homingProjectile = ProjectileEntity::createHoming(&ecs,
                                                       ProjectileType::Spell,
                                                       position,
                                                       direction,
                                                       owner,
                                                       target,
                                                       5.0f); // homing strength
```

## Architecture Notes

### Namespaces
- **CatEngine**: Engine ECS components (Entity, System, etc.)
- **Engine**: Math types (vec3, Transform, Quaternion)
- **CatGame**: Game-specific components and systems

### Component Design
All components are designed to be trivially copyable or move constructible for optimal ECS performance.

### System Priorities
Lower values execute first:
1. HealthSystem (10) - Process damage/death first
2. ProjectileSystem (50) - Move projectiles and detect hits
3. EnemyAISystem (100) - Update enemy behavior
4. WaveSystem (200) - Manage wave spawning last

### Thread Safety
Systems are designed to be single-threaded. If multi-threading is needed:
- Use separate read-only query passes
- Defer entity destruction to end of frame
- Use atomic operations for shared state

## TODO/Future Enhancements

1. **Renderer Integration**
   - Add mesh/material components to entities
   - Implement death animations
   - Add hit effects and particles

2. **Advanced AI**
   - Obstacle avoidance
   - Pathfinding with navigation mesh
   - Group behavior (formations, flanking)

3. **Loot System**
   - Spawn collectibles on enemy death
   - Experience/currency drops

4. **Score Tracking**
   - Integrate with game state
   - High score persistence

5. **Audio Integration**
   - Attack sounds
   - Death sounds
   - Wave completion audio cues

## Dependencies
- Engine ECS system (/engine/ecs/)
- Engine math library (/engine/math/)
- C++20 standard library

## Testing
Systems can be tested individually:
1. Create minimal ECS with required components
2. Spawn test entities
3. Run system update with fixed deltaTime
4. Verify expected behavior

All systems follow RAII and can be safely destroyed without leaks.
