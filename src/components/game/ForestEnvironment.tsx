import { useRef, useState, useEffect } from 'react';
import { useFrame } from '@react-three/fiber';
import * as THREE from 'three';
import { useGameStore } from '@/lib/store/gameStore';

/**
 * Tree model component
 */
const Tree = ({ position, scale = 1, type = 'pine' }: { position: [number, number, number]; scale?: number; type?: 'pine' | 'oak' }) => {
  // Create refs for animation
  const treeRef = useRef<THREE.Group>(null);
  const animOffset = useRef(Math.random() * Math.PI * 2); // Random animation offset
  
  // Animate tree swaying slightly in the wind
  useFrame((_, delta) => {
    if (treeRef.current) {
      const time = animOffset.current + delta * 0.5;
      const swayAmount = 0.01;
      treeRef.current.rotation.x = Math.sin(time) * swayAmount;
      treeRef.current.rotation.z = Math.cos(time * 0.7) * swayAmount;
    }
  });
  
  // Generate tree based on type
  if (type === 'pine') {
    return (
      <group ref={treeRef} position={position} scale={[scale, scale, scale]}>
        {/* Tree trunk */}
        <mesh castShadow receiveShadow position={[0, 2, 0]}>
          <cylinderGeometry args={[0.2, 0.3, 4, 8]} />
          <meshStandardMaterial color="#8B4513" roughness={0.9} />
        </mesh>
        
        {/* Tree foliage - layers of cones */}
        <mesh castShadow position={[0, 4, 0]}>
          <coneGeometry args={[1.5, 3, 8]} />
          <meshStandardMaterial color="#2d4c2a" roughness={0.8} />
        </mesh>
        
        <mesh castShadow position={[0, 5.5, 0]}>
          <coneGeometry args={[1.2, 2.5, 8]} />
          <meshStandardMaterial color="#2d4c2a" roughness={0.8} />
        </mesh>
        
        <mesh castShadow position={[0, 6.5, 0]}>
          <coneGeometry args={[0.8, 2, 8]} />
          <meshStandardMaterial color="#2d4c2a" roughness={0.8} />
        </mesh>
      </group>
    );
  } else {
    // Oak tree with wider canopy
    return (
      <group ref={treeRef} position={position} scale={[scale, scale, scale]}>
        {/* Tree trunk */}
        <mesh castShadow receiveShadow position={[0, 2, 0]}>
          <cylinderGeometry args={[0.3, 0.4, 4, 8]} />
          <meshStandardMaterial color="#8B4513" roughness={0.9} />
        </mesh>
        
        {/* Tree foliage - sphere for the canopy */}
        <mesh castShadow position={[0, 5, 0]}>
          <sphereGeometry args={[2, 8, 8]} />
          <meshStandardMaterial color="#3a5f38" roughness={0.8} />
        </mesh>
      </group>
    );
  }
};

/**
 * Bush component
 */
const Bush = ({ position, scale = 1 }: { position: [number, number, number]; scale?: number }) => {
  return (
    <mesh castShadow position={position} scale={[scale, scale, scale]}>
      <sphereGeometry args={[0.7, 8, 8]} />
      <meshStandardMaterial color="#3a5f38" roughness={0.9} />
    </mesh>
  );
};

/**
 * Rock component
 */
const Rock = ({ position, scale = 1 }: { position: [number, number, number]; scale?: number }) => {
  // Generate a random rotation for variety
  const rotationY = Math.random() * Math.PI * 2;
  
  return (
    <mesh castShadow receiveShadow position={position} rotation={[0, rotationY, 0]} scale={[scale, scale, scale]}>
      <dodecahedronGeometry args={[0.5, 0]} />
      <meshStandardMaterial color="#777777" roughness={1} />
    </mesh>
  );
};

/**
 * Forest ground with grass texture
 */
