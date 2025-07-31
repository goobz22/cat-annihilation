/**
 * Game Configuration Constants
 * Centralized configuration to avoid magic numbers throughout the codebase
 */

export const GAME_CONFIG = {
  // Player Configuration
  PLAYER: {
    MOVEMENT_SPEED: 6,
    RUN_SPEED: 12,
    TURN_SPEED: 4.25,
    CAMERA_DISTANCE: 15,
    CAMERA_HEIGHT: 12,
    COLLISION_RADIUS: 0.5,
  },

  // Enemy Configuration
  ENEMY: {
    SPEED: 1.5,
    ATTACK_DAMAGE: 15,
    ATTACK_RANGE: 1.2,
    ATTACK_COOLDOWN: 1000, // milliseconds
    SEPARATION_RADIUS: 1.5,
    SEPARATION_FORCE: 3.0,
    BASE_HEALTH: 100,
    HEALTH_PER_WAVE: 20,
  },

  // Weapons Configuration
  WEAPONS: {
    SWORD: {
      RANGE: 2.0,
      BASE_DAMAGE: 40,
      DAMAGE_PER_LEVEL: 10,
      XP_PER_HIT: 10,
      ATTACK_DURATION: 500, // milliseconds
    },
    BOW: {
      BASE_DAMAGE: 25,
      DAMAGE_PER_LEVEL: 6,
      XP_PER_HIT: 10,
      PROJECTILE_SPEED: 15,
      ATTACK_DURATION: 300,
    },
    SHIELD: {
      RANGE: 1.5,
      DAMAGE: 25,
      ATTACK_DURATION: 600,
    },
    MAGIC: {
      BASE_DAMAGE: 20,
      DAMAGE_PER_LEVEL: 8,
      XP_PER_HIT: 10,
      PROJECTILE_SPEED: 8,
    },
  },

  // Projectile Configuration
  PROJECTILES: {
    UPDATE_FREQUENCY: 120, // FPS for collision detection
    MAX_DISTANCE: 50, // Units before auto-removal
    COLLISION_RADIUS: 1.0,
    SPAWN_OFFSET: 0.8, // Distance in front of player
  },

  // Wave System Configuration
  WAVES: {
    BASE_ENEMIES: 3,
    ENEMIES_PER_WAVE: 2,
    ENEMY_MULTIPLIER: 1.5,
    TRANSITION_DURATION: 3000, // milliseconds
    SPAWN_DISTANCE_MIN: 8,
    SPAWN_DISTANCE_MAX: 15,
    SPAWN_DELAY: 200, // milliseconds between spawns
  },

  // Experience and Leveling
  EXPERIENCE: {
    ENEMY_KILL_XP: 5,
    WAVE_BONUS_BASE: 25,
    WAVE_BONUS_PER_LEVEL: 5,
    MAX_LEVEL: 99,
  },

  // Cat Abilities
  CAT_ABILITIES: {
    REGENERATION_LEVEL: 5,
    REGENERATION_RATE: 2, // HP per second
    AGILITY_LEVEL: 10,
    AGILITY_MULTIPLIER: 1.25,
    NINE_LIVES_LEVEL: 15,
    NINE_LIVES_REVIVE_PERCENT: 0.3,
    PREDATOR_INSTINCT_LEVEL: 20,
    ALPHA_STRIKE_LEVEL: 25,
    HEALTH_PER_LEVEL: 20,
  },

  // UI Configuration
  UI: {
    HOTBAR_SLOTS: 7,
    INVENTORY_BAG_GRID_COLS: 4,
    EQUIPMENT_GRID_COLS: 6,
    SPELLBOOK_GRID_COLS: 3,
    UPDATE_THROTTLE: 16, // milliseconds (~60fps)
  },

  // Performance Configuration
  PERFORMANCE: {
    MAX_ENEMIES: 50,
    MAX_PROJECTILES: 100,
    COLLISION_UPDATE_INTERVAL: 8, // milliseconds
    POSITION_UPDATE_THRESHOLD: 0.01,
    ANIMATION_UPDATE_THRESHOLD: 0.01,
  },

  // Day/Night Cycle
  DAY_CYCLE: {
    DAY_DURATION: 120, // minutes
    NIGHT_DURATION: 40, // minutes
    START_TIME: 0.5, // noon
  },

  // Input Configuration
  INPUT: {
    DEFAULT_SENSITIVITY: 1.0,
    MIN_SENSITIVITY: 0.1,
    MAX_SENSITIVITY: 3.0,
    SENSITIVITY_STEP: 0.1,
  },
} as const;

export const ITEM_TYPES = {
  SPELL: 'spell',
  WEAPON: 'weapon',
  CONSUMABLE: 'consumable',
} as const;

export const ELEMENTS = {
  WATER: 'water',
  AIR: 'air',
  EARTH: 'earth',
  FIRE: 'fire',
} as const;

export const WEAPON_TYPES = {
  SWORD: 'sword',
  BOW: 'bow',
  SHIELD: 'shield',
} as const;

export const GAME_STATES = {
  LOADING: 'loading',
  PLAYING: 'playing',
  PAUSED: 'paused',
  GAME_OVER: 'game_over',
  WAVE_TRANSITION: 'wave_transition',
} as const;

// Utility function to calculate XP requirements
export const calculateXPForLevel = (level: number): number => {
  let total = 0;
  for (let i = 1; i < level; i++) {
    total += Math.floor(i + 300 * Math.pow(2, i / 7));
  }
  return Math.floor(total / 2.5);
};

export const calculateCatXPForLevel = (level: number): number => {
  let total = 0;
  for (let i = 1; i < level; i++) {
    total += Math.floor(i + 500 * Math.pow(2, i / 6));
  }
  return Math.floor(total / 5.4);
};