# Quest System Integration Guide

## Overview

The Quest System for Cat Annihilation provides a complete quest management solution with:
- **35 Total Quests**: 20 main story quests (5 per clan), 10 side quests, 5 daily quests
- **4 Cat Clans**: Shadow, Warrior, Hunter, Mystic
- **7 Objective Types**: Kill, Collect, Escort, Explore, Talk, Survive, Defend
- **Full Progression**: XP rewards, currency, items, ability unlocks, territory unlocks

## Files Created

1. **quest_system.hpp** - Quest system interface and data structures
2. **quest_system.cpp** - Quest system implementation
3. **quest_data.hpp** - All quest definitions (hardcoded)
4. **assets/quests/quests.json** - Quest definitions (JSON format)

## Quick Start

### 1. Add QuestSystem to Game

```cpp
#include "game/systems/quest_system.hpp"

// In CatAnnihilation::initializeSystems()
auto* questSystem = ecs_.registerSystem<QuestSystem>(150); // Priority 150
questSystem->setPlayerInfo(1, Clan::Shadow); // Set initial player info

// Set up callbacks
questSystem->setOnQuestCompleted([](const Quest& quest) {
    Engine::Logger::info("Game", "Quest completed: %s", quest.title.c_str());
    // Show completion UI, grant rewards to player
});

questSystem->setOnObjectiveCompleted([](const Quest& quest, const QuestObjective& obj) {
    Engine::Logger::info("Game", "Objective completed: %s", obj.description.c_str());
    // Show objective notification
});
```

### 2. Track Quest Progress

```cpp
// When enemy is killed (in CombatSystem or HealthSystem)
auto* questSystem = ecs->getSystem<QuestSystem>();
questSystem->onEnemyKilled("Dog"); // or "BigDog", "FastDog", "BossDog"

// When item is collected
questSystem->onItemCollected("mystic_crystal");

// When location is visited
questSystem->onLocationVisited("moonlit_grove");

// When NPC is talked to
questSystem->onNPCTalkedTo("shadow_elder_nyx");

// When player survives time
questSystem->onSurviveTime(deltaTime); // Call each frame during survival objectives

// When defend objective completes
questSystem->onDefendComplete("shadow_sanctum");
```

### 3. Quest Management

```cpp
// Get available quests for player
auto availableQuests = questSystem->getAvailableQuests();
for (const auto* quest : availableQuests) {
    // Display in quest board UI
}

// Activate a quest
questSystem->activateQuest("shadow_001_awakening");

// Get active quests
auto activeQuests = questSystem->getActiveQuests();

// Get quest tracker text for HUD
std::string trackerText = questSystem->getQuestTrackerText();

// Get waypoint for active quest
Engine::vec3 waypoint = questSystem->getActiveQuestWaypoint();

// Complete quest (usually auto-completes when objectives are done)
questSystem->completeQuest("shadow_001_awakening");

// Abandon quest
questSystem->abandonQuest("side_001_kitten");
```

### 4. Player Integration

```cpp
// Update player level and clan
questSystem->setPlayerInfo(playerLevel, playerClan);

// When granting quest rewards, integrate with:
// - Player XP system
// - Currency/gold system
// - Inventory system
// - Ability system
// - Territory system

// In quest_system.cpp::grantRewards(), add actual integration:
void QuestSystem::grantRewards(const QuestReward& rewards) {
    totalXPEarned_ += rewards.xp;
    totalCurrencyEarned_ += rewards.currency;

    // Add your integration here:
    // playerSystem->addXP(rewards.xp);
    // inventorySystem->addCurrency(rewards.currency);
    // for (const auto& item : rewards.items) {
    //     inventorySystem->addItem(item);
    // }
    // if (rewards.abilityUnlock.has_value()) {
    //     abilitySystem->unlockAbility(rewards.abilityUnlock.value());
    // }
    // if (rewards.territoryUnlock.has_value()) {
    //     territorySystem->unlockTerritory(rewards.territoryUnlock.value());
    // }
}
```

## Clan System

The Quest System introduces 4 cat clans, each with unique story quests:

### Shadow Clan
- **Focus**: Stealth and agility
- **Story Arc**: Master shadow arts, defeat the Dark Alpha, lead the Eclipse assault
- **Abilities**: shadow_step, shadow_eclipse
- **Territories**: shadow_sanctum

### Warrior Clan
- **Focus**: Strength and combat
- **Story Arc**: Prove might in trials, learn Iron Paw technique, become Champion
- **Abilities**: iron_paw_strike, berserker_rage, unstoppable_force
- **Territories**: warrior_stronghold

### Hunter Clan
- **Focus**: Ranged combat and tracking
- **Story Arc**: Master precision, hunt the Primal Beast, lead the Great Hunt
- **Abilities**: eagle_eye, hunters_mark
- **Territories**: hunters_lodge

