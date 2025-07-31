# Cat Annihilation Architecture Guidelines

## CRITICAL: State Management Rules

### ⚠️ NEVER USE ZUSTAND FOR DYNAMIC GAME ENTITIES ⚠️

**Problem:** Zustand store `set()` calls during `useFrame()` loops cause the cat to fly through terrain and corrupt 3D positioning.

**Root Cause:** Frequent Zustand updates during Three.js animation loops interfere with React Three Fiber's 3D positioning system.

### State Management Architecture

#### ✅ Use Zustand For:
- **Static UI state**: Player stats, health, inventory display
- **Settings**: Movement speed, graphics options, controls
- **Initial values**: Starting positions, game configuration
- **One-time events**: Level up, item pickup notifications
- **Pause/menu state**: UI that doesn't update every frame

#### ✅ Use Local React State For:
- **Dynamic game entities**: Enemies, projectiles, particles
- **Real-time positions**: Anything that moves every frame
- **Wave/round logic**: Dynamic spawning and completion
- **Animation states**: Attack animations, health changes
- **Collision detection**: Hit detection between moving objects

#### ✅ Use Global Objects For:
- **Cross-system communication**: `globalCollisionData` for projectile-enemy hits
- **Performance-critical data**: State that updates every frame without React re-renders

### Implementation Examples

```tsx
// ❌ WRONG - Causes terrain bugs
useFrame(() => {
  // This will break 3D positioning!
  setProjectilePosition({ x: newX, y: newY, z: newZ });
});

// ✅ CORRECT - Local state in game loop
const [projectiles, setProjectiles] = useState([]);
useFrame(() => {
  // Update local state only
  setProjectiles(prev => prev.map(p => ({ ...p, x: newX })));
});

// ✅ CORRECT - Zustand for UI display
const playerHealth = useGameStore(state => state.player.health);
return <div>Health: {playerHealth}</div>;
```

### File Naming Convention
- Files with dynamic game entities: Include `Local` prefix (e.g., `LocalEnemySystem.tsx`)
- Add warning comments in files that must never use Zustand during gameplay

### Warning Comments Template
```tsx
/**
 * !!!!!!! CRITICAL WARNING !!!!!!!!!
 * 
 * THIS SYSTEM MUST ***NEVER*** CALL ANY ZUSTAND STORE SET() FUNCTIONS
 * DURING RUNTIME OR IT WILL CAUSE THE CAT TO FLY THROUGH THE TERRAIN!!!!!
 * 
 * Uses ONLY local React state and NEVER touches the global game store
 * during useFrame() loops or real-time updates.
 * 
 * !!!!!!! CRITICAL WARNING !!!!!!!!!
 */
```

## Key Lessons Learned

1. **Hybrid approach is optimal**: Zustand + Local state + Global objects
2. **Animation loops are dangerous**: Never call Zustand `set()` in `useFrame()`
3. **Separation of concerns**: UI state vs game state vs performance-critical data
4. **Documentation is critical**: Future developers need these warnings

## Migration Strategy

When adding new dynamic features:
1. **Always start with local React state**
2. **Only use Zustand for UI display of the data**
3. **Use global objects for cross-system communication**
4. **Add warning comments immediately**

This architecture prevents the terrain/positioning bugs while maintaining clean, performant code.

## Current Implementation Guide

### Core Game Components

#### Dynamic Systems (Local React State Only)
- **`LocalEnemySystem.tsx`** - Wave-based dog enemy spawning with health scaling
- **`LocalProjectileSystem.tsx`** - Spell/arrow projectiles (not sword attacks)
- **`GlobalCollisionSystem.tsx`** - Projectile-enemy collision detection
- **`WaveState.ts`** - Wave transition management (no Zustand calls)

#### Static/UI Systems (Zustand Safe)
- **`GameProvider.tsx`** - Zustand store provider
- **`GameInterface.tsx`** - Health bars, inventory display
- **`WaveDisplay.tsx`** - "Round X Survive the Horde" display
- **`WaveTransition.tsx`** - "Wave X Complete!" popup

#### Core Game Systems
- **`BasicScene.tsx`** - Main scene orchestrator, camera, lighting
- **`CatCharacter/index.tsx`** - Player movement, sword attacks
- **`ForestEnvironment.tsx`** - Procedural terrain and trees
- **`Terrain.tsx`** - Ground mesh with collision

### Key Implementation Patterns

