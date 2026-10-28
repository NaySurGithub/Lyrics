#include "shared/Settings.hpp"

#include "shared/Names.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace lyrics::settings {
namespace {

constexpr Field kFields[]{
    {"mode", "normal: each line floats in front of you and stays put / sky: lines hang in the sky like the sun"},
    {"distance", "how far in front of you the lyrics float, in blocks (4 to 40, normal mode)"},
    {"sky_angle", "how high above the horizon the lyrics hang, in degrees (5 to 80, sky mode)"},
    {"height", "how high above your eyes the lyrics float, in blocks (-5 to 10, normal mode)"},
    {"scale", "size of the lyric text (0.25 to 3)"},
    {"time_offset", "shifts the lyrics earlier (positive) or later (negative) against the music, in ms"},
    {"text_color", "colour of the lyric text, #RRGGBB or #RRGGBBAA"},
    {"typewriter", "types each line out letter by letter as it starts (true / false)"},
    {"shadow", "Minecraft's drop shadow under the text (true / false)"},
    {"elytra_lead", "puts the lyrics ahead along your flight path while gliding fast (true / false)"},
    {"menu_key", "the key that opens the song search in game, e.g. RightShift, F8, Insert, L"},
};

struct KeyName {
    std::string_view name;
    int code;
};

constexpr KeyName kKeys[]{
    {"RightShift", VK_RSHIFT}, {"LeftShift", VK_LSHIFT}, {"RightControl", VK_RCONTROL}, {"LeftControl", VK_LCONTROL},
    {"RightAlt", VK_RMENU},    {"LeftAlt", VK_LMENU},    {"Insert", VK_INSERT},          {"Delete", VK_DELETE},
    {"Home", VK_HOME},         {"End", VK_END},          {"PageUp", VK_PRIOR},           {"PageDown", VK_NEXT},
    {"Tab", VK_TAB},           {"CapsLock", VK_CAPITAL}, {"Backquote", VK_OEM_3},
};

std::string lower(std::string_view text) {
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return out;
}

std::string_view trim(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) {
        text.remove_prefix(1);
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) {
        text.remove_suffix(1);
    }
    return text;
}

std::optional<float> number(std::string_view text) {
    float value = 0.0F;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    return error == std::errc{} && end == text.data() + text.size() ? std::optional(value) : std::nullopt;
}

std::optional<bool> boolean(std::string_view text) {
    const std::string value = lower(text);
    if (value == "true" || value == "on" || value == "yes" || value == "1") {
        return true;
    }
    if (value == "false" || value == "off" || value == "no" || value == "0") {
        return false;
    }
    return std::nullopt;
}

std::optional<std::array<float, 4>> color(std::string_view text) {
    if (text.starts_with('#')) {
        text.remove_prefix(1);
    }
    if (text.size() != 6 && text.size() != 8) {
        return std::nullopt;
    }
    std::array<float, 4> out{0.0F, 0.0F, 0.0F, 1.0F};
    for (std::size_t channel = 0; channel * 2 < text.size(); ++channel) {
        unsigned value = 0;
        const auto [end, error] = std::from_chars(text.data() + channel * 2, text.data() + channel * 2 + 2, value, 16);
        if (error != std::errc{} || end != text.data() + channel * 2 + 2) {
            return std::nullopt;
        }
        out[channel] = static_cast<float>(value) / 255.0F;
    }
    return out;
}

std::string format_number(float value) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%g", value);
    return buffer;
}

bool in_range(float value, float low, float high, std::string_view key, std::string& error) {
    if (value >= low && value <= high) {
        return true;
    }
    error = std::string(key) + " goes from " + format_number(low) + " to " + format_number(high);
    return false;
}

}

std::span<const Field> fields() {
    return kFields;
}

std::filesystem::path data_folder() {
    wchar_t buffer[MAX_PATH]{};
    const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, MAX_PATH);
    std::filesystem::path root =
        length > 0 && length < MAX_PATH ? std::filesystem::path(buffer) : std::filesystem::temp_directory_path();
    root /= shared::kDataFolder;
    std::error_code error;
    std::filesystem::create_directories(root, error);
    return root;
}

std::filesystem::path file() {
    return data_folder() / shared::kSettingsFile;
}

Settings load() {
    Settings settings;
    std::ifstream in(file());
    std::string line;
    std::string ignored;
    while (std::getline(in, line)) {
        const std::string_view text = trim(line);
        const std::size_t equals = text.find('=');
        if (text.empty() || text.starts_with('#') || equals == std::string_view::npos) {
            continue;
        }
        (void)set(settings, trim(text.substr(0, equals)), trim(text.substr(equals + 1)), ignored);
    }
    return settings;
}

bool save(const Settings& settings) {
    std::ostringstream out;
    out << "# Lyrics overlay settings. edit them with `lyrics config set <key> <value>`, the game picks changes up live\n";
    for (const Field& field : kFields) {
        out << "\n# " << field.description << "\n" << field.key << " = " << get(settings, field.key) << "\n";
    }
    std::ofstream file_out(file(), std::ios::trunc);
    file_out << out.str();
    return static_cast<bool>(file_out);
}

