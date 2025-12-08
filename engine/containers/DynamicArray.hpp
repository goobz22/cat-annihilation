#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <utility>
#include <initializer_list>
#include <iterator>

namespace Engine {

/**
 * DynamicArray - Custom dynamic array with small buffer optimization
 *
 * Features:
 * - Small buffer optimization (16 bytes inline storage)
 * - Custom allocator support
 * - Move semantics for efficient reallocation
 * - Compatible with standard algorithms
 *
 * @tparam T Element type
 * @tparam Allocator Allocator type (defaults to std::allocator<T>)
 */
template<typename T, typename Allocator = std::allocator<T>>
class DynamicArray {
public:
    using value_type = T;
    using allocator_type = Allocator;
    using size_type = size_t;
    using difference_type = ptrdiff_t;
    using reference = T&;
    using const_reference = const T&;
    using pointer = T*;
    using const_pointer = const T*;
    using iterator = T*;
    using const_iterator = const T*;
    using reverse_iterator = std::reverse_iterator<iterator>;
    using const_reverse_iterator = std::reverse_iterator<const_iterator>;

private:
    static constexpr size_type SMALL_BUFFER_SIZE = 16 / sizeof(T) > 0 ? 16 / sizeof(T) : 1;

    alignas(T) uint8_t small_buffer_[sizeof(T) * SMALL_BUFFER_SIZE];
    pointer data_;
    size_type size_;
    size_type capacity_;
    [[no_unique_address]] Allocator allocator_;

    [[nodiscard]] constexpr bool is_small() const noexcept {
        return data_ == reinterpret_cast<const T*>(small_buffer_);
    }

    void destroy_elements() noexcept {
        if constexpr (!std::is_trivially_destructible_v<T>) {
            for (size_type i = 0; i < size_; ++i) {
                std::allocator_traits<Allocator>::destroy(allocator_, data_ + i);
            }
        }
    }

    void deallocate_buffer() noexcept {
        if (!is_small() && data_) {
            std::allocator_traits<Allocator>::deallocate(allocator_, data_, capacity_);
        }
    }

    void grow(size_type min_capacity) {
        size_type new_capacity = capacity_ == 0 ? SMALL_BUFFER_SIZE : capacity_ * 2;
        while (new_capacity < min_capacity) {
            new_capacity *= 2;
        }
        reserve(new_capacity);
    }

public:
    // Constructors
    constexpr DynamicArray() noexcept(noexcept(Allocator()))
        : data_(reinterpret_cast<pointer>(small_buffer_))
        , size_(0)
        , capacity_(SMALL_BUFFER_SIZE)
        , allocator_()
    {}

    explicit constexpr DynamicArray(const Allocator& alloc) noexcept
        : data_(reinterpret_cast<pointer>(small_buffer_))
        , size_(0)
        , capacity_(SMALL_BUFFER_SIZE)
        , allocator_(alloc)
    {}

    explicit DynamicArray(size_type count, const T& value = T(), const Allocator& alloc = Allocator())
        : data_(reinterpret_cast<pointer>(small_buffer_))
        , size_(0)
        , capacity_(SMALL_BUFFER_SIZE)
        , allocator_(alloc)
    {
        reserve(count);
        for (size_type i = 0; i < count; ++i) {
            push_back(value);
        }
    }

    DynamicArray(std::initializer_list<T> init, const Allocator& alloc = Allocator())
        : data_(reinterpret_cast<pointer>(small_buffer_))
        , size_(0)
        , capacity_(SMALL_BUFFER_SIZE)
        , allocator_(alloc)
    {
        reserve(init.size());
        for (const auto& item : init) {
            push_back(item);
        }
    }

    // Copy constructor
    DynamicArray(const DynamicArray& other)
        : data_(reinterpret_cast<pointer>(small_buffer_))
        , size_(0)
        , capacity_(SMALL_BUFFER_SIZE)
        , allocator_(std::allocator_traits<Allocator>::select_on_container_copy_construction(other.allocator_))
    {
        reserve(other.size_);
        for (size_type i = 0; i < other.size_; ++i) {
            push_back(other.data_[i]);
        }
    }

    // Move constructor
    DynamicArray(DynamicArray&& other) noexcept
        : data_(reinterpret_cast<pointer>(small_buffer_))
        , size_(0)
        , capacity_(SMALL_BUFFER_SIZE)
        , allocator_(std::move(other.allocator_))
    {
        if (other.is_small()) {
            // Move elements from small buffer
            for (size_type i = 0; i < other.size_; ++i) {
                std::allocator_traits<Allocator>::construct(allocator_, data_ + i, std::move(other.data_[i]));
            }
            size_ = other.size_;
        } else {
            // Steal the heap buffer
            data_ = other.data_;
            size_ = other.size_;
            capacity_ = other.capacity_;

            other.data_ = reinterpret_cast<pointer>(other.small_buffer_);
            other.size_ = 0;
            other.capacity_ = SMALL_BUFFER_SIZE;
        }
    }

