import { create } from 'zustand';
import { 
  loadGameMode, 
  saveGameMode, 
  loadStoryProgress, 
  saveStoryProgress,
  loadPlayerProgress,
  savePlayerProgress
} from './gameStatePersistence';

/**
 * Interface for the game day/night cycle
 */
interface IDayCycle {
  currentTime: number; // 0-1 value (0 = midnight, 0.5 = noon)
  isNight: boolean;
  dayCycleMinutes: number;
  nightCycleMinutes: number;
}

/**
 * Interface for player position and movement
 */
interface IPosition {
  x: number;
  y: number;
  z: number;
  rotation: number;
}



/**
 * Interface for inventory items
 */
interface IInventoryItem {
  id: string;
  name: string;
  type: 'spell' | 'weapon' | 'consumable';
  element?: 'water' | 'air' | 'earth' | 'fire';
  icon: string;
  color: string;
}

/**
 * Interface for the player state
 */
interface IPlayerState {
  cat: any; // Simplified
  position: IPosition;
  isMoving: boolean;
  isRunning: boolean;
  isJumping: boolean;
  isAttacking: boolean;
  isDefending: boolean;
  health: number;
  maxHealth: number;
  inventory: (IInventoryItem | null)[];
  activeSlot: number;
  customization?: {
    primaryColor: string;
    secondaryColor?: string;
    eyeColor: string;
    noseColor: string;
    pawColor?: string;
    pattern?: 'solid' | 'tabby' | 'calico' | 'tuxedo' | 'siamese' | 'spots';
    patternColor?: string;
    earSize?: 'small' | 'normal' | 'large';
    tailLength?: 'short' | 'normal' | 'long';
    furLength?: 'short' | 'medium' | 'long';
    bodyType?: 'slim' | 'normal' | 'chubby';
    collar?: {
      color: string;
      hasTag?: boolean;
    };
    scars?: Array<{
      position: 'eye' | 'ear' | 'body';
      side?: 'left' | 'right';
    }>;
  };
}



/**
 * Interface for weapon skills
 */
interface IWeaponSkill {
  level: number;
  xp: number;
  xpToNextLevel: number;
}

interface IWeaponSkills {
  sword: IWeaponSkill;
  bow: IWeaponSkill;
  magic: {
    water: IWeaponSkill;
    air: IWeaponSkill;
    earth: IWeaponSkill;
    fire: IWeaponSkill;
  };
}

/**
 * Interface for cat-specific stats and abilities
 */
interface ICatStats {
  level: number;
  xp: number;
  xpToNextLevel: number;
  baseHealth: number;
  healthBonus: number; // Health gained from leveling
  abilities: {
    regeneration: boolean; // Level 5: Slow health regen
    agility: boolean; // Level 10: Faster movement
    nineLives: boolean; // Level 15: Survive death once per wave
    predatorInstinct: boolean; // Level 20: See enemy health bars
    alphaStrike: boolean; // Level 25: Critical hit chance
  };
  nineLivesUsed: boolean; // Track if nine lives was used this wave
}

/**
 * Interface for quest system
 */
interface IQuestObjective {
  id: string;
  type: 'kill' | 'collect' | 'visit' | 'talk' | 'survive' | 'escort';
  target: string;
  count: number;
  currentCount: number;
  description: string;
}

interface IQuestReward {
  type: 'xp' | 'item' | 'rank' | 'territory' | 'ability';
  value: string | number;
  amount?: number;
  description: string;
}

interface IQuest {
  id: string;
  title: string;
  description: string;
  category: 'novice' | 'warrior' | 'elite' | 'daily';
  prerequisites: string[];
  objectives: IQuestObjective[];
  rewards: IQuestReward[];
  status: 'locked' | 'available' | 'active' | 'completed';
  giver?: string; // NPC who gives the quest
  location?: string; // Where quest takes place
}

/**
 * Interface for clan system
 */
interface IClan {
  id: 'MistClan' | 'StormClan' | 'EmberClan' | 'FrostClan';
  name: string;
  territory: string;
  specialty: string;
  values: string[];
  sacredSite: string;
  relationship: 'ally' | 'neutral' | 'hostile';
}

/**
 * Interface for story mode skill progression
 */
interface IStorySkills {
  combat: number;
  hunting: number;
  herbalism: number;
  leadership: number;
  mysticism: number;
  exploration: number;
}

/**
 * Interface for NPC dialog system
 */
interface IDialog {
  isOpen: boolean;
  npcName: string;
  npcRole: string;
  message: string;
  options?: IDialogOption[];
}

