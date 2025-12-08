#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>
#include <utility>
#include <limits>
#include <stdexcept>

namespace Engine {

/**
 * SparseSet - Efficient set with O(1) operations
 *
 * Features:
 * - O(1) insert, remove, and contains operations
 * - Dense iteration (only iterates over existing elements)
 * - Sparse lookup using indirect indexing
 * - Perfect for ECS systems where entity IDs are used as keys
 * - Memory efficient for sparse domains
 *
 * @tparam T Element type (should be an integer or convertible to size_t)
 */
template<typename T = uint32_t>
class SparseSet {
private:
    std::vector<T> dense_;      // Dense array of elements (for iteration)
    std::vector<size_t> sparse_; // Sparse array mapping element to dense index
    size_t max_value_;          // Maximum element value seen

    [[nodiscard]] size_t to_index(T value) const noexcept {
        if constexpr (std::is_integral_v<T>) {
            return static_cast<size_t>(value);
        } else {
            return static_cast<size_t>(value);
        }
    }

    void ensure_capacity(size_t idx) {
        if (idx >= sparse_.size()) {
            size_t new_size = sparse_.size();
            if (new_size == 0) new_size = 16;

            while (new_size <= idx) {
                new_size *= 2;
            }

            sparse_.resize(new_size, std::numeric_limits<size_t>::max());
        }
    }

public:
    using value_type = T;
    using size_type = size_t;
    using reference = T&;
    using const_reference = const T&;
    using iterator = typename std::vector<T>::iterator;
    using const_iterator = typename std::vector<T>::const_iterator;

    // Constructors
    SparseSet() : max_value_(0) {}

    explicit SparseSet(size_type reserve_size)
        : max_value_(0)
    {
        dense_.reserve(reserve_size);
        sparse_.reserve(reserve_size);
    }

    // Capacity
    [[nodiscard]] bool empty() const noexcept { return dense_.empty(); }
    [[nodiscard]] size_type size() const noexcept { return dense_.size(); }
    [[nodiscard]] size_type capacity() const noexcept { return dense_.capacity(); }

    void reserve(size_type new_capacity) {
        dense_.reserve(new_capacity);
    }

    void clear() {
        dense_.clear();
        // Don't shrink sparse array, just invalidate all entries
        std::fill(sparse_.begin(), sparse_.end(), std::numeric_limits<size_t>::max());
    }

    // Modifiers
    bool insert(T value) {
        size_t idx = to_index(value);
        ensure_capacity(idx);

        // Check if already exists
        if (sparse_[idx] < dense_.size() && dense_[sparse_[idx]] == value) {
            return false;
        }

        // Add to dense array
        sparse_[idx] = dense_.size();
        dense_.push_back(value);

        if (idx > max_value_) {
            max_value_ = idx;
        }

        return true;
    }

    template<typename Iterator>
    void insert(Iterator first, Iterator last) {
        for (auto it = first; it != last; ++it) {
            insert(*it);
        }
    }

    bool erase(T value) {
        size_t idx = to_index(value);

        if (idx >= sparse_.size()) {
            return false;
        }

        size_t dense_idx = sparse_[idx];

        // Check if value exists
        if (dense_idx >= dense_.size() || dense_[dense_idx] != value) {
            return false;
        }

        // Swap with last element
        T last_value = dense_.back();
        dense_[dense_idx] = last_value;
        sparse_[to_index(last_value)] = dense_idx;

        // Remove last element
        dense_.pop_back();
        sparse_[idx] = std::numeric_limits<size_t>::max();

        return true;
    }

    iterator erase(iterator pos) {
        if (pos == dense_.end()) {
            return dense_.end();
        }

        T value = *pos;
        size_t dense_idx = pos - dense_.begin();

        // Swap with last element
        T last_value = dense_.back();
        dense_[dense_idx] = last_value;
        sparse_[to_index(last_value)] = dense_idx;
        sparse_[to_index(value)] = std::numeric_limits<size_t>::max();

        // Remove last element
        dense_.pop_back();

        return dense_.begin() + dense_idx;
    }

    // Lookup
    [[nodiscard]] bool contains(T value) const noexcept {
        size_t idx = to_index(value);

        if (idx >= sparse_.size()) {
            return false;
        }

        size_t dense_idx = sparse_[idx];
        return dense_idx < dense_.size() && dense_[dense_idx] == value;
    }

    [[nodiscard]] size_type count(T value) const noexcept {
        return contains(value) ? 1 : 0;
    }

    [[nodiscard]] iterator find(T value) {
        size_t idx = to_index(value);

        if (idx >= sparse_.size()) {
            return dense_.end();
        }

        size_t dense_idx = sparse_[idx];

        if (dense_idx < dense_.size() && dense_[dense_idx] == value) {
            return dense_.begin() + dense_idx;
        }

        return dense_.end();
    }

