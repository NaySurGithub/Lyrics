#pragma once

#include <imgui.h>

#include <string_view>

namespace lyrics::render::glyphs {

// a flat sheet in the world the text is laid on: its top left corner, the screen-right and screen-down directions
// and how many blocks one font pixel takes
struct Sheet {
    float origin[3]{};
    float right[3]{};
    float down[3]{};
    float pixel = 0.1F;
};

// Minecraft's own font: the vanilla glyph sheet, advances and a 1 pixel drop shadow, bold drawn twice one pixel apart.
// glyphs are 8 font pixels tall
inline constexpr int kHeight = 8;

[[nodiscard]] int width(std::string_view text, bool bold = false);
void draw(ImDrawList* draw, const Sheet& sheet, std::string_view text, ImU32 color, bool bold, bool shadow);
// the same font flat on screen, `pixel` screen pixels per font pixel
void draw_flat(ImDrawList* draw, ImVec2 position, float pixel, std::string_view text, ImU32 color, bool shadow);

}
