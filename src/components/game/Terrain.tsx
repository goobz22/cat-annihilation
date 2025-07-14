import { useRef, useEffect, useState } from 'react';
import { useFrame } from '@react-three/fiber';
import * as THREE from 'three';
import { useGameStore } from '@/lib/store/gameStore';

/**
 * Height map generation
 */
const generateHeightmap = (
  size: number = 1024,
  height: number = 100,
  seed: number = 0
): { data: Float32Array; size: number; height: number } => {
  // Generate a forest terrain with gentle hills
  const data = new Float32Array(size * size);
  
  let z = seed;
  for (let i = 0; i < data.length; i++) {
    const x = i % size;
    const y = ~~(i / size);
    
    // Create a more natural terrain with multiple frequency noise
    const noise = 
      Math.sin(x / 30) * Math.sin(y / 30) * 0.5 + // Medium hills
      Math.sin(x / 100 + z) * Math.sin(y / 100 + z) * 0.8 + // Large rolling hills
      Math.sin(x / 7 + z) * Math.sin(y / 7 + z) * 0.2; // Small bumps
    
    // Create a flat area in the middle for the starting area
    const centerX = size / 2;
    const centerY = size / 2;
    const distanceFromCenter = Math.sqrt(Math.pow(x - centerX, 2) + Math.pow(y - centerY, 2));
    const maxFlatRadius = size / 10; // Size of flat area
    
    let heightValue;
    if (distanceFromCenter < maxFlatRadius) {
      // Flat area in the center with a very slight random variation
      heightValue = (Math.random() * 0.05) * height;
    } else {
      // Transition zone - gradually blend from flat to hilly
      const blend = Math.min(1, (distanceFromCenter - maxFlatRadius) / (maxFlatRadius * 2));
      heightValue = ((noise * blend) + 1) / 2 * height;
    }
    
    data[i] = heightValue;
  }
  
  return { data, size, height };
};

/**
 * Terrain component
 */
const Terrain = () => {
  // Get world info from store
  const world = useGameStore((state) => state.world);
  const isNight = useGameStore((state) => state.dayCycle.isNight);
  const editorMode = useGameStore((state) => state.editorMode);
  
  // Mesh and geometry refs
  const meshRef = useRef<THREE.Mesh>(null);
  const geometryRef = useRef<THREE.PlaneGeometry>(null);
  
  // Terrain size and height
  const terrainSize = world?.terrain?.size || 2000;
  const terrainHeight = world?.terrain?.maxHeight || 100;
  const resolution = 256; // Higher resolution for better editing
  
  // Create simple textures that will definitely work
  const [grassColor] = useState('#4a7c59');
  
  // Wait for geometry to be ready
  if (!geometryRef.current) {
    return (
      <group>
        <mesh
          rotation={[-Math.PI / 2, 0, 0]}
          position={[0, 0, 0]}
          scale={[terrainSize / resolution, terrainSize / resolution, 1]}
          receiveShadow
        >
          <planeGeometry
            args={[resolution, resolution, resolution - 1, resolution - 1]}
          />
          <meshStandardMaterial
            color={grassColor}
            roughness={0.8}
            metalness={0.1}
          />
        </mesh>
      </group>
    );
  }
  
  // Generate heightmap
  useEffect(() => {
    if (!geometryRef.current) return;
    
    // Generate heightmap
    const { data } = generateHeightmap(resolution, terrainHeight);
    
    // Apply heightmap to geometry
    const vertices = geometryRef.current.attributes.position.array as Float32Array;
    for (let i = 0; i < vertices.length; i += 3) {
      const x = i / 3 % resolution;
      const y = ~~(i / 3 / resolution);
      
      vertices[i + 2] = data[y * resolution + x];
    }
    
    // Update geometry
    geometryRef.current.attributes.position.needsUpdate = true;
    geometryRef.current.computeVertexNormals();
    
    // Set shadow properties
    if (meshRef.current) {
      meshRef.current.receiveShadow = true;
      meshRef.current.castShadow = true;
    }
  }, [terrainHeight, resolution]);
  
  // Update material based on day/night and editor mode
  useEffect(() => {
    if (meshRef.current) {
      const material = meshRef.current.material as THREE.MeshStandardMaterial;
      
      // Adjust material based on editor mode
      if (editorMode.isActive && editorMode.selectedTool === 'terrain') {
        // Show wireframe in terrain editing mode
        material.wireframe = true;
      } else {
        material.wireframe = false;
        
        // Adjust material based on day/night
        if (isNight) {
          material.emissive.set(0x0a1a2a); // Slight blue glow at night
          material.emissiveIntensity = 0.1;
        } else {
          material.emissive.set(0x000000);
          material.emissiveIntensity = 0;
        }
      }
    }
  }, [isNight, editorMode.isActive, editorMode.selectedTool]);
  
  return (
    <group>
      <mesh
        ref={meshRef}
        name="terrain-mesh" // Add name for editor to find mesh
        rotation={[-Math.PI / 2, 0, 0]}
        position={[0, 0, 0]} // Center at origin so it covers from -1000 to +1000
        scale={[terrainSize / resolution, terrainSize / resolution, 1]}
        receiveShadow
      >
        <planeGeometry
          ref={geometryRef}
          args={[resolution, resolution, resolution - 1, resolution - 1]}
        />
        <meshStandardMaterial
          color={grassColor}
          roughness={0.8}
          metalness={0.1}
        />
      </mesh>
    </group>
  );
};

export default Terrain; 