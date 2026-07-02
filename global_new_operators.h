#ifndef GLOBAL_NEW_OPERATORS_H
#define GLOBAL_NEW_OPERATORS_H

#include <cstddef>

// 定义全局的placement new操作符
inline void* operator new(std::size_t, void* ptr) noexcept {
    return ptr;
}

inline void operator delete(void*, void*) noexcept {}

#endif // GLOBAL_NEW_OPERATORS_H