import React, { useRef, memo } from 'react';
import { useFrame } from '@react-three/fiber';
import * as THREE from 'three';
import { useGameStore } from '../../../lib/store/gameStore';

interface IInventoryItem {
  id: string;
  name: string;
  type: 'spell' | 'weapon' | 'consumable';
  element?: 'water' | 'air' | 'earth' | 'fire';
  icon: string;
  color: string;
}

interface EquipmentProps {
  activeItem: IInventoryItem | null;
}

/**
 * Equipment and weapon display for the cat character
 */
const Equipment = memo(({ activeItem }: EquipmentProps) => {
  const groupRef = useRef<THREE.Group>(null);
  const shieldRef = useRef<THREE.Group>(null);
  const bowRef = useRef<THREE.Group>(null);
  
  const player = useGameStore((state) => state.player);
  const inventory = useGameStore((state) => state.player.inventory);
  
  // Check if shield is equipped in any slot
  const equippedShield = inventory.find(item => item?.id === 'shield');
  const activeBow = activeItem?.id === 'bow';
  
  // Equipment animations
  useFrame((state) => {
    // Sword animation
    if (activeItem?.id === 'sword' && groupRef.current) {
      if (player.isAttacking) {
        console.log('SWORD ANIMATION: Attacking!');
        const swingTime = state.clock.getElapsedTime() * 8;
        const swingPhase = Math.sin(swingTime);
        const thrustDistance = Math.max(0, swingPhase) * 0.8;
        groupRef.current.position.z = 1.3 + thrustDistance;
        groupRef.current.rotation.x = swingPhase * 0.3;
      } else {
        groupRef.current.rotation.x = THREE.MathUtils.lerp(groupRef.current.rotation.x, 0, 0.1);
        groupRef.current.position.z = THREE.MathUtils.lerp(groupRef.current.position.z, 1.3, 0.1);
      }
    }
    
    // Bow animation
    if (activeBow && bowRef.current) {
      if (player.isAttacking) {
        const drawTime = state.clock.getElapsedTime() * 6;
        const drawPhase = Math.sin(drawTime);
        const drawDistance = Math.max(0, drawPhase) * 0.3;
        bowRef.current.position.z = 1.2 - drawDistance;
        bowRef.current.rotation.x = drawPhase * 0.1;
      } else {
        bowRef.current.position.z = THREE.MathUtils.lerp(bowRef.current.position.z, 1.2, 0.1);
        bowRef.current.rotation.x = THREE.MathUtils.lerp(bowRef.current.rotation.x, 0, 0.1);
      }
    }
    
    // Shield animation
    if (equippedShield && shieldRef.current) {
      if (player.isAttacking) {
        const bashTime = state.clock.getElapsedTime() * 10;
        const bashPhase = Math.sin(bashTime);
        const bashDistance = Math.max(0, bashPhase) * 0.6;
        shieldRef.current.position.z = 1.3 + bashDistance;
        shieldRef.current.rotation.x = bashPhase * 0.2;
      } else if (player.isDefending) {
        shieldRef.current.position.z = THREE.MathUtils.lerp(shieldRef.current.position.z, 1.6, 0.2);
        shieldRef.current.position.y = THREE.MathUtils.lerp(shieldRef.current.position.y, 0.6, 0.2);
        shieldRef.current.rotation.x = THREE.MathUtils.lerp(shieldRef.current.rotation.x, 0, 0.1);
      } else {
        shieldRef.current.position.z = THREE.MathUtils.lerp(shieldRef.current.position.z, 1.3, 0.1);
        shieldRef.current.position.y = THREE.MathUtils.lerp(shieldRef.current.position.y, 0.5, 0.1);
        shieldRef.current.rotation.x = THREE.MathUtils.lerp(shieldRef.current.rotation.x, 0, 0.1);
      }
      
      // Subtle breathing/idle movement
      if (!player.isAttacking && !player.isDefending) {
        const time = state.clock.getElapsedTime();
        const breathCycle = Math.sin(time * 0.5) * 0.02;
        shieldRef.current.position.y += breathCycle;
      }
    }
  });
  
  return (
    <group>
      {/* Sword */}
      {activeItem?.id === 'sword' && (
        <group ref={groupRef} position={[0.4, 0.5, 1.3]} rotation={[0, 0, 0]}>
          <mesh position={[0, 0.1, 0]} castShadow>
            <boxGeometry args={[0.08, 0.6, 0.02]} />
            <meshBasicMaterial color="#ffffff" />
          </mesh>
          <mesh position={[0, 0.5, 0]} rotation={[0, 0, 0]} castShadow>
            <coneGeometry args={[0.04, 0.2, 4]} />
            <meshBasicMaterial color="#ffffff" />
          </mesh>
          <mesh position={[0, -0.2, 0]} castShadow>
            <boxGeometry args={[0.3, 0.05, 0.05]} />
            <meshBasicMaterial color="#e0e0e0" />
          </mesh>
          <mesh position={[0, -0.4, 0]} castShadow>
            <cylinderGeometry args={[0.06, 0.06, 0.3, 8]} />
            <meshBasicMaterial color="#D2691E" />
          </mesh>
          <mesh position={[0, -0.55, 0]} castShadow>
            <sphereGeometry args={[0.05, 8, 8]} />
            <meshBasicMaterial color="#d0d0d0" />
          </mesh>
        </group>
      )}
      
      {/* Shield */}
      {equippedShield && (
        <group ref={shieldRef} position={[0, 0.5, 1.3]} rotation={[0, 0, 0]}>
          <mesh position={[0, 0, 0]} castShadow receiveShadow rotation={[Math.PI/2, 0, 0]}>
            <cylinderGeometry args={[0.35, 0.35, 0.08, 16]} />
            <meshStandardMaterial color="#8B4513" metalness={0.3} roughness={0.7} />
          </mesh>
          <mesh position={[0, 0, 0.05]} castShadow rotation={[Math.PI/2, 0, 0]}>
            <sphereGeometry args={[0.08, 12, 12]} />
            <meshStandardMaterial color="#FFD700" metalness={0.9} roughness={0.1} />
          </mesh>
          <mesh position={[0, 0, -0.06]} castShadow rotation={[Math.PI/2, 0, 0]}>
            <cylinderGeometry args={[0.03, 0.03, 0.2, 8]} />
            <meshStandardMaterial color="#8B4513" />
          </mesh>
          <mesh position={[0, 0.1, -0.04]} rotation={[0, 0, 0]} castShadow>
            <cylinderGeometry args={[0.015, 0.015, 0.15, 6]} />
            <meshStandardMaterial color="#654321" />
          </mesh>
          <mesh position={[0, -0.1, -0.04]} rotation={[0, 0, 0]} castShadow>
            <cylinderGeometry args={[0.015, 0.015, 0.15, 6]} />
            <meshStandardMaterial color="#654321" />
          </mesh>
        </group>
      )}
      
      {/* Bow */}
      {activeBow && (
        <group ref={bowRef} position={[-0.3, 0.5, 1.2]} rotation={[0, 0, 0]}>
          <mesh position={[0, 0, 0]} castShadow>
            <torusGeometry args={[0.4, 0.02, 8, 16, Math.PI]} />
            <meshStandardMaterial color="#8B4513" />
          </mesh>
          <mesh position={[0, -0.4, 0]} castShadow>
            <cylinderGeometry args={[0.025, 0.025, 0.15, 8]} />
            <meshStandardMaterial color="#654321" />
          </mesh>
          <mesh position={[0, 0, 0]} castShadow>
            <cylinderGeometry args={[0.002, 0.002, 0.8, 4]} />
            <meshStandardMaterial color="#FFFFFF" />
          </mesh>
        </group>
      )}
    </group>
  );
});

Equipment.displayName = 'Equipment';

export default Equipment;