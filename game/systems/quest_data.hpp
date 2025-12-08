#pragma once

#include "quest_system.hpp"
#include <vector>

namespace CatGame {
namespace QuestData {

/**
 * Get all quests in the game
 */
inline std::vector<Quest> getAllQuests() {
    std::vector<Quest> quests;

    // ========================================================================
    // SHADOW CLAN - Main Story Quests
    // ========================================================================

    // Shadow Clan Quest 1: Awakening
    {
        Quest q;
        q.id = "shadow_001_awakening";
        q.title = "Shadow Awakening";
        q.description = "Begin your journey as a Shadow Clan warrior";
        q.loreText = "The Shadow Clan has long protected the realm from the darkness. Now, "
                     "the canine hordes threaten our territory. Prove yourself worthy.";
        q.type = QuestType::MainStory;
        q.requiredLevel = 1;
        q.requiredClan = Clan::Shadow;
        q.questGiverId = "shadow_elder_nyx";

        QuestObjective obj1;
        obj1.type = ObjectiveType::Talk;
        obj1.description = "Speak with Elder Nyx";
        obj1.targetId = "shadow_elder_nyx";
        obj1.requiredCount = 1;
        q.objectives.push_back(obj1);

        QuestObjective obj2;
        obj2.type = ObjectiveType::Kill;
        obj2.description = "Defeat 10 dogs to prove your skill";
        obj2.targetId = "Dog";
        obj2.requiredCount = 10;
        q.objectives.push_back(obj2);

        q.rewards.xp = 100;
        q.rewards.currency = 50;
        q.rewards.items = {"shadow_cloak"};

        quests.push_back(q);
    }

    // Shadow Clan Quest 2: Stealth Training
    {
        Quest q;
        q.id = "shadow_002_stealth";
        q.title = "Master of Stealth";
        q.description = "Learn the ancient arts of shadow combat";
        q.loreText = "To strike from darkness is the way of the Shadow Clan. Master stealth, "
                     "and your enemies will never see you coming.";
        q.type = QuestType::MainStory;
        q.requiredLevel = 3;
        q.requiredClan = Clan::Shadow;
        q.prerequisites = {"shadow_001_awakening"};
        q.questGiverId = "shadow_master_umbra";

        QuestObjective obj1;
        obj1.type = ObjectiveType::Kill;
        obj1.description = "Eliminate 15 FastDogs using stealth";
        obj1.targetId = "FastDog";
        obj1.requiredCount = 15;
        q.objectives.push_back(obj1);

        QuestObjective obj2;
        obj2.type = ObjectiveType::Explore;
        obj2.description = "Scout the Moonlit Grove";
        obj2.targetId = "moonlit_grove";
        obj2.requiredCount = 1;
        obj2.waypointLocation = Engine::vec3(150.0f, 0.0f, 200.0f);
        q.objectives.push_back(obj2);

        q.rewards.xp = 250;
        q.rewards.currency = 100;
        q.rewards.abilityUnlock = "shadow_step";

        quests.push_back(q);
    }

    // Shadow Clan Quest 3: The Dark Alpha
    {
        Quest q;
        q.id = "shadow_003_dark_alpha";
        q.title = "The Dark Alpha";
        q.description = "Face the leader of the shadow pack";
        q.loreText = "A corrupted alpha leads the dogs in our territory. This beast must be "
                     "stopped before it spreads its influence further.";
        q.type = QuestType::MainStory;
        q.requiredLevel = 5;
        q.requiredClan = Clan::Shadow;
        q.prerequisites = {"shadow_002_stealth"};
        q.questGiverId = "shadow_elder_nyx";

        QuestObjective obj1;
        obj1.type = ObjectiveType::Kill;
        obj1.description = "Defeat the Dark Alpha";
        obj1.targetId = "BossDog";
        obj1.requiredCount = 1;
        q.objectives.push_back(obj1);

        QuestObjective obj2;
        obj2.type = ObjectiveType::Collect;
        obj2.description = "Retrieve the Shadow Gem from the Alpha";
        obj2.targetId = "shadow_gem";
        obj2.requiredCount = 1;
        q.objectives.push_back(obj2);

        q.rewards.xp = 500;
        q.rewards.currency = 250;
        q.rewards.items = {"shadow_blade", "alpha_fang"};
        q.rewards.territoryUnlock = "shadow_sanctum";

        quests.push_back(q);
    }

    // Shadow Clan Quest 4: Shadows United
    {
        Quest q;
        q.id = "shadow_004_united";
        q.title = "Shadows United";
        q.description = "Rally the Shadow Clan warriors";
        q.loreText = "The time has come to unite our scattered forces. Gather the shadow "
                     "warriors and prepare for the final confrontation.";
        q.type = QuestType::MainStory;
        q.requiredLevel = 8;
        q.requiredClan = Clan::Shadow;
        q.prerequisites = {"shadow_003_dark_alpha"};
        q.questGiverId = "shadow_elder_nyx";

        QuestObjective obj1;
        obj1.type = ObjectiveType::Talk;
        obj1.description = "Recruit Shadow Assassin Whisper";
        obj1.targetId = "shadow_whisper";
        obj1.requiredCount = 1;
        q.objectives.push_back(obj1);

        QuestObjective obj2;
        obj2.type = ObjectiveType::Talk;
        obj2.description = "Recruit Shadow Scout Dusk";
        obj2.targetId = "shadow_dusk";
        obj2.requiredCount = 1;
        q.objectives.push_back(obj2);

        QuestObjective obj3;
        obj3.type = ObjectiveType::Defend;
        obj3.description = "Defend the Shadow Sanctum";
        obj3.targetId = "shadow_sanctum";
        obj3.requiredCount = 1;
        q.objectives.push_back(obj3);

        q.rewards.xp = 750;
        q.rewards.currency = 400;
        q.rewards.items = {"shadow_armor_set"};

        quests.push_back(q);
    }

    // Shadow Clan Quest 5: Eclipse
    {
        Quest q;
        q.id = "shadow_005_eclipse";
        q.title = "Eclipse of the Dogs";
        q.description = "Lead the final assault against the canine invasion";
        q.loreText = "Under the cover of the lunar eclipse, we strike. The dogs will know "
                     "the true power of the Shadow Clan!";
        q.type = QuestType::MainStory;
        q.requiredLevel = 10;
        q.requiredClan = Clan::Shadow;
        q.prerequisites = {"shadow_004_united"};
        q.questGiverId = "shadow_elder_nyx";

        QuestObjective obj1;
        obj1.type = ObjectiveType::Kill;
        obj1.description = "Defeat 50 enemies in the Eclipse Battle";
        obj1.targetId = "Dog";
        obj1.requiredCount = 50;
        q.objectives.push_back(obj1);

        QuestObjective obj2;
        obj2.type = ObjectiveType::Kill;
        obj2.description = "Slay the Corrupted Warlord";
        obj2.targetId = "BossDog";
        obj2.requiredCount = 1;
        q.objectives.push_back(obj2);

        q.rewards.xp = 1500;
        q.rewards.currency = 1000;
        q.rewards.items = {"eclipse_shadowblade", "shadow_clan_crown"};
        q.rewards.abilityUnlock = "shadow_eclipse";

        quests.push_back(q);
    }

    // ========================================================================
    // WARRIOR CLAN - Main Story Quests
    // ========================================================================

    // Warrior Clan Quest 1: Trial by Combat
    {
        Quest q;
        q.id = "warrior_001_trial";
        q.title = "Trial by Combat";
        q.description = "Prove your strength in the Warrior Arena";
        q.loreText = "Strength is earned through battle. Show the Warrior Clan that you have "
                     "what it takes to stand among legends.";
        q.type = QuestType::MainStory;
        q.requiredLevel = 1;
        q.requiredClan = Clan::Warrior;
        q.questGiverId = "warrior_chief_rex";

        QuestObjective obj1;
        obj1.type = ObjectiveType::Kill;
        obj1.description = "Defeat 15 dogs in honorable combat";
        obj1.targetId = "Dog";
        obj1.requiredCount = 15;
        q.objectives.push_back(obj1);

        QuestObjective obj2;
        obj2.type = ObjectiveType::Kill;
        obj2.description = "Defeat 5 BigDogs to prove your might";
        obj2.targetId = "BigDog";
        obj2.requiredCount = 5;
        q.objectives.push_back(obj2);

        q.rewards.xp = 100;
        q.rewards.currency = 50;
        q.rewards.items = {"warrior_axe"};

        quests.push_back(q);
    }

    // Warrior Clan Quest 2: The Iron Paw
    {
        Quest q;
        q.id = "warrior_002_iron_paw";
        q.title = "The Iron Paw";
        q.description = "Master the legendary Iron Paw technique";
        q.loreText = "Ancient warriors forged the Iron Paw technique to crush any foe. "
                     "Train under Master Fang to learn this devastating art.";
        q.type = QuestType::MainStory;
        q.requiredLevel = 3;
        q.requiredClan = Clan::Warrior;
        q.prerequisites = {"warrior_001_trial"};
        q.questGiverId = "warrior_master_fang";

        QuestObjective obj1;
        obj1.type = ObjectiveType::Survive;
        obj1.description = "Survive 300 seconds of combat training";
        obj1.targetId = "survive";
        obj1.requiredCount = 300;
        q.objectives.push_back(obj1);

        QuestObjective obj2;
        obj2.type = ObjectiveType::Kill;
        obj2.description = "Defeat 20 enemies using melee combat only";
        obj2.targetId = "Dog";
        obj2.requiredCount = 20;
        q.objectives.push_back(obj2);

        q.rewards.xp = 250;
        q.rewards.currency = 100;
        q.rewards.abilityUnlock = "iron_paw_strike";

        quests.push_back(q);
    }

    // Warrior Clan Quest 3: Berserker's Rage
    {
        Quest q;
        q.id = "warrior_003_berserker";
        q.title = "Berserker's Rage";
        q.description = "Channel the ancient berserker fury";
        q.loreText = "The most fearsome warriors tap into a primal rage that makes them "
                     "unstoppable. But beware - the berserker's path is dangerous.";
        q.type = QuestType::MainStory;
        q.requiredLevel = 5;
        q.requiredClan = Clan::Warrior;
        q.prerequisites = {"warrior_002_iron_paw"};
        q.questGiverId = "warrior_chief_rex";

        QuestObjective obj1;
        obj1.type = ObjectiveType::Kill;
        obj1.description = "Defeat 30 enemies without retreating";
        obj1.targetId = "Dog";
        obj1.requiredCount = 30;
        q.objectives.push_back(obj1);

        QuestObjective obj2;
        obj2.type = ObjectiveType::Kill;
        obj2.description = "Slay the Brutal Warlord";
        obj2.targetId = "BossDog";
        obj2.requiredCount = 1;
        q.objectives.push_back(obj2);

        q.rewards.xp = 500;
        q.rewards.currency = 250;
        q.rewards.items = {"berserker_helmet", "rage_stone"};
        q.rewards.abilityUnlock = "berserker_rage";

        quests.push_back(q);
    }

    // Warrior Clan Quest 4: The Battle of Crimson Fields
    {
        Quest q;
        q.id = "warrior_004_crimson";
        q.title = "Battle of Crimson Fields";
        q.description = "Lead the warriors into legendary battle";
        q.loreText = "The Crimson Fields will run red with the blood of our enemies. "
                     "Rally the warriors and show no mercy!";
        q.type = QuestType::MainStory;
        q.requiredLevel = 8;
        q.requiredClan = Clan::Warrior;
        q.prerequisites = {"warrior_003_berserker"};
        q.questGiverId = "warrior_chief_rex";

        QuestObjective obj1;
        obj1.type = ObjectiveType::Defend;
        obj1.description = "Hold the Crimson Fields against the invasion";
        obj1.targetId = "crimson_fields";
        obj1.requiredCount = 1;
        q.objectives.push_back(obj1);

        QuestObjective obj2;
        obj2.type = ObjectiveType::Kill;
        obj2.description = "Defeat 100 enemies in the battle";
        obj2.targetId = "Dog";
        obj2.requiredCount = 100;
        q.objectives.push_back(obj2);

        q.rewards.xp = 750;
        q.rewards.currency = 400;
        q.rewards.items = {"crimson_armor_set"};
        q.rewards.territoryUnlock = "warrior_stronghold";

        quests.push_back(q);
    }

    // Warrior Clan Quest 5: Champion of War
    {
        Quest q;
        q.id = "warrior_005_champion";
        q.title = "Champion of War";
        q.description = "Become the legendary Champion of the Warrior Clan";
        q.loreText = "Only the strongest warrior can claim the title of Champion. "
                     "Face the ultimate challenge and earn eternal glory!";
        q.type = QuestType::MainStory;
        q.requiredLevel = 10;
        q.requiredClan = Clan::Warrior;
        q.prerequisites = {"warrior_004_crimson"};
        q.questGiverId = "warrior_chief_rex";

        QuestObjective obj1;
        obj1.type = ObjectiveType::Kill;
        obj1.description = "Defeat 3 Boss Dogs in single combat";
        obj1.targetId = "BossDog";
        obj1.requiredCount = 3;
        q.objectives.push_back(obj1);

        QuestObjective obj2;
        obj2.type = ObjectiveType::Survive;
        obj2.description = "Survive 600 seconds in the Arena of Champions";
        obj2.targetId = "survive";
        obj2.requiredCount = 600;
        q.objectives.push_back(obj2);

        q.rewards.xp = 1500;
        q.rewards.currency = 1000;
        q.rewards.items = {"champion_warhammer", "warrior_crown"};
        q.rewards.abilityUnlock = "unstoppable_force";

        quests.push_back(q);
    }

    // ========================================================================
    // HUNTER CLAN - Main Story Quests
    // ========================================================================

    // Hunter Clan Quest 1: The First Hunt
    {
        Quest q;
        q.id = "hunter_001_first_hunt";
        q.title = "The First Hunt";
        q.description = "Prove your worth as a Hunter";
        q.loreText = "The Hunter Clan values precision and patience. Show us you can track "
                     "and eliminate your targets with skill.";
        q.type = QuestType::MainStory;
        q.requiredLevel = 1;
        q.requiredClan = Clan::Hunter;
        q.questGiverId = "hunter_elder_talon";

        QuestObjective obj1;
        obj1.type = ObjectiveType::Kill;
        obj1.description = "Hunt down 12 FastDogs";
        obj1.targetId = "FastDog";
        obj1.requiredCount = 12;
        q.objectives.push_back(obj1);

        QuestObjective obj2;
        obj2.type = ObjectiveType::Collect;
        obj2.description = "Collect tracking marks from your prey";
        obj2.targetId = "tracking_mark";
        obj2.requiredCount = 12;
        q.objectives.push_back(obj2);

        q.rewards.xp = 100;
        q.rewards.currency = 50;
        q.rewards.items = {"hunter_bow"};

        quests.push_back(q);
    }

    // Hunter Clan Quest 2: Eagle Eye
    {
        Quest q;
        q.id = "hunter_002_eagle_eye";
        q.title = "Eagle Eye Training";
        q.description = "Master the art of precision shooting";
        q.loreText = "A true hunter never misses. Train your eye to see what others cannot, "
                     "and your arrows will always find their mark.";
        q.type = QuestType::MainStory;
        q.requiredLevel = 3;
        q.requiredClan = Clan::Hunter;
        q.prerequisites = {"hunter_001_first_hunt"};
        q.questGiverId = "hunter_master_keen";

        QuestObjective obj1;
        obj1.type = ObjectiveType::Kill;
        obj1.description = "Defeat 25 enemies from range";
        obj1.targetId = "Dog";
        obj1.requiredCount = 25;
        q.objectives.push_back(obj1);

        QuestObjective obj2;
        obj2.type = ObjectiveType::Explore;
        obj2.description = "Scout the Eagle's Perch";
        obj2.targetId = "eagles_perch";
        obj2.requiredCount = 1;
        obj2.waypointLocation = Engine::vec3(200.0f, 50.0f, 150.0f);
        q.objectives.push_back(obj2);

        q.rewards.xp = 250;
        q.rewards.currency = 100;
        q.rewards.abilityUnlock = "eagle_eye";

        quests.push_back(q);
    }

    // Hunter Clan Quest 3: The Primal Hunt
    {
        Quest q;
        q.id = "hunter_003_primal";
        q.title = "The Primal Hunt";
        q.description = "Track and hunt the legendary Primal Beast";
        q.loreText = "Deep in the wilderness lurks a beast of legend. Only the greatest "
                     "hunter can track and defeat this primal terror.";
        q.type = QuestType::MainStory;
        q.requiredLevel = 5;
        q.requiredClan = Clan::Hunter;
        q.prerequisites = {"hunter_002_eagle_eye"};
        q.questGiverId = "hunter_elder_talon";

        QuestObjective obj1;
        obj1.type = ObjectiveType::Explore;
        obj1.description = "Find the Primal Beast's lair";
        obj1.targetId = "primal_lair";
        obj1.requiredCount = 1;
        obj1.waypointLocation = Engine::vec3(300.0f, 0.0f, 300.0f);
        q.objectives.push_back(obj1);

        QuestObjective obj2;
        obj2.type = ObjectiveType::Kill;
        obj2.description = "Slay the Primal Beast";
        obj2.targetId = "BossDog";
        obj2.requiredCount = 1;
        q.objectives.push_back(obj2);

        q.rewards.xp = 500;
        q.rewards.currency = 250;
        q.rewards.items = {"primal_bow", "beast_pelt"};
        q.rewards.territoryUnlock = "hunters_lodge";

        quests.push_back(q);
    }

    // Hunter Clan Quest 4: Pack Tactics
    {
        Quest q;
        q.id = "hunter_004_pack";
        q.title = "Pack Tactics";
        q.description = "Assemble your hunting party";
        q.loreText = "Even the lone hunter needs allies. Gather the finest trackers and "
                     "prepare for the great hunt.";
        q.type = QuestType::MainStory;
        q.requiredLevel = 8;
        q.requiredClan = Clan::Hunter;
        q.prerequisites = {"hunter_003_primal"};
        q.questGiverId = "hunter_elder_talon";

        QuestObjective obj1;
        obj1.type = ObjectiveType::Talk;
        obj1.description = "Recruit Tracker Swift";
        obj1.targetId = "hunter_swift";
        obj1.requiredCount = 1;
        q.objectives.push_back(obj1);

        QuestObjective obj2;
        obj2.type = ObjectiveType::Talk;
        obj2.description = "Recruit Sniper Hawkeye";
        obj2.targetId = "hunter_hawkeye";
        obj2.requiredCount = 1;
        q.objectives.push_back(obj2);

        QuestObjective obj3;
        obj3.type = ObjectiveType::Kill;
        obj3.description = "Hunt 40 enemies as a pack";
        obj3.targetId = "Dog";
        obj3.requiredCount = 40;
        q.objectives.push_back(obj3);

        q.rewards.xp = 750;
        q.rewards.currency = 400;
        q.rewards.items = {"hunters_armor_set"};

        quests.push_back(q);
    }

    // Hunter Clan Quest 5: The Great Hunt
    {
        Quest q;
        q.id = "hunter_005_great_hunt";
        q.title = "The Great Hunt";
        q.description = "Lead the ultimate hunt against the invaders";
        q.loreText = "The time has come for the Great Hunt. Every dog in our territory "
                     "shall be tracked down and eliminated. None shall escape!";
        q.type = QuestType::MainStory;
        q.requiredLevel = 10;
        q.requiredClan = Clan::Hunter;
        q.prerequisites = {"hunter_004_pack"};
        q.questGiverId = "hunter_elder_talon";

        QuestObjective obj1;
        obj1.type = ObjectiveType::Kill;
        obj1.description = "Eliminate 75 enemies in the Great Hunt";
        obj1.targetId = "Dog";
        obj1.requiredCount = 75;
        q.objectives.push_back(obj1);

        QuestObjective obj2;
        obj2.type = ObjectiveType::Kill;
        obj2.description = "Defeat the Pack Alpha";
        obj2.targetId = "BossDog";
        obj2.requiredCount = 1;
        q.objectives.push_back(obj2);

        q.rewards.xp = 1500;
        q.rewards.currency = 1000;
        q.rewards.items = {"legendary_longbow", "hunter_crown"};
        q.rewards.abilityUnlock = "hunters_mark";

        quests.push_back(q);
    }

    // ========================================================================
    // MYSTIC CLAN - Main Story Quests
    // ========================================================================

    // Mystic Clan Quest 1: Awakening the Gift
    {
        Quest q;
        q.id = "mystic_001_awakening";
        q.title = "Awakening the Gift";
        q.description = "Discover your mystical powers";
        q.loreText = "The Mystic Clan channels ancient energies that flow through all things. "
                     "You possess the gift - now you must learn to control it.";
        q.type = QuestType::MainStory;
        q.requiredLevel = 1;
        q.requiredClan = Clan::Mystic;
        q.questGiverId = "mystic_oracle_luna";

        QuestObjective obj1;
        obj1.type = ObjectiveType::Collect;
        obj1.description = "Gather 5 Mystic Crystals";
        obj1.targetId = "mystic_crystal";
        obj1.requiredCount = 5;
        q.objectives.push_back(obj1);

        QuestObjective obj2;
        obj2.type = ObjectiveType::Kill;
        obj2.description = "Channel your power to defeat 10 dogs";
        obj2.targetId = "Dog";
        obj2.requiredCount = 10;
        q.objectives.push_back(obj2);

        q.rewards.xp = 100;
        q.rewards.currency = 50;
        q.rewards.items = {"mystic_staff"};

        quests.push_back(q);
    }

    // Mystic Clan Quest 2: The Arcane Path
    {
        Quest q;
        q.id = "mystic_002_arcane";
        q.title = "The Arcane Path";
        q.description = "Study the ancient arcane arts";
        q.loreText = "The path of the mystic is one of knowledge and power. Study the ancient "
                     "texts and unlock abilities beyond mortal comprehension.";
        q.type = QuestType::MainStory;
        q.requiredLevel = 3;
        q.requiredClan = Clan::Mystic;
        q.prerequisites = {"mystic_001_awakening"};
        q.questGiverId = "mystic_sage_wisdom";

        QuestObjective obj1;
        obj1.type = ObjectiveType::Explore;
        obj1.description = "Study at the Ancient Library";
        obj1.targetId = "ancient_library";
        obj1.requiredCount = 1;
        obj1.waypointLocation = Engine::vec3(100.0f, 20.0f, 250.0f);
        q.objectives.push_back(obj1);

        QuestObjective obj2;
        obj2.type = ObjectiveType::Collect;
        obj2.description = "Collect 3 Arcane Tomes";
        obj2.targetId = "arcane_tome";
        obj2.requiredCount = 3;
        q.objectives.push_back(obj2);

        q.rewards.xp = 250;
        q.rewards.currency = 100;
        q.rewards.abilityUnlock = "arcane_bolt";

        quests.push_back(q);
    }

    // Mystic Clan Quest 3: Elemental Mastery
    {
        Quest q;
        q.id = "mystic_003_elemental";
        q.title = "Elemental Mastery";
        q.description = "Master control over the elements";
        q.loreText = "Fire, ice, lightning - the elements obey those with true power. "
                     "Prove you can wield these forces and bend them to your will.";
        q.type = QuestType::MainStory;
        q.requiredLevel = 5;
        q.requiredClan = Clan::Mystic;
        q.prerequisites = {"mystic_002_arcane"};
        q.questGiverId = "mystic_oracle_luna";

        QuestObjective obj1;
        obj1.type = ObjectiveType::Kill;
        obj1.description = "Defeat 20 enemies using elemental magic";
        obj1.targetId = "Dog";
        obj1.requiredCount = 20;
        q.objectives.push_back(obj1);

        QuestObjective obj2;
        obj2.type = ObjectiveType::Kill;
        obj2.description = "Slay the Elemental Guardian";
        obj2.targetId = "BossDog";
        obj2.requiredCount = 1;
        q.objectives.push_back(obj2);

        q.rewards.xp = 500;
        q.rewards.currency = 250;
        q.rewards.items = {"elemental_orb", "magic_robes"};
        q.rewards.abilityUnlock = "elemental_fury";

        quests.push_back(q);
    }

    // Mystic Clan Quest 4: The Circle of Power
    {
        Quest q;
        q.id = "mystic_004_circle";
        q.title = "The Circle of Power";
        q.description = "Form the Mystic Circle";
        q.loreText = "The greatest magic requires the power of many. Form the Circle of Power "
                     "and channel energies beyond imagination.";
        q.type = QuestType::MainStory;
        q.requiredLevel = 8;
        q.requiredClan = Clan::Mystic;
        q.prerequisites = {"mystic_003_elemental"};
        q.questGiverId = "mystic_oracle_luna";

        QuestObjective obj1;
        obj1.type = ObjectiveType::Talk;
        obj1.description = "Recruit Enchanter Starlight";
        obj1.targetId = "mystic_starlight";
        obj1.requiredCount = 1;
        q.objectives.push_back(obj1);

        QuestObjective obj2;
        obj2.type = ObjectiveType::Talk;
        obj2.description = "Recruit Summoner Void";
        obj2.targetId = "mystic_void";
        obj2.requiredCount = 1;
        q.objectives.push_back(obj2);

        QuestObjective obj3;
        obj3.type = ObjectiveType::Defend;
        obj3.description = "Defend the Mystic Sanctuary";
        obj3.targetId = "mystic_sanctuary";
        obj3.requiredCount = 1;
        q.objectives.push_back(obj3);

        q.rewards.xp = 750;
        q.rewards.currency = 400;
        q.rewards.items = {"mystic_armor_set"};
        q.rewards.territoryUnlock = "arcane_tower";

        quests.push_back(q);
    }

    // Mystic Clan Quest 5: Ascension
    {
        Quest q;
        q.id = "mystic_005_ascension";
        q.title = "Arcane Ascension";
        q.description = "Ascend to become an Archmage";
        q.loreText = "The ultimate power awaits those brave enough to reach for it. "
                     "Transcend mortal limitations and achieve Arcane Ascension!";
        q.type = QuestType::MainStory;
        q.requiredLevel = 10;
        q.requiredClan = Clan::Mystic;
        q.prerequisites = {"mystic_004_circle"};
        q.questGiverId = "mystic_oracle_luna";

        QuestObjective obj1;
        obj1.type = ObjectiveType::Collect;
        obj1.description = "Gather 10 Essence of Ascension";
        obj1.targetId = "essence_ascension";
        obj1.requiredCount = 10;
        q.objectives.push_back(obj1);

        QuestObjective obj2;
        obj2.type = ObjectiveType::Kill;
        obj2.description = "Defeat the Corrupted Archmage";
        obj2.targetId = "BossDog";
        obj2.requiredCount = 1;
        q.objectives.push_back(obj2);

        q.rewards.xp = 1500;
        q.rewards.currency = 1000;
        q.rewards.items = {"staff_of_ascension", "mystic_crown"};
        q.rewards.abilityUnlock = "arcane_ascension";

        quests.push_back(q);
    }

    // ========================================================================
    // SIDE QUESTS (Available to all clans)
    // ========================================================================

    // Side Quest 1: Lost Kitten
    {
        Quest q;
        q.id = "side_001_kitten";
        q.title = "Lost Kitten";
        q.description = "Find and rescue the lost kitten";
        q.loreText = "A young kitten has wandered into dangerous territory. "
                     "Rescue them before the dogs find them!";
        q.type = QuestType::SideQuest;
        q.requiredLevel = 2;
        q.questGiverId = "worried_mother";

        QuestObjective obj1;
        obj1.type = ObjectiveType::Explore;
        obj1.description = "Find the lost kitten";
        obj1.targetId = "lost_kitten_location";
        obj1.requiredCount = 1;
        obj1.waypointLocation = Engine::vec3(50.0f, 0.0f, 100.0f);
        q.objectives.push_back(obj1);

        QuestObjective obj2;
        obj2.type = ObjectiveType::Escort;
        obj2.description = "Escort the kitten home safely";
        obj2.targetId = "kitten_home";
        obj2.requiredCount = 1;
        q.objectives.push_back(obj2);

        q.rewards.xp = 150;
        q.rewards.currency = 75;
        q.rewards.items = {"kitten_toy"};

        quests.push_back(q);
    }

    // Side Quest 2: Supply Run
    {
        Quest q;
        q.id = "side_002_supplies";
        q.title = "Supply Run";
        q.description = "Gather supplies for the settlement";
        q.loreText = "Our supplies are running low. Venture out and gather what we need.";
        q.type = QuestType::SideQuest;
        q.requiredLevel = 1;
        q.questGiverId = "quartermaster_patches";

        QuestObjective obj1;
        obj1.type = ObjectiveType::Collect;
        obj1.description = "Collect 10 food rations";
        obj1.targetId = "food_ration";
        obj1.requiredCount = 10;
        q.objectives.push_back(obj1);

        QuestObjective obj2;
        obj2.type = ObjectiveType::Collect;
        obj2.description = "Collect 5 medicine herbs";
        obj2.targetId = "medicine_herb";
        obj2.requiredCount = 5;
        q.objectives.push_back(obj2);

        q.rewards.xp = 100;
        q.rewards.currency = 100;

        quests.push_back(q);
    }

    // Side Quest 3: The Merchant's Problem
    {
        Quest q;
        q.id = "side_003_merchant";
        q.title = "The Merchant's Problem";
        q.description = "Clear the trade route of dogs";
        q.loreText = "Dogs are blocking the trade route. Clear them out so merchants "
                     "can pass safely.";
        q.type = QuestType::SideQuest;
        q.requiredLevel = 3;
        q.questGiverId = "merchant_felix";

        QuestObjective obj1;
        obj1.type = ObjectiveType::Kill;
        obj1.description = "Clear 15 dogs from the trade route";
        obj1.targetId = "Dog";
        obj1.requiredCount = 15;
        q.objectives.push_back(obj1);

        QuestObjective obj2;
        obj2.type = ObjectiveType::Talk;
        obj2.description = "Report back to Merchant Felix";
        obj2.targetId = "merchant_felix";
        obj2.requiredCount = 1;
        q.objectives.push_back(obj2);

        q.rewards.xp = 200;
        q.rewards.currency = 150;
        q.rewards.items = {"merchant_discount_token"};

        quests.push_back(q);
    }

    // Side Quest 4: Ancient Ruins
    {
        Quest q;
        q.id = "side_004_ruins";
        q.title = "Secrets of the Ruins";
        q.description = "Explore the ancient ruins";
        q.loreText = "Ancient ruins have been discovered nearby. Explore them and "
                     "uncover their secrets.";
        q.type = QuestType::SideQuest;
        q.requiredLevel = 5;
        q.questGiverId = "scholar_whiskers";

        QuestObjective obj1;
        obj1.type = ObjectiveType::Explore;
        obj1.description = "Explore the Ancient Ruins";
        obj1.targetId = "ancient_ruins";
        obj1.requiredCount = 1;
        obj1.waypointLocation = Engine::vec3(180.0f, 10.0f, 180.0f);
        q.objectives.push_back(obj1);

        QuestObjective obj2;
        obj2.type = ObjectiveType::Collect;
        obj2.description = "Collect 3 Ancient Artifacts";
        obj2.targetId = "ancient_artifact";
        obj2.requiredCount = 3;
        q.objectives.push_back(obj2);

        q.rewards.xp = 300;
        q.rewards.currency = 200;
        q.rewards.items = {"ancient_relic"};

        quests.push_back(q);
    }

    // Side Quest 5: The Healer's Request
    {
        Quest q;
        q.id = "side_005_healer";
        q.title = "The Healer's Request";
        q.description = "Gather rare healing ingredients";
        q.loreText = "The healer needs rare ingredients to create powerful remedies. "
                     "Help gather what is needed.";
        q.type = QuestType::SideQuest;
        q.requiredLevel = 4;
        q.questGiverId = "healer_moss";

        QuestObjective obj1;
        obj1.type = ObjectiveType::Collect;
        obj1.description = "Collect 5 Moonpetal Flowers";
        obj1.targetId = "moonpetal";
        obj1.requiredCount = 5;
        q.objectives.push_back(obj1);

        QuestObjective obj2;
        obj2.type = ObjectiveType::Collect;
        obj2.description = "Collect 3 Crystal Dewdrops";
        obj2.targetId = "crystal_dew";
        obj2.requiredCount = 3;
        q.objectives.push_back(obj2);

        q.rewards.xp = 250;
        q.rewards.currency = 125;
        q.rewards.items = {"healing_potion_superior"};

        quests.push_back(q);
    }

    // Side Quest 6: Guard Duty
    {
        Quest q;
        q.id = "side_006_guard";
        q.title = "Guard Duty";
        q.description = "Defend the outpost from dog attacks";
        q.loreText = "The outpost needs defenders. Take up arms and protect our territory!";
        q.type = QuestType::SideQuest;
        q.requiredLevel = 3;
        q.questGiverId = "guard_captain_stripes";

        QuestObjective obj1;
        obj1.type = ObjectiveType::Defend;
        obj1.description = "Defend the outpost";
        obj1.targetId = "outpost";
        obj1.requiredCount = 1;
        q.objectives.push_back(obj1);

        QuestObjective obj2;
        obj2.type = ObjectiveType::Kill;
        obj2.description = "Defeat 20 attacking dogs";
        obj2.targetId = "Dog";
        obj2.requiredCount = 20;
        q.objectives.push_back(obj2);

        q.rewards.xp = 200;
        q.rewards.currency = 150;

        quests.push_back(q);
    }

    // Side Quest 7: Rare Game
    {
        Quest q;
        q.id = "side_007_rare_game";
        q.title = "Rare Game Hunt";
        q.description = "Hunt down rare elite enemies";
        q.loreText = "Elite enemies have been spotted in the area. Track them down for glory!";
        q.type = QuestType::SideQuest;
        q.requiredLevel = 6;
        q.questGiverId = "huntmaster_claw";

        QuestObjective obj1;
        obj1.type = ObjectiveType::Kill;
        obj1.description = "Hunt 3 BigDogs";
        obj1.targetId = "BigDog";
        obj1.requiredCount = 3;
        q.objectives.push_back(obj1);

        QuestObjective obj2;
        obj2.type = ObjectiveType::Kill;
        obj2.description = "Hunt 3 FastDogs";
        obj2.targetId = "FastDog";
        obj2.requiredCount = 3;
        q.objectives.push_back(obj2);

        q.rewards.xp = 400;
        q.rewards.currency = 300;
        q.rewards.items = {"elite_hunter_trophy"};

        quests.push_back(q);
    }

    // Side Quest 8: Message Delivery
    {
        Quest q;
        q.id = "side_008_message";
        q.title = "Urgent Message";
        q.description = "Deliver an urgent message to the northern camp";
        q.loreText = "An urgent message must reach the northern camp. Speed is essential!";
        q.type = QuestType::SideQuest;
        q.requiredLevel = 2;
        q.questGiverId = "messenger_swift";
        q.timeLimit = 300.0f; // 5 minutes

        QuestObjective obj1;
        obj1.type = ObjectiveType::Explore;
        obj1.description = "Reach the northern camp";
        obj1.targetId = "northern_camp";
        obj1.requiredCount = 1;
        obj1.waypointLocation = Engine::vec3(0.0f, 0.0f, 300.0f);
        q.objectives.push_back(obj1);

        QuestObjective obj2;
        obj2.type = ObjectiveType::Talk;
        obj2.description = "Deliver message to Commander Frost";
        obj2.targetId = "commander_frost";
        obj2.requiredCount = 1;
        q.objectives.push_back(obj2);

        q.rewards.xp = 175;
        q.rewards.currency = 125;

        quests.push_back(q);
    }

    // Side Quest 9: Training Grounds
    {
        Quest q;
        q.id = "side_009_training";
        q.title = "Training Grounds";
        q.description = "Complete advanced combat training";
        q.loreText = "Test your skills in the training grounds and prove you're ready "
                     "for tougher challenges.";
        q.type = QuestType::SideQuest;
        q.requiredLevel = 5;
        q.questGiverId = "trainer_sharp";

        QuestObjective obj1;
        obj1.type = ObjectiveType::Kill;
        obj1.description = "Defeat 30 training dummies";
        obj1.targetId = "Dog";
        obj1.requiredCount = 30;
        q.objectives.push_back(obj1);

        QuestObjective obj2;
        obj2.type = ObjectiveType::Survive;
        obj2.description = "Survive 180 seconds of combat trials";
        obj2.targetId = "survive";
        obj2.requiredCount = 180;
        q.objectives.push_back(obj2);

        q.rewards.xp = 350;
        q.rewards.currency = 200;
        q.rewards.items = {"training_certificate"};

        quests.push_back(q);
    }

    // Side Quest 10: The Collector
    {
        Quest q;
        q.id = "side_010_collector";
        q.title = "The Collector";
        q.description = "Help the collector find rare items";
        q.loreText = "A collector seeks rare and exotic items. Find them and earn a reward!";
        q.type = QuestType::SideQuest;
        q.requiredLevel = 7;
        q.questGiverId = "collector_whisker";

        QuestObjective obj1;
        obj1.type = ObjectiveType::Collect;
        obj1.description = "Collect a Rare Gemstone";
        obj1.targetId = "rare_gemstone";
        obj1.requiredCount = 1;
        q.objectives.push_back(obj1);

        QuestObjective obj2;
        obj2.type = ObjectiveType::Collect;
        obj2.description = "Collect a Ancient Coin";
        obj2.targetId = "ancient_coin";
        obj2.requiredCount = 1;
        q.objectives.push_back(obj2);

        QuestObjective obj3;
        obj3.type = ObjectiveType::Collect;
        obj3.description = "Collect a Dragon Scale";
        obj3.targetId = "dragon_scale";
        obj3.requiredCount = 1;
        q.objectives.push_back(obj3);

        q.rewards.xp = 500;
        q.rewards.currency = 500;
        q.rewards.items = {"collector_badge"};

        quests.push_back(q);
    }

    // ========================================================================
    // DAILY QUESTS (Repeatable)
    // ========================================================================

    // Daily Quest 1: Daily Patrol
    {
        Quest q;
        q.id = "daily_001_patrol";
        q.title = "Daily Patrol";
        q.description = "Patrol the territory and eliminate threats";
        q.loreText = "Keep our territory safe with daily patrols.";
        q.type = QuestType::Daily;
        q.requiredLevel = 1;
        q.canRepeat = true;
        q.questGiverId = "patrol_leader";

        QuestObjective obj1;
        obj1.type = ObjectiveType::Kill;
        obj1.description = "Defeat 20 dogs";
        obj1.targetId = "Dog";
        obj1.requiredCount = 20;
        q.objectives.push_back(obj1);

        q.rewards.xp = 100;
        q.rewards.currency = 50;

        quests.push_back(q);
    }

    // Daily Quest 2: Resource Gathering
    {
        Quest q;
        q.id = "daily_002_gather";
        q.title = "Daily Gathering";
        q.description = "Gather resources for the settlement";
        q.loreText = "We always need more resources. Gather what you can.";
        q.type = QuestType::Daily;
        q.requiredLevel = 1;
        q.canRepeat = true;
        q.questGiverId = "resource_manager";

        QuestObjective obj1;
        obj1.type = ObjectiveType::Collect;
        obj1.description = "Collect 15 resources";
        obj1.targetId = "resource";
        obj1.requiredCount = 15;
        q.objectives.push_back(obj1);

        q.rewards.xp = 75;
        q.rewards.currency = 75;

        quests.push_back(q);
    }

    // Daily Quest 3: Elite Hunt
    {
        Quest q;
        q.id = "daily_003_elite";
        q.title = "Elite Hunt";
        q.description = "Hunt down elite enemies";
        q.loreText = "Elite enemies pose a greater threat. Hunt them for bonus rewards.";
        q.type = QuestType::Daily;
        q.requiredLevel = 5;
        q.canRepeat = true;
        q.questGiverId = "elite_hunter";

        QuestObjective obj1;
        obj1.type = ObjectiveType::Kill;
        obj1.description = "Defeat 5 BigDogs";
        obj1.targetId = "BigDog";
        obj1.requiredCount = 5;
        q.objectives.push_back(obj1);

        q.rewards.xp = 200;
        q.rewards.currency = 150;

        quests.push_back(q);
    }

    // Daily Quest 4: Speed Run
    {
        Quest q;
        q.id = "daily_004_speed";
        q.title = "Speed Challenge";
        q.description = "Defeat fast enemies quickly";
        q.loreText = "Test your speed against the fastest foes.";
        q.type = QuestType::Daily;
        q.requiredLevel = 3;
        q.canRepeat = true;
        q.questGiverId = "speed_master";

        QuestObjective obj1;
        obj1.type = ObjectiveType::Kill;
        obj1.description = "Defeat 10 FastDogs";
        obj1.targetId = "FastDog";
        obj1.requiredCount = 10;
        q.objectives.push_back(obj1);

        q.rewards.xp = 150;
        q.rewards.currency = 100;

        quests.push_back(q);
    }

    // Daily Quest 5: Survival Test
    {
        Quest q;
        q.id = "daily_005_survival";
        q.title = "Survival Test";
        q.description = "Survive against waves of enemies";
        q.loreText = "Test your endurance in the survival arena.";
        q.type = QuestType::Daily;
        q.requiredLevel = 4;
        q.canRepeat = true;
        q.questGiverId = "survival_instructor";

        QuestObjective obj1;
        obj1.type = ObjectiveType::Survive;
        obj1.description = "Survive for 240 seconds";
        obj1.targetId = "survive";
        obj1.requiredCount = 240;
        q.objectives.push_back(obj1);

        q.rewards.xp = 175;
        q.rewards.currency = 125;

        quests.push_back(q);
    }

    return quests;
}

} // namespace QuestData
} // namespace CatGame
