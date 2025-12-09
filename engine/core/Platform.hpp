#pragma once

// Platform-specific utilities for Cat Annihilation Engine

#include <cstdlib>
#include <cstddef>

namespace CatEngine {

// Aligned memory allocation - MSVC doesn't support std::aligned_alloc
inline void* aligned_alloc_compat(size_t alignment, size_t size) {
#ifdef _MSC_VER
    return _aligned_malloc(size, alignment);
#else
    return std::aligned_alloc(alignment, size);
#endif
}

inline void aligned_free_compat(void* ptr) {
#ifdef _MSC_VER
    _aligned_free(ptr);
#else
    std::free(ptr);
#endif
}

} // namespace CatEngine
