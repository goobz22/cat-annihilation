import { useState } from 'react';
import { useGameStore } from '../../lib/store/gameStore';

/**
 * Day/night cycle display
 */
const TimeDisplay = () => {
  const { currentTime, isNight } = useGameStore((state) => state.dayCycle);
  
  // Convert time (0-1) to hours (0-24)
  const hours = Math.floor(currentTime * 24);
  const minutes = Math.floor((currentTime * 24 * 60) % 60);
  
  // Format time as HH:MM
  const formattedTime = `${hours.toString().padStart(2, '0')}:${minutes.toString().padStart(2, '0')}`;
  
  return (
    <div className="time-display">
      <div className={`time-indicator ${isNight ? 'night' : 'day'}`} />
      <span className="time-text">{formattedTime}</span>
      <span className="time-period">{isNight ? 'Night' : 'Day'}</span>
    </div>
  );
};

/**
 * Zone information display
 */
const ZoneDisplay = () => {
  // Note: currentZone not implemented in store yet
  // const currentZone = useGameStore((state) => state.player.currentZone);
  
  // Placeholder zone info
  const currentZone = { name: 'Forest Zone', isPvp: false };
  
  if (!currentZone) return null;
  
  return (
    <div className="zone-display">
      <div className="zone-name">{currentZone.name}</div>
      <div className={`zone-status ${currentZone.isPvp ? 'pvp' : 'safe'}`}>
        {currentZone.isPvp ? 'PvP Zone' : 'Safe Zone'}
      </div>
    </div>
  );
};

/**
 * Health and stats display
 */
const StatsDisplay = () => {
  const player = useGameStore((state) => state.player);
  const cat = player.cat;
  
  if (!cat) return null;
  
  // Health percentage
  const healthPercent = (cat.health / cat.maxHealth) * 100;
  
  return (
    <div className="stats-display">
      <div className="stats-health-section">
        <div className="stats-health-label">Health</div>
        <div className="stats-health-bar">
          <div 
            className="stats-health-fill" 
            style={{ width: `${healthPercent}%` }}
          />
        </div>
      </div>
      
      <div className="stats-attributes">
        <div>Attack: {cat.attack}</div>
        <div>Defense: {cat.defense}</div>
        <div>Speed: {cat.speed}</div>
        <div>Level: {cat.level}</div>
      </div>
    </div>
  );
};

/**
 * Currency display
 */
const CurrencyDisplay = () => {
  const cat = useGameStore((state) => state.player.cat);
  
  if (!cat) return null;
  
  return (
    <div className="currency-display">
      <span className="currency-amount">{cat.currency}</span>
      <span className="currency-label">Coins</span>
    </div>
  );
};

/**
 * Controls help panel
 */
const ControlsHelp = () => {
  const [showControls, setShowControls] = useState(false);
  
  if (!showControls) {
    return (
      <button 
        className="action-button"
        onClick={() => setShowControls(true)}
      >
        Controls
      </button>
    );
  }
  
  return (
    <div className="controls-modal-overlay">
      <h3 className="controls-modal-title">Controls</h3>
      <div className="controls-grid">
        <div className="control-key">W</div>
        <div>Move Forward</div>
        <div className="control-key">S</div>
        <div>Move Backward</div>
        <div className="control-key">A</div>
        <div>Rotate Left</div>
        <div className="control-key">D</div>
        <div>Rotate Right</div>
        <div className="control-key">Space</div>
        <div>Attack</div>
      </div>
      <button 
        className="controls-close-btn"
        onClick={() => setShowControls(false)}
      >
        Close
      </button>
    </div>
  );
};

/**
 * Editor mode toggle
 */
const EditorToggle = () => {
  // Note: editorMode not implemented in store yet
  // const editorMode = useGameStore((state) => state.editorMode);
  // const setEditorMode = useGameStore((state) => state.setEditorMode);
  
  const [isActive, setIsActive] = useState(false);
  
  const toggleEditor = () => {
    setIsActive(!isActive);
  };
  
  return (
    <button 
      className={`action-button ${isActive ? 'active' : ''}`}
      onClick={toggleEditor}
    >
      {isActive ? 'Exit Editor' : 'Enter Editor'}
    </button>
  );
};

/**
 * Editor tools panel
 */
const EditorPanel = () => {
  // Note: editorMode not implemented in store yet
  // For now, we'll just return null (panel is hidden)
  return null;
};

/**
 * Main game interface
 */
const GameInterface = ({ children }: { children: React.ReactNode }) => {
  return (
    <>
      {children}
      <div className="game-interface-overlay">
        {/* Main UI Panel - Upper Left Corner */}
        <div className="game-interface-panel">
          <TimeDisplay />
          <ZoneDisplay />
          <StatsDisplay />
          <CurrencyDisplay />
        </div>
      </div>
    </>
  );
};

export default GameInterface; 