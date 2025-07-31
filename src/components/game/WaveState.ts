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
  subscribers: [] as ((state: { isTransition: boolean; currentWave: number; nextWaveEnemies: number }) => void)[],
  // Keep old onStateChange for compatibility but also use subscribers array
  onStateChange: null as ((state: { isTransition: boolean; currentWave: number; nextWaveEnemies: number }) => void) | null
};

export const updateWaveState = (newState: Partial<typeof waveState>) => {
  console.log(`🌊 updateWaveState called with:`, newState);
  Object.assign(waveState, newState);
  console.log(`🌊 waveState after update:`, waveState);
  
  // !!!! NEVER UPDATE ZUSTAND STORE - CAUSES TERRAIN BUGS !!!!
  // The WaveDisplay component will read from this local state instead
  
  const stateUpdate = {
    isTransition: waveState.isTransition,
    currentWave: waveState.currentWave,
    nextWaveEnemies: waveState.nextWaveEnemies
  };

  // Notify all subscribers
  console.log(`🌊 Notifying ${waveState.subscribers.length} subscribers`);
  waveState.subscribers.forEach((callback, index) => {
    console.log(`🌊 Calling subscriber ${index}`);
    callback(stateUpdate);
  });

  // Keep compatibility with old onStateChange callback
  if (waveState.onStateChange) {
    console.log(`🌊 Calling legacy onStateChange callback`);
    waveState.onStateChange(stateUpdate);
  }
};

// Helper functions for subscription management
export const subscribeToWaveState = (callback: (state: { isTransition: boolean; currentWave: number; nextWaveEnemies: number }) => void) => {
  waveState.subscribers.push(callback);
  console.log(`🌊 New subscriber added. Total subscribers: ${waveState.subscribers.length}`);
  
  // Return unsubscribe function
  return () => {
    const index = waveState.subscribers.indexOf(callback);
    if (index !== -1) {
      waveState.subscribers.splice(index, 1);
      console.log(`🌊 Subscriber removed. Total subscribers: ${waveState.subscribers.length}`);
    }
  };
};