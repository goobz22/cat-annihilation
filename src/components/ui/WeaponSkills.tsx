import { memo } from 'react';
import { useGameStore } from '../../lib/store/gameStore';

const WeaponSkills = () => {
  const weaponSkills = useGameStore(state => state.weaponSkills);
  const inventory = useGameStore(state => state.player.inventory);
  const activeSlot = useGameStore(state => state.player.activeSlot);
  const activeItem = inventory[activeSlot];
  
  // Determine which skill to show based on active weapon
  let activeSkill = null;
  let skillName = '';
  let weaponType = 'default';
  
  if (activeItem?.id === 'sword') {
    activeSkill = weaponSkills.sword;
    skillName = 'Sword';
    weaponType = 'sword';
  } else if (activeItem?.id === 'bow') {
    activeSkill = weaponSkills.bow;
    skillName = 'Bow';
    weaponType = 'bow';
  } else if (activeItem?.type === 'spell' && activeItem?.element) {
    const element = activeItem.element as 'water' | 'air' | 'earth' | 'fire';
    activeSkill = weaponSkills.magic[element];
    skillName = `${element.charAt(0).toUpperCase() + element.slice(1)} Magic`;
    weaponType = element;
  }
  
  if (!activeSkill) return null;
  
  // Calculate XP progress percentage
  const currentLevelXP = activeSkill.level === 1 ? 0 : calculateXPForLevel(activeSkill.level);
  const xpIntoLevel = activeSkill.xp - currentLevelXP;
  const xpNeededForLevel = activeSkill.xpToNextLevel - currentLevelXP;
  const xpPercent = (xpIntoLevel / xpNeededForLevel) * 100;
  
  return (
    <div className="weapon-skills-container" data-weapon={weaponType}>
      <div className="weapon-skills-title">
        {skillName} Level {activeSkill.level}
      </div>
      
      <div className="weapon-skills-progress-container">
        <div 
          className="weapon-skills-progress-bar"
          style={{ width: `${xpPercent}%` }}
        />
      </div>
      
      <div className="weapon-skills-xp">
        {activeSkill.xp.toLocaleString()} / {activeSkill.xpToNextLevel.toLocaleString()} XP
      </div>
      
      {activeSkill.level < 99 && (
        <div className="weapon-skills-next-level">
          {(activeSkill.xpToNextLevel - activeSkill.xp).toLocaleString()} XP to level {activeSkill.level + 1}
        </div>
      )}
      
      {activeSkill.level === 99 && (
        <div className="weapon-skills-max-level">
          MAX LEVEL!
        </div>
      )}
    </div>
  );
};

// Helper function (same as in store)
const calculateXPForLevel = (level: number): number => {
  let total = 0;
  for (let i = 1; i < level; i++) {
    total += Math.floor(i + 300 * Math.pow(2, i / 7));
  }
  return Math.floor(total / 2.5); // Reduced divisor from 4 to 2.5 to make leveling slower
};

export default memo(WeaponSkills);