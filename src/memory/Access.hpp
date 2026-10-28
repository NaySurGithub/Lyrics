#pragma once

#include <cstddef>
#include <cstdint>

namespace lyrics::memory {

// fault-safe copies: false instead of a crash on unmapped or protected memory
[[nodiscard]] bool read(std::uintptr_t address, void* out, std::size_t size);

template <typename T>
[[nodiscard]] bool read(std::uintptr_t address, T& value) {
    return read(address, &value, sizeof(value));
}

[[nodiscard]] std::uintptr_t image_base();

template <typename T = void>
[[nodiscard]] T* resolve(std::uintptr_t rva) {
    const std::uintptr_t base = image_base();
    return base == 0 || rva == 0 ? nullptr : reinterpret_cast<T*>(base + rva);
}

}
