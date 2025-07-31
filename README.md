# Cat Annihilation (Single-Player)

A single-player 3D cat survival game built with React Three Fiber, Three.js, and Bun.

## Setup

1. Install dependencies: `bun install`
2. Run dev server: `bun run dev`
3. Open http://localhost:3000 (or whatever port Bun uses)

## Build

`bun run build`

Outputs to dist/.

## Development

⚠️ **IMPORTANT**: Read `ARCHITECTURE.md` before making changes - contains critical state management rules.

### Key Architecture Rules:
- **NEVER use Zustand for dynamic game entities** (enemies, projectiles, waves)
- **Use local React state for real-time updates** (anything in useFrame loops)
- **Zustand only for UI/static state** (health display, inventory, settings)

### Game Features:
- 3D cat character with movement and combat
- Wave-based enemy spawning system
- Projectile and sword combat
- Forest environment with procedural trees
- Inventory and equipment system
- Dynamic terrain with collision detection

### Tech Stack:
- **React Three Fiber** - 3D rendering
- **Three.js** - 3D graphics engine
- **Zustand** - UI state management (with restrictions)
- **Bun** - Package manager and runtime
- **TypeScript** - Type safety

See `CLAUDE.md` for Claude Code context and `.cursorrules` for Cursor AI rules. 