std::string get(const Settings& settings, std::string_view key) {
    if (key == "mode") {
        return settings.mode == Mode::sky ? "sky" : "normal";
    }
    if (key == "distance") {
        return format_number(settings.distance);
    }
    if (key == "sky_angle") {
        return format_number(settings.sky_angle);
    }
    if (key == "height") {
        return format_number(settings.height);
    }
    if (key == "scale") {
        return format_number(settings.scale);
    }
    if (key == "time_offset") {
        return format_number(settings.time_offset);
    }
    if (key == "text_color") {
        char buffer[16];
        const auto& c = settings.text_color;
        const auto byte = [](float value) {
            return static_cast<unsigned>(std::clamp(value, 0.0F, 1.0F) * 255.0F + 0.5F);
        };
        std::snprintf(buffer, sizeof(buffer), "#%02X%02X%02X%02X", byte(c[0]), byte(c[1]), byte(c[2]), byte(c[3]));
        return buffer;
    }
    if (key == "typewriter") {
        return settings.typewriter ? "true" : "false";
    }
    if (key == "shadow") {
        return settings.shadow ? "true" : "false";
    }
    if (key == "elytra_lead") {
        return settings.elytra_lead ? "true" : "false";
    }
    if (key == "menu_key") {
        return key_name(settings.menu_key);
    }
    return {};
}

bool set(Settings& settings, std::string_view key, std::string_view value, std::string& error) {
    value = trim(value);
    const auto bad = [&](std::string_view expected) {
        error = std::string(key) + " takes " + std::string(expected);
        return false;
    };
    const auto ranged = [&](float& target, float low, float high) {
        const auto parsed = number(value);
        if (!parsed) {
            return bad("a number");
        }
        if (!in_range(*parsed, low, high, key, error)) {
            return false;
        }
        target = *parsed;
        return true;
    };
    const auto toggle = [&](bool& target) {
        const auto parsed = boolean(value);
        if (!parsed) {
            return bad("true or false");
        }
        target = *parsed;
        return true;
    };
    if (key == "mode") {
        const std::string mode = lower(value);
        if (mode != "normal" && mode != "sky") {
            return bad("normal or sky");
        }
        settings.mode = mode == "sky" ? Mode::sky : Mode::normal;
        return true;
    }
    if (key == "distance") {
        return ranged(settings.distance, 4.0F, 40.0F);
    }
    if (key == "sky_angle") {
        return ranged(settings.sky_angle, 5.0F, 80.0F);
    }
    if (key == "height") {
        return ranged(settings.height, -5.0F, 10.0F);
    }
    if (key == "scale") {
        return ranged(settings.scale, 0.25F, 3.0F);
    }
    if (key == "time_offset") {
        return ranged(settings.time_offset, -2000.0F, 2000.0F);
    }
    if (key == "text_color") {
        const auto parsed = color(value);
        if (!parsed) {
            return bad("#RRGGBB or #RRGGBBAA");
        }
        settings.text_color = *parsed;
        return true;
    }
    if (key == "typewriter") {
        return toggle(settings.typewriter);
    }
    if (key == "shadow") {
        return toggle(settings.shadow);
    }
    if (key == "elytra_lead") {
        return toggle(settings.elytra_lead);
    }
    if (key == "menu_key") {
        const auto code = key_code(value);
        if (!code) {
            return bad("a key name like RightShift, F8, Insert or L");
        }
        settings.menu_key = *code;
        return true;
    }
    error = "unknown setting " + std::string(key);
    return false;
}

std::optional<int> key_code(std::string_view name) {
    const std::string wanted = lower(trim(name));
    for (const KeyName& key : kKeys) {
        if (lower(key.name) == wanted) {
            return key.code;
        }
    }
    if (wanted.size() == 1 && std::isalnum(static_cast<unsigned char>(wanted[0]))) {
        return std::toupper(static_cast<unsigned char>(wanted[0]));
    }
    if (wanted.size() >= 2 && wanted[0] == 'f') {
        int index = 0;
        const auto [end, error] = std::from_chars(wanted.data() + 1, wanted.data() + wanted.size(), index);
        if (error == std::errc{} && end == wanted.data() + wanted.size() && index >= 1 && index <= 12) {
            return VK_F1 + index - 1;
        }
    }
    return std::nullopt;
}

std::string key_name(int code) {
    for (const KeyName& key : kKeys) {
        if (key.code == code) {
            return std::string(key.name);
        }
    }
    if (code >= VK_F1 && code <= VK_F12) {
        return "F" + std::to_string(code - VK_F1 + 1);
    }
    if ((code >= '0' && code <= '9') || (code >= 'A' && code <= 'Z')) {
        return std::string(1, static_cast<char>(code));
    }
    return "RightShift";
}

}
