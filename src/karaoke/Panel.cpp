#include "karaoke/Karaoke.hpp"

#include "karaoke/Text.hpp"
#include "render/Covers.hpp"
#include "render/Glyphs.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

// the song search, drawn in Minecraft's own font. every size is in font pixels, scaled to whole screen pixels so
// the glyphs stay sharp
namespace lyrics::karaoke {
namespace {

constexpr auto kSearchDebounce = std::chrono::milliseconds(350);
constexpr int kWidth = 190;
constexpr int kPad = 6;
constexpr int kGap = 4;
constexpr int kFieldHeight = 14;
constexpr int kRowHeight = 22;
constexpr int kCover = 18;

constexpr ImU32 kBackground = IM_COL32(14, 14, 16, 232);
constexpr ImU32 kBorder = IM_COL32(255, 255, 255, 28);
constexpr ImU32 kField = IM_COL32(0, 0, 0, 120);
constexpr ImU32 kHover = IM_COL32(255, 255, 255, 18);
constexpr ImU32 kPicked = IM_COL32(255, 237, 204, 30);
constexpr ImU32 kTitle = IM_COL32(255, 255, 255, 255);
constexpr ImU32 kText = IM_COL32(235, 235, 235, 255);
constexpr ImU32 kMuted = IM_COL32(150, 150, 155, 255);
constexpr ImU32 kAccent = IM_COL32(255, 237, 204, 255);

std::string clock_text(int seconds) {
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "%d:%02d", seconds / 60, seconds % 60);
    return buffer;
}

bool same_track(const music::Track& a, const music::Track& b) {
    return a.title == b.title && a.artist == b.artist;
}

bool hovered(ImVec2 min, ImVec2 max) {
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    return mouse.x >= min.x && mouse.x < max.x && mouse.y >= min.y && mouse.y < max.y;
}

}

// the search field takes every typed character while the panel is open
void Karaoke::edit_query() {
    ImGuiIO& io = ImGui::GetIO();
    bool edited = false;
    for (const ImWchar character : io.InputQueueCharacters) {
        if (character >= 0x20 && character != 0x7F && query_.size() < 120) {
            text::append_code_point(query_, character);
            edited = true;
        }
    }
    io.InputQueueCharacters.resize(0);
    if (ImGui::IsKeyPressed(ImGuiKey_Backspace, true) && !query_.empty()) {
        if (io.KeyCtrl) {
            query_.clear();
        } else {
            text::pop_code_point(query_);
        }
        edited = true;
    }
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V, false)) {
        if (const char* pasted = ImGui::GetClipboardText(); pasted != nullptr) {
            query_ += pasted;
            std::replace(query_.begin(), query_.end(), '\n', ' ');
            edited = true;
        }
    }
    if (edited) {
        typed_at_ = Clock::now();
    }
    const bool submitted = ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false);
    // searches while you type, once you pause; enter searches right away
    if (!query_.empty() && query_ != submitted_query_ &&
        (submitted || (typed_at_ != Clock::time_point{} && Clock::now() - typed_at_ >= kSearchDebounce))) {
        submitted_query_ = query_;
        search(query_);
    }
}

void Karaoke::render_panel() {
    edit_query();
    std::vector<music::Track> results;
    std::string status;
    std::optional<music::Track> current;
    bool searching = false;
    bool heard = false;
    {
        std::lock_guard lock(mutex_);
        results = results_;
        status = status_;
        current = current_;
        searching = searching_;
        heard = playback_ && (current_video_.empty() || playback_->video == current_video_);
    }
    std::string footer = searching ? "Searching..." : status;
    if (footer.empty() && current) {
        footer = heard ? "Now playing: " + current->artist + " - " + current->title
                       : "Waiting for the video, is the browser extension on?";
    }

    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const float p = std::max(2.0F, std::floor(display.y / 300.0F));
    const auto px = [p](int font_pixels) {
        return static_cast<float>(font_pixels) * p;
    };
    const int rows_h = results.empty() ? 0 : kGap + kRowHeight * static_cast<int>(results.size());
    const int footer_h = footer.empty() ? 0 : kGap + render::glyphs::kHeight;
    const int height = kPad + render::glyphs::kHeight + kGap + kFieldHeight + rows_h + footer_h + kPad;
    const int inner = kWidth - kPad * 2;
    const ImVec2 min{std::floor(display.x - px(kWidth) - px(8)), std::floor(px(24))};
    const ImVec2 max{min.x + px(kWidth), min.y + px(height)};

    ImDrawList* draw = ImGui::GetForegroundDrawList();
    draw->AddRectFilled(min, max, kBackground);
    draw->AddRect(min, max, kBorder, 0.0F, 0, p * 0.5F);
    const auto text = [&](float x, float y, ImU32 color, std::string_view line) {
        render::glyphs::draw_flat(draw, {x, y}, p, line, color, true);
    };

    const float left = min.x + px(kPad);
    float y = min.y + px(kPad);
    text(left, y, kTitle, "Lyrics");
    const std::string hint = "Esc to close";
    text(min.x + px(kWidth - kPad - render::glyphs::width(hint)), y, kMuted, hint);
    y += px(render::glyphs::kHeight + kGap);

    const ImVec2 field{left, y};
    const ImVec2 field_max{left + px(inner), y + px(kFieldHeight)};
    draw->AddRectFilled(field, field_max, kField);
    draw->AddRect(field, field_max, kBorder, 0.0F, 0, p * 0.5F);
    const float text_y = y + px((kFieldHeight - render::glyphs::kHeight) / 2) + p;
    const bool caret = std::fmod(ImGui::GetTime(), 1.0) < 0.5;
    if (query_.empty()) {
        text(left + px(3), text_y, kMuted, "Search a song...");
    } else {
        // the end of a long query stays in view
        std::string shown = query_;
        while (render::glyphs::width(shown) > inner - 10 && !shown.empty()) {
            shown.erase(0, 1);
        }
        text(left + px(3), text_y, kText, shown + (caret ? "_" : ""));
    }
    y = field_max.y;

    if (!results.empty()) {
        y += px(kGap);
    }
    const bool clicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    for (const music::Track& track : results) {
        const ImVec2 row{left, y};
        const ImVec2 row_max{left + px(inner), y + px(kRowHeight)};
        const bool picked = current && same_track(*current, track);
        const bool over = hovered(row, row_max);
        if (picked || over) {
            draw->AddRectFilled(row, row_max, picked ? kPicked : kHover);
        }
        const ImVec2 cover{row.x + px(2), row.y + px(2)};
        const ImVec2 cover_max{cover.x + px(kCover), cover.y + px(kCover)};
        if (!render::covers::draw(draw, track.cover, cover, cover_max)) {
            draw->AddRectFilled(cover, cover_max, kHover);
        }
        const std::string length = clock_text(track.duration);
        const int length_w = render::glyphs::width(length);
        const int text_left = 2 + kCover + 4;
        const int text_w = inner - text_left - length_w - 6;
        text(row.x + px(text_left), row.y + px(2), picked ? kAccent : kText, text::fit(track.title, text_w));
        text(row.x + px(text_left), row.y + px(12), kMuted, text::fit(track.artist, text_w));
        text(row_max.x - px(length_w + 3), row.y + px(7), kMuted, length);
        if (over && clicked) {
            play(track);
        }
        y += px(kRowHeight);
    }

    if (!footer.empty()) {
        text(left, max.y - px(kPad + render::glyphs::kHeight), kMuted, text::fit(footer, inner));
    }
}

}
