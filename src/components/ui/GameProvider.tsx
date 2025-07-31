import { useEffect } from 'react';
import { useGameStore } from '../../lib/store/gameStore';
import { initialPlayerPosition, initialPlayerCat, initialWorldData } from '../../lib/game/initialState';

interface GameProviderProps {
  children: React.ReactNode;
}

/**
 * GameProvider sets up the initial game state
 * This is used for development and testing
 */
const GameProvider = ({ children }: GameProviderProps) => {
  const setWorld = useGameStore((state) => state.setWorld);
  const setPlayer = useGameStore((state) => state.setPlayer);
  const isWorldLoaded = useGameStore((state) => state.isWorldLoaded);
  
  // Initialize game state on mount
  useEffect(() => {
    if (isWorldLoaded) return;
    
    // Set up global store reference for weapon XP system
    (globalThis as any).__GAME_STORE__ = useGameStore;
    
    // Set world data
    setWorld(initialWorldData);
    
    // Set player data
    setPlayer({
      cat: initialPlayerCat,
      position: initialPlayerPosition,
      isMoving: false,
      isAttacking: false,
    });
    
    console.log('Game state initialized');
  }, [isWorldLoaded, setWorld, setPlayer]);
  
  return <>{children}</>;
};

export default GameProvider; 