const ForestGround = () => {
  const [texture, setTexture] = useState<THREE.Texture | null>(null);
  
  // Create simple but detailed grass texture
  useEffect(() => {
    const createGrassTexture = () => {
      const canvas = document.createElement('canvas');
      canvas.width = 256;
      canvas.height = 256;
      const context = canvas.getContext('2d');
      
      if (context) {
        // Base grass color
        context.fillStyle = '#4a7c59';
        context.fillRect(0, 0, 256, 256);
        
        // Add grass patterns - simpler approach
        for (let i = 0; i < 1000; i++) {
          const x = Math.random() * 256;
          const y = Math.random() * 256;
          const length = Math.random() * 4 + 2;
          const width = 1;
          
          // Vary green shade
          const variation = Math.floor(Math.random() * 20) - 10;
          context.fillStyle = `rgb(${84 + variation}, ${140 + variation}, 99)`;
          context.fillRect(x, y, width, length);
        }
        
        // Add some darker spots
        for (let i = 0; i < 50; i++) {
          const x = Math.random() * 256;
          const y = Math.random() * 256;
          const size = Math.random() * 3 + 1;
          
          context.fillStyle = 'rgba(45, 80, 50, 0.5)';
          context.beginPath();
          context.arc(x, y, size, 0, Math.PI * 2);
          context.fill();
        }
      }
      
      const newTexture = new THREE.CanvasTexture(canvas);
      newTexture.wrapS = newTexture.wrapT = THREE.RepeatWrapping;
      newTexture.repeat.set(20, 20);
      setTexture(newTexture);
    };
    
    createGrassTexture();
  }, []);
  
  return (
    <mesh rotation={[-Math.PI / 2, 0, 0]} position={[0, -0.1, 0]} receiveShadow>
      <planeGeometry args={[10000, 10000]} />
      <meshStandardMaterial 
        color="#4a7c59" // Always show grass color as base
        map={texture || undefined} // Use texture if available
        roughness={0.9}
        metalness={0}
      />
    </mesh>
  );
};

/**
 * Main forest environment component
 */
const ForestEnvironment = () => {
  // Add ambient sounds
  useEffect(() => {
    const audio = new Audio('/assets/sounds/forest-ambient.mp3');
    audio.loop = true;
    audio.volume = 0.3;
    
    // Uncomment when you have the sound file
    // audio.play().catch(e => console.log('Audio playback error:', e));
    
    return () => {
      audio.pause();
      audio.currentTime = 0;
    };
  }, []);
  
  // Generate trees in a natural pattern around the starting area
  const trees = [];
  const bushes = [];
  const rocks = [];
  
  // Add trees in a circle around the center, but not in the immediate center
  for (let i = 0; i < 100; i++) {
    const angle = Math.random() * Math.PI * 2;
    const distance = 20 + Math.random() * 200;
    const x = Math.sin(angle) * distance;
    const z = Math.cos(angle) * distance;
    
    // Get height at this position (for now we'll use 0)
    const y = 0;
    
    // Randomize tree type and scale
    const treetype = Math.random() > 0.3 ? 'pine' : 'oak';
    const scale = 0.5 + Math.random() * 0.5;
    
    trees.push(
      <Tree 
        key={`tree-${i}`} 
        position={[x, y, z]} 
        scale={scale}
        type={treetype as 'pine' | 'oak'} 
      />
    );
  }
  
  // Add additional trees in a grid pattern for better coverage
  for (let x = -300; x <= 300; x += 80) {
    for (let z = -300; z <= 300; z += 80) {
      // Skip the center area to keep it clear for the player
      const distanceFromCenter = Math.sqrt(x * x + z * z);
      if (distanceFromCenter < 50) continue;
      
      // Add some randomness to avoid perfect grid
      const offsetX = x + (Math.random() - 0.5) * 40;
      const offsetZ = z + (Math.random() - 0.5) * 40;
      
      // Only add tree if not too close to existing trees
      const treetype = Math.random() > 0.4 ? 'pine' : 'oak';
      const scale = 0.4 + Math.random() * 0.6;
      
      trees.push(
        <Tree 
          key={`grid-tree-${x}-${z}`} 
          position={[offsetX, 0, offsetZ]} 
          scale={scale}
          type={treetype as 'pine' | 'oak'} 
        />
      );
    }
  }
  
  // Add some bushes
  for (let i = 0; i < 60; i++) {
    const angle = Math.random() * Math.PI * 2;
    const distance = 10 + Math.random() * 150;
    const x = Math.sin(angle) * distance;
    const z = Math.cos(angle) * distance;
    
    bushes.push(
      <Bush 
        key={`bush-${i}`} 
        position={[x, 0, z]} 
        scale={0.5 + Math.random() * 0.5}
      />
    );
  }
  
  // Add some rocks
  for (let i = 0; i < 40; i++) {
    const angle = Math.random() * Math.PI * 2;
    const distance = 5 + Math.random() * 120;
    const x = Math.sin(angle) * distance;
    const z = Math.cos(angle) * distance;
    
    rocks.push(
      <Rock 
        key={`rock-${i}`} 
        position={[x, 0, z]} 
        scale={0.5 + Math.random() * 1}
      />
    );
  }
  
  return (
    <group>
      <ForestGround />
      {trees}
      {bushes}
      {rocks}
      
      {/* Add forest fog */}
      <fog attach="fog" args={['#4c6156', 30, 150]} />
    </group>
  );
};

export default ForestEnvironment;