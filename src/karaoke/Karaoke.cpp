#include "karaoke/Karaoke.hpp"

#include "game/Camera.hpp"
#include "game/Player.hpp"
#include "karaoke/Text.hpp"
#include "music/Align.hpp"
#include "music/Json.hpp"
#include "net/Http.hpp"
#include "render/Covers.hpp"
#include "render/Glyphs.hpp"
#include "render/Overlay.hpp"

#include <algorithm>
#include <cmath>
#include <memory>
#include <utility>

namespace lyrics::karaoke {
namespace {

// the port and payload LyricsPlus's bridge uses, so its companion tools talk to this one too
constexpr std::uint16_t kBridgePort = 8974;
constexpr auto kPlaybackFresh = std::chrono::milliseconds(3000);
constexpr auto kServeRetry = std::chrono::seconds(2);
constexpr auto kSettingsPoll = std::chrono::seconds(1);
// tenths of a second a youtube tab gets to claim a song before it opens in a new one
constexpr int kHandOffWait = 20;
// one font pixel at scale 1, in blocks
constexpr float kPixel = 0.18F;
constexpr int kLineHeight = 12;
// a sung line wraps past this many font pixels, two rows for most lines
constexpr int kWrapWidth = 100;
constexpr double kTypeMsPerChar = 35.0;
constexpr double kLeaveMs = 220.0;
// blocks the camera can land away from where its speed said it would before it counts as a jump
constexpr float kCameraJump = 1.5F;
constexpr float kVelocitySmoothing = 0.15F;
// gliding faster than this, in blocks per second, puts the lines on the flight path this many seconds ahead
constexpr float kLeadSpeed = 10.0F;
constexpr float kLeadSeconds = 1.5F;
// sky lines sit this far out, scaled up so they look as big as a normal line at 18 blocks
constexpr float kSkyDistance = 300.0F;
constexpr float kSkyScale = kSkyDistance / 18.0F;

std::unique_ptr<Karaoke> instance;

bool same_track(const music::Track& a, const music::Track& b) {
    return a.title == b.title && a.artist == b.artist;
}

std::vector<music::Line> read_lines(const music::Json& list) {
    std::vector<music::Line> out;
    for (std::size_t index = 0; index < list.size(); ++index) {
        music::Line line{static_cast<long long>(list[index]["ms"].number()), list[index]["text"].string(), {}};
        const music::Json& marks = list[index]["marks"];
        for (std::size_t mark = 0; mark < marks.size(); ++mark) {
            line.marks.push_back(
                {static_cast<int>(marks[mark]["word"].number()), static_cast<long long>(marks[mark]["ms"].number())});
        }
        out.push_back(std::move(line));
    }
    return out;
}

}

void start() {
    if (!instance) {
        instance = std::make_unique<Karaoke>();
    }
}

void stop() {
    instance.reset();
}

void render() {
    if (instance) {
        instance->render();
    }
}

Karaoke::Karaoke() {
    reload_settings();
    worker_ = std::thread([this] {
        work();
    });
}

Karaoke::~Karaoke() {
    stopping_ = true;
    queue_ready_.notify_all();
    if (worker_.joinable()) {
        worker_.join();
    }
    if (serving_) {
        net::stop_serving();
    }
}

// `lyrics config set` writes the file while the game runs; the change shows up within a second
void Karaoke::reload_settings() {
    settings_checked_ = Clock::now();
    std::error_code error;
    const auto written = std::filesystem::last_write_time(settings::file(), error);
    if (!error && written != settings_written_) {
        settings_written_ = written;
        settings_ = settings::load();
    }
    render::set_menu_key(settings_.menu_key);
}

void Karaoke::start_serving() {
    serve_tried_ = Clock::now();
    serving_ = net::serve(kBridgePort, [this](std::string_view method, std::string_view path, std::string_view body) {
        return on_request(method, path, body);
    });
    std::lock_guard lock(mutex_);
    if (!serving_) {
        status_ = "Port 8974 is busy, the extension can't report playback";
    } else if (status_.starts_with("Port 8974")) {
        status_.clear();
    }
}

void Karaoke::run(std::function<void()> task) {
    {
        std::lock_guard lock(queue_mutex_);
        queue_.push_back(std::move(task));
    }
    queue_ready_.notify_one();
}

void Karaoke::work() {
    while (true) {
        std::function<void()> task;
        {
            std::unique_lock lock(queue_mutex_);
            queue_ready_.wait(lock, [this] {
                return stopping_ || !queue_.empty();
            });
            if (stopping_) {
                return;
            }
            task = std::move(queue_.front());
            queue_.pop_front();
        }
        task();
    }
}

void Karaoke::search(std::string query) {
    int generation = 0;
    {
        std::lock_guard lock(mutex_);
        generation = ++search_generation_;
        searching_ = true;
    }
    run([this, query = std::move(query), generation] {
        auto found = music::search(query);
        std::vector<std::string> covers;
        {
            std::lock_guard lock(mutex_);
            if (generation != search_generation_) {
                return;
            }
            results_ = std::move(found);
            searching_ = false;
            for (const auto& track : results_) {
                if (!track.cover.empty() && !covers_.contains(track.cover)) {
                    covers_.emplace(track.cover, false);
                    covers.push_back(track.cover);
                }
            }
        }
        for (const auto& url : covers) {
            if (stopping_) {
                return;
            }
            if (auto image = music::download(url)) {
                std::lock_guard lock(mutex_);
                cover_downloads_.emplace_back(url, std::move(*image));
            }
        }
    });
}

void Karaoke::play(music::Track track) {
    {
        std::lock_guard lock(mutex_);
        current_ = track;
        current_video_.clear();
        lines_.clear();
        fetched_.clear();
        fetching_ = true;
        status_ = "Finding the video...";
        shown_line_ = -1;
    }
    run([this, track] {
        const auto video = music::youtube_video(track);
        const std::string url = video ? "https://www.youtube.com/watch?v=" + *video
                                      : "https://www.youtube.com/results?search_query=" +
                                            music::url_encode(track.artist + " " + track.title);
        {
            std::lock_guard lock(mutex_);
            if (!current_ || !same_track(*current_, track)) {
                return;
            }
            current_video_ = video.value_or("");
            settle();
            pending_open_ = url;
        }
        // an open youtube tab picks the url up through the extension and plays it right there, the browser stays
        // behind the game. no tab asking for it means opening it the loud way
        bool taken = false;
        for (int waited = 0; waited < kHandOffWait && !taken && !stopping_; ++waited) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            std::lock_guard lock(mutex_);
            taken = pending_open_.empty();
        }
        if (!taken) {
            {
                std::lock_guard lock(mutex_);
                pending_open_.clear();
            }
            net::open_url(url);
        }
        load_lyrics(track);
    });
}

