/**
 * Game state persistence using localStorage
 * Saves and loads game progress to maintain state across page refreshes
 */

// Keys for localStorage
const STORAGE_KEYS = {
  GAME_MODE: 'cat-annihilation-game-mode',
  STORY_PROGRESS: 'cat-annihilation-story-progress',
  PLAYER_PROGRESS: 'cat-annihilation-player-progress'
} as const;

// Types for persisted data
interface PersistedStoryProgress {
  isActive: boolean;
  playerClan: 'MistClan' | 'StormClan' | 'EmberClan' | 'FrostClan' | null;
  playerRank: 'outsider' | 'apprentice' | 'warrior' | 'senior_warrior' | 'deputy' | 'leader';
  mentorName: string;
  activeQuests: string[];
  completedQuests: string[];
  storySkills: {
    combat: number;
    hunting: number;
    herbalism: number;
    leadership: number;
    mysticism: number;
    exploration: number;
  };
  clanRelationships: Record<string, 'ally' | 'neutral' | 'hostile'>;
  territoryAccess: string[];
  mysticalConnections: number;
}

interface PersistedPlayerProgress {
  catLevel: number;
  catXp: number;
  weaponSkills: any; // Full weapon skills object
  survivalHighScore: {
    wave: number;
    enemiesKilled: number;
    survivalTime: number;
  };
}

/**
 * Safe localStorage operations with error handling
 */
const safeLocalStorage = {
  get: (key: string): any => {
    try {
      const item = localStorage.getItem(key);
      return item ? JSON.parse(item) : null;
    } catch (error) {
      console.warn(`[PERSISTENCE] Failed to load ${key}:`, error);
      return null;
    }
  },

  set: (key: string, value: any): void => {
    try {
      localStorage.setItem(key, JSON.stringify(value));
    } catch (error) {
      console.warn(`[PERSISTENCE] Failed to save ${key}:`, error);
    }
  },

  remove: (key: string): void => {
    try {
      localStorage.removeItem(key);
    } catch (error) {
      console.warn(`[PERSISTENCE] Failed to remove ${key}:`, error);
    }
  }
};

/**
 * Load persisted game mode
 */
export const loadGameMode = (): 'survival' | 'story' | null => {
  const saved = safeLocalStorage.get(STORAGE_KEYS.GAME_MODE);
  if (saved === 'survival' || saved === 'story') {
    console.log(`🔄 [PERSISTENCE] Restored game mode: ${saved}`);
    return saved;
  }
  return null;
};

/**
 * Save game mode to localStorage
 */
export const saveGameMode = (gameMode: 'survival' | 'story' | null): void => {
  if (gameMode) {
    safeLocalStorage.set(STORAGE_KEYS.GAME_MODE, gameMode);
    console.log(`[PERSISTENCE] Saved game mode: ${gameMode}`);
  } else {
    safeLocalStorage.remove(STORAGE_KEYS.GAME_MODE);
    console.log('[PERSISTENCE] Cleared game mode');
  }
};

/**
 * Load persisted story progress
 */
export const loadStoryProgress = (): Partial<PersistedStoryProgress> | null => {
  const saved = safeLocalStorage.get(STORAGE_KEYS.STORY_PROGRESS);
  if (saved && saved.isActive) {
    console.log('🔄 [PERSISTENCE] Restored story progress:', {
      clan: saved.playerClan,
      rank: saved.playerRank,
      activeQuests: saved.activeQuests?.length || 0,
      completedQuests: saved.completedQuests?.length || 0
    });
    return saved;
  }
  return null;
};

/**
 * Save story progress to localStorage
 */
export const saveStoryProgress = (storyMode: any): void => {
  if (!storyMode.isActive) {
    safeLocalStorage.remove(STORAGE_KEYS.STORY_PROGRESS);
    console.log('[PERSISTENCE] Cleared story progress (story mode inactive)');
    return;
  }

  const progressToSave: PersistedStoryProgress = {
    isActive: storyMode.isActive,
    playerClan: storyMode.playerClan,
    playerRank: storyMode.playerRank,
    mentorName: storyMode.mentorName,
    activeQuests: storyMode.activeQuests,
    completedQuests: storyMode.completedQuests,
    storySkills: storyMode.storySkills,
    clanRelationships: storyMode.clanRelationships,
    territoryAccess: storyMode.territoryAccess,
    mysticalConnections: storyMode.mysticalConnections
  };

  safeLocalStorage.set(STORAGE_KEYS.STORY_PROGRESS, progressToSave);
  console.log('[PERSISTENCE] Saved story progress');
};

/**
 * Load persisted player progress (levels, skills, high scores)
 */
export const loadPlayerProgress = (): Partial<PersistedPlayerProgress> | null => {
  const saved = safeLocalStorage.get(STORAGE_KEYS.PLAYER_PROGRESS);
  if (saved) {
    console.log('🔄 [PERSISTENCE] Restored player progress:', {
      level: saved.catLevel,
      xp: saved.catXp,
      highestWave: saved.survivalHighScore?.wave || 1
    });
    return saved;
  }
  return null;
};

/**
 * Save player progress to localStorage
 */
export const savePlayerProgress = (
  catStats: any,
  weaponSkills: any,
  currentWave: number,
  enemiesKilled: number,
  survivalTimeSeconds: number = 0
): void => {
  const existing = loadPlayerProgress() || {};

  const progressToSave: PersistedPlayerProgress = {
    catLevel: catStats.level,
    catXp: catStats.xp,
    weaponSkills: weaponSkills,
    survivalHighScore: {
      wave: Math.max(existing.survivalHighScore?.wave || 1, currentWave),
      enemiesKilled: Math.max(existing.survivalHighScore?.enemiesKilled || 0, enemiesKilled),
      survivalTime: Math.max(existing.survivalHighScore?.survivalTime || 0, survivalTimeSeconds)
    }
  };

  safeLocalStorage.set(STORAGE_KEYS.PLAYER_PROGRESS, progressToSave);
  console.log('[PERSISTENCE] Saved player progress');
};

/**
 * Clear all persisted data (for reset/new game)
 */
export const clearAllProgress = (): void => {
  Object.values(STORAGE_KEYS).forEach(key => {
    safeLocalStorage.remove(key);
  });
  console.log('[PERSISTENCE] Cleared all persisted data');
};

/**
 * Get storage usage information for debugging
 */
export const getStorageInfo = () => {
  const info: Record<string, any> = {};
  
  Object.entries(STORAGE_KEYS).forEach(([name, key]) => {
    const data = safeLocalStorage.get(key);
    info[name] = {
      exists: !!data,
      size: data ? JSON.stringify(data).length : 0,
      data: data
    };
  });
  
  return info;
};