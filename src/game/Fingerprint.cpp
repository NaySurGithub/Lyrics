#include "game/Fingerprint.hpp"

#include "game/Offsets.hpp"
#include "memory/Access.hpp"

#include <cstring>

namespace lyrics::game {
namespace {

template <std::size_t Size>
bool same_bytes(std::uintptr_t rva, const std::array<std::uint8_t, Size>& expected) {
    std::array<std::uint8_t, Size> actual{};
    const auto* site = memory::resolve<std::uint8_t>(rva);
    return site != nullptr && memory::read(reinterpret_cast<std::uintptr_t>(site), actual.data(), Size) &&
        actual == expected;
}

}

bool matches_version() {
    return same_bytes(offsets::ApplyTurnDelta, offsets::ApplyTurnDeltaBytes) &&
        same_bytes(offsets::RenderLevelCall, offsets::RenderLevelCallBytes) &&
        same_bytes(offsets::RenderLevelOtherCall, offsets::RenderLevelOtherCallBytes);
}

}
