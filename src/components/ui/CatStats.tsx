import { memo, useState, useEffect } from 'react';
import { useGameStore } from '../../lib/store/gameStore';

const CatStats = () => {
  const catStats = useGameStore(state => state.catStats);
  const player = useGameStore(state => state.player);
  const weaponSkills = useGameStore(state => state.weaponSkills);
  const inventory = useGameStore(state => state.player.inventory);
  const activeSlot = useGameStore(state => state.player.activeSlot);
  
  // Mobile detection
  const [isMobile, setIsMobile] = useState(false);
  
  useEffect(() => {
    const checkMobile = () => {
      setIsMobile(window.innerWidth <= 768);
    };
    
    checkMobile();
    window.addEventListener('resize', checkMobile);
    return () => window.removeEventListener('resize', checkMobile);
  }, []);
  
  // Calculate cat XP progress percentage
  const calculateCatXPForLevel = (level: number): number => {
    let total = 0;
    for (let i = 1; i < level; i++) {
      total += Math.floor(i + 500 * Math.pow(2, i / 6));
    }
    return Math.floor(total / 5.4); // 3x faster (1.8 * 3 = 5.4)
  };
  
  const currentLevelXP = catStats.level === 1 ? 0 : calculateCatXPForLevel(catStats.level);
  const xpIntoLevel = catStats.xp - currentLevelXP;
  const xpNeededForLevel = catStats.xpToNextLevel - currentLevelXP;
  const xpPercent = (xpIntoLevel / xpNeededForLevel) * 100;
  
  // Get active abilities
  const activeAbilities = Object.entries(catStats.abilities)
    .filter(([, active]) => active)
    .map(([ability]) => ability);
  
  const abilityIcons: Record<string, string> = {
    regeneration: '🔮',
    agility: '⚡',
    nineLives: '💀',
    predatorInstinct: '👁️',
    alphaStrike: '⚔️'
  };
  
  const abilityNames: Record<string, string> = {
    regeneration: 'Regeneration',
    agility: 'Enhanced Agility',
    nineLives: 'Nine Lives',
    predatorInstinct: 'Predator Instinct',
    alphaStrike: 'Alpha Strike'
  };

  // Get next ability info
  const getNextAbilityInfo = () => {
    if (catStats.level >= 25) return null;
    
    const nextLevel = catStats.level < 5 ? 5 : 
                     catStats.level < 10 ? 10 : 
                     catStats.level < 15 ? 15 : 
                     catStats.level < 20 ? 20 : 25;
    
    const nextAbility = catStats.level < 5 ? 'regeneration' :
                       catStats.level < 10 ? 'agility' :
                       catStats.level < 15 ? 'nineLives' :
                       catStats.level < 20 ? 'predatorInstinct' : 'alphaStrike';
    
    return {
      level: nextLevel,
      ability: nextAbility,
      icon: abilityIcons[nextAbility],
      name: abilityNames[nextAbility]
    };
  };

  const nextAbility = getNextAbilityInfo();
  
  // Weapon Skills Logic for Mobile
  const calculateXPForLevel = (level: number): number => {
    let total = 0;
    for (let i = 1; i < level; i++) {
      total += Math.floor(i + 300 * Math.pow(2, i / 7));
    }
    return Math.floor(total / 2.5);
  };
  
  const getActiveWeaponSkill = () => {
    const activeItem = inventory[activeSlot];
    
    if (activeItem?.id === 'sword') {
      return {
        skill: weaponSkills.sword,
        name: 'Sword',
        type: 'sword'
      };
    } else if (activeItem?.id === 'bow') {
      return {
        skill: weaponSkills.bow,
        name: 'Bow',
        type: 'bow'
      };
    } else if (activeItem?.type === 'spell' && activeItem?.element) {
      const element = activeItem.element as 'water' | 'air' | 'earth' | 'fire';
      return {
        skill: weaponSkills.magic[element],
        name: `${element.charAt(0).toUpperCase() + element.slice(1)}`,
        type: element
      };
    }
    
    return null;
  };
  
  const activeWeaponSkill = getActiveWeaponSkill();
  
  return (
    <div className="cat-stats-container">
      {/* Cat Level */}
      <div className="cat-stats-section">
        <span className="cat-level-display">
          🐱 Lv.{catStats.level}
        </span>
      </div>
      
      {/* XP Bar */}
      <div className="cat-stats-section">
        <div className="cat-xp-container">
          <div className="cat-xp-bar">
            <div 
              className="cat-xp-fill"
              style={{ width: `${xpPercent}%` }}
            />
          </div>
          <span className="cat-xp-text">
            {catStats.xp.toLocaleString()}/{catStats.xpToNextLevel.toLocaleString()}
          </span>
        </div>
      </div>
      
      {/* Health Display */}
      <div className="cat-stats-section">
        <div className="cat-health-container">
          <span className="cat-health-icon">❤️</span>
          <span className="cat-health-text">
            {Math.floor(player.health)}/{player.maxHealth}
          </span>
          {catStats.healthBonus > 0 && (
            <span className="cat-health-bonus">(+{catStats.healthBonus})</span>
          )}
        </div>
      </div>
      
      {/* Active Abilities */}
      {activeAbilities.length > 0 && (
        <div className="cat-stats-section">
          <div className="cat-abilities-container">
            {activeAbilities.slice(0, 3).map(ability => (
              <div 
                key={ability}
                className="cat-ability-icon"
                title={abilityNames[ability]}
              >
                <span className="cat-ability-emoji">{abilityIcons[ability]}</span>
              </div>
            ))}
            {activeAbilities.length > 3 && (
              <span className="cat-abilities-overflow">+{activeAbilities.length - 3}</span>
            )}
          </div>
        </div>
      )}
      
      {/* Next Ability Preview */}
      {nextAbility && (
        <div className="cat-stats-section">
          <div className="cat-next-ability">
            <span>Next Lv.{nextAbility.level}:</span>
            <div className="cat-next-ability-icon">
              <span className="cat-next-ability-emoji">{nextAbility.icon}</span>
            </div>
            <span className="cat-next-ability-name">{nextAbility.name}</span>
          </div>
        </div>
      )}
      
      {/* Weapon Skills - Mobile Only */}
      {isMobile && activeWeaponSkill && (
        <div className="cat-stats-section mobile-weapon-skills">
          <div className="mobile-weapon-title">
            {activeWeaponSkill.name}
          </div>
          <div className="mobile-weapon-level">
            Lv.{activeWeaponSkill.skill.level}
          </div>
          <div className="mobile-weapon-xp-container">
            <div className="mobile-weapon-xp-bar">
              <div 
                className="mobile-weapon-xp-fill"
                style={{ 
                  width: `${((activeWeaponSkill.skill.xp - (activeWeaponSkill.skill.level === 1 ? 0 : calculateXPForLevel(activeWeaponSkill.skill.level))) / (activeWeaponSkill.skill.xpToNextLevel - (activeWeaponSkill.skill.level === 1 ? 0 : calculateXPForLevel(activeWeaponSkill.skill.level)))) * 100}%`,
                  backgroundColor: activeWeaponSkill.type === 'water' ? '#3b82f6' :
                                  activeWeaponSkill.type === 'fire' ? '#ef4444' :
                                  activeWeaponSkill.type === 'earth' ? '#10b981' :
                                  activeWeaponSkill.type === 'air' ? '#8b5cf6' :
                                  activeWeaponSkill.type === 'sword' ? '#f59e0b' :
                                  activeWeaponSkill.type === 'bow' ? '#06d6a0' : '#4a90e2'
                }}
              />
            </div>
            <div className="mobile-weapon-xp-text">
              {activeWeaponSkill.skill.xp.toLocaleString()}
            </div>
            {activeWeaponSkill.skill.level < 99 && (
              <div className="mobile-weapon-next">
                {(activeWeaponSkill.skill.xpToNextLevel - activeWeaponSkill.skill.xp).toLocaleString()} to {activeWeaponSkill.skill.level + 1}
              </div>
            )}
            {activeWeaponSkill.skill.level === 99 && (
              <div className="mobile-weapon-max">
                MAX!
              </div>
            )}
          </div>
        </div>
      )}
    </div>
  );
};

export default memo(CatStats);