import { useEffect } from 'react';
import { useGameStore } from '../../lib/store/gameStore';

const Dialog = () => {
  const dialog = useGameStore(state => state.storyMode.dialog);
  const closeDialog = useGameStore(state => state.closeDialog);
  const setMenuPaused = useGameStore(state => state.setMenuPaused);

  // Pause/unpause game when dialog opens/closes
  useEffect(() => {
    setMenuPaused(dialog.isOpen);
  }, [dialog.isOpen, setMenuPaused]);

  // Listen for escape key to close dialog
  useEffect(() => {
    const handleKeyPress = (e: KeyboardEvent) => {
      if (e.key === 'Escape' && dialog.isOpen) {
        closeDialog();
      }
    };

    window.addEventListener('keydown', handleKeyPress);
    return () => window.removeEventListener('keydown', handleKeyPress);
  }, [dialog.isOpen, closeDialog]);

  if (!dialog.isOpen) return null;

  return (
    <div className="dialog-overlay">
      <div className="dialog-container">
        {/* Dialog Header */}
        <div className="dialog-header">
          <div className="dialog-npc-info">
            <h3 className="dialog-npc-name">{dialog.npcName}</h3>
            <span className="dialog-npc-role">{dialog.npcRole.replace('-', ' ')}</span>
          </div>
          <button 
            className="dialog-close-btn"
            onClick={closeDialog}
          >
            ✕
          </button>
        </div>

        {/* Dialog Content */}
        <div className="dialog-content">
          <div className="dialog-message">
            {dialog.message}
          </div>

          {/* Dialog Options (if any) */}
          {dialog.options && dialog.options.length > 0 && (
            <div className="dialog-options">
              {dialog.options.map((option, index) => (
                <button
                  key={option.id}
                  className="dialog-option-btn"
                  onClick={() => {
                    if (option.action) {
                      option.action();
                    }
                    closeDialog();
                  }}
                >
                  {option.text}
                </button>
              ))}
            </div>
          )}
        </div>

        {/* Dialog Footer */}
        <div className="dialog-footer">
          {(!dialog.options || dialog.options.length === 0) && (
            <button 
              className="dialog-continue-btn"
              onClick={closeDialog}
            >
              Continue (Press ESC)
            </button>
          )}
        </div>
      </div>
    </div>
  );
};

export default Dialog;