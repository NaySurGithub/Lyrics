#pragma once

namespace lyrics::render {

using FrameCallback = void (*)();

// hooks the game's d3d12 swap chain; `frame` runs inside every imgui frame
bool init(FrameCallback frame);
void shutdown();

// the song search: open, it takes the mouse and keyboard from the game
[[nodiscard]] bool is_menu_visible();
void set_menu_visible(bool visible);
// a windows virtual key code
void set_menu_key(int key);

}