    [[nodiscard]] const_iterator find(T value) const {
        size_t idx = to_index(value);

        if (idx >= sparse_.size()) {
            return dense_.end();
        }

        size_t dense_idx = sparse_[idx];

        if (dense_idx < dense_.size() && dense_[dense_idx] == value) {
            return dense_.begin() + dense_idx;
        }

        return dense_.end();
    }

    // Iterators (iterate over dense array)
    [[nodiscard]] iterator begin() noexcept { return dense_.begin(); }
    [[nodiscard]] const_iterator begin() const noexcept { return dense_.begin(); }
    [[nodiscard]] const_iterator cbegin() const noexcept { return dense_.cbegin(); }

    [[nodiscard]] iterator end() noexcept { return dense_.end(); }
    [[nodiscard]] const_iterator end() const noexcept { return dense_.end(); }
    [[nodiscard]] const_iterator cend() const noexcept { return dense_.cend(); }

    // Element access
    [[nodiscard]] reference operator[](size_type pos) { return dense_[pos]; }
    [[nodiscard]] const_reference operator[](size_type pos) const { return dense_[pos]; }

    [[nodiscard]] reference at(size_type pos) {
        if (pos >= dense_.size()) {
            throw std::out_of_range("SparseSet::at");
        }
        return dense_[pos];
    }

    [[nodiscard]] const_reference at(size_type pos) const {
        if (pos >= dense_.size()) {
            throw std::out_of_range("SparseSet::at");
        }
        return dense_[pos];
    }

    [[nodiscard]] reference front() { return dense_.front(); }
    [[nodiscard]] const_reference front() const { return dense_.front(); }
    [[nodiscard]] reference back() { return dense_.back(); }
    [[nodiscard]] const_reference back() const { return dense_.back(); }

    // Direct access to dense array
    [[nodiscard]] T* data() noexcept { return dense_.data(); }
    [[nodiscard]] const T* data() const noexcept { return dense_.data(); }

    // Set operations
    void swap(SparseSet& other) noexcept {
        using std::swap;
        swap(dense_, other.dense_);
        swap(sparse_, other.sparse_);
        swap(max_value_, other.max_value_);
    }

    /**
     * Get the index in the dense array for a given value
     * @return Index in dense array, or size() if not found
     */
    [[nodiscard]] size_type index_of(T value) const noexcept {
        size_t idx = to_index(value);

        if (idx >= sparse_.size()) {
            return dense_.size();
        }

        size_t dense_idx = sparse_[idx];

        if (dense_idx < dense_.size() && dense_[dense_idx] == value) {
            return dense_idx;
        }

        return dense_.size();
    }

    /**
     * Remove all elements that satisfy the predicate
     * @param pred Unary predicate which returns true for elements to remove
     * @return Number of elements removed
     */
    template<typename Predicate>
    size_type remove_if(Predicate pred) {
        size_type original_size = dense_.size();

        for (size_type i = 0; i < dense_.size(); ) {
            if (pred(dense_[i])) {
                erase(dense_.begin() + i);
            } else {
                ++i;
            }
        }

        return original_size - dense_.size();
    }

    /**
     * Check if this set intersects with another set
     */
    [[nodiscard]] bool intersects(const SparseSet& other) const {
        const SparseSet& smaller = size() < other.size() ? *this : other;
        const SparseSet& larger = size() < other.size() ? other : *this;

        for (T value : smaller) {
            if (larger.contains(value)) {
                return true;
            }
        }

        return false;
    }

    /**
     * Perform intersection with another set (modifies this set)
     */
    void intersection(const SparseSet& other) {
        for (size_type i = 0; i < dense_.size(); ) {
            if (!other.contains(dense_[i])) {
                erase(dense_.begin() + i);
            } else {
                ++i;
            }
        }
    }

    /**
     * Perform union with another set (modifies this set)
     */
    void union_with(const SparseSet& other) {
        for (T value : other) {
            insert(value);
        }
    }

    /**
     * Perform difference with another set (modifies this set)
     * Removes all elements that are in the other set
     */
    void difference(const SparseSet& other) {
        for (T value : other) {
            erase(value);
        }
    }
};

/**
 * SparseSetWithData - Sparse set that associates data with each element
 *
 * Perfect for ECS component storage where entity IDs are sparse but
 * you need to store component data densely.
 *
 * @tparam Key Element type (entity ID)
 * @tparam Value Associated data type (component data)
 */
template<typename Key = uint32_t, typename Value = void>
class SparseSetWithData {
private:
    std::vector<Key> dense_keys_;
    std::vector<Value> dense_values_;
    std::vector<size_t> sparse_;

