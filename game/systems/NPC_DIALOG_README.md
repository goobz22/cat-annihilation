# NPC and Dialog System Documentation

## Overview

The NPC and Dialog system provides a complete framework for interactive NPCs with branching conversations, merchant shops, training services, and quest integration for the Cat Annihilation game.

## System Architecture

### Core Components

1. **NPCSystem** - Manages NPC spawning, interactions, and player proximity
2. **DialogSystem** - Handles dialog trees, variable substitution, and conversation flow
3. **MerchantSystem** - Manages inventory, currency, buying/selling, and pricing

## File Structure

```
game/systems/
├── NPCSystem.hpp           # NPC system header
├── NPCSystem.cpp           # NPC system implementation
├── DialogSystem.hpp        # Dialog system header
├── DialogSystem.cpp        # Dialog system implementation
├── MerchantSystem.hpp      # Merchant system header
└── MerchantSystem.cpp      # Merchant system implementation

assets/
├── npcs/
│   └── npcs.json          # NPC definitions and data
├── dialogs/
│   ├── mentor_intro_mist.json
│   ├── mentor_intro_storm.json
│   ├── mentor_intro_ember.json
│   ├── clan_leader_welcome_mist.json
│   ├── merchant_general.json
│   ├── healer_general.json
│   └── quest_dialogs/
│       └── scout_elimination.json
└── config/
    └── items.json         # Item database
```

## Quick Start

### 1. Initialize Systems

```cpp
#include "game/systems/NPCSystem.hpp"
#include "game/systems/DialogSystem.hpp"
#include "game/systems/MerchantSystem.hpp"

// Create systems
auto dialogSystem = std::make_shared<DialogSystem>(140);
auto merchantSystem = std::make_shared<MerchantSystem>(130);
auto npcSystem = std::make_shared<NPCSystem>(150);

// Initialize
dialogSystem->init(ecs);
merchantSystem->init(ecs);
npcSystem->init(ecs);

// Link systems
npcSystem->setDialogSystem(dialogSystem);
npcSystem->setMerchantSystem(merchantSystem);

// Load data
dialogSystem->loadDialogTreesFromDirectory("assets/dialogs");
merchantSystem->loadItemDatabase("assets/config/items.json");
npcSystem->loadNPCsFromFile("assets/npcs/npcs.json");
```

### 2. Set Player Context

```cpp
// Set player entity for distance checks
npcSystem->setPlayer(playerEntity);

// Set player data for dialog/shop filtering
npcSystem->setPlayerClan(Clan::MistClan);
npcSystem->setPlayerLevel(5);

// Set player currency
merchantSystem->setPlayerCurrency(1000);
```

### 3. Start an Interaction

```cpp
// Find closest interactable NPC
auto* npc = npcSystem->getClosestInteractableNPC(playerPosition);

if (npc && npcSystem->canInteract(npc->id, playerPosition)) {
    // Start interaction
    npcSystem->startInteraction(npc->id);

    // Get current dialog node
    const DialogNode* node = npcSystem->getCurrentDialogNode();

    // Display dialog to player UI
    displayDialog(node->speakerName, node->text, node->options);
}
```

### 4. Handle Dialog Options

```cpp
// When player selects an option
void onPlayerSelectOption(int optionIndex) {
    npcSystem->selectDialogOption(optionIndex);

    // Check if dialog continues
    if (npcSystem->isInDialog()) {
        const DialogNode* node = npcSystem->getCurrentDialogNode();
        displayDialog(node->speakerName, node->text, node->options);
    }
}
```

### 5. Open Merchant Shop

```cpp
// Open shop for merchant NPC
npcSystem->openShop(merchantNpcId);

// Get shop inventory
const auto& inventory = npcSystem->getShopInventory();

// Purchase item
for (const auto& item : inventory) {
    if (item.itemId == "health_potion") {
        if (merchantSystem->buyItem(item, 3)) {
            // Purchase successful
        }
    }
}

// Close shop
npcSystem->closeShop();
```

## NPC Types

### Mentor
- Teaches clan-specific abilities
- Provides training options
- Example: Shadowwhisker (MistClan)

