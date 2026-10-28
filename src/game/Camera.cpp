#include "game/Camera.hpp"

#include "game/Offsets.hpp"
#include "game/Player.hpp"
#include "memory/Access.hpp"

#include <safetyhook.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <mutex>

namespace lyrics::game::camera {
namespace {

using Clock = std::chrono::steady_clock;
namespace layout = offsets::camera;

constexpr float kNear = 0.05F;

// a point relative to the camera origin maps to X = x . d + x[3] (same for Y and W); on screen it is
// center + (X, -Y) / W
struct Camera {
    bool valid = false;
    float origin[3]{};
    float x[4]{};
    float y[4]{};
    float w[4]{};
    ImVec2 center{};
};

struct Clip {
    float x = 0.0F;
    float y = 0.0F;
    float w = 0.0F;
};

struct Raw {
    float view[16]{};
    float projection[16]{};
    float origin[3]{};
};

Camera current;
safetyhook::MidHook render_hook;
safetyhook::MidHook other_render_hook;

// the eye point moves every frame while walking; read at present time it can already belong to the next frame, so
// the text would swim. a copy taken during the level render stays in step, and the frame being presented is the one
// recorded a renderLevel before the newest copy
std::mutex captured_mutex;
Raw captured;
Raw presented;
Clock::time_point captured_at{};

std::uintptr_t pointer_at(std::uintptr_t address) {
    std::uintptr_t value = 0;
    return address != 0 && memory::read(address, value) ? value : 0;
}

bool finite(const float* values, int count) {
    return std::all_of(values, values + count, [](float value) {
        return std::isfinite(value) && std::abs(value) <= 1.0e4F;
    });
}

bool read_game(Raw& raw) {
    const std::uintptr_t client = player::client();
    if (client == 0) {
        return false;
    }
    const std::uintptr_t matrices = pointer_at(pointer_at(client + layout::ClientMatrices) + layout::Matrices);
    const std::uintptr_t source = pointer_at(pointer_at(client + layout::ClientCamera) + layout::Camera);
    if (matrices == 0 || source == 0 || !memory::read(matrices + layout::View, raw.view, sizeof(raw.view)) ||
        !memory::read(matrices + layout::Projection, raw.projection, sizeof(raw.projection)) ||
        !memory::read(source + layout::Origin, raw.origin, sizeof(raw.origin)) || !finite(raw.view, 16) ||
        !finite(raw.projection, 16) || !finite(raw.origin, 3)) {
        return false;
    }
    // the live projection carries this frame's fov (sprint, zoom); only its x/y scale is taken, and only when the
    // aspect agrees
    float live[16]{};
    float* projection = raw.projection;
    if (memory::read(source + layout::LiveProjection, live, sizeof(live)) && live[0] > 0.01F && live[5] > 0.01F &&
        live[0] < 1000.0F && live[5] < 1000.0F && projection[0] > 0.01F && projection[5] > 0.01F) {
        const float ratio = live[5] / live[0];
        if (std::abs(projection[5] / projection[0] - ratio) < ratio * 0.01F) {
            projection[0] = live[0];
            projection[5] = live[5];
        }
    }
    return true;
}

void capture(safetyhook::Context&) {
    Raw raw;
    if (!read_game(raw)) {
        return;
    }
    std::lock_guard lock(captured_mutex);
    presented = captured;
    captured = raw;
    captured_at = Clock::now();
}

Clip clip(const float* point) {
    const float d[3]{point[0] - current.origin[0], point[1] - current.origin[1], point[2] - current.origin[2]};
    return {current.x[0] * d[0] + current.x[1] * d[1] + current.x[2] * d[2] + current.x[3],
            current.y[0] * d[0] + current.y[1] * d[1] + current.y[2] * d[2] + current.y[3],
            current.w[0] * d[0] + current.w[1] * d[1] + current.w[2] * d[2] + current.w[3]};
}

}

bool install() {
    render_hook = safetyhook::create_mid(
        memory::resolve(offsets::RenderLevelCall + offsets::CallLength + offsets::RenderLevelHookSkip), capture);
    other_render_hook =
        safetyhook::create_mid(memory::resolve(offsets::RenderLevelOtherCall + offsets::CallLength), capture);
    return static_cast<bool>(render_hook) && static_cast<bool>(other_render_hook);
}

void remove() {
    render_hook = {};
    other_render_hook = {};
}

void update() {
    current.valid = false;
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    if (!player::in_world() || display.x <= 0.0F || display.y <= 0.0F) {
        return;
    }
    Raw raw;
    {
        std::lock_guard lock(captured_mutex);
        if (Clock::now() - captured_at < std::chrono::milliseconds(200)) {
            raw = presented;
        } else if (!read_game(raw)) {
            return;
        }
    }
    // glm keeps matrices column major, so with the floats taken row by row a point transforms as d * view * projection
    float combined[16]{};
    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) {
            float sum = 0.0F;
            for (int k = 0; k < 4; ++k) {
                sum += raw.view[row * 4 + k] * raw.projection[k * 4 + column];
            }
            combined[row * 4 + column] = sum;
        }
    }
    const float half_width = display.x * 0.5F;
    const float half_height = display.y * 0.5F;
    for (int row = 0; row < 4; ++row) {
        current.x[row] = combined[row * 4 + 0] * half_width;
        current.y[row] = combined[row * 4 + 1] * half_height;
        current.w[row] = combined[row * 4 + 3];
    }
    std::copy(raw.origin, raw.origin + 3, current.origin);
    current.center = {half_width, half_height};
    current.valid = true;
}

bool ready() {
    return current.valid;
}

bool project(const float* point, ImVec2& out) {
    if (!current.valid) {
        return false;
    }
    const Clip projected = clip(point);
    if (projected.w < kNear) {
        return false;
    }
    out = {current.center.x + projected.x / projected.w, current.center.y - projected.y / projected.w};
    return true;
}

bool ahead(float distance, float* out) {
    if (!current.valid) {
        return false;
    }
    // the depth row of the transform points where the camera looks
    const float length = std::sqrt(current.w[0] * current.w[0] + current.w[1] * current.w[1] + current.w[2] * current.w[2]);
    if (!(length > 0.0F)) {
        return false;
    }
    for (int axis = 0; axis < 3; ++axis) {
        out[axis] = current.origin[axis] + current.w[axis] / length * distance;
    }
    return true;
}

}
