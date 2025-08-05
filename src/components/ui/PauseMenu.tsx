import { useEffect, useState } from 'react';
import { useGameStore } from '../../lib/store/gameStore';

const PauseMenu = () => {
  const isPaused = useGameStore((state) => state.isPaused);
  const isMenuPaused = useGameStore((state) => state.isMenuPaused);
  const setPaused = useGameStore((state) => state.setPaused);
  const [spinSensitivity, setSpinSensitivity] = useState(() => {
    const saved = localStorage.getItem('catSpinSensitivity');
    return saved ? parseFloat(saved) : 1.0;
  });
  
  const [moveSpeed, setMoveSpeed] = useState(() => {
    const saved = localStorage.getItem('catMoveSpeed');
    return saved ? parseFloat(saved) : 1.0;
  });

  useEffect(() => {
    const handleKeyDown = (e: KeyboardEvent) => {
      if (e.key === 'Escape' || e.key === 'p' || e.key === 'P') {
        e.preventDefault();
        setPaused(!isPaused);
      }
    };

    window.addEventListener('keydown', handleKeyDown);
    return () => window.removeEventListener('keydown', handleKeyDown);
  }, [isPaused, setPaused]);

  const handleSensitivityChange = (value: number) => {
    setSpinSensitivity(value);
    localStorage.setItem('catSpinSensitivity', value.toString());
    window.dispatchEvent(new CustomEvent('spinSensitivityChanged', { detail: value }));
  };
  
  const handleMoveSpeedChange = (value: number) => {
    setMoveSpeed(value);
    localStorage.setItem('catMoveSpeed', value.toString());
    window.dispatchEvent(new CustomEvent('moveSpeedChanged', { detail: value }));
  };

  const handleResume = () => {
    setPaused(false);
  };

  const handleQuitGame = () => {
    // Reset game mode to show the selection screen
    useGameStore.setState({ 
      gameMode: null,
      storyMode: {
        ...useGameStore.getState().storyMode,
        isActive: false
      }
    });
    
    // Reset game state
    const gameStore = useGameStore.getState();
    gameStore.setGameOver(false);
    gameStore.setPaused(false);
    gameStore.setCurrentWave(1);
    gameStore.setWaveTransition(false);
    useGameStore.setState({ enemiesKilled: 0 });
  };

  if (!isPaused || isMenuPaused) return null;

  return (
    <div className="pause-overlay">
      <div className="pause-modal">
        {/* Title bar */}
        <div className="pause-title-bar">
          <h1 className="pause-title">PAUSED</h1>
        </div>
        
        {/* Content */}
        <div className="pause-content">
          {/* Sensitivity setting */}
          <div className="pause-section">
            <h2 className="pause-section-title">TURN SENSITIVITY</h2>
            <div className="pause-control-panel">
              <div className="sensitivity-control">
                <span className="sensitivity-label">SLOW</span>
                <input
                  type="range"
                  min="0.1"
                  max="2.0"
                  step="0.1"
                  value={spinSensitivity}
                  onChange={(e) => handleSensitivityChange(parseFloat(e.target.value))}
                  className="sensitivity-slider"
                  style={{
                    background: `linear-gradient(to right, #4a90e2 0%, #4a90e2 ${(spinSensitivity - 0.1) / 1.9 * 100}%, #555 ${(spinSensitivity - 0.1) / 1.9 * 100}%, #555 100%)`
                  }}
                />
                <span className="sensitivity-label">FAST</span>
                <span className="sensitivity-value">
                  {spinSensitivity.toFixed(1)}
                </span>
              </div>
            </div>
          </div>

          {/* Movement Speed setting */}
          <div className="pause-section">
            <h2 className="pause-section-title">MOVEMENT SPEED</h2>
            <div className="pause-control-panel">
              <div className="sensitivity-control">
                <span className="sensitivity-label">SLOW</span>
                <input
                  type="range"
                  min="0.5"
                  max="2.0"
                  step="0.1"
                  value={moveSpeed}
                  onChange={(e) => handleMoveSpeedChange(parseFloat(e.target.value))}
                  className="sensitivity-slider"
                  style={{
                    background: `linear-gradient(to right, #4a90e2 0%, #4a90e2 ${(moveSpeed - 0.5) / 1.5 * 100}%, #555 ${(moveSpeed - 0.5) / 1.5 * 100}%, #555 100%)`
                  }}
                />
                <span className="sensitivity-label">FAST</span>
                <span className="sensitivity-value">
                  {moveSpeed.toFixed(1)}
                </span>
              </div>
            </div>
          </div>

          {/* Controls */}
          <div className="pause-section">
            <h2 className="pause-section-title">CONTROLS</h2>
            <div className="controls-grid">
              <div className="control-row">
                <span className="control-action">Move</span>
                <span className="control-key">W A S D</span>
              </div>
              <div className="control-row">
                <span className="control-action">Run</span>
                <span className="control-key">SHIFT</span>
              </div>
              <div className="control-row">
                <span className="control-action">Attack/Cast</span>
                <span className="control-key">SPACE</span>
              </div>
              <div className="control-row">
                <span className="control-action">Quick Slots</span>
                <span className="control-key">1-7</span>
              </div>
            </div>
          </div>

          {/* Buttons */}
          <div className="pause-buttons">
            <button
              onClick={handleResume}
              className="pause-button pause-button-resume"
            >
              Resume Game
            </button>
            <button
              onClick={handleQuitGame}
              className="pause-button pause-button-quit"
            >
              Quit Game
            </button>
          </div>

          {/* Instructions */}
          <div className="pause-instructions">
            Press <span className="pause-key-hint">ESC</span> or <span className="pause-key-hint">P</span> to resume
          </div>
        </div>
      </div>
    </div>
  );
};

export default PauseMenu;