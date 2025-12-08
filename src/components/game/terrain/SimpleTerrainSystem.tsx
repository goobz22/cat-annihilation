import React, { useEffect, useMemo, useRef } from 'react';
import * as THREE from 'three';
import { useFrame } from '@react-three/fiber';
import { useGameStore } from '../../../lib/store/gameStore';
import { BiomeType } from '../biomes/types';
import { terrainCollisionData } from './TerrainCollisionSystem';

interface SimpleTerrainSystemProps {
  playerPosition: THREE.Vector3;
}

// Biome configuration
const BIOMES = {
  mist: { x: -150, z: -150, color: '#3D2F1F', name: 'MistClan Territory' },
  storm: { x: 150, z: -150, color: '#5A5A5A', name: 'StormClan Territory' },
  ember: { x: 150, z: 150, color: '#654321', name: 'EmberClan Territory' },
  frost: { x: -150, z: 150, color: '#87CEEB', name: 'FrostClan Territory' }, // Changed to light blue
  gathering: { x: 0, z: 0, color: '#8B7355', name: 'The Gathering Place' } // At the central bridge
};

const SimpleTerrainSystem: React.FC<SimpleTerrainSystemProps> = ({ playerPosition }) => {
  console.log('[SIMPLE TERRAIN] Initialized once');

  // Create a single terrain mesh with vertex colors and river cutout
  const terrainGeometry = useMemo(() => {
    console.log('[SIMPLE TERRAIN] Creating geometry with river cutout...');
    const geometry = new THREE.PlaneGeometry(600, 600, 60, 60);
    
    // Add vertex colors based on biome proximity
    const colors = [];
    const positions = geometry.attributes.position;
    const vertices = positions.array;
    
    for (let i = 0; i < positions.count; i++) {
      const x = positions.getX(i);
      const z = positions.getZ(i);
      
      // Check if vertex is near either river and lower it
      const riverWidth = 15;
      const channelWidth = 25; // Wider to create banks
      
      // North-South river (along x = 0)
      const nsDistance = Math.abs(x);
      
      // East-West river (along z = 0)
      const ewDistance = Math.abs(z);
      
      // Lower terrain near rivers to create channels
      if (nsDistance < channelWidth / 2) {
        const depthFactor = Math.pow(1 - (nsDistance / (channelWidth / 2)), 2);
        positions.setY(i, -depthFactor * 1.5);
      }
      
      if (ewDistance < channelWidth / 2) {
        const depthFactor = Math.pow(1 - (ewDistance / (channelWidth / 2)), 2);
        const currentY = positions.getY(i);
        positions.setY(i, Math.min(currentY, -depthFactor * 1.5));
      }
      
      // Calculate biome influences
      let r = 0.5, g = 0.6, b = 0.4; // Base grass color
      
      Object.entries(BIOMES).forEach(([biome, data]) => {
        const distance = Math.sqrt(Math.pow(x - data.x, 2) + Math.pow(z - data.z, 2));
        const influence = Math.max(0, 1 - distance / 100);
        
        if (influence > 0) {
          const color = new THREE.Color(data.color);
          r = r * (1 - influence) + color.r * influence;
          g = g * (1 - influence) + color.g * influence;
          b = b * (1 - influence) + color.b * influence;
        }
      });
      
      colors.push(r, g, b);
    }
    
    // Update positions
    positions.needsUpdate = true;
    
    geometry.setAttribute('color', new THREE.Float32BufferAttribute(colors, 3));
    console.log('[SIMPLE TERRAIN] Geometry created');
    return geometry;
  }, []);

  // Update current biome in store
  useFrame(() => {
    let currentBiome: BiomeType = 'neutral';
    let minDistance = Infinity;
    
    // First check if player is actually ON the Gathering Bridge (not just near it)
    let isOnGatheringBridge = false;
    terrainCollisionData.bridges.forEach(bridge => {
      if (bridge.x === 0 && bridge.z === 0) { // Gathering Bridge
        const halfWidth = bridge.width / 2;
        const halfLength = bridge.length / 2;
        
        // Check if player is on the bridge platform (considering height)
        if (playerPosition.x >= bridge.x - halfWidth && 
            playerPosition.x <= bridge.x + halfWidth &&
            playerPosition.z >= bridge.z - halfLength && 
            playerPosition.z <= bridge.z + halfLength &&
            playerPosition.y >= bridge.height - 0.5) { // Must be at bridge height
          isOnGatheringBridge = true;
          currentBiome = 'gathering';
        }
      }
    });
    
    // If not on gathering bridge, check other biomes (excluding gathering from normal distance check)
    if (!isOnGatheringBridge) {
    Object.entries(BIOMES).forEach(([biome, data]) => {
        if (biome === 'gathering') return; // Skip gathering - only accessible via bridge height check
        
      const distance = Math.sqrt(
        Math.pow(playerPosition.x - data.x, 2) + 
        Math.pow(playerPosition.z - data.z, 2)
      );
      
      if (distance < 80 && distance < minDistance) {
        currentBiome = biome as BiomeType;
        minDistance = distance;
      }
    });
    }
    
    // Update store only if biome changed
    const storedBiome = useGameStore.getState().storyMode.currentBiome;
    if (storedBiome !== currentBiome) {
      useGameStore.setState(state => ({
        storyMode: {
          ...state.storyMode,
          currentBiome
        }
      }));
    }
  });

  // Log mount/unmount
  useEffect(() => {
    console.log('[SIMPLE TERRAIN] Component mounted');
    return () => {
      console.log('[SIMPLE TERRAIN] Component unmounted');
    };
  }, []);

  return (
    <group>
      {/* Single terrain mesh */}
      <mesh 
        rotation={[-Math.PI / 2, 0, 0]} 
        position={[0, 0, 0]} 
        receiveShadow
        geometry={terrainGeometry}
      >
        <meshStandardMaterial vertexColors roughness={0.9} />
      </mesh>

      {/* Biome markers */}
      {Object.entries(BIOMES).map(([biome, data]) => (
        <group key={biome} position={[data.x, 0, data.z]}>
          {/* Marker pole */}
          <mesh position={[0, 5, 0]}>
            <cylinderGeometry args={[0.5, 0.5, 10, 8]} />
            <meshStandardMaterial 
              color={data.color} 
              emissive={data.color} 
              emissiveIntensity={0.3} 
            />
          </mesh>
          {/* Territory boundary */}
          <mesh rotation={[-Math.PI / 2, 0, 0]} position={[0, 0.1, 0]}>
            <ringGeometry args={[75, 80, 32]} />
            <meshBasicMaterial 
              color={data.color} 
              transparent 
              opacity={0.2} 
              side={THREE.DoubleSide}
            />
          </mesh>
          {/* Debug text (biome name) */}
          <mesh position={[0, 15, 0]}>
            <boxGeometry args={[20, 5, 0.1]} />
            <meshBasicMaterial color={data.color} />
          </mesh>
        </group>
      ))}

      {/* Stream down the middle */}
      <Stream />

      {/* Simple trees */}
      <SimpleTreesStatic />

      {/* Static fog */}
      <fog attach="fog" args={['#87CEEB', 30, 300]} />
    </group>
  );
};

