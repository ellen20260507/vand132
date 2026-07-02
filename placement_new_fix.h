#ifndef PLACEMENT_NEW_FIX_H
#define PLACEMENT_NEW_FIX_H

#include <cstddef>

// 定义全局的placement new操作符
inline void* operator new(std::size_t, void* ptr) noexcept {
    return ptr;
}

inline void operator delete(void*, void*) noexcept {}

#endif // PLACEMENT_NEW_FIX_H