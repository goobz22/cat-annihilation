import { useEffect, useState } from 'react';

interface WaveTransitionProps {
  isVisible: boolean;
  currentWave: number;
  nextWaveEnemies: number;
}

const WaveTransition = ({ isVisible, currentWave, nextWaveEnemies }: WaveTransitionProps) => {
  const [showComplete, setShowComplete] = useState(false);
  const [showNext, setShowNext] = useState(false);
  const [showEnemies, setShowEnemies] = useState(false);

  useEffect(() => {
    if (isVisible) {
      // Stagger the text animations
      setShowComplete(true);
      setTimeout(() => setShowNext(true), 800);
      setTimeout(() => setShowEnemies(true), 1600);
    } else {
      setShowComplete(false);
      setShowNext(false);
      setShowEnemies(false);
    }
  }, [isVisible]);

  if (!isVisible) return null;

  return (
    <div style={{
      position: 'fixed',
      top: '50%',
      left: '50%',
      transform: 'translate(-50%, -50%)',
      zIndex: 1000,
      textAlign: 'center',
      color: 'white',
      textShadow: '2px 2px 4px rgba(0,0,0,0.8)',
      fontFamily: 'monospace',
      fontSize: '24px',
      fontWeight: 'bold'
    }}>
      <div style={{
        background: 'rgba(0,0,0,0.8)',
        padding: '30px',
        borderRadius: '15px',
        border: '2px solid #444',
        minWidth: '400px'
      }}>
        {showComplete && (
          <div style={{
            color: '#00ff00',
            fontSize: '32px',
            marginBottom: '20px',
            animation: 'fadeIn 0.5s ease-in'
          }}>
            🎉 WAVE {currentWave} COMPLETE! 🎉
          </div>
        )}
        
        {showNext && (
          <div style={{
            color: '#ffff00',
            fontSize: '28px',
            marginBottom: '15px',
            animation: 'fadeIn 0.5s ease-in'
          }}>
            WAVE {currentWave + 1} STARTING SOON...
          </div>
        )}
        
        {showEnemies && (
          <div style={{
            color: '#ffffff',
            fontSize: '20px',
            animation: 'fadeIn 0.5s ease-in'
          }}>
            🐕 {nextWaveEnemies} ENEMIES INCOMING 🐕
          </div>
        )}
      </div>
      
      <style>{`
        @keyframes fadeIn {
          from { opacity: 0; transform: translateY(20px); }
          to { opacity: 1; transform: translateY(0); }
        }
      `}</style>
    </div>
  );
};

export default WaveTransition;