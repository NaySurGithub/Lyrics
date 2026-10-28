#pragma once

#include <array>
#include <cstdint>

// Minecraft Bedrock 26.60, Windows x64. addresses are rvas from the game image, layouts are byte offsets. a new game
// version moves all of this; Fingerprint.cpp refuses to hook when the bytes below don't match
namespace lyrics::game::offsets {

inline constexpr char kVersion[] = "26.60";

// LocalPlayer::applyTurnDelta(this, delta): runs every frame, for the local player only
inline constexpr std::uintptr_t ApplyTurnDelta = 0x6DA4BD0;
// the two renderLevel calls of a frame. right after them the camera holds this frame's eye point
inline constexpr std::uintptr_t RenderLevelCall = 0x4E0B87C;
inline constexpr std::uintptr_t RenderLevelOtherCall = 0x4E0B730;
inline constexpr std::uintptr_t CallLength = 5;
// a 2-byte xor follows the first call's return and the instruction after it is a jump target
inline constexpr std::uintptr_t RenderLevelHookSkip = 2;

// what those sites look like in 26.60
inline constexpr std::array<std::uint8_t, 8> ApplyTurnDeltaBytes{0x55, 0x41, 0x57, 0x41, 0x56, 0x41, 0x54, 0x56};
inline constexpr std::array<std::uint8_t, 7> RenderLevelCallBytes{0xE8, 0xFF, 0x40, 0x55, 0x00, 0x31, 0xFF};
inline constexpr std::array<std::uint8_t, 5> RenderLevelOtherCallBytes{0xE8, 0x4B, 0x42, 0x55, 0x00};

// LocalPlayer -> its ClientInstance
inline constexpr std::uintptr_t LocalPlayerClient = 0xD98;

namespace camera {
// ClientInstance -> matrices holder -> view / projection (glm, column major)
inline constexpr std::uintptr_t ClientMatrices = 0x1C8;
inline constexpr std::uintptr_t Matrices = 0x1628;
inline constexpr std::uintptr_t View = 0x3C0;
inline constexpr std::uintptr_t Projection = 0x440;
// ClientInstance -> camera holder -> camera: the eye point and the projection with this frame's fov
inline constexpr std::uintptr_t ClientCamera = 0x1E0;
inline constexpr std::uintptr_t Camera = 0x480;
inline constexpr std::uintptr_t Origin = 0x658;
inline constexpr std::uintptr_t LiveProjection = 0xF50;
}

// the actor's EnTT registry and its storages
namespace entity {
inline constexpr std::uintptr_t ContextRegistry = 0x10;
inline constexpr std::uintptr_t ContextIdentifier = 0x18;
inline constexpr std::uintptr_t BucketsBegin = 0x48;
inline constexpr std::uintptr_t BucketsEnd = 0x50;
inline constexpr std::uintptr_t NodesBegin = 0x68;
inline constexpr std::uintptr_t NodesEnd = 0x70;
inline constexpr std::uintptr_t NodeSize = 0x20;
inline constexpr std::uintptr_t NodeKey = 0x08;
inline constexpr std::uintptr_t NodeStorage = 0x10;
inline constexpr std::uintptr_t SparseBegin = 0x08;
inline constexpr std::uintptr_t SparseEnd = 0x10;
inline constexpr std::uintptr_t PackedPages = 0x50;
inline constexpr std::uint32_t EntityMask = 0x3FFFF;
inline constexpr std::uint32_t VersionMask = 0xFFFC0000;
}

// ActorDataFlagComponent: a bitset, one bit per ActorFlags value
inline constexpr std::size_t ActorDataFlagSize = 24;
inline constexpr std::uint32_t GlidingFlag = 32;

}
