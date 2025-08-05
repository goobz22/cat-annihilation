import React, { useState, useEffect, useRef } from 'react';
import { useFrame, useThree } from '@react-three/fiber';
import * as THREE from 'three';
import { useGameStore } from '../../lib/store/gameStore';
import { globalCollisionData } from './GlobalCollisionSystem';

/**
 * Error boundary for StoryEncounterSystem - prevents story encounters from crashing
 */
class StoryEncounterErrorBoundary extends React.Component<
  { children: React.ReactNode },
  { hasError: boolean; errorCount: number }
> {
  private retryTimeout?: NodeJS.Timeout;

  constructor(props: { children: React.ReactNode }) {
    super(props);
    this.state = { hasError: false, errorCount: 0 };
  }

  static getDerivedStateFromError() {
    return { hasError: true };
  }

  componentDidCatch(error: Error, errorInfo: React.ErrorInfo) {
    console.error('[STORY ENCOUNTER ERROR BOUNDARY] Story encounter error:', error, errorInfo);
    
    this.setState(prevState => ({
      errorCount: prevState.errorCount + 1
    }));

    // Auto-retry for story encounters
    if (this.state.errorCount < 3) {
      console.log(`[STORY ENCOUNTER ERROR BOUNDARY] Auto-retry attempt ${this.state.errorCount + 1} in 3 seconds...`);
      this.retryTimeout = setTimeout(() => {
        console.log('[STORY ENCOUNTER ERROR BOUNDARY] Attempting to recover story encounters...');
        this.setState({ hasError: false });
      }, 3000);
    }
  }

  componentWillUnmount() {
    if (this.retryTimeout) {
      clearTimeout(this.retryTimeout);
    }
  }

  render() {
    if (this.state.hasError) {
      console.log('[STORY ENCOUNTER ERROR BOUNDARY] Rendering fallback - basic training dummy');
      
      // Render a simple fallback training dummy
      return (
        <group position={[5, 0, 5]}>
          <mesh position={[0, 1, 0]} castShadow>
            <cylinderGeometry args={[0.3, 0.3, 2, 8]} />
            <meshStandardMaterial color="#8B4513" />
          </mesh>
          <mesh position={[0, 2.2, 0]} castShadow>
            <sphereGeometry args={[0.4, 8, 8]} />
            <meshStandardMaterial color="#D2691E" />
          </mesh>
          {/* Simple health indicator */}
          <mesh position={[0, 2.8, 0]}>
            <boxGeometry args={[0.8, 0.1, 0.02]} />
            <meshBasicMaterial color="#ff4444" />
          </mesh>
        </group>
      );
    }

    return this.props.children;
  }
}

/**
 * Story-specific training dummy/practice target system
 * These are non-aggressive targets for training quests
 */

interface PracticeTarget {
  id: string;
  position: { x: number; y: number; z: number };
  health: number;
  maxHealth: number;
  isActive: boolean;
}

const PracticeTargetMesh = ({ target }: { 
  target: PracticeTarget; 
}) => {
  const meshRef = useRef<THREE.Mesh>(null);
  const healthBarRef = useRef<THREE.Group>(null);
  const { camera } = useThree();

  useFrame(() => {
    if (!target.isActive) return;

    // Make health bar face camera
    if (healthBarRef.current) {
      healthBarRef.current.lookAt(camera.position);
    }

    // Simple bob animation
    if (meshRef.current) {
      const time = Date.now() * 0.001;
      meshRef.current.position.y = target.position.y + Math.sin(time + target.position.x) * 0.1;
    }
  });

  if (!target.isActive) return null;

  const healthPercent = (target.health / target.maxHealth) * 100;

  return (
    <group position={[target.position.x, target.position.y, target.position.z]}>
      {/* Target Dummy */}
      <mesh ref={meshRef}>
        <cylinderGeometry args={[0.4, 0.4, 1.5, 8]} />
        <meshStandardMaterial color="#8b7355" />
      </mesh>
      
      {/* Target Post */}
      <mesh position={[0, -1, 0]}>
        <cylinderGeometry args={[0.1, 0.1, 1, 8]} />
        <meshStandardMaterial color="#654321" />
      </mesh>

      {/* Health Bar */}
      <group ref={healthBarRef} position={[0, 1.5, 0]}>
        {/* Background */}
        <mesh position={[0, 0, 0.01]}>
          <planeGeometry args={[1, 0.1]} />
          <meshBasicMaterial color="#333333" />
        </mesh>
        
        {/* Health Fill */}
        <mesh position={[(healthPercent - 100) / 200, 0, 0.02]} scale={[healthPercent / 100, 1, 1]}>
          <planeGeometry args={[1, 0.1]} />
          <meshBasicMaterial color={healthPercent > 50 ? "#4ade80" : healthPercent > 20 ? "#fbbf24" : "#ef4444"} />
        </mesh>
      </group>
    </group>
  );
};

