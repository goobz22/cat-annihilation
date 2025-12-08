#pragma once

#include "Entity.hpp"
#include "Component.hpp"
#include "ComponentPool.hpp"
#include <vector>
#include <tuple>
#include <unordered_map>
#include <memory>

namespace CatEngine {

/**
 * Query result iterator for entities with specific components
 * Provides cache-friendly iteration over matching entities
 */
template<typename... Components>
class QueryIterator {
public:
    using ValueType = std::tuple<Entity, Components*...>;

    QueryIterator(const std::vector<Entity>& entities,
                  std::tuple<ComponentPool<Components>*...> pools,
                  size_t index)
        : entities_(entities), pools_(pools), index_(index) {
        // Skip to first valid entity
        findNext();
    }

    bool operator!=(const QueryIterator& other) const {
        return index_ != other.index_;
    }

    QueryIterator& operator++() {
        ++index_;
        findNext();
        return *this;
    }

    ValueType operator*() const {
        Entity entity = entities_[index_];
        return std::tuple_cat(
            std::make_tuple(entity),
            getComponents(entity, std::index_sequence_for<Components...>{})
        );
    }

private:
    void findNext() {
        while (index_ < entities_.size()) {
            Entity entity = entities_[index_];
            if (hasAllComponents(entity, std::index_sequence_for<Components...>{})) {
                return;
            }
            ++index_;
        }
    }

    template<size_t... Is>
    bool hasAllComponents(Entity entity, std::index_sequence<Is...>) const {
        return (std::get<Is>(pools_)->has(entity) && ...);
    }

    template<size_t... Is>
    std::tuple<Components*...> getComponents(Entity entity, std::index_sequence<Is...>) const {
        return std::make_tuple(std::get<Is>(pools_)->get(entity)...);
    }

    const std::vector<Entity>& entities_;
    std::tuple<ComponentPool<Components>*...> pools_;
    size_t index_;
};

/**
 * Query view for entities with specific components
 * Usage: for (auto [entity, transform, velocity] : query.view()) { ... }
 */
template<typename... Components>
class QueryView {
public:
    QueryView(const std::vector<Entity>& entities,
              std::tuple<ComponentPool<Components>*...> pools)
        : entities_(entities), pools_(pools) {}

    QueryIterator<Components...> begin() const {
        return QueryIterator<Components...>(entities_, pools_, 0);
    }

    QueryIterator<Components...> end() const {
        return QueryIterator<Components...>(entities_, pools_, entities_.size());
    }

private:
    const std::vector<Entity>& entities_;
    std::tuple<ComponentPool<Components>*...> pools_;
};

/**
 * Query builder for finding entities with specific components
 * Supports efficient iteration over entities that have ALL specified components
 */
template<typename... Components>
class Query {
public:
    Query(std::tuple<ComponentPool<Components>*...> pools)
        : pools_(pools) {
        // Find the smallest component pool to minimize iterations
        findSmallestPool();
    }

    /**
     * Get a view for iterating over matching entities
     * Usage: for (auto [entity, comp1, comp2] : query.view()) { ... }
     */
    QueryView<Components...> view() const {
        if constexpr (sizeof...(Components) == 0) {
            static std::vector<Entity> empty;
            return QueryView<Components...>(empty, pools_);
        } else {
            return QueryView<Components...>(getSmallestEntities(), pools_);
        }
    }

    /**
     * Execute a function for each matching entity
     * @param func Function taking (Entity, Components*...)
     */
    template<typename Func>
    void forEach(Func&& func) const {
        for (auto tuple : view()) {
            std::apply(std::forward<Func>(func), tuple);
        }
    }

    /**
     * Count entities matching this query
     */
    size_t count() const {
        if constexpr (sizeof...(Components) == 0) {
            return 0;
        }

        size_t result = 0;
        for ([[maybe_unused]] auto _ : view()) {
            ++result;
        }
        return result;
    }

    /**
     * Check if any entities match this query
     */
    bool empty() const {
        return view().begin() == view().end();
    }

private:
    void findSmallestPool() {
        if constexpr (sizeof...(Components) > 0) {
            size_t minSize = SIZE_MAX;
            smallestPoolIndex_ = 0;

            findSmallestPoolHelper<0, Components...>(minSize);
        }
    }

    template<size_t Index, typename First, typename... Rest>
    void findSmallestPoolHelper(size_t& minSize) {
        auto* pool = std::get<Index>(pools_);
        size_t size = pool ? pool->size() : 0;

        if (size < minSize) {
            minSize = size;
            smallestPoolIndex_ = Index;
        }

        if constexpr (sizeof...(Rest) > 0) {
            findSmallestPoolHelper<Index + 1, Rest...>(minSize);
        }
    }

    const std::vector<Entity>& getSmallestEntities() const {
        return getEntitiesAtIndex(smallestPoolIndex_, std::index_sequence_for<Components...>{});
    }

    template<size_t... Is>
    const std::vector<Entity>& getEntitiesAtIndex(size_t index, std::index_sequence<Is...>) const {
        const std::vector<Entity>* result = nullptr;
        ((Is == index ? (result = &std::get<Is>(pools_)->getEntities(), true) : false), ...);
        static std::vector<Entity> empty;
        return result ? *result : empty;
    }

    std::tuple<ComponentPool<Components>*...> pools_;
    size_t smallestPoolIndex_ = 0;
};

/**
 * Helper to create queries
 */
template<typename... Components>
Query<Components...> makeQuery(ComponentPool<Components>*... pools) {
    return Query<Components...>(std::make_tuple(pools...));
}

} // namespace CatEngine
