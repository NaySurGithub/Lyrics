cmake_minimum_required(VERSION 3.24)
project(Lyrics VERSION 1.1.0 LANGUAGES CXX)

if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Lyrics targets Windows x64; on macOS or Linux use the mingw preset")
endif()
# the fault-safe memory reads are gnu inline asm
if(MSVC)
    message(FATAL_ERROR "Lyrics builds with mingw-w64 (MSYS2 on Windows), not MSVC")
endif()

include(FetchContent)
set(SAFETYHOOK_FETCH_ZYDIS ON CACHE BOOL "" FORCE)
FetchContent_Declare(
    safetyhook
    GIT_REPOSITORY https://github.com/cursey/safetyhook.git
    GIT_TAG c3f3f306a0f12d1811c0b713ad2ed2a8ddc6cf55
)
FetchContent_MakeAvailable(safetyhook)

add_library(lyrics_shared STATIC shared/Settings.cpp)
target_compile_features(lyrics_shared PUBLIC cxx_std_20)
target_include_directories(lyrics_shared PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# windows 10 apis (pointer messages) on every toolchain, msys2 defaults lower than the cross compiler
target_compile_definitions(lyrics_shared PUBLIC NOMINMAX UNICODE _UNICODE _WIN32_WINNT=0x0A00 WINVER=0x0A00)

set(imgui_sources
    third_party/imgui/imgui.cpp
    third_party/imgui/imgui_draw.cpp
    third_party/imgui/imgui_tables.cpp
    third_party/imgui/imgui_widgets.cpp
    third_party/imgui/backends/imgui_impl_dx12.cpp
    third_party/imgui/backends/imgui_impl_win32.cpp
)
# upstream code, built as is
set_source_files_properties(${imgui_sources} PROPERTIES COMPILE_OPTIONS "-w")

# the overlay the cli loads into the game
file(GLOB_RECURSE overlay_sources CONFIGURE_DEPENDS src/*.cpp)
add_library(LyricsOverlay SHARED ${overlay_sources} ${imgui_sources})
target_compile_features(LyricsOverlay PRIVATE cxx_std_23)
target_include_directories(LyricsOverlay PRIVATE
    src
    third_party/imgui
    third_party/imgui/backends
    third_party/stb
)
target_link_libraries(LyricsOverlay PRIVATE lyrics_shared safetyhook::safetyhook d3d12 dxgi d3dcompiler dwmapi winhttp ws2_32)
set_target_properties(LyricsOverlay PROPERTIES PREFIX "")

add_executable(lyrics cli/Main.cpp cli/Injector.cpp)
target_compile_features(lyrics PRIVATE cxx_std_20)
target_link_libraries(lyrics PRIVATE lyrics_shared)

target_compile_options(LyricsOverlay PRIVATE -Wall -Wextra)
target_compile_options(lyrics PRIVATE -Wall -Wextra -municode)
# one dll and one exe with nothing else to ship: no libstdc++ or winpthread next to them
target_link_options(LyricsOverlay PRIVATE -static -s)
target_link_options(lyrics PRIVATE -static -s -municode)

# dist/ is what a user downloads: the cli, the overlay and the browser extension
set(LYRICS_DIST ${CMAKE_BINARY_DIR}/dist)
add_custom_target(dist ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory ${LYRICS_DIST}
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:lyrics> $<TARGET_FILE:LyricsOverlay> ${LYRICS_DIST}
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/extension ${LYRICS_DIST}/extension
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/README.md ${LYRICS_DIST}
    DEPENDS lyrics LyricsOverlay
)
