import { useEffect, useState } from 'react';
import { waveState, subscribeToWaveState } from '../game/WaveState';

const WaveDisplay = () => {
  const [currentWave, setCurrentWave] = useState(waveState.currentWave);
  const [isWaveTransition, setIsWaveTransition] = useState(waveState.isTransition);
  const [showTransition, setShowTransition] = useState(false);
  const [animateWave, setAnimateWave] = useState(false);

  // Subscribe to wave state changes
  useEffect(() => {
    const updateWaveDisplay = (state: { isTransition: boolean; currentWave: number; nextWaveEnemies: number }) => {
      console.log(`📺 WaveDisplay received state update:`, state);
      setCurrentWave(state.currentWave);
      setIsWaveTransition(state.isTransition);
    };

    // Set up subscription using new subscriber system
    console.log(`📺 WaveDisplay setting up subscription, initial wave:`, waveState.currentWave);
    const unsubscribe = subscribeToWaveState(updateWaveDisplay);
    
    // Clean up subscription
    return () => {
      console.log(`📺 WaveDisplay cleaning up subscription`);
      unsubscribe();
    };
  }, []);

  useEffect(() => {
    if (isWaveTransition) {
      setShowTransition(true);
      setTimeout(() => {
        setShowTransition(false);
      }, 3000);
    }
  }, [isWaveTransition]);

  // Trigger wave counter animation when wave number changes and transition ends
  useEffect(() => {
    if (!isWaveTransition && currentWave > 1) {
      setAnimateWave(true);
      setTimeout(() => setAnimateWave(false), 500);
    }
  }, [currentWave, isWaveTransition]);

  return (
    <>
      {/* Permanent wave counter */}
      <div className="wave-display-counter">
        <div className={`wave-counter-content ${animateWave ? 'animate' : ''}`}>
          <div className="wave-counter-title">
            ROUND {currentWave}
          </div>
          <div className="wave-counter-subtitle">
            SURVIVE THE HORDE
          </div>
        </div>
      </div>

      {/* Wave transition screen */}
      {showTransition && (
        <div className="wave-transition-overlay">
          <div className="wave-transition-content">
            <div className="wave-transition-title">
              ROUND {currentWave}
            </div>
            <div className="wave-transition-message">
              {currentWave === 1 ? 'THE DOGS ARE COMING...' : 'MORE DOGS INCOMING!'}
            </div>
            <div className="wave-transition-details">
              {currentWave > 1 && `${Math.floor((3 + (currentWave * 2)) * 1.5)} dogs this round`}
            </div>
          </div>
        </div>
      )}
    </>
  );
};

export default WaveDisplay;