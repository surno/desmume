# DeSmuME Architecture (Reorganized)

This document describes the new directory layout, grouping modules by role and
platform abstraction for maintainability and clarity.

## Core Emulation Engine
- src/core/cpu/ — ARM7/ARM9 CPU, CP15, instruction tables, JIT glue
- src/core/memory/ — MMU, timings, memory headers
- src/core/graphics/ — GPU2D/3D, rasterizer, render3D, texcache, matrix math
- src/core/audio/ — SPU, MetaSPU (time-stretching, outputs)
- src/core/io/ — Input/Output devices (slot1/2, WIFI, mic, rtc)
- src/core/storage/ — Firmware, saves, memory card
- src/core/system/ — NDSSystem, BIOS, registers

## Platform Abstraction
- src/platform/opengl/ — OpenGL render backends
- src/platform/simd/ — SIMD-optimized GPU operations (SSE2/AVX2/NEON/AltiVec)
- src/platform/sdl/ — SDL audio/input/window glue (and GL context helper)
- src/platform/threading/libretro-common/rthreads/ — Threading primitives

## Frontends
- src/frontend/api/ — Public API
  - interface/ — C++ interface API (+ SDL window draw)
  - c_api/ — C API (if/when present)
- src/frontend/cli/posix/ — POSIX CLI frontend
- src/frontend/gui/ — GUI frontends
  - cocoa/ — macOS
  - windows/ — Windows
  - gtk, gtk2 — GTK UIs (if present)

## Utilities & Tools
- src/utils/common/ — Common facilities (paths, version, tasks, etc.)
- src/utils/file/ — File I/O utilities (emufile, readwrite, ROMReader)
- src/utils/debug/ — Debug tools and gdbstub
- src/utils/cheats/ — Cheat system
- src/utils/movie/ — Movie subsystem
- src/utils/database/ — Game database

## Graphics Filters
- src/graphics/filters/ — xBRZ, hq2x/hq3x/hq4x, scanline, etc.

## Addons
- src/addons/ — Slot addons for expansion/retail media

## Build Systems
- Meson: src/frontend/api/interface/meson.build (library: desmume)
- Autotools: src/frontend/posix/configure.ac, Makefile.am
- CMake: top-level CMakeLists.txt builds desmume-core and desmume-interface
- Windows: src/frontend/interface/windows/DeSmuME_Interface.vcxproj

## Quick Build Scripts
- tools/build/build-meson.sh
- tools/build/build-autotools.sh
- tools/build/build-cmake.sh

## Notes
- SDL usage is isolated under src/platform/sdl/ and consumed via frontends.
- All includes updated to module-rooted paths (e.g., core/…, platform/…).
- OpenGL backends are under platform/opengl/; SIMD ops under platform/simd/.
