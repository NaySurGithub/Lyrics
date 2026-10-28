#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace lyrics::cli::injector {

struct Result {
    bool ok = false;
    std::string message;
};

struct Status {
    std::optional<unsigned long> game;
    bool loaded = false;
};

[[nodiscard]] Status status();
Result inject(const std::filesystem::path& dll);
Result uninject();

}
