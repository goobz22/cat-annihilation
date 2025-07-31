import { useRef, useEffect, useState, useCallback } from 'react';
import { useFrame, useThree } from '@react-three/fiber';
import { useGameStore } from '../../../lib/store/gameStore';
import { GAME_CONFIG } from '../../../config/gameConfig';
import CatMesh from './CatMesh';
import Equipment from './Equipment';

/**
 * Player controls - simplified version
 */
const Controls = () => {
  const { gl } = useThree();
  const player = useGameStore((state) => state.player);
  const setPlayerPosition = useGameStore((state) => state.setPlayerPosition);
  const setPlayerMoving = useGameStore((state) => state.setPlayerMoving);
  const setPlayerRunning = useGameStore((state) => state.setPlayerRunning);
  const setPlayerAttacking = useGameStore((state) => state.setPlayerAttacking);
  const setPlayerDefending = useGameStore((state) => state.setPlayerDefending);
  
  const [spinSensitivity, setSpinSensitivity] = useState(() => {
    const saved = localStorage.getItem('catSpinSensitivity');
    return saved ? parseFloat(saved) : GAME_CONFIG.INPUT.DEFAULT_SENSITIVITY;
  });
  
  const [moveSpeedMultiplier, setMoveSpeedMultiplier] = useState(() => {
    const saved = localStorage.getItem('catMoveSpeed');
    return saved ? parseFloat(saved) : 1.0;
  });
  
  const [mobileSpeedMultiplier, setMobileSpeedMultiplier] = useState(1.0);
  
  // Reference to controls active state
  const controlsActive = useRef({
    forward: false,
    backward: false,
    left: false,
    right: false,
    run: false,
    attack: false,
    defend: false,
  });

  // Save sensitivity to localStorage when it changes
  useEffect(() => {
    localStorage.setItem('catSpinSensitivity', spinSensitivity.toString());
  }, [spinSensitivity]);

  useEffect(() => {
    const handleSensitivityChange = (e: CustomEvent<number>) => {
      setSpinSensitivity(e.detail);
    };
    
    const handleMoveSpeedChange = (e: CustomEvent<number>) => {
      setMoveSpeedMultiplier(e.detail);
    };

    const handleMobileSpeedChange = (e: CustomEvent<number>) => {
      setMobileSpeedMultiplier(e.detail);
    };

    window.addEventListener('spinSensitivityChanged', handleSensitivityChange as EventListener);
    window.addEventListener('moveSpeedChanged', handleMoveSpeedChange as EventListener);
    window.addEventListener('mobileSpeedChange', handleMobileSpeedChange as EventListener);
    return () => {
      window.removeEventListener('spinSensitivityChanged', handleSensitivityChange as EventListener);
      window.removeEventListener('moveSpeedChanged', handleMoveSpeedChange as EventListener);
      window.removeEventListener('mobileSpeedChange', handleMobileSpeedChange as EventListener);
    };
  }, []);
  
  // Attack function - just set attacking state for sword attacks
  const performAttack = useCallback(() => {
    const inventory = useGameStore.getState().player.inventory;
    const activeSlot = useGameStore.getState().player.activeSlot;
    const activeItem = inventory[activeSlot];
    
    // Only trigger attack animation for sword
    if (activeItem && activeItem.id === 'sword') {
      console.log('Sword attack triggered');
      setPlayerAttacking(true);
      
      // Reset attack state after short delay
      setTimeout(() => {
        setPlayerAttacking(false);
      }, 300);
    }
  }, [setPlayerAttacking]);

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
        case ' ': // Spacebar for sword attacks only
          e.preventDefault();
          performAttack();
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
        case 0: // Left click for sword attacks only
          performAttack();
          break;
        case 2: // Right click for defend
          controlsActive.current.defend = true;
          setPlayerDefending(true);
          break;
      }
    };

    const handleMouseUp = (e: MouseEvent) => {
      switch (e.button) {
        case 2: // Right click for defend
          controlsActive.current.defend = false;
          setPlayerDefending(false);
          break;
        // REMOVED LEFT CLICK - let ShootingSystem handle it
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
  }, [gl, setPlayerAttacking, setPlayerDefending, setPlayerMoving, setPlayerRunning, performAttack]);
  
  // Movement and rotation update
  useFrame((_, delta) => {
    const { position } = player;
    if (!position) return;
    
    const baseSpeed = GAME_CONFIG.PLAYER.MOVEMENT_SPEED;
    const runMultiplier = GAME_CONFIG.PLAYER.RUN_SPEED / GAME_CONFIG.PLAYER.MOVEMENT_SPEED;
    let speed = baseSpeed * delta * moveSpeedMultiplier;
    
    // Apply mobile speed multiplier (from joystick distance)
    // This creates variable speed: closer to center = slower, further = faster
    // At full joystick extension (1.0), we get sprint speed
    const mobileRunMultiplier = 1.0 + (runMultiplier - 1.0) * mobileSpeedMultiplier;
    speed *= mobileRunMultiplier;
    
    // Traditional keyboard sprint still works
    if (controlsActive.current.run) {
      speed *= runMultiplier;
    }
    
    const rotationSpeed = GAME_CONFIG.PLAYER.TURN_SPEED * delta * spinSensitivity;
    let x = position.x;
    const y = 0; // Keep cat on ground
    let z = position.z;
    let rotation = position.rotation || 0;
    let isMoving = false;
    
    // Rotate left/right
    if (controlsActive.current.left) {
      rotation += rotationSpeed;
    }
    if (controlsActive.current.right) {
      rotation -= rotationSpeed;
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
    const hasRotated = controlsActive.current.left || controlsActive.current.right;
    
    if (isMoving || hasRotated) {
      // Prevent NaN values
      if (isNaN(rotation)) rotation = position.rotation || 0;
      if (isNaN(x)) x = position.x;
      if (isNaN(z)) z = position.z;
      
      setPlayerPosition({ x, y, z, rotation });
    }
    
    if (isMoving && !player.isMoving) {
      setPlayerMoving(true);
    } else if (!isMoving && player.isMoving) {
      setPlayerMoving(false);
    }
    
    // Update running state based on mobile speed multiplier or keyboard sprint
    const isRunningFromMobile = mobileSpeedMultiplier > 0.7; // Consider running if joystick > 70% extended
    const shouldBeRunning = controlsActive.current.run || isRunningFromMobile;
    if (shouldBeRunning && !player.isRunning) {
      setPlayerRunning(true);
    } else if (!shouldBeRunning && player.isRunning) {
      setPlayerRunning(false);
    }
  });
  
  return null;
};

/**
 * Main cat character component - MOVEMENT ONLY
 */
const CatCharacter = () => {
  const player = useGameStore((state) => state.player);
  const position = player.position;
  const inventory = useGameStore((state) => state.player.inventory);
  const activeSlot = useGameStore((state) => state.player.activeSlot);
  const activeItem = inventory[activeSlot];
  
  if (!position) {
    return null;
  }
  
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
      <Equipment activeItem={activeItem} />
    </group>
  );
};

export default CatCharacter;