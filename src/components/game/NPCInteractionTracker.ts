/**
 * Global NPC interaction tracking system
 * This allows NPCs to communicate with the quest system without breaking architecture rules
 */

interface NPCInteraction {
  npcId: string;
  timestamp: number;
  questId?: string;
}

export const npcInteractionData = {
  interactions: [] as NPCInteraction[],
  listeners: [] as ((interaction: NPCInteraction) => void)[]
};

export const recordNPCInteraction = (npcId: string, questId?: string) => {
  const interaction: NPCInteraction = {
    npcId,
    timestamp: Date.now(),
    questId
  };
  
  npcInteractionData.interactions.push(interaction);
  
  // Notify all listeners
  npcInteractionData.listeners.forEach(listener => {
    listener(interaction);
  });
};

export const subscribeToNPCInteractions = (listener: (interaction: NPCInteraction) => void) => {
  npcInteractionData.listeners.push(listener);
  
  // Return unsubscribe function
  return () => {
    const index = npcInteractionData.listeners.indexOf(listener);
    if (index !== -1) {
      npcInteractionData.listeners.splice(index, 1);
    }
  };
};

export const getInteractionsWithNPC = (npcId: string): NPCInteraction[] => {
  return npcInteractionData.interactions.filter(interaction => interaction.npcId === npcId);
};