    [[nodiscard]] size_t to_index(Key key) const noexcept {
        if constexpr (std::is_integral_v<Key>) {
            return static_cast<size_t>(key);
        } else {
            return static_cast<size_t>(key);
        }
    }

    void ensure_capacity(size_t idx) {
        if (idx >= sparse_.size()) {
            size_t new_size = sparse_.size();
            if (new_size == 0) new_size = 16;

            while (new_size <= idx) {
                new_size *= 2;
            }

            sparse_.resize(new_size, std::numeric_limits<size_t>::max());
        }
    }

public:
    using key_type = Key;
    using mapped_type = Value;
    using size_type = size_t;

    // Constructors
    SparseSetWithData() = default;

    explicit SparseSetWithData(size_type reserve_size) {
        dense_keys_.reserve(reserve_size);
        dense_values_.reserve(reserve_size);
        sparse_.reserve(reserve_size);
    }

    // Capacity
    [[nodiscard]] bool empty() const noexcept { return dense_keys_.empty(); }
    [[nodiscard]] size_type size() const noexcept { return dense_keys_.size(); }

    void clear() {
        dense_keys_.clear();
        dense_values_.clear();
        std::fill(sparse_.begin(), sparse_.end(), std::numeric_limits<size_t>::max());
    }

    // Modifiers
    template<typename... Args>
    bool emplace(Key key, Args&&... args) {
        size_t idx = to_index(key);
        ensure_capacity(idx);

        if (sparse_[idx] < dense_keys_.size() && dense_keys_[sparse_[idx]] == key) {
            return false;
        }

        sparse_[idx] = dense_keys_.size();
        dense_keys_.push_back(key);
        dense_values_.emplace_back(std::forward<Args>(args)...);

        return true;
    }

    bool insert(Key key, const Value& value) {
        return emplace(key, value);
    }

    bool insert(Key key, Value&& value) {
        return emplace(key, std::move(value));
    }

    bool erase(Key key) {
        size_t idx = to_index(key);

        if (idx >= sparse_.size()) {
            return false;
        }

        size_t dense_idx = sparse_[idx];

        if (dense_idx >= dense_keys_.size() || dense_keys_[dense_idx] != key) {
            return false;
        }

        Key last_key = dense_keys_.back();
        dense_keys_[dense_idx] = last_key;
        dense_values_[dense_idx] = std::move(dense_values_.back());
        sparse_[to_index(last_key)] = dense_idx;

        dense_keys_.pop_back();
        dense_values_.pop_back();
        sparse_[idx] = std::numeric_limits<size_t>::max();

        return true;
    }

    // Lookup
    [[nodiscard]] bool contains(Key key) const noexcept {
        size_t idx = to_index(key);

        if (idx >= sparse_.size()) {
            return false;
        }

        size_t dense_idx = sparse_[idx];
        return dense_idx < dense_keys_.size() && dense_keys_[dense_idx] == key;
    }

    [[nodiscard]] Value* get(Key key) noexcept {
        size_t idx = to_index(key);

        if (idx >= sparse_.size()) {
            return nullptr;
        }

        size_t dense_idx = sparse_[idx];

        if (dense_idx < dense_keys_.size() && dense_keys_[dense_idx] == key) {
            return &dense_values_[dense_idx];
        }

        return nullptr;
    }

    [[nodiscard]] const Value* get(Key key) const noexcept {
        size_t idx = to_index(key);

        if (idx >= sparse_.size()) {
            return nullptr;
        }

        size_t dense_idx = sparse_[idx];

        if (dense_idx < dense_keys_.size() && dense_keys_[dense_idx] == key) {
            return &dense_values_[dense_idx];
        }

        return nullptr;
    }

    [[nodiscard]] Value& at(Key key) {
        Value* val = get(key);
        if (!val) {
            throw std::out_of_range("SparseSetWithData::at");
        }
        return *val;
    }

    [[nodiscard]] const Value& at(Key key) const {
        const Value* val = get(key);
        if (!val) {
            throw std::out_of_range("SparseSetWithData::at");
        }
        return *val;
    }

    Value& operator[](Key key) {
        Value* val = get(key);
        if (val) {
            return *val;
        }

        emplace(key);
        return *get(key);
    }

    // Direct access to dense arrays
    [[nodiscard]] const Key* keys() const noexcept { return dense_keys_.data(); }
    [[nodiscard]] Value* values() noexcept { return dense_values_.data(); }
    [[nodiscard]] const Value* values() const noexcept { return dense_values_.data(); }
};

} // namespace Engine