const StoryEncounterSystem = () => {
  const storyModeActive = useGameStore(state => state.storyMode.isActive);
  const activeQuests = useGameStore(state => state.storyMode.activeQuests);
  const quests = useGameStore(state => state.storyMode.quests);
  const [practiceTargets, setPracticeTargets] = useState<PracticeTarget[]>([]);

  // Check if we need practice targets for hunting quest
  const needsPracticeTargets = storyModeActive && activeQuests.some(questId => {
    const quest = quests.find(q => q.id === questId);
    return quest && quest.objectives.some(obj => obj.type === 'kill' && obj.target === 'practice-targets');
  });

  // Spawn practice targets when hunting quest is active
  useEffect(() => {
    if (needsPracticeTargets && practiceTargets.length === 0) {
      const newTargets: PracticeTarget[] = [];
      
      // Spawn 8 practice targets in a circle around spawn
      for (let i = 0; i < 8; i++) {
        const angle = (i / 8) * Math.PI * 2;
        const distance = 8 + Math.random() * 4; // 8-12 units from center
        const x = Math.cos(angle) * distance;
        const z = Math.sin(angle) * distance;
        
        newTargets.push({
          id: `practice-target-${i}`,
          position: { x, y: 0, z },
          health: 50,
          maxHealth: 50,
          isActive: true
        });
      }
      
      setPracticeTargets(newTargets);
      console.log('🎯 Practice targets spawned for hunting training!');
    } else if (!needsPracticeTargets && practiceTargets.length > 0) {
      // Clear targets when quest is no longer active
      setPracticeTargets([]);
    }
  }, [needsPracticeTargets, practiceTargets.length]);

  // Register practice targets with collision system
  useEffect(() => {
    practiceTargets.forEach(target => {
      if (!target.isActive) return;
      
      // Check if already registered
      const existing = globalCollisionData.enemies.find(e => e.id === target.id);
      if (existing) return;

      globalCollisionData.enemies.push({
        id: target.id,
        position: target.position,
        onDamage: (damage: number) => {
          setPracticeTargets(prev => prev.map(t => {
            if (t.id === target.id) {
              const newHealth = Math.max(0, t.health - damage);
              if (newHealth <= 0) {
                console.log(`🎯 Practice target destroyed! (${damage} damage)`);
                
                // Handle target destruction logic inline
                setTimeout(() => {
                  // Remove from collision system
                  const index = globalCollisionData.enemies.findIndex(e => e.id === target.id);
                  if (index !== -1) {
                    globalCollisionData.enemies.splice(index, 1);
                  }
                  
                  // Increment enemies killed for quest tracking
                  const currentKilled = useGameStore.getState().enemiesKilled;
                  useGameStore.setState({ enemiesKilled: currentKilled + 1 });
                  
                  // Remove from state
                  setPracticeTargets(prev => prev.filter(t => t.id !== target.id));
                }, 100); // Small delay for visual feedback
                
                return { ...t, health: 0, isActive: false };
              }
              return { ...t, health: newHealth };
            }
            return t;
          }));
        }
      });
    });

    // Cleanup destroyed targets from collision system
    return () => {
      practiceTargets.forEach(target => {
        const index = globalCollisionData.enemies.findIndex(e => e.id === target.id);
        if (index !== -1) {
          globalCollisionData.enemies.splice(index, 1);
        }
      });
    };
  }, [practiceTargets]);

  // Only active in story mode (after all hooks)
  if (!storyModeActive) return null;

  return (
    <>
      {practiceTargets.map(target => (
        <PracticeTargetMesh
          key={target.id}
          target={target}
        />
      ))}
    </>
  );
};

// Wrapped StoryEncounterSystem with error boundary
const SafeStoryEncounterSystem = () => {
  return (
    <StoryEncounterErrorBoundary>
      <StoryEncounterSystem />
    </StoryEncounterErrorBoundary>
  );
};

export default SafeStoryEncounterSystem;