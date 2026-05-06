#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <utility>
#include <functional>
#include <stdexcept>
#include <iterator>

namespace Engine {

/**
 * HashMap - Robin Hood hashing implementation
 *
 * Features:
 * - Robin Hood hashing for better cache performance
 * - Open addressing with linear probing
 * - 0.75 load factor for optimal performance
 * - Cache-friendly memory layout
 *
 * @tparam K Key type
 * @tparam V Value type
 * @tparam Hash Hash function (defaults to std::hash<K>)
 * @tparam KeyEqual Key equality predicate (defaults to std::equal_to<K>)
 */
template<
    typename K,
    typename V,
    typename Hash = std::hash<K>,
    typename KeyEqual = std::equal_to<K>
>
class HashMap {
public:
    using key_type = K;
    using mapped_type = V;
    using value_type = std::pair<const K, V>;
    using size_type = size_t;
    using difference_type = ptrdiff_t;
    using hasher = Hash;
    using key_equal = KeyEqual;
    using reference = value_type&;
    using const_reference = const value_type&;

private:
    static constexpr size_type INITIAL_CAPACITY = 16;
    static constexpr size_type MAX_LOAD_FACTOR_NUMERATOR = 75;
    static constexpr size_type MAX_LOAD_FACTOR_DENOMINATOR = 100;

    struct Bucket {
        alignas(value_type) uint8_t storage[sizeof(value_type)];
        // We store the FULL std::hash<K> result (size_t) rather than a
        // truncated uint32_t. Truncation looks innocuous but hits a real
        // 50%-collision pathology for keys whose std::hash is identity
        // (libstdc++ / libc++ / MSVC all use identity for std::hash<uint64_t>):
        // any 64-bit ID and its low-32 alias would hash-equal here and force
        // a full key compare on every probe. Bucket already pays alignment
        // padding to value_type's alignment, so widening hash to size_t is
        // free in most layouts and removes the foot-gun entirely.
        size_t hash;
        uint32_t distance; // Distance from ideal position (for Robin Hood)
        bool occupied;

        value_type* ptr() noexcept {
            return reinterpret_cast<value_type*>(storage);
        }

        const value_type* ptr() const noexcept {
            return reinterpret_cast<const value_type*>(storage);
        }
    };

    Bucket* buckets_;
    size_type capacity_;
    size_type size_;
    [[no_unique_address]] Hash hash_fn_;
    [[no_unique_address]] KeyEqual key_eq_;

    [[nodiscard]] size_type hash_key(const K& key) const {
        return hash_fn_(key);
    }

    [[nodiscard]] size_type index_from_hash(size_type hash) const noexcept {
        return hash & (capacity_ - 1); // Assumes capacity is power of 2
    }

    [[nodiscard]] size_type next_power_of_2(size_type n) const noexcept {
        if (n == 0) return 1;
        --n;
        n |= n >> 1;
        n |= n >> 2;
        n |= n >> 4;
        n |= n >> 8;
        n |= n >> 16;
        if constexpr (sizeof(size_type) > 4) {
            n |= n >> 32;
        }
        return n + 1;
    }

    [[nodiscard]] bool should_grow() const noexcept {
        return size_ * MAX_LOAD_FACTOR_DENOMINATOR >= capacity_ * MAX_LOAD_FACTOR_NUMERATOR;
    }

    void grow() {
        size_type new_capacity = capacity_ * 2;
        rehash(new_capacity);
    }

    void rehash(size_type new_capacity) {
        Bucket* old_buckets = buckets_;
        size_type old_capacity = capacity_;

        capacity_ = next_power_of_2(new_capacity);
        buckets_ = static_cast<Bucket*>(::operator new(capacity_ * sizeof(Bucket)));

        for (size_type i = 0; i < capacity_; ++i) {
            buckets_[i].occupied = false;
            buckets_[i].distance = 0;
            buckets_[i].hash = 0;
        }

        size_type old_size = size_;
        size_ = 0;

        // Reinsert all elements
        for (size_type i = 0; i < old_capacity; ++i) {
            if (old_buckets[i].occupied) {
                insert_internal(std::move(*old_buckets[i].ptr()), old_buckets[i].hash);
                old_buckets[i].ptr()->~value_type();
            }
        }

        if (old_buckets) {
            ::operator delete(old_buckets);
        }
    }

