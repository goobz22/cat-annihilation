#pragma once

#include "../../engine/ecs/System.hpp"
#include "../../engine/ecs/ECS.hpp"
#include "NPCSystem.hpp"
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>

namespace CatGame {

/**
 * Item category enumeration
 */
enum class ItemCategory {
    Weapon,
    Armor,
    Consumable,
    Material,
    Quest,
    Misc
};

/**
 * Item rarity enumeration
 */
enum class ItemRarity {
    Common,
    Uncommon,
    Rare,
    Epic,
    Legendary
};

/**
 * Player inventory item
 */
struct InventoryItem {
    std::string itemId;
    std::string name;
    std::string description;
    ItemCategory category;
    ItemRarity rarity;
    int quantity = 1;
    int maxStack = 99;
    int sellPrice = 0;
    bool canSell = true;
    bool canDrop = true;
};

/**
 * Transaction record
 */
struct Transaction {
    std::string itemId;
    std::string itemName;
    int quantity;
    int pricePerUnit;
    int totalPrice;
    bool isPurchase;  // true = buy, false = sell
    int timestamp;
};

/**
 * Merchant System
 * Handles buying, selling, inventory, and pricing
 */
class MerchantSystem : public CatEngine::System {
public:
    using TransactionCallback = std::function<void(const Transaction& transaction)>;
    using InventoryChangeCallback = std::function<void(const std::string& itemId, int quantityChange)>;

    explicit MerchantSystem(int priority = 130);
    ~MerchantSystem() override = default;

    void init(CatEngine::ECS* ecs) override;
    void update(float dt) override;
    const char* getName() const override { return "MerchantSystem"; }

    /**
     * Currency management
     */
    void setPlayerCurrency(int amount) { playerCurrency_ = amount; }
    int getPlayerCurrency() const { return playerCurrency_; }
    void addCurrency(int amount);
    void removeCurrency(int amount);
    bool hasCurrency(int amount) const { return playerCurrency_ >= amount; }

    /**
     * Inventory management
     */
    bool addItem(const std::string& itemId, int quantity = 1);
    bool removeItem(const std::string& itemId, int quantity = 1);
    bool hasItem(const std::string& itemId, int quantity = 1) const;
    int getItemQuantity(const std::string& itemId) const;
    const InventoryItem* getInventoryItem(const std::string& itemId) const;
    const std::unordered_map<std::string, InventoryItem>& getInventory() const { return inventory_; }
    void clearInventory();

    /**
     * Buying and selling
     */
    bool buyItem(const ShopItem& item, int quantity = 1);
    bool sellItem(const std::string& itemId, int quantity = 1);
    int calculateBuyPrice(const ShopItem& item, int quantity = 1) const;
    int calculateSellPrice(const std::string& itemId, int quantity = 1) const;

    /**
     * Price modifiers
     */
    void setClanReputationModifier(Clan clan, float modifier) { clanReputationModifiers_[clan] = modifier; }
    float getClanReputationModifier(Clan clan) const;
    void setGlobalPriceModifier(float modifier) { globalPriceModifier_ = modifier; }
    float getGlobalPriceModifier() const { return globalPriceModifier_; }

    /**
     * Transaction history
     */
    const std::vector<Transaction>& getTransactionHistory() const { return transactionHistory_; }
    void clearTransactionHistory();

    /**
     * Callbacks
     */
    void setOnTransaction(TransactionCallback callback) { onTransaction_ = callback; }
    void setOnInventoryChange(InventoryChangeCallback callback) { onInventoryChange_ = callback; }

    /**
     * Item database
     */
    bool loadItemDatabase(const std::string& filepath);
    void registerItem(const InventoryItem& item);
    const InventoryItem* getItemTemplate(const std::string& itemId) const;

    /**
     * Debugging
     */
    void printInventory() const;
    void printTransactionHistory() const;

private:
    /**
     * Record transaction
     */
    void recordTransaction(const std::string& itemId, const std::string& itemName, int quantity, int pricePerUnit, bool isPurchase);

    /**
     * Apply price modifiers
     */
    float calculatePriceModifier(Clan merchantClan) const;

    // Player data
    int playerCurrency_ = 0;
    std::unordered_map<std::string, InventoryItem> inventory_;

    // Item database (templates)
    std::unordered_map<std::string, InventoryItem> itemDatabase_;

    // Price modifiers
    std::unordered_map<Clan, float> clanReputationModifiers_;
    float globalPriceModifier_ = 1.0f;

    // Transaction history
    std::vector<Transaction> transactionHistory_;
    int maxTransactionHistory_ = 100;

    // Callbacks
    TransactionCallback onTransaction_;
    InventoryChangeCallback onInventoryChange_;
};

/**
 * Utility functions
 */
std::string itemCategoryToString(ItemCategory category);
ItemCategory stringToItemCategory(const std::string& str);
std::string itemRarityToString(ItemRarity rarity);
ItemRarity stringToItemRarity(const std::string& str);

} // namespace CatGame