// Static trees that don't re-render
const SimpleTreesStatic = React.memo(() => {
  console.log('[SIMPLE TERRAIN] Creating static trees');
  
  const trees = useMemo(() => {
    const treeElements = [];
    const treeCollisionData = [];
    
    // Add a few trees around each biome
    Object.entries(BIOMES).forEach(([biome, data]) => {
      for (let i = 0; i < 10; i++) {
        const angle = (i / 10) * Math.PI * 2;
        const distance = 30 + Math.random() * 30;
        const x = data.x + Math.sin(angle) * distance;
        const z = data.z + Math.cos(angle) * distance;
        
        // Add collision data for this tree
        treeCollisionData.push({
          x,
          z,
          radius: 2.5, // Tree collision radius
          type: 'tree' as const
        });
        
        treeElements.push(
          <group key={`${biome}-tree-${i}`} position={[x, 0, z]}>
            <mesh castShadow position={[0, 2, 0]}>
              <cylinderGeometry args={[0.3, 0.4, 4, 6]} />
              <meshStandardMaterial color="#654321" />
            </mesh>
            <mesh castShadow position={[0, 5, 0]}>
              <sphereGeometry args={[2, 6, 6]} />
              <meshStandardMaterial color="#228B22" />
            </mesh>
          </group>
        );
      }
    });
    
    // Update global collision data
    terrainCollisionData.staticObjects = treeCollisionData;
    
    return treeElements;
  }, []);

  return <>{trees}</>;
});