void Karaoke::load_lyrics(music::Track track) {
    auto lines = music::fetch_lyrics(track);
    std::lock_guard lock(mutex_);
    if (!current_ || !same_track(*current_, track)) {
        return;
    }
    fetched_ = std::move(lines);
    fetching_ = false;
    settle();
}

// called locked. the video's hand-made captions win since they're timed on the video itself; LRCLIB's lyrics come
// next, slid onto the video with the auto captions when there are some; the auto captions alone are the last resort
void Karaoke::settle() {
    const auto found = std::find_if(captions_.begin(), captions_.end(), [this](const Captions& captions) {
        return !current_video_.empty() && captions.video == current_video_;
    });
    const Captions* ours = found == captions_.end() ? nullptr : &*found;
    std::vector<music::Line> lines;
    if (ours != nullptr && ours->trusted) {
        lines = ours->lines;
    } else if (!fetched_.empty()) {
        lines = fetched_;
        if (const auto shift = ours != nullptr ? music::align(fetched_, ours->words) : std::nullopt) {
            for (music::Line& line : lines) {
                line.ms += *shift;
            }
        }
    } else if (ours != nullptr && !fetching_) {
        lines = ours->lines;
    }
    status_ = !lines.empty() ? std::string() : fetching_ ? "Loading lyrics..." : "No synced lyrics for this song";
    const auto same = [](const music::Line& a, const music::Line& b) {
        return a.ms == b.ms && a.text == b.text;
    };
    if (!std::equal(lines.begin(), lines.end(), lines_.begin(), lines_.end(), same)) {
        lines_ = std::move(lines);
        shown_line_ = -1;
    }
}

