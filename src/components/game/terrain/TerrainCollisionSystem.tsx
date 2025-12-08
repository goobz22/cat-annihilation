import { useFrame } from '@react-three/fiber';
import { useGameStore } from '../../../lib/store/gameStore';

// Global terrain collision data
export const terrainCollisionData = {
  bridges: [] as Array<{
    x: number;
    z: number;
    width: number;
    length: number;
    height: number;
  }>,
  rivers: [] as Array<{
    minX: number;
    maxX: number;
    minZ: number;
    maxZ: number;
  }>,
  staticObjects: [] as Array<{
    x: number;
    z: number;
    radius: number;
    type: 'tree' | 'rock' | 'bush';
  }>
};

const TerrainCollisionSystem = () => {
  useFrame(() => {
    const player = useGameStore.getState().player;
    const setPosition = useGameStore.getState().setPlayerPosition;
    
    if (!player.position) return;
    
    const { x, z } = player.position;
    let newX = x;
    let newZ = z;
    let onBridge = false;
    
    // Check if player is on a bridge
    terrainCollisionData.bridges.forEach(bridge => {
      const halfWidth = bridge.width / 2;
      const halfLength = bridge.length / 2;
      
      if (x >= bridge.x - halfWidth && x <= bridge.x + halfWidth &&
          z >= bridge.z - halfLength && z <= bridge.z + halfLength) {
        onBridge = true;
      }
    });
    
    // Check river collisions only if not on bridge
    if (!onBridge) {
      terrainCollisionData.rivers.forEach(river => {
        // Add buffer zone around water to prevent getting too close
        const waterBuffer = 1.5; // Buffer distance from water edge
        const bufferedMinX = river.minX - waterBuffer;
        const bufferedMaxX = river.maxX + waterBuffer;
        const bufferedMinZ = river.minZ - waterBuffer;
        const bufferedMaxZ = river.maxZ + waterBuffer;
        
        // Check if player is in buffered river bounds (absolutely no water entry)
        if (x >= bufferedMinX && x <= bufferedMaxX && z >= bufferedMinZ && z <= bufferedMaxZ) {
          // Aggressively push player away from water
          const centerX = (river.minX + river.maxX) / 2;
          const centerZ = (river.minZ + river.maxZ) / 2;
          
          // Calculate distance to each buffered edge
          const distToMinX = Math.abs(x - bufferedMinX);
          const distToMaxX = Math.abs(x - bufferedMaxX);
          const distToMinZ = Math.abs(z - bufferedMinZ);
          const distToMaxZ = Math.abs(z - bufferedMaxZ);
          
          const minDist = Math.min(distToMinX, distToMaxX, distToMinZ, distToMaxZ);
          
          // Push to closest safe edge with extra safety margin
          if (minDist === distToMinX) newX = bufferedMinX - 0.5;
          else if (minDist === distToMaxX) newX = bufferedMaxX + 0.5;
          else if (minDist === distToMinZ) newZ = bufferedMinZ - 0.5;
          else if (minDist === distToMaxZ) newZ = bufferedMaxZ + 0.5;
        }
      });
    }
    
    // Check static object collisions (trees, rocks, bushes)
    terrainCollisionData.staticObjects.forEach(obj => {
      const dx = x - obj.x;
      const dz = z - obj.z;
      const distance = Math.sqrt(dx * dx + dz * dz);
      
      if (distance < obj.radius) {
        // Push player away from object
        const pushDirection = Math.atan2(dz, dx);
        const pushDistance = obj.radius - distance + 0.5; // Add small buffer
        newX = x + Math.cos(pushDirection) * pushDistance;
        newZ = z + Math.sin(pushDirection) * pushDistance;
      }
    });
    
    // Update position if needed
    if (newX !== x || newZ !== z) {
      setPosition(newX, newZ, player.position.rotation);
    }
  });
  
  return null;
};

export default TerrainCollisionSystem;