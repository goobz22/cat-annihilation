import { useState, useEffect, useRef } from 'react';
import { useFrame, useThree } from '@react-three/fiber';
import * as THREE from 'three';
import { useGameStore } from '../../lib/store/gameStore';
import { globalCollisionData, globalWeaponXP } from './GlobalCollisionSystem';
import { updateWaveState } from './WaveState';

/**
 * !!!!!!! CRITICAL WARNING !!!!!!!
 * 
 * THIS ENEMY SYSTEM MUST ***NEVER*** CALL ANY ZUSTAND STORE SET() FUNCTIONS
 * DURING RUNTIME OR IT WILL CAUSE THE CAT TO FLY THROUGH THE TERRAIN!!!
 * 
 * Just like the projectile system, this uses ONLY local React state and NEVER 
 * touches the global game store except for reading initial values.
 * 
 * !!!!!!! CRITICAL WARNING !!!!!!!
 */

interface LocalEnemy {
  id: string;
  position: { x: number; y: number; z: number; rotation: number };
  health: number;
  maxHealth: number;
  lastAttackTime: number;
  lastDamageSource?: { weapon: 'sword' | 'bow' | 'magic'; element?: string };
  lastSwordHitTime?: number;
}

const LocalDogEnemy = ({ enemy, onDamage, onKill, allEnemies }: { 
  enemy: LocalEnemy;
  onDamage: (id: string, damage: number, weaponType?: { weapon: 'sword' | 'bow' | 'magic'; element?: string }) => void;
  onKill: (id: string) => void;
  allEnemies: LocalEnemy[];
}) => {
  const groupRef = useRef<THREE.Group>(null);
  const meshRef = useRef<THREE.Mesh>(null);
  const healthBarRef = useRef<THREE.Group>(null);
  const positionRef = useRef(enemy.position);
  
  const { camera } = useThree();
  const playerPosition = useGameStore(state => state.player.position);
  const damagePlayer = useGameStore(state => state.damagePlayer);
  
  const ENEMY_SPEED = 1.5;
  const ATTACK_DAMAGE = 15;
  const ATTACK_RANGE = 1.2;
  const ATTACK_COOLDOWN = 1000;
  const ENEMY_SEPARATION_RADIUS = 1.5;
  const SEPARATION_FORCE = 3.0;

  useFrame((_, delta) => {
    if (enemy.health <= 0) {
      onKill(enemy.id);
      return;
    }

    const dx = playerPosition.x - positionRef.current.x;
    const dz = playerPosition.z - positionRef.current.z;
    const distance = Math.sqrt(dx * dx + dz * dz);
    
    // Check for sword damage if player is attacking and close enough
    const player = useGameStore.getState().player;
    if (player.isAttacking && distance <= 2.0) {
      const inventory = useGameStore.getState().player.inventory;
      const activeSlot = useGameStore.getState().player.activeSlot;
      const activeItem = inventory[activeSlot];
      
      if (activeItem && activeItem.id === 'sword') {
        const currentTime = Date.now();
        const swordCooldown = 500; // Match GAME_CONFIG.WEAPONS.SWORD.ATTACK_DURATION
        
        // Only hit if enough time has passed since last sword hit on this enemy
        if (!enemy.lastSwordHitTime || currentTime - enemy.lastSwordHitTime >= swordCooldown) {
          // Calculate proper sword damage based on level
          const swordLevel = useGameStore.getState().weaponSkills.sword.level;
          const damage = 40 + (swordLevel - 1) * 10; // BASE_DAMAGE + level scaling
          
          onDamage(enemy.id, damage, { weapon: 'sword' }); // Sword damage
          
                  // Award weapon XP for hit immediately using global system
        globalWeaponXP.awardXP('sword', 10);
          
          // Update last hit time for this enemy
          enemy.lastSwordHitTime = currentTime;
        }
      }
    }
    
    // Enemy separation logic
    let separationX = 0;
    let separationZ = 0;
    let nearbyEnemies = 0;
    
    allEnemies.forEach(otherEnemy => {
      if (otherEnemy.id !== enemy.id) {
        const otherDx = positionRef.current.x - otherEnemy.position.x;
        const otherDz = positionRef.current.z - otherEnemy.position.z;
        const otherDistance = Math.sqrt(otherDx * otherDx + otherDz * otherDz);
        
        if (otherDistance < ENEMY_SEPARATION_RADIUS && otherDistance > 0) {
          const separationStrength = (ENEMY_SEPARATION_RADIUS - otherDistance) / ENEMY_SEPARATION_RADIUS;
          separationX += (otherDx / otherDistance) * separationStrength * SEPARATION_FORCE;
          separationZ += (otherDz / otherDistance) * separationStrength * SEPARATION_FORCE;
          nearbyEnemies++;
        }
      }
    });
    
    // Movement toward player
    if (distance > ATTACK_RANGE) {
      let moveX = (dx / distance) * ENEMY_SPEED * delta;
      let moveZ = (dz / distance) * ENEMY_SPEED * delta;
      
      if (nearbyEnemies > 0) {
        moveX += separationX * delta;
        moveZ += separationZ * delta;
      }
      
      const rotation = Math.atan2(dx, dz);
      
      positionRef.current.x += moveX;
      positionRef.current.z += moveZ;
      positionRef.current.rotation = rotation;
      
      if (groupRef.current) {
        groupRef.current.position.set(positionRef.current.x, positionRef.current.y, positionRef.current.z);
        groupRef.current.rotation.y = rotation;
      }
      
      // Update global collision data
      const globalEnemy = globalCollisionData.enemies.find(e => e.id === enemy.id);
      if (globalEnemy) {
        globalEnemy.position = positionRef.current;
      }
    }
    
    // Attack logic
    if (distance <= ATTACK_RANGE) {
      const currentTime = Date.now();
      if (currentTime - enemy.lastAttackTime >= ATTACK_COOLDOWN) {
        damagePlayer(ATTACK_DAMAGE);
        enemy.lastAttackTime = currentTime;
        
        if (meshRef.current) {
          meshRef.current.scale.setScalar(1.3);
          setTimeout(() => {
            if (meshRef.current) {
              meshRef.current.scale.setScalar(1);
            }
          }, 200);
        }
      }
    }
    
    // Health bar billboard
    if (healthBarRef.current && groupRef.current) {
      healthBarRef.current.lookAt(camera.position);
      healthBarRef.current.rotation.x = 0;
      healthBarRef.current.rotation.z = 0;
    }
  });

  const healthPercent = enemy.health / enemy.maxHealth;
  const enemyColor = healthPercent > 0.6 ? '#8B4513' : 
                    healthPercent > 0.3 ? '#654321' : '#3E2723';

  return (
    <group ref={groupRef} position={[enemy.position.x, enemy.position.y, enemy.position.z]}>
      {/* Dog body - same as original Enemy.tsx */}
      <mesh ref={meshRef} castShadow receiveShadow position={[0, 0.4, 0]}>
        <boxGeometry args={[0.6, 0.5, 1.2]} />
        <meshStandardMaterial color={enemyColor} />
      </mesh>
      
      {/* Dog head */}
      <mesh castShadow position={[0, 0.5, 0.6]}>
        <boxGeometry args={[0.4, 0.4, 0.5]} />
        <meshStandardMaterial color={enemyColor} />
      </mesh>
      
      {/* Snout */}
      <mesh castShadow position={[0, 0.4, 0.9]}>
        <boxGeometry args={[0.25, 0.2, 0.3]} />
        <meshStandardMaterial color="#333333" />
      </mesh>
      
      {/* Eyes */}
      <mesh position={[-0.12, 0.55, 0.75]}>
        <sphereGeometry args={[0.04, 8, 8]} />
        <meshBasicMaterial color="white" />
      </mesh>
      <mesh position={[0.12, 0.55, 0.75]}>
        <sphereGeometry args={[0.04, 8, 8]} />
        <meshBasicMaterial color="white" />
      </mesh>
      
      {/* Eye pupils */}
      <mesh position={[-0.12, 0.55, 0.78]}>
        <sphereGeometry args={[0.02, 8, 8]} />
        <meshBasicMaterial color="black" />
      </mesh>
      <mesh position={[0.12, 0.55, 0.78]}>
        <sphereGeometry args={[0.02, 8, 8]} />
        <meshBasicMaterial color="black" />
      </mesh>
      
      {/* Ears */}
      <mesh castShadow position={[-0.15, 0.7, 0.5]} rotation={[0, 0, -0.3]}>
        <coneGeometry args={[0.08, 0.2, 4]} />
        <meshStandardMaterial color={enemyColor} />
      </mesh>
      <mesh castShadow position={[0.15, 0.7, 0.5]} rotation={[0, 0, 0.3]}>
        <coneGeometry args={[0.08, 0.2, 4]} />
        <meshStandardMaterial color={enemyColor} />
      </mesh>
      
      {/* Legs */}
      <mesh castShadow position={[-0.2, 0.1, 0.4]}>
        <cylinderGeometry args={[0.06, 0.06, 0.4, 8]} />
        <meshStandardMaterial color="#1a1a1a" />
      </mesh>
      <mesh castShadow position={[0.2, 0.1, 0.4]}>
        <cylinderGeometry args={[0.06, 0.06, 0.4, 8]} />
        <meshStandardMaterial color="#1a1a1a" />
      </mesh>
      <mesh castShadow position={[-0.2, 0.1, -0.4]}>
        <cylinderGeometry args={[0.06, 0.06, 0.4, 8]} />
        <meshStandardMaterial color="#1a1a1a" />
      </mesh>
      <mesh castShadow position={[0.2, 0.1, -0.4]}>
        <cylinderGeometry args={[0.06, 0.06, 0.4, 8]} />
        <meshStandardMaterial color="#1a1a1a" />
      </mesh>
      
      {/* Tail */}
      <mesh castShadow position={[0, 0.5, -0.7]} rotation={[0.5, 0, 0]}>
        <cylinderGeometry args={[0.04, 0.06, 0.4, 8]} />
        <meshStandardMaterial color={enemyColor} />
      </mesh>
      
      {/* Nose */}
      <mesh position={[0, 0.4, 1.05]}>
        <sphereGeometry args={[0.03, 8, 8]} />
        <meshBasicMaterial color="black" />
      </mesh>
      
      {/* Health bar */}
      <group ref={healthBarRef} position={[0, 1.5, 0]}>
        <mesh position={[0, 0, 0]} rotation={[0, 0, 0]}>
          <boxGeometry args={[1, 0.08, 0.01]} />
          <meshBasicMaterial color="#333333" />
        </mesh>
        <mesh position={[-(1 - healthPercent) * 0.5, 0, 0.005]} rotation={[0, 0, 0]}>
          <boxGeometry args={[healthPercent, 0.06, 0.01]} />
          <meshBasicMaterial color={
            healthPercent > 0.6 ? '#ff4444' : 
            healthPercent > 0.3 ? '#ff8844' : '#cc2222'
          } />
        </mesh>
      </group>
    </group>
  );
};