// the extension's requests. POST /update: {title, artist, videoId, positionMs, durationMs, isPlaying}
std::string Karaoke::on_request(std::string_view method, std::string_view path, std::string_view body) {
    if (path.starts_with("/status")) {
        return R"({"status":"ok","mod":"Lyrics"})";
    }
    // every youtube tab asks once a second whether a song is waiting to be played in it
    if (path.starts_with("/poll")) {
        std::lock_guard lock(mutex_);
        const std::string url = std::exchange(pending_open_, std::string());
        return R"({"open":")" + url + R"("})";
    }
    // POST /captions: {videoId, trusted, lines: [{ms, text, marks: [{word, ms}]}], words: [{ms, text}]}
    if (method == "POST" && path.starts_with("/captions")) {
        const auto json = music::Json::parse(body);
        if (!json || (*json)["videoId"].string().empty()) {
            return R"({"success":false})";
        }
        Captions captions;
        captions.video = (*json)["videoId"].string();
        captions.trusted = (*json)["trusted"].boolean();
        captions.lines = read_lines((*json)["lines"]);
        captions.words = read_lines((*json)["words"]);
        std::lock_guard lock(mutex_);
        std::erase_if(captions_, [&captions](const Captions& held) {
            return held.video == captions.video;
        });
        captions_.push_front(std::move(captions));
        if (captions_.size() > 4) {
            captions_.pop_back();
        }
        settle();
        return R"({"success":true})";
    }
    if (method != "POST" || !path.starts_with("/update")) {
        return R"({"success":false})";
    }
    const auto json = music::Json::parse(body);
    if (!json) {
        return R"({"success":false})";
    }
    Playback update;
    update.video = (*json)["videoId"].string();
    update.title = (*json)["title"].string();
    update.channel = (*json)["artist"].string();
    update.position_ms = static_cast<long long>((*json)["positionMs"].number());
    update.duration_ms = static_cast<long long>((*json)["durationMs"].number());
    update.playing = (*json)["isPlaying"].boolean();
    update.received = Clock::now();
    std::optional<music::Track> derived;
    {
        std::lock_guard lock(mutex_);
        // with two youtube tabs open the one actually playing wins, the paused one keeps quiet
        if (playback_ && playback_->video != update.video && playback_->playing && !update.playing &&
            update.received - playback_->received < kPlaybackFresh) {
            return R"({"success":true})";
        }
        const bool new_video = !update.video.empty() && (!playback_ || playback_->video != update.video);
        // a video not picked in game: the lyrics come from its title instead
        if (new_video && update.video != current_video_ && !update.title.empty()) {
            derived = music::from_video_title(update.title, update.channel);
            derived->duration = static_cast<int>(update.duration_ms / 1000);
            current_ = derived;
            current_video_ = update.video;
            fetched_.clear();
            fetching_ = true;
            settle();
        }
        playback_ = std::move(update);
    }
    if (derived) {
        run([this, track = *derived] {
            load_lyrics(track);
        });
    }
    return R"({"success":true})";
}

// only the extension knows where the video really is: page loads and ads make any guess from the click run early
std::optional<long long> Karaoke::position_ms() const {
    if (!playback_ || (!current_video_.empty() && playback_->video != current_video_)) {
        return std::nullopt;
    }
    long long position = playback_->position_ms;
    const auto since = Clock::now() - playback_->received;
    // a silent extension (tab closed, ad, frozen page) holds the line instead of running on
    if (playback_->playing && since < kPlaybackFresh) {
        position += std::chrono::duration_cast<std::chrono::milliseconds>(since).count();
    }
    return position + static_cast<long long>(settings_.time_offset);
}

void Karaoke::render() {
    if (Clock::now() - settings_checked_ > kSettingsPoll) {
        reload_settings();
    }
    // a busy port may free up later
    if (!serving_ && !stopping_ && Clock::now() - serve_tried_ > kServeRetry) {
        start_serving();
    }
    {
        std::lock_guard lock(mutex_);
        while (!cover_downloads_.empty()) {
            auto [url, bytes] = std::move(cover_downloads_.front());
            cover_downloads_.pop_front();
            covers_[url] = render::covers::add(url, bytes);
        }
    }
    if (!game::player::in_world() && render::is_menu_visible()) {
        render::set_menu_visible(false);
    }
    if (render::is_menu_visible()) {
        render_panel();
    }
    render_world();
}

