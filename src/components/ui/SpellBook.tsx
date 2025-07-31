import { useEffect, useState, memo, useCallback } from 'react';
import { useGameStore } from '../../lib/store/gameStore';

const SpellBook = () => {
  const [isOpen, setIsOpen] = useState(false);
  const [selectedSpellIndex, setSelectedSpellIndex] = useState(0);
  const inventory = useGameStore((state) => state.player.inventory);
  const availableSpells = useGameStore((state) => state.availableSpells);
  const setInventorySlot = useGameStore((state) => state.setInventorySlot);
  const setMenuPaused = useGameStore((state) => state.setMenuPaused);
  
  // Use available spells from the spellbook collection
  const spells = availableSpells;
  
  const selectSpell = useCallback((spellIndex: number) => {
    if (spellIndex >= 0 && spellIndex < spells.length) {
      const selectedSpell = spells[spellIndex];
      
      // Find the original slot of the selected spell to clear it
      const originalSpellSlot = inventory.findIndex(item => item && item.id === selectedSpell.id);
      
      // Always put selected spell in slot 0 (first hotbar slot, shows as "1" to user)
      setInventorySlot(0, selectedSpell);
      
      // Clear original spell slot if it was equipped elsewhere
      if (originalSpellSlot !== -1 && originalSpellSlot !== 0) {
        setInventorySlot(originalSpellSlot, null);
      }
      
      setIsOpen(false);
    }
  }, [spells, inventory, setInventorySlot]);
  
  useEffect(() => {
    const handleKeyPress = (e: KeyboardEvent) => {
      if (e.key === 'm' || e.key === 'M') {
        e.preventDefault();
        e.stopPropagation();
        setIsOpen(!isOpen);
        return;
      }
      
      if (!isOpen) return;
      
      // Only prevent specific navigation keys when spellbook is open
      if (['ArrowUp', 'ArrowDown', 'ArrowLeft', 'ArrowRight', 'Enter', 'Escape'].includes(e.key)) {
        e.preventDefault();
        e.stopPropagation();
      }
      
      if (e.key === 'ArrowUp') {
        e.preventDefault();
        setSelectedSpellIndex((prev) => Math.max(0, prev - 1));
      } else if (e.key === 'ArrowDown') {
        e.preventDefault();
        setSelectedSpellIndex((prev) => Math.min(spells.length - 1, prev + 1));
      } else if (e.key === 'ArrowLeft') {
        e.preventDefault();
        setSelectedSpellIndex((prev) => Math.max(0, prev - 2));
      } else if (e.key === 'ArrowRight') {
        e.preventDefault();
        setSelectedSpellIndex((prev) => Math.min(spells.length - 1, prev + 2));
      } else if (e.key === 'Enter') {
        e.preventDefault();
        e.stopPropagation();
        selectSpell(selectedSpellIndex);
      } else if (e.key === 'Escape') {
        setIsOpen(false);
      }
    };

    window.addEventListener('keydown', handleKeyPress);
    return () => window.removeEventListener('keydown', handleKeyPress);
  }, [isOpen, selectedSpellIndex, spells.length, selectSpell]);

  // Pause/unpause game when spellbook opens/closes
  useEffect(() => {
    setMenuPaused(isOpen);
  }, [isOpen, setMenuPaused]);

  if (!isOpen) {
      return null;
  }
  

  return (
    <div className="spellbook-overlay">
      <div className="spellbook-modal">
        {/* Close X button */}
        <button
          onClick={() => setIsOpen(false)}
          className="spellbook-close-btn"
        >
          ×
        </button>
        
        <div className="spellbook-content">
          <h2 className="spellbook-title">
            📖 Spell Tome
          </h2>
          
          <p className="spellbook-instructions">
            Arrow keys • Enter/Click to select
          </p>
          
          <div className="spellbook-grid">
            {/* Show 9 slots total - 4 filled, 5 empty */}
            {Array.from({ length: 9 }, (_, index) => {
              const spell = spells[index];
              const isEmpty = !spell;
              const isSelected = selectedSpellIndex === index && !isEmpty;
              
              return (
                <div
                  key={isEmpty ? `empty-${index}` : spell.id}
                  className={`spell-slot ${isSelected ? 'selected' : ''} ${isEmpty ? 'empty' : ''}`}
                  onClick={() => !isEmpty && setSelectedSpellIndex(index)}
                  title={spell ? spell.element : 'Empty Spell Slot'}
                >
                  <div className="spell-slot-center">
                    {spell ? (
                      <span className="spell-icon">
                        {spell.icon}
                      </span>
                    ) : (
                      <span className="spell-number">
                        {index + 1}
                      </span>
                    )}
                  </div>
                  
                  {/* Enhanced Selection indicator */}
                  {isSelected && (
                    <>
                      <div className="spell-selection-pulse" />
                      <div className="spell-selection-ping" />
                    </>
                  )}
                </div>
              );
            })}
          </div>
          
          {spells.length === 0 && (
            <div className="spellbook-empty-state">
              <div className="spellbook-empty-icon">📜</div>
              <div className="spellbook-empty-title">No spells discovered yet...</div>
              <div className="spellbook-empty-subtitle">Explore the world to find magical knowledge</div>
            </div>
          )}
          
          {/* Submit Button */}
          <div className="spellbook-actions">
            <button
              onClick={() => selectSpell(selectedSpellIndex)}
              disabled={selectedSpellIndex >= spells.length || !spells[selectedSpellIndex]}
              className="spellbook-select-btn"
            >
              Select Spell
            </button>
          </div>
        </div>
      </div>
    </div>
  );
};

export default memo(SpellBook);