interface IDialogOption {
  id: string;
  text: string;
  action?: () => void;
}

/**
 * Interface for story mode state
 */
interface IStoryModeState {
  isActive: boolean;
  playerClan: 'MistClan' | 'StormClan' | 'EmberClan' | 'FrostClan' | null;
  playerRank: 'outsider' | 'apprentice' | 'warrior' | 'senior_warrior' | 'deputy' | 'leader';
  mentorName: string;
  quests: IQuest[];
  activeQuests: string[]; // Quest IDs currently being pursued
  completedQuests: string[];
  storySkills: IStorySkills;
  clanRelationships: Record<string, 'ally' | 'neutral' | 'hostile'>;
  territoryAccess: string[]; // Areas player can access
  mysticalConnections: number; // Connection to ancestor spirits (0-100)
  questBookOpen: boolean;
  dialog: IDialog;
}

/**
 * Interface for the game state
 */
interface IGameState {
  isWorldLoaded: boolean;
  world: any;
  dayCycle: IDayCycle;
  connectionError: string | null;
  player: IPlayerState;
  availableSpells: IInventoryItem[];
  inventoryBag: IInventoryItem[];
  enemies: Array<{ id: string; position: IPosition; health: number }>;
  isGameOver: boolean;
  currentWave: number;
  enemiesKilled: number;
  isWaveTransition: boolean;
  weaponSkills: IWeaponSkills;
  catStats: ICatStats;
  isPaused: boolean;
  isMenuPaused: boolean;
  gameMode: 'survival' | 'story' | null;
  storyMode: IStoryModeState;
  setWorldLoaded: (loaded: boolean) => void;
  setWorld: (world: any) => void;
  setDayCycle: (cycle: IDayCycle) => void;
  setConnectionError: (error: string | null) => void;
  setPlayer: (newPlayer: Partial<IPlayerState>) => void;
  setPlayerPosition: (position: Partial<IPosition>) => void;
  setPlayerMoving: (isMoving: boolean) => void;
  setPlayerRunning: (isRunning: boolean) => void;
  setPlayerJumping: (isJumping: boolean) => void;
  setPlayerAttacking: (isAttacking: boolean) => void;
  setPlayerDefending: (isDefending: boolean) => void;
  setPlayerHealth: (health: number) => void;
  damagePlayer: (amount: number) => void;
  addEnemy: (enemy: { id: string; position: IPosition; health: number }) => void;
  removeEnemy: (id: string) => void;
  damageEnemy: (id: string, amount: number) => void;
  updateEnemyPosition: (id: string, position: IPosition) => void;
  setGameOver: (over: boolean) => void;
  setActiveSlot: (slot: number) => void;
  setInventorySlot: (slot: number, item: IInventoryItem | null) => void;
  removeFromInventoryBag: (itemId: string) => void;
  addToInventoryBag: (item: IInventoryItem) => void;
  setCurrentWave: (wave: number) => void;
  incrementEnemiesKilled: () => void;
  setWaveTransition: (isTransition: boolean) => void;
  addWeaponXP: (weapon: 'sword' | 'bow' | 'magic', xp: number, element?: 'water' | 'air' | 'earth' | 'fire') => void;
  addCatXP: (xp: number) => void;
  setCatHealth: (health: number) => void;
  resetNineLives: () => void;
  setPaused: (paused: boolean) => void;
  setMenuPaused: (paused: boolean) => void;
  setGameMode: (mode: 'survival' | 'story') => void;
  setPlayerClan: (clan: 'MistClan' | 'StormClan' | 'EmberClan' | 'FrostClan' | null) => void;
  setPlayerRank: (rank: 'outsider' | 'apprentice' | 'warrior' | 'senior_warrior' | 'deputy' | 'leader') => void;
  addQuest: (quest: IQuest) => void;
  updateQuest: (questId: string, updates: Partial<IQuest>) => void;
  completeQuest: (questId: string) => void;
  activateQuest: (questId: string) => void;
  updateQuestObjective: (questId: string, objectiveId: string, progress: number) => void;
  addStoryXP: (skill: keyof IStorySkills, amount: number) => void;
  setClanRelationship: (clan: string, relationship: 'ally' | 'neutral' | 'hostile') => void;
  toggleQuestBook: () => void;
  showDialog: (npcName: string, npcRole: string, message: string, options?: IDialogOption[]) => void;
  closeDialog: () => void;
}

/**
 * Initial day cycle state
 */
const initialDayCycle: IDayCycle = {
  currentTime: 0.5, // Start at noon
  isNight: false,
  dayCycleMinutes: 120,
  nightCycleMinutes: 40,
};

