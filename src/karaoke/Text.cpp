#include "karaoke/Text.hpp"

#include "render/Glyphs.hpp"

#include <algorithm>

namespace lyrics::karaoke::text {
namespace {

bool continuation(char byte) {
    return (static_cast<unsigned char>(byte) & 0xC0) == 0x80;
}

}

std::size_t code_points(std::string_view text) {
    return static_cast<std::size_t>(std::count_if(text.begin(), text.end(), [](char byte) {
        return !continuation(byte);
    }));
}

std::string_view first_code_points(std::string_view text, std::size_t count) {
    std::size_t end = 0;
    for (std::size_t seen = 0; end < text.size(); ++end) {
        if (!continuation(text[end]) && seen++ == count) {
            break;
        }
    }
    return text.substr(0, end);
}

std::size_t words_to_code_points(std::string_view text, std::size_t words) {
    if (words == 0 || words == std::string_view::npos) {
        return words;
    }
    std::size_t points = 0;
    std::size_t seen = 0;
    for (std::size_t at = 0; at < text.size(); ++at) {
        if (text[at] == ' ' && ++seen == words) {
            return points;
        }
        if (!continuation(text[at])) {
            ++points;
        }
    }
    return std::string_view::npos;
}

std::vector<std::string> wrap(std::string_view text, int width) {
    std::vector<std::string> rows;
    std::string row;
    std::size_t start = 0;
    while (start < text.size()) {
        std::size_t end = text.find(' ', start);
        if (end == std::string_view::npos) {
            end = text.size();
        }
        const std::string_view word = text.substr(start, end - start);
        start = end + 1;
        if (word.empty()) {
            continue;
        }
        std::string joined = row.empty() ? std::string(word) : row + " " + std::string(word);
        if (!row.empty() && render::glyphs::width(joined) > width) {
            rows.push_back(std::move(row));
            row = std::string(word);
        } else {
            row = std::move(joined);
        }
    }
    if (!row.empty()) {
        rows.push_back(std::move(row));
    }
    return rows;
}

std::string fit(std::string_view text, int width) {
    if (render::glyphs::width(text) <= width) {
        return std::string(text);
    }
    const int dots = render::glyphs::width("...");
    for (std::size_t length = code_points(text); length > 0; --length) {
        const std::string_view cut = first_code_points(text, length - 1);
        if (render::glyphs::width(cut) + dots <= width) {
            return std::string(cut) + "...";
        }
    }
    return "...";
}

void pop_code_point(std::string& text) {
    while (!text.empty() && continuation(text.back())) {
        text.pop_back();
    }
    if (!text.empty()) {
        text.pop_back();
    }
}

void append_code_point(std::string& text, unsigned code) {
    if (code < 0x80) {
        text.push_back(static_cast<char>(code));
    } else if (code < 0x800) {
        text.push_back(static_cast<char>(0xC0 | (code >> 6)));
        text.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    } else if (code < 0x10000) {
        text.push_back(static_cast<char>(0xE0 | (code >> 12)));
        text.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
        text.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    }
}

}
