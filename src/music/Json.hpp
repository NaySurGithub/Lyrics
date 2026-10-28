#pragma once

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lyrics::music {

// just enough json for the api answers here: objects, arrays, strings, numbers, bools, null
class Json {
public:
    enum class Kind { null, boolean, number, string, array, object };

    [[nodiscard]] static std::optional<Json> parse(std::string_view text);

    [[nodiscard]] Kind kind() const {
        return kind_;
    }
    [[nodiscard]] const Json& operator[](std::string_view key) const;
    [[nodiscard]] const Json& operator[](std::size_t index) const;
    [[nodiscard]] std::size_t size() const;
    [[nodiscard]] std::string string(std::string_view fallback = {}) const;
    [[nodiscard]] double number(double fallback = 0.0) const;
    [[nodiscard]] bool boolean(bool fallback = false) const;
    [[nodiscard]] bool is_null() const {
        return kind_ == Kind::null;
    }

private:
    friend class Parser;
    Kind kind_ = Kind::null;
    bool boolean_ = false;
    double number_ = 0.0;
    std::string string_;
    std::vector<Json> items_;
    std::map<std::string, Json, std::less<>> fields_;
};

// percent-encodes a query parameter
[[nodiscard]] std::string url_encode(std::string_view text);

}
