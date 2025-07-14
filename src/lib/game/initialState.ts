/**
 * Initial state values for the game
 */

/**
 * Initial player position
 */
export const initialPlayerPosition = {
  x: 0,
  y: 0,
  z: 0,
  rotation: 0,
};

/**
 * Initial player cat data
 */
export const initialPlayerCat = {
  name: 'Shadowpaw',
  level: 1,
  experience: 0,
  health: 100,
  maxHealth: 100,
  attack: 10,
  defense: 5,
  speed: 5,
  currency: 0,
  inventory: {
    items: [],
    maxSize: 20,
  },
  equipment: {
    armor: null,
    claws: null,
    accessory: null,
  },
};

/**
 * Initial world data
 */
export const initialWorldData = {
  name: 'Cat Annihilation World',
  description: 'A vast wilderness for warrior cats to explore and battle',
  terrain: {
    heightmap: '/assets/terrain/default-heightmap.png',
    size: 2000,
    maxHeight: 100,
  },
  zones: [
    {
      name: 'Starting Area',
      type: 'safe',
      bounds: {
        minX: -100,
        maxX: 100,
        minZ: -100,
        maxZ: 100,
      },
      isPvpAtNight: false,
    },
    {
      name: 'Battle Grounds',
      type: 'pvp',
      bounds: {
        minX: 200,
        maxX: 400,
        minZ: 200,
        maxZ: 400,
      },
      isPvpAtNight: true,
    },
    {
      name: 'Forest',
      type: 'normal',
      bounds: {
        minX: -300,
        maxX: -100,
        minZ: -300,
        maxZ: -100,
      },
      isPvpAtNight: true,
    },
  ],
  dayCycleMinutes: 120,
  nightCycleMinutes: 40,
  currentTime: 0.5, // Start at noon
  isNight: false,
}; 