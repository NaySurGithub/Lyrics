#include "net/Http.hpp"

#include "shared/Settings.hpp"

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <shellapi.h>
#include <winhttp.h>

#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

namespace lyrics::net {
namespace {

std::wstring widen(std::string_view text) {
    if (text.empty()) {
        return {};
    }
    const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring out(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), length);
    return out;
}

struct Handle {
    HINTERNET value = nullptr;
    ~Handle() {
        if (value != nullptr) {
            WinHttpCloseHandle(value);
        }
    }
};

std::atomic_bool serving{false};
std::thread server_thread;
SOCKET listener = INVALID_SOCKET;
std::mutex server_mutex;

std::string answer(int status, const std::string& body) {
    std::string reason = status == 200 ? "OK" : (status == 204 ? "No Content" : "Bad Request");
    std::string out = "HTTP/1.1 " + std::to_string(status) + " " + reason + "\r\n";
    // the browser extension posts from its own origin; preflights also ask for private network access
    out += "Access-Control-Allow-Origin: *\r\nAccess-Control-Allow-Methods: GET, POST, OPTIONS\r\n";
    out += "Access-Control-Allow-Headers: Content-Type\r\nAccess-Control-Allow-Private-Network: true\r\n";
    out += "Content-Type: application/json\r\nConnection: close\r\nContent-Length: " + std::to_string(body.size()) + "\r\n\r\n";
    return out + body;
}

void serve_client(SOCKET client, const Handler& handler) {
    std::string request;
    char buffer[4096];
    std::size_t header_end = std::string::npos;
    std::size_t expected = 0;
    while (true) {
        const int received = recv(client, buffer, sizeof(buffer), 0);
        if (received <= 0) {
            break;
        }
        request.append(buffer, static_cast<std::size_t>(received));
        if (header_end == std::string::npos && (header_end = request.find("\r\n\r\n")) != std::string::npos) {
            std::string lower;
            for (const char c : request.substr(0, header_end)) {
                lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
            }
            if (const auto at = lower.find("content-length:"); at != std::string::npos) {
                expected = std::strtoul(lower.c_str() + at + 15, nullptr, 10);
            }
        }
        if (header_end != std::string::npos && request.size() >= header_end + 4 + expected) {
            break;
        }
        if (request.size() > 1 << 20) {
            break;
        }
    }
    std::string response;
    if (header_end == std::string::npos) {
        response = answer(400, "{}");
    } else {
        const std::size_t method_end = request.find(' ');
        const std::size_t path_end = request.find(' ', method_end + 1);
        const std::string_view method(request.data(), method_end);
        const std::string_view path(request.data() + method_end + 1, path_end - method_end - 1);
        if (method == "OPTIONS") {
            response = answer(204, "");
        } else {
            response = answer(200, handler(method, path, std::string_view(request).substr(header_end + 4, expected)));
        }
    }
    send(client, response.data(), static_cast<int>(response.size()), 0);
    closesocket(client);
}

}