### ClanLeader
- Gives main story quests
- Represents clan leadership
- Example: Mistheart (MistClan)

### Merchant
- Sells items and equipment
- Offers clan-specific wares
- Example: Fogpaw (MistClan)

### Healer
- Provides healing services
- Sells healing items
- Example: Snowpaw (FrostClan)

### Trainer
- Offers ability training
- Teaches advanced techniques
- Example: Lightningclaw (StormClan)

### QuestGiver
- Provides side quests
- Example: Mysterious Wanderer

### Villager
- Generic NPCs for atmosphere
- May provide information

## Clans

The game features four main clans:

### MistClan
- **Specialty**: Stealth and agility
- **Abilities**: Stealth Step, Shadow Strike, Mist Cloak
- **Philosophy**: Precision over power

### StormClan
- **Specialty**: Speed and lightning
- **Abilities**: Lightning Dash, Thunder Roar, Storm Fury
- **Philosophy**: Strike like lightning

### EmberClan
- **Specialty**: Fire magic
- **Abilities**: Fireball, Flame Shield, Inferno
- **Philosophy**: Destructive power

### FrostClan
- **Specialty**: Ice magic
- **Abilities**: Ice Shard, Frozen Armor, Blizzard
- **Philosophy**: Control and defense

## Dialog System Features

### Variable Substitution

Dialog text supports variable substitution:

```json
{
  "text": "Welcome, $playerName! Your clan, $clanName, is honored here."
}
```

Set variables in code:
```cpp
dialogSystem->setVariable("playerName", "Shadowpaw");
dialogSystem->setVariable("clanName", "MistClan");
```

### Conditional Options

Options can have requirements:

```json
{
  "text": "Show me clan-exclusive items",
  "responseId": "clan_items",
  "requiredClan": "MistClan",
  "requiredLevel": 5
}
```

### Auto-Advance Nodes

For cinematic sequences:

```json
{
  "id": "epic_moment",
  "text": "The ancient power awakens...",
  "autoAdvance": true,
  "autoAdvanceDelay": 3.0,
  "nextNode": "aftermath"
}
```

## Creating Custom NPCs

### Define NPC in JSON

```json
{
  "id": "custom_npc",
  "name": "Custom NPC",
  "type": "Merchant",
  "clan": "MistClan",
  "position": [10.0, 0.5, 15.0],
  "interactionRadius": 3.5,
  "modelPath": "models/cats/custom.glb",
  "dialogTreeId": "custom_dialog",
  "shopInventory": [
    {
      "itemId": "special_item",
      "name": "Special Item",
      "description": "A unique item",
      "price": 100,
      "stock": 1
    }
  ]
}
```

### Create Dialog Tree

```json
{
  "id": "custom_dialog",
  "name": "Custom Dialog",
  "startNode": "greeting",
  "nodes": [
    {
      "id": "greeting",
      "speaker": "Custom NPC",
      "text": "Hello traveler!",
      "options": [
        {
          "text": "Hello!",
          "responseId": "friendly"
        }
      ]
    },
    {
      "id": "friendly",
      "speaker": "Custom NPC",
      "text": "Nice to meet you!",
      "options": [
        {
          "text": "Goodbye",
          "responseId": ""
        }
      ]
    }
  ]
}
```

## Merchant System

### Item Categories
- **Weapon**: Swords, bows, magical weapons
- **Armor**: Defensive equipment
- **Consumable**: Potions, food, buffs
- **Material**: Crafting ingredients
- **Quest**: Quest-specific items
- **Misc**: Everything else

### Item Rarity
- **Common**: Basic items
- **Uncommon**: Improved items
- **Rare**: Powerful items
- **Epic**: Very powerful items
- **Legendary**: Unique, game-changing items

### Price Modifiers

```cpp
// Clan reputation affects prices
merchantSystem->setClanReputationModifier(Clan::MistClan, 0.8f); // 20% discount

// Global price modifier
merchantSystem->setGlobalPriceModifier(1.2f); // 20% price increase
```

### Inventory Management

