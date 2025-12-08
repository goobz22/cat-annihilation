#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>
#include <utility>
#include <stdexcept>
#include <limits>

namespace Engine {

/**
 * SlotMap - Handle-based container with generational indices
 *
 * Features:
 * - Generational indices (32-bit index + 32-bit generation)
 * - O(1) insert, remove, and lookup
 * - No dangling references - invalid handles are detected
 * - Stable iteration over dense array
 * - Handles remain valid even when other elements are removed
 *
 * @tparam T Element type
 */
template<typename T>
class SlotMap {
public:
    /**
     * Handle type - uniquely identifies an element
     * Contains index and generation to detect use-after-free
     */
    struct Handle {
        uint32_t index;
        uint32_t generation;

        constexpr Handle() : index(UINT32_MAX), generation(0) {}
        constexpr Handle(uint32_t idx, uint32_t gen) : index(idx), generation(gen) {}

        [[nodiscard]] constexpr bool is_valid() const noexcept {
            return index != UINT32_MAX;
        }

        [[nodiscard]] constexpr bool operator==(const Handle& other) const noexcept {
            return index == other.index && generation == other.generation;
        }

        [[nodiscard]] constexpr bool operator!=(const Handle& other) const noexcept {
            return !(*this == other);
        }
    };

private:
    struct Slot {
        uint32_t index;      // Index into data_ array (or next free slot if unused)
        uint32_t generation; // Generation counter
        bool occupied;

        Slot() : index(0), generation(0), occupied(false) {}
    };

    std::vector<Slot> slots_;      // Sparse array of slots
    std::vector<T> data_;          // Dense array of actual data
    std::vector<uint32_t> erase_;  // Maps data index back to slot index
    uint32_t free_head_;           // Head of free list
    size_t size_;

public:
    using value_type = T;
    using size_type = size_t;
    using reference = T&;
    using const_reference = const T&;

    /**
     * Iterator over dense data array
     */
    class iterator {
        friend class SlotMap;
        T* ptr_;

        explicit iterator(T* ptr) : ptr_(ptr) {}

    public:
        using iterator_category = std::random_access_iterator_tag;
        using value_type = T;
        using difference_type = ptrdiff_t;
        using pointer = T*;
        using reference = T&;

        iterator() : ptr_(nullptr) {}

        reference operator*() const { return *ptr_; }
        pointer operator->() const { return ptr_; }
        reference operator[](difference_type n) const { return ptr_[n]; }

        iterator& operator++() { ++ptr_; return *this; }
        iterator operator++(int) { iterator tmp = *this; ++ptr_; return tmp; }
        iterator& operator--() { --ptr_; return *this; }
        iterator operator--(int) { iterator tmp = *this; --ptr_; return tmp; }

        iterator& operator+=(difference_type n) { ptr_ += n; return *this; }
        iterator& operator-=(difference_type n) { ptr_ -= n; return *this; }

        iterator operator+(difference_type n) const { return iterator(ptr_ + n); }
        iterator operator-(difference_type n) const { return iterator(ptr_ - n); }
        difference_type operator-(const iterator& other) const { return ptr_ - other.ptr_; }

        bool operator==(const iterator& other) const { return ptr_ == other.ptr_; }
        bool operator!=(const iterator& other) const { return ptr_ != other.ptr_; }
        bool operator<(const iterator& other) const { return ptr_ < other.ptr_; }
        bool operator<=(const iterator& other) const { return ptr_ <= other.ptr_; }
        bool operator>(const iterator& other) const { return ptr_ > other.ptr_; }
        bool operator>=(const iterator& other) const { return ptr_ >= other.ptr_; }
    };

    class const_iterator {
        friend class SlotMap;
        const T* ptr_;

        explicit const_iterator(const T* ptr) : ptr_(ptr) {}

    public:
        using iterator_category = std::random_access_iterator_tag;
        using value_type = T;
        using difference_type = ptrdiff_t;
        using pointer = const T*;
        using reference = const T&;

        const_iterator() : ptr_(nullptr) {}
        const_iterator(const iterator& it) : ptr_(it.ptr_) {}

        reference operator*() const { return *ptr_; }
        pointer operator->() const { return ptr_; }
        reference operator[](difference_type n) const { return ptr_[n]; }

        const_iterator& operator++() { ++ptr_; return *this; }
        const_iterator operator++(int) { const_iterator tmp = *this; ++ptr_; return tmp; }
        const_iterator& operator--() { --ptr_; return *this; }
        const_iterator operator--(int) { const_iterator tmp = *this; --ptr_; return tmp; }

        const_iterator& operator+=(difference_type n) { ptr_ += n; return *this; }
        const_iterator& operator-=(difference_type n) { ptr_ -= n; return *this; }

        const_iterator operator+(difference_type n) const { return const_iterator(ptr_ + n); }
        const_iterator operator-(difference_type n) const { return const_iterator(ptr_ - n); }
        difference_type operator-(const const_iterator& other) const { return ptr_ - other.ptr_; }

        bool operator==(const const_iterator& other) const { return ptr_ == other.ptr_; }
        bool operator!=(const const_iterator& other) const { return ptr_ != other.ptr_; }
        bool operator<(const const_iterator& other) const { return ptr_ < other.ptr_; }
        bool operator<=(const const_iterator& other) const { return ptr_ <= other.ptr_; }
        bool operator>(const const_iterator& other) const { return ptr_ > other.ptr_; }
        bool operator>=(const const_iterator& other) const { return ptr_ >= other.ptr_; }
    };

