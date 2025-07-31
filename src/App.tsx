import BasicScene from './components/game/BasicScene';
import GameInterface from './components/ui/GameInterface';
import GameProvider from './components/ui/GameProvider';
import InventoryHotbar from './components/ui/InventoryHotbar';
import SpellBook from './components/ui/SpellBook';
import InventoryPopup from './components/ui/InventoryPopup';
import GameOverScreen from './components/ui/GameOverScreen';
import WaveDisplay from './components/ui/WaveDisplay';
import WeaponSkills from './components/ui/WeaponSkills';
import CatStats from './components/ui/CatStats';
import PauseMenu from './components/ui/PauseMenu';
import MobileControls from './components/ui/MobileControls';

function App() {
  return (
    <GameProvider>
      <div className="app-container">
        {/* UI Components */}
        <WaveDisplay />
        <WeaponSkills />
        <CatStats />
        <InventoryHotbar />
        <SpellBook />
        <InventoryPopup />
        <GameOverScreen />
        <PauseMenu />
        <MobileControls />
        
        {/* 3D Game Scene */}
        <BasicScene />
      </div>
    </GameProvider>
  );
}

export default App;