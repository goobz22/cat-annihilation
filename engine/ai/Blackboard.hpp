#pragma once

#include <any>
#include <string>
#include <unordered_map>
#include <functional>
#include <vector>
#include <typeindex>
#include <memory>

namespace CatEngine {

/**
 * Blackboard scope for hierarchical data storage
 */
enum class BlackboardScope {
    Local,   // Entity-specific data
    Team,    // Team/faction shared data
    Global   // World-wide shared data
};

/**
 * Observer callback for blackboard value changes
 */
using BlackboardObserver = std::function<void(const std::string& key)>;

/**
 * Key-value store for AI state management
 * Supports type-safe storage, retrieval, and observation of changes
 */
class Blackboard {
public:
    Blackboard() = default;
    ~Blackboard() = default;

    /**
     * Set a value in the blackboard
     * @param key The key to store the value under
     * @param value The value to store
     */
    template<typename T>
    void set(const std::string& key, const T& value) {
        data_[key] = value;
        notifyObservers(key);
    }

    /**
     * Get a value from the blackboard
     * @param key The key to retrieve
     * @return Pointer to the value, or nullptr if not found or wrong type
     */
    template<typename T>
    T* get(const std::string& key) {
        auto it = data_.find(key);
        if (it == data_.end()) {
            return nullptr;
        }

        try {
            return std::any_cast<T>(&it->second);
        } catch (const std::bad_any_cast&) {
            return nullptr;
        }
    }

    /**
     * Get a value from the blackboard (const)
     */
    template<typename T>
    const T* get(const std::string& key) const {
        auto it = data_.find(key);
        if (it == data_.end()) {
            return nullptr;
        }

        try {
            return std::any_cast<T>(&it->second);
        } catch (const std::bad_any_cast&) {
            return nullptr;
        }
    }

    /**
     * Get a value with a default if not found
     */
    template<typename T>
    T getOrDefault(const std::string& key, const T& defaultValue) const {
        const T* value = get<T>(key);
        return value ? *value : defaultValue;
    }

    /**
     * Check if a key exists in the blackboard
     */
    bool has(const std::string& key) const {
        return data_.find(key) != data_.end();
    }

    /**
     * Check if a key exists with a specific type
     */
    template<typename T>
    bool hasType(const std::string& key) const {
        auto it = data_.find(key);
        if (it == data_.end()) {
            return false;
        }
        return it->second.type() == typeid(T);
    }

    /**
     * Remove a key from the blackboard
     */
    void remove(const std::string& key) {
        auto it = data_.find(key);
        if (it != data_.end()) {
            data_.erase(it);
            notifyObservers(key);
        }
    }

    /**
     * Clear all values
     */
    void clear() {
        data_.clear();
    }

    /**
     * Get number of entries
     */
    size_t size() const {
        return data_.size();
    }

    /**
     * Check if blackboard is empty
     */
    bool empty() const {
        return data_.empty();
    }

    /**
     * Add observer for value changes
     * @param observer Callback function called when any value changes
     * @return Observer ID for removal
     */
    size_t addObserver(BlackboardObserver observer) {
        size_t id = nextObserverId_++;
        observers_[id] = std::move(observer);
        return id;
    }

    /**
     * Add observer for specific key changes
     */
    size_t addObserver(const std::string& key, BlackboardObserver observer) {
        size_t id = nextObserverId_++;
        keyObservers_[key][id] = std::move(observer);
        return id;
    }

    /**
     * Remove observer
     */
    void removeObserver(size_t observerId) {
        observers_.erase(observerId);

        // Remove from key-specific observers
        for (auto& [key, observers] : keyObservers_) {
            observers.erase(observerId);
        }
    }

    /**
     * Set parent blackboard for hierarchical lookups
     */
    void setParent(Blackboard* parent) {
        parent_ = parent;
    }

    /**
     * Get parent blackboard
     */
    Blackboard* getParent() const {
        return parent_;
    }

    /**
     * Get value from this blackboard or parent chain
     */
    template<typename T>
    const T* getHierarchical(const std::string& key) const {
        const T* value = get<T>(key);
        if (value) {
            return value;
        }

        if (parent_) {
            return parent_->getHierarchical<T>(key);
        }

        return nullptr;
    }

    /**
     * Check if key exists in this blackboard or parent chain
     */
    bool hasHierarchical(const std::string& key) const {
        if (has(key)) {
            return true;
        }
        if (parent_) {
            return parent_->hasHierarchical(key);
        }
        return false;
    }

private:
    void notifyObservers(const std::string& key) {
        // Notify global observers
        for (auto& [id, observer] : observers_) {
            observer(key);
        }

        // Notify key-specific observers
        auto it = keyObservers_.find(key);
        if (it != keyObservers_.end()) {
            for (auto& [id, observer] : it->second) {
                observer(key);
            }
        }
    }

    std::unordered_map<std::string, std::any> data_;
    std::unordered_map<size_t, BlackboardObserver> observers_;
    std::unordered_map<std::string, std::unordered_map<size_t, BlackboardObserver>> keyObservers_;
    size_t nextObserverId_ = 0;
    Blackboard* parent_ = nullptr;
};

/**
 * Scoped blackboard system for managing local, team, and global blackboards
 */
class ScopedBlackboard {
public:
    ScopedBlackboard() {
        local_ = std::make_unique<Blackboard>();
        team_ = std::make_unique<Blackboard>();
        global_ = std::make_unique<Blackboard>();

        // Set up hierarchy: local -> team -> global
        local_->setParent(team_.get());
        team_->setParent(global_.get());
    }

    /**
     * Get blackboard for specific scope
     */
    Blackboard* get(BlackboardScope scope) {
        switch (scope) {
            case BlackboardScope::Local: return local_.get();
            case BlackboardScope::Team: return team_.get();
            case BlackboardScope::Global: return global_.get();
        }
        return nullptr;
    }

    /**
     * Get local blackboard (entity-specific)
     */
    Blackboard* local() { return local_.get(); }
    const Blackboard* local() const { return local_.get(); }

    /**
     * Get team blackboard (shared among team)
     */
    Blackboard* team() { return team_.get(); }
    const Blackboard* team() const { return team_.get(); }

    /**
     * Get global blackboard (world-wide)
     */
    Blackboard* global() { return global_.get(); }
    const Blackboard* global() const { return global_.get(); }

    /**
     * Set value in local blackboard
     */
    template<typename T>
    void set(const std::string& key, const T& value) {
        local_->set(key, value);
    }

    /**
     * Get value with hierarchical lookup (local -> team -> global)
     */
    template<typename T>
    const T* get(const std::string& key) const {
        return local_->getHierarchical<T>(key);
    }

    /**
     * Check if key exists in any scope
     */
    bool has(const std::string& key) const {
        return local_->hasHierarchical(key);
    }

    /**
     * Clear all scopes
     */
    void clear() {
        local_->clear();
        team_->clear();
        global_->clear();
    }

    /**
     * Clear only local scope
     */
    void clearLocal() {
        local_->clear();
    }

private:
    std::unique_ptr<Blackboard> local_;
    std::unique_ptr<Blackboard> team_;
    std::unique_ptr<Blackboard> global_;
};

} // namespace CatEngine
