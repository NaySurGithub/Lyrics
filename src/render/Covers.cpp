#include "render/Covers.hpp"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#include <stb_image.h>

#include <imgui_internal.h>

#include <cstring>
#include <map>

namespace lyrics::render::covers {
namespace {

ImFontAtlas* owner = nullptr;
std::map<std::string, ImFontAtlasRectId, std::less<>> rects;

}

void attach(ImFontAtlas* atlas) {
    owner = atlas;
    // covers are colour images, the atlas has to be rgba for them
    atlas->TexDesiredFormat = ImTextureFormat_RGBA32;
    rects.clear();
}

bool add(std::string_view name, std::string_view encoded) {
    if (owner == nullptr || rects.contains(name)) {
        return false;
    }
    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_uc* pixels = stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(encoded.data()),
                                            static_cast<int>(encoded.size()), &width, &height, &channels, 4);
    if (pixels == nullptr) {
        return false;
    }
    ImFontAtlasRect rect;
    const ImFontAtlasRectId id = owner->AddCustomRect(width, height, &rect);
    const bool stored = id != ImFontAtlasRectId_Invalid && owner->TexData->Format == ImTextureFormat_RGBA32;
    if (stored) {
        for (int y = 0; y < height; ++y) {
            std::memcpy(owner->TexData->GetPixelsAt(rect.x, rect.y + y), pixels + static_cast<std::size_t>(y) * width * 4,
                        static_cast<std::size_t>(width) * 4);
        }
        ImFontAtlasTextureBlockQueueUpload(owner, owner->TexData, rect.x, rect.y, rect.w, rect.h);
        rects.emplace(std::string(name), id);
    }
    stbi_image_free(pixels);
    return stored;
}

bool draw(ImDrawList* draw, std::string_view name, ImVec2 min, ImVec2 max) {
    const auto found = rects.find(name);
    ImFontAtlasRect rect;
    if (owner == nullptr || found == rects.end() || !owner->GetCustomRect(found->second, &rect)) {
        return false;
    }
    draw->AddImage(owner->TexRef, min, max, rect.uv0, rect.uv1);
    return true;
}

}
