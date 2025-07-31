import React, { useEffect, useState } from 'react';
import { useGameStore } from '../../lib/store/gameStore';

const MobileControls: React.FC = () => {
  const [isMobile, setIsMobile] = useState(false);
  const isPaused = useGameStore((state) => state.isPaused);
  const isGameOver = useGameStore((state) => state.isGameOver);

  useEffect(() => {
    const checkMobile = () => {
      const mobile = window.innerWidth <= 768 || 'ontouchstart' in window;
      setIsMobile(mobile);
    };

    checkMobile();
    window.addEventListener('resize', checkMobile);
    return () => window.removeEventListener('resize', checkMobile);
  }, []);

  const handleMovement = (direction: string, isPressed: boolean) => {
    const event = new KeyboardEvent(isPressed ? 'keydown' : 'keyup', {
      key: direction,
      bubbles: true,
    });
    window.dispatchEvent(event);
  };

  const handleAction = () => {
    const spaceEvent = new KeyboardEvent('keydown', {
      key: ' ',
      bubbles: true,
    });
    window.dispatchEvent(spaceEvent);
    
    setTimeout(() => {
      const spaceUpEvent = new KeyboardEvent('keyup', {
        key: ' ',
        bubbles: true,
      });
      window.dispatchEvent(spaceUpEvent);
    }, 100);
  };

  const handleInventory = () => {
    const inventoryEvent = new KeyboardEvent('keydown', {
      key: 'i',
      bubbles: true,
    });
    window.dispatchEvent(inventoryEvent);
  };

  const handleSpellbook = () => {
    const spellbookEvent = new KeyboardEvent('keydown', {
      key: 'm',
      bubbles: true,
    });
    window.dispatchEvent(spellbookEvent);
  };

  if (!isMobile || isPaused || isGameOver) {
    return null;
  }

  return (
    <>
      {/* Movement Controls - Center Bottom */}
      <div className="mobile-controls">
        <div className="mobile-movement-controls">
          <div className="mobile-dpad">
            <button
              className="mobile-control-btn mobile-btn-up"
              onTouchStart={() => handleMovement('w', true)}
              onTouchEnd={() => handleMovement('w', false)}
              onMouseDown={() => handleMovement('w', true)}
              onMouseUp={() => handleMovement('w', false)}
            >
              ↑
            </button>
            <div className="mobile-dpad-row">
              <button
                className="mobile-control-btn mobile-btn-left"
                onTouchStart={() => handleMovement('a', true)}
                onTouchEnd={() => handleMovement('a', false)}
                onMouseDown={() => handleMovement('a', true)}
                onMouseUp={() => handleMovement('a', false)}
              >
                ←
              </button>
              <button
                className="mobile-control-btn mobile-btn-right"
                onTouchStart={() => handleMovement('d', true)}
                onTouchEnd={() => handleMovement('d', false)}
                onMouseDown={() => handleMovement('d', true)}
                onMouseUp={() => handleMovement('d', false)}
              >
                →
              </button>
            </div>
            <button
              className="mobile-control-btn mobile-btn-down"
              onTouchStart={() => handleMovement('s', true)}
              onTouchEnd={() => handleMovement('s', false)}
              onMouseDown={() => handleMovement('s', true)}
              onMouseUp={() => handleMovement('s', false)}
            >
              ↓
            </button>
          </div>
        </div>
      </div>

      {/* Left Side Action Buttons */}
      <div className="mobile-left-actions">
        {/* Spellbook Button */}
        <button
          className="mobile-control-btn mobile-btn-menu"
          onTouchStart={handleSpellbook}
          onMouseDown={handleSpellbook}
        >
          📖
        </button>
        
        {/* Sprint Button */}
        <button
          className="mobile-control-btn mobile-btn-run"
          onTouchStart={() => handleMovement('Shift', true)}
          onTouchEnd={() => handleMovement('Shift', false)}
          onMouseDown={() => handleMovement('Shift', true)}
          onMouseUp={() => handleMovement('Shift', false)}
        >
          🏃
        </button>
      </div>

      {/* Right Side Action Buttons */}
      <div className="mobile-right-actions">
        {/* Inventory Button */}
        <button
          className="mobile-control-btn mobile-btn-menu"
          onTouchStart={handleInventory}
          onMouseDown={handleInventory}
        >
          🎒
        </button>
        
        {/* Attack Button */}
        <button
          className="mobile-control-btn mobile-btn-attack"
          onTouchStart={handleAction}
          onMouseDown={handleAction}
        >
          ⚔️
        </button>
      </div>
    </>
  );
};

export default MobileControls;