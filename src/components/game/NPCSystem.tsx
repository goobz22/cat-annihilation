import React, { useRef, useState, useEffect, useCallback } from 'react';
import { useFrame, useThree } from '@react-three/fiber';
import * as THREE from 'three';
import { Text } from '@react-three/drei';
import { useGameStore } from '../../lib/store/gameStore';
import { recordNPCInteraction } from './NPCInteractionTracker';
import CustomizableCatMesh, { CatCustomization } from './CatCharacter/CustomizableCatMesh';
import { NPC_CUSTOMIZATIONS, generateRandomClanCat } from '../../config/clanCustomizations';

/**
 * Error boundary for NPC components - prevents NPCs from unmounting on errors
 */
class NPCErrorBoundary extends React.Component<
  { children: React.ReactNode; npcName?: string },
  { hasError: boolean; errorCount: number }
> {
  private retryTimeout?: NodeJS.Timeout;

  constructor(props: { children: React.ReactNode; npcName?: string }) {
    super(props);
    this.state = { hasError: false, errorCount: 0 };
  }

  static getDerivedStateFromError() {
    return { hasError: true };
  }

  componentDidCatch(error: Error, errorInfo: React.ErrorInfo) {
    const npcName = this.props.npcName || 'NPC';
    console.error(`[NPC ERROR BOUNDARY] ${npcName} error:`, error, errorInfo);
    
    this.setState(prevState => ({
      errorCount: prevState.errorCount + 1
    }));

    // Auto-retry after 3 seconds if error count is low
    if (this.state.errorCount < 2) {
      console.log(`[NPC ERROR BOUNDARY] Auto-retry attempt ${this.state.errorCount + 1} in 3 seconds...`);
      this.retryTimeout = setTimeout(() => {
        console.log(`[NPC ERROR BOUNDARY] Attempting to recover ${npcName}...`);
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
      const npcName = this.props.npcName || 'NPC';
      console.log(`[NPC ERROR BOUNDARY] Rendering fallback for ${npcName}`);
      
      // Render a simple fallback NPC that maintains functionality
      return (
        <group position={[0, 0, 0]}>
          <mesh position={[0, 0.75, 0]}>
            <boxGeometry args={[0.8, 1.5, 0.6]} />
            <meshStandardMaterial color="#888888" />
          </mesh>
          <group position={[0, 2.5, 0]}>
            <Text
              fontSize={0.3}
              color="#ff6666"
              anchorX="center"
              anchorY="middle"
            >
              {npcName} (Error)
            </Text>
          </group>
        </group>
      );
    }

    return this.props.children;
  }
}

/**
 * Individual NPC Component
 */
interface NPCProps {
  id: string;
  name: string;
  position: [number, number, number];
  role: 'clan-leader' | 'mentor' | 'elder' | 'warrior' | 'apprentice';
  color: string;
  questGiver?: boolean;
  availableQuests?: string[];
  onInteract?: () => void;
  customization?: CatCustomization;
  clan?: string;
}

const NPC = ({ id, name, position, role, color, questGiver, availableQuests = [], onInteract, customization, clan }: NPCProps) => {
  const meshRef = useRef<THREE.Mesh>(null);
  const nameRef = useRef<THREE.Group>(null);
  const [isNearby, setIsNearby] = useState(false);
  const [showInteractPrompt, setShowInteractPrompt] = useState(false);
  
  const { camera } = useThree();
  const playerPosition = useGameStore(state => state.player.position);
  const quests = useGameStore(state => state.storyMode.quests);
  
  // Check if this NPC has available quests
  const hasAvailableQuests = availableQuests.some(questId => {
    const quest = quests.find(q => q.id === questId);
    return quest && quest.status === 'available';
  });

  useFrame(() => {
    if (!meshRef.current || !nameRef.current) return;
    
    // Calculate distance to player
    const dx = playerPosition.x - position[0];
    const dz = playerPosition.z - position[2];
    const distance = Math.sqrt(dx * dx + dz * dz);
    
    // Show interaction prompt when nearby
    const nearby = distance < 3;
    if (nearby !== isNearby) {
      setIsNearby(nearby);
      setShowInteractPrompt(nearby);
    }
    
    // Make name tag face camera
    nameRef.current.lookAt(camera.position);
    
    // Bob animation
    const time = Date.now() * 0.002;
    meshRef.current.position.y = position[1] + 0.5 + Math.sin(time + position[0]) * 0.1;
  });

  // Handle interaction
  const showDialog = useGameStore(state => state.showDialog);

  const handleInteraction = useCallback(() => {
    if (!isNearby) return;
    
    // Record interaction for quest tracking
    recordNPCInteraction(id);
    
    // Show dialog instead of console logs
    let dialogMessage = '';
    let dialogOptions: any[] = [];
    
    if (questGiver && hasAvailableQuests) {
      // Create dialog for quest giver
      const availableQuestsList = availableQuests
        .map(questId => quests.find(q => q.id === questId))
        .filter(quest => quest && quest.status === 'available');
      
      if (availableQuestsList.length > 0) {
        dialogMessage = "I have some important tasks that need doing. Would you be interested in helping our clan?";
        dialogOptions = availableQuestsList.map(quest => ({
          id: `quest-${quest!.id}`,
          text: `📋 ${quest!.title} - ${quest!.description}`,
          action: () => {
            const gameStore = useGameStore.getState();
            gameStore.activateQuest(quest!.id);
            gameStore.addStoryXP('leadership', 5);
          }
        }));
        dialogOptions.push({
          id: 'maybe-later',
          text: 'Maybe later',
          action: () => {}
        });
      } else {
        dialogMessage = "Thank you for your service to the clan. Keep up the good work!";
      }
    } else {
      // Default interaction message based on role
      switch (role) {
        case 'clan-leader':
          dialogMessage = `Welcome to our clan, young one. Prove yourself worthy and you will find your place among us.`;
          break;
        case 'mentor':
          dialogMessage = "You're learning quickly. Let me show you more of our ways. Remember, patience and practice make a true warrior.";
          break;
        case 'elder':
          dialogMessage = "In my day, apprentices had to prove themselves against much fiercer foes... But you show promise, young one.";
          break;
        case 'warrior':
          dialogMessage = "Want to spar? You need to be ready for anything out there. The clan depends on strong warriors like you.";
          break;
        case 'apprentice':
          dialogMessage = "Wow! You're so brave! I hope I can be like you someday. Could you teach me some of your moves?";
          break;
        default:
          dialogMessage = `${name} nods at you respectfully.`;
      }
    }
    
    // Show the dialog
    showDialog(name, role, dialogMessage, dialogOptions.length > 0 ? dialogOptions : undefined);
    
    if (onInteract) {
      onInteract();
    }
  }, [isNearby, name, role, id, questGiver, hasAvailableQuests, availableQuests, quests, onInteract, showDialog]);

  // Listen for interaction key
  useEffect(() => {
    const handleKeyPress = (e: KeyboardEvent) => {
      if (e.key === 'e' || e.key === 'E') {
        if (isNearby) {
          handleInteraction();
        }
      }
    };
    
    window.addEventListener('keydown', handleKeyPress);
    return () => window.removeEventListener('keydown', handleKeyPress);
  }, [isNearby, handleInteraction]);

  // Get customization for this NPC
  const npcCustomization = customization || 
    (clan && id ? NPC_CUSTOMIZATIONS[`${clan.toLowerCase()}-${role}`] : null) ||
    (clan ? generateRandomClanCat(clan) : {
      primaryColor: color,
      eyeColor: '#4CAF50',
      noseColor: '#FF69B4',
      pattern: 'solid',
      earSize: 'normal',
      tailLength: 'normal',
      furLength: 'medium',
      bodyType: 'normal'
    } as CatCustomization);

  return (
    <group position={position}>
      {/* NPC Cat Model */}
      <group ref={meshRef} onClick={handleInteraction}>
        <CustomizableCatMesh
          customization={npcCustomization}
          scale={0.8}
        />
      </group>
      
      {/* Name Tag */}
      <group ref={nameRef} position={[0, 2.5, 0]}>
        <Text
          fontSize={0.3}
          color={questGiver && hasAvailableQuests ? "#ffd700" : "#ffffff"}
          anchorX="center"
          anchorY="middle"
        >
          {name}
          {questGiver && hasAvailableQuests && " 📜"}
        </Text>
        
        {role !== 'apprentice' && (
          <Text
            fontSize={0.2}
            color="#aaaaaa"
            anchorX="center"
            anchorY="middle"
            position={[0, -0.4, 0]}
          >
            {role.replace('-', ' ')}
          </Text>
        )}
      </group>
      
      {/* Interaction Prompt */}
      {showInteractPrompt && (
        <group position={[0, 3.2, 0]}>
          <Text
            fontSize={0.25}
            color="#4ecdc4"
            anchorX="center"
            anchorY="middle"
          >
            Press E to interact
          </Text>
        </group>
      )}
      
      {/* Quest Indicator */}
      {questGiver && hasAvailableQuests && (
        <mesh position={[0, 3.5, 0]}>
          <sphereGeometry args={[0.1, 8, 8]} />
          <meshStandardMaterial 
            color="#ffd700" 
            emissive="#ffd700"
            emissiveIntensity={0.5}
          />
        </mesh>
      )}
    </group>
  );
};

/**
 * Main NPC System Component
 */
const NPCSystem = () => {
  const playerClan = useGameStore(state => state.storyMode.playerClan);
  const addStoryXP = useGameStore(state => state.addStoryXP);
  
  if (!playerClan) return null;
  
  // Define clan-specific NPCs
  const getClanNPCs = () => {
    const baseClanColor = {
      MistClan: "#4ecdc4",
      StormClan: "#8b7355", 
      EmberClan: "#d4a574",
      FrostClan: "#b3c7d6"
    }[playerClan] || "#888888";

    return [
      // Clan Leader
      {
        id: 'clan-leader',
        name: `${playerClan} Leader`,
        position: [0, 0, -5] as [number, number, number],
        role: 'clan-leader' as const,
        color: baseClanColor,
        clan: playerClan,
        questGiver: true,
        availableQuests: ['first-pawsteps'],
        onInteract: () => {
          // Quest activation is now handled by the dialog system
          // This is just for additional actions if needed
        }
      },
      
      // Mentor
      {
        id: 'mentor',
        name: 'Your Mentor',
        position: [3, 0, -3] as [number, number, number],
        role: 'mentor' as const,
        color: baseClanColor,
        clan: playerClan,
        questGiver: true,
        availableQuests: ['territory-bounds', 'first-hunt'],
        onInteract: () => {
          addStoryXP('combat', 3);
        }
      },
      
      // Elder
      {
        id: 'elder',
        name: `${playerClan} Elder`,
        position: [-3, 0, -3] as [number, number, number],
        role: 'elder' as const,
        color: "#999999",
        clan: playerClan,
        questGiver: false,
        onInteract: () => {
          addStoryXP('mysticism', 5);
        }
      },
      
      // Fellow Warrior
      {
        id: 'fellow-warrior',
        name: `${playerClan} Warrior`,
        position: [5, 0, 0] as [number, number, number],
        role: 'warrior' as const,
        color: baseClanColor,
        clan: playerClan,
        questGiver: false,
        onInteract: () => {
          addStoryXP('combat', 7);
        }
      },
      
      // Young Apprentice
      {
        id: 'apprentice',
        name: 'Young Apprentice',
        position: [-5, 0, 2] as [number, number, number],
        role: 'apprentice' as const,
        color: "#cccccc",
        clan: playerClan,
        questGiver: false,
        onInteract: () => {
          addStoryXP('leadership', 3);
        }
      }
    ];
  };

  return (
    <>
      {getClanNPCs().map(npc => (
        <NPCErrorBoundary key={`boundary-${npc.id}`} npcName={npc.name}>
          <NPC
            key={npc.id}
            id={npc.id}
            name={npc.name}
            position={npc.position}
            role={npc.role}
            color={npc.color}
            clan={npc.clan}
            questGiver={npc.questGiver}
            availableQuests={npc.availableQuests}
            onInteract={npc.onInteract}
          />
        </NPCErrorBoundary>
      ))}
    </>
  );
};

export default NPCSystem;