#include "music/Align.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <string>
#include <string_view>

namespace lyrics::music {
namespace {

constexpr long long kSearchMs = 45000;
constexpr long long kBinMs = 200;
constexpr std::size_t kMinVotes = 3;

// lowercase letters and digits only, so "Don't," and "dont" meet
std::string normalize(std::string_view word) {
    std::string out;
    for (const char c : word) {
        const auto byte = static_cast<unsigned char>(c);
        if (std::isalnum(byte) != 0 || byte >= 0x80) {
            out.push_back(static_cast<char>(std::tolower(byte)));
        }
    }
    return out;
}

std::string first_word(std::string_view text) {
    std::size_t start = 0;
    while (start < text.size()) {
        const std::size_t end = std::min(text.find(' ', start), text.size());
        std::string word = normalize(text.substr(start, end - start));
        if (word.size() >= 2) {
            return word;
        }
        start = end + 1;
    }
    return {};
}

}

std::optional<long long> align(const std::vector<Line>& lyrics, const std::vector<Line>& heard) {
    std::multimap<std::string, long long> spoken;
    for (const Line& word : heard) {
        if (std::string key = normalize(word.text); !key.empty()) {
            spoken.emplace(std::move(key), word.ms);
        }
    }
    std::map<long long, std::size_t> bins;
    std::vector<long long> votes;
    std::size_t sung = 0;
    for (const Line& line : lyrics) {
        const std::string word = first_word(line.text);
        if (word.empty()) {
            continue;
        }
        ++sung;
        const auto [from, to] = spoken.equal_range(word);
        for (auto it = from; it != to; ++it) {
            const long long gap = it->second - line.ms;
            if (gap > -kSearchMs && gap < kSearchMs) {
                ++bins[gap / kBinMs];
                votes.push_back(gap);
            }
        }
    }
    if (bins.empty()) {
        return std::nullopt;
    }
    // neighbouring bins count together, a true gap often straddles two
    long long best_bin = 0;
    std::size_t best = 0;
    for (const auto& [bin, count] : bins) {
        const auto around = [&bins](long long at) {
            const auto found = bins.find(at);
            return found == bins.end() ? std::size_t{0} : found->second;
        };
        const std::size_t total = count + around(bin - 1) + around(bin + 1);
        if (total > best) {
            best = total;
            best_bin = bin;
        }
    }
    if (best < std::max(kMinVotes, sung / 5)) {
        return std::nullopt;
    }
    long long sum = 0;
    std::size_t count = 0;
    for (const long long gap : votes) {
        if (gap / kBinMs >= best_bin - 1 && gap / kBinMs <= best_bin + 1) {
            sum += gap;
            ++count;
        }
    }
    return sum / static_cast<long long>(count);
}

}