    bool insert_internal(value_type&& kv_in, size_t hash) {
        // Stash the incoming pair into an std::optional so the Robin Hood
        // swap path can "reassign" it across loop iterations. value_type is
        // std::pair<const K, V>, which is NOT move-assignable: the const K
        // member can't be reassigned member-wise, so the previous form
        // (`kv = std::move(temp);`) fails to compile the moment the
        // template is instantiated with a real (K, V). std::optional<T>
        // sidesteps the assignment requirement entirely — reset() destroys
        // in place and emplace() placement-news a fresh value into the
        // same buffer, which is exactly the lifecycle the swap path needs.
        std::optional<value_type> kv(std::move(kv_in));

        size_type idx = index_from_hash(hash);
        uint32_t distance = 0;

        while (true) {
            if (!buckets_[idx].occupied) {
                // Empty slot found
                new (buckets_[idx].storage) value_type(std::move(*kv));
                buckets_[idx].hash = hash;
                buckets_[idx].distance = distance;
                buckets_[idx].occupied = true;
                ++size_;
                return true;
            }

            // Robin Hood: steal from the rich
            if (distance > buckets_[idx].distance) {
                // Swap with current bucket
                std::swap(hash, buckets_[idx].hash);
                std::swap(distance, buckets_[idx].distance);

                value_type temp(std::move(*buckets_[idx].ptr()));
                buckets_[idx].ptr()->~value_type();
                new (buckets_[idx].storage) value_type(std::move(*kv));
                // Replace optional payload via destroy-then-construct, the
                // const-K-safe equivalent of `kv = std::move(temp);`.
                kv.reset();
                kv.emplace(std::move(temp));
            } else if (buckets_[idx].hash == hash && key_eq_(buckets_[idx].ptr()->first, kv->first)) {
                // Key already exists
                return false;
            }

            idx = (idx + 1) & (capacity_ - 1);
            ++distance;
        }
    }

    size_type find_index(const K& key) const {
        if (capacity_ == 0) return capacity_;

        size_t hash = hash_key(key);
        size_type idx = index_from_hash(hash);
        uint32_t distance = 0;

        while (buckets_[idx].occupied) {
            if (buckets_[idx].hash == hash && key_eq_(buckets_[idx].ptr()->first, key)) {
                return idx;
            }

            // If we've probed further than this bucket's distance, the key doesn't exist
            if (distance > buckets_[idx].distance) {
                break;
            }

            idx = (idx + 1) & (capacity_ - 1);
            ++distance;
        }

        return capacity_; // Not found
    }

public:
    class iterator {
        friend class HashMap;
        Bucket* current_;
        Bucket* end_;

        void advance() {
            ++current_;
            while (current_ != end_ && !current_->occupied) {
                ++current_;
            }
        }

        iterator(Bucket* current, Bucket* end) : current_(current), end_(end) {}

    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = HashMap::value_type;
        using difference_type = ptrdiff_t;
        using pointer = value_type*;
        using reference = value_type&;

        iterator() : current_(nullptr), end_(nullptr) {}

        reference operator*() const { return *current_->ptr(); }
        pointer operator->() const { return current_->ptr(); }

        iterator& operator++() {
            advance();
            return *this;
        }

        iterator operator++(int) {
            iterator tmp = *this;
            advance();
            return tmp;
        }

        bool operator==(const iterator& other) const { return current_ == other.current_; }
        bool operator!=(const iterator& other) const { return current_ != other.current_; }
    };

    class const_iterator {
        friend class HashMap;
        const Bucket* current_;
        const Bucket* end_;

        void advance() {
            ++current_;
            while (current_ != end_ && !current_->occupied) {
                ++current_;
            }
        }

        const_iterator(const Bucket* current, const Bucket* end) : current_(current), end_(end) {}

    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = HashMap::value_type;
        using difference_type = ptrdiff_t;
        using pointer = const value_type*;
        using reference = const value_type&;

        const_iterator() : current_(nullptr), end_(nullptr) {}
        const_iterator(const iterator& it) : current_(it.current_), end_(it.end_) {}

        reference operator*() const { return *current_->ptr(); }
        pointer operator->() const { return current_->ptr(); }

        const_iterator& operator++() {
            advance();
            return *this;
        }

        const_iterator operator++(int) {
            const_iterator tmp = *this;
            advance();
            return tmp;
        }

