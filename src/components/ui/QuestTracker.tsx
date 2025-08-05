import { useEffect } from 'react';
import { useGameStore } from '../../lib/store/gameStore';
import { subscribeToNPCInteractions } from '../game/NPCInteractionTracker';

/**
 * QuestTracker monitors game state and automatically updates quest progress
 * This component runs in the background to track objectives
 */
const QuestTracker = () => {
  const storyModeActive = useGameStore(state => state.storyMode.isActive);
  const activeQuests = useGameStore(state => state.storyMode.activeQuests);
  const quests = useGameStore(state => state.storyMode.quests);
  const playerPosition = useGameStore(state => state.player.position);
  const enemies = useGameStore(state => state.enemies);
  const enemiesKilled = useGameStore(state => state.enemiesKilled);
  const playerClan = useGameStore(state => state.storyMode.playerClan);
  
  const updateQuestObjective = useGameStore(state => state.updateQuestObjective);
  const addStoryXP = useGameStore(state => state.addStoryXP);

  useEffect(() => {
    // Track movement-based objectives for territory exploration
    const checkMovementObjectives = () => {
      activeQuests.forEach(questId => {
        const quest = quests.find(q => q.id === questId);
        if (!quest) return;

        quest.objectives.forEach(objective => {
          if (objective.type === 'visit' && objective.target === 'territory-bounds') {
            // Track territory exploration based on distance traveled
            const totalDistance = Math.abs(playerPosition.x) + Math.abs(playerPosition.z);
            const explorationProgress = Math.min(Math.floor(totalDistance / 3), objective.count);
            
            if (explorationProgress > objective.currentCount) {
              updateQuestObjective(questId, objective.id, explorationProgress);
              addStoryXP('exploration', 2);
              console.log(`🗺️ Territory exploration: ${explorationProgress}/${objective.count} (+2 Exploration XP)`);
            }
          }
        });
      });
    };

    checkMovementObjectives();
  }, [playerPosition, activeQuests, quests, updateQuestObjective, addStoryXP]);

  useEffect(() => {
    // Track combat-based objectives
    const checkCombatObjectives = () => {
      activeQuests.forEach(questId => {
        const quest = quests.find(q => q.id === questId);
        if (!quest) return;

        quest.objectives.forEach(objective => {
          if (objective.type === 'kill' && objective.target === 'practice-targets') {
            // Track enemies killed as "practice targets"
            const targetsDefeated = Math.min(enemiesKilled, objective.count);
            
            if (targetsDefeated > objective.currentCount) {
              updateQuestObjective(questId, objective.id, targetsDefeated);
              addStoryXP('hunting', 8);
              addStoryXP('combat', 5);
              console.log(`🎯 Practice target defeated! Progress: ${targetsDefeated}/${objective.count} (+8 Hunting, +5 Combat XP)`);
            }
          }
        });
      });
    };

    checkCombatObjectives();
  }, [enemiesKilled, activeQuests, quests, updateQuestObjective, addStoryXP]);

  useEffect(() => {
    // Track NPC interaction objectives
    const unsubscribe = subscribeToNPCInteractions((interaction) => {
      activeQuests.forEach(questId => {
        const quest = quests.find(q => q.id === questId);
        if (!quest) return;

        quest.objectives.forEach(objective => {
          if (objective.type === 'talk' && objective.target === interaction.npcId) {
            if (objective.currentCount < objective.count) {
              updateQuestObjective(questId, objective.id, objective.currentCount + 1);
              addStoryXP('leadership', 10);
            }
          }
        });
      });
    });

    return unsubscribe;
  }, [activeQuests, quests, updateQuestObjective, addStoryXP]);

  // Auto-activate first quest when story mode starts
  useEffect(() => {
    if (storyModeActive && activeQuests.length === 0) {
      const firstQuest = quests.find(q => q.id === 'first-pawsteps');
      if (firstQuest && firstQuest.status === 'available') {
        useGameStore.getState().activateQuest('first-pawsteps');
        // Show quest notification instead of console logs
        const showDialog = useGameStore.getState().showDialog;
        showDialog(
          'Quest Master',
          'quest-giver',
          '📋 Welcome to your clan! Talk to your clan leader to begin your journey.\n\n💡 Press E near NPCs to interact, Q to open Quest Book.'
        );
      }
    }
  }, [storyModeActive, activeQuests, quests]);

  // Auto-unlock next quest when prerequisites are met
  useEffect(() => {
    quests.forEach(quest => {
      if (quest.status === 'locked' && quest.prerequisites.length > 0) {
        const completedQuests = useGameStore.getState().storyMode.completedQuests;
        const prerequisitesMet = quest.prerequisites.every(prereq => 
          completedQuests.includes(prereq)
        );
        
        if (prerequisitesMet) {
          useGameStore.getState().updateQuest(quest.id, { status: 'available' });
          console.log(`🔓 New quest available: ${quest.title}`);
        }
      }
    });
  }, [quests]);

  // Only track if story mode is active (after all hooks are called)
  if (!storyModeActive) return null;

  return null; // This component just tracks, doesn't render anything
};

export default QuestTracker;