# Claude Code Configuration

## Project Context
This is a 3D cat-based survival game built with React Three Fiber, Three.js, and Zustand.

## CRITICAL ARCHITECTURE RULES

⚠️ **READ ARCHITECTURE.md FIRST** - Contains critical state management rules that prevent game-breaking bugs.

### Key Points:
- **NEVER use Zustand for dynamic game entities** (enemies, projectiles, waves)
- **Local React state for real-time updates** (anything in useFrame loops)
- **Zustand only for UI/static state** (health display, inventory, settings)

### Warning Signs:
- If cat flies through terrain → Check for Zustand calls in useFrame()
- If positioning gets corrupted → Look for store.set() calls during animation
- If movement breaks → Verify dynamic entities use local state only

## Quick Reference Files:
- `ARCHITECTURE.md` - Full state management guidelines AND current implementation guide
- `README.md` - Setup instructions and tech stack overview
- `src/components/game/Local*` - Examples of correct local state usage
- Files with "CRITICAL WARNING" comments - Systems that must avoid Zustand

## Current Status:
- Game uses hybrid architecture (Zustand + Local state)
- All dynamic systems converted to local state
- Terrain/positioning bugs resolved
- Wave system working with proper popups