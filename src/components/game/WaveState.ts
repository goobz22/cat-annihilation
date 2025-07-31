/**
 * !!!!!!! CRITICAL WARNING !!!!!!!!!
 * 
 * THIS WAVE STATE SYSTEM MUST ***NEVER*** CALL ANY ZUSTAND STORE SET() FUNCTIONS
 * DURING RUNTIME OR IT WILL CAUSE THE CAT TO FLY THROUGH THE TERRAIN!!!!!
 * 
 * Just like projectiles and enemies, this uses ONLY local state and NEVER 
 * touches the global game store during gameplay.
 * 
 * !!!!!!! CRITICAL WARNING !!!!!!!!!
 */

// Simple global state for wave transitions (separate from Zustand to avoid movement issues)
export const waveState = {
  isTransition: false,
  currentWave: 1,
  nextWaveEnemies: 0,
  onStateChange: null as ((state: { isTransition: boolean; currentWave: number; nextWaveEnemies: number }) => void) | null
};

export const updateWaveState = (newState: Partial<typeof waveState>) => {
  Object.assign(waveState, newState);
  
  // !!!! NEVER UPDATE ZUSTAND STORE - CAUSES TERRAIN BUGS !!!!
  // The WaveDisplay component will read from this local state instead
  
  if (waveState.onStateChange) {
    waveState.onStateChange({
      isTransition: waveState.isTransition,
      currentWave: waveState.currentWave,
      nextWaveEnemies: waveState.nextWaveEnemies
    });
  }
};