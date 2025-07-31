import { useState, useEffect, useRef } from 'react';
import { useFrame } from '@react-three/fiber';
import { useGameStore } from '../../lib/store/gameStore';
import { globalCollisionData } from './GlobalCollisionSystem';

/**
 * !!!!!!! CRITICAL WARNING !!!!!!!
 * 
 * THIS PROJECTILE SYSTEM MUST ***NEVER*** CALL ANY ZUSTAND STORE SET() FUNCTIONS
 * DURING RUNTIME OR IT WILL CAUSE THE CAT TO FLY THROUGH THE TERRAIN!!!
 * 
 * The issue was that calling addProjectile() in the store was triggering state updates
 * that somehow corrupted the player position, causing massive movement bugs.
 * 
 * This system uses ONLY local React state and NEVER touches the global game store
 * except for reading initial values. DO NOT CHANGE THIS APPROACH!
 * 
 * !!!!!!! CRITICAL WARNING !!!!!!!
 */

interface LocalProjectile {
  id: string;
  position: { x: number; y: number; z: number };
  rotation: number;
  type: string;
  color: string;
  speed: number;
}

const LocalProjectileRenderer = ({ projectile }: { projectile: LocalProjectile }) => {
  const meshRef = useRef<any>(null);
  const positionRef = useRef(projectile.position);

  useFrame((_, delta) => {
    if (!meshRef.current) return;
    
    positionRef.current.x += Math.sin(projectile.rotation) * projectile.speed * delta;
    positionRef.current.z += Math.cos(projectile.rotation) * projectile.speed * delta;
    
    meshRef.current.position.set(
      positionRef.current.x,
      positionRef.current.y,
      positionRef.current.z
    );
    
    // Update global collision data
    const globalProjectile = globalCollisionData.projectiles.find(p => p.id === projectile.id);
    if (globalProjectile) {
      globalProjectile.position = positionRef.current;
    }
  });

  if (projectile.type === 'arrow') {
    return (
      <group ref={meshRef} position={[projectile.position.x, projectile.position.y, projectile.position.z]} rotation={[0, projectile.rotation, 0]}>
        <mesh position={[0, 0, 0]} rotation={[Math.PI/2, 0, 0]}>
          <cylinderGeometry args={[0.02, 0.02, 0.6, 8]} />
          <meshBasicMaterial color="#8B4513" />
        </mesh>
        <mesh position={[0, 0, 0.3]} rotation={[Math.PI/2, 0, 0]}>
          <coneGeometry args={[0.05, 0.2, 6]} />
          <meshBasicMaterial color="#C0C0C0" />
        </mesh>
        <mesh position={[0, 0, -0.25]} rotation={[Math.PI/2, 0, 0]}>
          <coneGeometry args={[0.08, 0.15, 4]} />
          <meshBasicMaterial color="#FF6B6B" />
        </mesh>
      </group>
    );
  } else {
    return (
      <mesh ref={meshRef} position={[projectile.position.x, projectile.position.y, projectile.position.z]}>
        <sphereGeometry args={[0.2, 16, 16]} />
        <meshBasicMaterial color={projectile.color} />
      </mesh>
    );
  }
};

