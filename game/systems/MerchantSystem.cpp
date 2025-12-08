#include "MerchantSystem.hpp"
#include "../../engine/core/Logger.hpp"
#include <fstream>
#include <algorithm>
#include <cmath>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace CatGame {

MerchantSystem::MerchantSystem(int priority)
    : System(priority)
{}

void MerchantSystem::init(CatEngine::ECS* ecs) {
    System::init(ecs);

    // Initialize default clan reputation modifiers (1.0 = no discount)
    clanReputationModifiers_[Clan::MistClan] = 1.0f;
    clanReputationModifiers_[Clan::StormClan] = 1.0f;
    clanReputationModifiers_[Clan::EmberClan] = 1.0f;
    clanReputationModifiers_[Clan::FrostClan] = 1.0f;

    CatEngine::Logger::info("MerchantSystem initialized");
}

void MerchantSystem::update(float dt) {
    // Merchant system is mainly event-driven
}

void MerchantSystem::addCurrency(int amount) {
    if (amount <= 0) {
        return;
    }

    playerCurrency_ += amount;
    CatEngine::Logger::info("Added " + std::to_string(amount) + " currency. Total: " + std::to_string(playerCurrency_));
}

void MerchantSystem::removeCurrency(int amount) {
    if (amount <= 0) {
        return;
    }

    playerCurrency_ = std::max(0, playerCurrency_ - amount);
    CatEngine::Logger::info("Removed " + std::to_string(amount) + " currency. Total: " + std::to_string(playerCurrency_));
}

bool MerchantSystem::addItem(const std::string& itemId, int quantity) {
    if (quantity <= 0) {
        CatEngine::Logger::warning("Invalid quantity: " + std::to_string(quantity));
        return false;
    }

    // Get item template
    const InventoryItem* itemTemplate = getItemTemplate(itemId);
    if (!itemTemplate) {
        CatEngine::Logger::error("Item not found in database: " + itemId);
        return false;
    }

    // Check if item exists in inventory
    auto it = inventory_.find(itemId);
    if (it != inventory_.end()) {
        // Item exists, add to stack
        int newQuantity = it->second.quantity + quantity;
        int maxStack = it->second.maxStack;

        if (newQuantity > maxStack) {
            CatEngine::Logger::warning("Cannot add " + std::to_string(quantity) + " of " + itemTemplate->name +
                                       " (would exceed max stack of " + std::to_string(maxStack) + ")");
            return false;
        }

        it->second.quantity = newQuantity;
    } else {
        // New item, add to inventory
        InventoryItem newItem = *itemTemplate;
        newItem.quantity = quantity;
        inventory_[itemId] = newItem;
    }

    CatEngine::Logger::info("Added " + std::to_string(quantity) + "x " + itemTemplate->name + " to inventory");

    // Trigger callback
    if (onInventoryChange_) {
        onInventoryChange_(itemId, quantity);
    }

    return true;
}

bool MerchantSystem::removeItem(const std::string& itemId, int quantity) {
    if (quantity <= 0) {
        CatEngine::Logger::warning("Invalid quantity: " + std::to_string(quantity));
        return false;
    }

    auto it = inventory_.find(itemId);
    if (it == inventory_.end()) {
        CatEngine::Logger::warning("Item not in inventory: " + itemId);
        return false;
    }

    if (it->second.quantity < quantity) {
        CatEngine::Logger::warning("Not enough items: have " + std::to_string(it->second.quantity) +
                                   ", need " + std::to_string(quantity));
        return false;
    }

    it->second.quantity -= quantity;

    std::string itemName = it->second.name;

    // Remove from inventory if quantity reaches 0
    if (it->second.quantity == 0) {
        inventory_.erase(it);
    }

    CatEngine::Logger::info("Removed " + std::to_string(quantity) + "x " + itemName + " from inventory");

    // Trigger callback
    if (onInventoryChange_) {
        onInventoryChange_(itemId, -quantity);
    }

    return true;
}

bool MerchantSystem::hasItem(const std::string& itemId, int quantity) const {
    return getItemQuantity(itemId) >= quantity;
}

int MerchantSystem::getItemQuantity(const std::string& itemId) const {
    auto it = inventory_.find(itemId);
    return (it != inventory_.end()) ? it->second.quantity : 0;
}

const InventoryItem* MerchantSystem::getInventoryItem(const std::string& itemId) const {
    auto it = inventory_.find(itemId);
    return (it != inventory_.end()) ? &it->second : nullptr;
}

void MerchantSystem::clearInventory() {
    inventory_.clear();
    CatEngine::Logger::info("Cleared inventory");
}

