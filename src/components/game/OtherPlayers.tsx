import { useRef, useMemo, useState } from 'react';
import { useFrame } from '@react-three/fiber';
import * as THREE from 'three';
import { useGameStore } from '@/lib/store/gameStore';

/**
 * Component for a single other player's cat
 */
const OtherCat = ({ 
  position, 
  rotation = 0,
  isMoving = false,
  isAttacking = false,
  cat = null,
}: { 
  position: { x: number; y: number; z: number };
  rotation?: number;
  isMoving?: boolean;
  isAttacking?: boolean;
  cat?: any;
}) => {
  // Group ref
  const group = useRef<THREE.Group>(null);
  
  // Animation state
  const [animState, setAnimState] = useState({
    walkTime: 0,
    attackTime: 0,
  });
  
  // Generate color based on cat data
  const catColor = useMemo(() => {
    if (!cat?.name) return '#964B00'; // Default brown
    
    // Hash the cat name to generate a color
    let hash = 0;
    for (let i = 0; i < cat.name.length; i++) {
      hash = cat.name.charCodeAt(i) + ((hash << 5) - hash);
    }
    
    // Convert hash to RGB
    const r = (hash & 0xFF0000) >> 16;
    const g = (hash & 0x00FF00) >> 8;
    const b = hash & 0x0000FF;
    
    return `#${r.toString(16).padStart(2, '0')}${g.toString(16).padStart(2, '0')}${b.toString(16).padStart(2, '0')}`;
  }, [cat?.name]);
  
  // Update animations
  useFrame((_, delta) => {
    if (!group.current) return;
    
    // Update animation timers
    const newAnimState = { ...animState };
    
    if (isMoving) {
      // Walking animation
      newAnimState.walkTime += delta * 5;
      
      // Animate body parts
      const walkCycle = Math.sin(newAnimState.walkTime);
      
      // Apply animations to the cat parts
      const body = group.current.children[0] as THREE.Mesh;
      if (body) {
        body.position.y = Math.abs(walkCycle) * 0.1 + 0.2;
      }
      
      // Head bobbing
      const head = group.current.children[1] as THREE.Mesh;
      if (head) {
        head.rotation.x = walkCycle * 0.1;
      }
      
      // Tail wagging
      const tail = group.current.children[3] as THREE.Mesh;
      if (tail) {
        tail.rotation.z = Math.sin(newAnimState.walkTime * 2) * 0.2;
      }
    }
    
    if (isAttacking) {
      // Attack animation
      newAnimState.attackTime += delta * 10;
      
      // Forward lunge
      const attackPhase = Math.min(1, (newAnimState.attackTime % 1) * 2);
      const attackOffset = attackPhase < 0.5 
        ? attackPhase * 2 // Forward motion
        : 1 - ((attackPhase - 0.5) * 2); // Backward motion
      
      // Apply attack animation
      group.current.position.z = attackOffset * 0.3;
      
      // Reset walk cycle during attack
      newAnimState.walkTime = 0;
    } else {
      // Reset attack timer when not attacking
      newAnimState.attackTime = 0;
      group.current.position.z = 0;
    }
    
    // Idle animation when not moving or attacking
    if (!isMoving && !isAttacking) {
      // Breathing animation
      newAnimState.walkTime += delta;
      const breathCycle = Math.sin(newAnimState.walkTime);
      
      // Subtle body movement
      const body = group.current.children[0] as THREE.Mesh;
      if (body) {
        body.position.y = Math.abs(breathCycle) * 0.05 + 0.2;
      }
      
      // Slight tail movement
      const tail = group.current.children[3] as THREE.Mesh;
      if (tail) {
        tail.rotation.z = Math.sin(newAnimState.walkTime * 0.5) * 0.1;
      }
    }
    
    // Update animation state
    setAnimState(newAnimState);
  });
  
  return (
    <group
      ref={group}
      position={[position.x, position.y, position.z]}
      rotation={[0, rotation, 0]}
    >
      {/* Simple cat shape made of primitives */}
      <mesh castShadow position={[0, 0.2, 0]}>
        <boxGeometry args={[0.8, 0.4, 1.2]} />
        <meshStandardMaterial color={catColor} />
      </mesh>
      
      {/* Head */}
      <mesh castShadow position={[0, 0.4, 0.5]}>
        <boxGeometry args={[0.6, 0.4, 0.5]} />
        <meshStandardMaterial color={catColor} />
      </mesh>
      
      {/* Eyes */}
      <group position={[0, 0.5, 0.7]}>
        <mesh position={[0.15, 0, 0.15]} castShadow>
          <sphereGeometry args={[0.08, 8, 8]} />
          <meshStandardMaterial color="#000000" />
        </mesh>
        <mesh position={[-0.15, 0, 0.15]} castShadow>
          <sphereGeometry args={[0.08, 8, 8]} />
          <meshStandardMaterial color="#000000" />
        </mesh>
      </group>
      
      {/* Tail */}
      <mesh castShadow position={[0, 0.3, -0.7]} rotation={[0, 0, 0]}>
        <boxGeometry args={[0.15, 0.15, 0.6]} />
        <meshStandardMaterial color={catColor} />
      </mesh>
      
      {/* Ears */}
      <group position={[0, 0.6, 0.5]}>
        <mesh position={[0.25, 0.15, 0]} castShadow>
          <coneGeometry args={[0.1, 0.2, 4]} />
          <meshStandardMaterial color={catColor} />
        </mesh>
        <mesh position={[-0.25, 0.15, 0]} castShadow>
          <coneGeometry args={[0.1, 0.2, 4]} />
          <meshStandardMaterial color={catColor} />
        </mesh>
      </group>
      
      {/* Legs */}
      <group position={[0, 0, 0]}>
        {/* Front legs */}
        <mesh position={[0.3, -0.15, 0.4]} castShadow>
          <boxGeometry args={[0.15, 0.3, 0.15]} />
          <meshStandardMaterial color={catColor} />
        </mesh>
        <mesh position={[-0.3, -0.15, 0.4]} castShadow>
          <boxGeometry args={[0.15, 0.3, 0.15]} />
          <meshStandardMaterial color={catColor} />
        </mesh>
        
        {/* Back legs */}
        <mesh position={[0.3, -0.15, -0.4]} castShadow>
          <boxGeometry args={[0.15, 0.3, 0.15]} />
          <meshStandardMaterial color={catColor} />
        </mesh>
        <mesh position={[-0.3, -0.15, -0.4]} castShadow>
          <boxGeometry args={[0.15, 0.3, 0.15]} />
          <meshStandardMaterial color={catColor} />
        </mesh>
      </group>
      
      {/* Player name label */}
      {cat?.name && (
        <group position={[0, 1.2, 0]}>
          <mesh>
            <planeGeometry args={[1, 0.3]} />
            <meshBasicMaterial color="#00000080" />
          </mesh>
          {/* In a real implementation, you'd use a TextGeometry or HTML overlay for the name */}
        </group>
      )}
    </group>
  );
};

/**
 * Component to render all other players
 */
const OtherPlayers = () => {
  const otherPlayers = useGameStore((state) => state.otherPlayers);
  const playerIds = Object.keys(otherPlayers);
  
  if (playerIds.length === 0) return null;
  
  return (
    <group>
      {playerIds.map((playerId) => {
        const { position, isMoving, isAttacking, cat } = otherPlayers[playerId];
        if (!position) return null;
        
        return (
          <OtherCat
            key={playerId}
            position={position}
            rotation={position.rotation}
            isMoving={isMoving}
            isAttacking={isAttacking}
            cat={cat}
          />
        );
      })}
    </group>
  );
};

export default OtherPlayers; 