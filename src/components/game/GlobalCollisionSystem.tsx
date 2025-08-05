import React from 'react';
import { useFrame } from '@react-three/fiber';

/**
 * Error boundary for GlobalCollisionSystem - prevents collision system crashes
 */
class CollisionErrorBoundary extends React.Component<
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
    console.error('[COLLISION ERROR BOUNDARY] Collision system error:', error, errorInfo);
    
    this.setState(prevState => ({
      errorCount: prevState.errorCount + 1
    }));

    // Auto-retry for collision system
    if (this.state.errorCount < 4) {
      console.log(`[COLLISION ERROR BOUNDARY] Auto-retry attempt ${this.state.errorCount + 1} in 1 second...`);
      this.retryTimeout = setTimeout(() => {
        console.log('[COLLISION ERROR BOUNDARY] Attempting to recover collision system...');
        this.setState({ hasError: false });
      }, 1000); // Fast retry for critical collision system
    }
  }

  componentWillUnmount() {
    if (this.retryTimeout) {
      clearTimeout(this.retryTimeout);
    }
  }

  render() {
    if (this.state.hasError) {
      console.log('[COLLISION ERROR BOUNDARY] Collision system disabled - manual combat only');
      
      // Return invisible component - collision detection will be disabled but game continues
      return null;
    }

    return this.props.children;
  }
}

// Global collision arrays that both systems can access
export const globalCollisionData = {
  projectiles: [] as Array<{
    id: string;
    position: { x: number; y: number; z: number };
    type: string;
    element?: string; // For spell projectiles
    onHit?: () => void;
  }>,
  enemies: [] as Array<{
    id: string;
    position: { x: number; y: number; z: number };
    onDamage?: (damage: number, weaponType?: { weapon: 'sword' | 'bow' | 'magic'; element?: string }) => void;
  }>
};

// Global weapon XP system (follows architecture - no Zustand calls during gameplay)
export const globalWeaponXP = {
  // Award weapon XP and sync to UI
  awardXP: (weapon: 'sword' | 'bow' | 'magic', xp: number, element?: string) => {
    // This will be called by UI components to sync the display
    const updateUI = () => {
      // Get the current store state without causing re-renders during gameplay
      const store = (globalThis as any).__GAME_STORE__;
      if (store) {
        store.getState().addWeaponXP(weapon, xp, element);
      }
    };
    
    // Defer UI update to avoid Zustand calls during useFrame
    setTimeout(updateUI, 0);
    
    console.log(`🎉 ${weapon}${element ? ` (${element})` : ''} gained ${xp} XP!`);
  }
};

const GlobalCollisionSystem = () => {
  useFrame(() => {
    // Clean up any orphaned projectiles that are too far from origin (crash prevention)
    globalCollisionData.projectiles = globalCollisionData.projectiles.filter(projectile => {
      const distanceFromOrigin = Math.sqrt(
        projectile.position.x * projectile.position.x + 
        projectile.position.z * projectile.position.z
      );
      if (distanceFromOrigin > 100) {
        console.log(`🗑️ Removing far projectile ${projectile.id} at distance ${distanceFromOrigin.toFixed(1)} from origin`);
        return false;
      }
      return true;
    });
    
    // Check projectile-enemy collisions
    globalCollisionData.projectiles.forEach(projectile => {
      globalCollisionData.enemies.forEach(enemy => {
        const dx = projectile.position.x - enemy.position.x;
        const dz = projectile.position.z - enemy.position.z;
        const dy = projectile.position.y - enemy.position.y;
        const distance = Math.sqrt(dx * dx + dy * dy + dz * dz);
        
        // Skip collision check if projectile is too far away (performance optimization)
        if (distance > 15) return; // Skip distant projectiles - they'll be cleaned up anyway
        
        if (distance < 1.5) { // Increased collision radius
          // Collision detected!
          console.log(`🎯 HIT! Projectile ${projectile.type} hit enemy!`);
          
          let damage = 20;
          let weaponType: { weapon: 'sword' | 'bow' | 'magic'; element?: string } | undefined;
          
          if (projectile.type === 'arrow') {
            damage = 30; // Bow damage
            weaponType = { weapon: 'bow' };
            globalWeaponXP.awardXP('bow', 10);
          } else {
            // Magic spell damage
            damage = 20;
            if (projectile.element) {
              weaponType = { weapon: 'magic', element: projectile.element };
              globalWeaponXP.awardXP('magic', 10, projectile.element);
            }
          }
          
          if (enemy.onDamage) {
            enemy.onDamage(damage, weaponType);
          }
          
          if (projectile.onHit) {
            projectile.onHit();
          }
        }
      });
    });
  });

  return null;
};

// Wrapped GlobalCollisionSystem with error boundary
const SafeGlobalCollisionSystem = () => {
  return (
    <CollisionErrorBoundary>
      <GlobalCollisionSystem />
    </CollisionErrorBoundary>
  );
};

export default SafeGlobalCollisionSystem;