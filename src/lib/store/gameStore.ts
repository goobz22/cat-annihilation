import { create } from 'zustand';
import { ICat } from '@/models/Cat';
import { IWorld, IZone } from '@/models/World';

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
 * Interface for the current game zone
 */
interface ICurrentZone {
  name: string;
  type: string;
  isPvp: boolean;
}

/**
 * Interface for the player state
 */
interface IPlayerState {
  isLoaded: boolean;
  cat: Partial<ICat> | null;
  position: IPosition;
  isMoving: boolean;
  isRunning: boolean;
  isJumping: boolean;
  isAttacking: boolean;
  isDefending: boolean;
  currentZone: ICurrentZone | null;
}

/**
 * Interface for other players in the game
 */
interface IOtherPlayers {
  [playerId: string]: {
    cat: Partial<ICat>;
    position: IPosition;
    isMoving: boolean;
    isRunning: boolean;
    isJumping: boolean;
    isAttacking: boolean;
    isDefending: boolean;
  };
}

/**
 * Interface for the game editor mode
 */
interface IEditorMode {
  isActive: boolean;
  selectedTool: 'terrain' | 'zones' | 'items' | 'cats' | null;
}

/**
 * Interface for editor parameter state (3D -> UI communication)
 */
interface IEditorParams {
  terrain?: {
    brushSize: number;
    brushStrength: number;
    isRaising: boolean;
  };
  zones?: {
    creatingZone: boolean;
    currentZone: {
      name: string;
      type: string;
      bounds: {
        minX: number;
        maxX: number;
        minZ: number;
        maxZ: number;
      };
      isPvpAtNight: boolean;
    } | null;
    worldZonesLength: number;
    callbacks?: {
      onChangeName?: (name: string) => void;
      onChangeType?: () => void;
      onTogglePvP?: () => void;
    };
  };
  items?: {
    selectedItem: string | null;
    itemsLength: number;
    availableItems: Array<{ id: string; label: string }>;
    callbacks?: {
      onSelectItem?: (id: string) => void;
    };
  };
  cats?: {
    selectedCatType: string | null;
    catsLength: number;
    availableCatTypes: Array<{ id: string; label: string }>;
    callbacks?: {
      onSelectCatType?: (id: string) => void;
    };
  };
}

/**
 * Interface for the game state
 */
interface IGameState {
  // World state
  isWorldLoaded: boolean;
  world: Partial<IWorld> | null;
  dayCycle: IDayCycle;
  
  // Player state
  player: IPlayerState;
  otherPlayers: IOtherPlayers;
  
  // Editor mode
  editorMode: IEditorMode;
  editorParams: IEditorParams | null;
  
  // Game connection state
  isConnected: boolean;
  connectionError: string | null;
  
  // Actions
  setWorld: (world: Partial<IWorld>) => void;
  setDayCycle: (dayCycle: Partial<IDayCycle>) => void;
  setPlayer: (player: Partial<IPlayerState>) => void;
  setPlayerPosition: (position: Partial<IPosition>) => void;
  setPlayerMoving: (isMoving: boolean) => void;
  setPlayerRunning: (isRunning: boolean) => void;
  setPlayerJumping: (isJumping: boolean) => void;
  setPlayerAttacking: (isAttacking: boolean) => void;
  setPlayerDefending: (isDefending: boolean) => void;
  setCurrentZone: (zone: ICurrentZone | null) => void;
  updateOtherPlayer: (playerId: string, data: Partial<IOtherPlayers[string]>) => void;
  removeOtherPlayer: (playerId: string) => void;
  setEditorMode: (editorMode: Partial<IEditorMode>) => void;
  setEditorParams: (params: Partial<IEditorParams>) => void;
  setConnectionState: (isConnected: boolean, error?: string | null) => void;
  reset: () => void;
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
 * Initial player state
 */
const initialPlayerState: IPlayerState = {
  isLoaded: false,
  cat: null,
  position: { x: 0, y: 0, z: 0, rotation: 0 },
  isMoving: false,
  isRunning: false,
  isJumping: false,
  isAttacking: false,
  isDefending: false,
  currentZone: null,
};

/**
 * Initial editor mode state
 */
const initialEditorMode: IEditorMode = {
  isActive: false,
  selectedTool: null,
};

/**
 * Create the game store
 */
export const useGameStore = create<IGameState>((set) => ({
  // World state
  isWorldLoaded: false,
  world: null,
  dayCycle: initialDayCycle,
  
  // Player state
  player: initialPlayerState,
  otherPlayers: {},
  
  // Editor mode
  editorMode: initialEditorMode,
  editorParams: null,
  
  // Game connection state
  isConnected: false,
  connectionError: null,
  
  // Actions
  setWorld: (world) => set((state) => ({ 
    world: { ...state.world, ...world },
    isWorldLoaded: true
  })),
  
  setDayCycle: (dayCycle) => set((state) => ({ 
    dayCycle: { ...state.dayCycle, ...dayCycle }
  })),
  
  setPlayer: (player) => set((state) => ({ 
    player: { ...state.player, ...player }
  })),
  
  setPlayerPosition: (position) => set((state) => ({ 
    player: { 
      ...state.player, 
      position: { ...state.player.position, ...position }
    }
  })),
  
  setPlayerMoving: (isMoving) => set((state) => ({ 
    player: { ...state.player, isMoving }
  })),
  
  setPlayerRunning: (isRunning) => set((state) => ({ 
    player: { ...state.player, isRunning }
  })),
  
  setPlayerJumping: (isJumping) => set((state) => ({ 
    player: { ...state.player, isJumping }
  })),
  
  setPlayerAttacking: (isAttacking) => set((state) => ({ 
    player: { ...state.player, isAttacking }
  })),
  
  setPlayerDefending: (isDefending) => set((state) => ({ 
    player: { ...state.player, isDefending }
  })),
  
  setCurrentZone: (zone) => set((state) => ({ 
    player: { ...state.player, currentZone: zone }
  })),
  
  updateOtherPlayer: (playerId, data) => set((state) => ({
    otherPlayers: {
      ...state.otherPlayers,
      [playerId]: {
        ...state.otherPlayers[playerId] || {
          cat: {},
          position: { x: 0, y: 0, z: 0, rotation: 0 },
          isMoving: false,
          isRunning: false,
          isJumping: false,
          isAttacking: false,
          isDefending: false,
        },
        ...data,
      },
    },
  })),
  
  removeOtherPlayer: (playerId) => set((state) => {
    const { [playerId]: removed, ...otherPlayers } = state.otherPlayers;
    return { otherPlayers };
  }),
  
  setEditorMode: (editorMode) => set((state) => ({ 
    editorMode: { ...state.editorMode, ...editorMode }
  })),
  
  setEditorParams: (params) => set((state) => ({ 
    editorParams: { ...state.editorParams, ...params }
  })),
  
  setConnectionState: (isConnected, error = null) => set({ 
    isConnected, 
    connectionError: error
  }),
  
  reset: () => set({
    isWorldLoaded: false,
    world: null,
    dayCycle: initialDayCycle,
    player: initialPlayerState,
    otherPlayers: {},
    editorMode: initialEditorMode,
    editorParams: null,
    isConnected: false,
    connectionError: null,
  }),
})); 