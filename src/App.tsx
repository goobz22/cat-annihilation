import { lazy, Suspense } from 'react';
import GameProvider from './components/ui/GameProvider';
import GameModeSelection from './components/ui/GameModeSelection';
import { useGameStore } from './lib/store/gameStore';

// Lazy load ALL game components so they don't load until needed
const BasicScene = lazy(() => import('./components/game/BasicScene'));
const InventoryHotbar = lazy(() => import('./components/ui/InventoryHotbar'));
const SpellBook = lazy(() => import('./components/ui/SpellBook'));
const InventoryPopup = lazy(() => import('./components/ui/InventoryPopup'));
const GameOverScreen = lazy(() => import('./components/ui/GameOverScreen'));
const WaveDisplay = lazy(() => import('./components/ui/WaveDisplay'));
const WeaponSkills = lazy(() => import('./components/ui/WeaponSkills'));
const CatStats = lazy(() => import('./components/ui/CatStats'));
const PauseMenu = lazy(() => import('./components/ui/PauseMenu'));
const MobileControls = lazy(() => import('./components/ui/MobileControls'));
const QuestBook = lazy(() => import('./components/ui/QuestBook'));
const QuestTracker = lazy(() => import('./components/ui/QuestTracker'));
const QuestObjectiveOverlay = lazy(() => import('./components/ui/QuestObjectiveOverlay'));
const Dialog = lazy(() => import('./components/ui/Dialog'));

function App() {
  return (
    <GameProvider>
      <AppContent />
    </GameProvider>
  );
}

function AppContent() {
  const gameMode = useGameStore(state => state.gameMode);
  const storyModeActive = useGameStore(state => state.storyMode.isActive);
  
  // Check if a game mode has been selected
  const gameModeSelected = (gameMode === 'survival' || gameMode === 'story') || storyModeActive;
  
  return (
    <div className="app-container">
      {/* Always show game mode selection until a mode is chosen */}
      <GameModeSelection />
      
      {/* Only render game components after mode selection */}
      {gameModeSelected && (
        <Suspense fallback={<div className="loading-screen">Loading game...</div>}>
          {/* UI Components */}
          <WaveDisplay />
          <WeaponSkills />
          <CatStats />
          <QuestObjectiveOverlay />
          <InventoryHotbar />
          <SpellBook />
          <InventoryPopup />
          <QuestBook />
          <GameOverScreen />
          <PauseMenu />
          <MobileControls />
          <Dialog />
          
          {/* Background Systems */}
          <QuestTracker />
          
          {/* 3D Game Scene - This will only load after mode selection */}
          <BasicScene />
        </Suspense>
      )}
    </div>
  );
}

export default App;