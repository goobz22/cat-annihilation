#pragma once

#include <cstdint>
#include <functional>

namespace CatEngine {

/**
 * Entity uses generational index pattern for safe entity references
 * Lower 32 bits: entity index
 * Upper 32 bits: generation counter
 */
struct Entity {
    uint64_t id;

    constexpr Entity() : id(0) {}
    explicit constexpr Entity(uint64_t id) : id(id) {}
    constexpr Entity(uint32_t index, uint32_t generation)
        : id(static_cast<uint64_t>(generation) << 32 | index) {}

    uint32_t index() const { return static_cast<uint32_t>(id & 0xFFFFFFFF); }
    uint32_t generation() const { return static_cast<uint32_t>(id >> 32); }

    bool operator==(const Entity& other) const { return id == other.id; }
    bool operator!=(const Entity& other) const { return id != other.id; }
    bool operator<(const Entity& other) const { return id < other.id; }

    bool isValid() const { return id != 0; }
};

// Null entity constant
inline constexpr Entity NULL_ENTITY{0};

} // namespace CatEngine

// Hash function for Entity to use in unordered containers
namespace std {
    template<>
    struct hash<CatEngine::Entity> {
        size_t operator()(const CatEngine::Entity& entity) const {
            return hash<uint64_t>()(entity.id);
        }
    };
}