    // Destructor
    ~DynamicArray() {
        destroy_elements();
        deallocate_buffer();
    }

    // Copy assignment
    DynamicArray& operator=(const DynamicArray& other) {
        if (this != &other) {
            DynamicArray temp(other);
            swap(temp);
        }
        return *this;
    }

    // Move assignment
    DynamicArray& operator=(DynamicArray&& other) noexcept {
        if (this != &other) {
            destroy_elements();
            deallocate_buffer();

            allocator_ = std::move(other.allocator_);

            if (other.is_small()) {
                data_ = reinterpret_cast<pointer>(small_buffer_);
                capacity_ = SMALL_BUFFER_SIZE;
                size_ = 0;
                for (size_type i = 0; i < other.size_; ++i) {
                    std::allocator_traits<Allocator>::construct(allocator_, data_ + i, std::move(other.data_[i]));
                    ++size_;
                }
            } else {
                data_ = other.data_;
                size_ = other.size_;
                capacity_ = other.capacity_;

                other.data_ = reinterpret_cast<pointer>(other.small_buffer_);
                other.size_ = 0;
                other.capacity_ = SMALL_BUFFER_SIZE;
            }
        }
        return *this;
    }

    // Element access
    [[nodiscard]] constexpr reference operator[](size_type pos) noexcept {
        return data_[pos];
    }

    [[nodiscard]] constexpr const_reference operator[](size_type pos) const noexcept {
        return data_[pos];
    }

    [[nodiscard]] constexpr reference at(size_type pos) {
        if (pos >= size_) {
            throw std::out_of_range("DynamicArray::at");
        }
        return data_[pos];
    }

    [[nodiscard]] constexpr const_reference at(size_type pos) const {
        if (pos >= size_) {
            throw std::out_of_range("DynamicArray::at");
        }
        return data_[pos];
    }

    [[nodiscard]] constexpr reference front() noexcept { return data_[0]; }
    [[nodiscard]] constexpr const_reference front() const noexcept { return data_[0]; }
    [[nodiscard]] constexpr reference back() noexcept { return data_[size_ - 1]; }
    [[nodiscard]] constexpr const_reference back() const noexcept { return data_[size_ - 1]; }
    [[nodiscard]] constexpr pointer data() noexcept { return data_; }
    [[nodiscard]] constexpr const_pointer data() const noexcept { return data_; }

    // Iterators
    [[nodiscard]] constexpr iterator begin() noexcept { return data_; }
    [[nodiscard]] constexpr const_iterator begin() const noexcept { return data_; }
    [[nodiscard]] constexpr const_iterator cbegin() const noexcept { return data_; }
    [[nodiscard]] constexpr iterator end() noexcept { return data_ + size_; }
    [[nodiscard]] constexpr const_iterator end() const noexcept { return data_ + size_; }
    [[nodiscard]] constexpr const_iterator cend() const noexcept { return data_ + size_; }

    [[nodiscard]] constexpr reverse_iterator rbegin() noexcept { return reverse_iterator(end()); }
    [[nodiscard]] constexpr const_reverse_iterator rbegin() const noexcept { return const_reverse_iterator(end()); }
    [[nodiscard]] constexpr const_reverse_iterator crbegin() const noexcept { return const_reverse_iterator(end()); }
    [[nodiscard]] constexpr reverse_iterator rend() noexcept { return reverse_iterator(begin()); }
    [[nodiscard]] constexpr const_reverse_iterator rend() const noexcept { return const_reverse_iterator(begin()); }
    [[nodiscard]] constexpr const_reverse_iterator crend() const noexcept { return const_reverse_iterator(begin()); }

    // Capacity
    [[nodiscard]] constexpr bool empty() const noexcept { return size_ == 0; }
    [[nodiscard]] constexpr size_type size() const noexcept { return size_; }
    [[nodiscard]] constexpr size_type capacity() const noexcept { return capacity_; }

    void reserve(size_type new_capacity) {
        if (new_capacity <= capacity_) return;

        pointer new_data;
        if (new_capacity <= SMALL_BUFFER_SIZE) {
            new_data = reinterpret_cast<pointer>(small_buffer_);
        } else {
            new_data = std::allocator_traits<Allocator>::allocate(allocator_, new_capacity);
        }

        // Move elements to new buffer
        if constexpr (std::is_trivially_copyable_v<T>) {
            std::memcpy(new_data, data_, size_ * sizeof(T));
        } else {
            for (size_type i = 0; i < size_; ++i) {
                std::allocator_traits<Allocator>::construct(allocator_, new_data + i, std::move(data_[i]));
                std::allocator_traits<Allocator>::destroy(allocator_, data_ + i);
            }
        }

        deallocate_buffer();
        data_ = new_data;
        capacity_ = new_capacity;
    }