#### Enemy System
```tsx
// ✅ CORRECT - Local state only
const [localEnemies, setLocalEnemies] = useState<LocalEnemy[]>([]);

// Register with global collision
globalCollisionData.enemies.push({
  id: enemy.id,
  position: enemy.position,
  onDamage: (damage) => {
    setLocalEnemies(prev => prev.map(/* update health */));
  }
});
```

#### Projectile System
```tsx
// ✅ CORRECT - Local state + global collision
const [projectiles, setProjectiles] = useState<Projectile[]>([]);

useFrame(() => {
  // Update positions locally
  setProjectiles(prev => prev.map(/* update positions */));
});
```

#### Wave Management
```tsx
// ✅ CORRECT - Local state with global communication
const [currentWave, setCurrentWave] = useState(1);

// Update UI through non-Zustand global state
updateWaveState({
  currentWave: newWave,
  isTransition: true
});
```

### ⚠️ CRITICAL: Wave State Subscription System ⚠️

**IMPORTANT:** The wave state system supports multiple subscribers. **DO NOT revert to single callback system!**

#### The Problem We Fixed
Previously, multiple components tried to listen to wave state changes using a single `onStateChange` callback:
- `WaveDisplay.tsx` (permanent "ROUND X" counter) set up a callback
- `BasicScene.tsx` (for wave transition popup) **overwrote** that callback
- Result: Only popup worked, permanent counter stayed at "ROUND 1"

#### ✅ Current Solution - Multi-Subscriber System
```tsx
// WaveState.ts - Support multiple subscribers
export const waveState = {
  isTransition: false,
  currentWave: 1,
  nextWaveEnemies: 0,
  subscribers: [] as Function[],
  onStateChange: null // Kept for backward compatibility
};

// ✅ CORRECT - Use subscription system in components
import { subscribeToWaveState } from '../game/WaveState';

useEffect(() => {
  const updateDisplay = (state) => {
    setCurrentWave(state.currentWave);
    setIsWaveTransition(state.isTransition);
  };
  
  const unsubscribe = subscribeToWaveState(updateDisplay);
  return unsubscribe; // Clean up on unmount
}, []);
```

#### ❌ DO NOT DO THIS - Single Callback (Broken)
```tsx
// ❌ WRONG - This overwrites other components' callbacks!
waveState.onStateChange = myCallback; // Breaks other components!
```

#### Current Implementation Status
- **WaveDisplay.tsx**: Uses new subscription system ✅
- **BasicScene.tsx**: Uses legacy callback (compatibility) ✅  
- **Both components**: Receive wave updates simultaneously ✅

#### Testing Wave Display Fix
1. Start game → Permanent counter shows "ROUND 1"
2. Complete wave 1 → Wave transition popup appears
3. After popup → Permanent counter updates to "ROUND 2" ✅
4. Console shows: `🌊 Notifying X subscribers` ✅

**Never revert the subscription system - it's critical for proper wave display updates!**

### File Structure
```
src/
├── components/
│   ├── game/
│   │   ├── Local*System.tsx     # Dynamic systems
│   │   ├── CatCharacter/        # Player character
│   │   ├── ForestEnvironment.tsx
│   │   ├── Terrain.tsx
│   │   ├── BasicScene.tsx
│   │   ├── WaveState.ts         # Global non-Zustand state
│   │   └── GlobalCollisionSystem.tsx
│   └── ui/
│       ├── GameInterface.tsx    # Zustand-connected UI
│       ├── WaveDisplay.tsx      # UI display only
│       └── WaveTransition.tsx   # Popup component
├── lib/
│   └── store/
│       └── gameStore.ts         # Zustand store (UI only)
└── hooks/
    └── useGameLoop.ts           # Game loop management
```

### Performance Considerations
- **Collision Detection**: Global object updates without React re-renders
- **Enemy Spawning**: Staggered spawning (200ms intervals) to prevent frame drops
- **Projectile Cleanup**: Remove projectiles beyond 500 units distance
- **Wave Transitions**: 4-second delay between waves for proper UI timing

### Testing Checklist
1. ✅ Cat moves smoothly without flying through terrain
2. ✅ Enemies spawn in waves with proper scaling
3. ✅ Projectiles damage enemies (spells: 20, arrows: 30)
4. ✅ Sword attacks work on spacebar (25 damage, 2-unit range)
5. ✅ Wave completion shows popup with next wave info
6. ✅ Wave display updates to correct round number
7. ✅ No Zustand calls in useFrame loops