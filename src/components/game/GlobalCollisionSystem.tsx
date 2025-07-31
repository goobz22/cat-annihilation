import { useFrame } from '@react-three/fiber';

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
    // Check projectile-enemy collisions
    globalCollisionData.projectiles.forEach(projectile => {
      globalCollisionData.enemies.forEach(enemy => {
        const dx = projectile.position.x - enemy.position.x;
        const dz = projectile.position.z - enemy.position.z;
        const dy = projectile.position.y - enemy.position.y;
        const distance = Math.sqrt(dx * dx + dy * dy + dz * dz);
        
        console.log(`Checking collision: Projectile ${projectile.id} at (${projectile.position.x.toFixed(1)}, ${projectile.position.z.toFixed(1)}) vs Enemy ${enemy.id} at (${enemy.position.x.toFixed(1)}, ${enemy.position.z.toFixed(1)}) - Distance: ${distance.toFixed(2)}`);
        
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

export default GlobalCollisionSystem;