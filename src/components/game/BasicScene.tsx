import { useState, useEffect } from 'react';
import { Canvas, useFrame, useThree } from '@react-three/fiber';
import { Box, PerspectiveCamera } from '@react-three/drei';
import ForestEnvironment from './ForestEnvironment';
import CatCharacter from './CatCharacter';
import LocalProjectileSystem from './LocalProjectileSystem';
import LocalEnemySystem from './LocalEnemySystem';
import GlobalCollisionSystem from './GlobalCollisionSystem';
import WaveTransition from '../ui/WaveTransition';
import { waveState } from './WaveState';
import { useGameStore } from '../../lib/store/gameStore';

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

const ProjectileUpdater = () => {
  const updateProjectiles = useGameStore((state) => state.updateProjectiles);

  useFrame((_, delta) => {
    updateProjectiles(delta);
  });

  return null;
};

const SimpleProjectileUpdater = () => {
  const projectiles = useGameStore(state => state.projectiles);
  const removeProjectile = useGameStore(state => state.removeProjectile);
  
  useFrame((_, delta) => {
    projectiles.forEach(proj => {
      const speed = proj.type === 'arrow' ? 25 : 15;
      const newX = proj.position.x + Math.sin(proj.rotation) * speed * delta;
      const newZ = proj.position.z + Math.cos(proj.rotation) * speed * delta;
      
      // Update position directly without triggering store update every frame
      proj.position.x = newX;
      proj.position.z = newZ;
      
      // Remove projectiles that are too far away
      const distance = Math.sqrt(newX * newX + newZ * newZ);
      if (distance > 500) {
        removeProjectile(proj.id);
      }
    });
  });
  
  return null;
};

export default function BasicScene() {
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
    <div style={{ width: '100vw', height: '100vh' }}>
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
        
        {/* LOCAL ENEMY SYSTEM - NEVER TOUCHES ZUSTAND STORE */}
        <LocalEnemySystem />
        
        {/* LOCAL PROJECTILE SYSTEM - NEVER TOUCHES ZUSTAND STORE */}
        <LocalProjectileSystem />
      </Canvas>
    </div>
  );
}