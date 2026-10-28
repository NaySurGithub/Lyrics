#include "game/Player.hpp"

#include "game/Entity.hpp"
#include "game/Offsets.hpp"
#include "memory/Access.hpp"

#include <safetyhook.hpp>

#include <atomic>
#include <chrono>

namespace lyrics::game::player {
namespace {

using Clock = std::chrono::steady_clock;

constexpr std::uint32_t kActorDataFlag = entity::type_hash("ActorDataFlagComponent");

safetyhook::MidHook turn_hook;
std::atomic<std::uintptr_t> player{0};
std::atomic<Clock::rep> turned_at{0};

void on_turn(safetyhook::Context& context) {
    player.store(context.rcx, std::memory_order_release);
    turned_at.store(Clock::now().time_since_epoch().count(), std::memory_order_release);
}

}

bool install() {
    turn_hook = safetyhook::create_mid(memory::resolve(offsets::ApplyTurnDelta), on_turn);
    return static_cast<bool>(turn_hook);
}

void remove() {
    turn_hook = {};
    player.store(0);
}

std::uintptr_t local() {
    return in_world() ? player.load(std::memory_order_acquire) : 0;
}

std::uintptr_t client() {
    std::uintptr_t owner = 0;
    const std::uintptr_t actor = local();
    return actor != 0 && memory::read(actor + offsets::LocalPlayerClient, owner) ? owner : 0;
}

bool in_world() {
    const Clock::duration since = Clock::now().time_since_epoch() - Clock::duration{turned_at.load()};
    return turned_at.load() != 0 && since < std::chrono::seconds(1);
}

bool gliding() {
    const std::uintptr_t flags = entity::find_component(local(), kActorDataFlag, offsets::ActorDataFlagSize);
    std::uint8_t bits = 0;
    return flags != 0 && memory::read(flags + (offsets::GlidingFlag >> 3), bits) &&
        (bits & (1U << (offsets::GlidingFlag & 7))) != 0;
}

}
