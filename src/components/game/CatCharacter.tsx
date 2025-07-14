import { useRef, useEffect, useState } from 'react';
import { useFrame, useThree } from '@react-three/fiber';
import * as THREE from 'three';
import { useGameStore } from '@/lib/store/gameStore';

/**
 * Cat character mesh and animations
 */
const CatMesh = ({ 
  isMoving, 
  isRunning,
  isJumping,
  isAttacking,
  isDefending 
}: { 
  isMoving: boolean; 
  isRunning: boolean;
  isJumping: boolean;
  isAttacking: boolean;
  isDefending: boolean;
}) => {
  // Load model and animations
  const group = useRef<THREE.Group>(null);
  
  // Animation state
  const [animState, setAnimState] = useState({
    walkTime: 0,
    attackTime: 0,
    defendTime: 0,
    headRotation: 0,
    tailRotation: 0,
  });
  
  // Update animations
  useFrame((_, delta) => {
    if (!group.current) return;
    
    // Update animation timers
    const newAnimState = { ...animState };
    
    if (isMoving) {
      // Determine animation speed based on running state
      const animSpeed = isRunning ? 10 : 5;
      
      // Walking/running animation
      newAnimState.walkTime += delta * animSpeed;
      
      // Animate body parts
      const walkCycle = Math.sin(newAnimState.walkTime);
      
      // Apply animations to the cat parts
      const body = group.current.children[0] as THREE.Mesh;
      // More exaggerated movement when running
      const heightAdjustment = isRunning ? 0.15 : 0.1;
      body.position.y = Math.abs(walkCycle) * heightAdjustment + 0.2;
      
      // Head bobbing
      const head = group.current.children[1] as THREE.Mesh;
      const headRotation = isRunning ? 0.15 : 0.1;
      head.rotation.x = walkCycle * headRotation;
      
      // Tail wagging
      const tail = group.current.children[3] as THREE.Mesh;
      const tailWagSpeed = isRunning ? 3 : 2;
      const tailWagAmount = isRunning ? 0.3 : 0.2;
      newAnimState.tailRotation = Math.sin(newAnimState.walkTime * tailWagSpeed) * tailWagAmount;
      tail.rotation.z = newAnimState.tailRotation;
    }
    
    // Jumping animation - handled by the Controls component
    
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
    } else if (isDefending) {
      // Defend animation
      newAnimState.defendTime += delta * 5;
      
      // Defensive crouch
      const body = group.current.children[0] as THREE.Mesh;
      body.position.y = 0.15; // Lower stance
      
      // Lower head in defensive posture
      const head = group.current.children[1] as THREE.Mesh;
      head.rotation.x = 0.2; // Tilt head forward
      
      // Aggressive tail positioning
      const tail = group.current.children[3] as THREE.Mesh;
      tail.rotation.x = 0.5; // Raise tail in alert position
      
      // Slight side-to-side defensive movement
      const defendPhase = Math.sin(newAnimState.defendTime * 3);
      group.current.rotation.y = defendPhase * 0.1;
    } else {
      // Reset animation timers when not attacking or defending
      newAnimState.attackTime = 0;
      newAnimState.defendTime = 0;
      group.current.position.z = 0;
      group.current.rotation.y = 0;
    }
    
    // Idle animation when not moving, jumping, attacking or defending
    if (!isMoving && !isJumping && !isAttacking && !isDefending) {
      // Breathing animation
      newAnimState.walkTime += delta;
      const breathCycle = Math.sin(newAnimState.walkTime);
      
      // Subtle body movement
      const body = group.current.children[0] as THREE.Mesh;
      body.position.y = Math.abs(breathCycle) * 0.05 + 0.2;
      
      // Slight tail movement
      const tail = group.current.children[3] as THREE.Mesh;
      newAnimState.tailRotation = Math.sin(newAnimState.walkTime * 0.5) * 0.1;
      tail.rotation.z = newAnimState.tailRotation;
    }
    
    // Update animation state
    setAnimState(newAnimState);
  });
  
  return (
    <group ref={group}>
      {/* Simple cat shape made of primitives */}
      <mesh castShadow position={[0, 0.2, 0]}>
        <boxGeometry args={[0.8, 0.4, 1.2]} />
        <meshStandardMaterial color="#964B00" />
      </mesh>
      
      {/* Head */}
      <mesh castShadow position={[0, 0.4, 0.5]}>
        <boxGeometry args={[0.6, 0.4, 0.5]} />
        <meshStandardMaterial color="#964B00" />
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
        <meshStandardMaterial color="#964B00" />
      </mesh>
      
      {/* Ears */}
      <group position={[0, 0.6, 0.5]}>
        <mesh position={[0.25, 0.15, 0]} castShadow>
          <coneGeometry args={[0.1, 0.2, 4]} />
          <meshStandardMaterial color="#964B00" />
        </mesh>
        <mesh position={[-0.25, 0.15, 0]} castShadow>
          <coneGeometry args={[0.1, 0.2, 4]} />
          <meshStandardMaterial color="#964B00" />
        </mesh>
      </group>
      
      {/* Legs */}
      <group position={[0, 0, 0]}>
        {/* Front legs */}
        <mesh position={[0.3, -0.15, 0.4]} castShadow>
          <boxGeometry args={[0.15, 0.3, 0.15]} />
          <meshStandardMaterial color="#7a3a00" />
        </mesh>
        <mesh position={[-0.3, -0.15, 0.4]} castShadow>
          <boxGeometry args={[0.15, 0.3, 0.15]} />
          <meshStandardMaterial color="#7a3a00" />
        </mesh>
        
        {/* Back legs */}
        <mesh position={[0.3, -0.15, -0.4]} castShadow>
          <boxGeometry args={[0.15, 0.3, 0.15]} />
          <meshStandardMaterial color="#7a3a00" />
        </mesh>
        <mesh position={[-0.3, -0.15, -0.4]} castShadow>
          <boxGeometry args={[0.15, 0.3, 0.15]} />
          <meshStandardMaterial color="#7a3a00" />
        </mesh>
      </group>
    </group>
  );
};

