import { useEffect, memo, useCallback, useState } from 'react';
import { useGameStore } from '../../lib/store/gameStore';

const InventoryHotbar = () => {
  const inventory = useGameStore((state) => state.player.inventory);
  const activeSlot = useGameStore((state) => state.player.activeSlot);
  const setActiveSlot = useGameStore((state) => state.setActiveSlot);
  
  // Track if we're on mobile
  const [isMobile, setIsMobile] = useState(false);

  useEffect(() => {
    const checkMobile = () => {
      const mobile = window.innerWidth <= 768;
      setIsMobile(mobile);
      
      // If switching to mobile and active slot is beyond mobile limit, reset to slot 0
      if (mobile && activeSlot >= 5) {
        setActiveSlot(0);
      }
    };
    
    checkMobile();
    window.addEventListener('resize', checkMobile);
    return () => window.removeEventListener('resize', checkMobile);
  }, [activeSlot, setActiveSlot]);

  const handleKeyPress = useCallback((e: KeyboardEvent) => {
    const key = parseInt(e.key);
    const maxSlot = isMobile ? 5 : 9;
    if (key >= 1 && key <= maxSlot) {
      setActiveSlot(key - 1);
    }
  }, [setActiveSlot, isMobile]);

  useEffect(() => {
    window.addEventListener('keydown', handleKeyPress);
    return () => window.removeEventListener('keydown', handleKeyPress);
  }, [handleKeyPress]);

  // Limit slots to 5 on mobile, 9 on desktop
  const maxSlots = isMobile ? 5 : 9;
  const displayInventory = inventory.slice(0, maxSlots);

  return (
    <div className="inventory-hotbar-container">
      {displayInventory.map((item, index) => (
        <div
          key={index}
          className={`hotbar-slot ${activeSlot === index ? 'active' : 'inactive'}`}
          onClick={() => setActiveSlot(index)}
          style={activeSlot === index && item?.color ? { '--item-color': item.color } as any : undefined}
        >
          <div className="hotbar-slot-content">
            {item ? (
              <span className="hotbar-slot-icon">{item.icon}</span>
            ) : (
              <span className="hotbar-slot-number">{index + 1}</span>
            )}
            <span className="hotbar-slot-key">
              {index + 1}
            </span>
          </div>
        </div>
      ))}
      {inventory[activeSlot] && activeSlot < maxSlots && (
        <div className="hotbar-active-item">
          <span className="hotbar-active-item-name">{inventory[activeSlot]?.name}</span>
        </div>
      )}
    </div>
  );
};

export default memo(InventoryHotbar);