```cpp
// Add items
merchantSystem->addItem("health_potion", 5);

// Remove items
merchantSystem->removeItem("health_potion", 2);

// Check inventory
if (merchantSystem->hasItem("health_potion", 3)) {
    // Player has at least 3 health potions
}

// Get item quantity
int quantity = merchantSystem->getItemQuantity("health_potion");
```

## Best Practices

### 1. Always Check Player Distance

```cpp
if (npcSystem->canInteract(npcId, playerPosition)) {
    // Safe to interact
}
```

### 2. Handle Dialog State

```cpp
// Always check if still in dialog after option selection
npcSystem->selectDialogOption(index);
if (!npcSystem->isInDialog()) {
    // Dialog ended, clean up UI
}
```

### 3. Validate Purchases

```cpp
// Check if player can afford item
int price = merchantSystem->calculateBuyPrice(item, quantity);
if (merchantSystem->hasCurrency(price)) {
    merchantSystem->buyItem(item, quantity);
}
```

### 4. Use Callbacks

```cpp
// Track interactions
npcSystem->setOnInteractionStart([](const std::string& npcId) {
    Logger::info("Started interaction with: " + npcId);
});

// Track purchases
merchantSystem->setOnTransaction([](const Transaction& t) {
    if (t.isPurchase) {
        Logger::info("Purchased: " + t.itemName);
    }
});
```

## Integration Example

Complete example of NPC interaction flow:

```cpp
class GameNPCController {
public:
    void update(float dt) {
        // Update systems
        npcSystem->update(dt);
        dialogSystem->update(dt);
        merchantSystem->update(dt);

        // Check for nearby NPCs
        auto* nearbyNPC = npcSystem->getClosestInteractableNPC(playerPos);

        if (nearbyNPC) {
            showInteractionPrompt(nearbyNPC->name);

            // Player presses interact key
            if (inputSystem->isActionPressed(Action::Interact)) {
                handleInteraction(nearbyNPC->id);
            }
        }
    }

    void handleInteraction(const std::string& npcId) {
        npcSystem->startInteraction(npcId);

        auto* npc = npcSystem->getNPC(npcId);
        auto* node = npcSystem->getCurrentDialogNode();

        if (node) {
            // Show dialog UI
            showDialogUI(node->speakerName, node->text, node->options);
        }
    }

    void onDialogOptionSelected(int optionIndex) {
        npcSystem->selectDialogOption(optionIndex);

        if (npcSystem->isInDialog()) {
            auto* node = npcSystem->getCurrentDialogNode();
            updateDialogUI(node->speakerName, node->text, node->options);
        } else {
            hideDialogUI();
        }
    }
};
```

## Debugging

### Print Dialog Tree

```cpp
dialogSystem->printDialogTree("mentor_intro_mist");
```

### Print Current Node

```cpp
dialogSystem->printCurrentNode();
```

### Print Inventory

```cpp
merchantSystem->printInventory();
```

### Print Transaction History

```cpp
merchantSystem->printTransactionHistory();
```

## Performance Considerations

1. **NPC Culling**: Only update NPCs near the player
2. **Dialog Caching**: Dialog trees are loaded once and cached
3. **Inventory Optimization**: Use hash maps for O(1) item lookup
4. **Distance Checks**: Use squared distance to avoid sqrt()

## Future Enhancements

- **Quest Integration**: Full quest system integration
- **Relationship System**: Track player relationship with each NPC
- **Dynamic Pricing**: Prices that change based on supply/demand
- **Voice Acting**: Support for audio clips in dialog nodes
- **Localization**: Multi-language support for dialog
- **Dialog History**: Full conversation log for players
- **Bartering**: Allow negotiation on prices

## Troubleshooting

### NPCs Not Spawning
- Check JSON file path
- Verify NPC data format
- Ensure models exist at specified paths

### Dialog Not Loading
- Validate JSON syntax
- Check dialog tree IDs match NPC definitions
- Ensure start node exists

### Items Not Purchasing
- Verify player has enough currency
- Check item availability and stock
- Ensure item exists in database

## License

Part of Cat Annihilation game engine. All rights reserved.
