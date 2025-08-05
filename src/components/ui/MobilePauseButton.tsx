import { useEffect, useState } from 'react';
import { useGameStore } from '../../lib/store/gameStore';

const MobilePauseButton = () => {
  const [isMobile, setIsMobile] = useState(false);
  const isPaused = useGameStore((state) => state.isPaused);
  const isGameOver = useGameStore((state) => state.isGameOver);
  const isMenuPaused = useGameStore((state) => state.isMenuPaused);
  const setPaused = useGameStore((state) => state.setPaused);

  useEffect(() => {
    const checkMobile = () => {
      setIsMobile(window.innerWidth <= 768);
    };
    
    checkMobile();
    window.addEventListener('resize', checkMobile);
    return () => window.removeEventListener('resize', checkMobile);
  }, []);

  const handlePause = () => {
    setPaused(true);
  };

  // Only show on mobile when game is not paused and not game over
  if (!isMobile || isPaused || isGameOver || isMenuPaused) {
    return null;
  }

  return (
    <button
      className="mobile-pause-button"
      onClick={handlePause}
      onTouchStart={handlePause}
    >
      ⏸️
    </button>
  );
};

export default MobilePauseButton;