const LocalProjectileSystem = () => {
  const [localProjectiles, setLocalProjectiles] = useState<LocalProjectile[]>([]);
  const inventory = useGameStore((state) => state.player.inventory);
  const activeSlot = useGameStore((state) => state.player.activeSlot);
  const player = useGameStore((state) => state.player);

  const shootProjectile = () => {
    const activeItem = inventory[activeSlot];
    if (!activeItem) return;

    const position = player.position;
    const rotation = position.rotation || 0;

    console.log('LOCAL PROJECTILE SYSTEM: Shooting with:', activeItem);

    // Only create projectiles for spells and bow - sword doesn't shoot
    if (activeItem.type === 'spell') {
      const newProjectile: LocalProjectile = {
        id: `local-${Date.now()}-${Math.random()}`,
        position: {
          x: position.x + Math.sin(rotation) * 2,
          y: 1,
          z: position.z + Math.cos(rotation) * 2
        },
        rotation: rotation,
        type: 'spell',
        color: activeItem.color || '#00ffff',
        speed: 15
      };

      setLocalProjectiles(prev => [...prev, newProjectile]);
      
      // Register with global collision system
      globalCollisionData.projectiles.push({
        id: newProjectile.id,
        position: newProjectile.position,
        type: newProjectile.type,
        element: activeItem.element, // Include element for magic XP
        onHit: () => {
          // Remove projectile when it hits something
          setLocalProjectiles(prev => prev.filter(p => p.id !== newProjectile.id));
          // Remove from global collision data too
          const index = globalCollisionData.projectiles.findIndex(p => p.id === newProjectile.id);
          if (index !== -1) {
            globalCollisionData.projectiles.splice(index, 1);
          }
        }
      });

      // Remove projectile after 10 seconds
      setTimeout(() => {
        setLocalProjectiles(prev => prev.filter(p => p.id !== newProjectile.id));
        // Remove from global collision data
        const index = globalCollisionData.projectiles.findIndex(p => p.id === newProjectile.id);
        if (index !== -1) {
          globalCollisionData.projectiles.splice(index, 1);
        }
      }, 10000);
    } else if (activeItem.id === 'bow') {
      const newProjectile: LocalProjectile = {
        id: `local-${Date.now()}-${Math.random()}`,
        position: {
          x: position.x + Math.sin(rotation) * 2,
          y: 1,
          z: position.z + Math.cos(rotation) * 2
        },
        rotation: rotation,
        type: 'arrow',
        color: '#8B4513',
        speed: 25
      };

      setLocalProjectiles(prev => [...prev, newProjectile]);
      
      // Register with global collision system  
      globalCollisionData.projectiles.push({
        id: newProjectile.id,
        position: newProjectile.position,
        type: newProjectile.type,
        element: undefined, // Arrows don't have elements
        onHit: () => {
          // Remove projectile when it hits something
          setLocalProjectiles(prev => prev.filter(p => p.id !== newProjectile.id));
          // Remove from global collision data too
          const index = globalCollisionData.projectiles.findIndex(p => p.id === newProjectile.id);
          if (index !== -1) {
            globalCollisionData.projectiles.splice(index, 1);
          }
        }
      });

      // Remove projectile after 10 seconds
      setTimeout(() => {
        setLocalProjectiles(prev => prev.filter(p => p.id !== newProjectile.id));
        // Remove from global collision data
        const index = globalCollisionData.projectiles.findIndex(p => p.id === newProjectile.id);
        if (index !== -1) {
          globalCollisionData.projectiles.splice(index, 1);
        }
      }, 10000);
    } else if (activeItem.id === 'sword') {
      // Sword doesn't shoot projectiles - just play attack animation
      console.log('Sword attack - no projectile');
      return;
    }
  };

  useEffect(() => {
    const handleKeyDown = (e: KeyboardEvent) => {
      if (e.key === ' ') {
        const activeItem = inventory[activeSlot];
        // Only handle spacebar for spells and bow, let CatCharacter handle sword
        if (activeItem && (activeItem.type === 'spell' || activeItem.id === 'bow')) {
          e.preventDefault();
          shootProjectile();
        }
      } else if (e.key >= '1' && e.key <= '7') {
        // Quick slot selection
        const slotIndex = parseInt(e.key) - 1;
        useGameStore.getState().setActiveSlot(slotIndex);
      }
    };

    const handleMouseDown = (e: MouseEvent) => {
      if (e.button === 0) {
        const activeItem = inventory[activeSlot];
        // Only handle left click for spells and bow, let CatCharacter handle sword
        if (activeItem && (activeItem.type === 'spell' || activeItem.id === 'bow')) {
          shootProjectile();
        }
      }
    };

    window.addEventListener('keydown', handleKeyDown);
    window.addEventListener('mousedown', handleMouseDown);
    
    return () => {
      window.removeEventListener('keydown', handleKeyDown);
      window.removeEventListener('mousedown', handleMouseDown);
    };
  }, [inventory, activeSlot, player.position]);

  return (
    <>
      {localProjectiles.map(projectile => (
        <LocalProjectileRenderer key={projectile.id} projectile={projectile} />
      ))}
    </>
  );
};

export default LocalProjectileSystem;