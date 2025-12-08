#include "damage_numbers.hpp"
#include <random>
#include <sstream>
#include <iomanip>
#include <algorithm>

namespace CatGame {

// Random number generator for offset
static std::random_device rd;
static std::mt19937 gen(rd());

void DamageNumberManager::spawnDamageNumber(
    const Engine::vec3& position,
    float damage,
    DamageType type,
    bool isCrit
) {
    DamageNumber number;
    number.position = addRandomOffset(position);
    number.velocity = Engine::vec3(0.0f, 2.0f, 0.0f);  // Float upward
    number.text = formatDamage(damage);
    number.type = DamageNumberColor::fromDamageType(type, isCrit);
    number.color = DamageNumberColor::forType(number.type);
    number.lifetime = 0.0f;
    number.maxLifetime = isCrit ? 2.0f : 1.5f;  // Crits stay longer
    number.scale = isCrit ? 1.5f : 1.0f;        // Crits are bigger
    number.fadeStartTime = isCrit ? 1.5f : 1.0f;
    number.active = true;

    // Add some horizontal velocity for spread
    std::uniform_real_distribution<float> dist(-0.5f, 0.5f);
    number.velocity.x = dist(gen);

    damageNumbers_.push_back(number);
}

void DamageNumberManager::spawnCustomNumber(
    const Engine::vec3& position,
    const std::string& text,
    DamageNumberType type,
    float scale
) {
    DamageNumber number;
    number.position = addRandomOffset(position);
    number.velocity = Engine::vec3(0.0f, 2.0f, 0.0f);
    number.text = text;
    number.type = type;
    number.color = DamageNumberColor::forType(type);
    number.lifetime = 0.0f;
    number.maxLifetime = 1.5f;
    number.scale = scale;
    number.fadeStartTime = 1.0f;
    number.active = true;

    // Add horizontal velocity
    std::uniform_real_distribution<float> dist(-0.5f, 0.5f);
    number.velocity.x = dist(gen);

    damageNumbers_.push_back(number);
}

void DamageNumberManager::spawnHealNumber(const Engine::vec3& position, float healAmount) {
    DamageNumber number;
    number.position = addRandomOffset(position);
    number.velocity = Engine::vec3(0.0f, 1.5f, 0.0f);
    number.text = "+" + formatDamage(healAmount);
    number.type = DamageNumberType::Heal;
    number.color = DamageNumberColor::forType(DamageNumberType::Heal);
    number.lifetime = 0.0f;
    number.maxLifetime = 1.5f;
    number.scale = 1.0f;
    number.fadeStartTime = 1.0f;
    number.active = true;

    damageNumbers_.push_back(number);
}

void DamageNumberManager::spawnMissIndicator(const Engine::vec3& position) {
    DamageNumber number;
    number.position = addRandomOffset(position);
    number.velocity = Engine::vec3(0.0f, 1.0f, 0.0f);
    number.text = "MISS";
    number.type = DamageNumberType::Miss;
    number.color = DamageNumberColor::forType(DamageNumberType::Miss);
    number.lifetime = 0.0f;
    number.maxLifetime = 1.0f;
    number.scale = 1.2f;
    number.fadeStartTime = 0.7f;
    number.active = true;

    damageNumbers_.push_back(number);
}

void DamageNumberManager::spawnBlockIndicator(
    const Engine::vec3& position,
    float damageBlocked,
    bool isPerfectBlock
) {
    DamageNumber number;
    number.position = addRandomOffset(position);
    number.velocity = Engine::vec3(0.0f, 1.5f, 0.0f);

    if (isPerfectBlock) {
        number.text = "PERFECT!";
        number.scale = 1.5f;
        number.maxLifetime = 2.0f;
        number.fadeStartTime = 1.5f;
        // Special gold color for perfect block
        number.color = DamageNumberColor(1.0f, 0.85f, 0.0f, 1.0f);
    } else {
        number.text = "BLOCK -" + formatDamage(damageBlocked);
        number.scale = 1.0f;
        number.maxLifetime = 1.5f;
        number.fadeStartTime = 1.0f;
        number.color = DamageNumberColor::forType(DamageNumberType::Block);
    }

    number.type = DamageNumberType::Block;
    number.lifetime = 0.0f;
    number.active = true;

    damageNumbers_.push_back(number);
}

void DamageNumberManager::spawnComboCounter(const Engine::vec3& position, int comboCount) {
    DamageNumber number;
    number.position = addRandomOffset(position);
    number.velocity = Engine::vec3(0.0f, 2.5f, 0.0f);  // Float faster for combo

    std::stringstream ss;
    ss << "x" << comboCount << " COMBO!";
    number.text = ss.str();

    number.type = DamageNumberType::Critical;  // Use critical (yellow) color
    number.color = DamageNumberColor::forType(DamageNumberType::Critical);
    number.lifetime = 0.0f;
    number.maxLifetime = 2.0f;
    number.scale = 1.5f + (comboCount * 0.1f);  // Bigger for higher combos
    number.fadeStartTime = 1.5f;
    number.active = true;

    damageNumbers_.push_back(number);
}

void DamageNumberManager::update(float dt) {
    // Update all numbers
    for (auto& number : damageNumbers_) {
        number.update(dt);
    }

    // Remove inactive numbers
    damageNumbers_.erase(
        std::remove_if(
            damageNumbers_.begin(),
            damageNumbers_.end(),
            [](const DamageNumber& num) { return !num.active; }
        ),
        damageNumbers_.end()
    );
}

void DamageNumberManager::clear() {
    damageNumbers_.clear();
}

Engine::vec3 DamageNumberManager::addRandomOffset(const Engine::vec3& position) {
    std::uniform_real_distribution<float> dist(-0.5f, 0.5f);

    return Engine::vec3(
        position.x + dist(gen),
        position.y + 1.5f,  // Spawn above the entity
        position.z + dist(gen)
    );
}

std::string DamageNumberManager::formatDamage(float damage) {
    std::stringstream ss;

    int roundedDamage = static_cast<int>(damage + 0.5f);

    if (roundedDamage >= 1000) {
        // Format as K (thousands)
        float thousands = roundedDamage / 1000.0f;
        ss << std::fixed << std::setprecision(1) << thousands << "K";
    } else {
        ss << roundedDamage;
    }

    return ss.str();
}

} // namespace CatGame