/**
 * Available spells for the spellbook
 */
const availableSpells: IInventoryItem[] = [
  { id: 'water-spell', name: 'Water Spell', type: 'spell', element: 'water', icon: '💧', color: '#00ffff' },
  { id: 'air-spell', name: 'Air Spell', type: 'spell', element: 'air', icon: '💨', color: '#e0e0e0' },
  { id: 'earth-spell', name: 'Earth Spell', type: 'spell', element: 'earth', icon: '🟫', color: '#8b4513' },
  { id: 'fire-spell', name: 'Fire Spell', type: 'spell', element: 'fire', icon: '🔥', color: '#ff4500' },
];

/**
 * Initial inventory items (quick slots - 9 slots total)
 */
const initialInventory: (IInventoryItem | null)[] = [
  { id: 'water-spell', name: 'Water Spell', type: 'spell', element: 'water', icon: '💧', color: '#00ffff' },
  { id: 'sword', name: 'Sword', type: 'weapon', icon: '⚔️', color: '#c0c0c0' },
  { id: 'bow', name: 'Bow', type: 'weapon', icon: '🏹', color: '#8b4513' },
  { id: 'shield', name: 'Shield', type: 'weapon', icon: '🛡️', color: '#c0c0c0' },
  null,
  null,
  null,
  null,
  null,
];

/**
 * Initial inventory bag items
 */
const initialInventoryBag: IInventoryItem[] = [
  { id: 'health-potion', name: 'Health Potion', type: 'consumable', icon: '🧪', color: '#ff0000' },
  { id: 'mana-potion', name: 'Mana Potion', type: 'consumable', icon: '🍶', color: '#0000ff' },
  { id: 'arrows', name: 'Arrows', type: 'weapon', icon: '🎯', color: '#654321' },
  { id: 'food', name: 'Food', type: 'consumable', icon: '🍖', color: '#8b4513' },
];

/**
 * Calculate XP needed for next level (RuneScape-style formula, but slower progression)
 */
const calculateXPForLevel = (level: number): number => {
  let total = 0;
  for (let i = 1; i < level; i++) {
    total += Math.floor(i + 300 * Math.pow(2, i / 7));
  }
  return Math.floor(total / 2.5); // Reduced divisor from 4 to 2.5 to make leveling slower
};

/**
 * Calculate XP needed for cat leveling (3x faster than original)
 */
const calculateCatXPForLevel = (level: number): number => {
  let total = 0;
  for (let i = 1; i < level; i++) {
    total += Math.floor(i + 500 * Math.pow(2, i / 6));
  }
  return Math.floor(total / 5.4); // 3x faster (1.8 * 3 = 5.4)
};

/**
 * Initial weapon skills
 */
const initialWeaponSkills: IWeaponSkills = {
  sword: { level: 1, xp: 0, xpToNextLevel: calculateXPForLevel(2) },
  bow: { level: 1, xp: 0, xpToNextLevel: calculateXPForLevel(2) },
  magic: {
    water: { level: 1, xp: 0, xpToNextLevel: calculateXPForLevel(2) },
    air: { level: 1, xp: 0, xpToNextLevel: calculateXPForLevel(2) },
    earth: { level: 1, xp: 0, xpToNextLevel: calculateXPForLevel(2) },
    fire: { level: 1, xp: 0, xpToNextLevel: calculateXPForLevel(2) },
  },
};

/**
 * Initial cat stats
 */
const initialCatStats: ICatStats = {
  level: 1,
  xp: 0,
  xpToNextLevel: calculateCatXPForLevel(2),
  baseHealth: 100,
  healthBonus: 0, // +20 health per level
  abilities: {
    regeneration: false,
    agility: false,
    nineLives: false,
    predatorInstinct: false,
    alphaStrike: false,
  },
  nineLivesUsed: false,
};

/**
 * Initial player state
 */
const initialPlayerState: IPlayerState = {
  cat: null,
  position: { x: 0, y: 0, z: 0, rotation: 0 },
  isMoving: false,
  isRunning: false,
  isJumping: false,
  isAttacking: false,
  isDefending: false,
  health: initialCatStats.baseHealth + initialCatStats.healthBonus,
  maxHealth: initialCatStats.baseHealth + initialCatStats.healthBonus,
  inventory: initialInventory,
  activeSlot: 0,
};

/**
 * Clan data for story mode
 */
