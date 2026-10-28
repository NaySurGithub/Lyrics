#pragma once

#include <cstdint>

namespace lyrics::game::player {

bool install();
void remove();

// the local player and its ClientInstance, 0 outside a world
[[nodiscard]] std::uintptr_t local();
[[nodiscard]] std::uintptr_t client();
// the game turned the player within the last second
[[nodiscard]] bool in_world();
[[nodiscard]] bool gliding();

}
