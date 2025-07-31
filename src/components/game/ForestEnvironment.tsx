import { useRef, useState, useEffect } from 'react';
import { useFrame } from '@react-three/fiber';
import * as THREE from 'three';

console.log('[FOREST ENV] ForestEnvironment module loading...');

/**
 * Tree model component
 */
const Tree = ({ position, scale = 1, type = 'pine' }: { position: [number, number, number]; scale?: number; type?: 'pine' | 'oak' }) => {
  console.log('[TREE] Creating tree component:', { position, scale, type });
  
  try {
    // Create refs for animation
    const treeRef = useRef<THREE.Group>(null);
    const animOffset = useRef(Math.random() * Math.PI * 2); // Random animation offset
    
    console.log('[TREE] Tree refs created successfully');
  
    // Animate tree swaying slightly in the wind
    useFrame((_, delta) => {
      try {
        if (treeRef.current) {
          const time = animOffset.current + delta * 0.5;
          const swayAmount = 0.01;
          treeRef.current.rotation.x = Math.sin(time) * swayAmount;
          treeRef.current.rotation.z = Math.cos(time * 0.7) * swayAmount;
        }
      } catch (error) {
        console.error('[TREE] Error in useFrame animation:', error);
      }
    });
  
    // Generate tree based on type
    console.log('[TREE] Generating pine tree JSX');
    if (type === 'pine') {
      return (
        <group ref={treeRef} position={position} scale={[scale, scale, scale]}>
          {/* Tree trunk */}
          <mesh castShadow receiveShadow position={[0, 2, 0]}>
            <cylinderGeometry args={[0.3, 0.4, 4, 8]} />
            <meshStandardMaterial color="#654321" roughness={0.9} />
          </mesh>
          
          {/* Tree foliage - single sphere for better performance */}
          <mesh castShadow position={[0, 5, 0]}>
            <sphereGeometry args={[2, 12, 12]} />
            <meshStandardMaterial color="#228B22" roughness={0.8} />
          </mesh>
        </group>
      );
    } else {
      // Oak tree with wider canopy
      console.log('[TREE] Generating oak tree JSX');
      return (
        <group ref={treeRef} position={position} scale={[scale, scale, scale]}>
          {/* Tree trunk */}
          <mesh castShadow receiveShadow position={[0, 2, 0]}>
            <cylinderGeometry args={[0.3, 0.4, 4, 8]} />
            <meshStandardMaterial color="#654321" roughness={0.9} />
          </mesh>
          
          {/* Tree foliage - sphere for the canopy */}
          <mesh castShadow position={[0, 5, 0]}>
            <sphereGeometry args={[2.5, 12, 12]} />
            <meshStandardMaterial color="#228B22" roughness={0.8} />
          </mesh>
        </group>
      );
    }
  } catch (error) {
    console.error('[TREE] Fatal error in Tree component:', error);
    return null;
  }
};

/**
 * Bush component
 */
const Bush = ({ position, scale = 1 }: { position: [number, number, number]; scale?: number }) => {
  console.log('[BUSH] Creating bush component:', { position, scale });
  try {
    return (
      <mesh castShadow position={position} scale={[scale, scale, scale]}>
        <sphereGeometry args={[0.7, 8, 8]} />
        <meshStandardMaterial color="#3a5f38" roughness={0.9} />
      </mesh>
    );
  } catch (error) {
    console.error('[BUSH] Error in Bush component:', error);
    return null;
  }
};

/**
 * Rock component
 */
const Rock = ({ position, scale = 1 }: { position: [number, number, number]; scale?: number }) => {
  console.log('[ROCK] Creating rock component:', { position, scale });
  try {
    // Generate a random rotation for variety
    const rotationY = Math.random() * Math.PI * 2;
    
    return (
      <mesh castShadow receiveShadow position={position} rotation={[0, rotationY, 0]} scale={[scale, scale, scale]}>
        <dodecahedronGeometry args={[0.5, 0]} />
        <meshStandardMaterial color="#777777" roughness={1} />
      </mesh>
    );
  } catch (error) {
    console.error('[ROCK] Error in Rock component:', error);
    return null;
  }
};

/**
 * Forest ground with grass texture
 */