/**
 * Equipment and item display
 */
const Equipment = ({ equipment }: { equipment: any }) => {
  // This would display armor, claws, etc.
  if (!equipment) return null;
  
  return (
    <group>
      {equipment.armor && (
        <mesh position={[0, 0.45, 0]} castShadow>
          <boxGeometry args={[0.9, 0.1, 1.3]} />
          <meshStandardMaterial color="#777777" metalness={0.8} roughness={0.2} />
        </mesh>
      )}
      
      {equipment.claws && (
        <group>
          <mesh position={[0.3, -0.3, 0.5]} rotation={[0.3, 0, 0]} castShadow>
            <coneGeometry args={[0.05, 0.2, 4]} />
            <meshStandardMaterial color="#DDDDDD" metalness={0.9} roughness={0.1} />
          </mesh>
          <mesh position={[-0.3, -0.3, 0.5]} rotation={[0.3, 0, 0]} castShadow>
            <coneGeometry args={[0.05, 0.2, 4]} />
            <meshStandardMaterial color="#DDDDDD" metalness={0.9} roughness={0.1} />
          </mesh>
        </group>
      )}
    </group>
  );
};

/**
 * Player controls
 */
const Controls = () => {
  const { gl } = useThree();
  const player = useGameStore((state) => state.player);
  const setPlayerPosition = useGameStore((state) => state.setPlayerPosition);
  const setPlayerMoving = useGameStore((state) => state.setPlayerMoving);
  const setPlayerRunning = useGameStore((state) => state.setPlayerRunning);
  const setPlayerJumping = useGameStore((state) => state.setPlayerJumping);
  const setPlayerAttacking = useGameStore((state) => state.setPlayerAttacking);
  const setPlayerDefending = useGameStore((state) => state.setPlayerDefending);
  
  // Reference to controls active state
  const controlsActive = useRef({
    forward: false,
    backward: false,
    left: false,
    right: false,
    run: false,
    jump: false,
    attack: false,
    defend: false,
  });

  // Jump animation timing
  const jumpState = useRef({
    jumpHeight: 0,
    jumpTime: 0,
    isJumping: false,
    jumpDuration: 0.5, // seconds
  });
  
  // Handle keyboard input
  useEffect(() => {
    const handleKeyDown = (e: KeyboardEvent) => {
      switch (e.key.toLowerCase()) {
        case 'w': controlsActive.current.forward = true; break;
        case 's': controlsActive.current.backward = true; break;
        case 'a': controlsActive.current.left = true; break;
        case 'd': controlsActive.current.right = true; break;
        case 'shift': 
          controlsActive.current.run = true; 
          setPlayerRunning(true);
          break;
        case ' ': // Space bar for jump
          if (!jumpState.current.isJumping) {
            controlsActive.current.jump = true;
            jumpState.current.isJumping = true;
            jumpState.current.jumpTime = 0;
            setPlayerJumping(true);
          }
          break;
      }
    };
    
    const handleKeyUp = (e: KeyboardEvent) => {
      switch (e.key.toLowerCase()) {
        case 'w': controlsActive.current.forward = false; break;
        case 's': controlsActive.current.backward = false; break;
        case 'a': controlsActive.current.left = false; break;
        case 'd': controlsActive.current.right = false; break;
        case 'shift': 
          controlsActive.current.run = false; 
          setPlayerRunning(false);
          break;
      }
    };

    const handleMouseDown = (e: MouseEvent) => {
      switch (e.button) {
        case 0: // Left click for attack
          controlsActive.current.attack = true;
          setPlayerAttacking(true);
          break;
        case 2: // Right click for defend
          controlsActive.current.defend = true;
          setPlayerDefending(true);
          break;
      }
    };

    const handleMouseUp = (e: MouseEvent) => {
      switch (e.button) {
        case 0: // Left click for attack
          controlsActive.current.attack = false;
          setPlayerAttacking(false);
          break;
        case 2: // Right click for defend
          controlsActive.current.defend = false;
          setPlayerDefending(false);
          break;
      }
    };

    // Prevent context menu on right click
    const handleContextMenu = (e: MouseEvent) => {
      e.preventDefault();
    };
    
    // Add event listeners
    window.addEventListener('keydown', handleKeyDown);
    window.addEventListener('keyup', handleKeyUp);
    
    // Capture canvas for focus and mouse events
    const canvas = gl.domElement;
    canvas.addEventListener('click', () => canvas.focus());
    canvas.addEventListener('mousedown', handleMouseDown);
    canvas.addEventListener('mouseup', handleMouseUp);
    canvas.addEventListener('contextmenu', handleContextMenu);
    canvas.tabIndex = 1;
    
    return () => {
      window.removeEventListener('keydown', handleKeyDown);
      window.removeEventListener('keyup', handleKeyUp);
      canvas.removeEventListener('mousedown', handleMouseDown);
      canvas.removeEventListener('mouseup', handleMouseUp);
      canvas.removeEventListener('contextmenu', handleContextMenu);
    };
  }, [gl, setPlayerAttacking, setPlayerDefending, setPlayerJumping, setPlayerMoving, setPlayerRunning]);
  
  // Movement, rotation, and jump update
  useFrame((_, delta) => {
    const { position } = player;
    if (!position) return;
    
    const baseSpeed = 5;
    const runMultiplier = 1.8;
    let speed = baseSpeed * delta;
    if (controlsActive.current.run) {
      speed *= runMultiplier;
    }
    
    const rotationSpeed = 2 * delta;
    let x = position.x;
    let y = position.y;
    let z = position.z;
    let rotation = position.rotation || 0;
    let isMoving = false;
    
    // Handle jumping
    if (jumpState.current.isJumping) {
      jumpState.current.jumpTime += delta;
      
      if (jumpState.current.jumpTime < jumpState.current.jumpDuration) {
        // Parabolic jump curve
        const jumpProgress = jumpState.current.jumpTime / jumpState.current.jumpDuration;
        const jumpCurve = Math.sin(jumpProgress * Math.PI);
        const maxJumpHeight = 1.5;
        
        jumpState.current.jumpHeight = jumpCurve * maxJumpHeight;
      } else {
        // End jump
        jumpState.current.isJumping = false;
        jumpState.current.jumpHeight = 0;
        controlsActive.current.jump = false;
        setPlayerJumping(false);
      }
    }
    
    // Apply jump height to position
    y = jumpState.current.jumpHeight;
    
    // Rotate left/right
    if (controlsActive.current.left) {
      rotation += rotationSpeed;
      isMoving = true;
    }
    if (controlsActive.current.right) {
      rotation -= rotationSpeed;
      isMoving = true;
    }
    
    // Move forward/backward in the direction we're facing
    if (controlsActive.current.forward) {
      x += Math.sin(rotation) * speed;
      z += Math.cos(rotation) * speed;
      isMoving = true;
    }
    if (controlsActive.current.backward) {
      x -= Math.sin(rotation) * speed;
      z -= Math.cos(rotation) * speed;
      isMoving = true;
    }
    
    // Apply movement and rotation
    if (isMoving || jumpState.current.isJumping) {
      // Update player position in store
      setPlayerPosition({ x, y, z, rotation });
      
      // Update player movement state if not already set
      if (!player.isMoving) {
        setPlayerMoving(true);
      }
    } else if (player.isMoving) {
      // If we were moving but stopped, update state
      setPlayerMoving(false);
    }
  });
  
  return null;
};

/**
 * Main cat character component
 */
const CatCharacter = () => {
  const player = useGameStore((state) => state.player);
  const position = player.position;
  
  if (!position) return null;
  
  return (
    <group position={[position.x, position.y, position.z]} rotation={[0, position.rotation || 0, 0]}>
      <Controls />
      <CatMesh 
        isMoving={player.isMoving} 
        isRunning={player.isRunning}
        isJumping={player.isJumping}
        isAttacking={player.isAttacking} 
        isDefending={player.isDefending}
      />
      <Equipment equipment={player.cat?.equipment} />
    </group>
  );
};

export default CatCharacter; 