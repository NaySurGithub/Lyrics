#pragma once

namespace lyrics::game {

// true when the running game is the version Offsets.hpp was taken from; nothing gets hooked otherwise
[[nodiscard]] bool matches_version();

}
