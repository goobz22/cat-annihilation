import { useState } from 'react';
import { useGameStore } from '@/lib/store/gameStore';

/**
 * Day/night cycle display
 */
const TimeDisplay = () => {
  const { currentTime, isNight } = useGameStore((state) => state.dayCycle);
  
  // Convert time (0-1) to hours (0-24)
  const hours = Math.floor(currentTime * 24);
  const minutes = Math.floor((currentTime * 24 * 60) % 60);
  
  // Format time as HH:MM
  const formattedTime = `${hours.toString().padStart(2, '0')}:${minutes.toString().padStart(2, '0')}`;
  
  return (
    <div className="flex items-center space-x-2 mb-2">
      <div className={`w-3 h-3 rounded-full ${isNight ? 'bg-blue-300' : 'bg-yellow-300'}`} />
      <span className="text-white font-bold">{formattedTime}</span>
      <span className="text-white text-sm">{isNight ? 'Night' : 'Day'}</span>
    </div>
  );
};

/**
 * Zone information display
 */
const ZoneDisplay = () => {
  const currentZone = useGameStore((state) => state.player.currentZone);
  
  if (!currentZone) return null;
  
  return (
    <div className="text-white mb-2">
      <div className="font-bold">{currentZone.name}</div>
      <div className="text-sm">
        {currentZone.isPvp ? 
          <span className="text-red-400">PvP Zone</span> : 
          <span className="text-green-400">Safe Zone</span>
        }
      </div>
    </div>
  );
};

/**
 * Health and stats display
 */
const StatsDisplay = () => {
  const player = useGameStore((state) => state.player);
  const cat = player.cat;
  
  if (!cat) return null;
  
  // Health percentage
  const healthPercent = (cat.health / cat.maxHealth) * 100;
  
  return (
    <div className="mb-3">
      <div className="mb-2">
        <div className="text-white text-sm mb-1">Health</div>
        <div className="w-32 h-2 bg-gray-700 rounded-full overflow-hidden">
          <div 
            className="h-full bg-red-600" 
            style={{ width: `${healthPercent}%` }}
          />
        </div>
      </div>
      
      <div className="text-xs text-white space-y-1">
        <div>Attack: {cat.attack}</div>
        <div>Defense: {cat.defense}</div>
        <div>Speed: {cat.speed}</div>
        <div>Level: {cat.level}</div>
      </div>
    </div>
  );
};

/**
 * Currency display
 */
const CurrencyDisplay = () => {
  const cat = useGameStore((state) => state.player.cat);
  
  if (!cat) return null;
  
  return (
    <div className="text-white mb-2">
      <span className="text-yellow-400 font-bold">{cat.currency}</span>
      <span className="text-xs ml-1">Coins</span>
    </div>
  );
};

/**
 * Controls help panel
 */
const ControlsHelp = () => {
  const [showControls, setShowControls] = useState(false);
  
  if (!showControls) {
    return (
      <button 
        className="px-2 py-1 bg-gray-700 hover:bg-gray-600 rounded text-xs text-white mr-2 mb-1"
        onClick={() => setShowControls(true)}
      >
        Controls
      </button>
    );
  }
  
  return (
    <div className="absolute left-1/2 top-1/2 transform -translate-x-1/2 -translate-y-1/2 bg-black bg-opacity-90 p-4 rounded-lg text-white max-w-md z-50">
      <h3 className="text-lg font-bold mb-3">Controls</h3>
      <div className="space-y-2 mb-4">
        <div className="grid grid-cols-2 gap-x-4">
          <div className="font-bold">W</div>
          <div>Move Forward</div>
          <div className="font-bold">S</div>
          <div>Move Backward</div>
          <div className="font-bold">A</div>
          <div>Rotate Left</div>
          <div className="font-bold">D</div>
          <div>Rotate Right</div>
          <div className="font-bold">Space</div>
          <div>Attack</div>
        </div>
      </div>
      <button 
        className="px-3 py-1 bg-gray-700 hover:bg-gray-600 rounded text-sm"
        onClick={() => setShowControls(false)}
      >
        Close
      </button>
    </div>
  );
};

/**
 * Editor mode toggle
 */
const EditorToggle = () => {
  const editorMode = useGameStore((state) => state.editorMode);
  const setEditorMode = useGameStore((state) => state.setEditorMode);
  
  const toggleEditor = () => {
    setEditorMode({ isActive: !editorMode.isActive });
  };
  
  return (
    <button 
      className={`px-2 py-1 rounded text-xs text-white mb-1 ${
        editorMode.isActive ? 'bg-blue-600 hover:bg-blue-500' : 'bg-gray-700 hover:bg-gray-600'
      }`}
      onClick={toggleEditor}
    >
      {editorMode.isActive ? 'Exit Editor' : 'Enter Editor'}
    </button>
  );
};

/**
 * Editor tools panel
 */
const EditorPanel = () => {
  const editorMode = useGameStore((state) => state.editorMode);
  const setEditorMode = useGameStore((state) => state.setEditorMode);
  
  if (!editorMode.isActive) return null;
  
  const tools = [
    { id: 'terrain', label: 'Terrain' },
    { id: 'zones', label: 'Zones' },
    { id: 'items', label: 'Items' },
    { id: 'cats', label: 'Cats' },
  ];
  
  return (
    <div className="absolute left-64 top-4 bg-black bg-opacity-80 p-4 rounded-lg text-white z-40">
      <h3 className="text-lg font-bold mb-3">Editor Tools</h3>
      <div className="space-y-2">
        {tools.map((tool) => (
          <button
            key={tool.id}
            className={`block w-full px-3 py-2 rounded text-sm ${
              editorMode.selectedTool === tool.id ? 'bg-blue-600' : 'bg-gray-700 hover:bg-gray-600'
            }`}
            onClick={() => setEditorMode({ selectedTool: tool.id as any })}
          >
            {tool.label}
          </button>
        ))}
      </div>
    </div>
  );
};

/**
 * Main game interface
 */
const GameInterface = () => {
  return (
    <div className="absolute inset-0 pointer-events-none">
      {/* Main UI Panel - Upper Left Corner */}
      <div className="absolute top-4 left-4 bg-black bg-opacity-60 backdrop-blur-sm p-4 rounded-lg pointer-events-auto max-w-xs">
        <TimeDisplay />
        <ZoneDisplay />
        <StatsDisplay />
        <CurrencyDisplay />
        
        {/* Action Buttons */}
        <div className="space-y-2">
          <button className="px-2 py-1 bg-gray-700 hover:bg-gray-600 rounded text-xs text-white w-full">
            Inventory
          </button>
          <div className="flex space-x-1">
            <ControlsHelp />
            <EditorToggle />
          </div>
        </div>
      </div>
      
      {/* Editor panel - positioned next to main UI when active */}
      <div className="pointer-events-auto">
        <EditorPanel />
      </div>
    </div>
  );
};

export default GameInterface; 