const clansData: Record<string, IClan> = {
  MistClan: {
    id: 'MistClan',
    name: 'MistClan',
    territory: 'Misty Marshlands & Creek Valleys',
    specialty: 'Stealth, fishing, swimming',
    values: ['Adaptability', 'Cunning', 'Patience'],
    sacredSite: 'Moonlit Falls',
    relationship: 'neutral'
  },
  StormClan: {
    id: 'StormClan',
    name: 'StormClan',
    territory: 'Rocky Highlands & Pine Forests',
    specialty: 'Mountain combat, endurance',
    values: ['Strength', 'Honor', 'Determination'],
    sacredSite: 'Thunder Peak',
    relationship: 'neutral'
  },
  EmberClan: {
    id: 'EmberClan',
    name: 'EmberClan',
    territory: 'Autumn Forests & Oak Groves',
    specialty: 'Hunting, herb knowledge',
    values: ['Wisdom', 'Tradition', 'Healing'],
    sacredSite: 'The Elder Grove',
    relationship: 'neutral'
  },
  FrostClan: {
    id: 'FrostClan',
    name: 'FrostClan',
    territory: 'Northern Pines & Snowy Valleys',
    specialty: 'Winter survival, tracking',
    values: ['Resilience', 'Loyalty', 'Community'],
    sacredSite: 'Crystal Caverns',
    relationship: 'neutral'
  }
};

/**
 * Initial quests for story mode
 */
const initialQuests: IQuest[] = [
  {
    id: 'first-pawsteps',
    title: 'First Pawsteps',
    description: 'Learn the basics of clan life by exploring your new territory.',
    category: 'novice',
    prerequisites: [],
    objectives: [
      {
        id: 'talk-to-leader',
        type: 'talk',
        target: 'clan-leader',
        count: 1,
        currentCount: 0,
        description: 'Speak with your clan leader'
      }
    ],
    rewards: [
      {
        type: 'xp',
        value: 'exploration',
        amount: 10,
        description: '+10 Exploration XP'
      },
      {
        type: 'rank',
        value: 'apprentice',
        description: 'Become an apprentice'
      }
    ],
    status: 'available',
    giver: 'clan-leader',
    location: 'clan-camp'
  },
  {
    id: 'territory-bounds',
    title: 'Territory Bounds',
    description: 'Learn where your clan\'s territory begins and ends.',
    category: 'novice',
    prerequisites: ['first-pawsteps'],
    objectives: [
      {
        id: 'talk-to-mentor',
        type: 'talk',
        target: 'mentor',
        count: 1,
        currentCount: 0,
        description: 'Learn from your mentor'
      },
      {
        id: 'explore-territory',
        type: 'visit',
        target: 'territory-bounds',
        count: 20,
        currentCount: 0,
        description: 'Explore the clan territory (move around)'
      }
    ],
    rewards: [
      {
        type: 'xp',
        value: 'exploration',
        amount: 15,
        description: '+15 Exploration XP'
      },
      {
        type: 'item',
        value: 'territory-map',
        description: 'Territory Map'
      }
    ],
    status: 'locked',
    giver: 'mentor',
    location: 'clan-camp'
  },
  {
    id: 'first-hunt',
    title: 'The First Hunt',
    description: 'Prove your hunting skills by catching prey for the clan.',
    category: 'novice',
    prerequisites: ['territory-bounds'],
    objectives: [
      {
        id: 'hunt-training',
        type: 'kill',
        target: 'practice-targets',
        count: 5,
        currentCount: 0,
        description: 'Practice hunting by defeating 5 practice targets'
      }
    ],
    rewards: [
      {
        type: 'xp',
        value: 'hunting',
        amount: 20,
        description: '+20 Hunting XP'
      },
      {
        type: 'xp',
        value: 'combat',
        amount: 10,
        description: '+10 Combat XP'
      }
    ],
    status: 'locked',
    giver: 'mentor',
    location: 'hunting-grounds'
  }
];

/**
 * Load initial story mode state (with persistence)
 */
const createInitialStoryMode = (): IStoryModeState => {
  const baseState: IStoryModeState = {
    isActive: false,
    playerClan: null,
    playerRank: 'outsider',
    mentorName: '',
    quests: initialQuests,
    activeQuests: [],
    completedQuests: [],
    storySkills: {
      combat: 0,
      hunting: 0,
      herbalism: 0,
      leadership: 0,
      mysticism: 0,
      exploration: 0
    },
    clanRelationships: {
      MistClan: 'neutral',
      StormClan: 'neutral',
      EmberClan: 'neutral',
      FrostClan: 'neutral'
    },
    territoryAccess: ['clan-camp'],
    mysticalConnections: 0,
    questBookOpen: false,
    dialog: {
      isOpen: false,
      npcName: '',
      npcRole: '',
      message: '',
      options: []
    }
  };

  // Load persisted story progress
  const savedProgress = loadStoryProgress();
  if (savedProgress) {
    return {
      ...baseState,
      ...savedProgress,
      // Always use current quest definitions
      quests: initialQuests,
      // Ensure dialog state is reset
      questBookOpen: false,
      dialog: baseState.dialog
    };
  }

  return baseState;
};