const LocalEnemySystem = () => {
  const [localEnemies, setLocalEnemies] = useState<LocalEnemy[]>([]);
  const [currentWave, setCurrentWave] = useState(1);
  const [isWaveTransition, setIsWaveTransition] = useState(false);
  const playerPosition = useGameStore(state => state.player.position);
  const waveStarted = useRef(false);
  const isFirstWave = useRef(true); // Simple flag to skip popup on first wave
  const waveStartTime = useRef<number>(0);
  const waveCompleteTime = useRef<number>(0);
  const allowNextWaveSpawn = useRef(true);
  
  // Initialize wave display on first render
  useEffect(() => {
    console.log(`🎯 Initializing wave display to show wave 1`);
    updateWaveState({
      isTransition: false,
      currentWave: 1,
      nextWaveEnemies: 0
    });
  }, []);

  const SPAWN_DISTANCE = 15;
  const MIN_SPAWN_DISTANCE = 8;

  const getEnemiesForWave = (wave: number) => {
    return Math.floor((3 + (wave * 2)) * 1.5);
  };

  const spawnWave = () => {
    if (waveStarted.current) return;
    
    waveStarted.current = true;
    waveStartTime.current = Date.now(); // Track when wave started
    const enemiesToSpawn = getEnemiesForWave(currentWave);
    const angleStep = (Math.PI * 2) / enemiesToSpawn;
    
    console.log(`🌊 Starting wave ${currentWave} with ${enemiesToSpawn} enemies`);
    
    const newEnemies: LocalEnemy[] = [];
    
    for (let i = 0; i < enemiesToSpawn; i++) {
      const angle = i * angleStep;
      const distance = MIN_SPAWN_DISTANCE + Math.random() * (SPAWN_DISTANCE - MIN_SPAWN_DISTANCE);
      
      const spawnX = playerPosition.x + Math.cos(angle) * distance;
      const spawnZ = playerPosition.z + Math.sin(angle) * distance;
      
      const baseHealth = 100;
      const healthBonus = (currentWave - 1) * 20;
      const totalHealth = baseHealth + healthBonus;
      
      const newEnemy: LocalEnemy = {
        id: `local-enemy-${i}-${Date.now()}`,
        position: { x: spawnX, y: 0.5, z: spawnZ, rotation: 0 },
        health: totalHealth,
        maxHealth: totalHealth,
        lastAttackTime: 0
      };
      
      newEnemies.push(newEnemy);
    }
    
    // Stagger enemy spawning
    newEnemies.forEach((enemy, index) => {
      setTimeout(() => {
        setLocalEnemies(prev => [...prev, enemy]);
        
        // Register with global collision system
        globalCollisionData.enemies.push({
          id: enemy.id,
          position: enemy.position,
          onDamage: (damage: number, weaponType?: { weapon: 'sword' | 'bow' | 'magic'; element?: string }) => {
            setLocalEnemies(prev => prev.map(e => {
              if (e.id === enemy.id) {
                const newHealth = Math.max(0, e.health - damage);
                console.log(`Enemy ${enemy.id} took ${damage} damage, health: ${newHealth}/${e.maxHealth}`);
                return { 
                  ...e, 
                  health: newHealth,
                  lastDamageSource: weaponType
                };
              }
              return e;
            }));
          }
        });
      }, index * 200);
    });
  };

  const damageEnemy = (id: string, damage: number, weaponType?: { weapon: 'sword' | 'bow' | 'magic'; element?: string }) => {
    setLocalEnemies(prev => prev.map(enemy => {
      if (enemy.id === id) {
        const newHealth = Math.max(0, enemy.health - damage);
        const updatedEnemy = { 
          ...enemy, 
          health: newHealth,
          lastDamageSource: weaponType
        };
        
        // Update sword hit time if it was a sword attack
        if (weaponType?.weapon === 'sword') {
          updatedEnemy.lastSwordHitTime = Date.now();
        }
        
        return updatedEnemy;
      }
      return enemy;
    }));
  };

  const killEnemy = (id: string) => {
    // Find the enemy to get damage source before removing it
    const killedEnemy = localEnemies.find(enemy => enemy.id === id);
    
    setLocalEnemies(prev => prev.filter(enemy => enemy.id !== id));
    
    // Remove from global collision data
    const index = globalCollisionData.enemies.findIndex(e => e.id === id);
    if (index !== -1) {
      globalCollisionData.enemies.splice(index, 1);
    }
    
    // Add XP for kill
    const addCatXP = useGameStore.getState().addCatXP;
    
    addCatXP(5);
    
    // Award weapon XP based on what killed the enemy
    if (killedEnemy?.lastDamageSource) {
      const { weapon, element } = killedEnemy.lastDamageSource;
      const killXP = 15; // Bonus XP for kills vs just hits
      
      if (weapon === 'magic' && element) {
        globalWeaponXP.awardXP('magic', killXP, element);
      } else if (weapon === 'sword' || weapon === 'bow') {
        globalWeaponXP.awardXP(weapon, killXP);
      }
    }
  };

  // Wave progression logic
  useFrame(() => {
    const currentTime = Date.now();
    
    // Don't do anything during wave transitions
    if (isWaveTransition) return;
    
    // Start wave if no enemies and wave hasn't started (only when allowed)
    if (localEnemies.length === 0 && !waveStarted.current && !isWaveTransition && allowNextWaveSpawn.current) {
      spawnWave();
    }
    
    // Check if wave is complete - but only if wave has actually started and enemies are gone
    if (waveStarted.current && localEnemies.length === 0) {
      // For first wave, wait 3 seconds after wave started before checking completion
      if (isFirstWave.current) {
        const timeElapsed = currentTime - waveStartTime.current;
        if (timeElapsed < 3000) return; // Wait 3 seconds before checking first wave completion
        
        // Set completion time if not already set
        if (waveCompleteTime.current === 0) {
          waveCompleteTime.current = currentTime;
          console.log(`🌊 Wave 1 enemies defeated, waiting 2 seconds before showing completion...`);
          return;
        }
        
        // Wait 2 seconds after enemies are gone before showing completion
        if (currentTime - waveCompleteTime.current < 2000) return;
        
        console.log(`🎯 Wave 1 complete! Showing popup`);
        isFirstWave.current = false;
        waveStarted.current = false;
        waveCompleteTime.current = 0;
        
        // Block spawning until popup completes
        allowNextWaveSpawn.current = false;
        setIsWaveTransition(true);
        
        const nextWave = currentWave + 1;
        const nextWaveEnemyCount = getEnemiesForWave(nextWave);
        
        // Update wave transition UI
        updateWaveState({
          isTransition: true,
          currentWave: currentWave,
          nextWaveEnemies: nextWaveEnemyCount
        });
        
        setTimeout(() => {
          setCurrentWave(nextWave);
          setIsWaveTransition(false);
          
          // Update the Zustand store immediately for WaveDisplay
          console.log(`🎯 Updating wave display to show wave ${nextWave}`);
          updateWaveState({
            isTransition: false,
            currentWave: nextWave,
            nextWaveEnemies: 0
          });
          
          // Allow next wave to spawn after popup disappears
          allowNextWaveSpawn.current = true;
        }, 4000);
        return;
      }
      
      // For other waves, wait 2 seconds after enemies are gone
      if (waveCompleteTime.current === 0) {
        waveCompleteTime.current = currentTime;
        console.log(`🎯 Wave ${currentWave} enemies defeated, waiting 2 seconds before showing completion...`);
        return;
      }
      
      // Wait 2 seconds after enemies are gone before showing completion
      if (currentTime - waveCompleteTime.current < 2000) return;
      
      console.log(`🎯 Wave ${currentWave} complete!`);
      setIsWaveTransition(true);
      waveStarted.current = false;
      waveCompleteTime.current = 0;
      
      const nextWave = currentWave + 1;
      const nextWaveEnemyCount = getEnemiesForWave(nextWave);
      
      // Update wave transition UI
      updateWaveState({
        isTransition: true,
        currentWave: currentWave,
        nextWaveEnemies: nextWaveEnemyCount
      });
      
      setTimeout(() => {
        setCurrentWave(nextWave);
        setIsWaveTransition(false);
        
        // Update the Zustand store immediately for WaveDisplay
        console.log(`🎯 Updating wave display to show wave ${nextWave}`);
        updateWaveState({
          isTransition: false,
          currentWave: nextWave,
          nextWaveEnemies: 0
        });
        
        // Allow next wave to spawn after popup disappears
        allowNextWaveSpawn.current = true;
      }, 4000); // Increased to 4 seconds to show message longer
    }
  });

  return (
    <>
      {/* Render dog enemies */}
      {localEnemies.map(enemy => (
        <LocalDogEnemy
          key={enemy.id}
          enemy={enemy}
          onDamage={damageEnemy}
          onKill={killEnemy}
          allEnemies={localEnemies}
        />
      ))}
    </>
  );
};

export default LocalEnemySystem;