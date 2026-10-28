#pragma once

#include "music/Service.hpp"
#include "shared/Settings.hpp"

#include <imgui.h>

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace lyrics::karaoke {

void start();
void stop();
// once per overlay frame, on the render thread
void render();

// search a song, play it on YouTube in the browser and read its synced lyrics floating in front of you. a small
// browser extension reports the video's position back on 127.0.0.1
class Karaoke {
public:
    Karaoke();
    ~Karaoke();
    Karaoke(const Karaoke&) = delete;
    Karaoke& operator=(const Karaoke&) = delete;

    void render();

private:
    using Clock = std::chrono::steady_clock;

    // what the extension last said about the playing video
    struct Playback {
        std::string video;
        std::string title;
        std::string channel;
        long long position_ms = 0;
        long long duration_ms = 0;
        bool playing = false;
        Clock::time_point received{};
    };

    // the video's own captions from the extension; settle() weighs them against what LRCLIB gave
    struct Captions {
        std::string video;
        // a hand-made track known to be in the sung language, not a guess or the auto captions
        bool trusted = false;
        std::vector<music::Line> lines;
        std::vector<music::Line> words;
    };

    // a sung line pinned where you were looking when it started, and the one before it shrinking away
    struct Placed {
        int line = -1;
        float anchor[3]{};
        float right[3]{};
        Clock::time_point at{};
        // sky lines hang at a fixed direction like the sun, so only where you look moves them
        bool sky = false;
        float direction[3]{};
    };

    void reload_settings();
    void start_serving();
    void run(std::function<void()> task);
    void work();
    void search(std::string query);
    void play(music::Track track);
    void load_lyrics(music::Track track);
    void settle();
    std::string on_request(std::string_view method, std::string_view path, std::string_view body);
    [[nodiscard]] std::optional<long long> position_ms() const;

    void render_panel();
    void edit_query();
    void render_world();
    bool place(Placed& placed, int line, const Clock::time_point& now) const;
    void track_camera(const Clock::time_point& now);
    void draw_placed(ImDrawList* draw, const Placed& placed, std::string_view text, std::size_t visible, float shrink,
                     float alpha) const;

    settings::Settings settings_;
    std::filesystem::file_time_type settings_written_{};
    Clock::time_point settings_checked_{};

    mutable std::mutex mutex_;
    std::vector<music::Track> results_;
    bool searching_ = false;
    std::optional<music::Track> current_;
    std::string current_video_;
    std::string pending_open_;
    std::vector<music::Line> lines_;
    // a few videos deep, a second tab must not wipe the first one's
    std::deque<Captions> captions_;
    std::vector<music::Line> fetched_;
    bool fetching_ = false;
    std::string status_;
    std::optional<Playback> playback_;
    // covers already asked for, and the raw downloads waiting for the render thread to put them in the atlas
    std::map<std::string, bool> covers_;
    std::deque<std::pair<std::string, std::string>> cover_downloads_;
    int search_generation_ = 0;

    std::string query_;
    std::string submitted_query_;
    Clock::time_point typed_at_{};

    int shown_line_ = -1;
    Placed sung_;
    Placed leaving_;
    float last_camera_[3]{};
    bool camera_seen_ = false;
    Clock::time_point camera_at_{};
    // the camera's smoothed speed in blocks per second, and whether lines lead along it this frame
    float velocity_[3]{};
    bool lead_ = false;

    std::thread worker_;
    std::mutex queue_mutex_;
    std::condition_variable queue_ready_;
    std::deque<std::function<void()>> queue_;
    std::atomic_bool stopping_{false};
    bool serving_ = false;
    Clock::time_point serve_tried_{};
};

}
