#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace lyrics::net {

struct Response {
    int status = 0;
    std::string body;
};

// a blocking https GET; call it from a worker thread. nullopt when the request never got an answer
[[nodiscard]] std::optional<Response> get(const std::string& url, std::string_view user_agent);
// hands the url to the system's default browser
bool open_url(const std::string& url);
// a writable folder for cached downloads
[[nodiscard]] std::filesystem::path cache_dir();

// a loopback-only http server on its own thread; the handler gets the method, path and body of each request and
// returns the json body to answer with. false when the port couldn't be bound or the platform has no server
using Handler = std::function<std::string(std::string_view method, std::string_view path, std::string_view body)>;
bool serve(std::uint16_t port, Handler handler);
void stop_serving();

}
