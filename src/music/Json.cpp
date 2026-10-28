#include "music/Json.hpp"

#include <cstdlib>

namespace lyrics::music {

class Parser {
public:
    explicit Parser(std::string_view text) : text_(text) {
    }

    std::optional<Json> value(int depth = 0) {
        skip();
        if (at_ >= text_.size() || depth > 64) {
            return std::nullopt;
        }
        Json out;
        const char c = text_[at_];
        if (c == '{') {
            out.kind_ = Json::Kind::object;
            ++at_;
            skip();
            if (peek('}')) {
                return out;
            }
            while (true) {
                skip();
                auto key = string();
                skip();
                if (!key || !peek(':')) {
                    return std::nullopt;
                }
                auto item = value(depth + 1);
                if (!item) {
                    return std::nullopt;
                }
                out.fields_.insert_or_assign(*key, std::move(*item));
                skip();
                if (peek(',')) {
                    continue;
                }
                return peek('}') ? std::optional<Json>(std::move(out)) : std::nullopt;
            }
        }
        if (c == '[') {
            out.kind_ = Json::Kind::array;
            ++at_;
            skip();
            if (peek(']')) {
                return out;
            }
            while (true) {
                auto item = value(depth + 1);
                if (!item) {
                    return std::nullopt;
                }
                out.items_.push_back(std::move(*item));
                skip();
                if (peek(',')) {
                    continue;
                }
                return peek(']') ? std::optional<Json>(std::move(out)) : std::nullopt;
            }
        }
        if (c == '"') {
            auto text = string();
            if (!text) {
                return std::nullopt;
            }
            out.kind_ = Json::Kind::string;
            out.string_ = std::move(*text);
            return out;
        }
        if (text_.substr(at_, 4) == "true" || text_.substr(at_, 5) == "false") {
            out.kind_ = Json::Kind::boolean;
            out.boolean_ = text_[at_] == 't';
            at_ += out.boolean_ ? 4 : 5;
            return out;
        }
        if (text_.substr(at_, 4) == "null") {
            at_ += 4;
            return out;
        }
        const std::string number(text_.substr(at_, std::min<std::size_t>(64, text_.size() - at_)));
        char* end = nullptr;
        const double parsed = std::strtod(number.c_str(), &end);
        if (end == number.c_str()) {
            return std::nullopt;
        }
        out.kind_ = Json::Kind::number;
        out.number_ = parsed;
        at_ += static_cast<std::size_t>(end - number.c_str());
        return out;
    }

private:
    void skip() {
        while (at_ < text_.size() && (text_[at_] == ' ' || text_[at_] == '\n' || text_[at_] == '\r' || text_[at_] == '\t')) {
            ++at_;
        }
    }

    bool peek(char c) {
        if (at_ < text_.size() && text_[at_] == c) {
            ++at_;
            return true;
        }
        return false;
    }

    static void append_utf8(std::string& out, unsigned code) {
        if (code < 0x80) {
            out.push_back(static_cast<char>(code));
        } else if (code < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (code >> 6)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        } else if (code < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (code >> 12)));
            out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (code >> 18)));
            out.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        }
    }

    unsigned hex4() {
        if (at_ + 4 > text_.size()) {
            return 0xFFFD;
        }
        const std::string digits(text_.substr(at_, 4));
        at_ += 4;
        return static_cast<unsigned>(std::strtoul(digits.c_str(), nullptr, 16));
    }

    std::optional<std::string> string() {
        if (!peek('"')) {
            return std::nullopt;
        }
        std::string out;
        while (at_ < text_.size()) {
            const char c = text_[at_++];
            if (c == '"') {
                return out;
            }
            if (c != '\\' || at_ >= text_.size()) {
                out.push_back(c);
                continue;
            }
            const char escaped = text_[at_++];
            switch (escaped) {
            case 'n':
                out.push_back('\n');
                break;
            case 't':
                out.push_back('\t');
                break;
            case 'r':
                break;
            case 'b':
            case 'f':
                break;
            case 'u': {
                unsigned code = hex4();
                // a surrogate pair carries one code point above the basic plane
                if (code >= 0xD800 && code <= 0xDBFF && text_.substr(at_, 2) == "\\u") {
                    at_ += 2;
                    const unsigned low = hex4();
                    code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
                }
                append_utf8(out, code);
                break;
            }
            default:
                out.push_back(escaped);
            }
        }
        return std::nullopt;
    }

    std::string_view text_;
    std::size_t at_ = 0;
};

std::optional<Json> Json::parse(std::string_view text) {
    return Parser(text).value();
}

const Json& Json::operator[](std::string_view key) const {
    static const Json missing;
    const auto found = fields_.find(key);
    return found == fields_.end() ? missing : found->second;
}

const Json& Json::operator[](std::size_t index) const {
    static const Json missing;
    return index < items_.size() ? items_[index] : missing;
}

std::size_t Json::size() const {
    return kind_ == Kind::array ? items_.size() : fields_.size();
}

std::string Json::string(std::string_view fallback) const {
    return kind_ == Kind::string ? string_ : std::string(fallback);
}

double Json::number(double fallback) const {
    return kind_ == Kind::number ? number_ : fallback;
}

bool Json::boolean(bool fallback) const {
    return kind_ == Kind::boolean ? boolean_ : fallback;
}

std::string url_encode(std::string_view text) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string out;
    for (const char c : text) {
        const auto byte = static_cast<unsigned char>(c);
        if ((byte >= 'A' && byte <= 'Z') || (byte >= 'a' && byte <= 'z') || (byte >= '0' && byte <= '9') || byte == '-' ||
            byte == '_' || byte == '.' || byte == '~') {
            out.push_back(c);
        } else {
            out.push_back('%');
            out.push_back(kHex[byte >> 4]);
            out.push_back(kHex[byte & 15]);
        }
    }
    return out;
}

}
