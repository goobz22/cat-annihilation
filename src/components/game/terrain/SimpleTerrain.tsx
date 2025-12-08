import React, { useMemo, useRef } from 'react';
import * as THREE from 'three';
import { useFrame } from '@react-three/fiber';
import { useGameStore } from '../../../lib/store/gameStore';

interface SimpleTerrainProps {
  playerPosition: THREE.Vector3;
}

// Biome centers
const BIOMES = {
  mist: { x: -150, z: -150, color: '#4A90E2' },
  storm: { x: 150, z: -150, color: '#5A5A5A' },
  ember: { x: 150, z: 150, color: '#D2691E' },
  frost: { x: -150, z: 150, color: '#87CEEB' },
  gathering: { x: 0, z: 0, color: '#FFD700' }
};

const SimpleTerrain: React.FC<SimpleTerrainProps> = ({ playerPosition }) => {
  const meshRef = useRef<THREE.Mesh>(null);

  // Create a simple blended terrain texture
  const terrainMaterial = useMemo(() => {
    const canvas = document.createElement('canvas');
    canvas.width = 1024;
    canvas.height = 1024;
    const ctx = canvas.getContext('2d')!;
    
    // Base grass
    ctx.fillStyle = '#567D46';
    ctx.fillRect(0, 0, 1024, 1024);
    
    // Add biome colors as gradients
    Object.entries(BIOMES).forEach(([biome, data]) => {
      const x = (data.x + 250) * 2; // Map world coords to texture coords
      const z = (data.z + 250) * 2;
      
      const gradient = ctx.createRadialGradient(x, z, 0, x, z, 200);
      gradient.addColorStop(0, data.color);
      gradient.addColorStop(1, 'rgba(86, 125, 70, 0)');
      
      ctx.fillStyle = gradient;
      ctx.fillRect(0, 0, 1024, 1024);
    });
    
    const texture = new THREE.CanvasTexture(canvas);
    texture.needsUpdate = true;
    
    return new THREE.MeshStandardMaterial({
      map: texture,
      roughness: 0.8,
      metalness: 0
    });
  }, []);

  // Update current biome based on player position
  useFrame(() => {
    let currentBiome = 'neutral';
    let minDistance = Infinity;
    
    Object.entries(BIOMES).forEach(([biome, data]) => {
      const distance = Math.sqrt(
        Math.pow(playerPosition.x - data.x, 2) + 
        Math.pow(playerPosition.z - data.z, 2)
      );
      
      if (distance < 100 && distance < minDistance) {
        currentBiome = biome;
        minDistance = distance;
      }
    });
    
    // Update store
    const storedBiome = useGameStore.getState().storyMode.currentBiome;
    if (storedBiome !== currentBiome) {
      useGameStore.setState(state => ({
        storyMode: {
          ...state.storyMode,
          currentBiome: currentBiome as any
        }
      }));
    }
  });

  return (
    <group>
      {/* Main terrain */}
      <mesh 
        ref={meshRef}
        rotation={[-Math.PI / 2, 0, 0]} 
        position={[0, -0.1, 0]} 
        receiveShadow
      >
        <planeGeometry args={[500, 500, 50, 50]} />
        {terrainMaterial}
      </mesh>

      {/* Biome markers */}
      {Object.entries(BIOMES).forEach(([biome, data]) => (
        <group key={biome} position={[data.x, 0, data.z]}>
          {/* Territory marker */}
          <mesh position={[0, 5, 0]}>
            <cylinderGeometry args={[0.5, 0.5, 10, 8]} />
            <meshStandardMaterial color={data.color} emissive={data.color} emissiveIntensity={0.3} />
          </mesh>
          
          {/* Territory ring on ground */}
          <mesh rotation={[-Math.PI / 2, 0, 0]} position={[0, 0.1, 0]}>
            <ringGeometry args={[90, 100, 32]} />
            <meshStandardMaterial 
              color={data.color} 
              transparent 
              opacity={0.2} 
              side={THREE.DoubleSide}
            />
          </mesh>
        </group>
      ))}

      {/* Simple trees */}
      <SimpleVegetation />
      
      {/* Fog */}
      <fog attach="fog" args={['#87CEEB', 30, 300]} />
    </group>
  );
};

/**
 * Simple vegetation without complex systems
 */
const SimpleVegetation: React.FC = () => {
  const trees = useMemo(() => {
    const treeElements = [];
    
    // Add trees around each biome
    Object.entries(BIOMES).forEach(([biome, data]) => {
      for (let i = 0; i < 30; i++) {
        const angle = Math.random() * Math.PI * 2;
        const distance = 20 + Math.random() * 70;
        const x = data.x + Math.sin(angle) * distance;
        const z = data.z + Math.cos(angle) * distance;
        const scale = 0.5 + Math.random() * 0.5;
        
        treeElements.push(
          <group key={`${biome}-tree-${i}`} position={[x, 0, z]} scale={[scale, scale, scale]}>
            <mesh castShadow position={[0, 2, 0]}>
              <cylinderGeometry args={[0.3, 0.4, 4, 6]} />
              <meshStandardMaterial color="#654321" />
            </mesh>
            <mesh castShadow position={[0, 5, 0]}>
              <sphereGeometry args={[2, 8, 6]} />
              <meshStandardMaterial 
                color={biome === 'frost' ? '#0A3A0A' : 
                       biome === 'ember' ? '#D2691E' : '#228B22'} 
              />
            </mesh>
          </group>
        );
      }
    });
    
    return treeElements;
  }, []);

  return <>{trees}</>;
};

export default SimpleTerrain;