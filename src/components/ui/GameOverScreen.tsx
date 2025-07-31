import { useEffect, useState, useCallback } from 'react';
import { useGameStore } from '../../lib/store/gameStore';

const GameOverScreen = () => {
  const isGameOver = useGameStore(state => state.isGameOver);
  const enemies = useGameStore(state => state.enemies);
  const [showScreen, setShowScreen] = useState(false);
  const [deathStats, setDeathStats] = useState({ enemiesKilled: 0, survivalTime: 0 });
  
  useEffect(() => {
    if (isGameOver) {
      // Calculate stats
      const totalEnemies = enemies.length;
      const survivalTime = Math.floor((Date.now() - performance.timing.navigationStart) / 1000);
      setDeathStats({
        enemiesKilled: totalEnemies, // This is a rough estimate
        survivalTime
      });
      
      // Show screen after a short delay
      setTimeout(() => setShowScreen(true), 500);
    } else {
      setShowScreen(false);
    }
  }, [isGameOver, enemies.length]);
  
  const handleRestart = useCallback(() => {
    // Reset game state
    window.location.reload(); // Simple reload for now
  }, []);
  
  // Add keyboard support for restart
  useEffect(() => {
    if (!showScreen) return;
    
    const handleKeyPress = (e: KeyboardEvent) => {
      if (e.key === ' ' || e.key === 'Enter') {
        e.preventDefault();
        handleRestart();
      }
    };
    
    window.addEventListener('keydown', handleKeyPress);
    return () => window.removeEventListener('keydown', handleKeyPress);
  }, [showScreen, handleRestart]);
  
  if (!showScreen) return null;
  
  return (
    <div className="game-over-overlay">
      <div className="game-over-modal">
        <h1 className="game-over-title">YOU DIED</h1>
        
        <div className="game-over-content">
          <p className="game-over-message">Your cat has fallen in battle!</p>
          <div className="game-over-stats">
            <p>Survival Time: {Math.floor(deathStats.survivalTime / 60)}m {deathStats.survivalTime % 60}s</p>
            <p>Enemies Remaining: {enemies.length}</p>
          </div>
        </div>
        
        <div className="game-over-actions">
          <button 
            onClick={handleRestart}
            className="game-over-restart-btn"
          >
            Try Again
          </button>
          
          <p className="game-over-hint">
            Press Space or click to restart
          </p>
        </div>
      </div>
    </div>
  );
};

export default GameOverScreen;