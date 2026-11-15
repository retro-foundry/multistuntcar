# Multi Stunt Car

This is a fork of *StuntCarRemake* with two player and reworked physics for uncapped frame rate.

Play: https://retro-foundry.github.io/multistuntcar/

## Current Project Layout

- `src/` - game and platform source code
- `data/` - runtime assets (tracks, sounds, bitmaps, font)
- `build/` - out-of-source native build directory
- `build-web/` - out-of-source Emscripten build directory (recommended)

## Build System

Desktop/native rendering and audio use SDL2 + OpenGL + SDL_ttf.
Web builds use Emscripten SDL ports.

## Native Build (Windows/Linux/macOS)

```bash
cmake -S . -B build
cmake --build build
```

Windows Release:

```powershell
cmake -S . -B build
cmake --build build --config Release
```

## Web Build (Emscripten)

```bash
emcmake cmake -S . -B build-web
cmake --build build-web
```

## Web multiplayer

The web build supports 2-player over WebRTC: one tab hosts (runs the game and streams the canvas), the other joins as guest (watches the stream and sends controls as player 2). A signaling server is required.

To deploy your own signaling server on **Cloudflare Workers** (Durable Objects), see **[webrtc/muttistuntcarsignal/README.md](webrtc/muttistuntcarsignal/README.md)** for setup, `wrangler` commands, and how to point the game at your worker URL.

## Browser controller setup

Open **Controller setup** in the top-right overlay. Connect the controller and press a button if it does not appear, then select the device. For each action, click **Detect** and move the joystick or press its button, or choose an axis/button from the list. Assign all five actions, click **Apply controls**, and close the panel to play. The live readout shows the browser's axis values and pressed button numbers.

For an Amiga joystick with a USB/Arduino adapter, bind left/right to steering, up to accelerate, down to brake, and fire to boost. Fire also confirms menus; left/right select tracks or the player mode. Controls are stored per device ID in this browser, including for remote multiplayer guests. **Reset and reload** removes that device's custom controls and restores the original mappings. Gamepad input is suspended while the panel is open.

This supports adapters the browser exposes through the Gamepad API, even when SDL has no existing controller mapping. A device working in a browser gamepad tester confirms browser detection; it does not establish that SDL recognises its layout. No adapter-specific layout is assumed.

Run the binding regression checks with `node --test tools/test_controller_config.cjs`.

## Desktop controller setup

On Windows, Linux and macOS, press **F3** from the track menu or track preview to open **Controller setup**. The screen lists SDL joysticks, including USB/Arduino adapters without a known controller mapping, and shows their live axes, hats and pressed buttons.

Use keyboard **Up/Down** to select a device, then **Enter** to configure it. Follow the five prompts: steer left, steer right, accelerate/up, brake/down, then boost/fire. Release each control before using the next one. Directions can be axes, hat directions or buttons; no particular adapter layout is assumed. Completing the fire binding saves and applies the mapping immediately. **Escape** cancels an unfinished setup; **Escape** or **F3** closes the screen. The game ignores driving/menu controls while setup is open.

Mappings are stored per SDL device GUID in `controllers.txt` in SDL's per-user `RetroFoundry` / `MultiStuntCar` app preferences directory, and loaded on startup. On Windows this is `%APPDATA%\RetroFoundry\MultiStuntCar\controllers.txt`. Reconfigure a device to replace its saved bindings. Standard gamepads keep their existing controls until explicitly configured; `SDL_GAMECONTROLLERCONFIG` remains available for external SDL mappings.

The native regression tests use SDL virtual joysticks and do not require a physical controller:

```bash
cmake -S . -B build -DSTUNT_BUILD_TESTS=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

## TNT (YNY) Track Extraction Summary

The hidden TNT 8-track pack is extracted from the `reference/SCR-TNT` binary with `tools/extract_tnt_tracks.py`.

- The script validates the source fingerprint (`sha1=f6fd44dc425e367b3ce3c6af18cf07f2d7a50d7b`) unless `--force` is used.
- It finds the embedded `piece.data.offsets` table by signature, then decodes track data using the original Amiga `set.road.data1` flow from the disassembly.
- Hidden raw track blocks are decoded from these starts (hex): `0x03BE, 0x04BE, 0x0563, 0x067C, 0x074F, 0x0807, 0x0926, 0x09F3`.
- Compressed section data is expanded into the port’s standard `804`-byte track format:
  - section count + start section
  - `100` x/z entries
  - `100` angle/template entries
  - `100` left y-id entries
  - `100` right y-id entries
  - `100` left overall y-shifts (word values)
  - `100` right overall y-shifts (word values)
  - standard/super boost bytes
- Output files are written to `data/Tracks/TNT/` in fixed order:
  - `DizzyDescent`, `WittyWay`, `CrazyCaper`, `AmazingAdept`
  - `JerkilyJump`, `EvillyEpisode`, `TeasingTemper`, `RatRace`
- A manifest is written to `data/Tracks/TNT/manifest.json` with source hash, block offsets, and per-track SHA1/SHA256.

## Original Track Pack

The Original pack currently contains `Skyline Spiral`, generated by `tools/generate_original_tracks.py`.

- Output is written to `data/Tracks/Original/SkylineSpiral.bin`.
- The route is a 46-piece loop with orange/white side panels, an outside launch ramp, right-side stutters, elevated inner switchbacks, and a staged descent back to the start.
- Regenerate it with:

```bash
python tools/generate_original_tracks.py
```

## Notes

- Original project: http://sourceforge.net/projects/stuntcarremake/
- Forked from: https://github.com/ptitSeb/stuntcarremake
- Some original sound-loading code came from Forsaken/ProjectX port work by chino.
