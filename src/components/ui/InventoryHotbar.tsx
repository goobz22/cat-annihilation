import { useEffect, memo, useCallback } from 'react';
import { useGameStore } from '../../lib/store/gameStore';

const InventoryHotbar = () => {
  const inventory = useGameStore((state) => state.player.inventory);
  const activeSlot = useGameStore((state) => state.player.activeSlot);
  const setActiveSlot = useGameStore((state) => state.setActiveSlot);

  const handleKeyPress = useCallback((e: KeyboardEvent) => {
    const key = parseInt(e.key);
    if (key >= 1 && key <= 7) {
      setActiveSlot(key - 1);
    }
  }, [setActiveSlot]);

  useEffect(() => {
    window.addEventListener('keydown', handleKeyPress);
    return () => window.removeEventListener('keydown', handleKeyPress);
  }, [handleKeyPress]);

  return (
    <div className="inventory-hotbar-container">
      {inventory.map((item, index) => (
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
      {inventory[activeSlot] && (
        <div className="hotbar-active-item">
          <span className="hotbar-active-item-name">{inventory[activeSlot]?.name}</span>
        </div>
      )}
    </div>
  );
};

export default memo(InventoryHotbar);