        bool operator==(const const_iterator& other) const { return current_ == other.current_; }
        bool operator!=(const const_iterator& other) const { return current_ != other.current_; }
    };

    // Constructors
    HashMap()
        : buckets_(nullptr)
        , capacity_(0)
        , size_(0)
        , hash_fn_()
        , key_eq_()
    {
        reserve(INITIAL_CAPACITY);
    }

    explicit HashMap(size_type initial_capacity)
        : buckets_(nullptr)
        , capacity_(0)
        , size_(0)
        , hash_fn_()
        , key_eq_()
    {
        reserve(initial_capacity);
    }

    HashMap(const HashMap& other)
        : buckets_(nullptr)
        , capacity_(0)
        , size_(0)
        , hash_fn_(other.hash_fn_)
        , key_eq_(other.key_eq_)
    {
        reserve(other.capacity_);
        for (size_type i = 0; i < other.capacity_; ++i) {
            if (other.buckets_[i].occupied) {
                insert(*other.buckets_[i].ptr());
            }
        }
    }

    HashMap(HashMap&& other) noexcept
        : buckets_(other.buckets_)
        , capacity_(other.capacity_)
        , size_(other.size_)
        , hash_fn_(std::move(other.hash_fn_))
        , key_eq_(std::move(other.key_eq_))
    {
        other.buckets_ = nullptr;
        other.capacity_ = 0;
        other.size_ = 0;
    }

    ~HashMap() {
        clear();
        if (buckets_) {
            ::operator delete(buckets_);
        }
    }

    HashMap& operator=(const HashMap& other) {
        if (this != &other) {
            HashMap temp(other);
            swap(temp);
        }
        return *this;
    }

    HashMap& operator=(HashMap&& other) noexcept {
        if (this != &other) {
            clear();
            if (buckets_) {
                ::operator delete(buckets_);
            }

            buckets_ = other.buckets_;
            capacity_ = other.capacity_;
            size_ = other.size_;
            hash_fn_ = std::move(other.hash_fn_);
            key_eq_ = std::move(other.key_eq_);

            other.buckets_ = nullptr;
            other.capacity_ = 0;
            other.size_ = 0;
        }
        return *this;
    }

    // Iterators
    iterator begin() {
        for (size_type i = 0; i < capacity_; ++i) {
            if (buckets_[i].occupied) {
                return iterator(&buckets_[i], buckets_ + capacity_);
            }
        }
        return end();
    }

    const_iterator begin() const {
        for (size_type i = 0; i < capacity_; ++i) {
            if (buckets_[i].occupied) {
                return const_iterator(&buckets_[i], buckets_ + capacity_);
            }
        }
        return end();
    }

    const_iterator cbegin() const { return begin(); }

    iterator end() { return iterator(buckets_ + capacity_, buckets_ + capacity_); }
    const_iterator end() const { return const_iterator(buckets_ + capacity_, buckets_ + capacity_); }
    const_iterator cend() const { return end(); }

    // Capacity
    [[nodiscard]] bool empty() const noexcept { return size_ == 0; }
    [[nodiscard]] size_type size() const noexcept { return size_; }
    [[nodiscard]] size_type capacity() const noexcept { return capacity_; }

    void reserve(size_type new_capacity) {
        if (new_capacity > capacity_) {
            rehash(new_capacity);
        }
    }

    // Modifiers
    void clear() {
        for (size_type i = 0; i < capacity_; ++i) {
            if (buckets_[i].occupied) {
                buckets_[i].ptr()->~value_type();
                buckets_[i].occupied = false;
            }
        }
        size_ = 0;
    }

    std::pair<iterator, bool> insert(const value_type& kv) {
        if (should_grow()) {
            grow();
        }

        size_t hash = hash_key(kv.first);
        size_type existing = find_index(kv.first);

        if (existing != capacity_) {
            return {iterator(&buckets_[existing], buckets_ + capacity_), false};
        }

        value_type temp = kv;
        bool inserted = insert_internal(std::move(temp), hash);

        if (inserted) {
            size_type idx = find_index(kv.first);
            return {iterator(&buckets_[idx], buckets_ + capacity_), true};
        }

        return {end(), false};
    }

