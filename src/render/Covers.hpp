#pragma once

#include <imgui.h>

#include <string>
#include <string_view>

// album covers live in imgui's own atlas, so they draw with the same texture as everything else
namespace lyrics::render::covers {

void attach(ImFontAtlas* atlas);
// encoded image bytes (jpeg / png) under `name`; render thread only
bool add(std::string_view name, std::string_view encoded);
// false until the cover is in the atlas
bool draw(ImDrawList* draw, std::string_view name, ImVec2 min, ImVec2 max);

}