bool MerchantSystem::buyItem(const ShopItem& item, int quantity) {
    if (quantity <= 0) {
        CatEngine::Logger::warning("Invalid quantity: " + std::to_string(quantity));
        return false;
    }

    // Check if item is available
    if (!item.isAvailable) {
        CatEngine::Logger::warning("Item not available: " + item.name);
        return false;
    }

    // Check stock
    if (item.stock >= 0 && quantity > item.stock) {
        CatEngine::Logger::warning("Not enough stock: have " + std::to_string(item.stock) +
                                   ", requested " + std::to_string(quantity));
        return false;
    }

    // Calculate price
    int totalPrice = calculateBuyPrice(item, quantity);

    // Check if player has enough currency
    if (!hasCurrency(totalPrice)) {
        CatEngine::Logger::warning("Not enough currency: have " + std::to_string(playerCurrency_) +
                                   ", need " + std::to_string(totalPrice));
        return false;
    }

    // Add item to inventory
    if (!addItem(item.itemId, quantity)) {
        return false;
    }

    // Deduct currency
    removeCurrency(totalPrice);

    // Record transaction
    recordTransaction(item.itemId, item.name, quantity, item.price, true);

    CatEngine::Logger::info("Purchased " + std::to_string(quantity) + "x " + item.name +
                           " for " + std::to_string(totalPrice) + " currency");

    return true;
}

bool MerchantSystem::sellItem(const std::string& itemId, int quantity) {
    if (quantity <= 0) {
        CatEngine::Logger::warning("Invalid quantity: " + std::to_string(quantity));
        return false;
    }

    // Check if player has the item
    if (!hasItem(itemId, quantity)) {
        CatEngine::Logger::warning("Not enough items to sell");
        return false;
    }

    const InventoryItem* item = getInventoryItem(itemId);
    if (!item) {
        return false;
    }

    // Check if item can be sold
    if (!item->canSell) {
        CatEngine::Logger::warning("Item cannot be sold: " + item->name);
        return false;
    }

    // Calculate sell price
    int totalPrice = calculateSellPrice(itemId, quantity);

    // Remove item from inventory
    if (!removeItem(itemId, quantity)) {
        return false;
    }

    // Add currency
    addCurrency(totalPrice);

    // Record transaction
    recordTransaction(itemId, item->name, quantity, item->sellPrice, false);

    CatEngine::Logger::info("Sold " + std::to_string(quantity) + "x " + item->name +
                           " for " + std::to_string(totalPrice) + " currency");

    return true;
}

int MerchantSystem::calculateBuyPrice(const ShopItem& item, int quantity) const {
    float basePrice = static_cast<float>(item.price * quantity);
    float modifier = globalPriceModifier_;

    // Apply clan reputation modifier if item has clan requirement
    if (item.requiredClan.has_value()) {
        modifier *= getClanReputationModifier(item.requiredClan.value());
    }

    return static_cast<int>(std::ceil(basePrice * modifier));
}

int MerchantSystem::calculateSellPrice(const std::string& itemId, int quantity) const {
    const InventoryItem* item = getInventoryItem(itemId);
    if (!item) {
        return 0;
    }

    float basePrice = static_cast<float>(item->sellPrice * quantity);
    float modifier = globalPriceModifier_ * 0.5f;  // Sell for 50% of buy price (modified by global modifier)

    return static_cast<int>(std::floor(basePrice * modifier));
}

float MerchantSystem::getClanReputationModifier(Clan clan) const {
    auto it = clanReputationModifiers_.find(clan);
    return (it != clanReputationModifiers_.end()) ? it->second : 1.0f;
}

void MerchantSystem::clearTransactionHistory() {
    transactionHistory_.clear();
    CatEngine::Logger::info("Cleared transaction history");
}

bool MerchantSystem::loadItemDatabase(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        CatEngine::Logger::error("Failed to open item database: " + filepath);
        return false;
    }

    try {
        json data = json::parse(file);

        if (!data.contains("items") || !data["items"].is_array()) {
            CatEngine::Logger::error("Invalid item database format: missing 'items' array");
            return false;
        }

        for (const auto& itemJson : data["items"]) {
            InventoryItem item;

            item.itemId = itemJson.value("itemId", "");
            item.name = itemJson.value("name", "");
            item.description = itemJson.value("description", "");
            item.category = stringToItemCategory(itemJson.value("category", "Misc"));
            item.rarity = stringToItemRarity(itemJson.value("rarity", "Common"));
            item.quantity = 1;  // Default quantity
            item.maxStack = itemJson.value("maxStack", 99);
            item.sellPrice = itemJson.value("sellPrice", 0);
            item.canSell = itemJson.value("canSell", true);
            item.canDrop = itemJson.value("canDrop", true);

            itemDatabase_[item.itemId] = item;
        }

        CatEngine::Logger::info("Loaded " + std::to_string(itemDatabase_.size()) + " items from " + filepath);
        return true;

    } catch (const json::exception& e) {
        CatEngine::Logger::error("Failed to parse item database: " + std::string(e.what()));
        return false;
    }
}

