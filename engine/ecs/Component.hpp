#pragma once

#include <cstdint>
#include <type_traits>

namespace CatEngine {

using ComponentTypeId = uint32_t;

/**
 * Component type ID generator using compile-time type hashing
 * Each component type gets a unique ID
 */
namespace detail {
    inline ComponentTypeId nextComponentTypeId() {
        static ComponentTypeId id = 0;
        return id++;
    }
}

template<typename T>
inline ComponentTypeId getComponentTypeId() {
    static_assert(std::is_trivially_copyable_v<T> || std::is_move_constructible_v<T>,
                  "Components must be trivially copyable or move constructible");
    static const ComponentTypeId id = detail::nextComponentTypeId();
    return id;
}

/**
 * Concept to check if a type is a valid component
 */
template<typename T>
concept Component = std::is_trivially_copyable_v<T> ||
                    (std::is_move_constructible_v<T> && std::is_destructible_v<T>);

/**
 * Get component type IDs for multiple component types
 */
template<typename... Components>
inline std::vector<ComponentTypeId> getComponentTypeIds() {
    return {getComponentTypeId<Components>()...};
}

} // namespace CatEngine
