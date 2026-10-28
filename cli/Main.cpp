#include "cli/Injector.hpp"
#include "shared/Names.hpp"
#include "shared/Settings.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdio>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace injector = lyrics::cli::injector;
namespace settings = lyrics::settings;

constexpr char kUsage[] = R"(lyrics - synced song lyrics floating in your Minecraft world

usage:
  lyrics inject                      load the overlay into the running game
  lyrics uninject                    take it back out
  lyrics status                      is the game running, is the overlay in
  lyrics config                      list every setting and its value
  lyrics config set <key> <value>    change one, the game picks it up live
  lyrics config reset                back to the defaults
  lyrics extension                   where the browser extension is and how to add it

in game, press the menu key (RightShift by default) to search a song.
)";

std::filesystem::path own_folder() {
    std::wstring buffer(MAX_PATH, L'\0');
    while (true) {
        const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length < buffer.size()) {
            buffer.resize(length);
            break;
        }
        buffer.resize(buffer.size() * 2);
    }
    return std::filesystem::path(buffer).parent_path();
}

std::string narrow(const std::wstring& text) {
    if (text.empty()) {
        return {};
    }
    const int length =
        WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), length, nullptr, nullptr);
    return out;
}

int report(const injector::Result& result) {
    std::printf("%s: %s\n", result.ok ? "ok" : "error", result.message.c_str());
    return result.ok ? 0 : 1;
}

int inject() {
    const auto dll = own_folder() / lyrics::shared::kDllName;
    if (!std::filesystem::exists(dll)) {
        std::printf("error: %s should sit next to lyrics.exe\n", narrow(lyrics::shared::kDllName).c_str());
        return 1;
    }
    return report(injector::inject(dll));
}

int status() {
    const auto state = injector::status();
    if (!state.game) {
        std::puts("minecraft is not running");
        return 0;
    }
    std::printf("minecraft is running (pid %lu), the overlay is %s\n", *state.game,
                state.loaded ? "injected" : "not injected");
    return 0;
}

int config(const std::vector<std::string>& args) {
    settings::Settings current = settings::load();
    if (args.empty()) {
        std::printf("%s\n\n", narrow(settings::file().wstring()).c_str());
        for (const settings::Field& field : settings::fields()) {
            std::printf("  %-12s %-12s %.*s\n", std::string(field.key).c_str(),
                        settings::get(current, field.key).c_str(), static_cast<int>(field.description.size()),
                        field.description.data());
        }
        return 0;
    }
    if (args[0] == "reset" && args.size() == 1) {
        if (!settings::save({})) {
            std::puts("error: could not write the settings file");
            return 1;
        }
        std::puts("ok: settings reset");
        return 0;
    }
    if (args[0] == "set" && args.size() >= 3) {
        std::string value = args[2];
        for (std::size_t index = 3; index < args.size(); ++index) {
            value += " " + args[index];
        }
        std::string error;
        if (!settings::set(current, args[1], value, error)) {
            std::printf("error: %s\n", error.c_str());
            return 1;
        }
        if (!settings::save(current)) {
            std::puts("error: could not write the settings file");
            return 1;
        }
        std::printf("ok: %s = %s\n", args[1].c_str(), settings::get(current, args[1]).c_str());
        return 0;
    }
    std::fputs(kUsage, stderr);
    return 2;
}

int extension() {
    const auto folder = own_folder() / "extension";
    std::printf("the extension is in %s\n\n", narrow(folder.wstring()).c_str());
    std::puts("it tells the overlay what YouTube is playing, so the lyrics stay on time. to add it:");
    std::puts("  1. open chrome://extensions (or edge://extensions)");
    std::puts("  2. turn on Developer mode");
    std::puts("  3. Load unpacked, and pick that folder");
    return 0;
}

}

int wmain(int argc, wchar_t** argv) {
    std::vector<std::string> args;
    for (int index = 1; index < argc; ++index) {
        args.push_back(narrow(argv[index]));
    }
    if (args.empty() || args[0] == "help" || args[0] == "--help" || args[0] == "-h") {
        std::fputs(kUsage, args.empty() ? stderr : stdout);
        return args.empty() ? 2 : 0;
    }
    const std::string command = args[0];
    args.erase(args.begin());
    if (command == "inject" && args.empty()) {
        return inject();
    }
    if (command == "uninject" && args.empty()) {
        return report(injector::uninject());
    }
    if (command == "status" && args.empty()) {
        return status();
    }
    if (command == "config") {
        return config(args);
    }
    if (command == "extension" && args.empty()) {
        return extension();
    }
    std::fputs(kUsage, stderr);
    return 2;
}