const initialStoryMode = createInitialStoryMode();

/**
 * Load initial state with persistence
 */
const createInitialState = () => {
  // Load persisted game mode
  const savedGameMode = loadGameMode();
  
  // Load persisted player progress
  const savedPlayerProgress = loadPlayerProgress();
  let loadedCatStats = initialCatStats;
  let loadedWeaponSkills = initialWeaponSkills;
  
  if (savedPlayerProgress) {
    // Apply saved cat stats
    if (savedPlayerProgress.catLevel && savedPlayerProgress.catXp) {
      loadedCatStats = {
        ...initialCatStats,
        level: savedPlayerProgress.catLevel,
        xp: savedPlayerProgress.catXp,
        xpToNextLevel: calculateXPNeeded(savedPlayerProgress.catLevel + 1)
      };
    }
    
    // Apply saved weapon skills
    if (savedPlayerProgress.weaponSkills) {
      loadedWeaponSkills = savedPlayerProgress.weaponSkills;
    }
  }

  return {
    gameMode: savedGameMode,
    catStats: loadedCatStats,
    weaponSkills: loadedWeaponSkills,
    storyMode: initialStoryMode
  };
};

/**
 * Create the game store
 */
export const useGameStore = create<IGameState>((set, get) => {
  const initialState = createInitialState();
  
  return {
    isWorldLoaded: false,
    world: null,
    dayCycle: initialDayCycle,
    connectionError: null,
    player: initialPlayerState,
    availableSpells: availableSpells,
    inventoryBag: initialInventoryBag,
    enemies: [],
    isGameOver: false,
    currentWave: 1,
    enemiesKilled: 0,
    isWaveTransition: false,
    weaponSkills: initialState.weaponSkills,
    catStats: initialState.catStats,
    isPaused: false,
    isMenuPaused: false,
    gameMode: initialState.gameMode,
    storyMode: initialState.storyMode,
    setWorldLoaded: (loaded) => set({ isWorldLoaded: loaded }),
  setWorld: (world) => set({ world }),
  setDayCycle: (cycle) => set({ dayCycle: cycle }),
  setConnectionError: (error) => set({ connectionError: error }),
  setPlayer: (newPlayer) => set((state) => ({ player: { ...state.player, ...newPlayer } })),
  setPlayerPosition: (position) => set((state) => ({ 
    player: { ...state.player, position: { ...state.player.position, ...position } }
  })),
  setPlayerMoving: (isMoving) => set((state) => ({ player: { ...state.player, isMoving } })),
  setPlayerRunning: (isRunning) => set((state) => ({ player: { ...state.player, isRunning } })),
  setPlayerJumping: (isJumping) => set((state) => ({ player: { ...state.player, isJumping } })),
  setPlayerAttacking: (isAttacking) => set((state) => ({ player: { ...state.player, isAttacking } })),
  setPlayerDefending: (isDefending) => set((state) => ({ player: { ...state.player, isDefending } })),
  setPlayerHealth: (health) => set((state) => ({ player: { ...state.player, health } })),
  damagePlayer: (amount) => set((state) => { 
    const newHealth = Math.max(0, state.player.health - amount);
    let isGameOver = newHealth <= 0;
    let updatedCatStats = state.catStats;
    
    // Nine Lives ability (Level 15+) - survive death once per wave
    if (isGameOver && state.catStats.abilities.nineLives && !state.catStats.nineLivesUsed) {
      isGameOver = false;
      const reviveHealth = Math.floor(state.player.maxHealth * 0.3); // Revive with 30% health
      updatedCatStats = { ...state.catStats, nineLivesUsed: true };
      console.log('💀 Nine Lives activated! Survived death with 30% health!');
      return { 
        player: { ...state.player, health: reviveHealth },
        catStats: updatedCatStats,
        isGameOver: false 
      };
    }
    
    return { 
      player: { ...state.player, health: newHealth },
      isGameOver 
    };
  }),
  addEnemy: (enemy) => set((state) => ({ enemies: [...state.enemies, enemy] })),
  removeEnemy: (id) => set((state) => ({ enemies: state.enemies.filter(e => e.id !== id) })),
  damageEnemy: (id, amount) => set((state) => ({
    enemies: state.enemies.map(e => e.id === id ? { ...e, health: Math.max(0, e.health - amount) } : e)
  })),
  updateEnemyPosition: (id, position) => set((state) => ({
    enemies: state.enemies.map(e => e.id === id ? { ...e, position } : e)
  })),
  setGameOver: (over) => set({ isGameOver: over }),
  setActiveSlot: (slot) => set((state) => ({ player: { ...state.player, activeSlot: slot } })),
  setInventorySlot: (slot, item) => set((state) => {
    const newInventory = [...state.player.inventory];
    newInventory[slot] = item;
    return { player: { ...state.player, inventory: newInventory } };
  }),
  removeFromInventoryBag: (itemId) => set((state) => {
    const itemIndex = state.inventoryBag.findIndex(item => item.id === itemId);
    if (itemIndex !== -1) {
      const newInventoryBag = [...state.inventoryBag];
      newInventoryBag.splice(itemIndex, 1);
      return { inventoryBag: newInventoryBag };
    }
    return state;
  }),
  addToInventoryBag: (item) => set((state) => {
    const newInventoryBag = [...state.inventoryBag, item];
    return { inventoryBag: newInventoryBag };
  }),
  setCurrentWave: (wave) => set({ currentWave: wave }),
  incrementEnemiesKilled: () => set((state) => {
    const newEnemiesKilled = state.enemiesKilled + 1;
    // Award cat XP for killing enemies (5 XP per kill)
    const catXP = 5;
    const catStats = state.catStats;
    const newCatXP = catStats.xp + catXP;
    let newCatLevel = catStats.level;
    let catXpToNextLevel = catStats.xpToNextLevel;
    let healthBonus = catStats.healthBonus;
    const abilities = { ...catStats.abilities };
    
    // Check for cat level up
    while (newCatXP >= calculateCatXPForLevel(newCatLevel + 1)) {
      newCatLevel++;
      healthBonus += 20; // +20 health per cat level
      console.log(`🐱 CAT LEVEL UP! Now level ${newCatLevel} (+20 health)`);
      
      // Unlock abilities at specific levels
      if (newCatLevel === 5) {
        abilities.regeneration = true;
        console.log('🔮 Unlocked: Health Regeneration!');
      } else if (newCatLevel === 10) {
        abilities.agility = true;
        console.log('⚡ Unlocked: Enhanced Agility!');
      } else if (newCatLevel === 15) {
        abilities.nineLives = true;
        console.log('💀 Unlocked: Nine Lives!');
      } else if (newCatLevel === 20) {
        abilities.predatorInstinct = true;
        console.log('👁️ Unlocked: Predator Instinct!');
      } else if (newCatLevel === 25) {
        abilities.alphaStrike = true;
        console.log('⚔️ Unlocked: Alpha Strike!');
      }
    }
    
    // Calculate XP to next level
    if (newCatLevel < 99) {
      catXpToNextLevel = calculateCatXPForLevel(newCatLevel + 1);
    }
    
    const newCatStats = {
      ...catStats,
      level: newCatLevel,
      xp: newCatXP,
      xpToNextLevel: catXpToNextLevel,
      healthBonus,
      abilities
    };
    
    // Update player max health if cat leveled up
    const newMaxHealth = catStats.baseHealth + healthBonus;
    let newCurrentHealth = state.player.health;
    let newPlayerMaxHealth = state.player.maxHealth;
    
    if (newMaxHealth > state.player.maxHealth) {
      newPlayerMaxHealth = newMaxHealth;
      // Heal player when max health increases
      const healthIncrease = newMaxHealth - state.player.maxHealth;
      newCurrentHealth = Math.min(state.player.health + healthIncrease, newMaxHealth);
    }
    
    return { 
      enemiesKilled: newEnemiesKilled,
      catStats: newCatStats,
      player: {
        ...state.player,
        health: newCurrentHealth,
        maxHealth: newPlayerMaxHealth
      }
    };
  }),
  setWaveTransition: (isTransition) => set({ isWaveTransition: isTransition }),
  addWeaponXP: (weapon, xp, element) => set((state) => {
    if (weapon === 'magic' && element) {
      // Handle magic element-specific XP
      const skill = state.weaponSkills.magic[element];
      const newXP = skill.xp + xp;
      let newLevel = skill.level;
      let xpToNextLevel = skill.xpToNextLevel;
      
      // Check for level up
      while (newXP >= calculateXPForLevel(newLevel + 1)) {
        newLevel++;
        console.log(`🎉 ${element.toUpperCase()} MAGIC LEVEL UP! Now level ${newLevel}`);
      }
      
      // Calculate XP to next level
      if (newLevel < 99) { // Max level 99 like RuneScape
        xpToNextLevel = calculateXPForLevel(newLevel + 1);
      }
      
      return {
        weaponSkills: {
          ...state.weaponSkills,
          magic: {
            ...state.weaponSkills.magic,
            [element]: {
              level: newLevel,
              xp: newXP,
              xpToNextLevel
            }
          }
        }
      };
    } else {
      // Handle sword and bow XP (non-magic weapons)
      const skill = state.weaponSkills[weapon as 'sword' | 'bow'];
      const newXP = skill.xp + xp;
      let newLevel = skill.level;
      let xpToNextLevel = skill.xpToNextLevel;
      
      // Check for level up
      while (newXP >= calculateXPForLevel(newLevel + 1)) {
        newLevel++;
        console.log(`🎉 ${weapon.toUpperCase()} LEVEL UP! Now level ${newLevel}`);
      }
      
      // Calculate XP to next level
      if (newLevel < 99) { // Max level 99 like RuneScape
        xpToNextLevel = calculateXPForLevel(newLevel + 1);
      }
      
      return {
        weaponSkills: {
          ...state.weaponSkills,
          [weapon]: {
            level: newLevel,
            xp: newXP,
            xpToNextLevel
          }
        }
      };
    }
  }),
  addCatXP: (xp) => set((state) => {
    const catStats = state.catStats;
    const newXP = catStats.xp + xp;
    let newLevel = catStats.level;
    let xpToNextLevel = catStats.xpToNextLevel;
    let healthBonus = catStats.healthBonus;
    const abilities = { ...catStats.abilities };
    
    // Check for level up
    while (newXP >= calculateCatXPForLevel(newLevel + 1)) {
      newLevel++;
      healthBonus += 20; // +20 health per cat level
      console.log(`🐱 CAT LEVEL UP! Now level ${newLevel} (+20 health)`);
      
      // Unlock abilities at specific levels
      if (newLevel === 5) {
        abilities.regeneration = true;
        console.log('🔮 Unlocked: Health Regeneration!');
      } else if (newLevel === 10) {
        abilities.agility = true;
        console.log('⚡ Unlocked: Enhanced Agility!');
      } else if (newLevel === 15) {
        abilities.nineLives = true;
        console.log('💀 Unlocked: Nine Lives!');
      } else if (newLevel === 20) {
        abilities.predatorInstinct = true;
        console.log('👁️ Unlocked: Predator Instinct!');
      } else if (newLevel === 25) {
        abilities.alphaStrike = true;
        console.log('⚔️ Unlocked: Alpha Strike!');
      }
    }
    
    // Calculate XP to next level
    if (newLevel < 99) {
      xpToNextLevel = calculateCatXPForLevel(newLevel + 1);
    }
    
    const newCatStats = {
      ...catStats,
      level: newLevel,
      xp: newXP,
      xpToNextLevel,
      healthBonus,
      abilities
    };
    
    // Update player max health when cat levels up
    const newMaxHealth = catStats.baseHealth + healthBonus;
    const currentHealthRatio = state.player.health / state.player.maxHealth;
    const newCurrentHealth = Math.max(state.player.health, Math.ceil(newMaxHealth * currentHealthRatio));
    
    // Save player progress to localStorage
    savePlayerProgress(newCatStats, state.weaponSkills, state.currentWave, state.enemiesKilled);
    
    return {
      catStats: newCatStats,
      player: {
        ...state.player,
        health: newCurrentHealth,
        maxHealth: newMaxHealth
      }
    };
  }),
  setCatHealth: (health) => set((state) => ({ 
    player: { ...state.player, health: Math.min(health, state.player.maxHealth) }
  })),
  resetNineLives: () => set((state) => ({
    catStats: { ...state.catStats, nineLivesUsed: false }
  })),
  setPaused: (paused) => set({ isPaused: paused }),
  setMenuPaused: (paused) => set({ isMenuPaused: paused }),
  setGameMode: (mode) => set((state) => {
    saveGameMode(mode);
    return { gameMode: mode };
  }),
  setPlayerClan: (clan) => set((state) => {
    const newStoryMode = { ...state.storyMode, playerClan: clan, isActive: true };
    saveStoryProgress(newStoryMode);
    return { storyMode: newStoryMode };
  }),
  setPlayerRank: (rank) => set((state) => {
    const newStoryMode = { ...state.storyMode, playerRank: rank };
    saveStoryProgress(newStoryMode);
    return { storyMode: newStoryMode };
  }),
  addQuest: (quest) => set((state) => ({
    storyMode: { 
      ...state.storyMode, 
      quests: [...state.storyMode.quests, quest] 
    }
  })),
  updateQuest: (questId, updates) => set((state) => ({
    storyMode: {
      ...state.storyMode,
      quests: state.storyMode.quests.map(quest => 
        quest.id === questId ? { ...quest, ...updates } : quest
      )
    }
  })),
  completeQuest: (questId) => set((state) => {
    const quest = state.storyMode.quests.find(q => q.id === questId);
    if (!quest) return state;

    // Award quest rewards
    let newStorySkills = { ...state.storyMode.storySkills };
    let newPlayerRank = state.storyMode.playerRank;
    
    quest.rewards.forEach(reward => {
      if (reward.type === 'xp' && typeof reward.value === 'string') {
        const skill = reward.value as keyof IStorySkills;
        if (skill in newStorySkills) {
          newStorySkills[skill] += reward.amount || 0;
        }
      } else if (reward.type === 'rank') {
        newPlayerRank = reward.value as typeof newPlayerRank;
      }
    });

    console.log(`📜 Quest completed: ${quest.title}`);
    quest.rewards.forEach(reward => {
      console.log(`🎁 Reward: ${reward.description}`);
    });

    const newStoryMode = {
      ...state.storyMode,
      quests: state.storyMode.quests.map(q => 
        q.id === questId ? { ...q, status: 'completed' as const } : q
      ),
      activeQuests: state.storyMode.activeQuests.filter(id => id !== questId),
      completedQuests: [...state.storyMode.completedQuests, questId],
      storySkills: newStorySkills,
      playerRank: newPlayerRank
    };
    
    saveStoryProgress(newStoryMode);
    return { storyMode: newStoryMode };
  }),
  activateQuest: (questId) => set((state) => {
    const quest = state.storyMode.quests.find(q => q.id === questId);
    if (!quest || quest.status !== 'available') return state;

    console.log(`📋 Quest activated: ${quest.title}`);
    
    const newStoryMode = {
      ...state.storyMode,
      quests: state.storyMode.quests.map(q => 
        q.id === questId ? { ...q, status: 'active' as const } : q
      ),
      activeQuests: [...state.storyMode.activeQuests, questId]
    };
    
    saveStoryProgress(newStoryMode);
    return { storyMode: newStoryMode };
  }),
  updateQuestObjective: (questId, objectiveId, progress) => set((state) => {
    const quest = state.storyMode.quests.find(q => q.id === questId);
    if (!quest) return state;

    const updatedQuest = {
      ...quest,
      objectives: quest.objectives.map(obj => 
        obj.id === objectiveId 
          ? { ...obj, currentCount: Math.min(progress, obj.count) }
          : obj
      )
    };

    // Check if quest is completed
    const allObjectivesComplete = updatedQuest.objectives.every(obj => obj.currentCount >= obj.count);
    if (allObjectivesComplete && quest.status === 'active') {
      // Auto-complete the quest
      setTimeout(() => {
        useGameStore.getState().completeQuest(questId);
      }, 100);
    }

    return {
      storyMode: {
        ...state.storyMode,
        quests: state.storyMode.quests.map(q => 
          q.id === questId ? updatedQuest : q
        )
      }
    };
  }),
  addStoryXP: (skill, amount) => set((state) => {
    const newSkills = { ...state.storyMode.storySkills };
    newSkills[skill] += amount;
    
    console.log(`📈 +${amount} ${skill.toUpperCase()} XP (Total: ${newSkills[skill]})`);
    
    const newStoryMode = { ...state.storyMode, storySkills: newSkills };
    saveStoryProgress(newStoryMode);
    return { storyMode: newStoryMode };
  }),
  setClanRelationship: (clan, relationship) => set((state) => ({
    storyMode: {
      ...state.storyMode,
      clanRelationships: {
        ...state.storyMode.clanRelationships,
        [clan]: relationship
      }
    }
  })),
  toggleQuestBook: () => set((state) => ({
    storyMode: { ...state.storyMode, questBookOpen: !state.storyMode.questBookOpen }
  })),
  showDialog: (npcName: string, npcRole: string, message: string, options?: IDialogOption[]) => set((state) => ({
    storyMode: {
      ...state.storyMode,
      dialog: {
        isOpen: true,
        npcName,
        npcRole,
        message,
        options: options || []
      }
    }
  })),
  closeDialog: () => set((state) => ({
    storyMode: {
      ...state.storyMode,
      dialog: {
        isOpen: false,
        npcName: '',
        npcRole: '',
        message: '',
        options: []
      }
    }
  })),
  };
}); 