bool Karaoke::place(Placed& placed, int line, const Clock::time_point& now) const {
    float camera[3]{};
    float ahead[3]{};
    if (!game::camera::ahead(0.0F, camera) || !game::camera::ahead(1.0F, ahead)) {
        return false;
    }
    float forward[3]{ahead[0] - camera[0], 0.0F, ahead[2] - camera[2]};
    const float flat = std::hypot(forward[0], forward[2]);
    if (flat < 1.0e-4F) {
        return false;
    }
    forward[0] /= flat;
    forward[2] /= flat;
    float distance = settings_.distance;
    float direction[3]{forward[0], 0.0F, forward[2]};
    if (lead_) {
        // along the glide itself, far enough that it's still ahead by the time it's read
        const float speed = std::hypot(velocity_[0], velocity_[1], velocity_[2]);
        for (int axis = 0; axis < 3; ++axis) {
            direction[axis] = velocity_[axis] / speed;
        }
        distance = std::max(distance, speed * kLeadSeconds);
        if (const float glide_flat = std::hypot(direction[0], direction[2]); glide_flat > 0.2F) {
            forward[0] = direction[0] / glide_flat;
            forward[2] = direction[2] / glide_flat;
        }
    }
    placed.line = line;
    placed.at = now;
    placed.sky = settings_.mode == settings::Mode::sky;
    if (placed.sky) {
        const float angle = settings_.sky_angle * 0.0174532925F;
        placed.direction[0] = forward[0] * std::cos(angle);
        placed.direction[1] = std::sin(angle);
        placed.direction[2] = forward[2] * std::cos(angle);
    }
    placed.anchor[0] = camera[0] + direction[0] * distance;
    placed.anchor[1] = camera[1] + direction[1] * distance + settings_.height;
    placed.anchor[2] = camera[2] + direction[2] * distance;
    // turned toward you once when it appears, then it stays put like a sign
    placed.right[0] = -forward[2];
    placed.right[1] = 0.0F;
    placed.right[2] = forward[0];
    return true;
}

void Karaoke::track_camera(const Clock::time_point& now) {
    float camera[3]{};
    if (!game::camera::ahead(0.0F, camera)) {
        return;
    }
    const float elapsed = camera_seen_ ? std::chrono::duration<float>(now - camera_at_).count() : 0.0F;
    float moved[3]{};
    float missed = 0.0F;
    for (int axis = 0; axis < 3; ++axis) {
        moved[axis] = camera[axis] - last_camera_[axis];
        const float off = moved[axis] - velocity_[axis] * elapsed;
        missed += off * off;
    }
    const auto carry = [this, &now] {
        const auto typing_since = sung_.at;
        if (sung_.line >= 0 && !sung_.sky && place(sung_, sung_.line, now)) {
            sung_.at = typing_since;
        }
    };
    if (camera_seen_ && std::sqrt(missed) > kCameraJump) {
        // the camera leaping (third person, front view, a teleport) carries the line along, still mid-typing
        carry();
    } else if (elapsed > 0.0F && elapsed < 0.25F) {
        const float blend = 1.0F - std::exp(-elapsed / kVelocitySmoothing);
        for (int axis = 0; axis < 3; ++axis) {
            velocity_[axis] += (moved[axis] / elapsed - velocity_[axis]) * blend;
        }
    }
    lead_ = settings_.mode == settings::Mode::normal && settings_.elytra_lead && game::player::gliding() &&
        std::hypot(velocity_[0], velocity_[1], velocity_[2]) > kLeadSpeed;
    // flown past it: back onto the flight path ahead
    if (lead_ && sung_.line >= 0) {
        float behind = 0.0F;
        for (int axis = 0; axis < 3; ++axis) {
            behind += (sung_.anchor[axis] - camera[axis]) * velocity_[axis];
        }
        if (behind < 0.0F) {
            carry();
        }
    }
    std::copy(camera, camera + 3, last_camera_);
    camera_at_ = now;
    camera_seen_ = true;
}

