#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

// utf-8 helpers the karaoke counts typed letters and wraps lines with
namespace lyrics::karaoke::text {

[[nodiscard]] std::size_t code_points(std::string_view text);
[[nodiscard]] std::string_view first_code_points(std::string_view text, std::size_t count);
// how many code points the first `words` space separated words take, npos for all of them
[[nodiscard]] std::size_t words_to_code_points(std::string_view text, std::size_t words);
// greedy word wrap at `width` font pixels, a word longer than that keeps a row to itself
[[nodiscard]] std::vector<std::string> wrap(std::string_view text, int width);
// cut down to `width` font pixels with an ellipsis, so nothing spills out of its row
[[nodiscard]] std::string fit(std::string_view text, int width);
void pop_code_point(std::string& text);
void append_code_point(std::string& text, unsigned code);

}