std::optional<Response> get(const std::string& url, std::string_view user_agent) {
    const std::wstring wide_url = widen(url);
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    wchar_t host[256]{};
    wchar_t path[4096]{};
    parts.lpszHostName = host;
    parts.dwHostNameLength = 256;
    parts.lpszUrlPath = path;
    parts.dwUrlPathLength = 4096;
    wchar_t extra[4096]{};
    parts.lpszExtraInfo = extra;
    parts.dwExtraInfoLength = 4096;
    if (!WinHttpCrackUrl(wide_url.c_str(), 0, 0, &parts)) {
        return std::nullopt;
    }
    Handle session{WinHttpOpen(widen(user_agent).c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                               WINHTTP_NO_PROXY_BYPASS, 0)};
    if (session.value == nullptr) {
        return std::nullopt;
    }
    WinHttpSetTimeouts(session.value, 5000, 5000, 10000, 10000);
    Handle connection{WinHttpConnect(session.value, host, parts.nPort, 0)};
    const std::wstring object = std::wstring(path) + extra;
    const DWORD flags = parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
    Handle request{connection.value == nullptr ? nullptr
                                               : WinHttpOpenRequest(connection.value, L"GET", object.c_str(), nullptr,
                                                                    WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags)};
    if (request.value == nullptr || !WinHttpSendRequest(request.value, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request.value, nullptr)) {
        return std::nullopt;
    }
    Response response;
    DWORD status = 0;
    DWORD size = sizeof(status);
    WinHttpQueryHeaders(request.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                        &status, &size, WINHTTP_NO_HEADER_INDEX);
    response.status = static_cast<int>(status);
    DWORD available = 0;
    while (WinHttpQueryDataAvailable(request.value, &available) && available > 0) {
        std::string chunk(available, '\0');
        DWORD read = 0;
        if (!WinHttpReadData(request.value, chunk.data(), available, &read) || read == 0) {
            break;
        }
        response.body.append(chunk.data(), read);
    }
    return response;
}

bool open_url(const std::string& url, std::wstring_view executable) {
    const std::wstring wide = widen(url);
    if (!executable.empty()) {
        // installed browsers register their full path under App Paths, per user or for the machine
        const std::wstring key = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Paths\\" + std::wstring(executable);
        wchar_t path[MAX_PATH]{};
        DWORD size = sizeof(path);
        for (HKEY root : {HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE}) {
            size = sizeof(path);
            if (RegGetValueW(root, key.c_str(), nullptr, RRF_RT_REG_SZ, nullptr, path, &size) == ERROR_SUCCESS) {
                const std::wstring argument = L"\"" + wide + L"\"";
                if (reinterpret_cast<std::intptr_t>(
                        ShellExecuteW(nullptr, L"open", path, argument.c_str(), nullptr, SW_SHOWNORMAL)) > 32) {
                    return true;
                }
            }
        }
        // edge also answers its own url scheme on every windows 10 / 11
        if (executable == L"msedge.exe" &&
            reinterpret_cast<std::intptr_t>(ShellExecuteW(nullptr, L"open", (L"microsoft-edge:" + wide).c_str(), nullptr,
                                                          nullptr, SW_SHOWNORMAL)) > 32) {
            return true;
        }
    }
    // under wine this lands in winebrowser, which hands it to the mac's default browser
    return reinterpret_cast<std::intptr_t>(ShellExecuteW(nullptr, L"open", wide.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) > 32;
}

std::filesystem::path cache_dir() {
    std::filesystem::path root = settings::data_folder() / L"cache";
    std::error_code error;
    std::filesystem::create_directories(root, error);
    return root;
}

bool serve(std::uint16_t port, Handler handler) {
    std::lock_guard lock(server_mutex);
    if (serving) {
        return true;
    }
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
        return false;
    }
    listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == INVALID_SOCKET) {
        return false;
    }
    BOOL reuse = TRUE;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 || listen(listener, 8) != 0) {
        closesocket(listener);
        listener = INVALID_SOCKET;
        return false;
    }
    serving = true;
    server_thread = std::thread([handler = std::move(handler)] {
        while (serving) {
            fd_set ready;
            FD_ZERO(&ready);
            FD_SET(listener, &ready);
            timeval wait{0, 200000};
            if (select(0, &ready, nullptr, nullptr, &wait) <= 0) {
                continue;
            }
            const SOCKET client = accept(listener, nullptr, nullptr);
            if (client != INVALID_SOCKET) {
                // a client that never finishes its request can't hold the thread, stop_serving waits on it
                const DWORD timeout = 2000;
                setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
                serve_client(client, handler);
            }
        }
    });
    return true;
}

void stop_serving() {
    std::lock_guard lock(server_mutex);
    if (!serving) {
        return;
    }
    serving = false;
    if (server_thread.joinable()) {
        server_thread.join();
    }
    closesocket(listener);
    listener = INVALID_SOCKET;
}

}
