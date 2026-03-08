# stuntcarremake

[![stuntcarremake Linux build status](https://travis-ci.org/ptitSeb/stuntcarremake.svg?branch=master)](https://travis-ci.org/ptitSeb/stuntcarremake "stuntcarremake Linux build status") [![stuntcarremake Windows build status](https://ci.appveyor.com/api/projects/status/3b9bd69a4vsy0eu6/branch/master?svg=true)](https://ci.appveyor.com/project/ptitSeb/stuntcarremake/branch/master "stuntcarremake Windows Build status")

This is a port to Linux & OpenPandora of Stunt Car Racer Remake, a windows remake of the old Stunt Car Racer from the AtariST/Amiga time.

## Building

This project now uses CMake and an out-of-source build in `build/`.
Rendering is SDL2 + OpenGL on desktop and web (Emscripten), with SDL audio.

### Desktop (Linux/Windows/macOS with SDL2)

```bash
cmake -S . -B build
cmake --build build -j
```

Requirements:
- SDL2
- SDL2_ttf
- OpenGL development headers/libraries

### Windows (Visual Studio Generator)

```powershell
cmake -S . -B build
cmake --build build --config Release
```

You can play Emscripten version, built using [gl4es](https://github.com/ptitSeb/gl4es) here: [Web version](http://ptitseb.github.io/stuntcarremake/)

Some of the original sound-loading code came from Forsaken/ProjectX port work by chino.

## Display Features 

The SDL desktop build supports:

### Dynamic Window Resizing
- **Resizable window** with automatic aspect ratio preservation (16:10 for widescreen, 4:3 for standard)
- **Dynamic font scaling** - fonts and UI text scale based on window size
- **Dynamic cockpit scaling** - cockpit overlay scales proportionally with window resolution
- **Intelligent positioning** - UI elements maintain proper positioning at any resolution

**Technical Details:**
- Base resolutions: 800x480 (widescreen) or 640x480 (standard)
- All scaling factors calculated dynamically from current vs base resolution

**Contributors:** Dynamic scaling enhancements by omenoid <akaunist@gmail.com> (2025-11-30)

## Controls

Controls are:
On Linux / Windows
 4 Arrows for Turning / Accelerate / Brake
 Space for Boost

On Pandora
 DPad Left/Right for turning
 (X) Accelerate
 (B) Brake
 (R) Boost

[![Play on Youtube](https://img.youtube.com/vi/qKTFntQtG6E/0.jpg)](https://www.youtube.com/watch?v=qKTFntQtG6E)

Here is a video on StuntCarRemake running on the OpenPandora

Original project is here: http://sourceforge.net/projects/stuntcarremake/
