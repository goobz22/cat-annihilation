import { useEffect, useState, memo, useCallback } from 'react';
import { useGameStore } from '../../lib/store/gameStore';

const InventoryPopup = () => {
  const [isOpen, setIsOpen] = useState(false);
  const [selectedBagItem, setSelectedBagItem] = useState<string | null>(null);
  const [selectedQuickSlot, setSelectedQuickSlot] = useState<number | null>(null);
  const [draggedItem, setDraggedItem] = useState<string | null>(null);
  const [draggedFromSlot, setDraggedFromSlot] = useState<number | null>(null);
  
  const inventory = useGameStore((state) => state.player.inventory);
  const inventoryBag = useGameStore((state) => state.inventoryBag);
  const setInventorySlot = useGameStore((state) => state.setInventorySlot);
  const removeFromInventoryBag = useGameStore((state) => state.removeFromInventoryBag);
  const addToInventoryBag = useGameStore((state) => state.addToInventoryBag);
  const setMenuPaused = useGameStore((state) => state.setMenuPaused);

  useEffect(() => {
    const handleKeyPress = (e: KeyboardEvent) => {
      if (e.key === 'i' || e.key === 'I') {
        e.preventDefault();
        e.stopPropagation();
        setIsOpen(!isOpen);
        // Reset selections when closing
        if (isOpen) {
          setSelectedBagItem(null);
          setSelectedQuickSlot(null);
        }
        return;
      }
      
      if (!isOpen) return;
      
      if (e.key === 'Escape') {
        setIsOpen(false);
        setSelectedBagItem(null);
        setSelectedQuickSlot(null);
      }
    };

    window.addEventListener('keydown', handleKeyPress);
    return () => window.removeEventListener('keydown', handleKeyPress);
  }, [isOpen]);

  // Pause/unpause game when inventory opens/closes
  useEffect(() => {
    setMenuPaused(isOpen);
  }, [isOpen, setMenuPaused]);

  const handleBagItemClick = useCallback((itemId: string) => {
    setSelectedBagItem(selectedBagItem === itemId ? null : itemId);
    setSelectedQuickSlot(null);
  }, [selectedBagItem]);

  const handleQuickSlotClick = useCallback((slotIndex: number) => {
    // Allow all slots 1-9 (indices 0-8)
    if (slotIndex < 0 || slotIndex > 8) return;
    
    setSelectedQuickSlot(selectedQuickSlot === slotIndex ? null : slotIndex);
    setSelectedBagItem(null);
  }, [selectedQuickSlot]);

  const handleAssignItem = useCallback(() => {
    if (selectedBagItem && selectedQuickSlot !== null) {
      const bagItem = inventoryBag.find(item => item.id === selectedBagItem);
      if (bagItem) {
        const currentItemInSlot = inventory[selectedQuickSlot];
        
        // If there's already an item in the slot, move it back to inventory
        if (currentItemInSlot) {
          addToInventoryBag(currentItemInSlot);
        }
        
        // Remove item from bag and equip it
        removeFromInventoryBag(bagItem.id);
        setInventorySlot(selectedQuickSlot, bagItem);
        setSelectedBagItem(null);
        setSelectedQuickSlot(null);
      }
    }
  }, [selectedBagItem, selectedQuickSlot, inventoryBag, inventory, setInventorySlot, removeFromInventoryBag, addToInventoryBag]);

  const handleRemoveItem = useCallback(() => {
    if (selectedQuickSlot !== null) {
      const item = inventory[selectedQuickSlot];
      if (item) {
        addToInventoryBag(item);
        setInventorySlot(selectedQuickSlot, null);
        setSelectedQuickSlot(null);
      }
    }
  }, [selectedQuickSlot, inventory, setInventorySlot, addToInventoryBag]);

  // Drag and Drop handlers
  const handleDragStart = useCallback((e: React.DragEvent, itemId: string, fromSlot?: number) => {
    setDraggedItem(itemId);
    setDraggedFromSlot(fromSlot ?? null);
    
    // Create a custom drag image (optional)
    const dragImage = e.currentTarget.cloneNode(true) as HTMLElement;
    dragImage.style.transform = 'rotate(5deg)';
    dragImage.style.opacity = '0.8';
    document.body.appendChild(dragImage);
    e.dataTransfer.setDragImage(dragImage, 20, 20);
    setTimeout(() => document.body.removeChild(dragImage), 0);
    
    e.dataTransfer.effectAllowed = 'move';
    e.dataTransfer.setData('text/plain', itemId);
  }, []);

  const handleDragEnd = useCallback(() => {
    setDraggedItem(null);
    setDraggedFromSlot(null);
  }, []);

  const handleDragOver = useCallback((e: React.DragEvent) => {
    e.preventDefault();
    e.dataTransfer.dropEffect = 'move';
  }, []);

  const handleDropOnSlot = useCallback((e: React.DragEvent, targetSlot: number) => {
    e.preventDefault();
    
    if (draggedItem && targetSlot >= 0 && targetSlot <= 8) {
      // Find the item being dragged
      let item;
      const currentItemInSlot = inventory[targetSlot];
      
      if (draggedFromSlot !== null) {
        // Dragging from equipment slot to equipment slot
        item = inventory[draggedFromSlot];
        
        // If target slot has an item, move it back to inventory
        if (currentItemInSlot) {
          addToInventoryBag(currentItemInSlot);
        }
        
        // Clear source slot and set target slot
        setInventorySlot(draggedFromSlot, null);
        setInventorySlot(targetSlot, item);
      } else {
        // Dragging from inventory bag to equipment slot
        item = inventoryBag.find(bagItem => bagItem.id === draggedItem);
        
        if (item) {
          // If target slot has an item, move it back to inventory
          if (currentItemInSlot) {
            addToInventoryBag(currentItemInSlot);
          }
          
          // Remove item from inventory bag and equip it
          removeFromInventoryBag(item.id);
          setInventorySlot(targetSlot, item);
        }
      }
    }
    
    setDraggedItem(null);
    setDraggedFromSlot(null);
  }, [draggedItem, draggedFromSlot, inventory, inventoryBag, setInventorySlot, removeFromInventoryBag, addToInventoryBag]);

  const handleDropOnInventory = useCallback((e: React.DragEvent) => {
    e.preventDefault();
    
    if (draggedFromSlot !== null && draggedItem) {
      // Move item from equipment slot back to inventory bag
      const item = inventory[draggedFromSlot];
      if (item) {
        addToInventoryBag(item);
        setInventorySlot(draggedFromSlot, null);
      }
    }
    
    setDraggedItem(null);
    setDraggedFromSlot(null);
  }, [draggedFromSlot, draggedItem, inventory, addToInventoryBag, setInventorySlot]);

  if (!isOpen) {
    return null;
  }

  return (
    <div className="inventory-overlay">
      <div className="inventory-modal">
        {/* RuneScape-style title bar */}
        <div className="inventory-title-bar">
          <span className="inventory-title">
            Inventory
          </span>
          <button
            onClick={() => setIsOpen(false)}
            className="inventory-close-btn"
          >
            ×
          </button>
        </div>
        
        <div className="inventory-content">
          
          {/* Quick Slots Section */}
          <div className="inventory-section">
            <h3 className="inventory-section-title">
              Equipment Slots (1-9)
            </h3>
            <div className="equipment-slots-grid">
              {inventory.map((item, index) => {
                const actualIndex = index;
                const isSelected = selectedQuickSlot === actualIndex;
                const isDraggingFromThisSlot = draggedFromSlot === actualIndex;
                return (
                  <div
                    key={actualIndex}
                    className={`inventory-slot ${isSelected ? 'selected' : ''} ${isDraggingFromThisSlot ? 'dragging' : ''}`}
                    onClick={() => handleQuickSlotClick(actualIndex)}
                    onDragOver={handleDragOver}
                    onDrop={(e) => handleDropOnSlot(e, actualIndex)}
                    title={item ? item.name : `Slot ${actualIndex + 1}`}
                  >
                    {item ? (
                      <span 
                        draggable
                        onDragStart={(e) => handleDragStart(e, item.id, actualIndex)}
                        onDragEnd={handleDragEnd}
                        className="inventory-slot-icon"
                      >
                        {item.icon}
                      </span>
                    ) : (
                      <span className="inventory-slot-number">
                        {actualIndex + 1}
                      </span>
                    )}
                  </div>
                );
              })}
            </div>
          </div>

          {/* Inventory Bag Section */}
          <div className="inventory-bag-container">
            <h3 className="inventory-section-title">
              Inventory (20 items)
            </h3>
            <div 
              className="inventory-bag-grid"
              onDragOver={handleDragOver}
              onDrop={handleDropOnInventory}
            >
              {/* Create 20 slots */}
              {Array.from({ length: 20 }, (_, slotIndex) => {
                const item = inventoryBag[slotIndex];
                const isSelected = item && selectedBagItem === item.id;
                return (
                  <div
                    key={slotIndex}
                    className={`inventory-slot ${isSelected ? 'selected' : ''}`}
                    onClick={() => item && handleBagItemClick(item.id)}
                    title={item ? item.name : 'Empty'}
                  >
                    {item ? (
                      <span 
                        draggable
                        onDragStart={(e) => handleDragStart(e, item.id)}
                        onDragEnd={handleDragEnd}
                        className="inventory-slot-icon"
                      >
                        {item.icon}
                      </span>
                    ) : (
                      <span className="inventory-slot-empty">
                        •
                      </span>
                    )}
                  </div>
                );
              })}
            </div>
          </div>

          {/* Action Buttons - RuneScape style */}
          <div className="inventory-actions">
            {selectedBagItem && selectedQuickSlot !== null && (
              <button
                onClick={handleAssignItem}
                className="inventory-action-btn"
              >
                Equip → Slot {selectedQuickSlot + 1}
              </button>
            )}
            
            {selectedQuickSlot !== null && inventory[selectedQuickSlot] && (
              <button
                onClick={handleRemoveItem}
                className="inventory-action-btn"
              >
                Unequip Slot {selectedQuickSlot + 1}
              </button>
            )}
          </div>
        </div>
      </div>
    </div>
  );
};

export default memo(InventoryPopup);