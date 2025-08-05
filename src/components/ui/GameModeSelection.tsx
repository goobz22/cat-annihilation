import { useState } from 'react';
import { useGameStore } from '../../lib/store/gameStore';
import { clearAllProgress, getStorageInfo } from '../../lib/store/gameStatePersistence';
import { getPlayerStarterCustomization } from '../../config/clanCustomizations';
import { CatCustomization } from '../game/CatCharacter/CustomizableCatMesh';
import CustomizableCatMesh from '../game/CatCharacter/CustomizableCatMesh';
import { Canvas } from '@react-three/fiber';
import { useCatCustomization } from '../../contexts/CatCustomizationContext';

const GameModeSelection = () => {
  const setGameMode = useGameStore(state => state.setGameMode);
  const setPlayerClan = useGameStore(state => state.setPlayerClan);
  const { setPlayerCustomization } = useCatCustomization();
  const [selectedClan, setSelectedClan] = useState<'MistClan' | 'StormClan' | 'EmberClan' | 'FrostClan' | null>(null);
  const [showClanSelection, setShowClanSelection] = useState(false);
  const [showCustomization, setShowCustomization] = useState(false);
  const [selectedMode, setSelectedMode] = useState<'survival' | 'story' | null>(null);
  const [catCustomization, setCatCustomization] = useState<CatCustomization>({
    primaryColor: '#964B00',
    eyeColor: '#4CAF50',
    noseColor: '#FF69B4',
    pattern: 'solid',
    patternColor: '#5D4037',
    earSize: 'normal',
    tailLength: 'normal',
    furLength: 'medium',
    bodyType: 'normal'
  });

  // Track whether user has made their selection
  const storyMode = useGameStore(state => state.storyMode);
  const [gameModeSelected, setGameModeSelected] = useState(false);
  const [showResetConfirm, setShowResetConfirm] = useState(false);

  const handleResetProgress = () => {
    if (showResetConfirm) {
      clearAllProgress();
      window.location.reload(); // Refresh to reset everything
    } else {
      setShowResetConfirm(true);
      setTimeout(() => setShowResetConfirm(false), 3000); // Auto-hide after 3 seconds
    }
  };


  const handleSurvivalMode = () => {
    setSelectedMode('survival');
    setShowCustomization(true);
  };
  
  const confirmSurvivalMode = () => {
    setGameMode('survival');
    setGameModeSelected(true);
    
    // Reset game state for survival mode
    const gameStore = useGameStore.getState();
    gameStore.setGameOver(false);
    
    // Ensure player has full health
    const fullHealth = gameStore.catStats.baseHealth + gameStore.catStats.healthBonus;
    gameStore.setPlayerHealth(fullHealth);
    useGameStore.setState(state => ({
      player: {
        ...state.player,
        health: fullHealth,
        maxHealth: fullHealth
      }
    }));
    
    // Set customization in context
    setPlayerCustomization(catCustomization);
    
    gameStore.setCurrentWave(1);
    gameStore.setWaveTransition(false);
    useGameStore.setState({ enemiesKilled: 0 });
    
    console.log(`💗 Player health set to ${fullHealth}/${fullHealth}`);
    console.log('⚔️ Survival mode selected! Fight endless waves of enemies.');
  };

  const handleStoryMode = () => {
    setShowClanSelection(true);
  };

  const handleClanSelection = (clan: 'MistClan' | 'StormClan' | 'EmberClan' | 'FrostClan') => {
    setSelectedClan(clan);
    // Update cat customization with clan colors
    const clanCustomization = getPlayerStarterCustomization(clan);
    setCatCustomization(clanCustomization);
  };

  const handleConfirmStoryMode = () => {
    if (selectedClan) {
      setSelectedMode('story');
      setShowClanSelection(false);
      setShowCustomization(true);
    }
  };
  
  const confirmStoryModeWithCustomization = () => {
    if (selectedClan) {
      setPlayerClan(selectedClan);
      setGameMode('story');
      
      // Activate story mode and set player customization
      useGameStore.setState(state => ({
        storyMode: {
          ...state.storyMode,
          isActive: true,
          playerClan: selectedClan
        }
      }));
      
      // Set customization in context
      setPlayerCustomization(catCustomization);
      
      setGameModeSelected(true);
      console.log(`🏛️ Joined ${selectedClan}! Welcome to your new clan.`);
    }
  };

  // Don't show if a game mode has already been selected (after all hooks are called)
  if (storyMode.isActive || gameModeSelected) return null;

  const clanData = {
    MistClan: {
      name: 'MistClan',
      emoji: '🌊',
      territory: 'Misty Marshlands & Creek Valleys',
      specialty: 'Stealth, fishing, swimming',
      values: 'Adaptability • Cunning • Patience',
      description: 'Swift as flowing water, MistClan cats are masters of stealth and adaptation.'
    },
    StormClan: {
      name: 'StormClan', 
      emoji: '⚡',
      territory: 'Rocky Highlands & Pine Forests',
      specialty: 'Mountain combat, endurance',
      values: 'Strength • Honor • Determination',
      description: 'Strong as mountain stone, StormClan warriors are feared in battle.'
    },
    EmberClan: {
      name: 'EmberClan',
      emoji: '🍂',
      territory: 'Autumn Forests & Oak Groves', 
      specialty: 'Hunting, herb knowledge',
      values: 'Wisdom • Tradition • Healing',
      description: 'Wise as ancient oaks, EmberClan preserves the old ways and healing arts.'
    },
    FrostClan: {
      name: 'FrostClan',
      emoji: '❄️',
      territory: 'Northern Pines & Snowy Valleys',
      specialty: 'Winter survival, tracking',
      values: 'Resilience • Loyalty • Community', 
      description: 'Hardy as winter winds, FrostClan cats endure through the harshest trials.'
    }
  };

  // Cat customization options
  const colors = {
    fur: ['#964B00', '#8B4513', '#D2691E', '#CD853F', '#F4A460', '#DEB887', '#2D3748', '#4A5568', '#718096', '#E2E8F0'],
    eyes: ['#4CAF50', '#2196F3', '#FF9800', '#9C27B0', '#F44336', '#00BCD4', '#FFEB3B', '#795548'],
    nose: ['#FF69B4', '#FF1493', '#C71585', '#DB7093', '#000000', '#8B4513']
  };

  const patterns: Array<CatCustomization['pattern']> = ['solid', 'tabby', 'calico', 'tuxedo', 'siamese', 'spots'];
  const sizes: Array<'small' | 'normal' | 'large'> = ['small', 'normal', 'large'];
  const lengths: Array<'short' | 'normal' | 'long'> = ['short', 'normal', 'long'];
  const bodyTypes: Array<'slim' | 'normal' | 'chubby'> = ['slim', 'normal', 'chubby'];

  return (
    <div className="game-mode-overlay">
      <div className="game-mode-container">
        {showCustomization ? (
          // Cat Customization Screen
          <div className="cat-customization-screen">
            <div className="customization-header">
              <h2>Customize Your Cat</h2>
              <p>{selectedMode === 'story' ? `${selectedClan} Warrior` : 'Survival Warrior'}</p>
            </div>
            
            <div className="customization-layout">
              {/* 3D Preview */}
              <div className="cat-preview">
                <Canvas key={`${catCustomization.pattern}-${catCustomization.primaryColor}`} camera={{ position: [0, 0, 5], fov: 50 }}>
                  <ambientLight intensity={0.5} />
                  <directionalLight position={[5, 5, 5]} intensity={1} />
                  <CustomizableCatMesh customization={catCustomization} scale={1.5} />
                </Canvas>
                <div className="mobile-rotate-hint">Drag to rotate</div>
              </div>
              
              {/* Customization Options */}
              <div className="customization-options">
                {/* Fur Color */}
                <div className="customizer-section">
                  <h3>Fur Color</h3>
                  <div className="color-grid">
                    {colors.fur.map(color => (
                      <button
                        key={color}
                        className={`color-option ${catCustomization.primaryColor === color ? 'selected' : ''}`}
                        style={{ backgroundColor: color }}
                        onClick={() => setCatCustomization({ ...catCustomization, primaryColor: color })}
                      />
                    ))}
                  </div>
                </div>

                {/* Eye Color */}
                <div className="customizer-section">
                  <h3>Eye Color</h3>
                  <div className="color-grid">
                    {colors.eyes.map(color => (
                      <button
                        key={color}
                        className={`color-option ${catCustomization.eyeColor === color ? 'selected' : ''}`}
                        style={{ backgroundColor: color }}
                        onClick={() => setCatCustomization({ ...catCustomization, eyeColor: color })}
                      />
                    ))}
                  </div>
                </div>

                {/* Pattern */}
                <div className="customizer-section">
                  <h3>Pattern</h3>
                  <div className="option-grid">
                    {patterns.map(pattern => (
                      <button
                        key={pattern}
                        className={`text-option ${catCustomization.pattern === pattern ? 'selected' : ''}`}
                        onClick={() => {
                          // Set appropriate pattern color based on pattern type
                          let patternColor = '#000000'; // Default
                          if (pattern === 'tabby') {
                            patternColor = '#5D4037'; // Dark brown stripes
                          } else if (pattern === 'spots') {
                            patternColor = '#3E2723'; // Dark spots
                          } else if (pattern === 'tuxedo') {
                            patternColor = '#FFFFFF'; // White for tuxedo
                          } else if (pattern === 'calico') {
                            patternColor = '#FF6B35'; // Orange patches
                          } else if (pattern === 'siamese') {
                            patternColor = '#3E2723'; // Dark extremities
                          }
                          
                          setCatCustomization({ 
                            ...catCustomization, 
                            pattern,
                            patternColor,
                            secondaryColor: pattern === 'calico' ? '#000000' : undefined
                          });
                        }}
                      >
                        {pattern}
                      </button>
                    ))}
                  </div>
                </div>

                {/* Body Type */}
                <div className="customizer-section">
                  <h3>Body Type</h3>
                  <div className="option-grid">
                    {bodyTypes.map(type => (
                      <button
                        key={type}
                        className={`text-option ${catCustomization.bodyType === type ? 'selected' : ''}`}
                        onClick={() => setCatCustomization({ ...catCustomization, bodyType: type })}
                      >
                        {type}
                      </button>
                    ))}
                  </div>
                </div>
              </div>
            </div>
            
            <div className="customization-actions">
              <button 
                className="back-button"
                onClick={() => {
                  setShowCustomization(false);
                  if (selectedMode === 'story') {
                    setShowClanSelection(true);
                  }
                }}
              >
                ← Back
              </button>
              
              <button 
                className="confirm-button active"
                onClick={() => {
                  if (selectedMode === 'survival') {
                    confirmSurvivalMode();
                  } else {
                    confirmStoryModeWithCustomization();
                  }
                }}
              >
                Start Game
              </button>
            </div>
          </div>
        ) : !showClanSelection ? (
          // Game Mode Selection
          <>
            <div className="game-mode-header">
              <h1>🐱 Cat Warriors</h1>
              <p>Choose your adventure</p>
            </div>

            <div className="game-mode-options">
              <div 
                className="game-mode-option survival-mode"
                onClick={handleSurvivalMode}
              >
                <div className="mode-icon">⚔️</div>
                <h3>Survival Mode</h3>
                <p>Endless waves of enemies</p>
                <p className="mode-description">
                  Face unlimited waves of enemies and see how long you can survive.
                  Perfect your combat skills and climb the leaderboards.
                </p>
                <div className="mode-features">
                  • Endless wave-based combat<br/>
                  • Weapon skill progression<br/>
                  • Increasing difficulty<br/>
                  • Quick action gameplay
                </div>
              </div>

              <div 
                className="game-mode-option story-mode"
                onClick={handleStoryMode}
              >
                <div className="mode-icon">📜</div>
                <h3>Story Mode</h3>
                <p>Quest-driven clan adventure</p>
                <p className="mode-description">
                  Join a clan and embark on epic quests. Experience a Warriors-inspired 
                  adventure with rich storytelling and character progression.
                </p>
                <div className="mode-features">
                  • Choose from 4 unique clans<br/>
                  • Quest-based progression<br/>
                  • Rich storylines & dialogue<br/>
                  • Clan politics & relationships
                </div>
              </div>
            </div>

            {/* Development Status Notice */}
            <div className="development-notice">
              <div className="development-icon">🚧</div>
              <div className="development-content">
                <p><strong>Development Status:</strong> All game modes are in active live development</p>
                <p>Leaderboards coming soon! Track your progress and compete with other warriors.</p>
              </div>
            </div>
          </>
        ) : (
          // Clan Selection
          <>
            <div className="clan-selection-header">
              <h2>Choose Your Clan</h2>
              <p>Your clan will shape your destiny and determine your starting territory.</p>
            </div>

            <div className="clan-options">
              {(Object.keys(clanData) as Array<keyof typeof clanData>).map((clanId) => {
                const clan = clanData[clanId];
                const isSelected = selectedClan === clanId;
                
                return (
                  <div 
                    key={clanId}
                    className={`clan-option ${isSelected ? 'selected' : ''}`}
                    onClick={() => handleClanSelection(clanId)}
                  >
                    <div className="clan-emoji">{clan.emoji}</div>
                    <div className="clan-info">
                      <h3>{clan.name}</h3>
                      <p className="clan-territory">{clan.territory}</p>
                      <p className="clan-specialty">Specialty: {clan.specialty}</p>
                      <p className="clan-values">{clan.values}</p>
                      <p className="clan-description">{clan.description}</p>
                    </div>
                  </div>
                );
              })}
            </div>

            <div className="clan-selection-actions">
              <button 
                className="back-button"
                onClick={() => setShowClanSelection(false)}
              >
                ← Back to Mode Selection
              </button>
              
              <button 
                className={`confirm-button ${selectedClan ? 'active' : 'disabled'}`}
                onClick={handleConfirmStoryMode}
                disabled={!selectedClan}
              >
                {selectedClan ? `Join ${clanData[selectedClan].name}` : 'Select a Clan'}
              </button>
            </div>
          </>
        )}
        
        {/* Reset Progress Control */}
        <div className="game-mode-debug">
          <button 
            className={`reset-button ${showResetConfirm ? 'confirm' : ''}`}
            onClick={handleResetProgress}
            title="Reset all progress and start fresh"
          >
            {showResetConfirm ? '⚠️ Click again to confirm' : '🗑️ Reset Progress'}
          </button>
        </div>
      </div>
    </div>
  );
};

export default GameModeSelection;