void MerchantSystem::registerItem(const InventoryItem& item) {
    itemDatabase_[item.itemId] = item;
    CatEngine::Logger::info("Registered item: " + item.name + " (" + item.itemId + ")");
}

const InventoryItem* MerchantSystem::getItemTemplate(const std::string& itemId) const {
    auto it = itemDatabase_.find(itemId);
    return (it != itemDatabase_.end()) ? &it->second : nullptr;
}

void MerchantSystem::printInventory() const {
    CatEngine::Logger::info("=== Player Inventory ===");
    CatEngine::Logger::info("Currency: " + std::to_string(playerCurrency_));
    CatEngine::Logger::info("Items: " + std::to_string(inventory_.size()));

    for (const auto& pair : inventory_) {
        const InventoryItem& item = pair.second;
        CatEngine::Logger::info("  - " + item.name + " x" + std::to_string(item.quantity) +
                               " (" + itemCategoryToString(item.category) + ", " +
                               itemRarityToString(item.rarity) + ")");
    }
}

void MerchantSystem::printTransactionHistory() const {
    CatEngine::Logger::info("=== Transaction History ===");
    CatEngine::Logger::info("Total transactions: " + std::to_string(transactionHistory_.size()));

    for (const auto& transaction : transactionHistory_) {
        std::string type = transaction.isPurchase ? "BUY" : "SELL";
        CatEngine::Logger::info("  [" + type + "] " + transaction.itemName + " x" +
                               std::to_string(transaction.quantity) + " for " +
                               std::to_string(transaction.totalPrice) + " currency");
    }
}

void MerchantSystem::recordTransaction(const std::string& itemId, const std::string& itemName,
                                       int quantity, int pricePerUnit, bool isPurchase) {
    Transaction transaction;
    transaction.itemId = itemId;
    transaction.itemName = itemName;
    transaction.quantity = quantity;
    transaction.pricePerUnit = pricePerUnit;
    transaction.totalPrice = pricePerUnit * quantity;
    transaction.isPurchase = isPurchase;
    transaction.timestamp = static_cast<int>(transactionHistory_.size());

    transactionHistory_.push_back(transaction);

    // Keep history size limited
    if (static_cast<int>(transactionHistory_.size()) > maxTransactionHistory_) {
        transactionHistory_.erase(transactionHistory_.begin());
    }

    // Trigger callback
    if (onTransaction_) {
        onTransaction_(transaction);
    }
}

float MerchantSystem::calculatePriceModifier(Clan merchantClan) const {
    float modifier = globalPriceModifier_;

    if (merchantClan != Clan::None) {
        modifier *= getClanReputationModifier(merchantClan);
    }

    return modifier;
}

// Utility functions

std::string itemCategoryToString(ItemCategory category) {
    switch (category) {
        case ItemCategory::Weapon: return "Weapon";
        case ItemCategory::Armor: return "Armor";
        case ItemCategory::Consumable: return "Consumable";
        case ItemCategory::Material: return "Material";
        case ItemCategory::Quest: return "Quest";
        case ItemCategory::Misc: return "Misc";
        default: return "Unknown";
    }
}

ItemCategory stringToItemCategory(const std::string& str) {
    if (str == "Weapon") return ItemCategory::Weapon;
    if (str == "Armor") return ItemCategory::Armor;
    if (str == "Consumable") return ItemCategory::Consumable;
    if (str == "Material") return ItemCategory::Material;
    if (str == "Quest") return ItemCategory::Quest;
    return ItemCategory::Misc;
}

std::string itemRarityToString(ItemRarity rarity) {
    switch (rarity) {
        case ItemRarity::Common: return "Common";
        case ItemRarity::Uncommon: return "Uncommon";
        case ItemRarity::Rare: return "Rare";
        case ItemRarity::Epic: return "Epic";
        case ItemRarity::Legendary: return "Legendary";
        default: return "Unknown";
    }
}

ItemRarity stringToItemRarity(const std::string& str) {
    if (str == "Common") return ItemRarity::Common;
    if (str == "Uncommon") return ItemRarity::Uncommon;
    if (str == "Rare") return ItemRarity::Rare;
    if (str == "Epic") return ItemRarity::Epic;
    if (str == "Legendary") return ItemRarity::Legendary;
    return ItemRarity::Common;
}

} // namespace CatGame