// Stream component with two straight rivers forming a cross
const Stream = React.memo(() => {
  console.log('[SIMPLE TERRAIN] Creating cross rivers with bridges');
  
  // Water animation state
  const waterRef = useRef<THREE.Group>(null);
  const materialRef = useRef<THREE.ShaderMaterial>(null);
  
  // Use frame for water animation
  useFrame((state) => {
    if (materialRef.current) {
      // Update water flow
      materialRef.current.uniforms.time.value = state.clock.elapsedTime;
    }
  });
  
  // Create two straight rivers forming a cross
  const { northSouthRiver, eastWestRiver, bridges } = useMemo(() => {
    const riverWidth = 15;
    const riverDepth = 2;
    
    // North-South river
    const nsGeometry = new THREE.BoxGeometry(riverWidth, riverDepth, 600);
    
    // East-West river
    const ewGeometry = new THREE.BoxGeometry(600, riverDepth, riverWidth);
    
    // Bridge configurations - longer with platform design
    const bridgeData = [
      // Central bridge (Gathering Place) - large platform with 4 exits
      { x: 0, z: 0, width: 40, length: 40, color: '#8B7355', name: 'Gathering Bridge', height: 1 },
      // Clan bridges - at ground level for easy access
      { x: -100, z: 0, width: 15, length: 40, color: '#3D2F1F', name: 'MistClan Bridge', height: 0.1 }, // West
      { x: 100, z: 0, width: 15, length: 40, color: '#5A5A5A', name: 'StormClan Bridge', height: 0.1 }, // East
      { x: 0, z: -100, width: 40, length: 15, color: '#654321', name: 'EmberClan Bridge', height: 0.1 }, // South
      { x: 0, z: 100, width: 40, length: 15, color: '#87CEEB', name: 'FrostClan Bridge', height: 0.1 }, // North
    ];
    
    return { 
      northSouthRiver: nsGeometry, 
      eastWestRiver: ewGeometry,
      bridges: bridgeData
    };
  }, []);

  // Create animated water material
  const waterMaterial = useMemo(() => {
    return new THREE.ShaderMaterial({
      uniforms: {
        time: { value: 0 },
        flowSpeed: { value: 0.5 },
        waveHeight: { value: 0.2 }
      },
      vertexShader: `
        uniform float time;
        uniform float flowSpeed;
        uniform float waveHeight;
        varying vec2 vUv;
        varying vec3 vPos;
        varying vec3 vNormal;
        
        void main() {
          vUv = uv;
          vPos = position;
          vNormal = normal;
          
          // Create flowing water effect
          vec3 pos = position;
          float flow = time * flowSpeed;
          float wave = sin(position.z * 0.05 + flow) * waveHeight;
          wave += sin(position.z * 0.1 - flow * 1.5) * waveHeight * 0.5;
          pos.y += wave;
          
          gl_Position = projectionMatrix * modelViewMatrix * vec4(pos, 1.0);
        }
      `,
      fragmentShader: `
        uniform float time;
        varying vec2 vUv;
        varying vec3 vPos;
        varying vec3 vNormal;
        
        void main() {
          // Water color based on depth
          float depth = 1.0 - abs(vPos.x) / 10.0;
          vec3 shallowColor = vec3(0.4, 0.6, 0.8);
          vec3 deepColor = vec3(0.1, 0.3, 0.6);
          vec3 waterColor = mix(shallowColor, deepColor, depth);
          
          // Add some movement to the color
          waterColor += sin(time + vPos.z * 0.1) * 0.05;
          
          gl_FragColor = vec4(waterColor, 0.85);
        }
      `,
      transparent: true,
      side: THREE.DoubleSide
    });
  }, []);

  // Store material ref and set up collision data
  useEffect(() => {
    materialRef.current = waterMaterial;
    
    // Set up collision data for terrain features
    terrainCollisionData.bridges = bridges.map(bridge => ({
      x: bridge.x,
      z: bridge.z,
      width: bridge.width,
      length: bridge.length,
      height: bridge.height
    }));
    
    // Set up river collision bounds
    const riverWidth = 15;
    const halfWidth = riverWidth / 2;
    terrainCollisionData.rivers = [
      // North-South river
      { minX: -halfWidth, maxX: halfWidth, minZ: -300, maxZ: 300 },
      // East-West river
      { minX: -300, maxX: 300, minZ: -halfWidth, maxZ: halfWidth }
    ];
    
    return () => {
      terrainCollisionData.bridges = [];
      terrainCollisionData.rivers = [];
    };
  }, [waterMaterial, bridges]);

  return (
    <group ref={waterRef}>
      {/* North-South River */}
      <mesh
        geometry={northSouthRiver}
        material={waterMaterial}
        position={[0, -2.0, 0]}
        receiveShadow
      />
      
      {/* East-West River */}
      <mesh
        geometry={eastWestRiver}
        material={waterMaterial}
        position={[0, -2.0, 0]}
        receiveShadow
      />
      
      {/* Debug collision boundaries (remove later) */}
      <mesh position={[0, 0.01, 0]}>
        <boxGeometry args={[15, 0.1, 600]} />
        <meshBasicMaterial color="red" transparent opacity={0.2} />
      </mesh>
      <mesh position={[0, 0.01, 0]}>
        <boxGeometry args={[600, 0.1, 15]} />
        <meshBasicMaterial color="red" transparent opacity={0.2} />
      </mesh>
      
      {/* Debug bridge collision areas */}
      {bridges.map((bridge, i) => (
        <mesh key={`debug-bridge-${i}`} position={[bridge.x, 0.02, bridge.z]}>
          <boxGeometry args={[bridge.width, 0.1, bridge.length]} />
          <meshBasicMaterial color="green" transparent opacity={0.3} />
        </mesh>
      ))}
      
      {/* Bridges */}
      {bridges.map((bridge, i) => (
        <group key={`bridge-${i}`} position={[bridge.x, 0, bridge.z]}>
          {/* Bridge deck - raised platform */}
          <mesh position={[0, bridge.height, 0]} castShadow receiveShadow>
            <boxGeometry args={[bridge.width, bridge.height, bridge.length]} />
            <meshStandardMaterial color={bridge.color} />
          </mesh>
          
          {/* Bridge approach ramps for the central platform */}
          {bridge.name === 'Gathering Bridge' && (
            <>
              {/* Southwest wrap-around ramp */}
              <group>
                <mesh position={[-bridge.width/2 - 10, 0.2, -bridge.length/2]} rotation={[0, 0, 0.15]} castShadow>
                  <boxGeometry args={[20, 0.4, bridge.length * 0.6]} />
                  <meshStandardMaterial color={bridge.color} />
                </mesh>
                <mesh position={[-bridge.width/2, 0.2, -bridge.length/2 - 10]} rotation={[0.15, 0, 0]} castShadow>
                  <boxGeometry args={[bridge.width * 0.6, 0.4, 20]} />
                  <meshStandardMaterial color={bridge.color} />
                </mesh>
                <mesh position={[-bridge.width/2 - 5, 0.4, -bridge.length/2 - 5]} rotation={[0.1, 0, 0.1]} castShadow>
                  <boxGeometry args={[10, 0.8, 10]} />
                  <meshStandardMaterial color={bridge.color} />
                </mesh>
              </group>
              
              {/* Northwest wrap-around ramp */}
              <group>
                <mesh position={[-bridge.width/2 - 10, 0.2, bridge.length/2]} rotation={[0, 0, 0.15]} castShadow>
                  <boxGeometry args={[20, 0.4, bridge.length * 0.6]} />
                  <meshStandardMaterial color={bridge.color} />
                </mesh>
                <mesh position={[-bridge.width/2, 0.2, bridge.length/2 + 10]} rotation={[-0.15, 0, 0]} castShadow>
                  <boxGeometry args={[bridge.width * 0.6, 0.4, 20]} />
                  <meshStandardMaterial color={bridge.color} />
                </mesh>
                <mesh position={[-bridge.width/2 - 5, 0.4, bridge.length/2 + 5]} rotation={[-0.1, 0, 0.1]} castShadow>
                  <boxGeometry args={[10, 0.8, 10]} />
                  <meshStandardMaterial color={bridge.color} />
                </mesh>
              </group>
              
              {/* Northeast wrap-around ramp */}
              <group>
                <mesh position={[bridge.width/2 + 10, 0.2, bridge.length/2]} rotation={[0, 0, -0.15]} castShadow>
                  <boxGeometry args={[20, 0.4, bridge.length * 0.6]} />
                  <meshStandardMaterial color={bridge.color} />
                </mesh>
                <mesh position={[bridge.width/2, 0.2, bridge.length/2 + 10]} rotation={[-0.15, 0, 0]} castShadow>
                  <boxGeometry args={[bridge.width * 0.6, 0.4, 20]} />
                  <meshStandardMaterial color={bridge.color} />
                </mesh>
                <mesh position={[bridge.width/2 + 5, 0.4, bridge.length/2 + 5]} rotation={[-0.1, 0, -0.1]} castShadow>
                  <boxGeometry args={[10, 0.8, 10]} />
                  <meshStandardMaterial color={bridge.color} />
                </mesh>
              </group>
              
              {/* Southeast wrap-around ramp */}
              <group>
                <mesh position={[bridge.width/2 + 10, 0.2, -bridge.length/2]} rotation={[0, 0, -0.15]} castShadow>
                  <boxGeometry args={[20, 0.4, bridge.length * 0.6]} />
                  <meshStandardMaterial color={bridge.color} />
                </mesh>
                <mesh position={[bridge.width/2, 0.2, -bridge.length/2 - 10]} rotation={[0.15, 0, 0]} castShadow>
                  <boxGeometry args={[bridge.width * 0.6, 0.4, 20]} />
                  <meshStandardMaterial color={bridge.color} />
                </mesh>
                <mesh position={[bridge.width/2 + 5, 0.4, -bridge.length/2 - 5]} rotation={[0.1, 0, -0.1]} castShadow>
                  <boxGeometry args={[10, 0.8, 10]} />
                  <meshStandardMaterial color={bridge.color} />
                </mesh>
              </group>
            </>
          )}
          
          {/* Bridge railings - only on sides */}
          {bridge.name !== 'Gathering Bridge' && (
            <>
              {/* Determine railing placement based on bridge orientation */}
              {bridge.width > bridge.length ? (
                // East-West bridge - railings on north/south
                <>
                  <mesh position={[0, bridge.height + 1, bridge.length/2 - 0.5]} castShadow>
                    <boxGeometry args={[bridge.width, 1, 1]} />
                    <meshStandardMaterial color={bridge.color} opacity={0.8} transparent />
                  </mesh>
                  <mesh position={[0, bridge.height + 1, -bridge.length/2 + 0.5]} castShadow>
                    <boxGeometry args={[bridge.width, 1, 1]} />
                    <meshStandardMaterial color={bridge.color} opacity={0.8} transparent />
                  </mesh>
                </>
              ) : (
                // North-South bridge - railings on east/west
                <>
                  <mesh position={[bridge.width/2 - 0.5, bridge.height + 1, 0]} castShadow>
                    <boxGeometry args={[1, 1, bridge.length]} />
                    <meshStandardMaterial color={bridge.color} opacity={0.8} transparent />
                  </mesh>
                  <mesh position={[-bridge.width/2 + 0.5, bridge.height + 1, 0]} castShadow>
                    <boxGeometry args={[1, 1, bridge.length]} />
                    <meshStandardMaterial color={bridge.color} opacity={0.8} transparent />
                  </mesh>
                </>
              )}
            </>
          )}
          
          {/* Bridge supports - stone pillars */}
          <mesh position={[bridge.width * 0.3, -0.5, bridge.length * 0.3]} castShadow>
            <cylinderGeometry args={[2, 2.5, 3, 8]} />
            <meshStandardMaterial color="#4A4A4A" />
          </mesh>
          <mesh position={[-bridge.width * 0.3, -0.5, bridge.length * 0.3]} castShadow>
            <cylinderGeometry args={[2, 2.5, 3, 8]} />
            <meshStandardMaterial color="#4A4A4A" />
          </mesh>
          <mesh position={[bridge.width * 0.3, -0.5, -bridge.length * 0.3]} castShadow>
            <cylinderGeometry args={[2, 2.5, 3, 8]} />
            <meshStandardMaterial color="#4A4A4A" />
          </mesh>
          <mesh position={[-bridge.width * 0.3, -0.5, -bridge.length * 0.3]} castShadow>
            <cylinderGeometry args={[2, 2.5, 3, 8]} />
            <meshStandardMaterial color="#4A4A4A" />
          </mesh>
        </group>
      ))}
    </group>
  );
});

export default SimpleTerrainSystem;