#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace lyrics::game::entity {

// EnTT names a component type by the FNV-1a hash of its name
[[nodiscard]] constexpr std::uint32_t type_hash(std::string_view name) {
    std::uint32_t hash = 2166136261U;
    for (const char c : name) {
        hash = (hash ^ static_cast<std::uint8_t>(c)) * 16777619U;
    }
    return hash;
}

// the address of the actor's component of that type, 0 when it has none
[[nodiscard]] std::uintptr_t find_component(std::uintptr_t actor, std::uint32_t hash, std::size_t size);

}