### Mystic Clan
- **Focus**: Special abilities and magic
- **Story Arc**: Awaken mystical powers, master elements, achieve Arcane Ascension
- **Abilities**: arcane_bolt, elemental_fury, arcane_ascension
- **Territories**: arcane_tower

## Quest Types

### Main Story Quests (QuestType::MainStory)
- 5 quests per clan (20 total)
- Progress through clan storyline
- Unlock abilities and territories
- Clan-specific (requiredClan set)

### Side Quests (QuestType::SideQuest)
- 10 optional quests
- Available to all clans
- Extra rewards and content
- Various objectives and stories

### Daily Quests (QuestType::Daily)
- 5 repeatable quests
- Reset daily via `questSystem->resetDailyQuests()`
- Good for farming XP and currency
- canRepeat = true

### Future Quest Types (Not Yet Implemented)
- ClanMission - Clan-specific missions
- Bounty - Target-specific hunting quests

## Objective Types

1. **Kill** - Defeat X enemies of type Y
   - Example: Kill 10 Dogs

2. **Collect** - Gather X items
   - Example: Collect 5 Mystic Crystals

3. **Escort** - Protect and guide NPC to location
   - Example: Escort kitten home

4. **Explore** - Visit a specific location
   - Example: Scout the Moonlit Grove
   - Sets waypoint for navigation

5. **Talk** - Speak with NPC
   - Example: Talk to Elder Nyx

6. **Survive** - Stay alive for X seconds
   - Example: Survive 300 seconds of combat
   - Call onSurviveTime(dt) each frame

7. **Defend** - Protect a location
   - Example: Defend the Shadow Sanctum
   - Call onDefendComplete() when successful

## Quest Rewards

### XP Rewards
- Range: 75-1500 XP
- Main story quests: Higher XP
- Daily quests: Lower XP

### Currency Rewards
- Range: 50-1000 currency
- Used for buying items, upgrades

### Item Rewards
- Equipment: weapons, armor
- Consumables: potions, buffs
- Collectibles: trophies, tokens

### Ability Unlocks
- Clan-specific abilities
- Unlocked through main story
- Examples: shadow_step, eagle_eye, berserker_rage

### Territory Unlocks
- New areas to explore
- Clan headquarters
- Examples: shadow_sanctum, warrior_stronghold, hunters_lodge, arcane_tower

## UI Integration

### Quest Board
```cpp
// Display available quests
auto available = questSystem->getAvailableQuests();
for (const auto* quest : available) {
    // Show: quest->title, quest->description
    // Show: "Level " + quest->requiredLevel
    // Show: clan requirement if quest->requiredClan.has_value()
    // Show: rewards
    // Button to activate quest
}
```

### Quest Tracker HUD
```cpp
// Display active quest objectives
std::string trackerText = questSystem->getQuestTrackerText();
// Render to HUD overlay

// Or manually build:
auto active = questSystem->getActiveQuests();
for (const auto* quest : active) {
    // Show quest title
    for (const auto& obj : quest->objectives) {
        // Show: [X] or [ ] based on obj.completed
        // Show: obj.description (obj.getProgressText())
    }
}
```

### Quest Log
```cpp
// Full quest history
auto log = questSystem->getQuestLog();
for (const auto& entry : log) {
    // Display log entry
}

// Or separate lists:
auto active = questSystem->getActiveQuests();
auto completed = questSystem->getCompletedQuests();
```

### Quest Waypoints
```cpp
// Show waypoint marker for active quest objective
Engine::vec3 waypoint = questSystem->getActiveQuestWaypoint();
if (waypoint != Engine::vec3::zero()) {
    // Render waypoint indicator at waypoint position
    // Show distance to waypoint
    // Draw path to waypoint
}
```

## Advanced Features

### Prerequisites
Quests can require other quests to be completed first:
```cpp
q.prerequisites = {"shadow_001_awakening", "shadow_002_stealth"};
```

### Time Limits
Some quests have time limits:
```cpp
q.timeLimit = 300.0f; // 5 minutes
// Quest auto-fails if time runs out
```

### Quest Statistics
```cpp
int totalCompleted = questSystem->getTotalQuestsCompleted();
int totalXP = questSystem->getTotalXPEarned();
int totalCurrency = questSystem->getTotalCurrencyEarned();
```

### Max Active Quests
```cpp
questSystem->setMaxActiveQuests(5); // Default is 5
int max = questSystem->getMaxActiveQuests();
int current = questSystem->getActiveQuestCount();
```

## Example Quest Flows

