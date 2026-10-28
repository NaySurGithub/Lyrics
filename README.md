# Lyrics 🎤

**Karaoke in your Minecraft world.** Search a song in game, it plays on YouTube,
and every line floats in front of you right when it's sung, typed out in
Minecraft's own font.

## Features

- 🔎 Search any song right in game, no alt-tab
- ⏱️ Lines land on the voice: timed against the YouTube video itself, not a guess
- ⌨️ Each line types itself out, word by word when the video knows the timing
- 🌅 Two modes: lines that float in front of you, or hang in the sky like the sun
- 🪂 Flying with an elytra? The lyrics stay ahead of you instead of spawning behind
- 🎬 Play anything on YouTube yourself, the lyrics follow along
- 🌐 Works with Microsoft Edge and Google Chrome
- 🧹 One exe, one dll, nothing installed - `lyrics uninject` and it's gone

## Install

1. Download `Lyrics-x.y.z-windows-x64.zip` from the releases and unzip it.
2. Add the browser extension to Edge or Chrome:
   - **Edge**: open `edge://extensions`, turn on **Developer mode** (bottom left),
     click **Load unpacked** and pick the `extension` folder
   - **Chrome**: open `chrome://extensions`, turn on **Developer mode** (top right),
     click **Load unpacked** and pick the `extension` folder

   Not your default browser? `lyrics config set browser edge` (or `chrome`) and
   songs open there.
3. Start Minecraft, join a world, and run `lyrics inject`.
4. Press **Right Shift**, type a song, click it. Done!

## Config

Change anything with `lyrics config set <key> <value>`, the game picks it up
live. Everything is saved in `%LOCALAPPDATA%\Lyrics\settings.ini`.

| Option | Default | What it does |
| --- | --- | --- |
| `mode` | `normal` | `normal`: each line floats in front of you and stays put. `sky`: lines hang in the sky like the sun. |
| `distance` | `18` | How far in front of you the lyrics float, in blocks. |
| `height` | `2.5` | How high above your eyes they float, in blocks. |
| `sky_angle` | `25` | How high above the horizon they hang in `sky` mode, in degrees. |
| `scale` | `1` | Size of the text. |
| `time_offset` | `120` | Shifts the lyrics earlier (positive) or later (negative), in ms. |
| `text_color` | `#FFEDCCFF` | Colour of the text, `#RRGGBB` or `#RRGGBBAA`. |
| `typewriter` | `true` | Types each line out letter by letter. |
| `shadow` | `false` | Minecraft's drop shadow under the text. |
| `elytra_lead` | `true` | Puts the lyrics ahead along your flight path while gliding fast. |
| `menu_key` | `RightShift` | The key that opens the song search, e.g. `F8`, `Insert`, `L`. |
| `browser` | `default` | Where songs open: `default`, `edge` or `chrome`. Pick the one the extension is in. |

## Commands

| Command | What |
| --- | --- |
| `lyrics inject` | Loads the overlay into the running game |
| `lyrics uninject` | Takes it back out |
| `lyrics status` | Is the game running, is the overlay in |
| `lyrics config` | Lists every setting and its value |
| `lyrics config set <key> <value>` | Changes one |
| `lyrics config reset` | Back to the defaults |
| `lyrics extension` | Where the browser extension is and how to add it |

## Where the lyrics come from

- 🔎 Songs: Deezer's public search
- 📜 Synced lyrics: [LRCLIB](https://lrclib.net), cached on your disk
- 🎬 Timing: the extension tells the overlay where the video is, and the
  video's own captions line the lyrics up when it has some

The extension only ever talks to `127.0.0.1:8974`.

## Requirements

Windows 10/11 x64 · Minecraft Bedrock 26.60 · Microsoft Edge or Google Chrome

On any other game version the overlay refuses to hook and does nothing.

## Building

```bash
cmake --preset mingw          # from macOS / Linux with mingw-w64
cmake --build --preset mingw
```

On Windows, use the `windows` preset from an MSYS2 MINGW64 shell. Everything
you need to ship lands in `build/<preset>/dist`.

Built with [Dear ImGui](https://github.com/ocornut/imgui),
[stb_image](https://github.com/nothings/stb) and
[SafetyHook](https://github.com/cursey/safetyhook).

MIT license - do whatever you want with it.
