#include "music/Service.hpp"

#include "music/Json.hpp"
#include "net/Http.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <regex>
#include <sstream>
#include <thread>

namespace lyrics::music {
namespace {

constexpr std::string_view kUserAgent = "Lyrics/1.0";
// youtube only serves the results page to something that looks like a browser
constexpr std::string_view kBrowserAgent =
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/128.0.0.0 Safari/537.36";
constexpr int kAttempts = 4;

// lrclib answers 503 whenever it's overloaded, which is often; backing off gets through most of the time
std::optional<net::Response> get_with_retry(const std::string& url) {
    for (int attempt = 0; attempt < kAttempts; ++attempt) {
        auto response = net::get(url, kUserAgent);
        if (response && response->status != 503 && response->status != 429) {
            return response;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(1500.0 * std::pow(2.0, attempt))));
    }
    return std::nullopt;
}

std::string lower(std::string_view text) {
    std::string out;
    for (const char c : text) {
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return out;
}

std::filesystem::path cache_file(const Track& track) {
    std::string name = track.artist + " - " + track.title;
    for (char& c : name) {
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') {
            c = '_';
        }
    }
    auto folder = net::cache_dir() / "lyrics";
    std::error_code error;
    std::filesystem::create_directories(folder, error);
    const std::string file = name + ".lrc";
    return folder / std::filesystem::path(std::u8string(file.begin(), file.end()));
}

std::string trim(std::string text) {
    const auto start = text.find_first_not_of(" \t-|");
    const auto end = text.find_last_not_of(" \t-|");
    return start == std::string::npos ? std::string() : text.substr(start, end - start + 1);
}

}

std::vector<Track> search(std::string_view query) {
    std::vector<Track> out;
    const auto response = net::get("https://api.deezer.com/search?limit=8&q=" + url_encode(query), kUserAgent);
    if (!response || response->status != 200) {
        return out;
    }
    const auto json = Json::parse(response->body);
    if (!json) {
        return out;
    }
    const Json& data = (*json)["data"];
    for (std::size_t index = 0; index < data.size(); ++index) {
        const Json& item = data[index];
        Track track;
        track.title = item["title"].string();
        track.artist = item["artist"]["name"].string();
        track.album = item["album"]["title"].string();
        track.cover = item["album"]["cover_small"].string();
        track.duration = static_cast<int>(item["duration"].number());
        if (!track.title.empty()) {
            out.push_back(std::move(track));
        }
    }
    return out;
}

std::optional<std::string> youtube_video(const Track& track) {
    const auto response = net::get(
        "https://www.youtube.com/results?search_query=" + url_encode(track.artist + " " + track.title), kBrowserAgent);
    if (!response || response->status != 200) {
        return std::nullopt;
    }
    static const std::regex video(R"rx("videoId":"([A-Za-z0-9_-]{11})")rx");
    std::smatch match;
    if (std::regex_search(response->body, match, video)) {
        return match[1].str();
    }
    return std::nullopt;
}

std::vector<Line> parse_lrc(std::string_view lrc) {
    std::vector<Line> out;
    static const std::regex stamp(R"(\[(\d+):(\d+(?:\.\d+)?)\])");
    std::istringstream stream{std::string(lrc)};
    std::string raw;
    while (std::getline(stream, raw)) {
        std::vector<long long> stamps;
        auto begin = std::sregex_iterator(raw.begin(), raw.end(), stamp);
        std::size_t text_from = 0;
        for (auto it = begin; it != std::sregex_iterator(); ++it) {
            stamps.push_back(std::stoll((*it)[1]) * 60000LL + static_cast<long long>(std::stod((*it)[2]) * 1000.0));
            text_from = static_cast<std::size_t>(it->position() + it->length());
        }
        std::string text = raw.substr(text_from);
        while (!text.empty() && (text.back() == '\r' || text.back() == ' ')) {
            text.pop_back();
        }
        for (const long long ms : stamps) {
            out.push_back({ms, text, {}});
        }
    }
    std::stable_sort(out.begin(), out.end(), [](const Line& a, const Line& b) {
        return a.ms < b.ms;
    });
    return out;
}

std::vector<Line> fetch_lyrics(const Track& track) {
    const auto file = cache_file(track);
    if (std::ifstream cached{file, std::ios::binary}) {
        std::stringstream text;
        text << cached.rdbuf();
        return parse_lrc(text.str());
    }
    std::string synced;
    std::string url = "https://lrclib.net/api/get?artist_name=" + url_encode(track.artist) +
                      "&track_name=" + url_encode(track.title);
    if (!track.album.empty()) {
        url += "&album_name=" + url_encode(track.album);
    }
    if (track.duration > 0) {
        url += "&duration=" + std::to_string(track.duration);
    }
    if (const auto response = get_with_retry(url); response && response->status == 200) {
        if (const auto json = Json::parse(response->body)) {
            synced = (*json)["syncedLyrics"].string();
        }
    }
    if (synced.empty()) {
        const auto response = get_with_retry("https://lrclib.net/api/search?track_name=" + url_encode(track.title) +
                                             "&artist_name=" + url_encode(track.artist));
        if (response && response->status == 200) {
            if (const auto json = Json::parse(response->body)) {
                for (std::size_t index = 0; index < json->size() && synced.empty(); ++index) {
                    synced = (*json)[index]["syncedLyrics"].string();
                }
            }
        }
    }
    if (synced.empty()) {
        return {};
    }
    std::ofstream(file, std::ios::binary) << synced;
    return parse_lrc(synced);
}

std::optional<std::string> download(const std::string& url) {
    const auto response = net::get(url, kUserAgent);
    if (!response || response->status != 200) {
        return std::nullopt;
    }
    return response->body;
}

Track from_video_title(std::string_view title, std::string_view channel) {
    static const std::regex noise(R"rx(\s*[\(\[][^\)\]]*(official|clip|video|audio|lyric|paroles|visualizer|hd|4k)[^\)\]]*[\)\]])rx",
                                  std::regex::icase);
    static const std::regex featuring(R"(\s+(feat\.?|ft\.?)\s+.*$)", std::regex::icase);
    std::string cleaned = std::regex_replace(std::string(title), noise, "");
    Track track;
    if (const auto dash = cleaned.find(" - "); dash != std::string::npos) {
        track.artist = trim(cleaned.substr(0, dash));
        track.title = trim(std::regex_replace(cleaned.substr(dash + 3), featuring, ""));
    } else {
        std::string artist(channel);
        if (const auto topic = lower(artist).rfind(" - topic"); topic != std::string::npos) {
            artist.erase(topic);
        }
        track.artist = trim(artist);
        track.title = trim(std::regex_replace(cleaned, featuring, ""));
    }
    return track;
}

}
