#pragma once

#include <array>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>

// the overlay's settings: a plain key=value file the cli edits and the dll picks up while running
namespace lyrics::settings {

enum class Mode { normal, sky };
// where a song picked in game opens: the system default, or the browser the extension is installed in
enum class Browser { system, edge, chrome };

struct Settings {
    Mode mode = Mode::normal;
    float distance = 18.0F;
    float sky_angle = 25.0F;
    float height = 2.5F;
    float scale = 1.0F;
    float time_offset = 120.0F;
    std::array<float, 4> text_color{1.0F, 0.93F, 0.8F, 1.0F};
    bool typewriter = true;
    bool shadow = false;
    bool elytra_lead = true;
    // a windows virtual key code
    int menu_key = 0xA1;
    Browser browser = Browser::system;
};

struct Field {
    std::string_view key;
    // what values it takes, as shown to the user
    std::string_view takes;
    std::string_view description;
    std::string_view example;
};

[[nodiscard]] std::span<const Field> fields();

// %LOCALAPPDATA%\Lyrics, created on first use
[[nodiscard]] std::filesystem::path data_folder();
[[nodiscard]] std::filesystem::path file();

// defaults for anything missing or unreadable
[[nodiscard]] Settings load();
bool save(const Settings& settings);

[[nodiscard]] std::string get(const Settings& settings, std::string_view key);
// false with the reason in `error` when the key is unknown or the value doesn't fit it
bool set(Settings& settings, std::string_view key, std::string_view value, std::string& error);

[[nodiscard]] std::optional<int> key_code(std::string_view name);
[[nodiscard]] std::string key_name(int code);

}