    // Constructors
    SlotMap()
        : free_head_(UINT32_MAX)
        , size_(0)
    {}

    explicit SlotMap(size_type reserve_size)
        : free_head_(UINT32_MAX)
        , size_(0)
    {
        reserve(reserve_size);
    }

    // Capacity
    [[nodiscard]] bool empty() const noexcept { return size_ == 0; }
    [[nodiscard]] size_type size() const noexcept { return size_; }
    [[nodiscard]] size_type capacity() const noexcept { return slots_.capacity(); }

    void reserve(size_type new_capacity) {
        slots_.reserve(new_capacity);
        data_.reserve(new_capacity);
        erase_.reserve(new_capacity);
    }

    void clear() {
        data_.clear();
        erase_.clear();

        // Reset all slots and rebuild free list
        for (uint32_t i = 0; i < slots_.size(); ++i) {
            slots_[i].occupied = false;
            slots_[i].index = i + 1;
            slots_[i].generation++;
        }

        free_head_ = slots_.empty() ? UINT32_MAX : 0;
        size_ = 0;
    }

    // Modifiers
    template<typename... Args>
    Handle emplace(Args&&... args) {
        uint32_t slot_idx;

        if (free_head_ != UINT32_MAX) {
            // Use slot from free list
            slot_idx = free_head_;
            free_head_ = slots_[slot_idx].index;
        } else {
            // Allocate new slot
            slot_idx = static_cast<uint32_t>(slots_.size());
            slots_.emplace_back();
        }

        uint32_t data_idx = static_cast<uint32_t>(data_.size());

        // Update slot
        slots_[slot_idx].index = data_idx;
        slots_[slot_idx].occupied = true;

        // Add data
        data_.emplace_back(std::forward<Args>(args)...);
        erase_.push_back(slot_idx);

        ++size_;

        return Handle(slot_idx, slots_[slot_idx].generation);
    }

    Handle insert(const T& value) {
        return emplace(value);
    }

    Handle insert(T&& value) {
        return emplace(std::move(value));
    }

    bool erase(Handle handle) {
        if (!is_valid(handle)) {
            return false;
        }

        uint32_t slot_idx = handle.index;
        uint32_t data_idx = slots_[slot_idx].index;

        // Swap with last element in data array
        if (data_idx < data_.size() - 1) {
            uint32_t last_slot_idx = erase_.back();

            data_[data_idx] = std::move(data_.back());
            erase_[data_idx] = last_slot_idx;
            slots_[last_slot_idx].index = data_idx;
        }

        data_.pop_back();
        erase_.pop_back();

        // Add slot to free list
        slots_[slot_idx].occupied = false;
        slots_[slot_idx].index = free_head_;
        slots_[slot_idx].generation++;
        free_head_ = slot_idx;

        --size_;
        return true;
    }

    // Lookup
    [[nodiscard]] bool is_valid(Handle handle) const noexcept {
        if (!handle.is_valid()) return false;
        if (handle.index >= slots_.size()) return false;

        const auto& slot = slots_[handle.index];
        return slot.occupied && slot.generation == handle.generation;
    }

    [[nodiscard]] bool contains(Handle handle) const noexcept {
        return is_valid(handle);
    }

    [[nodiscard]] T* get(Handle handle) noexcept {
        if (!is_valid(handle)) return nullptr;
        return &data_[slots_[handle.index].index];
    }

    [[nodiscard]] const T* get(Handle handle) const noexcept {
        if (!is_valid(handle)) return nullptr;
        return &data_[slots_[handle.index].index];
    }

    [[nodiscard]] T& operator[](Handle handle) {
        return data_[slots_[handle.index].index];
    }

    [[nodiscard]] const T& operator[](Handle handle) const {
        return data_[slots_[handle.index].index];
    }

    [[nodiscard]] T& at(Handle handle) {
        if (!is_valid(handle)) {
            throw std::out_of_range("SlotMap::at - invalid handle");
        }
        return data_[slots_[handle.index].index];
    }

    [[nodiscard]] const T& at(Handle handle) const {
        if (!is_valid(handle)) {
            throw std::out_of_range("SlotMap::at - invalid handle");
        }
        return data_[slots_[handle.index].index];
    }

    // Iterators (iterate over dense data array)
    [[nodiscard]] iterator begin() noexcept { return iterator(data_.data()); }
    [[nodiscard]] const_iterator begin() const noexcept { return const_iterator(data_.data()); }
    [[nodiscard]] const_iterator cbegin() const noexcept { return const_iterator(data_.data()); }

    [[nodiscard]] iterator end() noexcept { return iterator(data_.data() + data_.size()); }
    [[nodiscard]] const_iterator end() const noexcept { return const_iterator(data_.data() + data_.size()); }
    [[nodiscard]] const_iterator cend() const noexcept { return const_iterator(data_.data() + data_.size()); }

    // Direct access to dense data (for systems that need to iterate efficiently)
    [[nodiscard]] T* data() noexcept { return data_.data(); }
    [[nodiscard]] const T* data() const noexcept { return data_.data(); }
};

} // namespace Engine

// Hash support for Handle
namespace std {
    template<typename T>
    struct hash<Engine::SlotMap<T>::Handle> {
        size_t operator()(const typename Engine::SlotMap<T>::Handle& handle) const noexcept {
            return static_cast<size_t>(handle.index) ^ (static_cast<size_t>(handle.generation) << 32);
        }
    };
}
