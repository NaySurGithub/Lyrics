#include "game/Camera.hpp"
#include "game/Fingerprint.hpp"
#include "game/Offsets.hpp"
#include "game/Player.hpp"
#include "karaoke/Karaoke.hpp"
#include "render/Overlay.hpp"
#include "shared/Names.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <string>

namespace {

HMODULE self = nullptr;
HANDLE unload_requested = nullptr;

void log(const std::string& line) {
    OutputDebugStringA(("[Lyrics] " + line + "\n").c_str());
}

void frame() {
    lyrics::karaoke::render();
}

bool start() {
    if (!lyrics::game::matches_version()) {
        log(std::string("this is not Minecraft ") + lyrics::game::offsets::kVersion + ", nothing was hooked");
        return false;
    }
    if (!lyrics::game::player::install() || !lyrics::game::camera::install()) {
        log("could not hook the game");
        return false;
    }
    lyrics::karaoke::start();
    if (!lyrics::render::init(&frame)) {
        log("could not hook d3d12");
        return false;
    }
    log("ready");
    return true;
}

// hooks go first so nothing new comes in, then the threads they fed
void stop() {
    lyrics::render::shutdown();
    lyrics::game::camera::remove();
    lyrics::game::player::remove();
    lyrics::karaoke::stop();
}

DWORD WINAPI run(LPVOID) {
    (void)start();
    WaitForSingleObject(unload_requested, INFINITE);
    stop();
    log("unloaded");
    CloseHandle(unload_requested);
    FreeLibraryAndExitThread(self, 0);
}

}

extern "C" __declspec(dllexport) DWORD WINAPI lyrics_unload(LPVOID) {
    return unload_requested != nullptr && SetEvent(unload_requested) ? 1 : 0;
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        self = module;
        DisableThreadLibraryCalls(module);
        unload_requested = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (unload_requested == nullptr) {
            return FALSE;
        }
        // loader lock is held here: everything real happens on a thread of its own
        if (HANDLE thread = CreateThread(nullptr, 0, run, nullptr, 0, nullptr)) {
            CloseHandle(thread);
        }
    }
    return TRUE;
}
