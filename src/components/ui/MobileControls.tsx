import React, { useEffect, useState, useRef } from 'react';
import { useGameStore } from '../../lib/store/gameStore';
import VirtualJoystick from './VirtualJoystick';

const MobileControls: React.FC = () => {
  const [isMobile, setIsMobile] = useState(false);
  const isPaused = useGameStore((state) => state.isPaused);
  const isGameOver = useGameStore((state) => state.isGameOver);
  const movementRef = useRef({ x: 0, y: 0 });
  const activeKeysRef = useRef(new Set<string>());

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

  const handleJoystickMove = (position: { x: number; y: number }) => {
    const deadzone = 0.1;
    const threshold = 0.3;
    
    // Update movement reference
    movementRef.current = position;
    
    // Calculate joystick distance/magnitude for variable speed
    const distance = Math.sqrt(position.x * position.x + position.y * position.y);
    const clampedDistance = Math.min(distance, 1.0); // Ensure max distance is 1.0
    
    // Dispatch mobile speed multiplier event based on joystick distance
    // Map distance to speed: 0.0 = walking speed, 1.0 = full sprint speed
    const speedMultiplier = Math.max(0.1, clampedDistance); // Minimum 10% speed
    const mobileSpeedEvent = new CustomEvent('mobileSpeedChange', { 
      detail: speedMultiplier 
    });
    window.dispatchEvent(mobileSpeedEvent);
    
    // Determine which keys should be active based on joystick position
    const newActiveKeys = new Set<string>();
    
    if (Math.abs(position.x) > deadzone || Math.abs(position.y) > deadzone) {
      // Forward/backward (Y axis)
      if (position.y > threshold) {
        newActiveKeys.add('w');
      } else if (position.y < -threshold) {
        newActiveKeys.add('s');
      }
      
      // Left/right (X axis) 
      if (position.x < -threshold) {
        newActiveKeys.add('a');
      } else if (position.x > threshold) {
        newActiveKeys.add('d');
      }
    }
    
    // Handle key changes
    const currentKeys = activeKeysRef.current;
    
    // Release keys that are no longer active
    for (const key of currentKeys) {
      if (!newActiveKeys.has(key)) {
        handleMovement(key, false);
      }
    }
    
    // Press keys that are newly active
    for (const key of newActiveKeys) {
      if (!currentKeys.has(key)) {
        handleMovement(key, true);
      }
    }
    
    activeKeysRef.current = newActiveKeys;
  };

  const handleJoystickStop = () => {
    // Release all movement keys
    for (const key of activeKeysRef.current) {
      handleMovement(key, false);
    }
    activeKeysRef.current.clear();
    movementRef.current = { x: 0, y: 0 };
    
    // Reset mobile speed when joystick is released
    const mobileSpeedEvent = new CustomEvent('mobileSpeedChange', { 
      detail: 1.0 
    });
    window.dispatchEvent(mobileSpeedEvent);
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
          <VirtualJoystick
            onMove={handleJoystickMove}
            onStop={handleJoystickStop}
            size={100}
            knobSize={54}
          />
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