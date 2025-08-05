import React, { useState, useEffect } from 'react';
import { Canvas, useFrame, useThree } from '@react-three/fiber';
import { Box, PerspectiveCamera } from '@react-three/drei';
import ForestEnvironment from './ForestEnvironment';
import CatCharacter from './CatCharacter';
import LocalProjectileSystem from './LocalProjectileSystem';
import LocalEnemySystem from './LocalEnemySystem';
import GlobalCollisionSystem from './GlobalCollisionSystem';
import WaveTransition from '../ui/WaveTransition';
import NPCSystem from './NPCSystem';
import StoryEncounterSystem from './StoryEncounterSystem';
import { waveState } from './WaveState';
import { useGameStore } from '../../lib/store/gameStore';

/**
 * Scene-level error boundary to prevent complete scene crashes
 */
class SceneErrorBoundary extends React.Component<
  { children: React.ReactNode; sceneName: string },
  { hasError: boolean; errorCount: number }
> {
  private retryTimeout?: NodeJS.Timeout;

  constructor(props: { children: React.ReactNode; sceneName: string }) {
    super(props);
    this.state = { hasError: false, errorCount: 0 };
  }

  static getDerivedStateFromError() {
    return { hasError: true };
  }

  componentDidCatch(error: Error, errorInfo: React.ErrorInfo) {
    console.error(`[SCENE ERROR BOUNDARY] ${this.props.sceneName} scene error:`, error, errorInfo);
    
    this.setState(prevState => ({
      errorCount: prevState.errorCount + 1
    }));

    // Auto-retry after 5 seconds if error count is low
    if (this.state.errorCount < 2) {
      console.log(`[SCENE ERROR BOUNDARY] Auto-retry attempt ${this.state.errorCount + 1} in 5 seconds...`);
      this.retryTimeout = setTimeout(() => {
        console.log(`[SCENE ERROR BOUNDARY] Attempting to recover ${this.props.sceneName} scene...`);
        this.setState({ hasError: false });
      }, 5000);
    }
  }

  componentWillUnmount() {
    if (this.retryTimeout) {
      clearTimeout(this.retryTimeout);
    }
  }

  render() {
    if (this.state.hasError) {
      console.log(`[SCENE ERROR BOUNDARY] Rendering fallback for ${this.props.sceneName} scene`);
      
      // Render minimal fallback scene
      return (
        <Canvas shadows>
          <color attach="background" args={['#87CEEB']} />
          <PerspectiveCamera makeDefault position={[0, 12, 15]} fov={75} />
          <ambientLight intensity={0.5} />
          <directionalLight position={[10, 10, 5]} intensity={1} />
          
          {/* Minimal ground */}
          <mesh rotation={[-Math.PI / 2, 0, 0]} position={[0, -0.1, 0]} receiveShadow>
            <planeGeometry args={[200, 200]} />
            <meshStandardMaterial color="#7fb069" />
          </mesh>
          
          {/* Basic character placeholder */}
          <mesh position={[0, 1, 0]}>
            <boxGeometry args={[1, 1, 1]} />
            <meshStandardMaterial color="#ffa500" />
          </mesh>
          
          <fog attach="fog" args={['#87CEEB', 30, 150]} />
        </Canvas>
      );
    }

    return this.props.children;
  }
}

function Player() {
  const [pos, setPos] = useState({ x: 0, z: 0 });
  
  useEffect(() => {
    const handleKey = (e: KeyboardEvent) => {
      const speed = 0.5;
      setPos(p => {
        switch(e.key) {
          case 'w': return { ...p, z: p.z - speed };
          case 's': return { ...p, z: p.z + speed };
          case 'a': return { ...p, x: p.x - speed };
          case 'd': return { ...p, x: p.x + speed };
          default: return p;
        }
      });
    };
    
    window.addEventListener('keydown', handleKey);
    return () => window.removeEventListener('keydown', handleKey);
  }, []);
  
  return (
    <Box position={[pos.x, 0.5, pos.z]} args={[1, 1, 1]}>
      <meshStandardMaterial color="orange" />
    </Box>
  );
}

function Tree({ position }: { position: [number, number, number] }) {
  const scale = 0.7 + Math.random() * 0.6;
  return (
    <group position={position} scale={[scale, scale, scale]}>
      {/* Trunk */}
      <mesh position={[0, 2, 0]} castShadow>
        <cylinderGeometry args={[0.3, 0.4, 4, 8]} />
        <meshStandardMaterial color="#654321" />
      </mesh>
      {/* Leaves */}
      <mesh position={[0, 5, 0]} castShadow>
        <sphereGeometry args={[2, 8, 8]} />
        <meshStandardMaterial color="#228B22" />
      </mesh>
    </group>
  );
}