    std::pair<iterator, bool> insert(value_type&& kv) {
        if (should_grow()) {
            grow();
        }

        size_t hash = hash_key(kv.first);
        size_type existing = find_index(kv.first);

        if (existing != capacity_) {
            return {iterator(&buckets_[existing], buckets_ + capacity_), false};
        }

        bool inserted = insert_internal(std::move(kv), hash);

        if (inserted) {
            size_type idx = find_index(kv.first);
            return {iterator(&buckets_[idx], buckets_ + capacity_), true};
        }

        return {end(), false};
    }

    template<typename... Args>
    std::pair<iterator, bool> emplace(Args&&... args) {
        value_type kv(std::forward<Args>(args)...);
        return insert(std::move(kv));
    }

    size_type erase(const K& key) {
        size_type idx = find_index(key);
        if (idx == capacity_) {
            return 0;
        }

        buckets_[idx].ptr()->~value_type();
        buckets_[idx].occupied = false;
        --size_;

        // Backward-shift deletion to maintain Robin Hood invariants.
        //
        // CRITICAL: Bucket holds value_type inside an aligned uint8_t buffer,
        // so the compiler-generated `Bucket::operator=` would byte-copy the
        // storage[] array — bypassing value_type's copy/move constructor. For
        // trivially-copyable value_type that is harmless, but a value_type
        // with internal pointers (std::string SSO, std::unique_ptr,
        // DynamicArray) ends up with a byte-clone that aliases the original's
        // owned memory; the original is then marked unoccupied and never
        // destructed, so the next clear() / erase() walks a dangling
        // self-pointer (canonical heap-corruption pattern) and the engine
        // leaks one heap allocation per shifted slot.
        //
        // We therefore shift each slot by hand: placement-new-move into the
        // freshly-vacated `idx` slot, destruct the source at `next_idx`, and
        // copy the integer hash/distance bookkeeping separately.
        size_type next_idx = (idx + 1) & (capacity_ - 1);
        while (buckets_[next_idx].occupied && buckets_[next_idx].distance > 0) {
            new (buckets_[idx].storage) value_type(std::move(*buckets_[next_idx].ptr()));
            buckets_[next_idx].ptr()->~value_type();

            buckets_[idx].hash = buckets_[next_idx].hash;
            buckets_[idx].distance = buckets_[next_idx].distance - 1;
            buckets_[idx].occupied = true;

            buckets_[next_idx].occupied = false;
            idx = next_idx;
            next_idx = (next_idx + 1) & (capacity_ - 1);
        }

        return 1;
    }

    iterator erase(iterator pos) {
        if (pos == end()) return end();
        size_type idx = pos.current_ - buckets_;
        erase(pos->first);

        // Find next occupied bucket
        for (size_type i = idx; i < capacity_; ++i) {
            if (buckets_[i].occupied) {
                return iterator(&buckets_[i], buckets_ + capacity_);
            }
        }
        return end();
    }

    void swap(HashMap& other) noexcept {
        using std::swap;
        swap(buckets_, other.buckets_);
        swap(capacity_, other.capacity_);
        swap(size_, other.size_);
        swap(hash_fn_, other.hash_fn_);
        swap(key_eq_, other.key_eq_);
    }

    // Lookup
    [[nodiscard]] iterator find(const K& key) {
        size_type idx = find_index(key);
        if (idx == capacity_) {
            return end();
        }
        return iterator(&buckets_[idx], buckets_ + capacity_);
    }

    [[nodiscard]] const_iterator find(const K& key) const {
        size_type idx = find_index(key);
        if (idx == capacity_) {
            return end();
        }
        return const_iterator(&buckets_[idx], buckets_ + capacity_);
    }

    [[nodiscard]] bool contains(const K& key) const {
        return find_index(key) != capacity_;
    }

    [[nodiscard]] size_type count(const K& key) const {
        return contains(key) ? 1 : 0;
    }

    V& operator[](const K& key) {
        auto it = find(key);
        if (it != end()) {
            return it->second;
        }

        auto [new_it, inserted] = insert({key, V{}});
        return new_it->second;
    }

    V& at(const K& key) {
        auto it = find(key);
        if (it == end()) {
            throw std::out_of_range("HashMap::at");
        }
        return it->second;
    }

    const V& at(const K& key) const {
        auto it = find(key);
        if (it == end()) {
            throw std::out_of_range("HashMap::at");
        }
        return it->second;
    }
};

} // namespace Engine