### Shadow Clan Progression
1. **Shadow Awakening** (Lvl 1) → Talk to Elder Nyx, Kill 10 dogs
2. **Master of Stealth** (Lvl 3) → Kill 15 FastDogs, Explore Moonlit Grove
3. **The Dark Alpha** (Lvl 5) → Kill Boss, Collect Shadow Gem
4. **Shadows United** (Lvl 8) → Recruit NPCs, Defend Sanctum
5. **Eclipse of the Dogs** (Lvl 10) → Kill 50 enemies, Defeat Warlord

### Side Quest Example: Lost Kitten
1. Accept quest from "worried_mother"
2. Explore location (50, 0, 100) to find kitten
3. Escort kitten back home
4. Quest auto-completes
5. Receive rewards: 150 XP, 75 currency, kitten_toy

### Daily Quest Cycle
```cpp
// At start of each day (e.g., midnight or login)
questSystem->resetDailyQuests();

// Player can activate and complete daily quests
// Can repeat the same quests the next day
```

## JSON Quest Loading (Future Enhancement)

The quest_system.cpp currently uses hardcoded quest data from quest_data.hpp. To enable JSON loading:

```cpp
// Implement JSON parsing in quest_system.cpp::loadQuestsFromFile()
// Use a JSON library (e.g., nlohmann/json, rapidjson)
// Parse assets/quests/quests.json
// Populate Quest structures from JSON data

// Then call:
questSystem->loadQuestsFromFile("assets/quests/quests.json");
```

## Performance Notes

- Quest system updates run at priority 150 (after AI, before rendering)
- Quest lookups use hash maps (O(1) average case)
- Active quest iteration is O(n) where n = active quest count (typically 1-5)
- Objective updates are O(m) where m = objectives per quest (typically 1-3)

## Extension Points

### Adding New Quest Types
1. Add to `QuestType` enum in quest_system.hpp
2. Update `QuestHelpers::questTypeToString()`
3. Add quest data to quest_data.hpp
4. Add any special logic to quest_system.cpp

### Adding New Objective Types
1. Add to `ObjectiveType` enum in quest_system.hpp
2. Update `QuestHelpers::objectiveTypeToString()`
3. Add tracking function (e.g., `onNewObjective()`)
4. Call from appropriate game system

### Adding New Clans
1. Add to `Clan` enum in quest_system.hpp
2. Update `QuestHelpers::clanToString()` and `stringToClan()`
3. Create 5 main story quests for the clan in quest_data.hpp
4. Design clan theme, abilities, and territories

## Troubleshooting

### Quest Won't Activate
- Check player level: `playerLevel >= quest.requiredLevel`
- Check clan requirement: `quest.requiredClan matches playerClan`
- Check prerequisites: All prerequisite quests completed
- Check max active quests: `activeQuestCount < maxActiveQuests`
- Check if already completed: Completed quests can't be activated unless `canRepeat = true`

### Objectives Not Updating
- Verify correct targetId matches exactly (case-sensitive)
- Ensure correct ObjectiveType is used
- Check that quest is active (`isActive = true`)
- Verify tracking function is being called from game systems

### Quest Won't Complete
- Check all objectives are marked completed
- For quests with turnInNpcId, must talk to that NPC
- For quests without turnInNpcId, auto-completes when objectives done
- Check for failed state or time limit expiration

## Integration Checklist

- [ ] Add QuestSystem to ECS in CatAnnihilation::initializeSystems()
- [ ] Set player info (level, clan) on game start and level up
- [ ] Call quest tracking functions from combat/collection/exploration systems
- [ ] Integrate reward granting with player systems (XP, currency, inventory, abilities)
- [ ] Create quest board UI for viewing available quests
- [ ] Create quest tracker HUD for active objectives
- [ ] Create quest log UI for quest history
- [ ] Implement waypoint rendering for quest locations
- [ ] Add NPC dialog system for quest givers
- [ ] Implement daily quest reset (e.g., at midnight or login)
- [ ] Add quest completion notifications/popups
- [ ] Add ability unlock system
- [ ] Add territory unlock system
- [ ] Test all quest flows for each clan

## Future Enhancements

1. **Quest Branching**: Choices that affect quest outcomes
2. **Quest Chains**: Auto-activate next quest in chain
3. **Hidden Quests**: Discovered through exploration
4. **Timed Events**: Quests available only during certain times
5. **Multiplayer Quests**: Cooperative quest completion
6. **Quest Leaderboards**: Rankings for quest completion speed
7. **Quest Rewards Scaling**: Scale rewards based on player level
8. **Quest Difficulty Tiers**: Easy/Normal/Hard/Legendary
9. **Quest Journal**: Rich text descriptions with images
10. **Quest Tracking Map**: Show quest locations on world map

---

**Created**: 2025-12-08
**Version**: 1.0
**Author**: Quest System for Cat Annihilation CUDA/Vulkan Engine
