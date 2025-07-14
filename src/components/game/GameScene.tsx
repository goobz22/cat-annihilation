import { useRef, useEffect, useState } from 'react';
import { Canvas, useFrame, useThree } from '@react-three/fiber';
import { Sky, Environment, OrbitControls, PerspectiveCamera } from '@react-three/drei';
import { useGameStore } from '@/lib/store/gameStore';
import Terrain from '@/components/game/Terrain';
import CatCharacter from '@/components/game/CatCharacter';
import OtherPlayers from '@/components/game/OtherPlayers';
import ForestEnvironment from '@/components/game/ForestEnvironment';

/**
 * Sky and environmental lighting
 */
const SkyEnvironment = () => {
  // Get game time from store
  const { currentTime, isNight } = useGameStore((state) => state.dayCycle);
  
  // Convert time (0-1) to sun position
  const sunPosition = [
    Math.cos(currentTime * Math.PI * 2) * 100,
    Math.sin(currentTime * Math.PI * 2) * 100,
    0,
  ];

  return (
    <>
      <Sky 
        distance={450000} 
        sunPosition={sunPosition} 
        inclination={currentTime} 
        azimuth={0.25} 
      />
      <ambientLight intensity={isNight ? 0.1 : 0.5} />
      <directionalLight 
        position={sunPosition} 
        intensity={isNight ? 0.2 : 1} 
        castShadow 
        shadow-mapSize-width={1024} 
        shadow-mapSize-height={1024} 
      />
      <Environment preset={isNight ? "night" : "sunset"} />
    </>
  );
};

/**
 * Camera controls and animation
 */
const CameraControl = () => {
  const { camera } = useThree();
  const controls = useRef<any>();
  const player = useGameStore((state) => state.player);
  
  // Follow player position
  useFrame(() => {
    if (player.position && controls.current) {
      const { x, y, z } = player.position;
      
      // Set target to player position
      controls.current.target.set(x, y, z);
      
      // Adjust camera position to follow behind player based on rotation
      const distance = 5;
      const height = 3;
      const angle = player.position.rotation || 0;
      
      camera.position.x = x - Math.sin(angle) * distance;
      camera.position.z = z - Math.cos(angle) * distance;
      camera.position.y = y + height;
    }
  });
  
  // Setup controls
  useEffect(() => {
    if (controls.current) {
      // Game mode - constrained camera
      controls.current.enableDamping = true;
      controls.current.dampingFactor = 0.1;
      controls.current.rotateSpeed = 0.5;
      controls.current.minDistance = 2;
      controls.current.maxDistance = 15;
      controls.current.minPolarAngle = Math.PI / 6;
      controls.current.maxPolarAngle = Math.PI / 2;
    }
  }, []);
  
  return (
    <OrbitControls
      ref={controls}
      enableDamping
      dampingFactor={0.1}
      rotateSpeed={0.5}
      minDistance={2}
      maxDistance={15}
      minPolarAngle={Math.PI / 6}
      maxPolarAngle={Math.PI / 2}
    />
  );
};

/**
 * Game controls info overlay
 */
const ControlsInfo = () => {
  const [visible, setVisible] = useState(true);
  
  // Hide controls info after 10 seconds
  useEffect(() => {
    const timer = setTimeout(() => {
      setVisible(false);
    }, 10000);
    
    return () => clearTimeout(timer);
  }, []);
  
  if (!visible) return null;
  
  return (
    <div className="absolute top-4 left-1/2 transform -translate-x-1/2 px-4 py-2 rounded bg-black bg-opacity-70 text-white pointer-events-none">
      <p className="text-center">WASD to move • Shift to run • Space to jump • Left click to attack • Right click to defend</p>
    </div>
  );
};

/**
 * Game initialization
 */
const GameInitializer = () => {
  const isWorldLoaded = useGameStore((state) => state.isWorldLoaded);
  const setWorld = useGameStore((state) => state.setWorld);
  const setDayCycle = useGameStore((state) => state.setDayCycle);
  const setPlayer = useGameStore((state) => state.setPlayer);
  
  useEffect(() => {
    if (!isWorldLoaded) {
      // Load world data (in a real app, this would come from a server)
      const worldData = {
        name: 'Forest Haven',
        description: 'A peaceful forest where warrior cats roam',
        terrain: {
          heightmap: '/assets/terrain/default-heightmap.png',
          size: 2000,
          maxHeight: 100,
        },
        zones: [
          {
            name: 'Forest Clearing',
            type: 'safe',
            bounds: {
              minX: -100,
              maxX: 100,
              minZ: -100,
              maxZ: 100,
            },
            isPvpAtNight: false,
          },
        ],
      };
      
      setWorld(worldData);
      setDayCycle({
        currentTime: 0.5, // Start at noon
        isNight: false,
        dayCycleMinutes: 120,
        nightCycleMinutes: 40,
      });
      
      // Initialize player
      setPlayer({
        isLoaded: true,
        cat: {
          name: "Shadowpaw",
          level: 5,
          health: 100,
          maxHealth: 100,
        },
        position: { x: 0, y: 0, z: 0, rotation: 0 }
      });
    }
  }, [isWorldLoaded, setWorld, setDayCycle, setPlayer]);
  
  return null;
};

/**
 * Main game scene
 */
const GameScene = () => {
  const [isLoading, setIsLoading] = useState(true);
  const isWorldLoaded = useGameStore((state) => state.isWorldLoaded);
  const connectionError = useGameStore((state) => state.connectionError);
  
  // Simulating world loading
  useEffect(() => {
    if (isWorldLoaded) {
      const timer = setTimeout(() => {
        setIsLoading(false);
      }, 1000);
      
      return () => clearTimeout(timer);
    }
  }, [isWorldLoaded]);
  
  if (connectionError) {
    return (
      <div className="flex items-center justify-center h-screen bg-black text-red-500">
        <div className="text-center">
          <h2 className="text-2xl font-bold mb-4">Connection Error</h2>
          <p>{connectionError}</p>
        </div>
      </div>
    );
  }
  
  if (isLoading) {
    return (
      <div className="flex items-center justify-center h-screen bg-black text-white">
        <div className="text-center">
          <h2 className="text-2xl font-bold mb-4">Loading Cat Annihilation</h2>
          <div className="w-64 h-4 bg-gray-800 rounded-full overflow-hidden">
            <div className="h-full bg-green-500 animate-pulse"></div>
          </div>
        </div>
      </div>
    );
  }
  
  return (
    <div className="w-full h-screen fixed inset-0">
      {/* Controls info overlay */}
      <ControlsInfo />
      
      <Canvas shadows style={{ position: 'absolute', width: '100%', height: '100%' }}>
        <GameInitializer />
        <SkyEnvironment />
        <CameraControl />
        <PerspectiveCamera makeDefault position={[10, 10, 10]} fov={75} />
        <Terrain />
        <CatCharacter />
        <OtherPlayers />
        {/* Forest Environment */}
        <ForestEnvironment />
      </Canvas>
    </div>
  );
};

export default GameScene; 