const ForestGround = () => {
  console.log('[FOREST GROUND] Initializing ForestGround component');
  const [texture, setTexture] = useState<THREE.Texture | null>(null);
  
  // Create simple but detailed grass texture
  useEffect(() => {
    console.log('[FOREST GROUND] Creating grass texture effect');
    const createGrassTexture = () => {
      try {
        console.log('[FOREST GROUND] Starting grass texture creation');
        const canvas = document.createElement('canvas');
        canvas.width = 256;
        canvas.height = 256;
        const context = canvas.getContext('2d');
        
        if (!context) {
          console.error('[FOREST GROUND] Failed to get 2D context');
          return;
        }
        
        console.log('[FOREST GROUND] Canvas context created successfully');
        
        // Base grass color - light green
        context.fillStyle = '#7fb069';
        context.fillRect(0, 0, 256, 256);
        console.log('[FOREST GROUND] Base grass color applied');
        
        // Add grass patterns - simpler approach
        console.log('[FOREST GROUND] Adding grass patterns');
        for (let i = 0; i < 1000; i++) {
          const x = Math.random() * 256;
          const y = Math.random() * 256;
          const length = Math.random() * 4 + 2;
          const width = 1;
          
          // Vary green shade - light green variations
          const variation = Math.floor(Math.random() * 20) - 10;
          context.fillStyle = `rgb(${127 + variation}, ${176 + variation}, 105)`;
          context.fillRect(x, y, width, length);
        }
        console.log('[FOREST GROUND] Grass patterns added');
        
        // Add some darker spots
        console.log('[FOREST GROUND] Adding darker spots');
        for (let i = 0; i < 50; i++) {
          const x = Math.random() * 256;
          const y = Math.random() * 256;
          const size = Math.random() * 3 + 1;
          
          context.fillStyle = 'rgba(45, 80, 50, 0.5)';
          context.beginPath();
          context.arc(x, y, size, 0, Math.PI * 2);
          context.fill();
        }
        console.log('[FOREST GROUND] Darker spots added');
        
        console.log('[FOREST GROUND] Creating THREE texture');
        const newTexture = new THREE.CanvasTexture(canvas);
        newTexture.wrapS = newTexture.wrapT = THREE.RepeatWrapping;
        newTexture.repeat.set(20, 20);
        setTexture(newTexture);
        console.log('[FOREST GROUND] Texture created and set');
      } catch (error) {
        console.error('[FOREST GROUND] Error creating grass texture:', error);
      }
    };
    
    createGrassTexture();
  }, []);
  
  console.log('[FOREST GROUND] Rendering ground mesh, texture:', texture);
  
  try {
    return (
      <mesh rotation={[-Math.PI / 2, 0, 0]} position={[0, -0.1, 0]} receiveShadow>
        <planeGeometry args={[10000, 10000]} />
        <meshStandardMaterial 
          color="#7fb069" // Light green grass color as base
          map={texture || undefined} // Use texture if available
          roughness={0.9}
          metalness={0}
        />
      </mesh>
    );
  } catch (error) {
    console.error('[FOREST GROUND] Error rendering ground mesh:', error);
    return null;
  }
};

/**
 * Main forest environment component
 */
const ForestEnvironment = () => {
  console.log('[FOREST ENV] Initializing main ForestEnvironment component');
  
  // Add ambient sounds
  useEffect(() => {
    console.log('[FOREST ENV] Setting up ambient sounds');
    try {
      const audio = new Audio('/assets/sounds/forest-ambient.mp3');
      audio.loop = true;
      audio.volume = 0.3;
      
      // Uncomment when you have the sound file
      // audio.play().catch(e => console.log('Audio playback error:', e));
      
      return () => {
        audio.pause();
        audio.currentTime = 0;
      };
    } catch (error) {
      console.error('[FOREST ENV] Error setting up ambient sounds:', error);
    }
  }, []);
  
  console.log('[FOREST ENV] Starting to generate environment objects');
  
  // Generate trees in a natural pattern around the starting area
  const trees = [];
  const bushes = [];
  const rocks = [];
  
  // Add trees in a circle around the center, but not in the immediate center
  console.log('[FOREST ENV] Generating circular trees');
  try {
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
    console.log('[FOREST ENV] Generated', trees.length, 'circular trees');
  } catch (error) {
    console.error('[FOREST ENV] Error generating circular trees:', error);
  }
  
  // Add additional trees in a grid pattern for better coverage
  console.log('[FOREST ENV] Generating grid trees');
  try {
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
    console.log('[FOREST ENV] Generated total trees:', trees.length);
  } catch (error) {
    console.error('[FOREST ENV] Error generating grid trees:', error);
  }
  
  // Add some bushes
  console.log('[FOREST ENV] Generating bushes');
  try {
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
    console.log('[FOREST ENV] Generated', bushes.length, 'bushes');
  } catch (error) {
    console.error('[FOREST ENV] Error generating bushes:', error);
  }
  
  // Add some rocks
  console.log('[FOREST ENV] Generating rocks');
  try {
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
    console.log('[FOREST ENV] Generated', rocks.length, 'rocks');
  } catch (error) {
    console.error('[FOREST ENV] Error generating rocks:', error);
  }
  
  console.log('[FOREST ENV] Rendering final forest environment group');
  
  try {
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
  } catch (error) {
    console.error('[FOREST ENV] Fatal error rendering forest environment:', error);
    return null;
  }
};

export default ForestEnvironment;