void Karaoke::draw_placed(ImDrawList* draw, const Placed& placed, std::string_view line, std::size_t visible,
                          float shrink, float alpha) const {
    const std::vector<std::string> rows = text::wrap(line, kWrapWidth);
    if (rows.empty()) {
        return;
    }
    float pixel = kPixel * settings_.scale * shrink;
    float down[3]{0.0F, -1.0F, 0.0F};
    float anchor[3]{placed.anchor[0], placed.anchor[1], placed.anchor[2]};
    if (placed.sky) {
        float camera[3]{};
        if (!game::camera::ahead(0.0F, camera)) {
            return;
        }
        pixel *= kSkyScale;
        // tilted back to face you, the sheet would read squashed looking up at it otherwise
        const float* dir = placed.direction;
        for (int axis = 0; axis < 3; ++axis) {
            anchor[axis] = camera[axis] + dir[axis] * kSkyDistance;
            down[axis] = dir[axis] * dir[1];
        }
        down[1] -= 1.0F;
        const float length = std::hypot(down[0], down[1], down[2]);
        for (float& component : down) {
            component /= length;
        }
    }
    const float block_h = static_cast<float>((rows.size() - 1) * kLineHeight + render::glyphs::kHeight);
    const auto& tint = settings_.text_color;
    const ImU32 color = ImGui::ColorConvertFloat4ToU32({tint[0], tint[1], tint[2], tint[3] * alpha});
    for (std::size_t index = 0; index < rows.size() && visible > 0; ++index) {
        const std::string_view row = text::first_code_points(rows[index], visible);
        // the space eaten by the wrap counts as a typed letter too
        const std::size_t row_points = text::code_points(rows[index]);
        visible = visible > row_points ? visible - row_points - 1 : 0;
        const float row_w = static_cast<float>(render::glyphs::width(rows[index]));
        const float top = static_cast<float>(index * kLineHeight) - block_h * 0.5F;
        render::glyphs::Sheet sheet;
        sheet.pixel = pixel;
        for (int axis = 0; axis < 3; ++axis) {
            sheet.right[axis] = placed.right[axis];
            sheet.down[axis] = down[axis];
            sheet.origin[axis] = anchor[axis] - placed.right[axis] * row_w * 0.5F * pixel + down[axis] * top * pixel;
        }
        render::glyphs::draw(draw, sheet, row, color, false, settings_.shadow);
    }
}

void Karaoke::render_world() {
    if (!game::camera::ready()) {
        return;
    }
    std::vector<music::Line> lines;
    std::optional<long long> position;
    {
        std::lock_guard lock(mutex_);
        if (lines_.empty()) {
            return;
        }
        lines = lines_;
        position = position_ms();
    }
    if (!position) {
        return;
    }
    // the sung line: the last one whose timestamp has passed
    int active = -1;
    for (std::size_t index = 0; index < lines.size(); ++index) {
        if (lines[index].ms <= *position) {
            active = static_cast<int>(index);
        }
    }
    const auto now = Clock::now();
    const auto text_of = [&lines](int line) -> std::string_view {
        return line >= 0 && line < static_cast<int>(lines.size())
            ? std::string_view(lines[static_cast<std::size_t>(line)].text)
            : std::string_view();
    };
    track_camera(now);
    if (active != shown_line_) {
        shown_line_ = active;
        leaving_ = sung_;
        leaving_.at = now;
        sung_ = {};
        if (!text_of(active).empty() && !place(sung_, active, now)) {
            sung_ = {};
        }
    }
    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    // the old line shrinks into its middle and fades while the new one types out
    const double left_for = std::chrono::duration<double, std::milli>(now - leaving_.at).count();
    if (!text_of(leaving_.line).empty() && left_for < kLeaveMs) {
        const auto t = static_cast<float>(left_for / kLeaveMs);
        draw_placed(draw, leaving_, text_of(leaving_.line), std::string_view::npos, 1.0F - t, 1.0F - t);
    }
    if (text_of(sung_.line).empty()) {
        return;
    }
    const auto& line = lines[static_cast<std::size_t>(sung_.line)];
    std::size_t visible = std::string_view::npos;
    if (line.marks.size() > 1) {
        // the captions know when each word is said, so the words land right on the voice
        std::size_t words = 0;
        for (std::size_t mark = 0; mark < line.marks.size(); ++mark) {
            if (line.marks[mark].ms <= *position) {
                words = mark + 1 < line.marks.size() ? static_cast<std::size_t>(line.marks[mark + 1].word)
                                                     : std::string_view::npos;
            }
        }
        visible = text::words_to_code_points(line.text, words);
    } else if (settings_.typewriter) {
        // never slower than half the line's time, fast rap would leave the typing behind the voice
        const std::size_t length = text::code_points(line.text);
        double per_char = kTypeMsPerChar;
        if (const auto next = static_cast<std::size_t>(sung_.line) + 1; next < lines.size() && length > 0) {
            per_char =
                std::min(per_char, static_cast<double>(lines[next].ms - line.ms) * 0.5 / static_cast<double>(length));
        }
        const double sung_for = std::chrono::duration<double, std::milli>(now - sung_.at).count();
        visible = static_cast<std::size_t>(sung_for / std::max(per_char, 1.0)) + 1;
    }
    draw_placed(draw, sung_, line.text, visible, 1.0F, 1.0F);
}

}
