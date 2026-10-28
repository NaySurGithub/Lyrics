#pragma once

#include "music/Service.hpp"

#include <optional>
#include <vector>

namespace lyrics::music {

// how far to shift fetched lyrics so they land on the video: each line's first word votes for the gap to where
// youtube's auto captions heard it, and the most popular gap wins. nullopt when too few words agree
[[nodiscard]] std::optional<long long> align(const std::vector<Line>& lyrics, const std::vector<Line>& heard);

}
