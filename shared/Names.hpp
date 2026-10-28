#pragma once

// what the cli and the dll have to agree on
namespace lyrics::shared {

inline constexpr wchar_t kGameExecutable[] = L"Minecraft.Windows.exe";
inline constexpr wchar_t kDllName[] = L"LyricsOverlay.dll";
// the dll's export the cli starts as a remote thread to take the overlay back out
inline constexpr char kUnloadExport[] = "lyrics_unload";
// both sides keep their files under %LOCALAPPDATA%\Lyrics
inline constexpr wchar_t kDataFolder[] = L"Lyrics";
inline constexpr wchar_t kSettingsFile[] = L"settings.ini";

}
