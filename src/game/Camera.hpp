#pragma once

#include <imgui.h>

namespace lyrics::game::camera {

bool install();
void remove();

// rebuilds the projection for this frame; call once per frame before anything is drawn in 3D
void update();
[[nodiscard]] bool ready();
[[nodiscard]] bool project(const float* point, ImVec2& out);
// the world point `distance` blocks straight ahead of the camera, 0 gives the eye itself
[[nodiscard]] bool ahead(float distance, float* out);

}