    void shrink_to_fit() {
        if (size_ == capacity_) return;

        if (size_ <= SMALL_BUFFER_SIZE && !is_small()) {
            pointer new_data = reinterpret_cast<pointer>(small_buffer_);
            for (size_type i = 0; i < size_; ++i) {
                std::allocator_traits<Allocator>::construct(allocator_, new_data + i, std::move(data_[i]));
                std::allocator_traits<Allocator>::destroy(allocator_, data_ + i);
            }
            deallocate_buffer();
            data_ = new_data;
            capacity_ = SMALL_BUFFER_SIZE;
        } else if (size_ > SMALL_BUFFER_SIZE) {
            pointer new_data = std::allocator_traits<Allocator>::allocate(allocator_, size_);
            for (size_type i = 0; i < size_; ++i) {
                std::allocator_traits<Allocator>::construct(allocator_, new_data + i, std::move(data_[i]));
                std::allocator_traits<Allocator>::destroy(allocator_, data_ + i);
            }
            deallocate_buffer();
            data_ = new_data;
            capacity_ = size_;
        }
    }

    // Modifiers
    void clear() noexcept {
        destroy_elements();
        size_ = 0;
    }

    void push_back(const T& value) {
        if (size_ >= capacity_) {
            grow(size_ + 1);
        }
        std::allocator_traits<Allocator>::construct(allocator_, data_ + size_, value);
        ++size_;
    }

    void push_back(T&& value) {
        if (size_ >= capacity_) {
            grow(size_ + 1);
        }
        std::allocator_traits<Allocator>::construct(allocator_, data_ + size_, std::move(value));
        ++size_;
    }

    template<typename... Args>
    reference emplace_back(Args&&... args) {
        if (size_ >= capacity_) {
            grow(size_ + 1);
        }
        std::allocator_traits<Allocator>::construct(allocator_, data_ + size_, std::forward<Args>(args)...);
        return data_[size_++];
    }

    void pop_back() noexcept {
        if (size_ > 0) {
            --size_;
            std::allocator_traits<Allocator>::destroy(allocator_, data_ + size_);
        }
    }

    void resize(size_type count, const T& value = T()) {
        if (count < size_) {
            for (size_type i = count; i < size_; ++i) {
                std::allocator_traits<Allocator>::destroy(allocator_, data_ + i);
            }
            size_ = count;
        } else if (count > size_) {
            reserve(count);
            for (size_type i = size_; i < count; ++i) {
                std::allocator_traits<Allocator>::construct(allocator_, data_ + i, value);
            }
            size_ = count;
        }
    }

    void swap(DynamicArray& other) noexcept {
        using std::swap;

        if (is_small() && other.is_small()) {
            // Both using small buffers - swap element by element
            size_type min_size = size_ < other.size_ ? size_ : other.size_;
            for (size_type i = 0; i < min_size; ++i) {
                swap(data_[i], other.data_[i]);
            }

            // Move remaining elements
            if (size_ > min_size) {
                for (size_type i = min_size; i < size_; ++i) {
                    std::allocator_traits<Allocator>::construct(other.allocator_, other.data_ + i, std::move(data_[i]));
                    std::allocator_traits<Allocator>::destroy(allocator_, data_ + i);
                }
            } else if (other.size_ > min_size) {
                for (size_type i = min_size; i < other.size_; ++i) {
                    std::allocator_traits<Allocator>::construct(allocator_, data_ + i, std::move(other.data_[i]));
                    std::allocator_traits<Allocator>::destroy(other.allocator_, other.data_ + i);
                }
            }
        } else if (!is_small() && !other.is_small()) {
            // Both using heap - simple pointer swap
            swap(data_, other.data_);
            swap(capacity_, other.capacity_);
        } else {
            // One small, one heap - need to move elements
            if (is_small()) {
                pointer temp_data = other.data_;
                size_type temp_capacity = other.capacity_;

                other.data_ = reinterpret_cast<pointer>(other.small_buffer_);
                other.capacity_ = SMALL_BUFFER_SIZE;
                for (size_type i = 0; i < size_; ++i) {
                    std::allocator_traits<Allocator>::construct(other.allocator_, other.data_ + i, std::move(data_[i]));
                    std::allocator_traits<Allocator>::destroy(allocator_, data_ + i);
                }

                data_ = temp_data;
                capacity_ = temp_capacity;
            } else {
                pointer temp_data = data_;
                size_type temp_capacity = capacity_;

                data_ = reinterpret_cast<pointer>(small_buffer_);
                capacity_ = SMALL_BUFFER_SIZE;
                for (size_type i = 0; i < other.size_; ++i) {
                    std::allocator_traits<Allocator>::construct(allocator_, data_ + i, std::move(other.data_[i]));
                    std::allocator_traits<Allocator>::destroy(other.allocator_, other.data_ + i);
                }

                other.data_ = temp_data;
                other.capacity_ = temp_capacity;
            }
        }

        swap(size_, other.size_);
        swap(allocator_, other.allocator_);
    }
};

} // namespace Engine
