import React, { useRef, useState, useEffect, useMemo } from 'react';
import { useFrame } from '@react-three/fiber';
import * as THREE from 'three';

console.log('[FOREST ENV] ForestEnvironment module loading...');

/**
 * Enhanced Error Boundary that prevents unmounting and provides meaningful fallbacks
 */
class RobustErrorBoundary extends React.Component<
  { 
    children: React.ReactNode;
    fallbackName?: string;
    persistOnError?: boolean;
  },
  { 
    hasError: boolean;
    errorCount: number;
    lastError?: Error;
  }
> {
  private retryTimeout?: NodeJS.Timeout;

  constructor(props: { children: React.ReactNode; fallbackName?: string; persistOnError?: boolean }) {
    super(props);
    this.state = { 
      hasError: false, 
      errorCount: 0,
      lastError: undefined
    };
  }

  static getDerivedStateFromError(error: Error) {
    return { 
      hasError: true,
      lastError: error
    };
  }

  componentDidCatch(error: Error, errorInfo: React.ErrorInfo) {
    console.error(`[ERROR BOUNDARY] ${this.props.fallbackName || 'Component'} error:`, error, errorInfo);
    
    this.setState(prevState => ({
      errorCount: prevState.errorCount + 1
    }));

    // Auto-retry after 5 seconds if persistOnError is true and error count is low
    if (this.props.persistOnError && this.state.errorCount < 3) {
      console.log(`[ERROR BOUNDARY] Auto-retry attempt ${this.state.errorCount + 1} in 5 seconds...`);
      this.retryTimeout = setTimeout(() => {
        console.log(`[ERROR BOUNDARY] Attempting to recover ${this.props.fallbackName}...`);
        this.setState({ hasError: false, lastError: undefined });
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
      const fallbackName = this.props.fallbackName || 'Component';
      console.log(`[ERROR BOUNDARY] Rendering fallback for ${fallbackName}`);
      
      // Provide specific fallbacks based on component type
      if (fallbackName.includes('Forest') || fallbackName.includes('Ground')) {
        return (
          <group>
            {/* Minimal forest environment fallback */}
            <mesh rotation={[-Math.PI / 2, 0, 0]} position={[0, -0.1, 0]} receiveShadow>
              <planeGeometry args={[200, 200]} />
              <meshStandardMaterial color="#7fb069" />
            </mesh>
            <fog attach="fog" args={['#4c6156', 30, 150]} />
            {/* Simple fallback trees */}
            {[...Array(10)].map((_, i) => (
              <mesh key={`fallback-tree-${i}`} position={[Math.sin(i) * 20, 3, Math.cos(i) * 20]} castShadow>
                <cylinderGeometry args={[0.5, 0.5, 6]} />
                <meshStandardMaterial color="#8B4513" />
              </mesh>
            ))}
          </group>
        );
      }
      
      if (fallbackName.includes('NPC')) {
        return (
          <mesh position={[0, 0.5, 0]}>
            <boxGeometry args={[0.8, 1.5, 0.6]} />
            <meshStandardMaterial color="#888888" />
          </mesh>
        );
      }
      
      if (fallbackName.includes('Enemy')) {
        return (
          <mesh position={[0, 0.5, 0]}>
            <boxGeometry args={[0.8, 0.8, 0.8]} />
            <meshStandardMaterial color="#8B4513" />
          </mesh>
        );
      }
      
      // Generic fallback - invisible placeholder that maintains structure
      return (
        <group>
          <mesh position={[0, 0, 0]} visible={false}>
            <boxGeometry args={[0.1, 0.1, 0.1]} />
            <meshBasicMaterial />
          </mesh>
        </group>
      );
    }

    return this.props.children;
  }
}

/**
 * Tree model component
 */
const Tree = ({ position, scale = 1, type = 'pine' }: { position: [number, number, number]; scale?: number; type?: 'pine' | 'oak' }) => {
  
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
 * Forest ground with grass texture - Internal component
 */
const ForestGroundInternal = () => {
  
  // Create texture using useMemo to prevent re-creation on every render
  const texture = useMemo(() => {
    console.log('[FOREST GROUND] Creating grass texture with useMemo');
    
    try {
      const canvas = document.createElement('canvas');
      canvas.width = 256;
      canvas.height = 256;
      const context = canvas.getContext('2d');
      
      if (!context) {
        console.error('[FOREST GROUND] Failed to get 2D context');
        return null;
      }
      
      // Base grass color - light green
      context.fillStyle = '#7fb069';
      context.fillRect(0, 0, 256, 256);
      
      // Add grass patterns - reduced for performance
      for (let i = 0; i < 300; i++) {
        const x = Math.random() * 256;
        const y = Math.random() * 256;
        const length = Math.random() * 4 + 2;
        const width = 1;
        
        // Vary green shade
        const variation = Math.floor(Math.random() * 20) - 10;
        context.fillStyle = `rgb(${127 + variation}, ${176 + variation}, 105)`;
        context.fillRect(x, y, width, length);
      }
      
      // Add some darker spots
      for (let i = 0; i < 15; i++) {
        const x = Math.random() * 256;
        const y = Math.random() * 256;
        const size = Math.random() * 3 + 1;
        
        context.fillStyle = 'rgba(45, 80, 50, 0.5)';
        context.beginPath();
        context.arc(x, y, size, 0, Math.PI * 2);
        context.fill();
      }
      
      const newTexture = new THREE.CanvasTexture(canvas);
      newTexture.wrapS = newTexture.wrapT = THREE.RepeatWrapping;
      newTexture.repeat.set(20, 20);
      console.log('[FOREST GROUND] Texture created successfully');
      return newTexture;
    } catch (error) {
      console.error('[FOREST GROUND] Error creating grass texture:', error);
      return null;
    }
  }, []); // Empty dependency array - only create once
  
  // Cleanup texture when component unmounts
  useEffect(() => {
    return () => {
      if (texture) {
        console.log('[FOREST GROUND] Disposing texture on unmount');
        texture.dispose();
      }
    };
  }, [texture]);
  
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
 * Wrapped ForestGround with error boundary
 */
const ForestGround = () => {
  return (
    <RobustErrorBoundary 
      fallbackName="ForestGround" 
      persistOnError={true}
    >
      <ForestGroundInternal />
    </RobustErrorBoundary>
  );
};

/**
 * Main forest environment component
 */
const ForestEnvironment = () => {
  
  // Add ambient sounds
  useEffect(() => {
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
  
  // Generate all environment objects once using useMemo to prevent infinite re-renders
  const { trees, bushes, rocks } = useMemo(() => {
    const generatedTrees = [];
    const generatedBushes = [];
    const generatedRocks = [];
  
  // Add trees in a circle around the center, but not in the immediate center
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
      
      generatedTrees.push(
        <Tree 
          key={`tree-${i}`} 
          position={[x, y, z]} 
          scale={scale}
          type={treetype as 'pine' | 'oak'} 
        />
      );
    }
    // Trees generated successfully
  } catch (error) {
    console.error('[FOREST ENV] Error generating circular trees:', error);
  }
  
  // Add additional trees in a grid pattern for better coverage
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
        
                generatedTrees.push(
          <Tree 
            key={`grid-tree-${x}-${z}`} 
            position={[offsetX, 0, offsetZ]} 
            scale={scale}
            type={treetype as 'pine' | 'oak'}
          />
        );
      }
    }
    // Grid trees generated
  } catch (error) {
    console.error('[FOREST ENV] Error generating grid trees:', error);
  }
  
  // Add some bushes
  try {
    for (let i = 0; i < 60; i++) {
      const angle = Math.random() * Math.PI * 2;
      const distance = 10 + Math.random() * 150;
      const x = Math.sin(angle) * distance;
      const z = Math.cos(angle) * distance;
      
      generatedBushes.push(
        <Bush 
          key={`bush-${i}`} 
          position={[x, 0, z]} 
          scale={0.5 + Math.random() * 0.5}
        />
      );
    }
    // Bushes generated
  } catch (error) {
    console.error('[FOREST ENV] Error generating bushes:', error);
  }
  
  // Add some rocks
  try {
    for (let i = 0; i < 40; i++) {
      const angle = Math.random() * Math.PI * 2;
      const distance = 5 + Math.random() * 120;
      const x = Math.sin(angle) * distance;
      const z = Math.cos(angle) * distance;
      
      generatedRocks.push(
        <Rock 
          key={`rock-${i}`} 
          position={[x, 0, z]} 
          scale={0.5 + Math.random() * 1}
        />
      );
    }
    // Rocks generated
  } catch (error) {
    console.error('[FOREST ENV] Error generating rocks:', error);
  }
  
    return {
      trees: generatedTrees,
      bushes: generatedBushes,
      rocks: generatedRocks
    };
  }, []); // Empty dependency array - generate only once
  
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

// Wrapped component with robust error boundary
const SafeForestEnvironment = () => {
  return (
    <RobustErrorBoundary 
      fallbackName="ForestEnvironment" 
      persistOnError={true}
    >
      <ForestEnvironment />
    </RobustErrorBoundary>
  );
};

export default SafeForestEnvironment;