const CameraFollow = () => {
  const { camera } = useThree();
  const playerPosition = useGameStore((state) => state.player.position);

  useFrame(() => {
    if (playerPosition) {
      const { x, z, rotation } = playerPosition;
      const distance = 10.5; // Distance behind player (30% closer: 15 * 0.7)
      const height = 8.4;   // Height above player (30% closer: 12 * 0.7)
      
      // Camera follows behind player based on rotation
      const cameraX = x - Math.sin(rotation) * distance;
      const cameraZ = z - Math.cos(rotation) * distance;
      
      camera.position.x = cameraX;
      camera.position.z = cameraZ;
      camera.position.y = height;
      camera.lookAt(x, 0, z);
    }
  });

  return null;
};

// Projectile management is now handled by LocalProjectileSystem
// These old updaters have been removed to prevent Zustand store issues

const SurvivalScene = () => {
  const [waveTransitionState, setWaveTransitionState] = useState({
    isTransition: false,
    currentWave: 1,
    nextWaveEnemies: 0
  });

  useEffect(() => {
    // Subscribe to wave state changes
    waveState.onStateChange = setWaveTransitionState;
    
    return () => {
      waveState.onStateChange = null;
    };
  }, []);

  return (
    <>
      {/* Wave Transition UI - outside Canvas so it renders on top */}
      <WaveTransition 
        isVisible={waveTransitionState.isTransition}
        currentWave={waveTransitionState.currentWave}
        nextWaveEnemies={waveTransitionState.nextWaveEnemies}
      />
      
      <Canvas shadows>
        <color attach="background" args={['#87CEEB']} />
        <fog attach="fog" args={['#87CEEB', 30, 150]} />
        <PerspectiveCamera makeDefault position={[0, 12, 15]} fov={75} />
        <CameraFollow />
        <ambientLight intensity={0.5} />
        <directionalLight position={[10, 10, 5]} intensity={1} castShadow />
        <CatCharacter />
        <ForestEnvironment />
        
        {/* GLOBAL COLLISION SYSTEM */}
        <GlobalCollisionSystem />
        
        {/* LOCAL ENEMY SYSTEM - SURVIVAL MODE ONLY */}
        <LocalEnemySystem />
        
        {/* LOCAL PROJECTILE SYSTEM */}
        <LocalProjectileSystem />
      </Canvas>
    </>
  );
};

const StoryScene = () => {
  return (
    <Canvas shadows>
      <color attach="background" args={['#87CEEB']} />
      <fog attach="fog" args={['#87CEEB', 30, 150]} />
      <PerspectiveCamera makeDefault position={[0, 12, 15]} fov={75} />
      <CameraFollow />
      <ambientLight intensity={0.5} />
      <directionalLight position={[10, 10, 5]} intensity={1} castShadow />
      <CatCharacter />
      <ForestEnvironment />
      
      {/* GLOBAL COLLISION SYSTEM */}
      <GlobalCollisionSystem />
      
      {/* LOCAL PROJECTILE SYSTEM - Still needed for story combat */}
      <LocalProjectileSystem />
      
      {/* STORY MODE NPCs - Clan members, quest givers, etc */}
      <NPCSystem />
      
      {/* STORY ENCOUNTERS - Practice targets, story-specific enemies */}
      <StoryEncounterSystem />
    </Canvas>
  );
};

export default function BasicScene() {
  const gameMode = useGameStore(state => state.gameMode);
  const storyModeActive = useGameStore(state => state.storyMode.isActive);
  
  console.log('[BASIC SCENE] Rendering with gameMode:', gameMode, 'storyModeActive:', storyModeActive);
  
  // Don't render anything if no game mode has been selected yet
  if (gameMode === 'survival' && !storyModeActive) {
    // This is survival mode - render survival scene with error boundary
    console.log('[BASIC SCENE] Rendering survival mode');
    return (
      <div style={{ width: '100vw', height: '100vh' }} key="survival-scene">
        <SceneErrorBoundary sceneName="Survival">
          <SurvivalScene />
        </SceneErrorBoundary>
      </div>
    );
  }
  
  if (storyModeActive) {
    // This is story mode - render story scene with error boundary
    console.log('[BASIC SCENE] Rendering story mode');
    return (
      <div style={{ width: '100vw', height: '100vh' }} key="story-scene">
        <SceneErrorBoundary sceneName="Story">
          <StoryScene />
        </SceneErrorBoundary>
      </div>
    );
  }

  // No mode selected yet - don't render anything (game mode selection will show)
  console.log('[BASIC SCENE] No mode selected, returning null');
  return null;
}